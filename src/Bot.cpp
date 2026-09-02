#include "Bot.hpp"
#include "Physics.hpp"
#include "Strategy.hpp"

#include <algorithm>
#include <cmath>

namespace bot {

static constexpr float GAME_HEIGHT = 580.0f;

BotController& BotController::get() {
    static BotController inst;
    return inst;
}

void BotController::setEnabled(bool v) {
    if (m_enabled == v) return;
    m_enabled = v;
    if (m_enabled) {
        m_hold1 = m_hold2 = false;
    } else if (m_layer) {
        if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
        m_hold1 = m_hold2 = false;
        m_clickLock1 = m_clickLock2 = 0;
    }
}

void BotController::setSafeMode(bool v) {
    if (m_safeMode == v) return;
    m_safeMode = v;
}

void BotController::toggleEnabled() {
    bool next = !m_enabled;
    setEnabled(next);
    Mod::get()->setSettingValue<bool>("enabled", next);
}

void BotController::toggleSafeMode() {
    bool next = !m_safeMode;
    setSafeMode(next);
    Mod::get()->setSettingValue<bool>("auto-safe-mode", next);
}

void BotController::cycleLookahead() {
    int cur = static_cast<int>(m_lookahead);
    cur += 1;
    if (cur > 10) cur = 2;
    m_lookahead = static_cast<float>(cur);
    Mod::get()->setSettingValue<int64_t>("lookahead-blocks", cur);
}

float BotController::lookaheadBlocks() const {
    return m_lookahead;
}

void BotController::syncSettingsFromMod() {
    bool master = Mod::get()->getSettingValue<bool>("enabled");
    m_enabled = master;
    if (!m_enabled) {
        if (m_layer && m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_layer && m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
        m_hold1 = m_hold2 = false;
    }
    m_safeMode = Mod::get()->getSettingValue<bool>("auto-safe-mode");
    m_lookahead = static_cast<float>(Mod::get()->getSettingValue<int64_t>("lookahead-blocks"));
}

void BotController::onUpdate(PlayLayer* playLayer, float dt) {
    if (!playLayer) return;

    if (m_layer != playLayer) {
        m_hold1 = m_hold2 = false;
        m_clickLock1 = m_clickLock2 = 0;
        m_layer = playLayer;
        m_p1Prev = PlayerFrame{};
        m_p2Prev = PlayerFrame{};
        m_cal1.reset();
        m_cal2.reset();
        syncSettingsFromMod();
        bool autoStart = Mod::get()->getSettingValue<bool>("auto-enable-at-start");
        if (autoStart) m_enabled = true;
    }

    if (m_enabled) {
        if (!m_engine) m_engine = new DecisionEngine(m_lookahead);
        m_engine->setLookahead(m_lookahead);

        bool dual = playLayer->m_gameState.m_isDualMode && playLayer->m_player2;
        if (dual) {
            tickUnified(playLayer, playLayer->m_player1, playLayer->m_player2, m_lookahead);
        } else {
            tickPlayer(playLayer, playLayer->m_player1, m_lookahead);
        }
    }
}

void BotController::onResetLevel(PlayLayer*) {
    m_hold1 = m_hold2 = false;
    m_clickLock1 = m_clickLock2 = 0;
    m_p1Prev = PlayerFrame{};
    m_p2Prev = PlayerFrame{};
}

bool BotController::onCompleteLevel() {
    if (m_layer) {
        if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
    }
    m_hold1 = m_hold2 = false;
    m_clickLock1 = m_clickLock2 = 0;
    // When safe mode is on, veto the save so no progress/new best/stars are stored.
    return m_enabled && m_safeMode;
}

void BotController::onLeaveLevel() {
    m_layer = nullptr;
    m_hold1 = m_hold2 = false;
    m_clickLock1 = m_clickLock2 = 0;
}

PlayerFrame BotController::snapshot(PlayerObject* player) const {
    PlayerFrame f;
    auto pos = player->getPosition();
    f.x = pos.x;
    f.y = pos.y;
    f.vy = static_cast<float>(player->getYVelocity());
    f.onGround = player->m_isOnGround;
    f.upsideDown = player->m_isUpsideDown;
    f.dead = player->m_isDead;
    f.scale = player->getScale();
    if (f.scale <= 0.0f) f.scale = 1.0f;
    pickMode(f, player);
    return f;
}

gd::vector<Cell> BotController::scan(PlayLayer* pl, float xMin, float xMax) const {
    gd::vector<Cell> out;
    out.reserve(256);
    auto const& objects = pl->m_objects;
    if (!objects) return out;

    for (auto obj : CCArrayExt<GameObject*>(objects)) {
        if (!obj) continue;
        auto type = obj->getType();
        Cell cell;
        switch (type) {
            case GameObjectType::Solid:
            case GameObjectType::Slope:
                cell.kind = CellKind::Solid;
                break;
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
                cell.kind = CellKind::Hazard;
                break;
            case GameObjectType::InverseGravityPortal:
            case GameObjectType::NormalGravityPortal:
            case GameObjectType::MiniSizePortal:
            case GameObjectType::RegularSizePortal:
            case GameObjectType::ShipPortal:
            case GameObjectType::CubePortal:
            case GameObjectType::BallPortal:
            case GameObjectType::UfoPortal:
            case GameObjectType::WavePortal:
            case GameObjectType::RobotPortal:
            case GameObjectType::SpiderPortal:
            case GameObjectType::SwingPortal:
                cell.kind = CellKind::Portal;
                switch (type) {
                    case GameObjectType::InverseGravityPortal:
                    case GameObjectType::NormalGravityPortal: cell.portal = PortalKind::GravitySwap; break;
                    case GameObjectType::MiniSizePortal: cell.portal = PortalKind::Mini; break;
                    case GameObjectType::RegularSizePortal: cell.portal = PortalKind::Normal; break;
                    case GameObjectType::CubePortal: cell.portal = PortalKind::Cube; break;
                    case GameObjectType::ShipPortal: cell.portal = PortalKind::Ship; break;
                    case GameObjectType::BallPortal: cell.portal = PortalKind::Ball; break;
                    case GameObjectType::UfoPortal: cell.portal = PortalKind::Ufo; break;
                    case GameObjectType::WavePortal: cell.portal = PortalKind::Wave; break;
                    case GameObjectType::RobotPortal: cell.portal = PortalKind::Robot; break;
                    case GameObjectType::SpiderPortal: cell.portal = PortalKind::Spider; break;
                    case GameObjectType::SwingPortal: cell.portal = PortalKind::Swing; break;
                    default: break;
                }
                break;
            default:
                continue;
        }
        cell.id = obj->m_objectID;
        cell.rect = obj->getObjectRect();
        if (std::isnan(cell.rect.getMinX()) || std::isnan(cell.rect.getMinY())) continue;
        if (cell.rect.getMaxX() < xMin || cell.rect.getMinX() > xMax) continue;
        if (cell.rect.getMaxY() < -200.0f || cell.rect.getMinY() > GAME_HEIGHT + 200.0f) continue;
        out.push_back(cell);
        if (out.size() >= 8192) break;
    }
    return out;
}

void BotController::applyInput(PlayerObject* player, bool hold, bool* wasHolding, int& lock) {
    if (!player) return;
    if (player->m_isDead) {
        lock = 0;
        if (*wasHolding) {
            player->releaseButton(PlayerButton::Jump);
            *wasHolding = false;
        }
        return;
    }
    if (*wasHolding == hold) return;
    if (lock > 0) return;
    // Transition with a minimum gap so the click sound plays naturally.
    constexpr int CLICK_GAP = 2;
    if (hold) {
        player->pushButton(PlayerButton::Jump);
        *wasHolding = true;
    } else {
        player->releaseButton(PlayerButton::Jump);
        *wasHolding = false;
    }
    if (lock < CLICK_GAP) lock = CLICK_GAP;
}

void BotController::tickInput(PlayerObject* player, bool hold, bool* wasHolding, int& lock) {
    if (lock > 0) --lock;
    applyInput(player, hold, wasHolding, lock);
}

void BotController::calibrate(PlayerFrame cur, PlayerFrame& prev, Calibration& cal) {
    if (prev.x > 0.0f) {
        float ddx = cur.x - prev.x;
        if (ddx > 0.0f && ddx < 3.0f * UNIT_PER_BLOCK) {
            cur.speedX = ddx / DT;
        } else {
            cur.speedX = cal.speedX;
        }
    } else {
        cur.speedX = cal.speedX > 0.0f ? cal.speedX : BASE_X_SPEED;
    }
    cal.observe(cur, prev);
}

void BotController::tickPlayer(PlayLayer* pl, PlayerObject* player, float lookahead) {
    if (!player) return;

    PlayerFrame cur = snapshot(player);
    calibrate(cur, m_p1Prev, m_cal1);

    if (cur.dead) {
        tickInput(player, false, &m_hold1, m_clickLock1);
        m_p1Prev = cur;
        return;
    }

    float xMin = cur.x - 3.0f * UNIT_PER_BLOCK;
    float xMax = cur.x + (lookahead + 2.0f) * UNIT_PER_BLOCK;
    m_cells1 = scan(pl, xMin, xMax);

    bool wantHold = m_engine->decide(cur, m_cells1);
    tickInput(player, wantHold, &m_hold1, m_clickLock1);
    m_p1Prev = cur;
}

void BotController::tickUnified(PlayLayer* pl, PlayerObject* p1, PlayerObject* p2, float lookahead) {
    PlayerFrame s1 = snapshot(p1);
    calibrate(s1, m_p1Prev, m_cal1);
    PlayerFrame s2 = p2 ? snapshot(p2) : s1;
    if (p2) calibrate(s2, m_p2Prev, m_cal2);

    bool dead1 = s1.dead;
    bool dead2 = p2 ? s2.dead : false;
    if (dead1 || dead2) {
        tickInput(p1, false, &m_hold1, m_clickLock1);
        if (p2) tickInput(p2, false, &m_hold2, m_clickLock2);
        m_p1Prev = s1;
        if (p2) m_p2Prev = s2;
        return;
    }

    float xMin = s1.x - 3.0f * UNIT_PER_BLOCK;
    float xMax = s1.x + (lookahead + 2.0f) * UNIT_PER_BLOCK;
    m_cells1 = scan(pl, xMin, xMax);
    m_cells2 = m_cells1;
    if (p2) {
        xMin = s2.x - 3.0f * UNIT_PER_BLOCK;
        xMax = s2.x + (lookahead + 2.0f) * UNIT_PER_BLOCK;
        m_cells2 = scan(pl, xMin, xMax);
    }

    bool bestAction = false;
    float bestProgress = -1.0f;
    for (bool o : {false, true}) {
        SimResult r1 = simulate(s1, m_cells1, lookahead, o);
        SimResult r2 = p2 ? simulate(s2, m_cells2, lookahead, o) : r1;
        bool ok = !r1.died && !r2.died;
        float prog = ok ? std::min(r1.progressX, r2.progressX) : -1.0f;
        if (prog > bestProgress) {
            bestAction = o;
            bestProgress = prog;
        }
    }

    tickInput(p1, bestAction, &m_hold1, m_clickLock1);
    if (p2) tickInput(p2, bestAction, &m_hold2, m_clickLock2);
    m_p1Prev = s1;
    if (p2) m_p2Prev = s2;
}

} // namespace bot