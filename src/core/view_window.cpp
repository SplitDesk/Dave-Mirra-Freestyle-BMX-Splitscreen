#include "core/view_window.hpp"

#include <algorithm>
#include <cmath>

namespace bmx {

ViewWindow computeViewWindow(float base, float viewAspect, float screenAspect, float fovFactor) {
    constexpr float k43 = 4.0f / 3.0f;
    if (viewAspect <= 0 || screenAspect <= 0) return {base, base * 0.75f};

    // Half-extents of the 4:3 reference picture.
    const float x43 = base * fovFactor;
    const float y43 = x43 * 0.75f;
    const float widest = std::max(screenAspect, k43);

    if (viewAspect < k43) return {x43, x43 / viewAspect};
    if (viewAspect <= widest) return {y43 * viewAspect, y43};
    const float yCrop = y43 * widest / viewAspect;
    const float y = std::sqrt(yCrop * y43);
    return {y * viewAspect, y};
}

}  // namespace bmx
