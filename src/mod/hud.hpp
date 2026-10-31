#pragma once
// A HUD per player. The game draws its HUD once per frame, into the first
// camera drawn. The mod draws it into every view, fitted to the view, and
// gives every player from 2 up a HUD of his own with his own numbers.

namespace bmx::hud {

// Hooks the HUD setup, its objects and its drawing.
bool install();

// Starts a rendered frame.
void beginFrame();

// Runs the other players' HUDs; once per game update while a level runs.
void update();

// Destroys the other players' HUDs when a level ends, before the game tears
// down its own.
void levelEnd();

// The HUD instances: player 1's is the game's own, and every other player has
// his own copy, made while his rider is on the course. Between begin(p) and
// end(), the game's HUD calls (HudSet and so on) go to player p's HUD; the
// calls nest. ready() tells whether a player's HUD exists.
bool ready(int player);
void begin(int player);
void end();

// Returns the player whose HUD is in (0 player 1).
int live();

// The same for the player in slot 2: player 2, or an extra rider in his turn
// there.
bool slot2Ready();
void beginSlot2();
void endSlot2();

// Marks the next HudSet(id) call of this frame as meant for the riders in
// slot 2; the run-start reset resets each rider's HUD score with the same
// call. takeRoute() consumes the mark.
void routeNextToRiders(int id);
bool takeRoute(int id);

}  // namespace bmx::hud
