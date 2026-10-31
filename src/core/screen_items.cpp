#include "core/screen_items.hpp"

namespace bmx {

int insertItemAfter(ScreenItem* items, int count, int capacity, uint16_t after, ScreenItem item) {
    if (count < 0 || count >= capacity) return count;
    int at = count;
    for (int i = 0; i < count; ++i)
        if (items[i].action == after) {
            at = i + 1;
            break;
        }
    for (int i = count; i > at; --i) items[i] = items[i - 1];
    items[at] = item;
    return count + 1;
}

}  // namespace bmx
