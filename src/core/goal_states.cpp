#include "core/goal_states.hpp"

namespace bmx {

namespace {
int itemByte(const uint8_t* block, int goal, int item) {
    const int difficulty = block[1] > 2 ? 0 : block[1];
    return (goal + 1 + difficulty * 4) * 3 + 1 + (item >> 1);
}
int itemShift(int item) { return (item & 1) * 4; }
}  // namespace

uint8_t goalItem(const uint8_t* block, int goal, int item) {
    return (block[itemByte(block, goal, item)] >> itemShift(item)) & 0x0F;
}

void setGoalItem(uint8_t* block, int goal, int item, uint8_t nibble) {
    uint8_t& b = block[itemByte(block, goal, item)];
    const int s = itemShift(item);
    b = static_cast<uint8_t>((b & ~(0x0F << s)) | ((nibble & 0x0F) << s));
}

void RiderGoals::resetFrom(const uint8_t* block) {
    for (int g = 0; g < kGoals; ++g)
        for (int i = 0; i < kGoalItems; ++i) {
            const uint8_t now = goalItem(block, g, i);
            items[g][i] = goalItemDone(now) ? 0 : now;
        }
}

void RiderGoals::load(uint8_t* block) const {
    for (int g = 0; g < kGoals; ++g)
        for (int i = 0; i < kGoalItems; ++i)
            if (!goalItemDone(goalItem(block, g, i))) setGoalItem(block, g, i, items[g][i]);
}

void RiderGoals::store(uint8_t* block, const uint8_t* before) {
    for (int g = 0; g < kGoals; ++g)
        for (int i = 0; i < kGoalItems; ++i) {
            const uint8_t now = goalItem(block, g, i);
            if (goalItemDone(now)) {
                items[g][i] = 0;
                continue;
            }
            items[g][i] = now;
            setGoalItem(block, g, i, goalItem(before, g, i));
        }
}

}  // namespace bmx
