#include "mod/splitscreen.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/split_layout.hpp"
#include "core/view_window.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/frontend.hpp"
#include "mod/hud.hpp"
#include "mod/perf.hpp"
#include "mod/riders.hpp"

namespace bmx::split {
namespace {

using namespace bmx::game;

// One view per player. View 1 is the game's camera (rider 1) and view 2
// follows rider 2; the other views follow the extra riders and copy view 1
// until their riders exist.
constexpr int kMaxViews = kMaxPlayers;

// A camera of the mod's: the zobjCamera and its zrwCamera.
struct ExtraCamera {
    void* obj = nullptr;
    ZrwCamera* cam = nullptr;
};

struct State {
    Config cfg;
    bool gaveUp = false;  // the cameras could not be made: no split until rider 2 leaves

    ZrwCamera* main = nullptr;          // the game camera being split
    ExtraCamera extras[kMaxViews - 1];  // views 2 and up
    int extraCount = 0;

    bool applied = false;  // the viewports are split
    int appliedW = 0, appliedH = 0;
    int views = 0;
    ViewRect rects[kMaxViews];
    int fullMenu = 0;  // the in-game screen showing full screen, 0 for none

    uint32_t frame = 0;
    int lastGameState = -1, lastRiderCount = -1, lastTurn = -1;
};

State g;
hook::Hook<RenderFrameFn> g_renderFrame;
hook::Hook<ZrwDestroyFn> g_zrwDestroy;
hook::Hook<VoidFn> g_cameraUpdateAll;

// The screen's width / height.
float screenAspect() {
    const int h = screenHeight();
    return h > 0 ? static_cast<float>(screenWidth()) / h : 4.0f / 3.0f;
}

// Sets a camera's viewport from a normalised rectangle on a w x h screen,
// rounded to whole pixels.
ViewRect setViewportPixels(ZrwCamera* cam, const Viewport& v, int w, int h) {
    const float x0 = std::round(v.x * w), x1 = std::round((v.x + v.w) * w);
    const float y0 = std::round(v.y * h), y1 = std::round((v.y + v.h) * h);
    const float rect[4] = {x0, y0, x1 - x0, y1 - y0};
    fnp::ZrwSetViewport(cam, rect);
    return ViewRect{rect[0], rect[1], rect[2], rect[3]};
}

// Sets a camera's view window for its viewport's shape
// (core/view_window.hpp).
void applyViewWindow(ZrwCamera* cam) {
    const int16_t fov = field<int16_t>(cam, zrw::kFov);
    const int vw = field<int>(cam, zrw::kViewW), vh = field<int>(cam, zrw::kViewH);
    if (fov == 0 || vw <= 0 || vh <= 0) return;

    const float base = fnp::FovScale(static_cast<uint16_t>(fov)) * 0.5f;
    const ViewWindow win = computeViewWindow(base, static_cast<float>(vw) / vh, screenAspect(), g.cfg.widescreenFixFov);
    const float xy[2] = {win.x, win.y};
    fnp::RwSetViewWindow(field<RwCamera*>(cam, zrw::kRwCamera), xy);
}

// Gives `dst` the clip planes and fog of `src`, which the level sets.
void mirrorEnvironment(ZrwCamera* dst, ZrwCamera* src) {
    uint32_t& dstFlags = field<uint32_t>(dst, zrw::kFlags);
    dstFlags = (dstFlags & ~zrw::kFlagFog) | (field<uint32_t>(src, zrw::kFlags) & zrw::kFlagFog);
    std::memcpy(&field<uint8_t>(dst, zrw::kFogColor), &field<uint8_t>(src, zrw::kFogColor), 4);

    RwCamera* d = field<RwCamera*>(dst, zrw::kRwCamera);
    RwCamera* s = field<RwCamera*>(src, zrw::kRwCamera);
    if (field<float>(d, rwcam::kNearPlane) != field<float>(s, rwcam::kNearPlane))
        fnp::RwSetNearPlane(d, field<float>(s, rwcam::kNearPlane));
    if (field<float>(d, rwcam::kFarPlane) != field<float>(s, rwcam::kFarPlane))
        fnp::RwSetFarPlane(d, field<float>(s, rwcam::kFarPlane));
    if (field<float>(d, rwcam::kFogPlane) != field<float>(s, rwcam::kFogPlane))
        fnp::RwSetFogDistance(d, field<float>(s, rwcam::kFogPlane));
}

// Makes `dst` show what `src` shows: its transform, field of view, clip
// planes and fog.
void mirrorCamera(ZrwCamera* dst, ZrwCamera* src) {
    fnp::ZrwSetMatrix(dst, &field<float>(src, zrw::kMatrix), &field<float>(src, zrw::kPosition));
    field<int16_t>(dst, zrw::kFov) = field<int16_t>(src, zrw::kFov);
    mirrorEnvironment(dst, src);
}

// Destroys the mod's cameras. Each is forgotten first, so onZrwDestroy
// treats it as any other camera.
void destroyExtras() {
    while (g.extraCount > 0) {
        ExtraCamera& e = g.extras[--g.extraCount];
        void* obj = e.obj;
        e = {};
        fnp::ZobjDestroy(obj);
        BMX_LOG_INFO("split: extra camera %d destroyed", g.extraCount + 2);
    }
}

// Creates cameras up to `count`.
bool createExtras(int count) {
    while (g.extraCount < count) {
        void* obj = fnp::ZobjCameraCreate();
        if (!obj || !zobjCamera(obj)) {
            BMX_LOG_ERROR("split: could not create a camera for view %d", g.extraCount + 2);
            if (obj) fnp::ZobjDestroy(obj);
            return false;
        }
        g.extras[g.extraCount++] = {obj, zobjCamera(obj)};
        BMX_LOG_INFO("split: camera for view %d: %p (object %p)", g.extraCount + 1,
                     static_cast<void*>(zobjCamera(obj)), obj);
    }
    return true;
}

// The views of this level: one per player.
int wantedViews() { return std::clamp(frontend::players(), 2, kMaxViews); }

// Puts the game camera back on the full screen and destroys the mod's
// cameras rather than disabling them: new cameras go to the front of the
// camera pool, and the loading and legal screens draw with the first one.
// The game's SetFov, which the widescreen fix may replace, recomputes the
// view window.
void unsplit() {
    if (!g.applied) return;
    g.applied = false;
    destroyExtras();
    g.views = 0;
    if (g.main) {
        setViewportPixels(g.main, Viewport{}, screenWidth(), screenHeight());
        auto setFov = global<void(__cdecl*)(ZrwCamera*, uint32_t)>(addr::kCamApiSetFov);
        setFov(g.main, static_cast<uint16_t>(field<int16_t>(g.main, zrw::kFov)));
    }
    BMX_LOG_INFO("split: off");
}

// Logs each change of the game state, rider count or turn.
void logGameState() {
    const int state = global<uint8_t>(addr::kGameState);
    const int riders = global<uint8_t>(addr::kRiderCount);
    const int turn = global<uint8_t>(addr::kCurrentTurnPlayer);
    if (state != g.lastGameState || riders != g.lastRiderCount || turn != g.lastTurn) {
        BMX_LOG_INFO("game: frame %u state=%d riders=%d turn=%d mode=%u", g.frame, state, riders, turn,
                     global<uint8_t>(addr::kSessionMode));
        g.lastGameState = state;
        g.lastRiderCount = riders;
        g.lastTurn = turn;
    }
}

// The in-game screen showing (pause, level intro, challenges, results and
// so on), which shows on the whole screen.
int fullScreenMenu() { return global<uint8_t>(addr::kGameScreen); }

// Splits the screen before each frame while every rider is on the course and
// no in-game screen shows: lays the views out when needed, moves each extra
// camera's transform into RenderWare (or copies view 1 until its rider
// exists) and sets every view window. Menus, loading screens and results are
// never split.
void beforeRender() {
    ++g.frame;
    perf::frame();
    hud::beginFrame();
    logGameState();
    riders::tick();

    ZrwCamera* main = mainCamera();
    if (main != g.main) {
        destroyExtras();
        g.main = main;
        g.applied = false;
        if (main) BMX_LOG_INFO("split: game camera is %p", static_cast<void*>(main));
    }
    if (!main) return;

    const int w = screenWidth(), h = screenHeight();
    if (w <= 0 || h <= 0) return;

    const int menu = fullScreenMenu();
    if (menu != g.fullMenu) {
        if (menu && g.applied) BMX_LOG_INFO("split: full screen for in-game screen %d", menu);
        g.fullMenu = menu;
    }
    if (!riders::secondActive()) g.gaveUp = false;
    const bool wanted = riders::secondActive() && !menu && !g.gaveUp;
    if (!wanted) {
        unsplit();
        return;
    }

    const int views = wantedViews();
    if (g.applied && views != g.views) unsplit();
    if (!createExtras(views - 1)) {
        destroyExtras();
        g.gaveUp = true;
        return;
    }

    if (!g.applied || w != g.appliedW || h != g.appliedH) {
        const auto layout = computeSplitLayout(views, g.cfg.splitMode, static_cast<float>(w) / h);
        g.rects[0] = setViewportPixels(main, layout[0], w, h);
        for (int i = 0; i < g.extraCount; ++i) {
            g.rects[i + 1] = setViewportPixels(g.extras[i].cam, layout[i + 1], w, h);
            field<uint32_t>(g.extras[i].cam, zrw::kFlags) |= zrw::kFlagEnabled;
        }
        g.applied = true;
        g.views = views;
        g.appliedW = w;
        g.appliedH = h;
        BMX_LOG_INFO("split: on, %dx%d, %d views of %dx%d", w, h, views, field<int>(main, zrw::kViewW),
                     field<int>(main, zrw::kViewH));
    }

    for (int i = 0; i < g.extraCount; ++i) {
        ExtraCamera& e = g.extras[i];
        if (i == 0 || i - 1 < extras::count()) {
            fnp::ZobjCameraUpdate(e.obj);
            mirrorEnvironment(e.cam, main);
        } else {
            mirrorCamera(e.cam, main);
        }
    }
    applyViewWindow(main);
    for (int i = 0; i < g.extraCount; ++i) applyViewWindow(g.extras[i].cam);
}

// RenderFrame.
int __cdecl onRenderFrame() {
    beforeRender();
    return g_renderFrame.original();
}

// CameraUpdateAll: the game keeps a camera controller per rider but runs the
// full update only for the active one, into the one game camera. It runs
// again with controller 1 active and view 2's camera object standing in for
// the game's, then for each extra rider in slot 2 into his view's camera.
void __cdecl onCameraUpdateAll() {
    g_cameraUpdateAll.original();
    if (!g.applied || g.extraCount == 0 || !riders::secondActive()) return;

    uint8_t& active = global<uint8_t>(addr::kActiveCameraCtl);
    void*& gameCamera = global<void*>(addr::kGameCameraObject);
    const uint8_t savedActive = active;
    void* const savedCamera = gameCamera;
    active = 1;
    gameCamera = g.extras[0].obj;
    g_cameraUpdateAll.original();
    for (int k = 0; k < extras::count() && k + 1 < g.extraCount; ++k)
        extras::as(k, [&] {
            gameCamera = g.extras[k + 1].obj;
            g_cameraUpdateAll.original();
        });
    active = savedActive;
    gameCamera = savedCamera;
}

// ZrwCameraDestroy: a camera of the mod's destroyed by the engine (e.g. the
// camera pool's shutdown) is forgotten. With the game camera the mod's go
// too, as the camera system refuses resolution changes while it holds a
// camera it does not know.
int __cdecl onZrwDestroy(ZrwCamera* cam) {
    for (int i = 0; cam && i < g.extraCount; ++i) {
        if (cam != g.extras[i].cam) continue;
        for (int k = i; k + 1 < g.extraCount; ++k) g.extras[k] = g.extras[k + 1];
        g.extras[--g.extraCount] = {};
        g.applied = false;
        BMX_LOG_INFO("split: camera for view %d released by the game", i + 2);
        return g_zrwDestroy.original(cam);
    }
    if (cam && cam == g.main) {
        destroyExtras();
        g.main = nullptr;
        g.applied = false;
    }
    return g_zrwDestroy.original(cam);
}

}  // namespace

SplitMode mode() { return g.cfg.splitMode; }

void setMode(SplitMode mode) {
    if (mode == g.cfg.splitMode) return;
    g.cfg.splitMode = mode;
    g.appliedW = 0;
    BMX_LOG_INFO("split: mode %s", splitModeName(mode));
}

bool applied() { return g.applied && g.extraCount > 0; }

int viewCount() { return applied() ? g.views : 0; }

int viewOf(const void* zrwCamera) {
    if (!applied() || !zrwCamera) return -1;
    if (zrwCamera == g.main) return 0;
    for (int i = 0; i < g.extraCount; ++i)
        if (zrwCamera == g.extras[i].cam) return i + 1;
    return -1;
}

ViewRect viewRect(int view) { return (view >= 0 && view < g.views) ? g.rects[view] : ViewRect{}; }

void* viewCameraObject(int view) {
    if (!applied() || !riders::secondActive() || view < 1 || view > g.extraCount) return nullptr;
    return g.extras[view - 1].obj;
}

bool install(const Config& cfg) {
    g.cfg = cfg;
    bool ok = true;
    ok &= g_renderFrame.install(fn::RenderFrame, &onRenderFrame, "RenderFrame");
    ok &= g_zrwDestroy.install(fn::ZrwCameraDestroy, &onZrwDestroy, "ZrwCameraDestroy");
    ok &= g_cameraUpdateAll.install(fn::CameraUpdateAll, &onCameraUpdateAll, "CameraUpdateAll");
    if (ok) BMX_LOG_INFO("split: ready (mode %s)", splitModeName(cfg.splitMode));
    return ok;
}

}  // namespace bmx::split
