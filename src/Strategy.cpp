#include "Strategy.hpp"

#include <algorithm>

namespace bot {

DecisionEngine::DecisionEngine(float lookaheadBlocks) : m_lookahead(lookaheadBlocks) {}

bool DecisionEngine::survive(bool hold, PlayerFrame const& s, gd::vector<Cell> const& c, SimResult& out) const {
    out = simulate(s, c, m_lookahead, hold);
    return !out.died;
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
    if (top > 90000.0f) {
        float g = s.vy > 0.0f ? 6.0f : -6.0f;
        return s.y + g;
    }
    return (top + bottom) * 0.5f;
}

bool DecisionEngine::targetAltitude(PlayerFrame const& s, gd::vector<Cell> const& c, float& targetY) const {
    float x0 = s.x + s.halfW();
    float x1 = s.x + (m_lookahead * 0.7f) * UNIT_PER_BLOCK;
    float bestTop = 100000.0f;
    float bestBot = -100000.0f;
    bool found = false;
    for (auto const& cell : c) {
        if (cell.kind != CellKind::Solid && cell.kind != CellKind::Hazard) continue;
        if (cell.rect.getMaxX() <= x0 || cell.rect.getMinX() >= x1) continue;
        if (cell.kind == CellKind::Solid) {
            if (cell.rect.getMinY() < bestTop) bestTop = cell.rect.getMinY();
            if (cell.rect.getMaxY() > bestBot) bestBot = cell.rect.getMaxY();
        } else {
            bestTop = std::min(bestTop, cell.rect.getMaxY());
        }
        found = true;
    }
    if (bestTop > 90000.0f) {
        float g = s.vy > 0.0f ? 6.0f : -6.0f;
        targetY = s.y + g;
        return true;
    }
    targetY = (bestTop + bestBot) * 0.5f;
    return true;
}

bool DecisionEngine::decide(PlayerFrame const& s, gd::vector<Cell> const& cells) {
    SimResult r0, r1;
    bool ok0 = survive(false, s, cells, r0);
    bool ok1 = survive(true, s, cells, r1);

    switch (s.mode) {
        case Mode::Ship: {
            float target;
            if (!targetAltitude(s, cells, target)) {
                return !ok0;
            }
            float err = target - s.y;
            bool needUp = err > 4.0f;
            if (std::abs(err) < 4.0f) return false;
            return s.upsideDown ? !needUp : needUp;
        }

        case Mode::Swing: {
            if (ok0 && ok1) return false;
            if (ok0) return false;
            if (ok1) return true;
            return !(r0.progressX >= r1.progressX);
        }

        case Mode::Wave: {
            if (ok0 && ok1) return false;
            if (ok0) return false;
            return true;
        }

        case Mode::Ball: {
            if (ok0 && ok1) {
                if (s.vy < 0.0f && (s.y < 40.0f)) return true;
                return false;
            }
            if (ok0) return false;
            if (ok1) return true;
            return false;
        }

        case Mode::Spider: {
            if (ok0 && ok1) return false;
            if (ok0) return false;
            return true;
        }

        case Mode::Ufo: {
            if (ok0 && ok1) return false;
            if (ok0) return false;
            return true;
        }

        case Mode::Cube:
        case Mode::Robot:
        default: {
            if (ok0) return false;
            if (ok1) return true;
            if (r0.progressX < 0.0f) return true;
            return false;
        }
    }
}

} // namespace bot