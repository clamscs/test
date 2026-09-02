#pragma once

#include "Bot.hpp"

namespace bot {

// Apply the physics for a single frame: integrate motion, portals, orbs/pads,
// then resolve collisions. Returns the resulting frame and whether the player died.
void stepSim(PlayerFrame const& in, bool input, Calibration const& cal, gd::vector<Cell> const& cells,
             PlayerFrame& out, bool& died);

void pickMode(PlayerFrame& f, PlayerObject* player);

} // namespace bot
