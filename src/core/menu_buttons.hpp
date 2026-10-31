#pragma once
// Pad buttons turned into the front end's GUI events, as the game does for
// player 1 (0x436C20..0x437180).

#include <cstdint>

namespace bmx {

// The front end's GUI events. Player 2's button events are player 1's plus
// kPlayer2Offset, from the PlayStation 2-player front end.
namespace guievent {
inline constexpr int kDraw = 0x15;
inline constexpr int kTick = 0x16;
inline constexpr int kUp = 0x1E, kDown = 0x1F, kLeft = 0x20, kRight = 0x21;
inline constexpr int kConfirm = 0x22, kCircle = 0x23, kStart = 0x24, kBack = 0x25;
inline constexpr int kL1 = 0x27, kR1 = 0x28, kL2 = 0x29, kR2 = 0x2A;
inline constexpr int kFirstPad = kUp, kLastPad = kR2;
inline constexpr int kPlayer2Offset = 13;
}  // namespace guievent

// Returns the game buttons that make a button event, e.g. Cross and Enter for
// kConfirm.
uint32_t buttonsFor(int event);

// Turns one player's held buttons into GUI events frame by frame, with the
// game's repeat for held directions.
class MenuButtons {
public:
    static constexpr int kMaxEvents = 12;

    // Takes this frame's held buttons, writes the events to send in the
    // game's order to `out` and returns their count.
    int update(uint32_t held, int* out);

    void reset() { *this = MenuButtons{}; }

private:
    uint32_t prev_ = 0;
    uint32_t heldAtPress_ = 0;
    int repeat_ = 0;
};

}  // namespace bmx
