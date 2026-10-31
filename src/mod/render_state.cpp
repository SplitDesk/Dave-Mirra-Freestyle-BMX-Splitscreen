#include "mod/render_state.hpp"

#include <cstdint>

#include "game/engine.hpp"
#include "hook/hook.hpp"

namespace bmx::render {
namespace {

using namespace bmx::game;

using RenderFlagsFn = int(__cdecl*)(uint32_t flags, int raster);
hook::Hook<RenderFlagsFn> g_renderFlags;
const auto RwRenderStateSet = reinterpret_cast<int(__cdecl*)(int state, int value)>(fn::RwRenderStateSet);
int g_noDepthWrite = 0;

// ZglSetRenderFlags: depth write off again while requested and depth is on.
int __cdecl onRenderFlags(uint32_t flags, int raster) {
    const int result = g_renderFlags.original(flags, raster);
    if (g_noDepthWrite > 0 && !(flags & 1)) RwRenderStateSet(fn::kRwStateZWrite, 0);
    return result;
}

// Returns whether the flags last applied have depth on (bit 0 clear).
bool depthOn() { return !(global<uint32_t>(fn::kZglCurrentFlags) & 1); }

}  // namespace

// zgl skips its state setup when the flags match the last ones applied, so
// the state is set here too.
void beginNoDepthWrite() {
    if (g_noDepthWrite++ == 0 && depthOn()) RwRenderStateSet(fn::kRwStateZWrite, 0);
}

void endNoDepthWrite() {
    if (g_noDepthWrite <= 0 || --g_noDepthWrite > 0) return;
    if (depthOn()) RwRenderStateSet(fn::kRwStateZWrite, 1);
}

bool install() { return g_renderFlags.install(fn::ZglSetRenderFlags, &onRenderFlags, "ZglSetRenderFlags"); }

}  // namespace bmx::render
