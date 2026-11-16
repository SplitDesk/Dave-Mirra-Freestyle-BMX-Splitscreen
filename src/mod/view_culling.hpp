#pragma once
// Performance mode (Split.PerformanceMode): every view draws the whole park
// and, without it, every rider and his shadow, so the riders' cost grows
// with players x views. With it on, a view leaves out the riders, and their
// shadows, too far from its own rider to be more than a few pixels tall in
// it: the further the views are split, the nearer that is.

#include <array>

#include "core/limits.hpp"

namespace bmx::viewculling {

// Turns performance mode on or off.
void setEnabled(bool on);
bool enabled();

// Returns whether player `player`'s rider and shadow are drawn in `view`.
bool drawn(int player, int view);

// Shows and hides the riders for `view`, about to be drawn; runs before
// each view's draw.
void beginView(int view);

// Shows again, at the end of the frame's render, the riders hidden for its
// views.
class Frame {
public:
    Frame() = default;
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

}  // namespace bmx::viewculling
