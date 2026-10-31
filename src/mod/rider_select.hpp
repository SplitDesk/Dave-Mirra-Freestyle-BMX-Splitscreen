#pragma once
// The multiplayer rider select. Every joined player gets his own copy of the
// game's select screen (rider, bike, style), all on screen at once, and they
// pick at the same time; more players join with confirm or Start. Once
// everyone is ready, player 1 picks the level on the game's own level step.
// Single player keeps the game's screen.

#include <cstdint>

namespace bmx::riderselect {

// Hooks the select screen and the GUI asset allocation.
bool install();

// Reads what player `player` (2 and up, 0-based) picked on the last rider
// select, as PlayerConfig +3..+5 (rider, outfit, bike); false when he did not
// pick.
bool pickOf(int player, uint8_t out[3]);

}  // namespace bmx::riderselect
