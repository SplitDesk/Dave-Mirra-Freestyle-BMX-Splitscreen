#pragma once
// Riders 3 and up. The game has two rider slots, and its rider, camera and
// input code is written for two (arrays of two in many places). The extra
// riders take turns in slot 2: each keeps his own copy of everything the game
// keeps per slot (rider state, camera controller, player config, model
// definition, controller record, bike physics), swapped into slot 2 for his
// turn and out again.

#include <cstdint>

namespace bmx::extras {

// Returns the number of extra riders in the level. Extra rider k is player
// k + 2 (0-based).
int count();
inline int playerOf(int k) { return k + 2; }

// Returns the extra rider in slot 2, or -1 when rider 2 is in it.
int current();

// Returns the player in slot 2: 1 (player 2) or an extra rider's player.
inline int slotPlayer() { return current() >= 0 ? playerOf(current()) : 1; }

// Puts extra rider k in slot 2 while rider 2 waits outside; end() swaps them
// back. The calls do not nest; asPlayer() handles that.
void begin(int k);
void end();

// Runs `f` with player `player` (1 and up) in slot 2, then puts back the
// rider who was there.
template <typename F>
void asPlayer(int player, F f) {
    const int k = player - 2, prev = current();
    if (k == prev) return f();
    if (prev >= 0) end();
    if (k >= 0) begin(k);
    f();
    if (k >= 0) end();
    if (prev >= 0) begin(prev);
}

// Runs `f` with extra rider k in slot 2.
template <typename F>
void as(int k, F f) { asPlayer(playerOf(k), f); }

// Runs `f` with each extra rider in turn in slot 2.
template <typename F>
void forEach(F f) {
    for (int k = 0; k < count(); ++k) as(k, f);
}

// Creates the extra riders of a game of `players`, each in turn in slot 2,
// while the level start creates rider 2. `create` makes player `player`'s
// rider in a slot (the game's CreateRider) and returns non-zero on success.
using CreateFn = int (*)(int player, int slot, int character, int outfit, int bike);
void createAll(int players, CreateFn create);

// Returns player `player`'s rider state wherever it is: in its slot or kept
// aside while another rider has slot 2; nullptr when there is no such rider.
const uint8_t* riderOf(int player);

// Forgets the extra riders at the level end, whose objects go with the level.
void levelEnd();

}  // namespace bmx::extras
