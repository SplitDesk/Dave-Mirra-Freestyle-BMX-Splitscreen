#pragma once
// The game's own memory limits that more players reach first. The GUI's
// heap (3 MB, in a 4 MB static arena) holds every rider select screen's
// texts and images and fills up near 16 players, and the 64 actors (two per
// rider) run out at 32 riders; each gets room for every player. The game's
// asserts, which show a message box and stop the game, are written to the
// log too.

#include <cstdint>

namespace bmx::gameheap {

// Patches the sizes, before the game sets the memory up, and hooks the
// assert.
bool install();

// Returns the bytes free in the GUI's heap, or 0 while it is not set up.
uint32_t guiHeapFree();

}  // namespace bmx::gameheap
