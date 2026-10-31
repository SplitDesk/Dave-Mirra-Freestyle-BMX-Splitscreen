#include "mod/riders.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/frontend.hpp"
#include "mod/hud.hpp"
#include "mod/input.hpp"
#include "mod/pause_options.hpp"
#include "mod/players_score.hpp"
#include "mod/render_state.hpp"

namespace bmx::riders {
namespace {

using namespace bmx::game;

struct State {
    bool forcing = false;     // inside GameStart, before the models finished loading
    bool active = false;      // a level is running with rider 2
    bool seenRiders = false;  // inside a SeenRiders, which changes the count on purpose
    uint32_t restarts = 0;
    DWORD levelStartTick = 0;
    DWORD lastReportTick = 0;
};
State g;

using LangFn = void(__cdecl*)(int);
hook::Hook<IntFn> g_gameStart;
hook::Hook<LangFn> g_loadHudStrings;
hook::Hook<SlotIntFn> g_riderRestart;
hook::Hook<SlotIntFn> g_cameraCtlSetMode;
hook::Hook<VoidFn> g_replaysDestroy;
hook::Hook<IntFn> g_getSessionMode;
hook::Hook<uint32_t(__cdecl*)(int)> g_turnSystem;
hook::Hook<VoidFn> g_poseActiveRider;
using CreateRiderFn = int(__cdecl*)(int slot, int character, int outfit, int bike);
hook::Hook<CreateRiderFn> g_createRider;
hook::Hook<VoidFn> g_ridersUpdate;
const auto RidersReadInput = reinterpret_cast<VoidFn>(fn::RidersReadInput);
const auto RidersApplyInput = reinterpret_cast<VoidFn>(fn::RidersApplyInput);
const auto RiderReset = reinterpret_cast<SlotIntFn>(fn::RiderReset);
const auto RiderRunSetup = reinterpret_cast<void(__cdecl*)(int slot)>(fn::RiderRunSetup);
const auto CameraCtlInit = reinterpret_cast<SlotIntFn>(fn::CameraCtlInit);
const auto PoseActor = reinterpret_cast<void(__cdecl*)(void* actor)>(fn::PoseActor);

constexpr int kSimultaneousMode = 11;
uint32_t g_simultaneousAnswers = 0;

uint8_t& riderCount() { return global<uint8_t>(addr::kRiderCount); }

// ----------------------------------------------------------------- shadows --
// The game has one shadow: a descriptor, overwritten by each rider created; a
// compute callback that fills one shared shadow grid around that rider; and a
// draw callback that draws the grid. Per camera the draw runs first, with the
// grid of the last compute, then the compute. With more riders, each rider's
// descriptor is kept, and the draw step computes and draws each rider's
// shadow in turn, so every rider has a shadow in every view.
struct ShadowDesc {
    uint8_t bytes[0x20]{};
    void* models[2]{};
    bool valid = false;
};
std::vector<ShadowDesc> g_shadow;  // by player
bool g_shadowComputeAllowed = false;
bool g_shadowLogged = false;
using ShadowFn = int(__cdecl*)(void* desc);
using ShadowDrawFn = int(__cdecl*)();
hook::Hook<ShadowFn> g_shadowCompute;
hook::Hook<ShadowDrawFn> g_shadowDraw;

// Keeps the shadow descriptor the game just set up for a new rider.
void keepShadow(int player) {
    if (player < 0) return;
    if (player >= static_cast<int>(g_shadow.size())) g_shadow.resize(player + 1);
    ShadowDesc& s = g_shadow[player];
    std::memcpy(s.bytes, reinterpret_cast<void*>(fn::kShadowDesc), sizeof s.bytes);
    void** models = *reinterpret_cast<void***>(fn::kShadowDesc + 4);
    s.models[0] = models ? models[0] : nullptr;
    s.models[1] = models ? models[1] : nullptr;
    *reinterpret_cast<void***>(s.bytes + 4) = s.models;
    s.valid = models && s.models[0] && *reinterpret_cast<void**>(s.bytes + 8);
}

// Creates player `player`'s rider in a slot and keeps his shadow.
int createInSlot(int player, int slot, int character, int outfit, int bike) {
    const int result = g_createRider.original(slot, character, outfit, bike);
    if (result) keepShadow(player);
    return result;
}

// CreateRider: keeps each rider's shadow; riders 3 and up are made right
// after rider 2, in his slot, so the level start loads their models too.
int __cdecl onCreateRider(int slot, int character, int outfit, int bike) {
    const int result = g_createRider.original(slot, character, outfit, bike);
    if (result && slot >= 0 && slot < 2) keepShadow(slot);
    if (result && slot == 1 && g.forcing) extras::createAll(frontend::players(), &createInSlot);
    return result;
}

// Switches the shadow draw's depth test, an immediate operand in its code
// (push 0x4103; bit 0 set means no depth test).
void setShadowDepthTest(bool on) {
    auto* imm = reinterpret_cast<uint8_t*>(fn::kShadowDrawFlags);
    const uint8_t want = on ? 0x02 : 0x03;
    if (*imm == want) return;
    DWORD old = 0;
    VirtualProtect(imm, 1, PAGE_EXECUTE_READWRITE, &old);
    *imm = want;
    VirtualProtect(imm, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), imm, 1);
}

// Returns whether every rider's shadow is drawn by the mod.
bool everyShadow() { return secondActive() && g_shadow.size() >= 2 && g_shadow[0].valid && g_shadow[1].valid; }

// ShadowDraw (the compute): done in the draw step instead.
int __cdecl onShadowCompute(void* desc) {
    if (everyShadow() && !g_shadowComputeAllowed) return 1;
    return g_shadowCompute.original(desc);
}

// ShadowGridDraw: computes and draws every rider's shadow. The game draws
// its shadow without a depth test, so a shadow in front of a rider, seen from
// his camera, would be drawn across his body; these are depth-tested but,
// like the game's, write no depth.
int __cdecl onShadowDraw() {
    if (!everyShadow()) return g_shadowDraw.original();
    setShadowDepthTest(true);
    render::beginNoDepthWrite();
    const int riders = 2 + extras::count();
    for (int p = 0; p < riders && p < static_cast<int>(g_shadow.size()); ++p) {
        if (!g_shadow[p].valid) continue;
        *reinterpret_cast<void***>(g_shadow[p].bytes + 4) = g_shadow[p].models;
        g_shadowComputeAllowed = true;
        g_shadowCompute.original(g_shadow[p].bytes);
        g_shadowComputeAllowed = false;
        g_shadowDraw.original();
    }
    render::endNoDepthWrite();
    setShadowDepthTest(false);
    if (!g_shadowLogged) {
        g_shadowLogged = true;
        BMX_LOG_INFO("riders: drawing %d riders' shadows", riders);
    }
    return 1;
}

// ------------------------------------------------------------------- debug --
// Returns an actor's visibility bit, or -1 when it does not exist.
int actorVisible(const uint8_t* rider, uint32_t offset) {
    const uint8_t* actor = *reinterpret_cast<uint8_t* const*>(rider + offset);
    if (!actor) return -1;
    return *reinterpret_cast<const uint32_t*>(actor + 4) & 1;
}

// Logs where an actor's model is: actor +0x1D4 is its zobj, zobj +4 its
// world position.
void logActor(int slot, const char* name, const uint8_t* rider, uint32_t offset) {
    const uint8_t* actor = *reinterpret_cast<uint8_t* const*>(rider + offset);
    if (!actor) {
        BMX_LOG_DEBUG("riders:   slot %d %s actor missing", slot, name);
        return;
    }
    const uint8_t* zobj = *reinterpret_cast<uint8_t* const*>(actor + 0x1D4);
    if (!zobj) {
        BMX_LOG_DEBUG("riders:   slot %d %s actor %p has no model", slot, name, static_cast<const void*>(actor));
        return;
    }
    const float* p = reinterpret_cast<const float*>(zobj + 4);
    BMX_LOG_DEBUG("riders:   slot %d %s actor %p flags=0x%X model %p render=%p pos=(%.1f, %.1f, %.1f)", slot, name,
                 static_cast<const void*>(actor), *reinterpret_cast<const uint32_t*>(actor + 4),
                 static_cast<const void*>(zobj), *reinterpret_cast<void* const*>(zobj + 0x2C), p[0], p[1], p[2]);
}

// Logs both slots' actors and controller devices (record +0 is the device,
// device +0 its type).
void logModels() {
    for (int slot = 0; slot < 2; ++slot) {
        const uint8_t* r = riderState(slot);
        logActor(slot, "bike ", r, addr::rider::kBikeActor);
        logActor(slot, "rider", r, addr::rider::kRiderActor);
        const uint8_t* ctl = reinterpret_cast<const uint8_t*>(addr::kControllerArray + slot * addr::kControllerStride);
        const uint8_t* dev = *reinterpret_cast<uint8_t* const*>(ctl);
        BMX_LOG_DEBUG("riders:   controller %d device %p type=%d flags=0x%02X", slot, static_cast<const void*>(dev),
                     dev ? *reinterpret_cast<const int*>(dev) : -1, ctl[0x0C]);
    }
}

// Logs both slots' rider state, camera controller and PlayerConfig.
void logRiders(const char* when) {
    for (int slot = 0; slot < 2; ++slot) {
        const uint8_t* r = riderState(slot);
        const float* p = riderPosition(slot);
        const uint8_t* cfg = playerConfig(slot);
        const uint8_t* ctl = reinterpret_cast<const uint8_t*>(addr::kCameraCtlArray + slot * addr::kCameraCtlStride);
        BMX_LOG_DEBUG("riders: %s slot %d char=%u flags=0x%08X state=%u pos=(%.1f, %.1f, %.1f) "
                     "visible bike=%d rider=%d buttons=0x%08X cam mode=%u view=%u cfg=[%u %u %u %u %u %u %u]",
                     when, slot, r[0], *reinterpret_cast<const uint32_t*>(r + addr::rider::kFlags),
                     r[addr::rider::kState], p[0], p[1], p[2], actorVisible(r, addr::rider::kBikeActor),
                     actorVisible(r, addr::rider::kRiderActor),
                     *reinterpret_cast<const uint32_t*>(r + addr::rider::kButtonsHeld), ctl[0], ctl[2], cfg[0],
                     cfg[1], cfg[2], cfg[3], cfg[4], cfg[5], cfg[6]);
    }
}

// ------------------------------------------------------------ level start --
// Does what CameraInitAll (0x4056C0) does for each rider, for one slot; it
// cannot run again, as it also registers a draw callback. The slot takes
// player 1's chase camera type, so every view looks the same.
void initCameraController(int slot) {
    uint8_t* ctl = reinterpret_cast<uint8_t*>(addr::kCameraCtlArray + slot * addr::kCameraCtlStride);
    const uint8_t* cfg = playerConfig(0);
    ctl[2] = 0xFF;
    CameraCtlInit(slot, cfg[addr::kPlayerCfgCamera]);
    ctl[0] = 0xFF;
    g_cameraCtlSetMode.original(slot, 0);
    ctl[3] = 0;
    ctl[10] = 1;
    ctl[11] = 0;
}

// Sets the rider in slot 2 up as GameStart does rider 1 (while the count was
// 1): his camera controller in rider 1's mode, and his start position.
void startSlot2() {
    const uint8_t* ctl0 = reinterpret_cast<const uint8_t*>(addr::kCameraCtlArray);
    initCameraController(1);
    g_cameraCtlSetMode.original(1, ctl0[0]);
    g_riderRestart.original(1, 1);
}

// GameStart: with 2 or more players the count is 2 while the riders are
// created and loaded, then 2 again once the start is done.
int __cdecl onGameStart() {
    const bool force = riderCount() == 1 && frontend::players() >= 2;
    BMX_LOG_INFO("riders: level start, count=%u mode=%u sub=%u level=%u%s", riderCount(),
                 global<uint8_t>(addr::kSessionMode), global<uint8_t>(addr::kSessionSubMode),
                 global<uint8_t>(addr::kLevel), force ? " -> adding rider 2" : "");
    g.active = false;
    if (force) {
        riderCount() = 2;
        g.forcing = true;
    }

    const int result = g_gameStart.original();

    if (force) {
        const bool loaded = !g.forcing;
        g.forcing = false;
        if (result == 0 && loaded) {
            riderCount() = 2;
            g.active = true;
            g.restarts = 0;
            g.levelStartTick = g.lastReportTick = GetTickCount();
            startSlot2();
            extras::forEach(startSlot2);
            pauseoptions::shareWithPlayers();
            logRiders("after start");
            logModels();
        } else {
            riderCount() = 1;
            extras::levelEnd();
            BMX_LOG_WARN("riders: level start did not finish (result %d, loaded %d), rider 2 not added",
                         result, loaded);
        }
    }
    return result;
}

// LoadHudStrings runs inside GameStart right after the rider models load;
// the rest of GameStart (HUD text, cameras, replays, session flow) sees the
// single-player count.
void __cdecl onLoadHudStrings(int lang) {
    if (g.forcing) {
        riderCount() = 1;
        g.forcing = false;
    }
    g_loadHudStrings.original(lang);
}

// ------------------------------------------------------- following rider 1 --
// RiderRestart: every rider restarts with rider 1.
void __cdecl onRiderRestart(int slot, int show) {
    g_riderRestart.original(slot, show);
    if (slot == 0 && secondActive()) {
        g_riderRestart.original(1, show);
        extras::forEach([&] { g_riderRestart.original(1, show); });
        if (g.restarts++ < 8) logRiders(show ? "restart (show)" : "restart (hide)");
    }
}

// CameraCtlSetMode: every camera follows rider 1's mode (chase, the flyby
// before a run, and so on).
void __cdecl onCameraCtlSetMode(int slot, int mode) {
    g_cameraCtlSetMode.original(slot, mode);
    if (slot == 0 && secondActive()) {
        g_cameraCtlSetMode.original(1, mode);
        extras::forEach([&] { g_cameraCtlSetMode.original(1, mode); });
    }
}

// ReplaysDestroy, at the level end, destroys one replay per rider plus a
// shared one when the count is above 1. Only rider 1's replay was created
// (GameStart saw a count of 1 then), so it runs with a count of 1; the mod's
// level state ends here too.
void __cdecl onReplaysDestroy() {
    hud::levelEnd();
    playersscore::levelEnd();
    if (secondActive()) {
        riderCount() = 1;
        g.active = false;
        extras::levelEnd();
        BMX_LOG_INFO("riders: level shutdown, rider 2 removed");
    }
    g_replaysDestroy.original();
}

// ------------------------------------------------------------- turn system --
// Does what the run-start reset loop of TurnSystem (0x40AD96) does for each
// rider, for an extra rider in slot 2; the loop knows only two riders. His
// restart came with player 1's, and his HUD gets the score and trick name
// resets routed with rider 2's.
void resetForRun() {
    RiderRunSetup(1);
    uint8_t* r = riderState(1);
    const uint8_t* cfg = playerConfig(1);
    r[0x04] = cfg[2];
    field<int32_t>(r, addr::rider::kRunScore) = 0;
    r[0xEC] = r[0xED] = 0;
    std::memset(r + 0xEF, 0, 0x1D6);
    std::memset(r + 0x2C6, 0, 0xEE);
    r[addr::rider::kTrickName] = 0xFF;
    if (g_getSessionMode.original() == 0 && (cfg[0] & 2)) field<uint32_t>(r, 0x3BC) = 0x3FFFFF;
}

// GetSessionMode: the turn system hides every rider whose turn it is not,
// and passes the turn on after a run, unless the session mode is 11 (every
// rider rides at once). Those two calls get 11 while rider 2 is on the
// course; all others see the real mode. At the run start the reset loop then
// resets rider 2 too, HUD score and trick name included, which go to the
// slot-2 riders' HUDs.
int __cdecl onGetSessionMode() {
    const uintptr_t ret = BMX_RETURN_ADDRESS();
    if ((ret == fn::TurnStartModeCheck || ret == fn::TurnEndModeCheck) && secondActive()) {
        if (g_simultaneousAnswers++ < 6)
            BMX_LOG_INFO("riders: turn system asked for the mode at 0x%08X, answering %d",
                         static_cast<unsigned>(ret), kSimultaneousMode);
        if (ret == fn::TurnStartModeCheck) {
            hud::routeNextToRiders(1);
            hud::routeNextToRiders(8);
            extras::forEach(resetForRun);
        }
        return kSimultaneousMode;
    }
    return g_getSessionMode.original();
}

// TurnSystem at the run end: every rider rides at once, so one run end
// finishes the run for all. The turn system sees one rider, as in single
// player, so it saves the career and shows the results instead of handing
// the next turn to rider 2, which never saves. The other riders get the reset
// it gives every rider there. The scores are recorded first, as the results
// open inside the run end.
uint32_t __cdecl onTurnSystem(int event) {
    if (event != fn::kTurnRunEnd || !secondActive() || riderCount() < 2) return g_turnSystem.original(event);
    playersscore::runOver();
    uint32_t result;
    {
        SeenRiders seen(1);
        result = g_turnSystem.original(event);
    }
    RiderReset(1, 0);
    extras::forEach([] { RiderReset(1, 0); });
    BMX_LOG_INFO("riders: run over for every rider (turn system saw 1 rider, state now %u)", result);
    return result;
}

// ------------------------------------------------------------- per frame --
// Poses the bike and body of the rider in `slot`: skeleton, then world
// placement.
void poseRider(int slot) {
    uint8_t* r = riderState(slot);
    void* bike = *reinterpret_cast<void**>(r + addr::rider::kBikeActor);
    void* body = *reinterpret_cast<void**>(r + addr::rider::kRiderActor);
    if (bike) PoseActor(bike);
    if (body) PoseActor(body);
}

// PoseActiveRider: the game poses only the rider whose camera is active; the
// others are posed too.
void __cdecl onPoseActiveRider() {
    g_poseActiveRider.original();
    if (!secondActive()) return;
    const int active = global<uint8_t>(addr::kActiveCameraCtl);
    for (int slot = 0; slot < 2; ++slot)
        if (slot != active) poseRider(slot);
    extras::forEach([] { poseRider(1); });
}

// RidersUpdate from the main loop reads the controllers, turns them into
// rider controls, then runs physics and animation. It runs again for each
// extra rider in slot 2, with rider 1 left out: the last two steps handle
// only riders whose active bit is set.
void __cdecl onRidersUpdate() {
    g_ridersUpdate.original();
    if (!secondActive() || extras::count() == 0 || BMX_RETURN_ADDRESS() != fn::kRidersUpdateFromMain) return;
    uint32_t& rider1Flags = field<uint32_t>(riderState(0), addr::rider::kFlags);
    const uint32_t saved = rider1Flags;
    for (int k = 0; k < extras::count(); ++k)
        extras::as(k, [&] {
            input::fillController(1, extras::playerOf(k));
            RidersReadInput();
            rider1Flags = saved & ~1u;
            RidersApplyInput();
            g_ridersUpdate.original();
            rider1Flags = saved;
        });
}

}  // namespace

