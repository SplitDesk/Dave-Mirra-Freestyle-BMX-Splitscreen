#include "mod/input.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/pad_mapping.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "input/gamepads.hpp"
#include "mod/button_icons.hpp"

namespace bmx::input {
namespace {

using namespace bmx::game;

// Controller record fields (docs/ADDRESS.md, "Input").
constexpr uint32_t kCtlIgnore = 0x04;   // buttons ignored until released
constexpr uint32_t kCtlHeld = 0x10;     // buttons held this frame
constexpr uint32_t kCtlChanged = 0x14;  // buttons pressed or released this frame
constexpr uint32_t kCtlAnalog = 0x18;   // float stick x, y, x2, y2

// Esc and Enter reach a player on a pad from the keyboard while his pad is
// disconnected, so the in-game menus can be left.
constexpr uint32_t kMenuKeys = gamebits::kEsc | gamebits::kEnter;

// kGameState while the attract demo plays.
constexpr uint8_t kGameStateDemo = 4;

// CreateController device types.
constexpr int kDeviceKeyboardOnly = 1;
constexpr int kDeviceKeyboardAndJoystick = 2;

// What drives a player: the game's own keyboard reading of his controller
// record, or a device of the mod's (an SDL pad or kKeyboard, in padIndex).
struct InputBinding {
    enum class Kind { None, Keyboard, Pad } kind = Kind::None;
    int padIndex = -1;
};

// The bindings until the title screen and the rider select decide: player 1
// on the keyboard, the others on pads in order.
std::array<InputBinding, kMaxPlayers> defaultBindings() {
    std::array<InputBinding, kMaxPlayers> b{};
    b[0] = {InputBinding::Kind::Keyboard};
    for (int i = 1; i < kMaxPlayers; ++i) b[i] = {InputBinding::Kind::Pad, i - 1};
    return b;
}

struct State {
    PadLayout layout = PadLayout::defaults();
    std::array<InputBinding, kMaxPlayers> bindings = defaultBindings();
    bool sdlTried = false;
    bool sdlOk = false;
    std::array<uint32_t, addr::kControllersInExe> lastWritten{};
    std::array<bool, addr::kControllersInExe> announced{};
    uint32_t menuLastWritten = 0;
    uint32_t menuIgnore = 0;  // player 1's pad buttons the menus skip until released
    bool pauseLogged = false;
    std::array<uint32_t, kMaxPlayers> extraPad{};  // players 3 and up: their pad last frame

    // Pad buttons ignored until released; the game's own ignore mask only
    // covers its keyboard and DirectInput device.
    std::array<uint32_t, addr::kControllersInExe> padIgnore{};
    std::array<uint32_t, addr::kControllersInExe> padHeldAtPause{};
    std::array<uint32_t, addr::kControllersInExe> lastPad{};
    bool ignoreLogged = false;

    // Each device's buttons at the last look (pads, then the keyboard), for
    // the title and join screens.
    std::array<uint32_t, kKeyboard + 1> watched{};

