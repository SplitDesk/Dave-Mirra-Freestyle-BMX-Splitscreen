#include "mod/rider_select.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/menu_buttons.hpp"
#include "core/pad_mapping.hpp"
#include "core/select_panels.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/frontend.hpp"
#include "mod/input.hpp"

namespace bmx::riderselect {
namespace {

using namespace bmx::game;

// Every player gets his own copy of the game's select screen (0xA009) with
// its steps: rider, bike, style. A copy is the screen's state (its globals,
// callbacks and images, and the bytes of its elements), kept here and swapped
// in for its player's input, tick and draw. Each copy draws into its own part
// of the screen: its primitives are moved and scaled there. Player 1's copy is
// the game's own.
//
// The GUI finds assets by id in one list, and a group's assets live in the
// group's memory, so two copies' groups (rider photos, bikes, a rider's
// styles) cannot both be in the list: an asset already there is not made
// again, and the newer group's copy of it would have no picture. Only the
// copy swapped in has its slot-3 assets in the list; the others keep theirs
// aside. The game's 128 asset handles are too few for several copies, so the
// mod adds spare ones when they run out.
//
// A player who finished his last step (style) is ready; once everyone is,
// player 1's copy goes on to the game's level step.

constexpr int kMaxCopies = kMaxPlayers;
constexpr int kMaxElements = 64;
// The step titles; "PRESS TO JOIN" and "READY" are drawn in their look.
constexpr uint32_t kTitles[] = {0xA0090012, 0xA0090013, 0xA0090014};

using GuiHandlerFn = int(__cdecl*)(int event, int a, int b, int c);
using MoveFn = void(__cdecl*)(int dir);

hook::Hook<IntFn> g_selectEnter;
hook::Hook<GuiHandlerFn> g_selectHandler;
hook::Hook<VoidFn> g_selectLeave;
hook::Hook<MoveFn> g_selectMove;
hook::Hook<uint8_t*(__cdecl*)()> g_allocAsset;

const auto FindElement = reinterpret_cast<uint8_t*(__cdecl*)(uint32_t id)>(fn::GuiFindElement);
const auto SetText = reinterpret_cast<void(__cdecl*)(void* element, const char* text)>(fn::GuiSetText);
const auto SendElement = reinterpret_cast<int(__cdecl*)(int event, void* element, int a, int b)>(fn::SendElement);
const auto MemAlloc = reinterpret_cast<uint32_t(__cdecl*)(uint32_t size)>(fn::MemAlloc);
const auto MemFree = reinterpret_cast<void(__cdecl*)(uint32_t handle)>(fn::MemFree);
const auto MemLock = reinterpret_cast<uint8_t*(__cdecl*)(uint32_t handle)>(fn::MemLock);
const auto MemUnlock = reinterpret_cast<void(__cdecl*)(uint32_t handle)>(fn::MemUnlock);
const auto FreeImage = reinterpret_cast<void(__cdecl*)(uintptr_t image)>(fn::GuiFreeImage);
const auto SetClip = reinterpret_cast<void(__cdecl*)(int x0, int y0, int x1, int y1)>(fn::GuiSetClip);
const auto UnloadGroup = reinterpret_cast<void(__cdecl*)(uint32_t slot)>(fn::ZPrimUnloadGroup);

constexpr int kEventDrawElement = 0x0C;
constexpr uint32_t kElemText = 0xD8, kElemTextSize = 0xDC;  // element: text memory handle and size

// One copy of the screen's state.
struct Copy {
    bool used = false;
    bool ready = false;
    std::array<uint8_t, addr::kSelectStateSize> state{};
    std::array<uint8_t, 0x10> callbacks{};
    std::array<uint8_t, addr::kSelectImagesSize> images{};
    int32_t glowStep = 1;  // the direction of the yellow frames' glow (+1 or -1)
    std::vector<uint8_t> elements;
    uintptr_t group3 = 0;
    std::vector<uint8_t*> assets;  // group 3's assets while another copy is swapped in
    MenuButtons buttons;
};

struct State {
    bool active = false;     // the multiplayer select screen is up
    bool levelStep = false;  // player 1 is on the level step or past it, his screen alone
    int live = 0;            // the copy in the game's memory
    int leaving = -1;        // a player who backed out of his rider, removed after his turn
    std::array<Copy, kMaxCopies> copy{};
    // The screen's elements, and their bytes and state as the screen made
    // them, which a new copy starts from.
    std::array<uint8_t*, kMaxElements> elements{};
    int elementCount = 0;
    std::vector<uint8_t> pristineElements;
    std::array<uint8_t, addr::kSelectStateSize> pristineState{};
    // What players 3 and up picked; players 1 and 2 go through PlayerConfig.
    std::array<uint8_t, 3 * kMaxPlayers> picked{};
    uint32_t pickedMask = 0;
    uint32_t ticks = 0;
    bool unknownPrimLogged = false;
};
State g;

// The asset group loaded in a GUI group slot.
uintptr_t& groupSlot(int slot) { return reinterpret_cast<uintptr_t*>(addr::kZPrimSlots)[slot]; }
int players() { return std::max(1, frontend::joinedPlayers()); }

// -------------------------------------------------------------- the copies --
uint8_t*& nextAsset(uint8_t* asset) { return field<uint8_t*>(asset, 4); }

// Spare asset handles. When the game's free list runs dry, the spare handles
// not in use (neither in the list nor set aside by a copy) are added to it.
constexpr int kSpareAssets = 1024;
alignas(16) uint8_t g_spareAssets[kSpareAssets][16];

void addSpareAssets() {
    std::vector<bool> used(kSpareAssets);
    auto mark = [&](uint8_t* asset) {
        const auto at = (asset - g_spareAssets[0]) / 16;
        if (at >= 0 && at < kSpareAssets) used[at] = true;
    };
    for (uint8_t* a = global<uint8_t*>(addr::kGuiAssetList); a; a = nextAsset(a)) mark(a);
    for (const Copy& c : g.copy)
        for (uint8_t* a : c.assets) mark(a);
    uint8_t*& free = global<uint8_t*>(addr::kGuiAssetFree);
    int added = 0;
    for (int i = 0; i < kSpareAssets; ++i)
        if (!used[i]) {
            std::memset(g_spareAssets[i], 0, 16);
            nextAsset(g_spareAssets[i]) = free;
            free = g_spareAssets[i];
            ++added;
        }
    BMX_LOG_INFO("riderselect: the game's GUI asset handles ran out, %d more", added);
}

// GuiAssetAlloc: refills the free list from the spares when it is empty.
uint8_t* __cdecl onAllocAsset() {
    if (!global<uint8_t*>(addr::kGuiAssetFree)) addSpareAssets();
    return g_allocAsset.original();
}

// Takes the assets in group `group`'s memory, the group too, out of the list.
std::vector<uint8_t*> detachAssets(uintptr_t group) {
    std::vector<uint8_t*> out;
    if (!group) return out;
    const uint32_t memory = field<uint32_t>(reinterpret_cast<uint8_t*>(group), 8);
    for (uint8_t** link = &global<uint8_t*>(addr::kGuiAssetList); *link;) {
        uint8_t* asset = *link;
        if (field<uint32_t>(asset, 8) == memory) {
            *link = nextAsset(asset);
            out.push_back(asset);
        } else {
            link = &nextAsset(asset);
        }
    }
    return out;
}

// Puts assets taken out by detachAssets back at the head of the list.
void attachAssets(std::vector<uint8_t*>& assets) {
    uint8_t*& head = global<uint8_t*>(addr::kGuiAssetList);
    for (auto it = assets.rbegin(); it != assets.rend(); ++it) {
        nextAsset(*it) = head;
        head = *it;
    }
    assets.clear();
}

// Copies the screen's live state into `c`, or `c` into it.
void save(Copy& c) {
    std::memcpy(c.state.data(), reinterpret_cast<void*>(addr::kSelectState), c.state.size());
    std::memcpy(c.callbacks.data(), reinterpret_cast<void*>(addr::kSelectCallbacks), c.callbacks.size());
    std::memcpy(c.images.data(), reinterpret_cast<void*>(addr::kSelectImages), c.images.size());
    c.glowStep = global<int32_t>(addr::kGlowStep);
    c.elements.resize(g.elementCount * addr::kGuiElementSize);
    for (int i = 0; i < g.elementCount; ++i)
        std::memcpy(&c.elements[i * addr::kGuiElementSize], g.elements[i], addr::kGuiElementSize);
    c.group3 = groupSlot(addr::kSelectPortraitGroup);
    c.assets = detachAssets(c.group3);
}

void load(Copy& c) {
    std::memcpy(reinterpret_cast<void*>(addr::kSelectState), c.state.data(), c.state.size());
    std::memcpy(reinterpret_cast<void*>(addr::kSelectCallbacks), c.callbacks.data(), c.callbacks.size());
    std::memcpy(reinterpret_cast<void*>(addr::kSelectImages), c.images.data(), c.images.size());
    global<int32_t>(addr::kGlowStep) = c.glowStep;
    for (int i = 0; i < g.elementCount && (i + 1) * addr::kGuiElementSize <= c.elements.size(); ++i)
        std::memcpy(g.elements[i], &c.elements[i * addr::kGuiElementSize], addr::kGuiElementSize);
    groupSlot(addr::kSelectPortraitGroup) = c.group3;
    attachAssets(c.assets);
}

// Swaps copy k into the game's memory.
void swapTo(int k) {
    if (k == g.live || !g.copy[k].used) return;
    save(g.copy[g.live]);
    load(g.copy[k]);
    g.live = k;
}

// The elements' texts are memory handles the game writes in place, so every
// copy needs its own: copyText duplicates one and ownTexts all of a copy's.
uint32_t copyText(uint32_t text, uint32_t size) {
    size = std::max<uint32_t>(size, 1);
    const uint32_t own = text ? MemAlloc(size) : 0;
    if (own) {
        std::memcpy(MemLock(own), MemLock(text), size);
        MemUnlock(text);
        MemUnlock(own);
    }
    return own;
}

void ownTexts(std::vector<uint8_t>& elements) {
    for (int i = 0; i < g.elementCount; ++i) {
        uint8_t* el = &elements[i * addr::kGuiElementSize];
        uint32_t& text = field<uint32_t>(el, kElemText);
        text = copyText(text, field<uint32_t>(el, kElemTextSize));
    }
}

// Frees the texts of a copy's elements.
void freeTexts(std::vector<uint8_t>& elements) {
    for (size_t at = 0; at + addr::kGuiElementSize <= elements.size(); at += addr::kGuiElementSize)
        if (uint32_t& text = field<uint32_t>(&elements[at], kElemText)) {
            MemFree(text);
            text = 0;
        }
}

// Records the screen as it was just made, before its first step: what a new
// copy starts from.
void notePristine() {
    g.elementCount = std::clamp(global<int>(addr::kGuiElementCount), 0, kMaxElements);
    for (int i = 0; i < g.elementCount; ++i) g.elements[i] = reinterpret_cast<uint8_t**>(addr::kGuiElements)[i];
    g.pristineElements.resize(g.elementCount * addr::kGuiElementSize);
    for (int i = 0; i < g.elementCount; ++i)
        std::memcpy(&g.pristineElements[i * addr::kGuiElementSize], g.elements[i], addr::kGuiElementSize);
    ownTexts(g.pristineElements);
    std::memcpy(g.pristineState.data(), reinterpret_cast<void*>(addr::kSelectState), g.pristineState.size());
    g.copy = {};
    g.copy[0].used = true;
    g.live = 0;
    g.leaving = -1;
}

// Makes a copy for a player who joined: the screen as just made, with texts
// of its own and none of player 1's images; its first step sets it up as the
// game does. The press that joined is not a menu press.
void createCopy(int slot) {
    Copy& c = g.copy[slot];
    c = {};
    c.used = true;
    c.state = g.pristineState;
    for (uintptr_t image : addr::kSelectCardImages)
        std::memset(&c.state[image - addr::kSelectState], 0, sizeof(uintptr_t));
    std::memset(&c.state[addr::kSelectStep - addr::kSelectState], 0xFF, sizeof(int));
    uintptr_t next = fn::SelectFirstStep;
    std::memcpy(&c.callbacks[addr::kScreenStep - addr::kSelectCallbacks], &next, sizeof next);
    c.elements = g.pristineElements;
    ownTexts(c.elements);
    c.glowStep = global<int32_t>(addr::kGlowStep);
    c.buttons.reset();
    uint32_t bits = 0;
    input::menuPad(slot, bits);
    int ignored[MenuButtons::kMaxEvents];
    c.buttons.update(bits, ignored);
    BMX_LOG_INFO("riderselect: player %d has his own select screen", slot + 1);
}

// Frees what a copy made for itself (images, groups and texts) and puts
// player 1's copy back without keeping this one.
void destroyCopy(int slot) {
    if (slot == 0 || !g.copy[slot].used) return;
    swapTo(slot);
    for (uintptr_t a : addr::kSelectCardImages)
        if (uintptr_t& image = global<uintptr_t>(a)) {
            FreeImage(image);
            image = 0;
        }
    auto* images = reinterpret_cast<uintptr_t*>(addr::kSelectImages);
    for (int i = 0; i < 16; ++i)
        if (images[i]) {
            FreeImage(images[i]);
            images[i] = 0;
        }
    UnloadGroup(addr::kSelectPortraitGroup);
    for (int i = 0; i < g.elementCount; ++i)
        if (uint32_t& text = field<uint32_t>(g.elements[i], kElemText)) {
            MemFree(text);
            text = 0;
        }
    load(g.copy[0]);
    g.live = 0;
    g.copy[slot] = {};
}

// Frees every copy but player 1's and the pristine texts.
void destroyCopies() {
    for (int slot = kMaxCopies - 1; slot >= 1; --slot) destroyCopy(slot);
    freeTexts(g.pristineElements);
    g.pristineElements.clear();
}

// Removes a player who left: his copy goes and the players after him move
// up.
void removePlayer(int slot) {
    destroyCopy(slot);
    for (int s = slot; s + 1 < kMaxCopies; ++s) std::swap(g.copy[s], g.copy[s + 1]);
    frontend::leave(slot);
}

// ----------------------------------------------------------------- drawing --
// Moves and scales the GUI primitives in [from, to) into a copy's place on
// the screen. They are PlayStation-style primitives with the kind in byte 7
// (its low 2 bits are blending and raw texture); x runs 0..512 across the
// screen and y 0..480. A clip area is in pixels, 32 units lower than drawn
// (0x45FE50); an empty one becomes the copy's place, so a name sliding in
// stays inside it.
void place(uint8_t* from, uint8_t* to, const SelectView& v) {
    const float vx = v.x * 512 / 640;
    auto points = [&](uint8_t* p, uint32_t first, uint32_t step, int count) {
        for (int i = 0; i < count; ++i) {
            int16_t& x = field<int16_t>(p, first + i * step);
            int16_t& y = field<int16_t>(p, first + i * step + 2);
            x = static_cast<int16_t>(vx + x * v.scale);
            y = static_cast<int16_t>(v.y + y * v.scale);
        }
    };
    const float* screen = reinterpret_cast<const float*>(addr::kScreenSize);
    const float pxX = screen[0] / 512, pxY = screen[1] / 480;
    constexpr float kClipY = 32;
    for (uint8_t* p = from; p < to;) {
        switch (p[7] & 0xFC) {
            case 0x28:  // flat quad
                points(p, 0x08, 4, 4);
                p += 0x84;
                break;
            case 0x2C:  // textured quad
                points(p, 0x08, 10, 4);
                p += 0x9C;
                break;
            case 0x38:  // shaded quad
                points(p, 0x08, 8, 4);
                p += 0x90;
                break;
            case 0x3C:  // shaded textured quad (the yellow frames)
                points(p, 0x0C, 12, 4);
                p += 0xA4;
                break;
            case 0x40:  // line
                points(p, 0x08, 4, 2);
                p += 0x7C;
                break;
            case 0xF8: {  // clip area
                int32_t* r = reinterpret_cast<int32_t*>(p + 8);
                if (!r[2] && !r[3]) {
                    r[0] = static_cast<int32_t>(vx * pxX);
                    r[1] = static_cast<int32_t>((v.y + kClipY) * pxY);
                    r[2] = static_cast<int32_t>(512 * v.scale * pxX);
                    r[3] = static_cast<int32_t>(480 * v.scale * pxY);
                } else {
                    r[0] = static_cast<int32_t>((vx + r[0] / pxX * v.scale) * pxX);
                    r[1] = static_cast<int32_t>((v.y + (r[1] / pxY - kClipY) * v.scale + kClipY) * pxY);
                    r[2] = static_cast<int32_t>(r[2] * v.scale);
                    r[3] = static_cast<int32_t>(r[3] * v.scale);
                }
                p += 0x18;
                break;
            }
            default:
                if (!g.unknownPrimLogged) {
                    g.unknownPrimLogged = true;
                    BMX_LOG_WARN("riderselect: GUI primitive kind %02X not placed", p[7]);
                }
                return;
        }
    }
}

uint8_t*& primCursor() { return global<uint8_t*>(addr::kGuiPrimCursor); }

// The step title on show (the rider's name, in yellow); three elements take
// turns sliding in.
uint8_t* titleElement() {
    for (uint32_t id : kTitles)
        if (uint8_t* el = FindElement(id); el && !(field<uint16_t>(el, 2) & 1) && field<int16_t>(el, 0x14) == 0)
            return el;
    return FindElement(kTitles[0]);
}

// Draws a line in the look of the step title, centred across the copy at `y`
// (the copy's units), clear of the clip area the copy's last element left,
// and puts the element back.
void drawTitleText(const char* text, int y) {
    uint8_t* el = titleElement();
    if (!el) return;
    std::array<uint8_t, addr::kGuiElementSize> keep;
    std::memcpy(keep.data(), el, keep.size());
    const uint32_t textHandle = field<uint32_t>(el, kElemText);
    std::vector<char> oldText;
    if (textHandle) {
        const char* t = reinterpret_cast<const char*>(MemLock(textHandle));
        oldText.assign(t, t + std::strlen(t) + 1);
        MemUnlock(textHandle);
    }
    SetText(el, text);
    field<uint16_t>(el, 2) &= ~1u;
    field<uint16_t>(el, 0xC8) = static_cast<uint16_t>((field<uint16_t>(el, 0xC8) & ~3u) | 0x20);
    field<int16_t>(el, 0x0C) = 256;
    field<int16_t>(el, 0x10) = 0;
    field<int16_t>(el, 0x14) = 0;
    field<int16_t>(el, 0xCC) = 0;
    field<int16_t>(el, 0xCE) = 0;
    field<int16_t>(el, 0x16) = static_cast<int16_t>(y - 104 - field<int16_t>(el, 0x0E));
    SetClip(0, 0, 639, 479);
    SendElement(kEventDrawElement, el, 0, 0);
    if (!oldText.empty()) SetText(el, oldText.data());
    const uint32_t newHandle = field<uint32_t>(el, kElemText);
    std::memcpy(el, keep.data(), keep.size());
    field<uint32_t>(el, kElemText) = newHandle;
    field<uint32_t>(el, kElemTextSize) = std::max(field<uint32_t>(el, kElemTextSize), field<uint32_t>(keep.data(), kElemTextSize));
}

// Draws every copy in its place, with "READY" over a ready player's and
// "PRESS TO JOIN" blinking beside player 1 while he is alone.
int drawCopies(int event, int a, int b, int c) {
    SelectView views[kMaxCopies + 1];
    const int n = players();
    selectViews(n, views);
    int result = 1;
    for (int slot = 0; slot < n; ++slot) {
        if (!g.copy[slot].used) continue;
        swapTo(slot);
        uint8_t* const from = primCursor();
        SetClip(0, 0, 639, 479);
        result = g_selectHandler.original(event, a, b, c);
        if (g.copy[slot].ready) drawTitleText("READY", 400);
        place(from, primCursor(), views[slot]);
    }
    swapTo(0);
    if (n == 1) {
        uint8_t* const from = primCursor();
        if ((g.ticks / 30) % 2 == 0) drawTitleText("PRESS TO JOIN", 230);
        place(from, primCursor(), views[1]);
    }
    SetClip(0, 0, 639, 479);
    return result;
}

// ------------------------------------------------------------------- input --
bool everyoneReady() {
    const int n = players();
    if (n < 2) return false;
    for (int slot = 0; slot < n; ++slot)
        if (!g.copy[slot].used || !g.copy[slot].ready) return false;
    return true;
}

// Sends a menu event to the copy swapped in. A ready player is past his last
// step, where only back does something, as on the game's level step.
int press(int slot, int event, int a, int b, int c) {
    if (g.copy[slot].ready && event != guievent::kBack) return 1;
    return g_selectHandler.original(event, a, b, c);
}

// Joins new players with what confirms on the game's select (confirm or
// Start), runs every other copy's input and tick, removes a player who left
// and starts the level step once everyone is ready.
void tickCopies() {
    ++g.ticks;
    const int joined = frontend::join(buttonsFor(guievent::kConfirm) | buttonsFor(guievent::kStart));
    if (joined > 0 && joined < kMaxCopies) createCopy(joined);
    const int n = players();
    for (int slot = 1; slot < n && slot < kMaxCopies; ++slot) {
        if (!g.copy[slot].used) createCopy(slot);
        swapTo(slot);
        uint32_t bits = 0;
        input::menuPad(slot, bits);
        int events[MenuButtons::kMaxEvents];
        const int count = g.copy[slot].buttons.update(bits, events);
        const uint8_t returnStep = global<uint8_t>(addr::kSelectReturnStep);
        global<uint8_t>(addr::kSelectReturnStep) = 0;
        for (int i = 0; i < count && g.copy[slot].used; ++i) press(slot, events[i], 0, 0, 0);
        if (g.copy[slot].used) g_selectHandler.original(guievent::kTick, 0, 0, 0);
        global<uint8_t>(addr::kSelectReturnStep) = returnStep;
    }
    swapTo(0);
    if (g.leaving > 0) {
        removePlayer(g.leaving);
        g.leaving = -1;
    }
    if (everyoneReady()) {
        BMX_LOG_INFO("riderselect: everyone is ready, player 1 picks the level");
        g.copy[0].ready = false;
        g.levelStep = true;
        g_selectMove.original(1);
    }
}

bool isPadEvent(int event) { return event >= guievent::kFirstPad && event <= guievent::kLastPad; }

// Returns whether every player's copy is on screen: from the screen's first
// frame ("LOADING" too) until player 1 goes on to the level step, which is
// his alone, full screen.
bool sharing() { return !g.levelStep; }

// ------------------------------------------------------------------- hooks --
// SelectEnter: multiplayer opens the shared screen; LEVEL SELECT after a run
// opens on the level step.
int __cdecl onSelectEnter() {
    g.active = frontend::multiplayer();
    g.elementCount = 0;
    g.levelStep = global<uint8_t>(addr::kSelectReturnStep) == 3;
    if (g.active) {
        input::watchPads();
    }
    return g_selectEnter.original();
}

// SelectHandler: the screen's first event records it as just made; then
// every copy is drawn and ticked, and the pad events go to player 1's copy.
int __cdecl onSelectHandler(int event, int a, int b, int c) {
    if (!g.active) return g_selectHandler.original(event, a, b, c);
    if (g.elementCount == 0) notePristine();
    if (!sharing()) return g_selectHandler.original(event, a, b, c);
    if (event == guievent::kDraw) return drawCopies(event, a, b, c);
    if (event == guievent::kTick) {
        const int result = g_selectHandler.original(event, a, b, c);
        if (sharing()) tickCopies();
        return result;
    }
    if (isPadEvent(event)) return press(0, event, a, b, c);
    return g_selectHandler.original(event, a, b, c);
}

// SelectMove: the game's confirm and back, with their sounds, move the select
// between its steps here; only where they lead changes. Confirm on the last
// step makes the player ready and back undoes it; back on the rider step
// makes players 2 and up leave, after their turn.
void __cdecl onSelectMove(int dir) {
    if (!g.active) return g_selectMove.original(dir);
    const int step = global<int>(addr::kSelectStep);
    const int slot = g.live;
    if (step == 2 && g.copy[slot].ready && dir < 0) {
        g.copy[slot].ready = false;
        BMX_LOG_INFO("riderselect: player %d is not ready", slot + 1);
        return;
    }
    if (step == 2 && dir > 0 && !everyoneReady()) {
        g.copy[slot].ready = true;
        BMX_LOG_INFO("riderselect: player %d is ready", slot + 1);
        return;
    }
    if (slot > 0 && step == 0 && dir < 0) {
        g.leaving = slot;
        return;
    }
    if (step == 3 && dir < 0) {
        g.levelStep = false;
        BMX_LOG_INFO("riderselect: back from the level step");
    }
    g_selectMove.original(dir);
}

// SelectLeave: when player 1's screen is torn down (game start or back to
// the main menu), the other copies go too. On to the game their picks are
// kept (a player without a copy this time, e.g. straight back to the level
// after a run, keeps his earlier pick); back to the main menu they leave and
// join again on the next rider select.
void __cdecl onSelectLeave() {
    const bool tearsDown = global<uint32_t>(addr::kSelectWaitUntil) <= global<uint32_t>(addr::kSelectTimer);
    if (!tearsDown || !g.active) return g_selectLeave.original();
    for (int slot = 1; slot < players() && slot < kMaxCopies; ++slot) {
        const Copy& c = g.copy[slot];
        if (!c.used) continue;
        auto pick = [&](uintptr_t array) {
            int v = 0;
            std::memcpy(&v, &c.state[array - addr::kSelectState], sizeof v);
            return static_cast<uint8_t>(v);
        };
        uint8_t* out = &g.picked[3 * slot];
        out[0] = pick(addr::kSelectRider);
        out[1] = pick(addr::kSelectOutfit);
        out[2] = pick(addr::kSelectBike);
        g.pickedMask |= 1u << slot;
        if (slot == 1) {
            uint8_t* cfg = playerConfig(1);
            cfg[3] = out[0];
            cfg[4] = out[1];
            cfg[5] = out[2];
        }
        BMX_LOG_INFO("riderselect: player %d rides rider %u, outfit %u, bike %u", slot + 1, out[0], out[1], out[2]);
    }
    destroyCopies();
    g.active = false;
    if (global<int>(addr::kSelectStep) == -1) {
        frontend::leaveRide();
        g.pickedMask = 0;
    }
    g_selectLeave.original();
}

}  // namespace

bool pickOf(int player, uint8_t out[3]) {
    if (player < 2 || player >= kMaxPlayers || !(g.pickedMask & (1u << player))) return false;
    std::memcpy(out, &g.picked[3 * player], 3);
    return true;
}

bool install() {
    bool ok = true;
    ok &= g_selectEnter.install(fn::SelectEnter, &onSelectEnter, "SelectEnter");
    ok &= g_selectHandler.install(fn::SelectHandler, &onSelectHandler, "SelectHandler");
    ok &= g_selectLeave.install(fn::SelectLeave, &onSelectLeave, "SelectLeave");
    ok &= g_selectMove.install(fn::SelectMove, &onSelectMove, "SelectMove");
    ok &= g_allocAsset.install(fn::GuiAssetAlloc, &onAllocAsset, "GuiAssetAlloc");
    if (ok) BMX_LOG_INFO("riderselect: ready (multiplayer: a select screen per player)");
    return ok;
}

}  // namespace bmx::riderselect
