#include "mod/hud.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/button_icons.hpp"
#include "mod/extra_riders.hpp"
#include "mod/low_memory.hpp"
#include "mod/render_state.hpp"
#include "mod/riders.hpp"
#include "mod/scoring.hpp"
#include "mod/splitscreen.hpp"

namespace bmx::hud {
namespace {

using namespace bmx::game;

constexpr uint32_t kScreenSpace = 0x2000000;  // zgl flag: the vertices are in screen pixels
constexpr uint32_t kSpriteSize = 0xA0;        // an overlay pool entry

// The HUD elements: one 0x18-byte entry per element at addr::kHudElements.
constexpr int kElements = 17;
constexpr uint32_t kElemFlags = 0x00;   // bit 0: hidden
constexpr uint32_t kElemShow = 0x04;    // (on) creates or destroys its objects
constexpr uint32_t kElemUpdate = 0x08;  // () per frame

// A player's HUD is a copy of the HUD's state (addr::hud); the two fonts
// inside are shared and stay in place.
using namespace bmx::game::addr::hud;
constexpr size_t kStateSize = kStateEnd - kStateBegin;
constexpr size_t kMenuTextsSize = 0x30;  // addr::kMenuTexts
// The u16 object handles the HUD elements keep, each reset to 0xFFFF by the
// HUD code.
constexpr uintptr_t kHandleFields[] = {kScoreText, kHandleA, kHandleB, kFadeBox, kHandleC, kHandleD, kTimerText};
constexpr uint32_t kMaxPoolEntries = 128 * kMaxPlayers;  // the biggest pool, enlarged

// The maker of an overlay object: the player whose HUD was in (0 player 1),
// or kShared for objects shown in every view.
using Owner = uint8_t;
constexpr Owner kShared = 0xFF;

// An overlay pool's two banks and its entry count.
struct Pool {
    uint8_t* base[2];
    uint32_t count;
};

struct State {
    const void* currentCamera = nullptr;
    std::array<uint8_t, 2> drawnViews{};  // per draw callback: bits of the views that drew this frame's table

    // While inside an overlay draw: the view drawn, whether its 2-D draws are
    // fitted to the view and whether primitives are filtered by owner.
    int view = -1;
    bool mapping = false;
    bool filtering = false;

    // The owner of each overlay object, by pool (handle bits 12..14) and
    // index.
    std::array<std::array<uint8_t, kMaxPoolEntries>, 8> owner{};
    std::array<Pool, 8> pools{};
    bool poolsKnown = false;
    Owner allocOwner = 0;

    // The HUD instances. The game's HUD state holds one at a time (`live`);
    // the others wait in `hud`, player 1's too while another's is in.
    bool hudUp = false;                          // the game's HUD is set up for this level
    std::array<uint8_t, kStateSize> snapshot{};  // the HUD state just before this level's HUD setup
    struct Instance {
        std::array<uint8_t, kStateSize> state{};
        std::array<uint32_t, kElements> flags{};  // the elements' shown/hidden flags
        bool ready = false;                       // made for this level (players 2 and up)
    };
    std::array<Instance, kMaxPlayers> hud{};
    int live = 0;
    std::array<int, 8> stack{};  // the HUDs begin() returns to
    int depth = 0;
    uint8_t turnOfPlayer1 = 0;
    uint32_t announced = 0;  // bits of the players whose HUD was logged

