#pragma once
// How a multiplayer game is scored, by game type.

namespace bmx {

// The menu's game types, numbered as the game numbers them (kSessionSubMode).
enum class GameType { ProQuest = 0, Session = 1, Freeride = 2, Other = 3 };

// Co-op: every rider's tricks count toward player 1's career (goals, trick
// book). Versus: each rider scores for himself and only player 1's own tricks
// reach the save.
enum class PlayRules { Coop, Versus };

// Returns co-op for ProQuest and versus for Session and Freeride.
//
// Planned VERSUS ProQuest (not built; the menu does not offer it):
// - Each player gets his own copy of player 1's challenges, with his own done
//   states, fresh at every run start.
// - "Score over N points" challenges use the player's own run score. A gate or
//   other prop opened by one player is open for all.
// - Every challenge part a player completes is worth 20000 points, added only
//   at the end on PLAYERS SCORE: run score + parts x 20000 = final score.
// - The highest final score wins; tied players all win.
// - Ending the run from the pause menu counts as the timer running out.
// - Nothing is saved: no careers, trick book, records or unlocks. The game's
//   new-record name entry (screen 0x11), its RESULTS screen (0x10, which
//   saves), the save itself and the "new record" messages are skipped.
PlayRules playRules(GameType type);

GameType gameTypeFromSubMode(int subMode);
const char* gameTypeName(GameType type);

}  // namespace bmx
