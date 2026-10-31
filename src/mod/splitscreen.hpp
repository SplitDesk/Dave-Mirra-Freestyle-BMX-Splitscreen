#pragma once
// The split screen: one view per player, each with its own camera following
// its player's rider and drawn into its own part of the screen. View 1 is the
// game's own camera; the others are cameras the mod creates.

#include "common/config.hpp"

namespace bmx::split {

// Hooks the frame render, the camera update and the camera destruction.
bool install(const Config& cfg);

// A view's rectangle on the screen, in pixels.
struct ViewRect {
    float x = 0, y = 0, w = 0, h = 0;
};

// The split mode (Split.Mode). A change shows the next time the views are
// laid out, e.g. after the in-game screen that changed it.
SplitMode mode();
void setMode(SplitMode mode);

// Returns whether the screen is split, and into how many views (0 when not).
bool applied();
int viewCount();

// Returns the view an engine camera draws (0 player 1, 1 player 2 and so
// on), or -1 when it is not a split camera.
int viewOf(const void* zrwCamera);

// Returns a view's rectangle.
ViewRect viewRect(int view);

// Returns the camera object (a zobjCamera) of view 2 and up while the screen
// is split, otherwise nullptr; view 1's is the game's.
void* viewCameraObject(int view);

}  // namespace bmx::split
