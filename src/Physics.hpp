#pragma once

#include "Bot.hpp"

namespace bot {

struct SimResult {
    bool died = false;
    PlayerFrame final{};
    int framesRun = 0;
    float progressX = 0.0f;
};

SimResult simulate(PlayerFrame const& start, gd::vector<Cell> const& cells, float lookahead, bool hold);

void applyPortals(PlayerFrame& f, gd::vector<Cell> const& cells, float prevX);

void pickMode(PlayerFrame& f, PlayerObject* player);

// Advance a single physics step (integrate + portals + collide).
void stepSim(PlayerFrame const& in, bool input, Calibration const& cal, gd::vector<Cell> const& cells,
             PlayerFrame& out, bool& died);

} // namespace bot