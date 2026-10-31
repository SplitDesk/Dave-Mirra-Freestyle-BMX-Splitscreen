#include "core/menu_buttons.hpp"

#include "core/pad_mapping.hpp"

namespace bmx {

namespace {

// A held direction repeats after 20 frames, then every 5th frame.
constexpr int kRepeatDelay = 0x14, kRepeatEvery = 5;
constexpr uint32_t kDirections = gamebits::kUp | gamebits::kDown | gamebits::kLeft | gamebits::kRight;

struct ButtonEvent {
    uint32_t mask;
    int event;
};
// The buttons of each event, as the game maps them; Enter and Esc are the
// keyboard's confirm and back.
constexpr ButtonEvent kButtons[] = {
    {gamebits::kStart, guievent::kStart},  {gamebits::kCross | gamebits::kEnter, guievent::kConfirm},
    {gamebits::kCircle, guievent::kCircle}, {gamebits::kTriangle | gamebits::kEsc, guievent::kBack},
    {gamebits::kL1, guievent::kL1},         {gamebits::kR1, guievent::kR1},
    {gamebits::kL2, guievent::kL2},         {gamebits::kR2, guievent::kR2},
};
}  // namespace

uint32_t buttonsFor(int event) {
    uint32_t mask = 0;
    for (const ButtonEvent& b : kButtons)
        if (b.event == event) mask |= b.mask;
    return mask;
}

int MenuButtons::update(uint32_t held, int* out) {
    int n = 0;
    const uint32_t pressed = held & ~prev_;
    prev_ = held;

    // A direction fires only when it is the only one pressed.
    const uint32_t dirs = pressed & kDirections;
    static constexpr int kDirEvent[] = {guievent::kUp, guievent::kDown, guievent::kLeft, guievent::kRight};
    bool fired = false;
    if (dirs && (dirs & (dirs - 1)) == 0) {
        for (int i = 0; i < 4; ++i)
            if (dirs == (1u << i)) out[n++] = kDirEvent[i];
        heldAtPress_ = held;
        repeat_ = 0;
        fired = true;
    } else if (dirs) {
        heldAtPress_ = held;
        repeat_ = 0;
        fired = true;
    }
    if (!fired) {
        const uint32_t still = held & heldAtPress_ & kDirections;
        if (still && (still & (still - 1)) == 0) {
            if (++repeat_ > kRepeatDelay && repeat_ % kRepeatEvery == 0)
                for (int i = 0; i < 4; ++i)
                    if (still == (1u << i)) out[n++] = kDirEvent[i];
        } else {
            repeat_ = 0;
            heldAtPress_ = 0;
        }
    }

    for (const ButtonEvent& b : kButtons)
        if (pressed & b.mask) out[n++] = b.event;
    return n;
}

}  // namespace bmx