    // Immediate primitives (text made each frame: trick names, combo points,
    // meter labels) come from a per-frame buffer rather than the pools; these
    // are the parts of it the other players' HUDs filled.
    struct Range {
        uintptr_t begin, end;
        Owner owner;
    };
    std::array<Range, 128> ranges{};
    size_t rangeCount = 0;
    uintptr_t rangeStart = 0;
};
State g;

// HudSet ids whose next call is for the riders in slot 2.
uint32_t g_routed = 0;

using BoolFn = void(__cdecl*)(int on);
using BeginUpdateFn = int(__cdecl*)(const void* zrwCamera);
using DrawPrimFn = void(__cdecl*)(void* prim);
using Zgl2DFn = void(__cdecl*)(uint32_t flags, uint32_t* items, int count, uint32_t extra);
using PoolFn = void(__cdecl*)(int type, int size, uint32_t bank, uint8_t** base, uint32_t* count, uint32_t* kind);
using AllocFn = uint32_t(__cdecl*)(int type, int size);
using EnableFn = void(__cdecl*)(int element, uint32_t on);
using HandleFn = uint32_t(__cdecl*)(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
using HudSetFn = void(__cdecl*)(int id, void* data);

hook::Hook<BeginUpdateFn> g_beginUpdate;
hook::Hook<IntFn> g_overlayDrawA;
hook::Hook<IntFn> g_overlayDrawB;
hook::Hook<DrawPrimFn> g_drawPrim;
hook::Hook<Zgl2DFn> g_lines;
hook::Hook<Zgl2DFn> g_tris;
hook::Hook<Zgl2DFn> g_quads;
hook::Hook<AllocFn> g_alloc;
hook::Hook<VoidFn> g_hudLevelInit;
hook::Hook<VoidFn> g_hudLevelShutdown;
hook::Hook<EnableFn> g_hudEnable;
hook::Hook<VoidFn> g_overlayBeginA;
hook::Hook<VoidFn> g_overlayBeginB;
hook::Hook<HandleFn> g_ovDestroy, g_ovShow, g_ovMove, g_ovSet1, g_ovSet2, g_ovSet3;
hook::Hook<PoolFn> g_overlayPool;
hook::Hook<IntFn> g_overlayInit;

const auto OverlayPool = reinterpret_cast<PoolFn>(fn::OverlayPool);
const auto HudSet = reinterpret_cast<HudSetFn>(fn::HudSet);

// ------------------------------------------------------------ bigger pools --
// Two overlay pools are sized for one HUD: the boxes (16 entries), where a
// second HUD's meter marker and balance meter do not fit, and the text glyphs
// (128 entries, one per character), where a text that does not fit is not
// shown at all. Each gets room for every player's HUD.
//
// A pool is a PlayStation-style list of primitives (28-bit links in each
// tag) that the bank flip links into the ordering table. The mod's entries
// are spliced into the game's chain: the game's entries 0..n-2, the mod's,
// then the game's last entry, whose link the flip sets. Only OverlayPool, the
// pool init and the flip know the pools' addresses.
struct BiggerPool {
    int type, size;
    uintptr_t gameBase;
    int gameEntries;
    int entries;
    uint8_t* mem = nullptr;  // 2 banks in low memory

    uint8_t* bank(uint32_t b) const { return mem + (b & 1) * entries * kSpriteSize; }
    uint8_t* gameBank(uint32_t b) const {
        return reinterpret_cast<uint8_t*>(gameBase + (b & 1) * gameEntries * kSpriteSize);
    }
};
BiggerPool g_biggerPools[] = {
    {0, 0, addr::kBoxPool, 16, 16 * kMaxPlayers},
    {0, 1, addr::kGlyphPool, 128, 128 * kMaxPlayers},
};

// Sets the link of a primitive's tag.
void setNext(uint8_t* prim, const void* next) {
    uint32_t& tag = *reinterpret_cast<uint32_t*>(prim);
    tag = (tag & 0xF0000000u) | (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(next)) & 0x0FFFFFFFu);
}

// Frees each of the mod's entries as the game's pool init (0x4578F0) does
// (tag length 0, no next glyph, kind 0) and splices them into the chain.
void initBiggerPools() {
    for (const BiggerPool& pool : g_biggerPools) {
        if (!pool.mem) continue;
        for (uint32_t b = 0; b < 2; ++b) {
            uint8_t* ours = pool.bank(b);
            uint8_t* game = pool.gameBank(b);
            uint8_t* gameLast = game + (pool.gameEntries - 1) * kSpriteSize;
            for (int i = 0; i < pool.entries; ++i) {
                uint8_t* e = ours + i * kSpriteSize;
                *reinterpret_cast<uint32_t*>(e) &= 0x0FFFFFFFu;
                *reinterpret_cast<uint16_t*>(e + 0x9E) = 0xFFFF;
                e[0x9C] = 0;
                setNext(e, i + 1 < pool.entries ? e + kSpriteSize : gameLast);
            }
            setNext(gameLast - kSpriteSize, ours);
        }
    }
}

// OverlayPool: reports the bigger pools' banks and sizes.
void __cdecl onOverlayPool(int type, int size, uint32_t bank, uint8_t** base, uint32_t* count, uint32_t* kind) {
    g_overlayPool.original(type, size, bank, base, count, kind);
    for (const BiggerPool& pool : g_biggerPools)
        if (pool.mem && (type & 0xFF) == pool.type && (size & 0xFF) == pool.size) {
            *base = pool.bank(bank & 0xFF);
            *count = pool.entries;
        }
}

// OverlayInit: sets the bigger pools up after the game's.
int __cdecl onOverlayInit() {
    const int result = g_overlayInit.original();
    initBiggerPools();
    return result;
}

// ---------------------------------------------------------------- elements --
uint8_t* element(int i) { return reinterpret_cast<uint8_t*>(addr::kHudElements + i * 0x18); }

template <typename F>
F elementFn(int i, uint32_t offset) { return *reinterpret_cast<F*>(element(i) + offset); }

uint32_t& elementFlags(int i) { return *reinterpret_cast<uint32_t*>(element(i) + kElemFlags); }

bool hidden(int i) { return element(i)[kElemFlags] & 1; }

// Runs one function of every element: show(arg) or update().
void runElements(uint32_t offset, int arg, bool skipHidden) {
    for (int i = 0; i < kElements; ++i) {
        if (skipHidden && hidden(i)) continue;
        if (offset == kElemUpdate) {
            if (auto f = elementFn<VoidFn>(i, offset)) f();
        } else if (auto f = elementFn<BoolFn>(i, offset)) {
            f(arg);
        }
    }
}

// -------------------------------------------------------- object ownership --
// Reads every pool's banks and size once.
void learnPools() {
    if (g.poolsKnown) return;
    for (int key = 0; key < 8; ++key) {
        if (key & 2) continue;
        for (uint32_t bank = 0; bank < 2; ++bank) {
            uint8_t* base = nullptr;
            uint32_t count = 0, kind = 0;
            OverlayPool((key >> 2) & 1, key & 3, bank, &base, &count, &kind);
            g.pools[key].base[bank] = base;
            g.pools[key].count = std::min<uint32_t>(count, kMaxPoolEntries);
        }
    }
    g.poolsKnown = true;
}

uintptr_t immediateCursor() { return global<uintptr_t>(addr::kOverlayPrimCursor); }

// Records that `owner` made the immediate primitives in [begin, end).
void noteRange(Owner owner, uintptr_t begin, uintptr_t end) {
    if (end <= begin || begin == 0xFFFFFFFF) return;
    if (g.rangeCount > 0) {
        auto& last = g.ranges[g.rangeCount - 1];
        if (last.owner == owner && last.end == begin) {
            last.end = end;
            return;
        }
    }
    if (g.rangeCount < g.ranges.size()) g.ranges[g.rangeCount++] = {begin, end, owner};
}

// OverlayBeginA/B: the per-frame buffer is rewound, and with it the ranges.
template <hook::Hook<VoidFn>& H>
void __cdecl onOverlayBegin() {
    H.original();
    g.rangeCount = 0;
}

// The owner of a primitive: by the immediate ranges, else by its pool entry.
Owner ownerOf(const void* prim) {
    const uint8_t* p = static_cast<const uint8_t*>(prim);
    const auto a = reinterpret_cast<uintptr_t>(prim);
    for (size_t i = 0; i < g.rangeCount; ++i)
        if (a >= g.ranges[i].begin && a < g.ranges[i].end) return g.ranges[i].owner;
    for (int key = 0; key < 8; ++key) {
        const Pool& pool = g.pools[key];
        for (int bank = 0; bank < 2; ++bank) {
            const uint8_t* base = pool.base[bank];
            if (!base || p < base || p >= base + pool.count * kSpriteSize) continue;
            return g.owner[key][(p - base) / kSpriteSize];
        }
    }
    return 0;
}

// OverlayAlloc: records the owner of each new object.
uint32_t __cdecl onAlloc(int type, int size) {
    const uint32_t handle = g_alloc.original(type, size);
    if ((handle & 0xFFFF) != 0xFFFF && (handle & 0xFFF) < kMaxPoolEntries)
        g.owner[(handle >> 12) & 7][handle & 0xFFF] = g.allocOwner;
    return handle;
}

// Player 1's HUD and the shared objects form one group.
Owner group(Owner o) { return o == kShared ? 0 : o; }

// Returns whether the HUD in may change the object `handle`: only objects of
// its own group, since its state can still hold player 1's handles. Handle
// 0xFFFF is refused, as the game's functions write through a wrong address
// when given it.
bool mayTouch(uint32_t handle) {
    if ((handle & 0xFFFF) == 0xFFFF) return false;
    learnPools();
    const uint32_t key = (handle >> 12) & 7;
    if ((handle & 0xFFF) >= g.pools[key].count) return false;
    const uint8_t o = g.owner[key][handle & 0xFFF];
    return group(o) == group(g.allocOwner);
}

int g_blockedLogged = 0;

// The overlay functions that change an object by handle: skipped for another
// group's objects.
template <hook::Hook<HandleFn>& H>
uint32_t __cdecl onHandleCall(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    if (!mayTouch(handle)) {
        if (group(g.allocOwner) != 0 && (handle & 0xFFFF) != 0xFFFF && g_blockedLogged < 24) {
            ++g_blockedLogged;
            BMX_LOG_INFO("hud: player %d's HUD tried %s on another player's object %04X (caller 0x%08X)",
                         g.allocOwner + 1, H.name(), handle & 0xFFFF,
                         static_cast<unsigned>(BMX_RETURN_ADDRESS()));
        }
        return 0;
    }
    return H.original(handle, a, b, c, d);
}

// OverlayDrawPrim: each view draws its player's objects (view n follows
// player n) and the shared ones; unsplit, player 1's.
void __cdecl onDrawPrim(void* prim) {
    if (g.filtering) {
        const Owner o = ownerOf(prim);
        if (o != kShared && o != (g.view > 0 ? g.view : 0)) return;
    }
    g_drawPrim.original(prim);
}

// ----------------------------------------------------------- HUD instances --
void copyState(uint8_t* to, uintptr_t from) { std::memcpy(to, reinterpret_cast<void*>(from), kStateSize); }

// Copies `from` into the live HUD state, except the shared fonts.
void loadState(const uint8_t* from) {
    uint8_t fonts[2][kFontSize];
    std::memcpy(fonts[0], reinterpret_cast<void*>(kFontA), kFontSize);
    std::memcpy(fonts[1], reinterpret_cast<void*>(kFontB), kFontSize);
    std::memcpy(reinterpret_cast<void*>(kStateBegin), from, kStateSize);
    std::memcpy(reinterpret_cast<void*>(kFontA), fonts[0], kFontSize);
    std::memcpy(reinterpret_cast<void*>(kFontB), fonts[1], kFontSize);
}

bool anyOtherReady() {
    for (int p = 1; p < kMaxPlayers; ++p)
        if (g.hud[p].ready) return true;
    return false;
}

// Puts player `p`'s HUD in the game's HUD state. Some HUD code reads the turn
// rider, so the other players' HUDs run with the turn on slot 2, where their
// rider is meanwhile. Texts take the button font's texture when they are
// made, so the player's icons go in too.
void switchTo(int p) {
    if (p == g.live) return;
    State::Instance& from = g.hud[g.live];
    State::Instance& to = g.hud[p];
    if (g.live != 0) noteRange(static_cast<Owner>(g.live), g.rangeStart, immediateCursor());
    copyState(from.state.data(), kStateBegin);
    for (int i = 0; i < kElements; ++i) {
        from.flags[i] = elementFlags(i);
        elementFlags(i) = to.flags[i];
    }
    loadState(to.state.data());
    uint8_t& turn = global<uint8_t>(addr::kCurrentTurnPlayer);
    if (g.live == 0) g.turnOfPlayer1 = turn;
    turn = p == 0 ? g.turnOfPlayer1 : 1;
    g.live = p;
    g.allocOwner = static_cast<Owner>(p);
    g.rangeStart = immediateCursor();
    buttonicons::setActivePlayer(p);
}

// Makes player p's HUD (p >= 1, his rider in slot 2). It starts from the HUD
// state of just before this level's HUD setup with the setup's resets, then,
// in the order of the game's setup, creates all its objects, hides them all
// and shows the elements player 1's HUD shows. It may only change its own
// objects, so the stale handles it starts with are harmless.
void create(int p) {
    State::Instance& hud = g.hud[p];
    if (hud.ready || !g.hudUp || g.live != 0) return;
    learnPools();
    hud.state = g.snapshot;
    auto at = [&](uintptr_t a) { return &hud.state[a - kStateBegin]; };
    for (uintptr_t h : kHandleFields) *reinterpret_cast<uint16_t*>(at(h)) = 0xFFFF;
    *reinterpret_cast<uint16_t*>(at(kFadeFrames)) = 0;
    std::memset(at(addr::kMenuTexts), 0xFF, kMenuTextsSize);
    *at(kResetByte) = 0xFF;
    *at(addr::kGameScreen) = 0;
    *reinterpret_cast<uint32_t*>(at(addr::kGameScreenPtr)) = 0;
    for (int i = 0; i < kElements; ++i) hud.flags[i] = elementFlags(i) & ~1u;
    hud.ready = true;
    begin(p);
    runElements(kElemShow, 1, false);
    for (int i = 0; i < kElements; ++i) g_hudEnable.original(i, 0);
    for (int i = 0; i < kElements; ++i)
        if ((g.hud[0].flags[i] & 1) == 0) g_hudEnable.original(i, 1);
    // The run-start reset came before this HUD existed: the rider's score is
    // shown at once, without counting up, and no trick name.
    struct {
        int32_t score;
        uint8_t snap;
    } score{field<int32_t>(riderState(1), addr::rider::kRunScore), 1};
    HudSet(1, &score);
    uint8_t noTrick = 0xFF;
    HudSet(8, &noTrick);
    end();
    if (!(g.announced & (1u << p))) {
        g.announced |= 1u << p;
        BMX_LOG_INFO("hud: player %d has his own HUD", p + 1);
    }
}

// Destroys the objects of every other player's HUD.
void destroyAll() {
    while (g.depth > 0) end();
    for (int p = 1; p < kMaxPlayers; ++p) {
        if (!g.hud[p].ready) continue;
        begin(p);
        runElements(kElemShow, 0, false);
        end();
        g.hud[p].ready = false;
    }
}

// HudLevelInit: keeps the HUD state of just before the setup for the other
// players' HUDs.
void __cdecl onHudLevelInit() {
    destroyAll();
    global<uint16_t>(kFadeFrames) = 0;
    copyState(g.snapshot.data(), kStateBegin);
    g_hudLevelInit.original();
    g.hudUp = true;
}

void __cdecl onHudLevelShutdown() {
    buttonicons::restoreOriginal();
    destroyAll();
    g.hudUp = false;
    g_hudLevelShutdown.original();
}

// HudEnable: an element the game shows or hides on player 1's HUD is shown or
// hidden on the other players' HUDs too, whose objects are their own.
void __cdecl onHudEnable(int i, uint32_t on) {
    const bool before = !hidden(i);
    g_hudEnable.original(i, on);
    const bool after = !hidden(i);
    if (before == after || g.live != 0) return;
    for (int p = 1; p < kMaxPlayers; ++p) {
        if (!g.hud[p].ready) continue;
        begin(p);
        g_hudEnable.original(i, after ? 1 : 0);
        end();
    }
}

// ----------------------------------------------------------------- mapping --
// While a view draws the HUD, the 2-D draws' vertices are fitted into the
// view and put back after each draw.
struct SavedVertex {
    float* v;
    float x, y;
};
std::array<SavedVertex, 256> g_saved;
size_t g_savedCount = 0;

bool alreadySaved(const float* v) {
    for (size_t i = 0; i < g_savedCount; ++i)
        if (g_saved[i].v == v) return true;
    return false;
}

// How one axis of a 2-D quad is fitted into the view: its centre `from`
// moves to `centre` and it is scaled by `k` around it.
struct AxisFit {
    float from, centre, k;
};

// Fits [lo, hi] on an axis of the screen (`full` long) into the view's
// [origin, origin + size]. `s` is the uniform scale for what keeps its shape
// (glyphs). Full-screen boxes fill the view. With `evenSpacing` positions map
// linearly, which keeps tall layouts such as the height meter straight;
// otherwise items stay centred (mapItems pins text runs instead when the view
// is wider than the scaled HUD).
AxisFit fitAxisRange(float lo, float hi, float full, float origin, float size, float s, bool evenSpacing) {
    const float extent = hi - lo, c = (lo + hi) / 2, linear = size / full;
    if (extent >= full * 0.9f) return {c, origin + c * linear, linear};
    if (std::fabs(size - full * s) < 1.0f) return {c, origin + c * s, s};
    if (evenSpacing) return {c, origin + c * linear, extent < full * 0.1f ? s : linear};
    return {c, origin + size / 2 + (c - full / 2) * s, s};
}

// Pins a coordinate to the nearest edge or the middle of the view.
float pinAxis(float c, float full, float origin, float size, float s) {
    if (c < full / 3) return origin + c * s;
    if (c < full * 2 / 3) return origin + size / 2 + (c - full / 2) * s;
    return origin + size - (full - c) * s;
}

// When the view is wider than the scaled HUD (stacked split), items are
// pinned to the left edge, the middle or the right edge. To keep words
// together, the overlay is first drawn without output to measure every quad;
// quads side by side on one line form a run, and a run is pinned by its
// middle. Runs go by position, not drawing order, as a text's glyphs sit
// wherever its pool had free entries.
struct Box {
    float x0, x1, y0, y1;
};
constexpr size_t kMaxItems = 4096;
std::vector<Box> g_boxes;        // sized at install
std::vector<float> g_runCentre;  // per quad: the centre x of its run
std::vector<uint32_t> g_order;   // the quads by line, then left to right
size_t g_boxCount = 0;
size_t g_itemIndex = 0;
bool g_measuring = false;
bool g_runsValid = false;

// Groups the measured quads into runs and records each run's centre.
void computeRuns(float W) {
    const auto height = [](const Box& b) { return b.y1 - b.y0; };
    const auto sameLine = [&](const Box& a, const Box& b) {
        const float h = std::max(height(a), height(b));
        return std::fabs(a.y0 - b.y0) <= h * 0.35f && std::fabs(height(a) - height(b)) <= h * 0.35f;
    };
    const auto small = [&](const Box& b) { return b.x1 - b.x0 < W * 0.5f; };
    const auto at = [&](size_t i) -> const Box& { return g_boxes[g_order[i]]; };

    for (size_t i = 0; i < g_boxCount; ++i) g_order[i] = static_cast<uint32_t>(i);
    std::sort(g_order.begin(), g_order.begin() + g_boxCount,
              [](uint32_t a, uint32_t b) { return g_boxes[a].y0 < g_boxes[b].y0; });

    for (size_t line = 0; line < g_boxCount;) {
        // A line: the quads at the height of its first one. Texts of another
        // size may share it; runs join only quads alike.
        size_t end = line + 1;
        while (end < g_boxCount && at(end).y0 - at(line).y0 <= height(at(line)) * 0.35f) ++end;
        std::sort(g_order.begin() + line, g_order.begin() + end,
                  [](uint32_t a, uint32_t b) { return g_boxes[a].x0 < g_boxes[b].x0; });
        // A run: small quads that touch, overlap or are close. Big texts have
        // a shadow copy a few pixels off, overlapping every glyph.
        for (size_t run = line; run < end;) {
            size_t last = run + 1;
            float runRight = at(run).x1;
            while (last < end && small(at(last - 1)) && small(at(last)) && sameLine(at(last - 1), at(last))) {
                const float h = std::max(height(at(last - 1)), height(at(last)));
                if (at(last).x0 - runRight > h * 2.0f) break;
                runRight = std::max(runRight, at(last).x1);
                ++last;
            }
            float lo = 1e9f, hi = -1e9f;
            for (size_t k = run; k < last; ++k) {
                lo = std::min(lo, at(k).x0);
                hi = std::max(hi, at(k).x1);
            }
            for (size_t k = run; k < last; ++k) g_runCentre[g_order[k]] = (lo + hi) / 2;
            run = last;
        }
        line = end;
    }
}

// Fits the vertices of `count` items of `verts` vertices into the view,
// saving the originals.
void mapItems(uint32_t* items, int count, int verts) {
    const float W = static_cast<float>(screenWidth()), H = static_cast<float>(screenHeight());
    const split::ViewRect r = split::viewRect(g.view);
    if (W <= 0 || H <= 0 || r.w <= 0 || r.h <= 0) return;
    const float s = std::min(r.w / W, r.h / H);
    const int stride = verts + 1;

    for (int i = 0; i < count; ++i) {
        float* v[4] = {};
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        int n = 0;
        for (int k = 0; k < verts; ++k) {
            float* p = reinterpret_cast<float*>(items[i * stride + 1 + k]);
            if (!p || alreadySaved(p) || g_savedCount >= g_saved.size()) continue;
            g_saved[g_savedCount++] = {p, p[0], p[1]};
            v[n++] = p;
            minX = std::min(minX, p[0]);
            maxX = std::max(maxX, p[0]);
            minY = std::min(minY, p[1]);
            maxY = std::max(maxY, p[1]);
        }
        const size_t index = g_itemIndex++;
        if (n == 0) continue;
        AxisFit fx = fitAxisRange(minX, maxX, W, r.x, r.w, s, false);
        const bool slack = std::fabs(r.w - W * s) >= 1.0f && maxX - minX < W * 0.9f;
        if (slack) {
            const float own = (minX + maxX) / 2;
            const float runC = (g_runsValid && index < g_boxCount) ? g_runCentre[index] : own;
            fx = {own, pinAxis(runC, W, r.x, r.w, s) + (own - runC) * s, s};
        }
        const AxisFit fy = fitAxisRange(minY, maxY, H, r.y, r.h, s, true);
        for (int k = 0; k < n; ++k) {
            v[k][0] = fx.centre + (v[k][0] - fx.from) * fx.k;
            v[k][1] = fy.centre + (v[k][1] - fy.from) * fy.k;
        }
    }
}

void restoreVertices() {
    for (size_t i = 0; i < g_savedCount; ++i) {
        g_saved[i].v[0] = g_saved[i].x;
        g_saved[i].v[1] = g_saved[i].y;
    }
    g_savedCount = 0;
}

// Zgl2DLines/Tris/Quads: in screen space while a view draws the HUD, the
// items are measured (first pass) or fitted into the view.
template <hook::Hook<Zgl2DFn>& H, int Verts>
void __cdecl on2D(uint32_t flags, uint32_t* items, int count, uint32_t extra) {
    if (!g.mapping || !(flags & kScreenSpace) || !items || count <= 0) {
        H.original(flags, items, count, extra);
        return;
    }
    if (g_measuring) {
        const int stride = Verts + 1;
        for (int i = 0; i < count && g_boxCount < kMaxItems; ++i) {
            Box b{1e9f, -1e9f, 1e9f, -1e9f};
            for (int k = 0; k < Verts; ++k) {
                const float* p = reinterpret_cast<const float*>(items[i * stride + 1 + k]);
                if (!p) continue;
                b.x0 = std::min(b.x0, p[0]);
                b.x1 = std::max(b.x1, p[0]);
                b.y0 = std::min(b.y0, p[1]);
                b.y1 = std::max(b.y1, p[1]);
            }
            g_boxes[g_boxCount++] = b;
        }
        return;
    }
    g_savedCount = 0;
    mapItems(items, count, Verts);
    H.original(flags, items, count, extra);
    restoreVertices();
}

// ------------------------------------------------------------ overlay draw --
// ZrwCameraBeginUpdate: remembers the camera being drawn.
int __cdecl onBeginUpdate(const void* zrwCamera) {
    g.currentCamera = zrwCamera;
    return g_beginUpdate.original(zrwCamera);
}

// OverlayDrawA/B: draws the ordering table in every view, fitted to it and
// filtered to its player's objects, keeping the pending flag set until all
// views have drawn it. With two riders the shadows are depth-tested and drawn
// after this, so the goal glow (3-D primitives in the table) must not write
// depth.
int overlayDraw(hook::Hook<IntFn>& h, uintptr_t pendingAddr, int which) {
    int& pending = global<int>(pendingAddr);
    const int wasPending = pending;
    const int view = split::viewOf(g.currentCamera);
    learnPools();
    g.view = view;
    g.mapping = view >= 0;
    g.filtering = anyOtherReady() || view >= 0;

    g_runsValid = false;
    if (view >= 0 && wasPending) {
        const float W = static_cast<float>(screenWidth()), H = static_cast<float>(screenHeight());
        const split::ViewRect r = split::viewRect(view);
        const float s = (W > 0 && H > 0) ? std::min(r.w / W, r.h / H) : 1.0f;
        if (W > 0 && std::fabs(r.w - W * s) >= 1.0f) {
            g_measuring = true;
            g_boxCount = 0;
            h.original();
            g_measuring = false;
            pending = wasPending;
            computeRuns(W);
            g_runsValid = true;
        }
    }
    g_itemIndex = 0;
    const bool noDepthWrite = riders::secondActive();
    if (noDepthWrite) render::beginNoDepthWrite();
    const int result = h.original();
    if (noDepthWrite) render::endNoDepthWrite();
    g_runsValid = false;
    g.filtering = false;
    g.mapping = false;
    g.view = -1;

    if (view >= 0 && wasPending) {
        g.drawnViews[which] |= static_cast<uint8_t>(1 << view);
        const uint8_t all = static_cast<uint8_t>((1 << split::viewCount()) - 1);
        if ((g.drawnViews[which] & all) != all) pending = wasPending;
    }
    return result;
}

int __cdecl onOverlayDrawA() { return overlayDraw(g_overlayDrawA, addr::kOverlayPendingA, 0); }
int __cdecl onOverlayDrawB() { return overlayDraw(g_overlayDrawB, addr::kOverlayPendingB, 1); }

// ------------------------------------------------------------ meter marker --
// The height meter's marker is the last of the meter's boxes. When a player's
// HUD found no free box for it, it is drawn as an immediate box each frame
// where the meter would put it.
constexpr int kMeterElement = 5;
bool g_markerLogged = false;

void drawMarkerIfMissing() {
    if (global<uint16_t>(kMeterMarker) != 0xFFFF) return;
    if (hidden(kMeterElement)) return;
    if (global<float>(kMeterHeight) == 0.0f) return;
    const int k = static_cast<int>((global<float>(kMeterValue) - global<float>(kMeterBase)) * global<float>(addr::kMeterScale));
    using BoxFn = void(__cdecl*)(int x, int y, int w, int h, uint32_t rgb);
    reinterpret_cast<BoxFn>(fn::OverlayImmediateBox)(0x213, 0x17B - k, 0x10, 2, 0xFFFFFF);
    if (!g_markerLogged) {
        g_markerLogged = true;
        BMX_LOG_INFO("hud: a meter marker is drawn directly (no free HUD box)");
    }
}

// -------------------------------------------------------------- team total --
// In co-op the team's total score is shown in green under each player's own,
// summed from every HUD's shown score so it counts up with them.
constexpr int kScoreElement = 1;
constexpr int kScoreRight = 0x24B, kScoreY = 0x190;  // the game's score position: right edge, y
constexpr uint32_t kTeamColour = 0x50FF50;           // the same in RGB and BGR order
bool g_teamLogged = false;

void drawTeamTotal(int32_t total) {
    if (hidden(kScoreElement)) return;
    char text[24];
    std::snprintf(text, sizeof text, "%ld", static_cast<long>(total));
    using WidthFn = uint16_t(__cdecl*)(const char* text, const void* font);
    using TextFn = void(__cdecl*)(const void* font, const char* text, int x, int y, uint32_t rgb, int flags);
    const void* font = reinterpret_cast<const void*>(addr::kHudFont);
    const int width = reinterpret_cast<WidthFn>(fn::OverlayTextWidth)(text, font);
    const int height = std::clamp<int>(reinterpret_cast<const uint8_t*>(font)[5], 8, 32);
    reinterpret_cast<TextFn>(fn::OverlayImmediateText)(font, text, kScoreRight - width, kScoreY + height + 2,
                                                       kTeamColour, 0);
}

}  // namespace

bool ready(int player) {
    if (player == 0) return g.hudUp;
    return player > 0 && player < kMaxPlayers && g.hud[player].ready;
}

int live() { return g.live; }

void begin(int player) {
    if (g.depth >= static_cast<int>(g.stack.size())) return;
    g.stack[g.depth++] = g.live;
    if (ready(player)) switchTo(player);
}

void end() {
    if (g.depth == 0) return;
    switchTo(g.stack[--g.depth]);
}

bool slot2Ready() { return ready(extras::slotPlayer()); }
void beginSlot2() { begin(extras::slotPlayer()); }
void endSlot2() { end(); }

void beginFrame() { g.drawnViews = {}; }

// Makes the HUDs of riders on the course, runs every other player's elements
// (with the session's timer) and draws the co-op total, except under an
// in-game screen, where PLAYERS SCORE shows its own.
void update() {
    if (!riders::secondActive()) {
        destroyAll();
        return;
    }
    const int players = 2 + extras::count();
    for (int p = 1; p < players; ++p) extras::asPlayer(p, [p] { create(p); });
    g_routed = 0;
    if (!anyOtherReady()) return;

    int32_t total = global<int32_t>(kScoreShown);
    for (int p = 1; p < players; ++p) {
        State::Instance& hud = g.hud[p];
        if (!hud.ready) continue;
        std::memcpy(&hud.state[kTimerValue - kStateBegin], reinterpret_cast<void*>(kTimerValue), 4);
        extras::asPlayer(p, [p] {
            begin(p);
            runElements(kElemUpdate, 0, true);
            drawMarkerIfMissing();
            end();
        });
        total += *reinterpret_cast<const int32_t*>(&hud.state[kScoreShown - kStateBegin]);
    }

    if (scoring::teamScore() && global<uint8_t>(addr::kGameScreen) == 0) {
        drawTeamTotal(total);
        for (int p = 1; p < players; ++p) {
            if (!g.hud[p].ready) continue;
            begin(p);
            drawTeamTotal(total);
            end();
        }
        if (!g_teamLogged) {
            g_teamLogged = true;
            BMX_LOG_INFO("hud: co-op, showing the team's total score");
        }
    }
}

void levelEnd() {
    buttonicons::restoreOriginal();
    destroyAll();
}

void routeNextToRiders(int id) {
    if (id >= 0 && id < 32) g_routed |= 1u << id;
}

bool takeRoute(int id) {
    if (id < 0 || id >= 32 || !(g_routed & (1u << id))) return false;
    g_routed &= ~(1u << id);
    return true;
}

bool install() {
    bool ok = true;
    ok &= g_beginUpdate.install(fn::ZrwCameraBeginUpdate, &onBeginUpdate, "ZrwCameraBeginUpdate");
    ok &= g_overlayDrawA.install(fn::OverlayDrawA, &onOverlayDrawA, "OverlayDrawA");
    ok &= g_overlayDrawB.install(fn::OverlayDrawB, &onOverlayDrawB, "OverlayDrawB");
    ok &= g_drawPrim.install(fn::OverlayDrawPrim, &onDrawPrim, "OverlayDrawPrim");
    ok &= g_lines.install(fn::Zgl2DLines, &on2D<g_lines, 2>, "Zgl2DLines");
    ok &= g_tris.install(fn::Zgl2DTris, &on2D<g_tris, 3>, "Zgl2DTris");
    ok &= g_quads.install(fn::Zgl2DQuads, &on2D<g_quads, 4>, "Zgl2DQuads");
    ok &= g_alloc.install(fn::OverlayAlloc, &onAlloc, "OverlayAlloc");
    ok &= g_hudLevelInit.install(fn::HudLevelInit, &onHudLevelInit, "HudLevelInit");
    ok &= g_hudLevelShutdown.install(fn::HudLevelShutdown, &onHudLevelShutdown, "HudLevelShutdown");
    ok &= g_hudEnable.install(fn::HudEnable, &onHudEnable, "HudEnable");
    ok &= g_overlayBeginA.install(fn::OverlayBeginA, &onOverlayBegin<g_overlayBeginA>, "OverlayBeginA");
    ok &= g_overlayBeginB.install(fn::OverlayBeginB, &onOverlayBegin<g_overlayBeginB>, "OverlayBeginB");
    ok &= g_ovDestroy.install(fn::OverlayDestroy, &onHandleCall<g_ovDestroy>, "OverlayDestroy");
    ok &= g_ovShow.install(fn::OverlayShow, &onHandleCall<g_ovShow>, "OverlayShow");
    ok &= g_ovMove.install(fn::OverlayMove, &onHandleCall<g_ovMove>, "OverlayMove");
    ok &= g_ovSet1.install(fn::OverlaySetA, &onHandleCall<g_ovSet1>, "OverlaySetA");
    ok &= g_ovSet2.install(fn::OverlaySetB, &onHandleCall<g_ovSet2>, "OverlaySetB");
    ok &= g_ovSet3.install(fn::OverlaySetC, &onHandleCall<g_ovSet3>, "OverlaySetC");
    if (!ok) return false;
    bool anyBigger = false;
    for (BiggerPool& pool : g_biggerPools) {
        pool.mem = lowmem::alloc(2 * pool.entries * kSpriteSize);
        anyBigger |= pool.mem != nullptr;
        if (pool.mem)
            BMX_LOG_INFO("hud: pool (%d, %d) has %d entries (the game's has %d)", pool.type, pool.size, pool.entries,
                         pool.gameEntries);
        else
            BMX_LOG_WARN("hud: no low memory for a bigger pool (%d, %d), some players' HUDs may be missing",
                         pool.type, pool.size);
    }
    if (anyBigger) {
        // The game may have set its pools up already.
        initBiggerPools();
        ok &= g_overlayPool.install(fn::OverlayPool, &onOverlayPool, "OverlayPool");
        ok &= g_overlayInit.install(fn::OverlayInit, &onOverlayInit, "OverlayInit");
        if (!ok) return false;
    }
    g_boxes.resize(kMaxItems);
    g_runCentre.resize(kMaxItems);
    g_order.resize(kMaxItems);
    BMX_LOG_INFO("hud: ready");
    return true;
}

}  // namespace bmx::hud
