#include "Strategy.hpp"

#include <algorithm>
#include <cmath>

namespace bot {

DecisionEngine::DecisionEngine(float lookaheadBlocks) : m_lookahead(lookaheadBlocks) {}

static Calibration makeCal(PlayerFrame const& s) {
    Calibration cal;
    cal.speedX = s.speedX > 0.0f ? s.speedX : BASE_X_SPEED;
    cal.gravity = 2370.0f;
    cal.jumpV = 604.5f;
    cal.fallCap = 903.6f;
    return cal;
}

float DecisionEngine::corridorCenter(PlayerFrame const& s, gd::vector<Cell> const& c) const {
    float x0 = s.x + s.halfW();
    float x1 = s.x + m_lookahead * UNIT_PER_BLOCK;
    float top = 100000.0f;
    float bottom = -100000.0f;
    for (auto const& cell : c) {
        if (cell.kind != CellKind::Solid && cell.kind != CellKind::Hazard) continue;
        if (cell.rect.getMaxX() <= x0 || cell.rect.getMinX() >= x1) continue;
        if (cell.kind == CellKind::Solid) {
            top = std::min(top, cell.rect.getMinY());
            bottom = std::max(bottom, cell.rect.getMaxY());
        } else {
            top = std::min(top, cell.rect.getMaxY());
            bottom = std::max(bottom, cell.rect.getMaxY() - 30.0f);
        }
    }
    if (top > 90000.0f) return s.y;
    return (top + bottom) * 0.5f;
}

// Score how centered/clear the player is in the vertical corridor defined by
// [bottom, top] and [x0, x1]. Returns >= 0; larger is safer.
float DecisionEngine::clearance(PlayerFrame const& s, gd::vector<Cell> const& c, float top, float bottom) const {
    float hh = s.halfH();
    float bestTop = top;
    float bestBot = bottom;
    float x0 = s.x - s.halfW();
    float x1 = s.x + s.halfW();
    for (auto const& cell : c) {
        if (cell.kind == CellKind::Hazard) {
            if (cell.rect.getMaxX() <= x0 || cell.rect.getMinX() >= x1) continue;
            float hTop = cell.rect.getMaxY();
            if (hTop < bestTop) bestTop = hTop;
            float hBot = cell.rect.getMaxY() - 30.0f;
            if (hBot > bestBot) bestBot = hBot;
        } else if (cell.kind == CellKind::Solid) {
            if (cell.rect.getMaxX() <= x0 || cell.rect.getMinX() >= x1) continue;
            if (cell.rect.getMinY() < bestTop) bestTop = cell.rect.getMinY();
            if (cell.rect.getMaxY() > bestBot) bestBot = cell.rect.getMaxY();
        }
    }
    float clearAbove = s.y + hh;
    float clearBelow = s.y - hh;
    float dAbove = bestTop - clearAbove;
    float dBelow = clearBelow - bestBot;
    if (dAbove < 0.0f || dBelow < 0.0f) return -1.0f;
    return std::min(dAbove, dBelow);
}

// Wrap continuous-control into a simple bool decision (true = hold).
bool DecisionEngine::continuous(PlayerFrame const& s, gd::vector<Cell> const& c, bool preferHold) const {
    float target = corridorCenter(s, c);
    int horizon = static_cast<int>(std::clamp(m_lookahead * 60.0f / 3.0f, 8.0f, 36.0f));

    Calibration cal = makeCal(s);
    auto simBranch = [&](bool first) -> float {
        PlayerFrame cur = s;
        bool firstFrame = true;
        int survival = 0;
        float worst = 1e9f;
        for (int i = 0; i < horizon; i++) {
            bool input = firstFrame ? first : (cur.y < target);
            if (s.upsideDown && !firstFrame) input = !input;
            firstFrame = false;
            PlayerFrame h, r;
            bool hd = false, rd = false;
            stepSim(cur, true, cal, c, h, hd);
            stepSim(cur, false, cal, c, r, rd);
            PlayerFrame next;
            bool died;
            if (input) {
                if (!hd) { next = h; died = false; } else { next = r; died = rd; }
            } else {
                if (!rd) { next = r; died = false; } else { next = h; died = hd; }
            }
            if (died) break;
            cur = next;
            survival++;
            float clr = clearance(cur, c, 1e9f, -1e9f);
            if (clr > 0.0f && clr < worst) worst = clr;
        }
        return survival * 100.0f + (worst < 1e8f ? std::min(worst, 60.0f) : 0.0f);
    };

    float holdScore = simBranch(true);
    float relScore = simBranch(false);
    return preferHold ? (holdScore >= relScore - 10.0f) : (relScore >= holdScore - 10.0f);
}

// Cube/Robot: find the best click delay by scanning, then click now only if delay==0.
// Cube/Robot: find the click delay that maximizes survival; click now if immediate
// clicking is not worse than any delayed click and strictly better than not clicking.
bool DecisionEngine::clickForCube(PlayerFrame const& s, gd::vector<Cell> const& c) const {
    Calibration cal = makeCal(s);
    int horizon = static_cast<int>(std::clamp(m_lookahead * 10.0f, 14.0f, 30.0f));

    auto sim = [&](int delay) -> float {
        PlayerFrame cur = s;
        bool jumped = false;
        int survival = 0;
        for (int i = 0; i < horizon; i++) {
            bool input = false;
            if (!jumped && cur.onGround && i >= delay) {
                input = true;
                jumped = true;
            }
            PlayerFrame n;
            bool died = false;
            stepSim(cur, input, cal, c, n, died);
            if (died) break;
            cur = n;
            survival++;
        }
        return static_cast<float>(survival);
    };

    float noClick = sim(100000);
    float clickNow = sim(0);
    if (clickNow <= noClick) return false;

    float best = clickNow;
    for (int d = 1; d <= 14; d++) {
        float sc = sim(d);
        if (sc > best) best = sc;
    }
    return std::fabs(best - clickNow) < 0.5f;
}

bool DecisionEngine::decide(PlayerFrame const& s, gd::vector<Cell> const& cells) {
    switch (s.mode) {
        case Mode::Ship:
        case Mode::Swing:
        case Mode::Wave:
        case Mode::Ufo:
            return continuous(s, cells, false);

        case Mode::Spider:
            // Spider teleports on click; only click when needed to survive.
            return continuous(s, cells, true);

        case Mode::Ball:
            return continuous(s, cells, false);

        case Mode::Cube:
        case Mode::Robot:
        default:
            return clickForCube(s, cells);
    }
}

} // namespace bot
