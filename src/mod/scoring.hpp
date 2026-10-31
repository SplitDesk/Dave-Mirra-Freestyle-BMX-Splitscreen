#pragma once
// Scoring for the riders in slot 2. The game tracks tricks only for the
// rider whose turn it is; the mod runs the same tracking each frame for every
// rider in slot 2, with the career writes going to player 1's career in
// co-op and nowhere in versus.

namespace bmx::scoring {

// Hooks the run update, scoring, goals and the HUD's set calls.
bool install();

// Returns whether the game is co-op with rider 2 on the course, where the HUD
// shows the team's total score.
bool teamScore();

}  // namespace bmx::scoring
