#pragma once
// ProQuest goal progress as the game keeps it in a career block (0x4102C0,
// 0x410350, 0x410510).
//
// A level's goal block is 0x27 bytes: +1 is the difficulty (0-2), followed by
// 4 goals of 3 bytes per difficulty: a flags byte and 4 items of 4 bits. The
// low 3 bits of an item are its state (1-3: attempt in progress, 4 and up:
// done); bit 3 is a separate flag set for every item at run start.
//
// In co-op every rider works on player 1's career: attempts are each rider's
// own, while done items are shared.

#include <array>
#include <cstdint>

namespace bmx {

inline constexpr int kGoalBlockSize = 0x27;
inline constexpr int kGoals = 4, kGoalItems = 4;
inline constexpr uint8_t kGoalItemDone = 4;
inline constexpr uint8_t kGoalStateMask = 7;

using GoalItems = std::array<std::array<uint8_t, kGoalItems>, kGoals>;

// Reads and writes an item's 4 bits (state and flag).
uint8_t goalItem(const uint8_t* block, int goal, int item);
void setGoalItem(uint8_t* block, int goal, int item, uint8_t nibble);
inline bool goalItemDone(uint8_t nibble) { return (nibble & kGoalStateMask) >= kGoalItemDone; }

// One rider's attempts in progress, kept between frames.
struct RiderGoals {
    GoalItems items{};

    // Takes the attempts of the items in `block` that are not done.
    void resetFrom(const uint8_t* block);

    // Writes this rider's attempts into the items of `block` that are not done.
    void load(uint8_t* block) const;

    // After this rider's update: keeps his changed attempts, restores the
    // previous attempts from `before` and leaves newly done items in `block`.
    void store(uint8_t* block, const uint8_t* before);
};

}  // namespace bmx
