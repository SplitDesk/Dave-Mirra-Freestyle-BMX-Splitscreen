#pragma once
// Typed access to the game's engine (ZGE and RenderWare 3) objects and
// functions. The layouts are listed in docs/ADDRESS.md.

#include <cstdint>

#include "game/addresses.hpp"

namespace bmx::game {

// A RenderWare camera.
struct RwCamera;

// A ZGE camera ("zrwCamera", 200 bytes, from addr::kCameraPool).
struct ZrwCamera;

// Offsets into ZrwCamera.
namespace zrw {
inline constexpr uint32_t kFlags      = 0x08;  // uint32 flags (kFlag*)
inline constexpr uint32_t kRwCamera   = 0x0C;  // RwCamera*
inline constexpr uint32_t kMatrix     = 0x54;  // the 64-byte matrix last set, position at +0x30
inline constexpr uint32_t kPosition   = 0x84;  // float[3], kMatrix + 0x30
inline constexpr uint32_t kViewW      = 0xA4;  // int viewport width in pixels
inline constexpr uint32_t kViewH      = 0xA8;  // int viewport height in pixels
inline constexpr uint32_t kFov        = 0xBC;  // int16 field of view in game units (0x271C default)
inline constexpr uint32_t kFogColor   = 0xC0;  // 4 bytes

inline constexpr uint32_t kFlagFog     = 0x2;  // fog on
inline constexpr uint32_t kFlagEnabled = 0x4;  // rendered
}  // namespace zrw

// Offsets into RwCamera.
namespace rwcam {
inline constexpr uint32_t kNearPlane = 0x80;  // float
inline constexpr uint32_t kFarPlane  = 0x84;  // float
inline constexpr uint32_t kFogPlane  = 0x88;  // float
}  // namespace rwcam

// The value of type T at `offset` bytes into `base`.
template <typename T>
inline T& field(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset);
}
// The game variable of type T at `address`.
template <typename T>
inline T& global(uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

// Signatures of the engine functions the mod calls or hooks.
using RenderFrameFn      = int(__cdecl*)();
using ZrwDestroyFn       = int(__cdecl*)(ZrwCamera* cam);
using ZrwSetMatrixFn     = void(__cdecl*)(ZrwCamera* cam, const float* matrix, const float* position);
using ZrwSetViewportFn   = void(__cdecl*)(ZrwCamera* cam, const float* rectXYWH);
using FovScaleFn         = float(__cdecl*)(uint32_t fov);
using RwSetViewWindowFn  = RwCamera*(__cdecl*)(RwCamera* cam, const float* xy);
using RwSetPlaneFn       = RwCamera*(__cdecl*)(RwCamera* cam, float value);
using ZobjCreateFn       = void*(__cdecl*)();
using ZobjFn             = int(__cdecl*)(void* zobj);
using VoidFn             = void(__cdecl*)();
using IntFn              = int(__cdecl*)();
using SlotIntFn          = void(__cdecl*)(int slot, int value);

// The engine functions the mod calls.
namespace fnp {
inline const auto ZrwSetMatrix     = reinterpret_cast<ZrwSetMatrixFn>(fn::ZrwCameraSetMatrix);
inline const auto ZrwSetViewport   = reinterpret_cast<ZrwSetViewportFn>(fn::ZrwCameraSetViewport);
inline const auto FovScale         = reinterpret_cast<FovScaleFn>(fn::FovScale);
inline const auto RwSetViewWindow  = reinterpret_cast<RwSetViewWindowFn>(fn::RwCameraSetViewWindow);
inline const auto RwSetNearPlane   = reinterpret_cast<RwSetPlaneFn>(fn::RwCameraSetNearClipPlane);
inline const auto RwSetFarPlane    = reinterpret_cast<RwSetPlaneFn>(fn::RwCameraSetFarClipPlane);
inline const auto RwSetFogDistance = reinterpret_cast<RwSetPlaneFn>(fn::RwCameraSetFogDistance);
inline const auto ZobjCameraCreate = reinterpret_cast<ZobjCreateFn>(fn::ZobjCameraCreate);
inline const auto ZobjCameraUpdate = reinterpret_cast<ZobjFn>(fn::ZobjCameraUpdate);
inline const auto ZobjDestroy      = reinterpret_cast<ZobjFn>(fn::ZobjDestroy);
}  // namespace fnp

// The ZrwCamera of a zobjCamera.
inline ZrwCamera* zobjCamera(void* zobj) { return field<ZrwCamera*>(zobj, 0x2C); }

// An in-game screen's entry (addr::kGameScreenTable). The tick returns
// whether it acted.
struct GameScreen {
    void(__cdecl* create)(int show);
    int(__cdecl* tick)();
    void(__cdecl* draw)();
    uint8_t itemCount;
    uint8_t selected;
    uint8_t unused[2];
};
static_assert(sizeof(void*) != 4 || sizeof(GameScreen) == 16);

// The entry of the in-game screen showing.
inline GameScreen* gameScreen() { return global<GameScreen*>(addr::kGameScreenPtr); }

// The PlayerConfig, RiderState and position of a player slot.
inline uint8_t* playerConfig(int slot) {
    return reinterpret_cast<uint8_t*>(addr::kPlayerConfigArray + static_cast<uintptr_t>(slot) * addr::kPlayerConfigStride);
}

inline uint8_t* riderState(int slot) {
    return reinterpret_cast<uint8_t*>(addr::kRiderArray + static_cast<uintptr_t>(slot) * addr::kRiderStride);
}
inline const float* riderPosition(int slot) {
    return reinterpret_cast<const float*>(riderState(slot) + addr::rider::kMatrix + 0x40);
}

// The camera the game renders with, or nullptr before it exists.
inline ZrwCamera* mainCamera() {
    void* zobj = global<void*>(addr::kGameCameraObject);
    return zobj ? zobjCamera(zobj) : nullptr;
}

// The screen size in pixels.
inline int screenWidth()  { return global<int>(addr::kScreenWidth); }
inline int screenHeight() { return global<int>(addr::kScreenHeight); }

}  // namespace bmx::game
