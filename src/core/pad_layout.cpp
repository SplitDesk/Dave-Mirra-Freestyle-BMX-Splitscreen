#include "core/pad_layout.hpp"

#include "core/pad_mapping.hpp"

namespace bmx {
namespace {

constexpr const char* kInputNames[padinput::Count] = {
    "south", "east", "west", "north", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
    "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4",
    "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6",
    "lefttrigger", "righttrigger",
    "-leftx", "+leftx", "-lefty", "+lefty", "-rightx", "+rightx", "-righty", "+righty",
};

// The ini value of an action without inputs.
constexpr const char* kNone = "none";

struct ActionInfo {
    const char* key;
    uint32_t bit;
};

constexpr ActionInfo kActions[kPadActions] = {
    {"Up", gamebits::kUp},          {"Down", gamebits::kDown},       {"Left", gamebits::kLeft},
    {"Right", gamebits::kRight},    {"Jump", gamebits::kCross},      {"Trick", gamebits::kSquare},
    {"Modifier", gamebits::kCircle}, {"Grind", gamebits::kTriangle}, {"SpinLeft", gamebits::kL1},
    {"SpinRight", gamebits::kR1},   {"Pause", gamebits::kStart},     {"L2", gamebits::kL2},
    {"R2", gamebits::kR2},          {"Select", gamebits::kSelect},
};

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

PadBinding binding(std::initializer_list<int> inputs) {
    PadBinding b;
    for (int input : inputs) b.inputs[b.count++] = static_cast<int8_t>(input);
    return b;
}

}  // namespace

const char* padInputName(int input) {
    return input >= 0 && input < padinput::Count ? kInputNames[input] : "";
}

int padInputFromName(std::string_view name) {
    for (int i = 0; i < padinput::Count; ++i)
        if (equalsNoCase(name, kInputNames[i])) return i;
    return -1;
}

const char* padActionKey(PadAction action) { return kActions[static_cast<int>(action)].key; }
uint32_t padActionBit(PadAction action) { return kActions[static_cast<int>(action)].bit; }

bool PadBinding::operator==(const PadBinding& other) const {
    if (count != other.count) return false;
    for (int i = 0; i < count; ++i)
        if (inputs[i] != other.inputs[i]) return false;
    return true;
}

PadInputs PadBinding::mask() const {
    PadInputs m = 0;
    for (int i = 0; i < count; ++i) m |= PadInputs{1} << inputs[i];
    return m;
}

void PadBinding::setFirst(int input) {
    PadBinding b;
    b.inputs[b.count++] = static_cast<int8_t>(input);
    for (int i = 0; i < count && b.count < kMaxInputs; ++i)
        if (inputs[i] != input) b.inputs[b.count++] = inputs[i];
    *this = b;
}

PadLayout PadLayout::defaults() {
    using namespace padinput;
    PadLayout l;
    l[PadAction::Up] = binding({DpadUp, LeftYNeg, RightTrigger});
    l[PadAction::Down] = binding({DpadDown, LeftYPos, LeftTrigger});
    l[PadAction::Left] = binding({DpadLeft, LeftXNeg});
    l[PadAction::Right] = binding({DpadRight, LeftXPos});
    l[PadAction::Jump] = binding({South});
    l[PadAction::Trick] = binding({West});
    l[PadAction::Modifier] = binding({East});
    l[PadAction::Grind] = binding({North});
    l[PadAction::SpinLeft] = binding({LeftShoulder});
    l[PadAction::SpinRight] = binding({RightShoulder});
    l[PadAction::Pause] = binding({Start});
    l[PadAction::L2] = binding({});
    l[PadAction::R2] = binding({});
    l[PadAction::Select] = binding({Back});
    return l;
}

bool parsePadBinding(std::string_view value, PadBinding& out) {
    if (equalsNoCase(trim(value), kNone)) {
        out = PadBinding{};
        return true;
    }
    PadBinding b;
    while (!value.empty()) {
        const size_t comma = value.find(',');
        const std::string_view name = trim(value.substr(0, comma));
        value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
        if (name.empty()) continue;
        const int input = padInputFromName(name);
        if (input < 0) return false;
        if (b.count < PadBinding::kMaxInputs && !(b.mask() & (PadInputs{1} << input)))
            b.inputs[b.count++] = static_cast<int8_t>(input);
    }
    if (b.count == 0) return false;
    out = b;
    return true;
}

std::string formatPadBinding(const PadBinding& binding) {
    if (binding.count == 0) return kNone;
    std::string out;
    for (int i = 0; i < binding.count; ++i) {
        if (i) out += ',';
        out += padInputName(binding.inputs[i]);
    }
    return out;
}

uint32_t padToGameBits(PadInputs held, const PadLayout& layout) {
    uint32_t bits = 0;
    for (int a = 0; a < kPadActions; ++a)
        if (held & layout.bindings[a].mask()) bits |= kActions[a].bit;
    return bits;
}

}  // namespace bmx
