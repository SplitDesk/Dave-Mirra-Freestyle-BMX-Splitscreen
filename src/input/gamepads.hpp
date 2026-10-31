#pragma once
// Gamepads through SDL3's gamepad subsystem, without an SDL window. Pads are
// numbered in the order they were connected, from 0.

#include <cstdint>
#include <string>

#include "core/pad_layout.hpp"

namespace bmx::gamepads {

// The button icon family that fits a pad.
enum class PadType {
    Unknown,
    Standard,
    Xbox,
    PlayStation,
    Nintendo,
};

// Loads SDL3 and starts its gamepad subsystem. Runs on the game's thread,
// not in DllMain.
bool init();
void shutdown();

// Handles connections and disconnections and refreshes the pads' state; runs
// once per game frame.
void update();

// Returns the number of connected pads.
int count();

// Reads the inputs held on pad `index`; returns false when no pad is
// connected there.
bool read(int index, PadInputs& held);

// Returns the icon family of pad `index`, by its type or, for pads SDL has no
// type for, by its face buttons' labels.
PadType type(int index);

// Returns the name of an input as pad `index` labels it (e.g. "Cross", "A",
// "LB", "D-Pad Up"); position names when no pad is connected there.
std::string inputLabel(int index, int input);

}  // namespace bmx::gamepads
