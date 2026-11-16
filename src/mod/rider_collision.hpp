#pragma once
// Riders crashing into each other, which the game does not have. When two
// riders touch and the faster one is going faster than 3/4 of his pedalling
// top speed, the slower rider bails as the game makes a rider bail when a
// moving object hits him, and loses his combo; at about the same speed, both
// bail when both are that fast. Slower, a rider meets the other rider's bike
// as the game's wheel test meets a wall. The riders start a run on top of
// each other, so a pair of riders passes through each other until they have
// been apart once, after every restart and every bail. A rider knocked down
// is a ghost for 3 seconds once he rides again, and after that until no one
// is inside him: no one meets him, and he blinks.

#include <array>

#include "core/limits.hpp"

namespace bmx::ridercollision {

// Hooks the wheel test against walls.
bool install();

// Sets whether riders crash into each other (General.Collision).
void setEnabled(bool on);
bool enabled();

// Makes every pair of riders pass through each other until they are apart;
// runs at the level start and on every restart.
void restart();

// Tests every pair of riders; runs once per frame after every rider's update.
void update();

// Hides the bike and body of each blinking ghost in its hidden phase for the
// object's lifetime: one frame's render.
class GhostBlink {
public:
    GhostBlink();
    ~GhostBlink();
    GhostBlink(const GhostBlink&) = delete;
    GhostBlink& operator=(const GhostBlink&) = delete;

private:
    std::array<void*, 2 * kMaxPlayers> hidden_{};
    int count_ = 0;
};

}  // namespace bmx::ridercollision
