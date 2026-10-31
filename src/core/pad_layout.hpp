#pragma once
// The gamepad layout: which gamepad inputs press each of the game's buttons.
// One layout serves every pad. It is kept in BmxSplitscreen.ini under
// [Input], one line per action with one or more inputs, e.g. "Jump=south" or
// "Up=dpup,-lefty", or "none" for an action no pad presses.

#include <array>
#include <initializer_list>
#include <cstdint>
#include <string>
#include <string_view>

namespace bmx {

// The gamepad inputs: SDL3's buttons in SDL_GamepadButton order, the two
// triggers and the four half axes of each stick (negative Y is up).
namespace padinput {
enum : int {
    South, East, West, North, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DpadUp, DpadDown, DpadLeft, DpadRight, Misc1, Paddle1, Paddle2, Paddle3, Paddle4, Touchpad,
    Misc2, Misc3, Misc4, Misc5, Misc6,
    LeftTrigger, RightTrigger,
    LeftXNeg, LeftXPos, LeftYNeg, LeftYPos, RightXNeg, RightXPos, RightYNeg, RightYPos,
    Count
};
inline constexpr int kButtons = LeftTrigger;
}  // namespace padinput

// A set of gamepad inputs, one bit per padinput value.
using PadInputs = uint64_t;

// Stick and trigger values past this count as pressed.
inline constexpr float kPadThreshold = 0.5f;

// Returns an input's ini name: SDL3's names, with the face buttons named by
// position (south, east, west, north) and the half axes written as SDL3's
// mappings write them (-leftx, +leftx, ...).
const char* padInputName(int input);

// Returns the input with ini name `name`, or -1.
int padInputFromName(std::string_view name);

// The actions of the game's controller options screen, in its row order, and
// the game buttons that are not on it.
enum class PadAction {
    Up, Down, Left, Right, Jump, Trick, Modifier, Grind, SpinLeft, SpinRight, Pause,
    L2, R2, Select,
    Count
};
inline constexpr int kPadActions = static_cast<int>(PadAction::Count);
inline constexpr int kScreenPadActions = static_cast<int>(PadAction::L2);

// Returns an action's ini key and the game button it presses.
const char* padActionKey(PadAction action);
uint32_t padActionBit(PadAction action);

// The inputs of one action, in the order written; the first is the one the
// controller options screen shows and changes.
struct PadBinding {
    static constexpr int kMaxInputs = 8;
    std::array<int8_t, kMaxInputs> inputs{};
    int count = 0;

    PadInputs mask() const;
    bool operator==(const PadBinding& other) const;
    // Makes `input` the first input, keeping the others after it.
    void setFirst(int input);
};

struct PadLayout {
    std::array<PadBinding, kPadActions> bindings{};

    PadBinding& operator[](PadAction a) { return bindings[static_cast<int>(a)]; }
    const PadBinding& operator[](PadAction a) const { return bindings[static_cast<int>(a)]; }

    // The layout used for every action the ini does not set.
    static PadLayout defaults();
};

// Parses an ini value ("dpup,-lefty", or "none" for no input); returns
// false, leaving `out` as it is, when a name is unknown or the value is
// empty.
bool parsePadBinding(std::string_view value, PadBinding& out);

// Formats a binding as an ini value; "none" when it has no input.
std::string formatPadBinding(const PadBinding& binding);

// Returns the game buttons the held inputs press.
uint32_t padToGameBits(PadInputs held, const PadLayout& layout);

}  // namespace bmx
