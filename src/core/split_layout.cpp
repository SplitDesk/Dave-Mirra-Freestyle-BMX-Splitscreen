#include "core/split_layout.hpp"

#include <algorithm>
#include <cmath>

namespace bmx {
namespace {

std::vector<Viewport> grid(int players, int cols, int rows) {
    std::vector<Viewport> out;
    out.reserve(players);
    const float w = 1.0f / cols, h = 1.0f / rows;
    for (int i = 0; i < players; ++i) {
        const int r = i / cols, c = i % cols;
        // A short last row is shared out evenly across the full width.
        const int inRow = std::min(cols, players - r * cols);
        if (inRow < cols) {
            const float rw = 1.0f / inRow;
            out.push_back({c * rw, r * h, rw, h});
        } else {
            out.push_back({c * w, r * h, w, h});
        }
    }
    return out;
}

}  // namespace

std::vector<Viewport> computeSplitLayout(int players, SplitMode mode, float screenAspect) {
    players = std::clamp(players, 1, kMaxPlayers);
    if (players == 1) return {Viewport{}};

    if (mode == SplitMode::Auto) {
        if (players == 2) {
            // Two views sit side by side on wide screens and stacked on 4:3.
            mode = screenAspect >= 1.6f ? SplitMode::Vertical : SplitMode::Horizontal;
        } else {
            mode = SplitMode::Grid;
        }
    }

    switch (mode) {
        case SplitMode::Horizontal: return grid(players, 1, players);
        case SplitMode::Vertical:   return grid(players, players, 1);
        case SplitMode::Grid:
        default: {
            // Two rows: 3-4 players in 2x2, 5-6 in 3x2, 7-8 in 4x2.
            if (players == 2) return grid(players, 2, 1);
            const int cols = static_cast<int>(std::ceil(players / 2.0f));
            return grid(players, std::max(cols, 2), 2);
        }
    }
}

}  // namespace bmx
