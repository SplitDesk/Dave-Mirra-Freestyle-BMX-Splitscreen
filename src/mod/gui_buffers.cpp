#include "mod/gui_buffers.hpp"

#include <windows.h>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/low_memory.hpp"

namespace bmx::guibuffers {
namespace {

using namespace bmx::game;

// The game's sizes times the player count: room for every player's screen.
constexpr int kTimes = kMaxPlayers;
constexpr uint32_t kBufferSize = addr::kGuiPrimBufferSize * kTimes;
constexpr int kEntries = addr::kOrderingTableEntries * kTimes;
constexpr uint32_t kTopOffset = (kEntries - 1) * 4;
static_assert(kEntries < 0x8000, "the clear (0x45FF60) counts in 16 bits");

uint8_t* g_buffer = nullptr;
int g_capacity = addr::kOrderingTableEntries;

hook::Hook<IntFn> g_setup;

// GuiPrimSetup: the game sets its buffer up again after taking it down; the
// mod's buffer is put back.
int __cdecl onSetup() {
    const int result = g_setup.original();
    global<uint8_t*>(addr::kGuiPrimBase) = g_buffer;
    g_capacity = kEntries;
    return result;
}

// A 32-bit value in the game's code: its address, the game's value and the
// mod's.
struct Patch {
    uintptr_t at;
    uint32_t game, ours;
};

// Writes the mod's value of `p`.
bool patch(const Patch& p) {
    uint32_t& value = *reinterpret_cast<uint32_t*>(p.at);
    DWORD old = 0;
    if (!VirtualProtect(&value, sizeof value, PAGE_EXECUTE_READWRITE, &old)) return false;
    value = p.ours;
    VirtualProtect(&value, sizeof value, old, &old);
    FlushInstructionCache(GetCurrentProcess(), &value, sizeof value);
    return true;
}

}  // namespace

int capacity() { return g_capacity; }

// The GUI's entries are taken from the top of the table down.
int used() {
    const auto table = global<uintptr_t>(addr::kOrderingTable);
    const auto cursor = global<uintptr_t>(addr::kGuiOtCursor);
    const uintptr_t top = table + (g_capacity - 1) * 4;
    return cursor <= top ? static_cast<int>((top - cursor) / 4) : 0;
}

bool install() {
    constexpr uint32_t kGameCount = addr::kOrderingTableEntries, kGameTop = (kGameCount - 1) * 4;
    const Patch patches[] = {
        {fn::kOtCountA, kGameCount, kEntries},   {fn::kOtCountB, kGameCount, kEntries},
        {fn::kOtTopDrawA, kGameTop, kTopOffset}, {fn::kOtTopB, kGameTop, kTopOffset},
        {fn::kOtTopDrawB, kGameTop, kTopOffset},
    };
    for (const Patch& p : patches)
        if (*reinterpret_cast<const uint32_t*>(p.at) != p.game) {
            BMX_LOG_ERROR("guibuffers: unexpected code at 0x%08X, menus keep the game's room",
                          static_cast<unsigned>(p.at));
            return true;
        }
    uint8_t* table = lowmem::alloc(kEntries * 4);
    g_buffer = lowmem::alloc(kBufferSize);
    if (!table || !g_buffer) {
        BMX_LOG_WARN("guibuffers: no low memory, menus keep the game's room (1024 primitives)");
        return true;
    }
    if (!g_setup.install(fn::GuiPrimSetup, &onSetup, "GuiPrimSetup")) return false;
    for (const Patch& p : patches)
        if (!patch(p)) return false;
    // The game clears the table at the start of each frame, before use.
    global<uint8_t*>(addr::kOrderingTable) = table;
    global<uint8_t*>(addr::kGuiPrimBase) = g_buffer;
    BMX_LOG_INFO("guibuffers: room for %d primitives a frame (the game's %d)", kEntries, kGameCount);
    return true;
}

}  // namespace bmx::guibuffers
