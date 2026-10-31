#include "mod/audio.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "common/log.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/splitscreen.hpp"

namespace bmx::audio {
namespace {

using namespace bmx::game;

// GameEvent events: the sound mode (a: 0/1 off, 2 the level's sounds) and the
// per-frame update of a rider slot's bike loops (b >> 28 is the slot, c the
// tyre volume, d the pedal volume).
constexpr int kSoundModeEvent = 0x03E8;
constexpr int kRiderLoopEvent = 0x0FA0;
// Samples of the in-game effects bank.
constexpr int kTireLoopSample = 0;
constexpr int kPedalLoopSample = 9;
constexpr uint16_t kInvalidHandle = 0xFFFF;

// The bike loops of one extra rider and his distance squared to the nearest
// camera.
struct RiderLoops {
    uint16_t tire = kInvalidHandle;
    uint16_t pedal = kInvalidHandle;
    float distSq = 0.0f;
};
std::vector<RiderLoops> g_extraLoops;

using ListenerSpaceFn = void(__cdecl*)(const float* world, float* out);
using GameEventFn = int(__cdecl*)(int event, int a, int b, int c, int d);
hook::Hook<ListenerSpaceFn> g_listenerSpace;
hook::Hook<GameEventFn> g_gameEvent;

const auto CameraMatrix = reinterpret_cast<const float*(__cdecl*)(void* zobjCamera, int type)>(fn::ZobjCameraMatrix);
const auto TransformPoint = reinterpret_cast<void(__cdecl*)(float* out, const float* matrix, const float* in)>(fn::TransformPoint);
const auto SfxBankSample = reinterpret_cast<void*(__cdecl*)(uint32_t bank, int sampleIdx)>(fn::SfxBankSample);
const auto SndSrcCreate = reinterpret_cast<uint16_t(__cdecl*)(void* sample, const float* pos, int flags)>(fn::SndSrcCreate);
const auto SndSrcDestroy = reinterpret_cast<void(__cdecl*)(uint32_t handle)>(fn::SndSrcDestroy);
const auto SndSrcSetVolume = reinterpret_cast<int(__cdecl*)(uint32_t handle, uint32_t volume)>(fn::SndSrcSetVolume);

float lengthSquared(const float* v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }

// Puts a sound's position into the space of the nearest view's camera: the
// volume falls off with that distance and the pan follows that camera.
void __cdecl onListenerSpace(const float* world, float* out) {
    g_listenerSpace.original(world, out);
    for (int view = 1; view < split::viewCount(); ++view) {
        void* camera = split::viewCameraObject(view);
        if (!camera) continue;
        float other[3];
        TransformPoint(other, CameraMatrix(camera, 0), world);
        if (lengthSquared(other) < lengthSquared(out)) {
            out[0] = other[0];
            out[1] = other[1];
            out[2] = other[2];
        }
    }
}

// Destroys a sound source and clears its handle.
void releaseHandle(uint16_t& handle) {
    if (handle != kInvalidHandle) {
        SndSrcDestroy(handle);
        handle = kInvalidHandle;
    }
}

void destroyExtraLoops() {
    for (RiderLoops& loops : g_extraLoops) {
        releaseHandle(loops.tire);
        releaseHandle(loops.pedal);
    }
    g_extraLoops.clear();
}

// Frees one sound source of the farthest extra rider other than `selfK`, if
// he is farther than `selfDistSq`; used when the game's 24 sources are taken.
bool stealFarthestHandle(int selfK, float selfDistSq) {
    int victim = -1;
    bool victimPedal = false;
    float maxDistSq = selfDistSq;
    for (int i = 0; i < static_cast<int>(g_extraLoops.size()); ++i) {
        if (i == selfK) continue;
        const RiderLoops& r = g_extraLoops[i];
        if ((r.tire != kInvalidHandle || r.pedal != kInvalidHandle) && r.distSq > maxDistSq) {
            maxDistSq = r.distSq;
            victim = i;
            victimPedal = (r.pedal != kInvalidHandle);
        }
    }
    if (victim < 0) return false;
    releaseHandle(victimPedal ? g_extraLoops[victim].pedal : g_extraLoops[victim].tire);
    return true;
}

int g_noSourceLogged = 0;

// Creates a looping sound source of `sampleIdx` for extra rider `k`, taking
// one from a farther rider when none is free.
uint16_t acquireLoop(int k, float distSq, uint16_t bank, int sampleIdx) {
    const float initPos[3] = {0.0f, 20000.0f, 0.0f};
    void* sample = SfxBankSample(bank, sampleIdx);
    uint16_t h = SndSrcCreate(sample, initPos, 0);
    if (h == kInvalidHandle && stealFarthestHandle(k, distSq)) {
        h = SndSrcCreate(sample, initPos, 0);
    }
    if (h != kInvalidHandle) {
        SndSrcSetVolume(h, global<uint8_t>(addr::kSfxVolume));
    } else if (g_noSourceLogged < 10) {
        ++g_noSourceLogged;
        BMX_LOG_INFO("audio: no free sound source for player %d's bike sound (the game has 24)", extras::playerOf(k) + 1);
    }
    return h;
}

// The pool entry of a sound source.
uint8_t* sndSrcEntry(uint16_t handle) {
    return reinterpret_cast<uint8_t*>(addr::kSndSrcPool + static_cast<uintptr_t>(handle) * addr::kSndSrcStride);
}

// Runs the bike loop update of extra rider `k`, who shares slot 1 with rider
// 2. The game keeps one tyre and one pedal loop per slot (kTireLoopHandles,
// kPedalLoopHandles), so the rider's own loops are swapped into slot 1 for
// the call. Loops exist only while audible (moving and within hearing range)
// so the 24 sources go round any number of riders. The event asserts on an
// invalid handle, so a loop the rider lacks keeps slot 1's handle and its
// pool entry is restored after the call.
int riderLoopEvent(int k, int event, int a, int b, int c, int d) {
    if (k >= static_cast<int>(g_extraLoops.size())) g_extraLoops.resize(k + 1);
    RiderLoops& loops = g_extraLoops[k];

    uint16_t& slotTire = global<uint16_t>(addr::kTireLoopHandles + 2);
    uint16_t& slotPedal = global<uint16_t>(addr::kPedalLoopHandles + 2);
    const uint16_t bank = global<uint16_t>(addr::kGameSfxBank);
    if (slotTire == kInvalidHandle || slotPedal == kInvalidHandle || bank == kInvalidHandle || !a) {
        releaseHandle(loops.tire);
        releaseHandle(loops.pedal);
        return 0;
    }

    float local[3];
    onListenerSpace(reinterpret_cast<const float*>(a), local);
    loops.distSq = lengthSquared(local);
    const auto maxDistSq = static_cast<float>(global<uint32_t>(addr::kSndSrcMaxDistSq));
    const bool inRange = loops.distSq < maxDistSq;

    if (inRange && c > 0) {
        if (loops.tire == kInvalidHandle) loops.tire = acquireLoop(k, loops.distSq, bank, kTireLoopSample);
    } else {
        releaseHandle(loops.tire);
    }

    if (inRange && d > 0) {
        if (loops.pedal == kInvalidHandle) loops.pedal = acquireLoop(k, loops.distSq, bank, kPedalLoopSample);
    } else {
        releaseHandle(loops.pedal);
    }

    if (loops.tire == kInvalidHandle && loops.pedal == kInvalidHandle) return 0;

    std::array<uint8_t, addr::kSndSrcStride> savedTire{};
    std::array<uint8_t, addr::kSndSrcStride> savedPedal{};
    const bool swapTire = (loops.tire != kInvalidHandle);
    const bool swapPedal = (loops.pedal != kInvalidHandle);

    if (swapTire) {
        std::swap(slotTire, loops.tire);
    } else {
        std::memcpy(savedTire.data(), sndSrcEntry(slotTire), savedTire.size());
    }
    if (swapPedal) {
        std::swap(slotPedal, loops.pedal);
    } else {
        std::memcpy(savedPedal.data(), sndSrcEntry(slotPedal), savedPedal.size());
    }

    const int res = g_gameEvent.original(event, a, b, c, d);

    if (swapTire) {
        std::swap(slotTire, loops.tire);
    } else {
        std::memcpy(sndSrcEntry(slotTire), savedTire.data(), savedTire.size());
    }
    if (swapPedal) {
        std::swap(slotPedal, loops.pedal);
    } else {
        std::memcpy(sndSrcEntry(slotPedal), savedPedal.data(), savedPedal.size());
    }
    return res;
}

// GameEvent: frees the extra riders' loops when the level's sounds stop and
// runs slot 1's bike loop update for the extra rider being updated.
int __cdecl onGameEvent(int event, int a, int b, int c, int d) {
    if (event == kSoundModeEvent) {
        if (a <= 1) {
            destroyExtraLoops();
        } else if (a == 2 && global<uint16_t>(addr::kTireLoopHandles + 2) == kInvalidHandle) {
            g_extraLoops.clear();
        }
        return g_gameEvent.original(event, a, b, c, d);
    }
    if (event == kRiderLoopEvent && (static_cast<uint32_t>(b) >> 28) == 1) {
        const int k = extras::current();
        if (k >= 0) return riderLoopEvent(k, event, a, b, c, d);
    }
    return g_gameEvent.original(event, a, b, c, d);
}

}  // namespace

bool install() {
    bool ok = true;
    ok &= g_listenerSpace.install(fn::SoundListenerSpace, &onListenerSpace, "SoundListenerSpace");
    ok &= g_gameEvent.install(fn::GameEvent, &onGameEvent, "GameEvent");
    if (!ok) return false;
    BMX_LOG_INFO("audio: sounds are heard from the nearest view, per-rider bike loops ready");
    return true;
}

}  // namespace bmx::audio
