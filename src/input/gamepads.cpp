// SDL3 is loaded at run time from SDL3.dll, so it can be replaced or updated
// and the mod still runs, keyboard only, without it. Only SDL's headers are
// used; nothing is linked.

#include "input/gamepads.hpp"

#include <SDL3/SDL.h>

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/log.hpp"

namespace bmx::gamepads {
namespace {

static_assert(padinput::kButtons == SDL_GAMEPAD_BUTTON_COUNT, "padinput follows SDL_GamepadButton");

// The oldest SDL3 that has every function below with these signatures.
constexpr int kMinVersion = SDL_VERSIONNUM(3, 2, 0);

struct Api {
    decltype(&SDL_GetVersion) GetVersion;
    decltype(&SDL_SetHint) SetHint;
    decltype(&SDL_Init) Init;
    decltype(&SDL_Quit) Quit;
    decltype(&SDL_GetError) GetError;
    decltype(&SDL_free) Free;
    decltype(&SDL_PumpEvents) PumpEvents;
    decltype(&SDL_PollEvent) PollEvent;
    decltype(&SDL_GetGamepads) GetGamepads;
    decltype(&SDL_OpenGamepad) OpenGamepad;
    decltype(&SDL_CloseGamepad) CloseGamepad;
    decltype(&SDL_GetGamepadName) GetGamepadName;
    decltype(&SDL_UpdateGamepads) UpdateGamepads;
    decltype(&SDL_GetGamepadButton) GetGamepadButton;
    decltype(&SDL_GetGamepadAxis) GetGamepadAxis;
    decltype(&SDL_GetGamepadType) GetGamepadType;
    decltype(&SDL_GetRealGamepadType) GetRealGamepadType;
    decltype(&SDL_GetGamepadButtonLabel) GetGamepadButtonLabel;
};
Api sdl{};
HMODULE g_dll = nullptr;

struct Pad {
    SDL_JoystickID id;
    SDL_Gamepad* handle;
};

// The connected pads in the order they were connected.
std::vector<Pad> g_pads;
bool g_ready = false;

template <typename T>
bool bind(T& slot, const char* name) {
    slot = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(g_dll, name)));
    if (!slot) BMX_LOG_WARN("SDL3.dll is missing %s", name);
    return slot != nullptr;
}

// Loads SDL3.dll from the mod's folder (the ASI loader also allows a
// "scripts" subfolder), otherwise through the normal search, which starts
// with the game's folder.
HMODULE loadNextToMod() {
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&loadNextToMod), &self)) {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring p(path, n);
            p = p.substr(0, p.find_last_of(L"\\/") + 1) + L"SDL3.dll";
            if (HMODULE h = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) return h;
        }
    }
    return LoadLibraryW(L"SDL3.dll");
}

bool loadSdl() {
    g_dll = loadNextToMod();
    if (!g_dll) {
        BMX_LOG_WARN("SDL3.dll not found next to the game - gamepads disabled, keyboard still works");
        return false;
    }
    bool ok = true;
    ok &= bind(sdl.GetVersion, "SDL_GetVersion");
    ok &= bind(sdl.SetHint, "SDL_SetHint");
    ok &= bind(sdl.Init, "SDL_Init");
    ok &= bind(sdl.Quit, "SDL_Quit");
    ok &= bind(sdl.GetError, "SDL_GetError");
    ok &= bind(sdl.Free, "SDL_free");
    ok &= bind(sdl.PumpEvents, "SDL_PumpEvents");
    ok &= bind(sdl.PollEvent, "SDL_PollEvent");
    ok &= bind(sdl.GetGamepads, "SDL_GetGamepads");
    ok &= bind(sdl.OpenGamepad, "SDL_OpenGamepad");
    ok &= bind(sdl.CloseGamepad, "SDL_CloseGamepad");
    ok &= bind(sdl.GetGamepadName, "SDL_GetGamepadName");
    ok &= bind(sdl.UpdateGamepads, "SDL_UpdateGamepads");
    ok &= bind(sdl.GetGamepadButton, "SDL_GetGamepadButton");
    ok &= bind(sdl.GetGamepadAxis, "SDL_GetGamepadAxis");
    ok &= bind(sdl.GetGamepadType, "SDL_GetGamepadType");
    // Optional: without them, pads are told apart by their type alone.
    bind(sdl.GetRealGamepadType, "SDL_GetRealGamepadType");
    bind(sdl.GetGamepadButtonLabel, "SDL_GetGamepadButtonLabel");
    if (!ok) {
        BMX_LOG_WARN("SDL3.dll is not a usable SDL 3 build - gamepads disabled");
        FreeLibrary(g_dll);
        g_dll = nullptr;
        return false;
    }
    const int v = sdl.GetVersion();
    if (v < kMinVersion) {
        BMX_LOG_WARN("SDL3.dll is version %d.%d.%d, need 3.2.0 or newer - gamepads disabled",
                     SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
        FreeLibrary(g_dll);
        g_dll = nullptr;
        return false;
    }
    BMX_LOG_INFO("SDL %d.%d.%d loaded from SDL3.dll", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
                 SDL_VERSIONNUM_MICRO(v));
    return true;
}

void open(SDL_JoystickID id) {
    for (const Pad& p : g_pads)
        if (p.id == id) return;
    SDL_Gamepad* pad = sdl.OpenGamepad(id);
    if (!pad) {
        BMX_LOG_WARN("gamepad %u could not be opened: %s", static_cast<unsigned>(id), sdl.GetError());
        return;
    }
    g_pads.push_back({id, pad});
    const char* name = sdl.GetGamepadName(pad);
    BMX_LOG_INFO("gamepad connected as pad%d: %s", static_cast<int>(g_pads.size()), name ? name : "(unnamed)");
}

void close(SDL_JoystickID id) {
    auto it = std::find_if(g_pads.begin(), g_pads.end(), [id](const Pad& p) { return p.id == id; });
    if (it == g_pads.end()) return;
    BMX_LOG_INFO("gamepad pad%d disconnected", static_cast<int>(it - g_pads.begin()) + 1);
    sdl.CloseGamepad(it->handle);
    g_pads.erase(it);
}

float axis(SDL_Gamepad* pad, SDL_GamepadAxis a) {
    return sdl.GetGamepadAxis(pad, a) / 32767.0f;
}

}  // namespace