    // The input trace of LogLevel=debug: the first few hundred changes.
    int traceLeft = 400;
    std::array<uint32_t, addr::kControllersInExe> traceKeys{}, tracePad{}, traceRider{};
    std::array<int, addr::kControllersInExe> traceAnalog{};
};
State g;

using UIntFn = uint32_t(__cdecl*)();
using CreateControllerFn = void*(__cdecl*)(int type);
hook::Hook<UIntFn> g_controllerUpdate;
hook::Hook<CreateControllerFn> g_createController;
hook::Hook<UIntFn> g_menuControllerUpdate;
using PauseFn = void(__cdecl*)(uint32_t paused);
hook::Hook<PauseFn> g_controllersSetPaused;

// Starts SDL on first use; returns whether pads are available.
bool sdlReady() {
    if (!g.sdlTried) {
        g.sdlTried = true;
        g.sdlOk = gamepads::init();
    }
    return g.sdlOk;
}

uint8_t* controller(int slot) {
    return reinterpret_cast<uint8_t*>(addr::kControllerArray + slot * addr::kControllerStride);
}

// Reads the keyboard with player 1's keys, as the game does.
uint32_t keyboardButtons() {
    return keyTableToGameBits(reinterpret_cast<const uint8_t*>(addr::kKeyboardLayoutP1),
                              reinterpret_cast<const uint8_t*>(addr::kKeyboardState));
}

// Reads a device's buttons (game bits); false for a disconnected pad.
bool deviceButtons(int device, uint32_t& bits) {
    bits = 0;
    if (device == kKeyboard) {
        bits = keyboardButtons();
        return true;
    }
    PadInputs held = 0;
    if (!gamepads::read(device, held)) return false;
    bits = padToGameBits(held, g.layout);
    return true;
}

uint32_t padButtons(int device) {
    uint32_t bits = 0;
    deviceButtons(device, bits);
    return bits;
}

const char* deviceName(int device, char* buf, size_t n) {
    if (device == kKeyboard) return "the keyboard (player 1's keys)";
    std::snprintf(buf, n, "pad%d", device + 1);
    return buf;
}

const char* describe(const InputBinding& b, char* buf, size_t n) {
    switch (b.kind) {
        case InputBinding::Kind::Keyboard: return "keyboard";
        case InputBinding::Kind::None: return "none";
        default: return deviceName(b.padIndex, buf, n);
    }
}

// Reads the player's own device; 0 when he has none or it is disconnected,
// which `plugged` reports.
uint32_t padBitsFor(int slot, bool* plugged = nullptr) {
    const InputBinding& b = g.bindings[slot];
    if (plugged) *plugged = false;
    if (b.kind != InputBinding::Kind::Pad) return 0;
    uint32_t bits = 0;
    const bool connected = deviceButtons(b.padIndex, bits);
    if (plugged) *plugged = connected;
    if (bits && !g.announced[slot]) {
        g.announced[slot] = true;
        char buf[16];
        BMX_LOG_INFO("input: player %d is using %s", slot + 1, deviceName(b.padIndex, buf, sizeof buf));
    }
    return bits;
}

// The player's buttons this frame, given the game's own keyboard reading
// for his record.
uint32_t playerBits(int slot, uint32_t keys) {
    const InputBinding& b = g.bindings[slot];
    uint32_t bits = 0;
    switch (b.kind) {
        case InputBinding::Kind::Keyboard: bits = keys; break;
        case InputBinding::Kind::None: break;
        case InputBinding::Kind::Pad: {
            bool plugged = false;
            const uint32_t pad = padBitsFor(slot, &plugged);
            if (!plugged) bits = keys & kMenuKeys;
            g.lastPad[slot] = pad;
            g.padIgnore[slot] &= pad;
            bits |= pad & ~g.padIgnore[slot];
            break;
        }
    }
    return bits;
}

// Logs, at LogLevel=debug, each change of what the game read from the
// keyboard, what the pad sent, the controller's analog values and what the
// rider got.
void trace(int slot, uint32_t keys, uint32_t padBits) {
    if (g.traceLeft <= 0 || !log::enabled(LogLevel::Debug)) return;
    const uint8_t* ctl = controller(slot);
    const float* an = &field<float>(const_cast<uint8_t*>(ctl), kCtlAnalog);
    int analog = 0;
    for (int i = 0; i < 4; ++i)
        if (an[i] > 0.01f || an[i] < -0.01f) analog |= 1 << i;
    const uint8_t* r = riderState(slot);
    const uint32_t riderHeld = *reinterpret_cast<const uint32_t*>(r + addr::rider::kButtonsHeld);
    if (keys == g.traceKeys[slot] && padBits == g.tracePad[slot] && analog == g.traceAnalog[slot] &&
        riderHeld == g.traceRider[slot])
        return;
    g.traceKeys[slot] = keys;
    g.tracePad[slot] = padBits;
    g.traceAnalog[slot] = analog;
    g.traceRider[slot] = riderHeld;
    --g.traceLeft;
    PadInputs held = 0;
    const InputBinding& b = g.bindings[slot];
    if (b.kind == InputBinding::Kind::Pad) gamepads::read(b.padIndex, held);
    BMX_LOG_DEBUG("trace: P%d keys=%05X pad=%05X rider=%05X analog=(%.2f %.2f %.2f %.2f) "
                 "inputs=%010llX mask=%05X",
                 slot + 1, keys, padBits, riderHeld, an[0], an[1], an[2], an[3],
                 static_cast<unsigned long long>(held), field<uint32_t>(const_cast<uint8_t*>(ctl), kCtlIgnore));
}

// Writes the held and changed buttons of a controller record.
void writeController(int slot, uint32_t bits) {
    uint8_t* ctl = controller(slot);
    field<uint32_t>(ctl, kCtlHeld) = bits;
    field<uint32_t>(ctl, kCtlChanged) = bits ^ g.lastWritten[slot];
    g.lastWritten[slot] = bits;
}

// ControllerUpdate: writes each player's buttons into his record. The game
// pauses on player 1's Start only, so a fresh Start from any other player,
// players 3 and up included, counts as player 1's.
uint32_t __cdecl onControllerUpdate() {
    const uint32_t result = g_controllerUpdate.original();
    if (!sdlReady()) return result;
    gamepads::update();
    buttonicons::update();

    std::array<uint32_t, addr::kControllersInExe> bits{};
    for (int slot = 0; slot < addr::kControllersInExe; ++slot) {
        const uint32_t keys = field<uint32_t>(controller(slot), kCtlHeld);
        bits[slot] = playerBits(slot, keys);
        trace(slot, keys, padBitsFor(slot));
    }

    // The attract demo ends on a fresh press on player 1's record, so the
    // keyboard and every pad reach it.
    if (global<uint8_t>(addr::kGameState) == kGameStateDemo) {
        bits[0] |= field<uint32_t>(controller(0), kCtlHeld);
        for (int pad = 0; pad < gamepads::count(); ++pad) bits[0] |= padButtons(pad);
    }

    auto passStart = [&](int player) {
        bits[0] |= gamebits::kStart;
        if (!g.pauseLogged) {
            g.pauseLogged = true;
            BMX_LOG_INFO("input: player %d pressed Start, passed to player 1 (pause)", player + 1);
        }
    };
    for (int slot = 1; slot < addr::kControllersInExe; ++slot)
        if ((bits[slot] & gamebits::kStart) && !(g.lastWritten[slot] & gamebits::kStart)) passStart(slot);
    for (int player = addr::kControllersInExe; player < kMaxPlayers; ++player) {
        uint32_t pad = 0;
        menuPad(player, pad);
        if ((pad & gamebits::kStart) && !(g.extraPad[player] & gamebits::kStart)) passStart(player);
        g.extraPad[player] = pad;
    }

    for (int slot = 0; slot < addr::kControllersInExe; ++slot) writeController(slot, bits[slot]);
    return result;
}

// ControllersSetPaused: the game pauses its controllers whenever play stops
// (pause menu, run start, run end) and on resume ignores the buttons held now
// but not before, so the confirm that picked "Continue" does not reach the
// rider. It covers its own devices only; the mod does the same for the pads.
void __cdecl onControllersSetPaused(uint32_t paused) {
    const bool changes = (paused & 0xFF) != global<uint8_t>(addr::kControllersPaused);
    g_controllersSetPaused.original(paused);
    BMX_LOG_DEBUG("input: controllers %s%s (P1 held %05X, ignore %05X)", (paused & 0xFF) ? "paused" : "resumed",
                  changes ? "" : " (no change)", field<uint32_t>(controller(0), kCtlHeld),
                  field<uint32_t>(controller(0), 0x4));
    if (!changes || !g.sdlOk) return;
    if (!(paused & 0xFF)) g.traceLeft = std::max(g.traceLeft, 120);
    // On resume the game puts back the buttons held before the pause; the
    // changed bits start from them too, or a confirm held in the menu would
    // show as released on the first frame back, and releasing jump hops.
    for (int slot = 0; slot < addr::kControllersInExe; ++slot)
        g.lastWritten[slot] = field<uint32_t>(controller(slot), kCtlHeld);
    for (int slot = 0; slot < addr::kControllersInExe; ++slot) {
        if (g.bindings[slot].kind != InputBinding::Kind::Pad) continue;
        const uint32_t now = padButtons(g.bindings[slot].padIndex);
        if (paused & 0xFF) {
            // What the rider held, leaving out ignored buttons: a button still
            // ignored from the last menu was never held, so it is ignored in
            // this one too.
            g.padHeldAtPause[slot] = g.lastPad[slot] & ~g.padIgnore[slot];
            g.padIgnore[slot] = now;
        } else {
            g.padIgnore[slot] = now & ~g.padHeldAtPause[slot];
            BMX_LOG_DEBUG("input: P%d pad now %05X, held at pause %05X -> ignored %05X", slot + 1, now,
                          g.padHeldAtPause[slot], g.padIgnore[slot]);
            if (g.padIgnore[slot] && !g.ignoreLogged) {
                g.ignoreLogged = true;
                BMX_LOG_INFO("input: player %d's pad buttons %05X ignored until released (held from the menu)", slot + 1,
                             g.padIgnore[slot]);
            }
        }
    }
}

// MenuControllerUpdate: the front end follows player 1, on his pad only, or
// on the keyboard while the pad is disconnected.
uint32_t __cdecl onMenuControllerUpdate() {
    const uint32_t result = g_menuControllerUpdate.original();
    if (!sdlReady()) return result;
    gamepads::update();
    uint32_t& held = global<uint32_t>(addr::kMenuController + 4);
    uint32_t& changed = global<uint32_t>(addr::kMenuController + 8);
    bool plugged = false;
    const uint32_t padBits = padBitsFor(0, &plugged);
    g.menuIgnore &= padBits;
    const uint32_t bits = (plugged ? 0 : held) | (padBits & ~g.menuIgnore);
    held = bits;
    changed = bits ^ g.menuLastWritten;
    g.menuLastWritten = bits;
    return result;
}

// CreateController: the game gives every controller (gameplay and menu) the
// keyboard and the first DirectInput joystick, so one pad would drive every
// rider. With SDL handling the pads, they are created keyboard-only.
void* __cdecl onCreateController(int type) {
    const uintptr_t ret = BMX_RETURN_ADDRESS();
    const bool gameplay = ret >= fn::InitControllers && ret < fn::InitControllersEnd;
    const bool menu = ret >= fn::MenuControllerInit && ret < fn::MenuControllerInitEnd;
    if ((gameplay || menu) && type == kDeviceKeyboardAndJoystick && sdlReady()) {
        BMX_LOG_INFO("input: %s controller is keyboard-only, gamepads go through SDL", gameplay ? "gameplay" : "menu");
        type = kDeviceKeyboardOnly;
    }
    return g_createController.original(type);
}

void bind(int player, const InputBinding& binding) {
    if (player < 0 || player >= kMaxPlayers) return;
    g.bindings[player] = binding;
    if (player < addr::kControllersInExe) {
        g.announced[player] = false;
        g.padIgnore[player] = 0;
    }
    char buf[32];
    BMX_LOG_INFO("input: player %d = %s", player + 1, describe(binding, buf, sizeof buf));
}

}  // namespace

const PadLayout& layout() { return g.layout; }

void setLayout(const PadLayout& layout) { g.layout = layout; }

PadInputs padInputs(int pad) {
    PadInputs held = 0;
    if (sdlReady()) gamepads::read(pad, held);
    return held;
}

bool menuPad(int player, uint32_t& bits) {
    bits = 0;
    if (!g.sdlOk || player < 0 || player >= kMaxPlayers) return false;
    const InputBinding& b = g.bindings[player];
    if (b.kind != InputBinding::Kind::Pad) return false;
    return deviceButtons(b.padIndex, bits);
}

int padOf(int player) {
    if (player < 0 || player >= kMaxPlayers) return -1;
    const InputBinding& b = g.bindings[player];
    if (b.kind == InputBinding::Kind::Keyboard) return kKeyboard;
    return b.kind == InputBinding::Kind::Pad ? b.padIndex : -1;
}

bool usesKeyboard(int player) {
    if (player < 0 || player >= kMaxPlayers) return false;
    const InputBinding& b = g.bindings[player];
    return b.kind == InputBinding::Kind::Keyboard || (b.kind == InputBinding::Kind::Pad && b.padIndex == kKeyboard);
}

void setPad(int player, int pad) {
    bind(player, pad >= 0 ? InputBinding{InputBinding::Kind::Pad, pad} : InputBinding{});
}

void setKeyboard(int player) { bind(player, {InputBinding::Kind::Keyboard}); }

void fillController(int slot, int player) {
    if (slot < 0 || slot >= addr::kControllersInExe || player < 0 || player >= kMaxPlayers) return;
    const int pad = padOf(player);
    const uint32_t bits = pad >= 0 && sdlReady() ? padButtons(pad) : 0;
    uint8_t* ctl = controller(slot);
    field<uint32_t>(ctl, kCtlChanged) = bits ^ field<uint32_t>(ctl, kCtlHeld);
    field<uint32_t>(ctl, kCtlHeld) = bits;
}

void ignoreHeld(int player) {
    const int pad = padOf(player);
    if (pad < 0) return;
    const uint32_t held = padButtons(pad);
    if (player == 0) g.menuIgnore = held;
    if (player < addr::kControllersInExe) g.padIgnore[player] = held;
}

void watchPads() {
    if (!sdlReady()) return;
    for (int device = 0; device < static_cast<int>(g.watched.size()); ++device) g.watched[device] = padButtons(device);
}

int freshPress(uint32_t buttons) {
    if (!sdlReady()) return -1;
    int fresh = -1;
    for (int device = 0; device < static_cast<int>(g.watched.size()); ++device) {
        const uint32_t now = padButtons(device);
        if ((now & ~g.watched[device] & buttons) && fresh < 0) fresh = device;
        g.watched[device] = now;
    }
    return fresh;
}

bool install() {
    bool ok = true;
    ok &= g_controllerUpdate.install(fn::ControllerUpdate, &onControllerUpdate, "ControllerUpdate");
    ok &= g_createController.install(fn::CreateController, &onCreateController, "CreateController");
    ok &= g_menuControllerUpdate.install(fn::MenuControllerUpdate, &onMenuControllerUpdate, "MenuControllerUpdate");
    ok &= g_controllersSetPaused.install(fn::ControllersSetPaused, &onControllersSetPaused, "ControllersSetPaused");
    if (!ok) return false;
    for (int slot = 0; slot < addr::kControllersInExe; ++slot) {
        char buf[32];
        BMX_LOG_INFO("input: player %d = %s", slot + 1, describe(g.bindings[slot], buf, sizeof buf));
    }
    return true;
}

}  // namespace bmx::input
