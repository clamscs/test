#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <cstdint>

using namespace geode::prelude;

namespace bot {

constexpr float DT = 1.0f / 60.0f;
constexpr float UNIT_PER_BLOCK = 30.0f;
constexpr float BASE_X_SPEED = 10.386f * UNIT_PER_BLOCK;

enum class Mode : uint8_t {
    Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing,
};

enum class CellKind : uint8_t {
    None, Solid, Hazard, Pad, Orb, Portal,
};

enum class PortalKind : uint8_t {
    None, GravitySwap, Mini, Normal, Dual, Solo, Teleport,
    Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing,
};

enum class OrbPadType : uint8_t {
    None,
    Yellow, Pink, Red, Green, Gravity, Dash, Spider,
};

struct Cell {
    cocos2d::CCRect rect;
    CellKind kind = CellKind::None;
    OrbPadType orbPadType = OrbPadType::None;
    uint16_t id = 0;
    PortalKind portal = PortalKind::None;
    bool isPad() const { return kind == CellKind::Pad; }
    bool isOrb() const { return kind == CellKind::Orb; }
};

struct PlayerFrame {
    float x = 0.0f;
    float y = 0.0f;
    float vy = 0.0f;
    float speedX = BASE_X_SPEED;
    float scale = 1.0f;
    bool onGround = false;
    bool upsideDown = false;
    bool dead = false;
    Mode mode = Mode::Cube;
    bool mini() const { return scale < 0.8f; }
    float halfW() const { return 15.0f * scale; }
    float halfH() const { return 15.0f * scale; }
};

struct Calibration {
    float gravity = 2370.0f;
    float jumpV = 604.5f;
    float fallCap = 903.6f;
    float speedX = BASE_X_SPEED;
    bool initialized = false;
    void reset();
};

struct PlanSegment {
    float xStart;
    bool hold;
};

struct Plan {
    gd::vector<PlanSegment> segments;
    float xStart = 0.0f;
    float xEnd = 0.0f;
    bool valid = false;

    void reset() { segments.clear(); xStart = 0.0f; xEnd = 0.0f; valid = false; }

    bool holdAt(float x) const {
        if (segments.empty()) return false;
        int lo = 0, hi = static_cast<int>(segments.size()) - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (segments[mid].xStart <= x) lo = mid;
            else hi = mid - 1;
        }
        return segments[lo].hold;
    }
};

class DecisionEngine;

class BotController : public cocos2d::CCObject {
public:
    static BotController& get();

    void onUpdate(PlayLayer* playLayer, float dt);
    void onResetLevel(PlayLayer* playLayer);
    bool onCompleteLevel();
    void onLeaveLevel();

    void setEnabled(bool v);
    bool enabled() const { return m_enabled; }
    void setSafeMode(bool v);
    bool safeMode() const { return m_safeMode; }
    bool safeModeActive() const { return m_enabled && m_safeMode; }
    void toggleEnabled();
    void toggleSafeMode();
    void cycleLookahead();
    float lookaheadBlocks() const { return m_lookahead; }

    PlayLayer* currentLayer() const { return m_layer; }
    void syncSettingsFromMod();

private:
    gd::vector<Cell> scan(PlayLayer* pl, float xMin, float xMax) const;
    PlayerFrame snapshot(PlayerObject* player) const;
    bool ensurePlan(PlayLayer* pl);
    void buildPlanWindow(PlayLayer* pl, PlayerFrame const& start, float toX);
    void applyHold(PlayerObject* player, bool hold, bool* wasHolding, int* lock);

    BotController() = default;

    PlayLayer* m_layer = nullptr;
    bool m_enabled = false;
    bool m_safeMode = true;
    float m_lookahead = 5.0f;

    bool m_hold1 = false;
    bool m_hold2 = false;
    int m_lock1 = 0;
    int m_lock2 = 0;

    Plan m_plan;
    gd::vector<Cell> m_cells;
    DecisionEngine* m_engine = nullptr;
};

} // namespace bot
