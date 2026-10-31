#pragma once
// Room for more menu primitives. The game draws its menus as PlayStation-style
// primitives, and one frame holds 1024 of them (one buffer, one ordering
// table) without a check. A rider select screen per player needs several
// times that, so the mod moves both into memory sized for kMaxPlayers screens
// and patches the sizes in the code.

namespace bmx::guibuffers {

// Allocates the buffer and the ordering table and patches the game to use
// them; the game keeps its own when the code or the memory is not as
// expected.
bool install();

// Returns the ordering-table entries, one per primitive, that the menus used
// in their last frame.
int used();

// Returns the number of entries.
int capacity();

}  // namespace bmx::guibuffers
