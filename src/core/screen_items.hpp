#pragma once
// The item list of an in-game screen such as RUN FINISHED: one {action, text}
// pair per item (0x56C028), with the count kept in the screen's entry.

#include <cstdint>

namespace bmx {

// One menu item: the action the screen's tick performs when it is picked and
// the string id of its text.
struct ScreenItem {
    uint16_t action;
    uint16_t text;
};

// Inserts `item` after the first item whose action is `after`, or at the end
// when there is none, and returns the new count. A full list is left as it is.
int insertItemAfter(ScreenItem* items, int count, int capacity, uint16_t after, ScreenItem item);

}  // namespace bmx
