#pragma once
// The game's controller buttons, and the keyboard turned into them
// (core/pad_layout.hpp does the gamepads).
//
// The game's buttons follow a PlayStation pad: 0x1 up, 0x2 down, 0x4 left,
// 0x8 right, 0x10 Triangle, 0x20 Square, 0x40 Circle, 0x80 Cross, 0x100 L1,
// 0x200 R1, 0x400 L2, 0x800 R2, 0x4000 Start, 0x8000 Select; the keyboard adds
// 0x10000 (Esc, back) and 0x20000 (Enter, confirm) for the menus.

#include <cstdint>

namespace bmx {

namespace gamebits {
inline constexpr uint32_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
inline constexpr uint32_t kTriangle = 0x0010, kSquare = 0x0020, kCircle = 0x0040, kCross = 0x0080;
inline constexpr uint32_t kL1 = 0x0100, kR1 = 0x0200, kL2 = 0x0400, kR2 = 0x0800;
inline constexpr uint32_t kStart = 0x4000, kSelect = 0x8000;
inline constexpr uint32_t kEsc = 0x10000, kEnter = 0x20000;
}  // namespace gamebits

// Reads the keyboard through one of the game's key layouts, as its device
// poll (0x493910) does. `table` holds {DirectInput key, input index} pairs
// ending with 0xFF (0x561E68 for player 1); `keys` is the 256-byte DirectInput
// keyboard state, bit 7 meaning down.
uint32_t keyTableToGameBits(const uint8_t* table, const uint8_t* keys);

}  // namespace bmx
