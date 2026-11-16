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
// The game's table clear counts in 16 bits; above this the mod clears.
constexpr int kGameClearMax = 0x7FFF;

uint8_t* g_buffer = nullptr;
int g_capacity = addr::kOrderingTableEntries;

hook::Hook<IntFn> g_setup;
hook::Hook<void(__cdecl*)(uint32_t* table, int count)> g_otClear;

// GuiPrimSetup: the game sets its buffer up again after taking it down; the
// mod's buffer is put back.
int __cdecl onSetup() {
    const int result = g_setup.original();
    global<uint8_t*>(addr::kGuiPrimBase) = g_buffer;
    g_capacity = kEntries;
    return result;
}

// OtClear: links each entry to the one below it and ends the chain at entry
// 0, as the game does, for tables too long for its 16-bit count.
void __cdecl onOtClear(uint32_t* table, int count) {
    if (count - 1 <= kGameClearMax) return g_otClear.original(table, count);
    for (int i = count - 1; i > 0; --i)
        table[i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&table[i - 1])) & 0x0FFFFFFFu;
    table[0] |= 0x0FFFFFFFu;
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
    const hook::Patch32 patches[] = {
        {fn::kOtCountA, kGameCount, kEntries},   {fn::kOtCountB, kGameCount, kEntries},
        {fn::kOtTopDrawA, kGameTop, kTopOffset}, {fn::kOtTopB, kGameTop, kTopOffset},
        {fn::kOtTopDrawB, kGameTop, kTopOffset},
    };
    if (!hook::matches(patches, "guibuffers")) {
        BMX_LOG_WARN("guibuffers: menus keep the game's room (1024 primitives)");
        return true;
    }
    uint8_t* table = lowmem::alloc(kEntries * 4);
    g_buffer = lowmem::alloc(kBufferSize);
    if (!table || !g_buffer) {
        BMX_LOG_WARN("guibuffers: no low memory, menus keep the game's room (1024 primitives)");
        return true;
    }
    if (!g_setup.install(fn::GuiPrimSetup, &onSetup, "GuiPrimSetup") ||
        !g_otClear.install(fn::OtClear, &onOtClear, "OtClear") || !hook::apply(patches))
        return false;
    // The game clears the table at the start of each frame, before use.
    global<uint8_t*>(addr::kOrderingTable) = table;
    global<uint8_t*>(addr::kGuiPrimBase) = g_buffer;
    BMX_LOG_INFO("guibuffers: room for %d primitives a frame (the game's %d)", kEntries, kGameCount);
    return true;
}

}  // namespace bmx::guibuffers