bool init() {
    if (g_ready) return true;
    if (!g_dll && !loadSdl()) return false;
    // SDL has no window of its own, so pads must work without SDL's focus.
    sdl.SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!sdl.Init(SDL_INIT_GAMEPAD)) {
        BMX_LOG_ERROR("SDL gamepad init failed: %s", sdl.GetError());
        return false;
    }
    int n = 0;
    if (SDL_JoystickID* ids = sdl.GetGamepads(&n)) {
        for (int i = 0; i < n; ++i) open(ids[i]);
        sdl.Free(ids);
    }
    if (n == 0) BMX_LOG_INFO("no gamepads connected yet (they can be plugged in any time)");
    g_ready = true;
    return true;
}

void shutdown() {
    if (!g_ready) return;
    for (const Pad& p : g_pads) sdl.CloseGamepad(p.handle);
    g_pads.clear();
    sdl.Quit();
    g_ready = false;
}

void update() {
    if (!g_ready) return;
    SDL_Event e;
    sdl.PumpEvents();
    while (sdl.PollEvent(&e)) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) open(e.gdevice.which);
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) close(e.gdevice.which);
    }
    sdl.UpdateGamepads();
}

int count() { return static_cast<int>(g_pads.size()); }

bool read(int index, PadInputs& held) {
    held = 0;
    if (index < 0 || index >= count()) return false;
    SDL_Gamepad* p = g_pads[index].handle;
    for (int b = 0; b < padinput::kButtons; ++b)
        if (sdl.GetGamepadButton(p, static_cast<SDL_GamepadButton>(b))) held |= PadInputs{1} << b;
    auto press = [&](bool on, int input) {
        if (on) held |= PadInputs{1} << input;
    };
    const float lt = axis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = axis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    const float lx = axis(p, SDL_GAMEPAD_AXIS_LEFTX), ly = axis(p, SDL_GAMEPAD_AXIS_LEFTY);
    const float rx = axis(p, SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(p, SDL_GAMEPAD_AXIS_RIGHTY);
    press(lt > kPadThreshold, padinput::LeftTrigger);
    press(rt > kPadThreshold, padinput::RightTrigger);
    press(lx < -kPadThreshold, padinput::LeftXNeg);
    press(lx > kPadThreshold, padinput::LeftXPos);
    press(ly < -kPadThreshold, padinput::LeftYNeg);
    press(ly > kPadThreshold, padinput::LeftYPos);
    press(rx < -kPadThreshold, padinput::RightXNeg);
    press(rx > kPadThreshold, padinput::RightXPos);
    press(ry < -kPadThreshold, padinput::RightYNeg);
    press(ry > kPadThreshold, padinput::RightYPos);
    return true;
}

PadType type(int index) {
    if (index < 0 || index >= count()) return PadType::Unknown;
    SDL_Gamepad* p = g_pads[index].handle;
    if (!p) return PadType::Unknown;

    // By the pad's type, the real one first: a PlayStation pad seen through an
    // Xbox emulator is still a PlayStation pad.
    SDL_GamepadType t = SDL_GAMEPAD_TYPE_UNKNOWN;
    if (sdl.GetRealGamepadType) t = sdl.GetRealGamepadType(p);
    if (t == SDL_GAMEPAD_TYPE_UNKNOWN && sdl.GetGamepadType) t = sdl.GetGamepadType(p);
    switch (t) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            return PadType::PlayStation;
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return PadType::Xbox;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        case SDL_GAMEPAD_TYPE_GAMECUBE:
            return PadType::Nintendo;
        default:
            break;
    }

    // Otherwise (Stadia, Steam and most others) by the face buttons' labels
    // in SDL's mapping.
    if (sdl.GetGamepadButtonLabel) {
        const SDL_GamepadButtonLabel south = sdl.GetGamepadButtonLabel(p, SDL_GAMEPAD_BUTTON_SOUTH);
        const SDL_GamepadButtonLabel east = sdl.GetGamepadButtonLabel(p, SDL_GAMEPAD_BUTTON_EAST);
        if (south == SDL_GAMEPAD_BUTTON_LABEL_CROSS) return PadType::PlayStation;
        if (south == SDL_GAMEPAD_BUTTON_LABEL_A && east == SDL_GAMEPAD_BUTTON_LABEL_B) return PadType::Xbox;
        if (south == SDL_GAMEPAD_BUTTON_LABEL_B && east == SDL_GAMEPAD_BUTTON_LABEL_A) return PadType::Nintendo;
    }
    return t == SDL_GAMEPAD_TYPE_STANDARD ? PadType::Standard : PadType::Unknown;
}

