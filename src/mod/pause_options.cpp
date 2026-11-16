#include "mod/pause_options.hpp"

#include <cstdio>

#include "common/config.hpp"
#include "common/ini_file.hpp"
#include "common/log.hpp"
#include "core/pad_mapping.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/hud.hpp"
#include "mod/rider_collision.hpp"
#include "mod/riders.hpp"
#include "mod/splitscreen.hpp"
#include "mod/view_culling.hpp"

namespace bmx::pauseoptions {
namespace {

using namespace bmx::game;

// The rows: 0 camera type, 1 tutorial hints, 2 split screen, 3 performance mode
// and 4 collision (the mod's), 5 audio options. The game lays its rows out by
// the screen's item count (0x415BDF), so with 6 it leaves room for the mod's
// rows; its audio options label, made on row 2, is made again on row 5. Its
// tick knows audio options as row 2.
constexpr int kSplitRow = 2, kPerformanceRow = 3, kCollisionRow = 4, kAudioRow = 5, kGameAudioRow = 2, kRows = 6;
constexpr int kRowsMiddleY = 0xF0, kRowStep = 0x21, kLabelX = 0x56, kValueRight = 0x229;
// The colours the game draws values with (0x416060).
constexpr uint32_t kTextColour = 0x13040E0, kSelectedValueColour = 0x2F0F0F0;
constexpr int kToggleSound = 0x7D1;
constexpr const char* kSplitLabel = "split screen";
constexpr const char* kPerformanceLabel = "performance mode";
constexpr const char* kCollisionLabel = "collision";
constexpr uint32_t kAudioString = 0x1086;  // "audio options"
// The tutorial hints' values: 0x416060 shows string 0x101A plus the hints bit.
constexpr uint32_t kOffString = 0x101A, kOnString = 0x101B;
constexpr int kCentreX = 0x13F;

// The screen's texts (addr::kMenuTexts); the selected row's is text[row].
// The game keeps audio options in slot 2, the title in slot 3, and the camera
// type's and tutorial hints' labels in slots 4 and 5. The title moves to the
// last slot, the labels to slots 8 and 9, and audio options to slot 5; the
// mod's rows have their values in their rows' slots and their labels in
// slots 6, 10 and 7.
constexpr int kTitleText = 3, kTitleMovedTo = 23, kCameraLabelText = 4, kCameraLabelMovedTo = 8;
constexpr int kHintsLabelText = 5, kHintsLabelMovedTo = 9;
constexpr int kSplitLabelText = 6, kCollisionLabelText = 7, kPerformanceLabelText = 10;
constexpr uint16_t kNoText = 0xFFFF;

// The tutorial hints' HUD element and its "no hint" value (0x415EB4).
constexpr int kHudTips = 0x0A;
constexpr uint8_t kNoTip = 0xFF;

constexpr int kNowMode = 0;  // the ControllerCheck mode OPTIONS' tick passes

using CheckFn = int(__cdecl*)(int controller, uint32_t mask, int mode);
using TextCreateFn = uint16_t(__cdecl*)(int a, const void* font, const char* text, int x, int y, uint32_t rgb, int b);
using WidthFn = uint16_t(__cdecl*)(const char* text, const void* font);
using DestroyFn = void(__cdecl*)(uint32_t handle);
using SoundFn = int(__cdecl*)(int id, int a, int b, int c, int d);
using CameraTypeFn = void(__cdecl*)(int slot, int type);
using HudSetFn = void(__cdecl*)(int id, void* data);
using StringFn = const char*(__cdecl*)(uint32_t id);

const auto ControllerCheck = reinterpret_cast<CheckFn>(fn::ControllerCheck);
const auto TextCreate = reinterpret_cast<TextCreateFn>(fn::OverlayTextCreate);
const auto TextWidth = reinterpret_cast<WidthFn>(fn::OverlayTextWidth);
const auto TextDestroy = reinterpret_cast<DestroyFn>(fn::OverlayDestroy);
const auto PlaySound = reinterpret_cast<SoundFn>(fn::GameEvent);
const auto SetCameraType = reinterpret_cast<CameraTypeFn>(fn::CameraCtlInit);
const auto HudSet = reinterpret_cast<HudSetFn>(fn::HudSet);
const auto GetString = reinterpret_cast<StringFn>(fn::GetString);
const void* const kFont = reinterpret_cast<const void*>(addr::kHudFont);

hook::Hook<void(__cdecl*)(int)> g_create;
hook::Hook<IntFn> g_tick;

std::wstring g_iniPath;

uint16_t* menuTexts() { return reinterpret_cast<uint16_t*>(addr::kMenuTexts); }
int rowY(int row) { return kRowsMiddleY - kRows * kRowStep / 2 + row * kRowStep; }

// Makes a row's value text again, as 0x416060 does the game's.
void showValue(int row, const char* value) {
    uint16_t& text = menuTexts()[row];
    if (text != kNoText) TextDestroy(text);
    const uint32_t colour = gameScreen()->selected == row ? kSelectedValueColour : kTextColour;
    text = TextCreate(1, kFont, value, kValueRight - TextWidth(value, kFont), rowY(row), colour, 0);
}

void showSplitMode() { showValue(kSplitRow, splitModeName(split::mode())); }
void showOnOff(int row, bool on) { showValue(row, GetString(on ? kOnString : kOffString)); }
void showPerformance() { showOnOff(kPerformanceRow, viewculling::enabled()); }
void showCollision() { showOnOff(kCollisionRow, ridercollision::enabled()); }

// Makes a row's "label:" text in `slot`.
void showLabel(int slot, int row, const char* name) {
    char label[32];
    std::snprintf(label, sizeof label, "%s:", name);
    menuTexts()[slot] = TextCreate(1, kFont, label, kLabelX, rowY(row), kTextColour, 0);
}

// Writes a setting to the ini.
void save(const char* section, const char* key, const char* value) {
    if (!saveIniValue(g_iniPath, section, key, value))
        BMX_LOG_WARN("pauseoptions: could not save %s.%s to BmxSplitscreen.ini", section, key);
}

// OptionsCreate: six rows, with audio options moved down for the mod's rows.
void __cdecl onCreate(int show) {
    if (show) gameScreen()->itemCount = kRows;
    g_create.original(show);
    if (!show) return;
    uint16_t* texts = menuTexts();
    texts[kTitleMovedTo] = texts[kTitleText];
    texts[kCameraLabelMovedTo] = texts[kCameraLabelText];
    texts[kHintsLabelMovedTo] = texts[kHintsLabelText];
    TextDestroy(texts[kGameAudioRow]);
    const char* audio = GetString(kAudioString);
    const int audioX = kCentreX - TextWidth(audio, kFont) / 2;
    texts[kAudioRow] = TextCreate(1, kFont, audio, audioX, rowY(kAudioRow), kTextColour, 0);
    texts[kSplitRow] = texts[kPerformanceRow] = texts[kCollisionRow] = kNoText;
    showLabel(kSplitLabelText, kSplitRow, kSplitLabel);
    showLabel(kPerformanceLabelText, kPerformanceRow, kPerformanceLabel);
    showLabel(kCollisionLabelText, kCollisionRow, kCollisionLabel);
    showSplitMode();
    showPerformance();
    showCollision();
}

// OptionsTick: left and right on the mod's rows change their setting, as on
// the game's rows. The rest is the game's tick, run with the rows it knows:
// audio options as row 2 and the mod's rows as row 3, which confirm takes
// back to PAUSE like its other rows. What it changes for player 1 goes to
// everyone.
int __cdecl onTick() {
    GameScreen* screen = gameScreen();
    const uint8_t row = screen->selected;
    const bool modRow = row == kSplitRow || row == kPerformanceRow || row == kCollisionRow;
    if (modRow) {
        const int turn = global<uint8_t>(addr::kCurrentTurnPlayer);
        const int step = ControllerCheck(turn, gamebits::kRight, kNowMode)  ? 1
                         : ControllerCheck(turn, gamebits::kLeft, kNowMode) ? -1
                                                                             : 0;
        if (step != 0) {
            if (row == kSplitRow) {
                constexpr int kModes = 4;
                split::setMode(static_cast<SplitMode>((static_cast<int>(split::mode()) + step + kModes) % kModes));
                showSplitMode();
                save("Split", "Mode", splitModeName(split::mode()));
            } else if (row == kPerformanceRow) {
                viewculling::setEnabled(!viewculling::enabled());
                showPerformance();
                save("Split", "PerformanceMode", viewculling::enabled() ? "1" : "0");
            } else {
                ridercollision::setEnabled(!ridercollision::enabled());
                showCollision();
                save("General", "Collision", ridercollision::enabled() ? "1" : "0");
            }
            PlaySound(kToggleSound, 0, 0, 0, 0);
            return 1;
        }
    }
    constexpr int kGameOtherRow = 3;
    screen->selected = row == kAudioRow ? kGameAudioRow : modRow ? kGameOtherRow : row;
    const int acted = g_tick.original();
    screen->selected = row;
    if (acted) shareWithPlayers();
    return acted;
}

}  // namespace

void shareWithPlayers() {
    if (!riders::secondActive()) return;
    const uint8_t* p1 = playerConfig(0);
    const uint8_t camera = p1[addr::kPlayerCfgCamera];
    const uint8_t hints = p1[addr::kPlayerCfgFlags] & addr::kPlayerCfgHints;
    const int players = 2 + extras::count();
    for (int p = 1; p < players; ++p) {
        bool hintsGone = false;
        extras::asPlayer(p, [&] {
            uint8_t* cfg = playerConfig(1);
            uint8_t& flags = cfg[addr::kPlayerCfgFlags];
            hintsGone = (flags & addr::kPlayerCfgHints) && !hints;
            flags = static_cast<uint8_t>((flags & ~addr::kPlayerCfgHints) | hints);
            cfg[addr::kPlayerCfgCamera] = camera;
            SetCameraType(1, camera);
        });
        // With hints turned off the hint showing goes too, as for player 1.
        if (hintsGone && hud::ready(p)) {
            uint8_t none = kNoTip;
            hud::begin(p);
            HudSet(kHudTips, &none);
            hud::end();
        }
    }
}

bool install(const std::wstring& iniPath) {
    g_iniPath = iniPath;
    bool ok = true;
    ok &= g_create.install(fn::OptionsCreate, &onCreate, "OptionsCreate");
    ok &= g_tick.install(fn::OptionsTick, &onTick, "OptionsTick");
    if (ok) BMX_LOG_INFO("pauseoptions: ready");
    return ok;
}

}  // namespace bmx::pauseoptions
