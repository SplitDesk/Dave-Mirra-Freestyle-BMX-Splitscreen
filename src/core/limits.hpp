#pragma once
// Limits shared by every part of the mod.

namespace bmx {

// The most players in one game: rider select slots, riders and views. Code
// treats players as a list and never assumes a fixed count.
inline constexpr int kMaxPlayers = 8;

}  // namespace bmx
