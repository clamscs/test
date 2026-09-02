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
    float corridorCenter(PlayerFrame const& s, gd::vector<Cell> const& c) const;
    float clearance(PlayerFrame const& s, gd::vector<Cell> const& c, float top, float bottom) const;
    bool continuous(PlayerFrame const& s, gd::vector<Cell> const& c, bool preferHold) const;
    bool clickForCube(PlayerFrame const& s, gd::vector<Cell> const& c) const;

    float m_lookahead;
};

} // namespace bot