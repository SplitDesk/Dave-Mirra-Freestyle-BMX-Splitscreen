#include "mod/game_heap.hpp"

#include "common/log.hpp"
#include "core/limits.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"

namespace bmx::gameheap {
namespace {

using namespace bmx::game;

// The game's sizes, and the GUI memory added for each player past the 8 they
// hold.
constexpr uint32_t kGameArena = 0x400000, kGameGuiHeap = 0x300000;
constexpr int kPlayersInGameSizes = 8;
constexpr uint32_t kPerPlayer = 0x80000;
constexpr uint32_t kExtra = kPerPlayer * (kMaxPlayers > kPlayersInGameSizes ? kMaxPlayers - kPlayersInGameSizes : 0);
// The game's 64 actors run out at 32 riders; every rider past the game's two
// gets his two more.
constexpr int kGameActors = 64, kActorsPerRider = 2;
constexpr int kActors = kGameActors + kActorsPerRider * (kMaxPlayers - 2);
constexpr uint32_t kExtraActorMemory = (kActors - kGameActors) * addr::kActorSize;

using AssertFn = void(__cdecl*)(const char* why, const char* file, int line, const char* function);
hook::Hook<AssertFn> g_assert;

// GameAssert: logs the assert before the game's message box.
void __cdecl onAssert(const char* why, const char* file, int line, const char* function) {
    BMX_LOG_ERROR("game assert: %s (%s, line %d%s%s)", why ? why : "?", file ? file : "?", line,
                  function ? ", " : "", function ? function : "");
    g_assert.original(why, file, line, function);
}

}  // namespace

uint32_t guiHeapFree() {
    uint32_t total = 0;
    for (auto block = global<uintptr_t>(addr::kGuiHeapFree); block && block != 0xFFFFFFFF;
         block = *reinterpret_cast<const uintptr_t*>(block + 4))
        total += *reinterpret_cast<const uint32_t*>(block + 8);
    return total;
}

bool install() {
    if (!g_assert.install(fn::GameAssert, &onAssert, "GameAssert")) return false;
    if (kExtra == 0) return true;
    if (global<uint32_t>(addr::kStaticArenaEnd) != 0 || global<uintptr_t>(addr::kActorPool) != 0) {
        BMX_LOG_WARN("gameheap: the game set its memory up before the mod loaded; it stays the game's");
        return true;
    }
    const hook::Patch32 patches[] = {
        {fn::kStaticArenaSize, kGameArena, kGameArena + kExtra + kExtraActorMemory},
        {fn::kGuiHeapSize, kGameGuiHeap, kGameGuiHeap + kExtra},
    };
    int& actors = global<int>(addr::kActorCount);
    if (actors != kGameActors || !hook::matches(patches, "gameheap") || !hook::apply(patches)) {
        BMX_LOG_WARN("gameheap: the game's memory stays as it is (room for about 8 rider selects and 32 riders)");
        return true;
    }
    actors = kActors;
    BMX_LOG_INFO("gameheap: GUI memory %u KB (the game's %u KB), %d actors (the game's %d)",
                 (kGameGuiHeap + kExtra) / 1024, kGameGuiHeap / 1024, kActors, kGameActors);
    return true;
}

}  // namespace bmx::gameheap
