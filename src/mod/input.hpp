#pragma once
// Per-player controls. The game reads its own devices (keyboard layouts for
// players 1 and 2, and a DirectInput joystick) into Controller[slot] every
// frame; right after, the mod puts in each player's own device: the one he
// pressed on the title screen (player 1) or joined with on the rider select.

#include <cstdint>

#include "core/pad_layout.hpp"

namespace bmx::input {

// Hooks the controller creation and updates.
bool install();

// The gamepad layout every pad follows (core/pad_layout.hpp).
const PadLayout& layout();
void setLayout(const PadLayout& layout);

// Returns the inputs held on pad `pad` (0 and up); none when it is not
// connected.
PadInputs padInputs(int pad);

// The keyboard as a device a player can join with, like a pad, with player
// 1's keys (the game's layout, 0x561E68). Pads are 0 and up.
inline constexpr int kKeyboard = 16;

// Reads a player's own pad (game button bits) in the front end, for the
// screens where each player chooses for himself. Returns false when the
// player has no pad or it is not connected.
bool menuPad(int player, uint32_t& bits);

// Returns the device (a pad or kKeyboard) a player uses, or -1.
int padOf(int player);

// Gives a player a device for this game; -1 for none.
void setPad(int player, int pad);

// Returns whether the keyboard drives the player.
bool usesKeyboard(int player);

// Puts the player on the game's own reading of the keyboard (title screen).
void setKeyboard(int player);

// Makes the buttons the player holds on his pad count only once released.
void ignoreHeld(int player);

// Writes the player's pad into the game's controller record `slot` (held, and
// changed since the record's held): for riders 3 and up, who borrow slot 2's
// record for their turn.
void fillController(int slot, int player);

// watchPads() records every device's held buttons; freshPress() returns a
// device on which one of `buttons` (game bits) went down since the last
// call, or -1.
void watchPads();
int freshPress(uint32_t buttons);

}  // namespace bmx::input
