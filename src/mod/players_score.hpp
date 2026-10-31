#pragma once
// The PLAYERS SCORE screen. After a multiplayer run (not Freeride) it lists
// every player's run score before RUN FINISHED, which gets a "players score"
// item below "trick list" to open it again. It is an in-game screen like the
// game's own, run by the game's screen code.

namespace bmx::playersscore {

// Hooks the screen opening, the string lookup and RUN FINISHED.
bool install();

// Records every player's run score when the run is over for every rider,
// before the results open.
void runOver();

// Forgets the scores when the level closes.
void levelEnd();

}  // namespace bmx::playersscore
