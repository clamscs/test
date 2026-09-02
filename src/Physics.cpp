#include "Physics.hpp"

#include <algorithm>
#include <cmath>

namespace bot {

void Calibration::reset() {
    gravity = 2370.0f;
    jumpV = 604.5f;
    fallCap = 903.6f;
    speedX = BASE_X_SPEED;
    initialized = false;
}

static bool intersects(cocos2d::CCRect const& a, cocos2d::CCRect const& b) {
    return a.getMaxX() > b.getMinX() && a.getMinX() < b.getMaxX() &&
           a.getMaxY() > b.getMinY() && a.getMinY() < b.getMaxY();
}

void pickMode(PlayerFrame& f, PlayerObject* player) {
    if (player->m_isShip) f.mode = Mode::Ship;
    else if (player->m_isBall) f.mode = Mode::Ball;
    else if (player->m_isBird) f.mode = Mode::Ufo;
    else if (player->m_isDart) f.mode = Mode::Wave;
    else if (player->m_isRobot) f.mode = Mode::Robot;
    else if (player->m_isSpider) f.mode = Mode::Spider;
    else if (player->m_isSwing) f.mode = Mode::Swing;
    else f.mode = Mode::Cube;
}

static PlayerFrame singleStep(PlayerFrame const& f, bool input, Calibration const& cal) {
    PlayerFrame n = f;
    n.speedX = cal.speedX > 0.0f ? cal.speedX : BASE_X_SPEED;

    float g = cal.gravity;
    if (f.mini()) g *= 0.7f;

    float dir = f.upsideDown ? -1.0f : 1.0f;

    switch (f.mode) {
        case Mode::Cube:
        case Mode::Robot:
            if (f.onGround) {
                n.vy = 0.0f;
                n.onGround = false;
                if (input) {
                    n.vy = dir * cal.jumpV * (f.mini() ? 0.78f : 1.0f);
                }
            } else {
                float cap = cal.fallCap;
                float next = n.vy - dir * g * DT;
                if (f.mode == Mode::Robot && input) {
                    next += dir * 3000.0f * DT;
                }
                if (dir > 0.0f && next < -cap) next = -cap;
                if (dir < 0.0f && next > cap) next = cap;
                n.vy = next;
            }
            break;

        case Mode::Ship:
        case Mode::Swing: {
            float gs = g * 0.9f;
            float cap = cal.fallCap * 1.4f;
            float next;
            if (f.mode == Mode::Ship) {
                next = n.vy + (input ? dir * gs * 1.25f : -dir * gs) * DT;
            } else {
                next = n.vy + (input ? -dir * gs * 1.3f : dir * gs * 1.1f) * DT;
            }
            if (dir > 0.0f) {
                if (next < -cap) next = -cap;
                if (next > 660.0f) next = 660.0f;
            } else {
                if (next > cap) next = cap;
                if (next < -660.0f) next = -660.0f;
            }
            n.vy = next;
            break;
        }

        case Mode::Ball:
            if (input) {
                n.upsideDown = !n.upsideDown;
                n.vy = 0.0f;
                n.onGround = false;
                break;
            }
            n.vy = n.vy - dir * g * 0.85f * DT;
            if (dir > 0.0f && n.vy < -cal.fallCap) n.vy = -cal.fallCap;
            if (dir < 0.0f && n.vy > cal.fallCap) n.vy = cal.fallCap;
            break;

        case Mode::Spider: {
            float cap = cal.fallCap;
            float next = n.vy - dir * g * 0.85f * DT;
            if (dir > 0.0f && next < -cap) next = -cap;
            if (dir < 0.0f && next > cap) next = cap;
            n.vy = next;
            if (input) {
                n.vy = dir * 1800.0f;
            }
            break;
        }

        case Mode::Ufo: {
            float cap = cal.fallCap * 0.85f;
            float next = n.vy - dir * g * 1.3f * DT;
            if (dir > 0.0f && next < -cap) next = -cap;
            if (dir < 0.0f && next > cap) next = cap;
            n.vy = next;
            if (input) {
                n.vy = dir * cal.jumpV * 0.6f;
            }
            break;
        }

        case Mode::Wave: {
            float mag = cal.speedX > 0.0f ? cal.speedX : BASE_X_SPEED;
            if (input) n.vy = dir * mag * 0.6f;
            else n.vy = -dir * mag * 0.6f;
            break;
        }
    }

    n.x += n.speedX * DT;
    n.y += n.vy * DT;
    n.onGround = false;
    return n;
}

static bool solidUnder(PlayerFrame const& f, PlayerFrame const& next, gd::vector<Cell> const& cells, bool upsideDown) {
    float hw = next.halfW();
    float hh = next.halfH();
    for (auto const& c : cells) {
        if (c.kind != CellKind::Solid) continue;
        if (c.rect.getMaxX() <= next.x - hw || c.rect.getMinX() >= next.x + hw) continue;
        if (upsideDown) {
            if (next.y + hh >= c.rect.getMinY() && next.y + hh <= c.rect.getMinY() + 50.0f) return true;
        } else {
            if (next.y - hh <= c.rect.getMaxY() && next.y - hh >= c.rect.getMaxY() - 50.0f) return true;
        }
    }
    return false;
}

static bool touchesAny(PlayerFrame const& f, gd::vector<Cell> const& cells, CellKind kind, cocos2d::CCRect const& box) {
    for (auto const& c : cells) {
        if (c.kind != kind) continue;
        if (intersects(box, c.rect)) return true;
    }
    return false;
}

static PlayerFrame place(PlayerFrame const& f, gd::vector<Cell> const& cells, bool& died) {
    PlayerFrame n = f;
    float hw = n.halfW();
    float hh = n.halfH();
    cocos2d::CCRect pb(n.x - hw, n.y - hh, hw * 2.0f, hh * 2.0f);

    if (touchesAny(n, cells, CellKind::Hazard, pb)) {
        died = true;
        return n;
    }

    bool blocked = touchesAny(n, cells, CellKind::Solid, pb);
    if (!blocked) {
        n.onGround = solidUnder(n, n, cells, n.upsideDown);
        return n;
    }

    if (!n.upsideDown) {
        if (f.vy <= 0.0f) {
            for (auto const& c : cells) {
                if (c.kind != CellKind::Solid) continue;
                if (c.rect.getMaxX() > n.x - hw && c.rect.getMinX() < n.x + hw) {
                    if (n.y - hh <= c.rect.getMaxY() && n.y - hh >= c.rect.getMaxY() - 60.0f) {
                        n.y = c.rect.getMaxY() + hh;
                        n.vy = 0.0f;
                        n.onGround = true;
                        return n;
                    }
                }
            }
        }
    } else {
        if (f.vy >= 0.0f) {
            for (auto const& c : cells) {
                if (c.kind != CellKind::Solid) continue;
                if (c.rect.getMaxX() > n.x - hw && c.rect.getMinX() < n.x + hw) {
                    if (n.y + hh >= c.rect.getMinY() && n.y + hh <= c.rect.getMinY() + 60.0f) {
                        n.y = c.rect.getMinY() - hh;
                        n.vy = 0.0f;
                        n.onGround = true;
                        return n;
                    }
                }
            }
        }
    }

    died = true;
    return n;
}

static void applyPortals(PlayerFrame& f, gd::vector<Cell> const& cells, float prevX) {
    for (auto const& c : cells) {
        if (c.kind != CellKind::Portal) continue;
        if (prevX <= c.rect.getMidX() && f.x > c.rect.getMidX()) {
            switch (c.portal) {
                case PortalKind::GravitySwap: f.upsideDown = !f.upsideDown; break;
                case PortalKind::Mini: f.scale = 0.5f; break;
                case PortalKind::Normal: f.scale = 1.0f; break;
                case PortalKind::Cube: f.mode = Mode::Cube; break;
                case PortalKind::Ship: f.mode = Mode::Ship; break;
                case PortalKind::Ball: f.mode = Mode::Ball; break;
                case PortalKind::Ufo: f.mode = Mode::Ufo; break;
                case PortalKind::Wave: f.mode = Mode::Wave; break;
                case PortalKind::Robot: f.mode = Mode::Robot; break;
                case PortalKind::Spider: f.mode = Mode::Spider; break;
                case PortalKind::Swing: f.mode = Mode::Swing; break;
                default: break;
            }
        }
    }
}

// Orb/pad triggers. Pads fire automatically on contact; orbs/rings only fire
// while the player is holding the button. Effects are applied immediately.
static void applyOrbsPads(PlayerFrame& n, PlayerFrame const& prev, gd::vector<Cell> const& cells, bool input) {
    float hw = n.halfW();
    float hh = n.halfH();
    cocos2d::CCRect pb(n.x - hw, n.y - hh, hw * 2.0f, hh * 2.0f);

    for (auto const& c : cells) {
        if (c.kind != CellKind::Pad && c.kind != CellKind::Orb) continue;
        bool isPad = (c.kind == CellKind::Pad);

        bool trigger = false;
        if (isPad) {
            trigger = intersects(pb, c.rect);
        } else {
            if (!input) continue;
            trigger = prev.x <= c.rect.getMidX() && n.x > c.rect.getMidX();
        }
        if (!trigger) continue;

        float dir = n.upsideDown ? -1.0f : 1.0f;
        n.onGround = false;
        switch (c.orbPadType) {
            case OrbPadType::Yellow:
            case OrbPadType::Green:
                n.vy = dir * 604.5f;
                break;
            case OrbPadType::Pink:
                n.vy = dir * 720.0f;
                break;
            case OrbPadType::Dash:
                n.vy = dir * 660.0f;
                break;
            case OrbPadType::Red:
                n.vy = dir * 1000.0f;
                break;
            case OrbPadType::Spider:
                n.vy = dir * 700.0f;
                break;
            case OrbPadType::Gravity:
                n.upsideDown = !n.upsideDown;
                n.vy = 0.0f;
                break;
            default:
                break;
        }
    }
}

void stepSim(PlayerFrame const& in, bool input, Calibration const& cal, gd::vector<Cell> const& cells,
             PlayerFrame& out, bool& died) {
    PlayerFrame next = singleStep(in, input, cal);
    applyPortals(next, cells, in.x);
    applyOrbsPads(next, in, cells, input);
    died = false;
    out = place(next, cells, died);
}

} // namespace bot
