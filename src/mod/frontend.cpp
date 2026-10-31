#include "mod/frontend.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "common/log.hpp"
#include "core/join_slots.hpp"
#include "core/limits.hpp"
#include "core/menu_buttons.hpp"
#include "core/pad_mapping.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/input.hpp"

namespace bmx::frontend {
namespace {

using namespace bmx::game;

// GameEvent sounds of the menus.
constexpr int kMoveSound = 2000;
constexpr int kConfirmSound = 0x7D2;

using GuiHandlerFn = int(__cdecl*)(int event, int a, int b, int c);
using FindElementFn = void*(__cdecl*)(uint32_t id);
using SetTextFn = void(__cdecl*)(void* element, const char* text);
using SendEventFn = int(__cdecl*)(int event, int a, int b);
using GetStringFn = const char*(__cdecl*)(uint32_t id);
using SoundFn = uint32_t(__cdecl*)(uint32_t id, float* a, float* b, uint32_t c, uint32_t d);

hook::Hook<VoidFn> g_titleEnter;
hook::Hook<GuiHandlerFn> g_titleHandler;
hook::Hook<int(__cdecl*)(int)> g_mainMenuEnter;
hook::Hook<GuiHandlerFn> g_mainMenuHandler;
hook::Hook<VoidFn> g_mainMenuLabels;
hook::Hook<IntFn> g_attractDemoStart;
hook::Hook<VoidFn> g_mainMenuInstall;

const auto FindElement = reinterpret_cast<FindElementFn>(fn::GuiFindElement);
const auto SetText = reinterpret_cast<SetTextFn>(fn::GuiSetText);
const auto SendEvent = reinterpret_cast<SendEventFn>(fn::GuiSendEvent);
const auto GetString = reinterpret_cast<GetStringFn>(fn::GetString);
const auto PlaySound = reinterpret_cast<SoundFn>(fn::GameEvent);
const auto FrontEndExit = reinterpret_cast<void(__cdecl*)(int)>(fn::FrontEndExit);

void playSound(int id) { PlaySound(static_cast<uint32_t>(id), nullptr, nullptr, 0, 0); }

// Sets the text of the main menu's wheel slots -3..3 around the centre.
void setSlotText(int k, const char* text) {
    if (void* el = FindElement(addr::kMainMenuSlotMid + k)) SetText(el, text);
}

void clearSlots() {
    for (int k = -3; k <= 3; ++k) setSlotText(k, "");
}

// ------------------------------------------------------------ title screen --
// "PRESS TO START": the first button pressed decides player 1's controls, the
// pad it was pressed on or the keyboard. While the screen waits, player 1 is
// on the keyboard, so the screen's own events come from the keyboard and the
// pads are watched here. The mod shows it before the main menu at start-up.

constexpr uint32_t kTitleText = 0xA0050001;

bool g_titleWaiting = false;
bool g_titleTextDone = false;
bool g_toTitle = false;  // the main menu is handing over to the title screen

// TitleEnter: puts player 1 on the keyboard and watches the pads.
void __cdecl onTitleEnter() {
    g_titleEnter.original();
    input::setKeyboard(0);
    input::watchPads();
    g_titleWaiting = true;
    g_titleTextDone = false;
    g_toTitle = false;
}

// Returns whether `event` is a pad button event of player 1 or 2.
bool isButtonEvent(int event) {
    const int e = event >= guievent::kFirstPad + guievent::kPlayer2Offset ? event - guievent::kPlayer2Offset : event;
    return e >= guievent::kFirstPad && e <= guievent::kLastPad;
}

// TitleHandler: replaces the title text, and starts on the first button
// pressed on the keyboard or a pad, giving that pad to player 1.
int __cdecl onTitleHandler(int event, int a, int b, int c) {
    if (!g_titleWaiting) return g_titleHandler.original(event, a, b, c);
    if (isButtonEvent(event)) {
        g_titleWaiting = false;
        BMX_LOG_INFO("frontend: player 1 is on the keyboard");
        return g_titleHandler.original(guievent::kStart, a, b, c);
    }
    const int result = g_titleHandler.original(event, a, b, c);
    if (event != guievent::kTick) return result;
    if (!g_titleTextDone && global<uint16_t>(addr::kTitleTextSet)) {
        g_titleTextDone = true;
        if (void* el = FindElement(kTitleText)) SetText(el, "PRESS TO START");
    }
    const int pad = input::freshPress(~0u);
    if (pad == input::kKeyboard) {
        g_titleWaiting = false;
        BMX_LOG_INFO("frontend: player 1 is on the keyboard");
        g_titleHandler.original(guievent::kStart, 0, 0, 0);
    } else if (pad >= 0) {
        g_titleWaiting = false;
        BMX_LOG_INFO("frontend: player 1 is on pad %d", pad + 1);
        input::setPad(0, pad);
        input::ignoreHeld(0);
        g_titleHandler.original(guievent::kStart, 0, 0, 0);
    }
    return result;
}

// --------------------------------------------------------------- main menu --
// The main menu is a 7-slot wheel over the game's 6 items. The mod keeps the
// wheel (its look, scrolling and sounds) and shows its own lists on it:
//   top:   SINGLEPLAYER, MULTIPLAYER, then the game's items 3..5
//   modes: PROQUEST, SESSION, FREERIDE
//   quit:  NO, YES, in place of the game's message box
// Each list wraps around the wheel as the game's does (slot k shows item
// (cursor + k) mod count, 0x4404E0). Picking a game type hands the confirm to
// the game with its own item selected.

enum class Action { Single, Multi, Original, ProQuest, Session, Freeride, Quit, QuitNo, QuitYes };
enum Level { kTop, kModes, kQuit, kLevels };

// A list item; `value` is the game's item for Original and Quit.
struct Item {
    Action action;
    int value = 0;
};

constexpr int kGameQuitItem = 5, kExitQuit = 1;
constexpr int kMaxListItems = 8;

struct Menu {
    int level = kTop;
    std::array<int, kLevels> cursor{};
    int pendingMove = 0;  // the scroll the game is animating
    bool multiplayer = false;
    int players = 1;
    JoinSlots join;
};
Menu m;

int wrap(int i, int n) { return ((i % n) + n) % n; }

// Writes the items of list `level` to `out` and returns their count.
int itemsOf(int level, Item* out) {
    int n = 0;
    switch (level) {
        case kTop:
            out[n++] = {Action::Single};
            out[n++] = {Action::Multi};
            for (int i = 3; i < addr::kMainMenuItems; ++i)
                out[n++] = i == kGameQuitItem ? Item{Action::Quit, i} : Item{Action::Original, i};
            break;
        case kModes:
            out[n++] = {Action::ProQuest};
            out[n++] = {Action::Session};
            out[n++] = {Action::Freeride};
            break;
        case kQuit:
            out[n++] = {Action::QuitNo};
            out[n++] = {Action::QuitYes};
            break;
    }
    return n;
}

// The text of the game's main menu item `item`.
const char* gameItemText(int item) {
    const uint16_t id = reinterpret_cast<const uint16_t*>(addr::kMainMenuStrings)[item];
    const char* t = GetString(id);
    return t ? t : "";
}

void itemText(const Item& it, char* buf, size_t n) {
    switch (it.action) {
        case Action::Single: std::snprintf(buf, n, "SINGLEPLAYER"); break;
        case Action::Multi: std::snprintf(buf, n, "MULTIPLAYER"); break;
        case Action::Original:
        case Action::Quit: std::snprintf(buf, n, "%s", gameItemText(it.value)); break;
        case Action::QuitNo: std::snprintf(buf, n, "NO"); break;
        case Action::QuitYes: std::snprintf(buf, n, "YES"); break;
        case Action::ProQuest: std::snprintf(buf, n, "%s", gameItemText(0)); break;
        case Action::Session: std::snprintf(buf, n, "%s", gameItemText(1)); break;
        case Action::Freeride: std::snprintf(buf, n, "%s", gameItemText(2)); break;
    }
}

int& cursorOf(int level) { return m.cursor[level >= 0 && level < kLevels ? level : kTop]; }

// Shows the current list on the wheel around its cursor.
void labelSlots() {
    Item items[kMaxListItems];
    const int count = itemsOf(m.level, items);
    for (int k = -3; k <= 3; ++k) {
        char text[64];
        itemText(items[wrap(cursorOf(m.level) + k, count)], text, sizeof text);
        setSlotText(k, text);
    }
}

// Shows list `level`; the quit list opens on NO.
void enterLevel(int level) {
    m.level = level;
    if (level == kQuit) cursorOf(kQuit) = 0;
    labelSlots();
}

// MULTIPLAYER: player 1 keeps his controls and the others join on the rider
// select.
void startJoin() {
    m.multiplayer = true;
    m.join.reset(kMaxPlayers, input::padOf(0));
    for (int slot = 1; slot < kMaxPlayers; ++slot) input::setPad(slot, JoinSlots::kNoPad);
    m.players = 2;
    input::watchPads();
    enterLevel(kModes);
}

// Gives every slot its joined pad, after a join or a leave moved players.
void applyJoins() {
    for (int slot = 1; slot < kMaxPlayers; ++slot)
        if (input::padOf(slot) != m.join.padOf(slot)) input::setPad(slot, m.join.padOf(slot));
    m.players = std::max(2, m.join.players());
}

// MainMenuLabels: applies the scroll the game animated and shows the list,
// or blank slots while the title screen is on its way.
void __cdecl onMainMenuLabels() {
    g_mainMenuLabels.original();
    if (g_toTitle) {
        clearSlots();
        return;
    }
    if (m.pendingMove) {
        Item items[kMaxListItems];
        const int count = itemsOf(m.level, items);
        int& c = cursorOf(m.level);
        c = wrap(c + m.pendingMove, count);
        m.pendingMove = 0;
    }
    labelSlots();
}

// Opens the title screen from the main menu, at start-up and on back from
// the top list, as the main menu's pending action.
void goToTitle() {
    g_toTitle = true;
    m.level = kTop;
    clearSlots();
    global<uintptr_t>(addr::kScreenPending) = fn::TitleEnterThunk;
    global<int>(fn::kScreenPendingTicks) = 0;
}

// MainMenuEnter: the game passes 0 at start-up and after a level (0x44DA7C),
// on its first item, and 1 from the title screen and on back from the rider
// select (0x447600), where the wheel keeps its list. With 0 the mod returns
// to the top list with player 1 alone, and at start-up opens the title
// screen.
int __cdecl onMainMenuEnter(int fromTitle) {
    static bool started = false;
    if (!fromTitle) {
        m.level = kTop;
        cursorOf(kTop) = 0;
        leaveRide();
    }
    const int result = g_mainMenuEnter.original(fromTitle);
    if (!started) {
        started = true;
        if (!fromTitle) goToTitle();
    }
    return result;
}

// AttractDemoStart: the menus come back after the demo through the main menu.
bool g_fromDemo = false;

int __cdecl onAttractDemoStart() {
    g_fromDemo = true;
    return g_attractDemoStart.original();
}

// MainMenuInstall: after the attract demo the title screen opens, on the top
// list with player 1 alone, as at start-up.
void __cdecl onMainMenuInstall() {
    g_mainMenuInstall.original();
    if (!g_fromDemo) return;
    g_fromDemo = false;
    m.level = kTop;
    cursorOf(kTop) = 0;
    leaveRide();
    goToTitle();
    BMX_LOG_INFO("frontend: demo over, back to the title screen");
}

// Lets the game act on its own item `item`, as if it were selected.
int forward(int item, int event, int a, int b, int c) {
    global<uint16_t>(addr::kMainMenuSelected) = static_cast<uint16_t>(item);
    return g_mainMenuHandler.original(event, a, b, c);
}

// MainMenuHandler: runs the mod's lists. The game scrolls the wheel, and acts
// on the items it owns.
int __cdecl onMainMenuHandler(int event, int a, int b, int c) {
    if (g_toTitle && event != guievent::kTick) return 1;
    const bool idle = global<uintptr_t>(addr::kScreenStep) == 0 && global<uintptr_t>(addr::kScreenPending) == 0;
    if (!idle) return g_mainMenuHandler.original(event, a, b, c);

    Item items[kMaxListItems];
    const int count = itemsOf(m.level, items);
    int& cursor = cursorOf(m.level);
    if (cursor >= count) cursor = count - 1;
    const Item& it = items[cursor];

    switch (event) {
        case guievent::kUp:
        case guievent::kDown:
            m.pendingMove = event == guievent::kUp ? -1 : 1;
            return g_mainMenuHandler.original(event, a, b, c);
        case guievent::kBack: {
            if (m.level == kTop) {
                goToTitle();
                playSound(kMoveSound);
                BMX_LOG_INFO("frontend: back to the title screen");
                return 1;
            }
            enterLevel(kTop);
            playSound(kMoveSound);
            return 1;
        }
        case guievent::kConfirm:
        case guievent::kStart:
            break;
        default:
            return g_mainMenuHandler.original(event, a, b, c);
    }

    switch (it.action) {
        case Action::Single:
            m.multiplayer = false;
            m.players = 1;
            BMX_LOG_INFO("frontend: 1 player");
            enterLevel(kModes);
            playSound(kConfirmSound);
            return 1;
        case Action::Multi:
            startJoin();
            playSound(kConfirmSound);
            return 1;
        case Action::ProQuest: return forward(0, event, a, b, c);
        case Action::Session: return forward(1, event, a, b, c);
        case Action::Freeride: return forward(2, event, a, b, c);
        case Action::Original: return forward(it.value, event, a, b, c);
        case Action::Quit:
            enterLevel(kQuit);
            playSound(kConfirmSound);
            return 1;
        case Action::QuitNo:
            enterLevel(kTop);
            playSound(kConfirmSound);
            return 1;
        case Action::QuitYes:
            BMX_LOG_INFO("frontend: quit");
            playSound(kConfirmSound);
            FrontEndExit(kExitQuit);
            return 1;
    }
    return 1;
}

}  // namespace

int players() { return m.players; }

bool multiplayer() { return m.multiplayer; }
int joinedPlayers() { return m.join.players(); }

int join(uint32_t buttons) {
    const int pad = input::freshPress(buttons);
    if (pad < 0) return -1;
    if (pad == input::kKeyboard && input::usesKeyboard(0)) return -1;
    const int slot = m.join.join(pad);
    if (slot <= 0) return -1;
    if (pad == input::kKeyboard) BMX_LOG_INFO("frontend: the keyboard joined as player %d", slot + 1);
    else BMX_LOG_INFO("frontend: pad %d joined as player %d", pad + 1, slot + 1);
    applyJoins();
    playSound(kConfirmSound);
    return slot;
}

void leave(int slot) {
    if (!m.join.leave(slot)) return;
    BMX_LOG_INFO("frontend: player %d left", slot + 1);
    applyJoins();
}

void leaveRide() {
    for (int slot = m.join.players() - 1; slot >= 1; --slot) leave(slot);
}

bool install() {
    bool ok = true;
    ok &= g_titleEnter.install(fn::TitleEnter, &onTitleEnter, "TitleEnter");
    ok &= g_titleHandler.install(fn::TitleHandler, &onTitleHandler, "TitleHandler");
    ok &= g_mainMenuEnter.install(fn::MainMenuEnter, &onMainMenuEnter, "MainMenuEnter");
    ok &= g_mainMenuHandler.install(fn::MainMenuHandler, &onMainMenuHandler, "MainMenuHandler");
    ok &= g_mainMenuLabels.install(fn::MainMenuLabels, &onMainMenuLabels, "MainMenuLabels");
    ok &= g_attractDemoStart.install(fn::AttractDemoStart, &onAttractDemoStart, "AttractDemoStart");
    ok &= g_mainMenuInstall.install(fn::MainMenuInstall, &onMainMenuInstall, "MainMenuInstall");
    if (ok) BMX_LOG_INFO("frontend: ready (main menu: single player / multiplayer, up to %d players)", kMaxPlayers);
    return ok;
}

}  // namespace bmx::frontend
