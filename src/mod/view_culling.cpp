#include "mod/view_culling.hpp"

#include <cmath>

#include "common/log.hpp"
#include "game/engine.hpp"
#include "mod/extra_riders.hpp"
#include "mod/splitscreen.hpp"

namespace bmx::viewculling {
namespace {

using namespace bmx::game;

// A rider further than this from a view's rider is left out of a view the
// screen's full height; a view of a fraction of the height leaves him out at
// that fraction of the distance, where he is the same size on the screen.
constexpr float kFullHeightDistance = 8000.0f;

const auto ActorShow = reinterpret_cast<void(__cdecl*)(void* actor)>(fn::ActorShow);
const auto ActorHide = reinterpret_cast<void(__cdecl*)(void* actor)>(fn::ActorHide);

constexpr uint32_t kActors[] = {addr::rider::kBikeActor, addr::rider::kRiderActor};
constexpr int kActorsPerRider = 2;

struct State {
    bool enabled = false;
    bool logged = false;
    // The actors hidden for the views of this frame, shown again at its end.
    std::array<void*, kActorsPerRider * kMaxPlayers> hidden{};
    int hiddenCount = 0;
};
State g;

// Returns a rider's position, or false when he does not exist.
bool positionOf(int player, float out[3]) {
    const uint8_t* s = extras::riderOf(player);
    if (!s) return false;
    const float* p = reinterpret_cast<const float*>(s + addr::rider::kPosition);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    return true;
}

// The distance past which a rider is left out of `view`.
float reach(int view) {
    const float screenH = static_cast<float>(screenHeight());
    const float viewH = split::viewRect(view).h;
    if (screenH <= 0 || viewH <= 0) return kFullHeightDistance;
    return kFullHeightDistance * viewH / screenH;
}

bool isHidden(void* actor) {
    for (int i = 0; i < g.hiddenCount; ++i)
        if (g.hidden[i] == actor) return true;
    return false;
}

void forget(void* actor) {
    for (int i = 0; i < g.hiddenCount; ++i)
        if (g.hidden[i] == actor) {
            g.hidden[i] = g.hidden[--g.hiddenCount];
            return;
        }
}

}  // namespace

void setEnabled(bool on) {
    if (on != g.enabled) BMX_LOG_INFO("viewculling: performance mode %s", on ? "on" : "off");
    g.enabled = on;
}

bool enabled() { return g.enabled; }

bool drawn(int player, int view) {
    if (!g.enabled || view < 0 || player == view || split::viewCount() < 2) return true;
    float a[3], b[3];
    if (!positionOf(player, a) || !positionOf(view, b)) return true;
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    const float r = reach(view);
    return dx * dx + dy * dy + dz * dz <= r * r;
}

void beginView(int view) {
    if (view < 0 || split::viewCount() < 2) return;
    if (!g.logged && g.enabled) {
        g.logged = true;
        BMX_LOG_INFO("viewculling: riders past %.0f units are left out of view 1", reach(0));
    }
    const int players = 2 + extras::count();
    for (int p = 0; p < players && p < kMaxPlayers; ++p) {
        const uint8_t* s = extras::riderOf(p);
        if (!s) continue;
        const bool show = drawn(p, view);
        for (uint32_t offset : kActors) {
            void* actor = *reinterpret_cast<void* const*>(s + offset);
            if (!actor) continue;
            if (show && isHidden(actor)) {
                ActorShow(actor);
                forget(actor);
            } else if (!show && !isHidden(actor) &&
                       (*reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(actor) + addr::kActorFlags) &
                        addr::kActorVisible) &&
                       g.hiddenCount < static_cast<int>(g.hidden.size())) {
                ActorHide(actor);
                g.hidden[g.hiddenCount++] = actor;
            }
        }
    }
}

Frame::~Frame() {
    for (int i = 0; i < g.hiddenCount; ++i) ActorShow(g.hidden[i]);
    g.hiddenCount = 0;
}

}  // namespace bmx::viewculling
