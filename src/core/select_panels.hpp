#pragma once
// Placement of the per-player copies of the rider select screen.

namespace bmx {

// A copy of the 640x480 screen drawn at (x, y) and scaled by `scale`.
struct SelectView {
    float x = 0, y = 0, scale = 1;
};

// Places `count` copies, at least 2, in a grid of equal cells with the last
// row centred, and returns how many places were written.
int selectViews(int count, SelectView* out);

}  // namespace bmx
