#pragma once
// The players of a multiplayer game and the device each one uses.
//
// Player 1 owns the menus and keeps his device. Every other player joins by
// pressing a button on a device nobody uses yet (a pad, or the keyboard) and
// takes the first free slot; when a player leaves, the players after him move
// up one slot.

#include <array>

#include "core/limits.hpp"

namespace bmx {

class JoinSlots {
public:
    static constexpr int kNoPad = -1;

    // Offers `maxPlayers` slots, player 1 included, with player 1 on
    // `player1Pad` (kNoPad for the keyboard).
    void reset(int maxPlayers, int player1Pad);

    // Joins `pad` and returns its slot, or -1 when the game is full or the pad
    // is already in use.
    int join(int pad);

    // Removes the player in `slot` (1 and up).
    bool leave(int slot);

    // Returns player 1 plus everyone who joined.
    int players() const;

    // Returns the device of `slot`, kNoPad when the slot is empty.
    int padOf(int slot) const;

private:
    int slotOfPad(int pad) const;

    int max_ = 2;
    int player1Pad_ = kNoPad;
    std::array<int, kMaxPlayers> pads_{};
};

}  // namespace bmx
