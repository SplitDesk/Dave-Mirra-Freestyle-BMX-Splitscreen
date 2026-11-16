#pragma once
// A health line in the log once a minute, to catch leaks and slowdowns over
// a long session: frame times and where they go, the process's memory and
// handles, and the game pools the mod adds to (cameras, GUI asset handles,
// menu primitives).

namespace bmx::perf {

// Counts a rendered frame and writes the line when a minute has passed.
void frame();

// The parts of a frame the line times: the whole render (every view), and
// within it the riders' shadows and the HUD; and before it the riders'
// update and the cameras' update.
enum class Phase { Render, Shadows, Hud, Riders, Cameras, Count };

// Times its lifetime into `phase`.
class Timer {
public:
    explicit Timer(Phase phase);
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

private:
    Phase phase_;
    long long start_;
};

}  // namespace bmx::perf
