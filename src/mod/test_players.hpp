#pragma once
// Test players, for trying many players with few pads (General.Debug=1). On
// the rider select F9 adds one, every frame while held, and F10 removes the
// last one the same way. A test player has a device of his own that no pad
// is: on the rider select it presses confirm until he is ready; on the
// course it gives player 1's buttons, each test player half a second later
// than the one before, so they ride after him in a line. Start and Select
// are left out, so test players never pause the game.

#include <cstdint>

#include "mod/input.hpp"

namespace bmx::testplayers {

// Turns the test players' keys on or off (General.Debug).
void setEnabled(bool on);
bool enabled();

// The device of test player k (0 and up), past the pads and the keyboard.
inline constexpr int kFirstDevice = input::kKeyboard + 1;
inline bool isTestDevice(int device) { return device >= kFirstDevice; }
inline int indexOf(int device) { return device - kFirstDevice; }

// Returns the buttons (game bits) of a test device now.
uint32_t buttons(int device);

// Keeps player 1's buttons of this frame; runs once per controller update.
void recordPlayer1(uint32_t bits);

// Forgets player 1's buttons of the last level.
void levelStart();

// Whether F9 (add) and F10 (remove) are held, with test players enabled.
bool addHeld();
bool removeHeld();

// Returns a test device no player in slots 1..players-1 uses.
int freeDevice(int players);

// Returns the last slot below `players` a test player is in, or -1.
int lastTestSlot(int players);

}  // namespace bmx::testplayers
