#pragma once
// Control over zgl's render states (zrwZgl.c).

namespace bmx::render {

// Hooks zgl's render flag setup.
bool install();

// Between begin and end, which nest, what zgl draws keeps its depth test but
// writes no depth. zgl's flags switch test and write together, so the write
// is turned off again each time zgl applies its flags.
void beginNoDepthWrite();
void endNoDepthWrite();

}  // namespace bmx::render
