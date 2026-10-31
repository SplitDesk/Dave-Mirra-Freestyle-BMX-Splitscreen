#include "mod/extra_riders.hpp"

#include <array>
#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "game/engine.hpp"
#include "mod/rider_select.hpp"

namespace bmx::extras {
namespace {

using namespace bmx::game;

// The slot the extra riders take turns in.
constexpr int kSlot = 1;
constexpr uintptr_t kSlotRider = addr::kRiderArray + kSlot * addr::kRiderStride;
constexpr uintptr_t kSlotConfig = addr::kPlayerConfigArray + kSlot * addr::kPlayerConfigStride;

// A block of game memory.
struct Region {
    uintptr_t address;
    size_t size;
};

// Everything the game keeps per rider slot that an extra rider needs his own
// copy of, the rider state first.
constexpr Region kRegions[] = {
    {kSlotRider, addr::kRiderStride},
    {addr::kCameraCtlArray + kSlot * addr::kCameraCtlStride, addr::kCameraCtlStride},
    {kSlotConfig, addr::kPlayerConfigStride},
    {addr::kPlayerDefs + kSlot * addr::kPlayerDefSize, addr::kPlayerDefSize},
    {addr::kControllerArray + kSlot * addr::kControllerStride, addr::kControllerStride},
    {addr::kBikePhysicsTable + kSlot * sizeof(void*), sizeof(void*)},
};

constexpr size_t slotDataSize() {
    size_t size = 0;
    for (const Region& r : kRegions) size += r.size;
    return size;
}

// One rider's copy of slot 2's regions, in kRegions order.
using SlotData = std::array<uint8_t, slotDataSize()>;

// Copies slot 2's regions into `to`.
void save(SlotData& to) {
    size_t at = 0;
    for (const Region& r : kRegions) {
        std::memcpy(to.data() + at, reinterpret_cast<const void*>(r.address), r.size);
        at += r.size;
    }
}

// Copies `from` into slot 2's regions.
void load(const SlotData& from) {
    size_t at = 0;
    for (const Region& r : kRegions) {
        std::memcpy(reinterpret_cast<void*>(r.address), from.data() + at, r.size);
        at += r.size;
    }
}

struct State {
    int count = 0;
    std::vector<SlotData> extras{};
    SlotData parked{};  // rider 2's copy while an extra rider has the slot
    int current = -1;
};
State g;

}  // namespace

int count() { return g.count; }
int current() { return g.current; }

// Parks rider 2's slot data and loads extra rider k's.
void begin(int k) {
    if (g.current >= 0 || k < 0 || k >= g.count) return;
    save(g.parked);
    load(g.extras[k]);
    g.current = k;
}

// Saves the current extra rider's slot data and loads rider 2's back.
void end() {
    if (g.current < 0) return;
    save(g.extras[g.current]);
    load(g.parked);
    g.current = -1;
}

// Player 1 and the player in slot 2 are in the game's array; the others are
// in their saved copies, the rider state at the start of each.
const uint8_t* riderOf(int player) {
    if (player == 0) return reinterpret_cast<const uint8_t*>(addr::kRiderArray);
    if (player == slotPlayer()) return reinterpret_cast<const uint8_t*>(kSlotRider);
    if (player == 1) return g.parked.data();
    const int k = player - playerOf(0);
    return k >= 0 && k < g.count ? g.extras[k].data() : nullptr;
}

void createAll(int players, CreateFn create) {
    const int wanted = players - 2;
    g.count = 0;
    g.extras.clear();
    if (wanted <= 0 || g.current >= 0) return;
    g.extras.resize(wanted);
    const uint8_t* p1 = playerConfig(0);
    int& defs = global<int>(addr::kPlayerDefCount);
    int& bikes = global<int>(addr::kBikePhysicsCount);
    save(g.parked);
    for (int k = 0; k < wanted; ++k) {
        // Each rider starts from rider 2's fresh slot, with his own character,
        // outfit and bike in PlayerConfig +3..+5 (player 1's when he did not
        // pick). The model definition and the bike physics go in slot 2's
        // entries, where the game would use a third definition and player 1's
        // bike physics; the counts are restored after.
        load(g.parked);
        uint8_t pick[3] = {p1[3], p1[4], p1[5]};
        riderselect::pickOf(playerOf(k), pick);
        uint8_t* cfg = reinterpret_cast<uint8_t*>(kSlotConfig);
        cfg[3] = pick[0];
        cfg[4] = pick[1];
        cfg[5] = pick[2];
        defs = kSlot;
        bikes = kSlot;
        const int made = create(playerOf(k), kSlot, cfg[3], cfg[4], cfg[5]);
        defs = 2;
        bikes = 0;
        if (!made) {
            BMX_LOG_WARN("extras: rider %d could not be made", playerOf(k) + 1);
            break;
        }
        save(g.extras[k]);
        g.count = k + 1;
        BMX_LOG_INFO("extras: rider %d made (config %u %u %u)", playerOf(k) + 1, cfg[3], cfg[4], cfg[5]);
    }
    load(g.parked);
}

void levelEnd() {
    if (g.current >= 0) end();
    if (g.count) BMX_LOG_INFO("extras: %d extra rider(s) removed", g.count);
    g.count = 0;
    g.extras.clear();
}

}  // namespace bmx::extras