bool secondActive() {
    if (g.active && riderCount() != 2 && !g.seenRiders) {
        g.active = false;
        BMX_LOG_INFO("riders: rider 2 gone (count=%u)", riderCount());
    }
    return g.active;
}

SeenRiders::SeenRiders(uint8_t count) : count_(riderCount()), was_(g.seenRiders) {
    g.seenRiders = true;
    riderCount() = count;
}

SeenRiders::~SeenRiders() {
    riderCount() = count_;
    g.seenRiders = was_;
}

void tick() {
    if (!secondActive() || !log::enabled(LogLevel::Debug)) return;
    // Once a second for the first 30 seconds of each level.
    const DWORD now = GetTickCount();
    if (now - g.levelStartTick < 30000 && now - g.lastReportTick >= 1000) {
        g.lastReportTick = now;
        logRiders("status");
        if ((now - g.levelStartTick) / 1000 % 5 == 0) logModels();
    }
}

bool install() {
    bool ok = true;
    ok &= g_gameStart.install(fn::GameStart, &onGameStart, "GameStart");
    ok &= g_loadHudStrings.install(fn::LoadHudStrings, &onLoadHudStrings, "LoadHudStrings");
    ok &= g_riderRestart.install(fn::RiderRestart, &onRiderRestart, "RiderRestart");
    ok &= g_cameraCtlSetMode.install(fn::CameraCtlSetMode, &onCameraCtlSetMode, "CameraCtlSetMode");
    ok &= g_replaysDestroy.install(fn::ReplaysDestroy, &onReplaysDestroy, "ReplaysDestroy");
    ok &= g_getSessionMode.install(fn::GetSessionMode, &onGetSessionMode, "GetSessionMode");
    ok &= g_turnSystem.install(fn::TurnSystem, &onTurnSystem, "TurnSystem");
    ok &= g_poseActiveRider.install(fn::PoseActiveRider, &onPoseActiveRider, "PoseActiveRider");
    ok &= g_createRider.install(fn::CreateRider, &onCreateRider, "CreateRider");
    ok &= g_ridersUpdate.install(fn::RidersUpdate, &onRidersUpdate, "RidersUpdate");
    ok &= g_shadowCompute.install(fn::ShadowDraw, &onShadowCompute, "ShadowCompute");
    ok &= g_shadowDraw.install(fn::ShadowGridDraw, &onShadowDraw, "ShadowGridDraw");
    if (*reinterpret_cast<const uint8_t*>(fn::kShadowDrawFlags - 1) != 0x68 ||
        *reinterpret_cast<const uint8_t*>(fn::kShadowDrawFlags) != 0x03) {
        BMX_LOG_ERROR("riders: shadow draw code is not as expected");
        return false;
    }
    if (ok) BMX_LOG_INFO("riders: ready");
    return ok;
}

}  // namespace bmx::riders
