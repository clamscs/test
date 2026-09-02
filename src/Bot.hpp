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
    Cube,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
};

enum class CellKind : uint8_t {
    None,
    Solid,
    Hazard,
    Pad,
    Orb,
    Ring,
    Portal,
};

enum class PortalKind : uint8_t {
    None,
    GravitySwap,
    Mini,
    Normal,
    Dual,
    Solo,
    Teleport,
    Cube,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Speed,
};

struct Cell {
    cocos2d::CCRect rect;
    CellKind kind = CellKind::None;
    uint16_t id = 0;
    PortalKind portal = PortalKind::None;
    float portalParam = 0.0f;
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
    float gravity = 0.0f;
    float jumpV = 0.0f;
    float fallCap = 0.0f;
    float speedX = BASE_X_SPEED;
    bool initialized = false;

    void reset();
    void observe(PlayerFrame const& cur, PlayerFrame const& prev);
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual bool tick(PlayerFrame const& state, gd::vector<Cell> const& cells, float lookahead) = 0;
    virtual Mode mode() const = 0;
};

class DecisionEngine;

class BotController {
public:
    static BotController& get();

    void onUpdate(PlayLayer* playLayer, float dt);
    void onResetLevel(PlayLayer* playLayer);
    void onCompleteLevel();
    void onLeaveLevel();

    void setEnabled(bool v);
    bool enabled() const { return m_enabled; }
    void setSafeMode(bool v);
    bool safeMode() const { return m_safeMode; }
    bool safeModeActive() const { return m_enabled && m_safeMode; }

    PlayLayer* currentLayer() const { return m_layer; }

    void drawGUI(bool v);
    bool shouldDrawGUI() const { return m_showGUI; }

private:
    void resetAttempts();
    void updateSafeModeLock();
    void tickPlayer(PlayLayer* pl, PlayerObject* player, float lookahead);
    void tickUnified(PlayLayer* pl, PlayerObject* p1, PlayerObject* p2, float lookahead);
    PlayerFrame snapshot(PlayerObject* player) const;
    void calibrate(PlayerFrame cur, PlayerFrame& prev, Calibration& cal);
    gd::vector<Cell> scan(PlayLayer* pl, float xMin, float xMax) const;
    void applyInput(PlayerObject* player, bool hold, bool* wasHolding);
    void buildOverlay();
    void refreshOverlay();
    void destroyOverlay();
    bool handleOverlayTouch(cocos2d::CCObject* sender);

    BotController() = default;

    PlayLayer* m_layer = nullptr;
    bool m_enabled = false;
    bool m_safeMode = true;
    bool m_showGUI = true;
    bool m_practiceBackupValid = false;
    bool m_practiceBackup = false;

    int m_attemptSnapshot = 0;

    PlayerFrame m_p1Prev{};
    PlayerFrame m_p2Prev{};
    Calibration m_cal1;
    Calibration m_cal2;

    bool m_hold1 = false;
    bool m_hold2 = false;

    DecisionEngine* m_engine = nullptr;
    gd::vector<Cell> m_cells1;
    gd::vector<Cell> m_cells2;

    cocos2d::CCLayerColor* m_overlay = nullptr;
    cocos2d::CCLabelBMFont* m_botLabel = nullptr;
    cocos2d::CCLabelBMFont* m_safeLabel = nullptr;
};

} // namespace bot