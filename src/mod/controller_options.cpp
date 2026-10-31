#include "mod/controller_options.hpp"

#include <array>

#include "common/config.hpp"
#include "common/ini_file.hpp"
#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/menu_buttons.hpp"
#include "core/pad_layout.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "input/gamepads.hpp"
#include "mod/input.hpp"

namespace bmx::controlleroptions {
namespace {

using namespace bmx::game;

constexpr uint32_t kJoypadText = 0xA00C0037;  // + row
constexpr uint8_t kOkRow = 0x0C;
constexpr uint8_t kCaptureOff = 0, kCaptureWaiting = 2;
constexpr uint32_t kDeviceRaw = 0x08;
constexpr uint32_t kStandInButton = 0x1;  // a raw button the capture can take
constexpr int kMaxPads = 16;

using HandlerFn = int(__cdecl*)(int event, int a, int b, int c);

hook::Hook<VoidFn> g_show;
hook::Hook<HandlerFn> g_handler;

const auto FindElement = reinterpret_cast<void*(__cdecl*)(uint32_t id)>(fn::GuiFindElement);
const auto SetText = reinterpret_cast<void(__cdecl*)(void* element, const char* text)>(fn::GuiSetText);
const auto InputDevice = reinterpret_cast<uint8_t*(__cdecl*)(int n)>(fn::InputDevice);

struct State {
    std::wstring iniPath;
    PadLayout pending;  // the layout as the screen shows it, until OK
    bool capturing = false;
    std::array<PadInputs, kMaxPads> held{};  // each pad's inputs at the last look
};
State g;

// The pad whose labels the screen shows: player 1's, else the first one.
int labelPad() {
    const int pad = input::padOf(0);
    if (pad >= 0 && pad < gamepads::count()) return pad;
    return gamepads::count() > 0 ? 0 : -1;
}

// Sets the JOYPAD text of an action row to its first input, or "None".
void showRow(int row) {
    const PadBinding& b = g.pending.bindings[row];
    const std::string label = b.count ? gamepads::inputLabel(labelPad(), b.inputs[0]) : std::string("None");
    if (void* el = FindElement(kJoypadText + row)) SetText(el, label.c_str());
}

bool isConfirm(int event) {
    const int e = event >= guievent::kFirstPad + guievent::kPlayer2Offset ? event - guievent::kPlayer2Offset : event;
    return e == guievent::kConfirm || e == guievent::kStart;
}

// Returns an input pressed on any pad since the last look, or -1.
int freshInput() {
    int found = -1;
    for (int pad = 0; pad < kMaxPads; ++pad) {
        const PadInputs now = pad < gamepads::count() ? input::padInputs(pad) : 0;
        const PadInputs fresh = now & ~g.held[pad];
        g.held[pad] = now;
        for (int i = 0; found < 0 && i < padinput::Count; ++i)
            if (fresh & (PadInputs{1} << i)) found = i;
    }
    return found;
}

// The [Input] section's comment line: every value an action takes.
std::string possibleValues() {
    std::string line = "; Possible values: none";
    for (int i = 0; i < padinput::Count; ++i) line += std::string(" | ") + padInputName(i);
    return line + " (several separated by ,)";
}

// Makes the pending layout the pads' layout and writes it to the ini: an
// action that differs from the default layout gets its line, the others
// lose theirs, and a section left without lines goes.
void commit() {
    input::setLayout(g.pending);
    std::string text = readTextFile(g.iniPath);
    const PadLayout defaults = PadLayout::defaults();
    for (int a = 0; a < kPadActions; ++a) {
        const char* key = padActionKey(static_cast<PadAction>(a));
        if (g.pending.bindings[a] == defaults.bindings[a]) {
            text = removeIniValue(text, "Input", key);
            continue;
        }
        if (!iniHasSection(text, "Input")) {
            const char* eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            if (!text.empty() && text.back() != '\n') text += eol;
            if (!text.empty()) text += eol;
            text += "[Input]" + std::string(eol) + possibleValues() + eol;
        }
        text = setIniValue(text, "Input", key, formatPadBinding(g.pending.bindings[a]));
    }
    if (writeTextFile(g.iniPath, text)) BMX_LOG_INFO("controls: pad layout saved to BmxSplitscreen.ini");
    else BMX_LOG_WARN("controls: could not save the pad layout to BmxSplitscreen.ini");
}

// ControlsShow: the JOYPAD column shows the pad layout.
void __cdecl onShow() {
    g_show.original();
    g.pending = input::layout();
    g.capturing = false;
    for (int row = 0; row < kScreenPadActions; ++row) showRow(row);
}

// Returns whether the game's capture reads the device on `event`: the tick
// and back, as the handler's capture branch does.
bool capturesOn(int event) {
    return event == guievent::kTick || event == guievent::kBack ||
           event == guievent::kBack + guievent::kPlayer2Offset;
}

// ControlsHandler: the game runs the capture itself, with its frame, text and
// sound. Its input device is the keyboard, so during the capture the device's
// raw buttons are replaced: none, or one stand-in button when a pad input was
// just pressed. When the game takes it, the row gets the pad input instead
// and the screen's copy of the mask is put back. Confirm on OK writes the
// layout.
int __cdecl onHandler(int event, int a, int b, int c) {
    uint8_t& capture = global<uint8_t>(addr::kControlsCapture);
    const uint8_t row = global<uint8_t>(addr::kControlsRow);
    if (capture != kCaptureWaiting) {
        g.capturing = false;
        const bool ok = capture == kCaptureOff && row == kOkRow && isConfirm(event);
        const int result = g_handler.original(event, a, b, c);
        if (ok) commit();
        return result;
    }

    int pressed = -1;
    if (capturesOn(event) && row < kScreenPadActions) {
        if (g.capturing) {
            pressed = freshInput();
        } else {
            g.capturing = true;
            freshInput();
        }
    }
    uint8_t* device = InputDevice(global<int>(addr::kControlsPlayer));
    uint32_t& mask = reinterpret_cast<uint32_t*>(addr::kControlsMasks)[row < kScreenPadActions ? row : 0];
    const uint32_t savedMask = mask;
    const uint32_t raw = device ? field<uint32_t>(device, kDeviceRaw) : 0;
    if (device) field<uint32_t>(device, kDeviceRaw) = pressed >= 0 ? kStandInButton : 0;
    const int result = g_handler.original(event, a, b, c);
    if (device) field<uint32_t>(device, kDeviceRaw) = raw;
    if (pressed < 0 || capture != kCaptureOff) return result;

    mask = savedMask;
    g.capturing = false;
    g.pending.bindings[row].setFirst(pressed);
    showRow(row);
    BMX_LOG_INFO("controls: %s = %s", padActionKey(static_cast<PadAction>(row)),
                 formatPadBinding(g.pending.bindings[row]).c_str());
    return result;
}

}  // namespace

bool install(const std::wstring& iniPath) {
    g.iniPath = iniPath;
    bool ok = true;
    ok &= g_show.install(fn::ControlsShow, &onShow, "ControlsShow");
    ok &= g_handler.install(fn::ControlsHandler, &onHandler, "ControlsHandler");
    if (ok) BMX_LOG_INFO("controls: ready (pad layout from BmxSplitscreen.ini)");
    return ok;
}

}  // namespace bmx::controlleroptions
