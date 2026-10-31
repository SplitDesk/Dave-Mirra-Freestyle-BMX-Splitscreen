#include "core/select_panels.hpp"

#include <algorithm>
#include <cmath>

namespace bmx {

int selectViews(int count, SelectView* out) {
    const int n = std::max(2, count);
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));
    const int rows = (n + cols - 1) / cols;
    const float s = std::min(1.0f / cols, 1.0f / rows);
    const float w = 640 * s, h = 480 * s;
    const float y0 = (480 - rows * h) / 2;
    int i = 0;
    for (int row = 0; row < rows; ++row) {
        const int inRow = std::min(cols, n - row * cols);
        const float x0 = (640 - inRow * w) / 2;
        for (int c = 0; c < inRow; ++c, ++i) out[i] = {x0 + c * w, y0 + row * h, s};
    }
    return n;
}

}  // namespace bmx
