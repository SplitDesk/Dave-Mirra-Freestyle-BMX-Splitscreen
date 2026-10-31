#pragma once
// Where each player's view goes on the screen.

#include <vector>

#include "common/config.hpp"
#include "core/limits.hpp"

namespace bmx {

// A screen rectangle in normalised coordinates: (0, 0) top left, (1, 1)
// bottom right.
struct Viewport {
    float x = 0, y = 0, w = 1, h = 1;
};

// Returns one viewport per player (1..kMaxPlayers) covering the screen.
// `screenAspect` (width / height) decides between stacked and side-by-side
// views in Auto mode.
std::vector<Viewport> computeSplitLayout(int players, SplitMode mode, float screenAspect);

}  // namespace bmx
