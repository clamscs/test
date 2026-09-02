#include "Bot.hpp"
#include "Physics.hpp"
#include "Strategy.hpp"

#include <algorithm>
#include <cmath>

namespace bot {

static constexpr float GAME_HEIGHT = 580.0f;
static constexpr float PLAN_MARGIN = 4.0f * UNIT_PER_BLOCK;

BotController& BotController::get() {
    static BotController inst;
    return inst;
}

void BotController::setEnabled(bool v) {
    if (m_enabled == v) return;
    m_enabled = v;
    if (m_enabled) {
        m_hold1 = m_hold2 = false;
        m_lock1 = m_lock2 = 0;
        m_plan.reset();
    } else if (m_layer) {
        if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
        m_hold1 = m_hold2 = false;
        m_lock1 = m_lock2 = 0;
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
    m_plan.reset();
    Mod::get()->setSettingValue<int64_t>("lookahead-blocks", cur);
}

void BotController::syncSettingsFromMod() {
    bool master = Mod::get()->getSettingValue<bool>("enabled");
    m_enabled = master;
    if (!m_enabled) {
        if (m_layer && m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_layer && m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
        m_hold1 = m_hold2 = false;
        m_lock1 = m_lock2 = 0;
    }
    m_safeMode = Mod::get()->getSettingValue<bool>("auto-safe-mode");
    m_lookahead = static_cast<float>(Mod::get()->getSettingValue<int64_t>("lookahead-blocks"));
}

void BotController::applyHold(PlayerObject* player, bool hold, bool* wasHolding, int* lock) {
    if (!player) return;
    if (player->m_isDead) {
        *lock = 0;
        if (*wasHolding) {
            player->releaseButton(PlayerButton::Jump);
            *wasHolding = false;
        }
        return;
    }
    if (*wasHolding == hold) return;
    if (*lock > 0) { --*lock; return; }
    constexpr int CLICK_GAP = 2;
    if (hold) player->pushButton(PlayerButton::Jump);
    else player->releaseButton(PlayerButton::Jump);
    *wasHolding = hold;
    *lock = CLICK_GAP;
}

void BotController::onUpdate(PlayLayer* playLayer, float dt) {
    if (!playLayer) return;

    if (m_layer != playLayer) {
        m_hold1 = m_hold2 = false;
        m_lock1 = m_lock2 = 0;
        m_layer = playLayer;
        m_plan.reset();
        syncSettingsFromMod();
        bool autoStart = Mod::get()->getSettingValue<bool>("auto-enable-at-start");
        if (autoStart) m_enabled = true;
    }

    if (!m_enabled) return;

    if (!m_engine) m_engine = new DecisionEngine(m_lookahead);
    m_engine->setLookahead(m_lookahead);

    ensurePlan(playLayer);

    PlayerObject* p1 = playLayer->m_player1;
    if (!p1) return;

    bool dual = playLayer->m_gameState.m_isDualMode && playLayer->m_player2;
    bool hold = m_plan.valid ? m_plan.holdAt(p1->getPositionX()) : false;

    applyHold(p1, hold, &m_hold1, &m_lock1);
    if (dual) {
        applyHold(playLayer->m_player2, hold, &m_hold2, &m_lock2);
    } else if (m_hold2 && playLayer->m_player2) {
        applyHold(playLayer->m_player2, false, &m_hold2, &m_lock2);
    }
}

void BotController::onResetLevel(PlayLayer*) {
    m_hold1 = m_hold2 = false;
    m_lock1 = m_lock2 = 0;
    m_plan.reset();
}

bool BotController::onCompleteLevel() {
    if (m_layer) {
        if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
    }
    m_hold1 = m_hold2 = false;
    m_lock1 = m_lock2 = 0;
    m_plan.reset();
    return m_enabled && m_safeMode;
}

void BotController::onLeaveLevel() {
    m_layer = nullptr;
    m_hold1 = m_hold2 = false;
    m_lock1 = m_lock2 = 0;
    m_plan.reset();
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
            case GameObjectType::Breakable:
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
            case GameObjectType::YellowJumpPad:
            case GameObjectType::PinkJumpPad:
            case GameObjectType::GravityPad:
            case GameObjectType::RedJumpPad:
            case GameObjectType::SpiderPad:
                cell.kind = CellKind::Pad;
                switch (type) {
                    case GameObjectType::YellowJumpPad: cell.orbPadType = OrbPadType::Yellow; break;
                    case GameObjectType::PinkJumpPad: cell.orbPadType = OrbPadType::Pink; break;
                    case GameObjectType::GravityPad: cell.orbPadType = OrbPadType::Gravity; break;
                    case GameObjectType::RedJumpPad: cell.orbPadType = OrbPadType::Red; break;
                    case GameObjectType::SpiderPad: cell.orbPadType = OrbPadType::Spider; break;
                    default: break;
                }
                break;
            case GameObjectType::YellowJumpRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::DashRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::TeleportOrb:
                cell.kind = CellKind::Orb;
                switch (type) {
                    case GameObjectType::YellowJumpRing: cell.orbPadType = OrbPadType::Yellow; break;
                    case GameObjectType::PinkJumpRing: cell.orbPadType = OrbPadType::Pink; break;
                    case GameObjectType::GravityRing: cell.orbPadType = OrbPadType::Gravity; break;
                    case GameObjectType::GreenRing: cell.orbPadType = OrbPadType::Green; break;
                    case GameObjectType::RedJumpRing: cell.orbPadType = OrbPadType::Red; break;
                    case GameObjectType::DashRing: cell.orbPadType = OrbPadType::Dash; break;
                    case GameObjectType::SpiderOrb: cell.orbPadType = OrbPadType::Spider; break;
                    case GameObjectType::TeleportOrb: cell.orbPadType = OrbPadType::None; break;
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

bool BotController::ensurePlan(PlayLayer* pl) {
    if (!m_engine) return false;
    PlayerObject* p1 = pl->m_player1;
    if (!p1 || p1->m_isDead) return false;

    PlayerFrame f = snapshot(p1);
    if (!m_plan.valid || f.x >= m_plan.xEnd - PLAN_MARGIN) {
        float toX = f.x + (m_lookahead * 3.0f + 2.0f) * UNIT_PER_BLOCK;
        buildPlanWindow(pl, f, toX);
    }
    return m_plan.valid;
}

void BotController::buildPlanWindow(PlayLayer* pl, PlayerFrame const& start, float toX) {
    m_plan.reset();
    m_cells = scan(pl, start.x - 3.0f * UNIT_PER_BLOCK, toX + 2.0f * UNIT_PER_BLOCK);

    Calibration cal;
    cal.speedX = start.speedX > 0.0f ? start.speedX : BASE_X_SPEED;
    cal.gravity = 2370.0f;
    cal.jumpV = 604.5f;
    cal.fallCap = 903.6f;

    PlayerFrame f = start;
    f.dead = false;

    bool hold = m_hold1;
    m_plan.xStart = start.x;
    m_plan.segments.clear();
    m_plan.segments.push_back({f.x, hold});

    int guard = 0;
    while (f.x < toX && !f.dead && guard < 2400) {
        guard++;
        bool want = m_engine->decide(f, m_cells);
        if (want != hold) {
            hold = want;
            m_plan.segments.push_back({f.x, hold});
        }
        bool died = false;
        PlayerFrame next;
        stepSim(f, hold, cal, m_cells, next, died);
        if (died) {
            f.dead = true;
            break;
        }
        f = next;
    }
    m_plan.xEnd = f.x;
    m_plan.valid = guard > 1 && !f.dead;
}

} // namespace bot
