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
    if (!m_enabled) {
        if (m_layer) {
            if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
            if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
        }
        m_hold1 = m_hold2 = false;
        updateSafeModeLock();
    }
    refreshOverlay();
}

void BotController::setSafeMode(bool v) {
    if (m_safeMode == v) return;
    m_safeMode = v;
    updateSafeModeLock();
    refreshOverlay();
}

void BotController::updateSafeModeLock() {
    if (!m_layer) return;
    if (safeModeActive()) {
        if (!m_practiceBackupValid) {
            m_practiceBackup = m_layer->m_isPracticeMode;
            m_practiceBackupValid = true;
        }
        m_layer->m_isPracticeMode = true;
    } else if (m_practiceBackupValid) {
        m_layer->m_isPracticeMode = m_practiceBackup;
        m_practiceBackupValid = false;
    }
}

void BotController::resetAttempts() {
    if (!m_layer) return;
    if (m_layer->m_attempts != m_attemptSnapshot) {
        m_layer->m_attempts = m_attemptSnapshot;
    }
}

void BotController::onUpdate(PlayLayer* playLayer, float dt) {
    if (!playLayer) return;

    if (m_layer != playLayer) {
        if (m_enabled) {
            m_hold1 = m_hold2 = false;
        }
        m_practiceBackupValid = false;
        m_overlay = nullptr;
        m_botLabel = nullptr;
        m_safeLabel = nullptr;
        m_layer = playLayer;
        m_attemptSnapshot = playLayer->m_attempts;
        m_p1Prev = PlayerFrame{};
        m_p2Prev = PlayerFrame{};
        m_cal1.reset();
        m_cal2.reset();

        bool master = Mod::get()->getSettingValue<bool>("enabled");
        bool autoStart = Mod::get()->getSettingValue<bool>("auto-enable-at-start");
        m_enabled = master && autoStart;
        updateSafeModeLock();
    }

    m_showGUI = Mod::get()->getSettingValue<bool>("show-gui");

    if (safeModeActive()) {
        resetAttempts();
        updateSafeModeLock();
    }

    if (m_showGUI && !m_overlay) {
        buildOverlay();
    } else if (!m_showGUI && m_overlay) {
        destroyOverlay();
    }

    if (m_enabled) {
        float lookahead = static_cast<float>(Mod::get()->getSettingValue<int64_t>("lookahead-blocks"));
        if (!m_engine) m_engine = new DecisionEngine(lookahead);
        m_engine->setLookahead(lookahead);

        bool dual = playLayer->m_gameState.m_isDualMode && playLayer->m_player2;
        if (dual) {
            tickUnified(playLayer, playLayer->m_player1, playLayer->m_player2, lookahead);
        } else {
            tickPlayer(playLayer, playLayer->m_player1, lookahead);
        }
    }

    refreshOverlay();
}

void BotController::onResetLevel(PlayLayer*) {
    m_hold1 = m_hold2 = false;
    m_p1Prev = PlayerFrame{};
    m_p2Prev = PlayerFrame{};
}

void BotController::onCompleteLevel() {
    if (m_layer) {
        if (m_hold1) m_layer->m_player1->releaseButton(PlayerButton::Jump);
        if (m_hold2 && m_layer->m_player2) m_layer->m_player2->releaseButton(PlayerButton::Jump);
    }
    m_hold1 = m_hold2 = false;
}

void BotController::onLeaveLevel() {
    m_layer = nullptr;
    m_overlay = nullptr;
    m_botLabel = nullptr;
    m_safeLabel = nullptr;
    m_hold1 = m_hold2 = false;
    m_practiceBackupValid = false;
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
            case GameObjectType::Breakable:
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

void BotController::applyInput(PlayerObject* player, bool hold, bool* wasHolding) {
    if (!player) return;
    if (player->m_isDead) {
        if (*wasHolding) {
            player->releaseButton(PlayerButton::Jump);
            *wasHolding = false;
        }
        return;
    }
    if (hold && !*wasHolding) {
        player->pushButton(PlayerButton::Jump);
        *wasHolding = true;
    } else if (!hold && *wasHolding) {
        player->releaseButton(PlayerButton::Jump);
        *wasHolding = false;
    }
}

void BotController::calibrate(PlayerFrame const& cur, PlayerFrame& prev, Calibration& cal) {
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
        applyInput(player, false, &m_hold1);
        m_p1Prev = cur;
        return;
    }

    float xMin = cur.x - 3.0f * UNIT_PER_BLOCK;
    float xMax = cur.x + (lookahead + 2.0f) * UNIT_PER_BLOCK;
    m_cells1 = scan(pl, xMin, xMax);

    bool wantHold = m_engine->decide(cur, m_cells1);
    applyInput(player, wantHold, &m_hold1);
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
        applyInput(p1, false, &m_hold1);
        if (p2) applyInput(p2, false, &m_hold2);
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

    bool options[2] = {false, true};
    bool bestBoth = false;
    bool bestAction = false;
    float bestProgress = -1.0f;
    for (bool o : options) {
        SimResult r1 = simulate(s1, m_cells1, lookahead, o);
        SimResult r2 = p2 ? simulate(s2, m_cells2, lookahead, o) : r1;
        bool ok = !r1.died && !r2.died;
        float prog = std::min(r1.progressX, r2.progressX);
        if (ok && prog > bestProgress) {
            bestBoth = true;
            bestAction = o;
            bestProgress = prog;
        } else if (!bestBoth && prog > bestProgress) {
            bestAction = o;
            bestProgress = prog;
        }
    }

    applyInput(p1, bestAction, &m_hold1);
    if (p2) applyInput(p2, bestAction, &m_hold2);
    m_p1Prev = s1;
    if (p2) m_p2Prev = s2;
}

void BotController::buildOverlay() {
    if (!m_layer || m_overlay) return;

    auto overlay = CCLayerColor::create(ccc4(0, 0, 0, 115));
    overlay->setContentSize(CCSize{170.0f, 62.0f});
    overlay->setAnchorPoint({0.5f, 0.5f});
    overlay->setPosition({340.0f, 30.0f});
    overlay->setZOrder(1000);

    m_botLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_safeLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_botLabel->setScale(0.5f);
    m_safeLabel->setScale(0.5f);

    auto itemBot = CCMenuItemLabel::create(m_botLabel, this, menu_selector(BotController::onToggleBot));
    auto itemSafe = CCMenuItemLabel::create(m_safeLabel, this, menu_selector(BotController::onToggleSafe));
    itemBot->setPosition({85.0f, 37.0f});
    itemSafe->setPosition({85.0f, 25.0f});

    auto menu = CCMenu::create();
    menu->setPosition(0.0f, 0.0f);
    menu->addChild(itemBot);
    menu->addChild(itemSafe);
    overlay->addChild(menu);

    m_overlay = overlay;
    m_layer->addChild(overlay);
    refreshOverlay();
}

void BotController::refreshOverlay() {
    if (!m_overlay || !m_botLabel || !m_safeLabel) return;
    m_botLabel->setString(m_enabled ? "BOT ON" : "BOT OFF");
    m_safeLabel->setString(safeModeActive() ? "SAFE ON" : "SAFE OFF");
}

void BotController::destroyOverlay() {
    if (!m_overlay) return;
    m_overlay->removeFromParent();
    m_overlay = nullptr;
    m_botLabel = nullptr;
    m_safeLabel = nullptr;
}

void BotController::onToggleBot(CCObject*) {
    setEnabled(!m_enabled);
}

void BotController::onToggleSafe(CCObject*) {
    setSafeMode(!m_safeMode);
}

} // namespace bot