std::string inputLabel(int index, int input) {
    using namespace padinput;
    SDL_Gamepad* p = index >= 0 && index < count() ? g_pads[index].handle : nullptr;
    if (input >= South && input <= North) {
        if (p && sdl.GetGamepadButtonLabel) {
            switch (sdl.GetGamepadButtonLabel(p, static_cast<SDL_GamepadButton>(input))) {
                case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
                case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
                case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
                case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
                case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
                case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
                case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
                case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
                default: break;
            }
        }
        static constexpr const char* kFace[] = {"South", "East", "West", "North"};
        return kFace[input];
    }

    // Shoulders, triggers and stick clicks as the pad family names them.
    static constexpr const char* kPlayStation[] = {"L3", "R3", "L1", "R1", "L2", "R2"};
    static constexpr const char* kXbox[] = {"LS", "RS", "LB", "RB", "LT", "RT"};
    static constexpr const char* kNintendo[] = {"LS", "RS", "L", "R", "ZL", "ZR"};
    static constexpr const char* kGeneric[] = {"L Stick Click", "R Stick Click", "L Shoulder",
                                               "R Shoulder",    "L Trigger",     "R Trigger"};
    const char* const* family = kGeneric;
    switch (type(index)) {
        case PadType::PlayStation: family = kPlayStation; break;
        case PadType::Xbox: family = kXbox; break;
        case PadType::Nintendo: family = kNintendo; break;
        default: break;
    }
    switch (input) {
        case LeftStick: return family[0];
        case RightStick: return family[1];
        case LeftShoulder: return family[2];
        case RightShoulder: return family[3];
        case LeftTrigger: return family[4];
        case RightTrigger: return family[5];
        case Back: return "Back";
        case Guide: return "Guide";
        case Start: return "Start";
        case DpadUp: return "D-Pad Up";
        case DpadDown: return "D-Pad Down";
        case DpadLeft: return "D-Pad Left";
        case DpadRight: return "D-Pad Right";
        case Touchpad: return "Touchpad";
        case LeftXNeg: return "L Stick Left";
        case LeftXPos: return "L Stick Right";
        case LeftYNeg: return "L Stick Up";
        case LeftYPos: return "L Stick Down";
        case RightXNeg: return "R Stick Left";
        case RightXPos: return "R Stick Right";
        case RightYNeg: return "R Stick Up";
        case RightYPos: return "R Stick Down";
        case Misc1: return "Misc 1";
        case Paddle1: case Paddle2: case Paddle3: case Paddle4:
            return "Paddle " + std::to_string(input - Paddle1 + 1);
        case Misc2: case Misc3: case Misc4: case Misc5: case Misc6:
            return "Misc " + std::to_string(input - Misc2 + 2);
        default: return padInputName(input);
    }
}

}  // namespace bmx::gamepads
