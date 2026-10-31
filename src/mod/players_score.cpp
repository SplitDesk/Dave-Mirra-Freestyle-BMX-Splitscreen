#include "mod/players_score.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/pad_mapping.hpp"
#include "core/play_rules.hpp"
#include "core/screen_items.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/riders.hpp"
#include "mod/scoring.hpp"

namespace bmx::playersscore {
namespace {

using namespace bmx::game;

// The mod's texts, handed out by the game's string lookup. The game's string
// set 1 holds ids 0x1000..0x11AE, so these ids are free.
constexpr uint32_t kTitleString = 0x1F00, kItemString = 0x1F01;
constexpr const char* kTitle = "PLAYERS SCORE";
constexpr const char* kItem = "players score";

// The game's texts.
constexpr uint32_t kPlayerString = 0x1000;     // "player"
constexpr uint32_t kYourScoreString = 0x1002;  // "your score"
constexpr uint32_t kTotalString = 0x1003;      // "total score"
constexpr uint32_t kContinueString = 0x1006;   // "continue"
constexpr uint32_t kWinsString = 0x1016;       // "wins"
constexpr uint32_t kTieString = 0x1019;        // "tie"

// RUN FINISHED's item actions (0x41A480); its tick does nothing for actions
// above 8.
constexpr uint16_t kTrickListAction = 6;
constexpr uint16_t kPlayersAction = 9;
constexpr uint16_t kContinueAction = 0;

// RUN FINISHED's font, layout and colours (0x41A480, 0x41A830).
const void* const kFont = reinterpret_cast<const void*>(addr::kHudFont);
constexpr int kCentreX = 0x13F, kTitleY = 0x3C;
constexpr int kLinesY = 0x6C, kLineStep = 0x1D, kLabelX = 0x35, kValueRight = 0x24A;
constexpr int kItemsBottom = 0x1A4;
constexpr uint32_t kTitleColour = 0xF0F0F0, kTextColour = 0x13040E0, kValueColour = 0xD0D0D0;
constexpr uint32_t kTeamColour = 0x50FF50;    // the HUD's team total green
constexpr uint32_t kWinnerColour = 0xE0A040;  // RUN FINISHED's highlighted score (0x41AA4A)

// How RUN FINISHED's tick reads the pads (0x41AD12): an item is picked with
// Cross, Enter or Start from any rider; the turn rider's L1 + Start takes a
// screenshot.
constexpr uint32_t kPick = gamebits::kCross | gamebits::kEnter, kPickStart = gamebits::kStart;
constexpr uint32_t kScreenshot = gamebits::kL1;
constexpr int kNowMode = 0, kScreenshotMode = 2;  // the ControllerCheck modes it passes
constexpr int kPickedSound = 0x7D2;               // played when an item is picked (0x41AF9B)

using OpenFn = void(__cdecl*)(int id);
using StringFn = const char*(__cdecl*)(uint32_t id);
using CheckFn = int(__cdecl*)(int controller, uint32_t mask, int mode);
using TextFn = void(__cdecl*)(const void* font, const char* text, int x, int y, uint32_t rgb, int flags);
using TextCreateFn = uint16_t(__cdecl*)(int a, const void* font, const char* text, int x, int y, uint32_t rgb, int b);
using WidthFn = uint16_t(__cdecl*)(const char* text, const void* font);
using SoundFn = int(__cdecl*)(int id, int a, int b, int c, int d);
using ColourFn = uint32_t(__cdecl*)();

const auto ScreenOpen = reinterpret_cast<OpenFn>(fn::ScreenOpen);
const auto ControllerCheck = reinterpret_cast<CheckFn>(fn::ControllerCheck);
const auto Text = reinterpret_cast<TextFn>(fn::OverlayImmediateText);
const auto TextCreate = reinterpret_cast<TextCreateFn>(fn::OverlayTextCreate);
const auto TextWidth = reinterpret_cast<WidthFn>(fn::OverlayTextWidth);
const auto PlaySound = reinterpret_cast<SoundFn>(fn::GameEvent);
const auto SelectedColour = reinterpret_cast<ColourFn>(fn::SelectedItemColour);

hook::Hook<OpenFn> g_open;
hook::Hook<StringFn> g_getString;
hook::Hook<void(__cdecl*)(int)> g_runFinishedCreate;
hook::Hook<IntFn> g_runFinishedTick;
hook::Hook<VoidFn> g_runFinishedDraw;

struct State {
    int players = 0;  // players in the last run's results; 0 when there are none to show
    std::array<int32_t, kMaxPlayers> scores{};
    int32_t total = 0;
    int32_t best = 0;  // the winning score in versus
    bool coop = false;
    bool showFirst = false;  // the next RUN FINISHED opens as PLAYERS SCORE
    bool opening = false;    // PLAYERS SCORE is opening
    bool teamLine = false;   // RUN FINISHED is drawing its score line with the team total
};
State g;

// The game's string lookup with the mod's texts; "your score" reads "total
// score" on RUN FINISHED's co-op line.
const char* GetString(uint32_t id) {
    switch (id & 0xFFFF) {
        case kTitleString: return kTitle;
        case kItemString: return kItem;
        case kYourScoreString: return g_getString.original(g.teamLine ? kTotalString : id);
        default: return g_getString.original(id);
    }
}

ScreenItem* items() { return reinterpret_cast<ScreenItem*>(addr::kScreenItems); }

// Returns whether an item is picked, as RUN FINISHED's tick decides.
bool picked() {
    const int turn = global<uint8_t>(addr::kCurrentTurnPlayer);
    if (ControllerCheck(turn, kScreenshot, kScreenshotMode) && ControllerCheck(turn, kPickStart, kNowMode))
        return false;
    for (int i = 0; i < global<uint8_t>(addr::kRiderCount); ++i)
        if (ControllerCheck(i, kPick, kNowMode) || ControllerCheck(i, kPickStart, kNowMode)) return true;
    return false;
}

// Opens PLAYERS SCORE in RUN FINISHED's place.
void openPlayersScore() {
    g.opening = true;
    ScreenOpen(addr::kRunFinishedScreen);
}

// ----------------------------------------------------------- PLAYERS SCORE --
// Laid out like RUN FINISHED: a title, one line per player ("player 1:
// 1234"), then the team total in co-op or the winner in versus ("player 2
// wins" or "tie", as the game's 2-player results put it, 0x4134B7), and the
// "continue" item.

// "player 1", "player 2" and so on: the game's "player" and a number.
void playerName(int p, char* buf, size_t n) { std::snprintf(buf, n, "%s %d", GetString(kPlayerString), p + 1); }

// Draws a score line: the label on the left, the value right-aligned.
void line(const char* label, const char* value, uint32_t valueColour, int y) {
    Text(kFont, label, kLabelX, y, kTextColour, 0);
    Text(kFont, value, kValueRight - TextWidth(value, kFont), y, valueColour, 0);
}

// Draws the items as RUN FINISHED does (0x41AB36), once they show.
void drawItems() {
    if (global<uint32_t>(addr::kScreenFrames) <= global<uint32_t>(addr::kScreenItemsDelay)) return;
    const GameScreen* entry = gameScreen();
    int y = kItemsBottom - entry->itemCount * kLineStep;
    for (int i = 0; i < entry->itemCount; ++i, y += kLineStep) {
        const char* text = GetString(items()[i].text);
        const uint32_t colour = i == entry->selected ? SelectedColour() : kTextColour;
        Text(kFont, text, kCentreX - TextWidth(text, kFont) / 2, y, colour, 0);
    }
}

// The screen's create, tick and draw functions.
void __cdecl createScreen(int show) {
    if (!show) return;
    const char* title = GetString(kTitleString);
    const int x = kCentreX - TextWidth(title, kFont) / 2;
    global<uint16_t>(addr::kScreenTitleText) = TextCreate(1, kFont, title, x, kTitleY, kTitleColour, 0);
    items()[0] = {kContinueAction, static_cast<uint16_t>(kContinueString)};
    gameScreen()->itemCount = 1;
}

int __cdecl tickScreen() {
    if (!picked()) return 0;
    ScreenOpen(addr::kRunFinishedScreen);
    PlaySound(kPickedSound, 0, 0, 0, 0);
    return 1;
}

void __cdecl drawScreen() {
    char name[40], label[48], value[16];
    int y = kLinesY;
    int winners = 0, winner = 0;
    for (int p = 0; p < g.players; ++p, y += kLineStep) {
        playerName(p, name, sizeof name);
        std::snprintf(label, sizeof label, "%s:", name);
        std::snprintf(value, sizeof value, "%ld", static_cast<long>(g.scores[p]));
        const bool won = !g.coop && g.scores[p] == g.best;
        if (won) {
            ++winners;
            winner = p;
        }
        line(label, value, won ? kWinnerColour : kValueColour, y);
    }
    if (g.coop) {
        std::snprintf(label, sizeof label, "%s:", GetString(kTotalString));
        std::snprintf(value, sizeof value, "%ld", static_cast<long>(g.total));
        line(label, value, kTeamColour, y);
    } else {
        if (winners == 1) {
            playerName(winner, name, sizeof name);
            std::snprintf(label, sizeof label, "%s %s", name, GetString(kWinsString));
        } else {
            std::snprintf(label, sizeof label, "%s", GetString(kTieString));
        }
        Text(kFont, label, kCentreX - TextWidth(label, kFont) / 2, y, kTextColour, 0);
    }
    drawItems();
}

GameScreen g_screen{&createScreen, &tickScreen, &drawScreen, 0, 0, {}};

// ------------------------------------------------------------------- hooks --
// ScreenOpen: PLAYERS SCORE opens under RUN FINISHED's id with its own entry,
// so the game treats it the same way (the run's replay behind it, its
// rules).
void __cdecl onScreenOpen(int id) {
    if (id == addr::kRunFinishedScreen && g.showFirst) {
        g.showFirst = false;
        g.opening = true;
    }
    if (id != addr::kRunFinishedScreen || !g.opening) return g_open.original(id);
    g.opening = false;
    GameScreen*& slot = reinterpret_cast<GameScreen**>(addr::kGameScreenTable)[id];
    GameScreen* const game = slot;
    slot = &g_screen;
    g_open.original(id);
    slot = game;
    BMX_LOG_INFO("playersscore: showing %d players' scores", g.players);
}

const char* __cdecl onGetString(uint32_t id) { return GetString(id); }

// RunFinishedCreate: adds "players score" below "trick list".
void __cdecl onRunFinishedCreate(int show) {
    g_runFinishedCreate.original(show);
    if (!show || g.players == 0) return;
    GameScreen* entry = gameScreen();
    entry->itemCount = static_cast<uint8_t>(insertItemAfter(items(), entry->itemCount, addr::kScreenItemsMax,
                                                            kTrickListAction,
                                                            {kPlayersAction, static_cast<uint16_t>(kItemString)}));
}

// RunFinishedTick: "players score" opens PLAYERS SCORE.
int __cdecl onRunFinishedTick() {
    const GameScreen* entry = gameScreen();
    if (g.players == 0 || items()[entry->selected].action != kPlayersAction || !picked())
        return g_runFinishedTick.original();
    openPlayersScore();
    PlaySound(kPickedSound, 0, 0, 0, 0);
    return 1;
}

// RunFinishedDraw: the game draws one score line per rider, but only player
// 1's line has his score (the run end adds only the turn rider's) and in
// ProQuest each reads "your score:". Co-op shows the single player line with
// the team's total, which counted for records and challenges, labelled "total
// score". Versus shows no score lines; every score is on PLAYERS SCORE.
void __cdecl onRunFinishedDraw() {
    if (g.players == 0 || !riders::secondActive()) return g_runFinishedDraw.original();
    riders::SeenRiders seen(g.coop ? 1 : 0);
    int32_t& shown = field<int32_t>(riderState(0), addr::rider::kSessionScore);
    const int32_t saved = shown;
    if (g.coop) shown = g.total;
    g.teamLine = g.coop;
    g_runFinishedDraw.original();
    g.teamLine = false;
    shown = saved;
}

}  // namespace

void runOver() {
    g.showFirst = false;
    g.players = 0;
    if (gameTypeFromSubMode(global<uint8_t>(addr::kSessionSubMode)) == GameType::Freeride) return;
    const int players = std::min(2 + extras::count(), kMaxPlayers);
    g.total = 0;
    for (int p = 0; p < players; ++p) {
        const uint8_t* r = extras::riderOf(p);
        g.scores[p] = r ? field<int32_t>(const_cast<uint8_t*>(r), addr::rider::kRunScore) : 0;
        g.total += g.scores[p];
        if (p == 0 || g.scores[p] > g.best) g.best = g.scores[p];
    }
    g.players = players;
    g.coop = scoring::teamScore();
    g.showFirst = true;
    BMX_LOG_INFO("playersscore: run over, %d players, %s %ld", players, g.coop ? "team total" : "versus, best",
                 static_cast<long>(g.coop ? g.total : g.best));
}

void levelEnd() {
    g.players = 0;
    g.showFirst = g.opening = false;
}

bool install() {
    bool ok = true;
    ok &= g_open.install(fn::ScreenOpen, &onScreenOpen, "ScreenOpen");
    ok &= g_getString.install(fn::GetString, &onGetString, "GetString");
    ok &= g_runFinishedCreate.install(fn::RunFinishedCreate, &onRunFinishedCreate, "RunFinishedCreate");
    ok &= g_runFinishedTick.install(fn::RunFinishedTick, &onRunFinishedTick, "RunFinishedTick");
    ok &= g_runFinishedDraw.install(fn::RunFinishedDraw, &onRunFinishedDraw, "RunFinishedDraw");
    if (ok) BMX_LOG_INFO("playersscore: ready");
    return ok;
}

}  // namespace bmx::playersscore
