#pragma once
// The front end's title screen and main menu.
//
// - The title screen reads "PRESS TO START"; the first button pressed decides
//   player 1's controls.
// - The main menu offers SINGLEPLAYER and MULTIPLAYER, then PROQUEST, SESSION
//   and FREERIDE. In multiplayer the other players join on the rider select
//   (rider_select.hpp).

#include <cstdint>

namespace bmx::frontend {

// Hooks the title screen and the main menu.
bool install();

// Returns the players of the game being started: 1 after SINGLEPLAYER,
// otherwise the players who joined (at least 2).
int players();

// Returns whether MULTIPLAYER was picked.
bool multiplayer();

// Returns player 1 plus everyone who joined.
int joinedPlayers();

// Joins the player who freshly pressed one of `buttons` on a free pad or the
// keyboard; returns the slot taken, or -1.
int join(uint32_t buttons);

// Removes a joined player (slot 1 and up); the players after him move up.
void leave(int slot);

// Removes everyone but player 1, when the main menu is reached from the
// rider select or after a level.
void leaveRide();

}  // namespace bmx::frontend
