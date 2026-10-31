#pragma once
// The perspective view window of one split-screen view.
//
// The game computes its view window for a 4:3 screen: x = base and
// y = base * 0.75, with base = FovScale(fov) * 0.5. Each view keeps that 4:3
// picture as its reference:
// - a view from 4:3 up to the screen's width keeps the vertical field of view
//   and widens horizontally, as the widescreen fix does for the full screen;
// - a view wider than the screen (stacked split) takes the geometric mean of
//   keeping the full-screen horizontal field of view (cropped top and bottom)
//   and keeping the vertical one (wider at the sides);
// - a view narrower than 4:3 (side by side) keeps the 4:3 horizontal field of
//   view and extends vertically.

namespace bmx {

struct ViewWindow {
    float x = 0, y = 0;
};

// `base` is FovScale(fov) * 0.5, `viewAspect` and `screenAspect` are width /
// height of the view and of the screen, and `fovFactor` scales the result
// (the widescreen fix's FOVFactor, 1 for none).
ViewWindow computeViewWindow(float base, float viewAspect, float screenAspect, float fovFactor);

}  // namespace bmx
