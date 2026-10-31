#include "core/join_slots.hpp"

namespace bmx {

void JoinSlots::reset(int maxPlayers, int player1Pad) {
    max_ = maxPlayers < 1 ? 1 : (maxPlayers > kMaxPlayers ? kMaxPlayers : maxPlayers);
    player1Pad_ = player1Pad;
    pads_.fill(kNoPad);
}

int JoinSlots::join(int pad) {
    if (pad < 0 || pad == player1Pad_ || slotOfPad(pad) >= 0) return -1;
    for (int slot = 1; slot < max_; ++slot)
        if (pads_[slot] == kNoPad) {
            pads_[slot] = pad;
            return slot;
        }
    return -1;
}

bool JoinSlots::leave(int slot) {
    if (slot < 1 || slot >= max_ || pads_[slot] == kNoPad) return false;
    for (int s = slot; s + 1 < max_; ++s) pads_[s] = pads_[s + 1];
    pads_[max_ - 1] = kNoPad;
    return true;
}

int JoinSlots::players() const {
    int n = 1;
    for (int slot = 1; slot < max_; ++slot) n += pads_[slot] != kNoPad;
    return n;
}

int JoinSlots::padOf(int slot) const {
    if (slot == 0) return player1Pad_;
    return slot > 0 && slot < max_ ? pads_[slot] : kNoPad;
}

int JoinSlots::slotOfPad(int pad) const {
    if (pad < 0) return -1;
    if (pad == player1Pad_) return 0;
    for (int slot = 1; slot < max_; ++slot)
        if (pads_[slot] == pad) return slot;
    return -1;
}

}  // namespace bmx
