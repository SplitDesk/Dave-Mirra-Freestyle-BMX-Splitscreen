#pragma once
// Every player's rider on the course at once.
//
// The game's level start creates one rider per slot up to the rider count,
// but the shipped game never sets the count above 1. The mod raises it to 2
// while the level loads, so rider 2 gets his models (and the extra riders
// theirs, made in his slot), lets the rest of the start run the single-player
// flow, then keeps the count at 2 so the per-frame rider, input and camera
// loops handle both slots. The riders all ride at once rather than in turns.

#include <cstdint>

namespace bmx::riders {

// Hooks the level start and end, the turn system and the rider updates.
bool install();

// Returns whether a level is running with rider 2 in slot 2.
bool secondActive();

// Makes the game see `count` riders (the rider count) for the object's
// lifetime, e.g. 1 as in single player, while rider 2 stays on the course.
class SeenRiders {
public:
    explicit SeenRiders(uint8_t count);
    ~SeenRiders();
    SeenRiders(const SeenRiders&) = delete;
    SeenRiders& operator=(const SeenRiders&) = delete;

private:
    uint8_t count_;
    bool was_;
};

// Logs the riders' state at LogLevel=debug; runs once per rendered frame.
void tick();

}  // namespace bmx::riders
