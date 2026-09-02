#pragma once

#include "Bot.hpp"
#include "Physics.hpp"

namespace bot {

class DecisionEngine {
public:
    DecisionEngine(float lookaheadBlocks);
    void setLookahead(float blocks) { m_lookahead = blocks; }
    float getLookahead() const { return m_lookahead; }
    bool decide(PlayerFrame const& state, gd::vector<Cell> const& cells);

private:
    bool survive(bool hold, PlayerFrame const& s, gd::vector<Cell> const& c, SimResult& out) const;
    bool targetAltitude(PlayerFrame const& s, gd::vector<Cell> const& c, float& targetY) const;
    float corridorCenter(PlayerFrame const& s, gd::vector<Cell> const& c) const;
    bool aim(float targetY, PlayerFrame const& s, gd::vector<Cell> const& cells, bool preferHold) const;

    float m_lookahead;
};

} // namespace bot