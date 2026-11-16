#include "mod/rider_collision.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/riders.hpp"

namespace bmx::ridercollision {
namespace {

using namespace bmx::game;

// Two riders touch when the points the game tests riders at are closer than
// this: the height the game's moving-object hit test (0x42E2F0) allows for a
// rider above or below an object.
constexpr float kReach = 120.0f;
// A crash needs the riders to meet faster than this: the lowest impact speed
// the game's rider impact callback (0x436020) acts on.
constexpr float kMinImpactSpeed = 5.0f;
// Speeds within this fraction of the faster one count as the same.
constexpr float kSameSpeed = 0.1f;
// A rider knocks another down only above this share of his pedalling top
// speed; slower, the other rider is a wall to him.
constexpr float kFastShare = 0.75f;
// A knocked-down rider is a ghost for this long once he rides again, and
// after that until no other rider is within kReach of him: no one meets him,
// and he blinks, shown and hidden kBlinkMs at a time.
constexpr int kGhostMs = 3000;
constexpr int kBlinkMs = 100;
// Longest step the ghost time takes per update; longer gaps are a hitch.
constexpr DWORD kMaxStepMs = 100;

// What the game's moving-object hit does to the rider it hits: the body's
// first parts take the object's velocity scaled by kHitScale and lifted by
// kHitLift (0x54D1A0 and 0x54D054), the bail text is kHitBailText, and the
// impact sound waits kHitSoundWait frames.
constexpr float kHitScale = 1.05f;
constexpr float kHitLift = 15.0f;
constexpr uint8_t kHitBailText = 3;
constexpr uint8_t kHitSoundWait = 0x3C;
constexpr int kHitBodyParts[] = {0, 1, 2, 13};
constexpr int kHitImpactParts[] = {0, 13};
// GameEvent(id, position, a, b, 0) of the hit sounds.
struct HitSound {
    int id, a, b;
};
constexpr HitSound kHitSounds[] = {{0xBBC, 7, 0x7F}, {0xBBD, 1, 3}, {0xBBE, 1, 3}};

// CollideLine's hit, as CollideLine fills it for a world face (0x469B00):
// uint32 words, with the face record at kFace and the material at
// kMaterial. Material 0 sounds like the default surface.
namespace hit {
inline constexpr int kWords = 0xD8 / 4;
inline constexpr int kType = 0;       // 1 world face
inline constexpr int kPoint = 2;      // float[3]
inline constexpr int kFraction = 6;   // float, along the segment
inline constexpr int kNormal = 7;     // float[3]
inline constexpr int kPlane = 10;     // float, -dot(normal, point)
inline constexpr int kIsFace = 0xB;   // 1
inline constexpr int kFacePtr = 0xC;  // -> kFace
inline constexpr int kMaterialPtr = 0xE;  // -> kMaterial
inline constexpr int kFace = 0x12;    // normal and plane again, then flags
inline constexpr int kFaceTag = 0x5C;  // byte offset; 0x88
inline constexpr uint8_t kFaceTagValue = 0x88;
inline constexpr int kMaterial = 0x34;
}  // namespace hit

using SetModeFn = uint8_t(__cdecl*)(void* rider, int mode);
using AddImpactFn = void(__cdecl*)(void* rider, int part, float speed);
using GameEventFn = int(__cdecl*)(int id, const float* position, int a, int b, int c);
using TransformFn = void(__cdecl*)(float* out, const float* matrix, const float* v);
using RiderFn = void(__cdecl*)(uint8_t* rider);
using CollideLineFn = int(__cdecl*)(const float* from, const float* to, uint32_t* hit);
const auto RiderSetMode = reinterpret_cast<SetModeFn>(fn::RiderSetMode);
const auto RiderAddImpact = reinterpret_cast<AddImpactFn>(fn::RiderAddImpact);
const auto GameEvent = reinterpret_cast<GameEventFn>(fn::GameEvent);
const auto ImpactShake = reinterpret_cast<VoidFn>(fn::ImpactShake);
const auto TransformPoint = reinterpret_cast<TransformFn>(fn::TransformPoint);
const auto ActorShow = reinterpret_cast<void(__cdecl*)(void* actor)>(fn::ActorShow);
const auto ActorHide = reinterpret_cast<void(__cdecl*)(void* actor)>(fn::ActorHide);

hook::Hook<RiderFn> g_groundPost;
hook::Hook<CollideLineFn> g_collideLine;

struct Vec {
    float x = 0, y = 0, z = 0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(Vec v) { return std::sqrt(dot(v, v)); }

Vec vecAt(const uint8_t* rider, uint32_t offset) {
    const float* f = reinterpret_cast<const float*>(rider + offset);
    return {f[0], f[1], f[2]};
}

// The point of segment [a, b] nearest to p.
Vec nearestOnSegment(Vec p, Vec a, Vec b) {
    const Vec ab = b - a;
    const float len2 = dot(ab, ab);
    const float t = len2 > 0 ? std::clamp(dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return a + ab * t;
}

struct State {
    bool enabled = true;
    // Whether each pair (lower player first) can crash: false until the two
    // have been apart since the last restart or bail of either.
    std::array<std::array<bool, kMaxPlayers>, kMaxPlayers> armed{};
    // Per player: knocked down and not yet riding again; a ghost, with the
    // ghost time left (from kGhostMs) and the time he has been one.
    std::array<bool, kMaxPlayers> knocked{};
    std::array<bool, kMaxPlayers> ghost{};
    std::array<int, kMaxPlayers> ghostMs{};
    std::array<int, kMaxPlayers> ghostAgeMs{};
    DWORD lastTick = 0;              // of the last update while riding
    uint8_t* groundRider = nullptr;  // the rider inside RiderGroundPost
};
State g;

// A rider as the pair test sees him.
struct Rider {
    bool riding = false;  // active and riding (not bailing or reset)
    bool solid = false;   // riding, and not knocked down or a ghost
    Vec point;            // where the game tests him against moving objects
    Vec velocity;
    float speed = 0;
    float topSpeed = 0;   // pedalling top speed
    bool fast = false;    // above kFastShare of topSpeed
    Vec wheels[addr::rider::kWheelCount];  // wheel centres
};

Rider readRider(int player) {
    Rider r;
    const uint8_t* s = extras::riderOf(player);
    if (!s) return r;
    const uint8_t mode = s[addr::rider::kState];
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(s + addr::rider::kFlags);
    r.riding = (flags & 1) && mode != addr::rider::kModeBail && mode != addr::rider::kModeReset;
    r.solid = r.riding && !g.knocked[player] && !g.ghost[player];
    const Vec pos = vecAt(s, addr::rider::kPosition), offset = vecAt(s, addr::rider::kTestOffset);
    r.point = pos + offset;
    r.velocity = vecAt(s, addr::rider::kVelocity);
    r.speed = length(r.velocity);
    const float stat = *reinterpret_cast<const float*>(s + addr::rider::kSpeedStat);
    r.topSpeed = global<float>(addr::kPedalTopSpeed) * stat;
    r.fast = r.speed > kFastShare * r.topSpeed;
    const float* matrix = reinterpret_cast<const float*>(s + addr::rider::kBikeMatrix);
    const Vec centre = vecAt(s, addr::rider::kBikeCentre);
    for (int w = 0; w < addr::rider::kWheelCount; ++w) {
        float out[3];
        TransformPoint(out, matrix,
                       reinterpret_cast<const float*>(s + addr::rider::kWheels + w * addr::rider::kWheelStride));
        r.wheels[w] = Vec{out[0], out[1], out[2]} + centre;
    }
    return r;
}

// Who bails when riders a and b meet: the faster one knocks the slower one
// down when he is fast; at about the same speed, each fast rider knocks the
// other. Neither bails when the faster rider is not fast.
struct Outcome {
    bool aBails = false, bBails = false;
    bool any() const { return aBails || bBails; }
};
Outcome outcome(const Rider& a, const Rider& b) {
    const bool same = std::fabs(a.speed - b.speed) <= kSameSpeed * std::max(a.speed, b.speed);
    return {b.fast && (same || a.speed < b.speed), a.fast && (same || b.speed < a.speed)};
}

bool& armed(int a, int b) { return a < b ? g.armed[a][b] : g.armed[b][a]; }

int playerCount() { return std::min(2 + extras::count(), kMaxPlayers); }

// Returns whether the riders can meet now.
bool active() { return g.enabled && riders::secondActive() && global<uint8_t>(addr::kGameState) == 0; }

// Does to the rider in `slot` what the game's moving-object hit does
// (0x42E61B): he bails, his body takes the hitter's velocity, the impact is
// added, and the hit sounds and the shake play unless an impact sound played
// in the last kHitSoundWait frames.
void bail(int slot, Vec hitterVelocity) {
    uint8_t* r = riderState(slot);
    const float speed = length(hitterVelocity);
    RiderSetMode(r, addr::rider::kModeBail);
    r[addr::rider::kTrickName2] = kHitBailText;
    const Vec v{hitterVelocity.x * kHitScale, hitterVelocity.y * kHitScale - kHitLift, hitterVelocity.z * kHitScale};
    if (uint8_t* body = *reinterpret_cast<uint8_t**>(r + addr::rider::kBody)) {
        for (int part : kHitBodyParts) {
            float* pv = reinterpret_cast<float*>(body + addr::rider::kBodyPartVelocity +
                                                 part * addr::rider::kBodyPartStride);
            pv[0] = v.x;
            pv[1] = v.y;
            pv[2] = v.z;
        }
    }
    for (int part : kHitImpactParts) RiderAddImpact(r, part, speed);
    uint8_t& wait = r[addr::rider::kImpactWait];
    if (wait == 0) {
        const float* pos = reinterpret_cast<const float*>(r + addr::rider::kPosition);
        for (const HitSound& s : kHitSounds) GameEvent(s.id, pos, s.a, s.b, 0);
        ImpactShake();
        wait = kHitSoundWait;
    }
}

// Makes player `player` bail, hit at `hitterVelocity`, with his rider in his
// slot.
void bailPlayer(int player, Vec hitterVelocity) {
    g.knocked[player] = true;
    g.ghost[player] = false;
    if (player == 0) return bail(0, hitterVelocity);
    extras::asPlayer(player, [&] { bail(1, hitterVelocity); });
}

// Returns whether another riding rider is within kReach of player p.
bool someoneInside(const std::array<Rider, kMaxPlayers>& riders, int players, int p) {
    for (int q = 0; q < players; ++q)
        if (q != p && riders[q].riding && length(riders[q].point - riders[p].point) < kReach) return true;
    return false;
}

// Makes knocked-down riders riding again ghosts, counts the ghost time by the
// time since the last update, and ends a ghost whose time is over once no
// one is inside him.
void updateGhosts(const std::array<Rider, kMaxPlayers>& riders, int players) {
    const DWORD now = GetTickCount();
    const int step = g.lastTick ? static_cast<int>(std::min(now - g.lastTick, kMaxStepMs)) : 0;
    g.lastTick = now;
    for (int p = 0; p < players; ++p) {
        if (g.ghost[p]) {
            g.ghostMs[p] = std::max(0, g.ghostMs[p] - step);
            g.ghostAgeMs[p] += step;
            if (g.ghostMs[p] == 0 && !someoneInside(riders, players, p)) g.ghost[p] = false;
        }
        if (g.knocked[p] && riders[p].riding) {
            g.knocked[p] = false;
            g.ghost[p] = true;
            g.ghostMs[p] = kGhostMs;
            g.ghostAgeMs[p] = 0;
        }
    }
}

// ------------------------------------------------------------- the wall --
// A rider who is not fast enough to knock another down meets him as a wall:
// the game's own wheel test (RiderGroundPost) also tests its rays against the
// other riders' bikes, each the segment between his wheel centres widened by
// kWheelRaySide, and a bike it reaches first is reported as a wall face. The
// game then does what it does at a wall: the sound, a bail when the impact
// is hard enough, otherwise the bounce or the turn along it.

// Returns the player whose rider is `rider`, or -1.
int playerOfRider(const uint8_t* rider) {
    if (rider == riderState(0)) return 0;
    if (rider == riderState(1)) return extras::slotPlayer();
    return -1;
}

// Finds where the ray [from, to] of player `self`, moving with `velocity`,
// first reaches another rider's bike that is a wall to him. Each bike is
// tested in its rider's frame, so a bike moving away is reached later.
// Returns the fraction along the ray, or a negative value, with the contact
// point and the wall's normal.
float firstBike(int self, Vec from, Vec to, Vec& point, Vec& normal) {
    const Rider me = readRider(self);
    if (!me.solid) return -1;
    const float radius = global<float>(addr::kWheelRaySide);
    float best = -1;
    for (int p = 0; p < playerCount(); ++p) {
        if (p == self || !armed(self, p)) continue;
        const Rider other = readRider(p);
        if (!other.solid || outcome(me, other).any()) continue;
        const Vec a = other.wheels[0], b = other.wheels[1];
        const Vec end = to - other.velocity;  // the ray in the other rider's frame
        auto inside = [&](float t) {
            const Vec q = from + (end - from) * t;
            return length(q - nearestOnSegment(q, a, b)) <= radius;
        };
        // The first sample inside the bike, then the edge between it and the
        // sample before.
        constexpr int kSamples = 16, kRefine = 8;
        float lo = 0, hi = -1;
        for (int i = 0; i <= kSamples; ++i) {
            const float t = static_cast<float>(i) / kSamples;
            if (inside(t)) {
                hi = t;
                break;
            }
            lo = t;
        }
        if (hi < 0 || (best >= 0 && hi >= best)) continue;
        if (hi > 0)
            for (int i = 0; i < kRefine; ++i) {
                const float mid = (lo + hi) / 2;
                (inside(mid) ? hi : lo) = mid;
            }
        const Vec q = from + (end - from) * hi;
        Vec n = q - nearestOnSegment(q, a, b);
        n.y = 0;  // a wall, never a slope
        if (length(n) < 1e-3f) n = Vec{from.x - to.x, 0, from.z - to.z};
        if (length(n) < 1e-3f) continue;
        best = hi;
        normal = n * (1 / length(n));
        point = from + (to - from) * hi;
    }
    return best;
}

// Fills `out` as CollideLine fills a world face hit.
void writeWallHit(uint32_t* out, Vec point, float fraction, Vec normal) {
    std::memset(out, 0, hit::kWords * 4);
    auto f = [&](int word) -> float& { return *reinterpret_cast<float*>(out + word); };
    out[hit::kType] = 1;
    f(hit::kPoint) = point.x;
    f(hit::kPoint + 1) = point.y;
    f(hit::kPoint + 2) = point.z;
    f(hit::kFraction) = fraction;
    f(hit::kNormal) = f(hit::kFace) = normal.x;
    f(hit::kNormal + 1) = f(hit::kFace + 1) = normal.y;
    f(hit::kNormal + 2) = f(hit::kFace + 2) = normal.z;
    f(hit::kPlane) = f(hit::kFace + 3) = -dot(normal, point);
    out[hit::kIsFace] = 1;
    out[hit::kFacePtr] = reinterpret_cast<uintptr_t>(out + hit::kFace);
    out[hit::kMaterialPtr] = reinterpret_cast<uintptr_t>(out + hit::kMaterial);
    reinterpret_cast<uint8_t*>(out)[hit::kFaceTag] = hit::kFaceTagValue;
}

// RiderGroundPost: remembers the rider for the wheel test.
void __cdecl onGroundPost(uint8_t* rider) {
    uint8_t* const outer = g.groundRider;
    g.groundRider = rider;
    g_groundPost.original(rider);
    g.groundRider = outer;
}

// CollideLine: the wheel test also reaches the other riders' bikes, when one
// is nearer than the world.
int __cdecl onCollideLine(const float* from, const float* to, uint32_t* out) {
    const uintptr_t ret = BMX_RETURN_ADDRESS();
    const int result = g_collideLine.original(from, to, out);
    if (ret != fn::kWheelWallTest || !g.groundRider || !active()) return result;
    const int self = playerOfRider(g.groundRider);
    if (self < 0) return result;
    Vec point, normal;
    const float t = firstBike(self, {from[0], from[1], from[2]}, {to[0], to[1], to[2]}, point, normal);
    if (t < 0) return result;
    if (result && *reinterpret_cast<const float*>(out + hit::kFraction) <= t) return result;
    writeWallHit(out, point, t, normal);
    return 1;
}

}  // namespace

GhostBlink::GhostBlink() {
    if (!active()) return;
    for (int p = 0; p < playerCount(); ++p) {
        if (!g.ghost[p] || (g.ghostAgeMs[p] / kBlinkMs) % 2 == 0) continue;
        const uint8_t* s = extras::riderOf(p);
        if (!s) continue;
        for (uint32_t offset : {addr::rider::kBikeActor, addr::rider::kRiderActor}) {
            uint8_t* actor = *reinterpret_cast<uint8_t* const*>(s + offset);
            if (!actor || !(*reinterpret_cast<const uint32_t*>(actor + addr::kActorFlags) & addr::kActorVisible) ||
                count_ == static_cast<int>(hidden_.size()))
                continue;
            ActorHide(actor);
            hidden_[count_++] = actor;
        }
    }
}

GhostBlink::~GhostBlink() {
    for (int i = 0; i < count_; ++i) ActorShow(hidden_[i]);
}

bool install() {
    bool ok = true;
    ok &= g_groundPost.install(fn::RiderGroundPost, &onGroundPost, "RiderGroundPost");
    ok &= g_collideLine.install(fn::CollideLine, &onCollideLine, "CollideLine");
    if (ok) BMX_LOG_INFO("collision: ready (%s)", g.enabled ? "on" : "off");
    return ok;
}

void setEnabled(bool on) {
    if (on != g.enabled) BMX_LOG_INFO("collision: %s", on ? "on" : "off");
    g.enabled = on;
}

bool enabled() { return g.enabled; }

void restart() {
    g.armed = {};
    g.knocked = {};
    g.ghost = {};
}

void update() {
    if (!active()) {
        g.lastTick = 0;
        return;
    }
    const int players = playerCount();
    std::array<Rider, kMaxPlayers> riders;
    for (int p = 0; p < players; ++p) riders[p] = readRider(p);
    updateGhosts(riders, players);

    for (int a = 0; a < players; ++a) {
        for (int b = a + 1; b < players; ++b) {
            bool& pairArmed = g.armed[a][b];
            Rider &ra = riders[a], &rb = riders[b];
            if (!ra.solid || !rb.solid) {
                pairArmed = false;
                continue;
            }
            const float distance = length(ra.point - rb.point);
            if (distance >= kReach) {
                pairArmed = true;
                continue;
            }
            const float impact = length(ra.velocity - rb.velocity);
            if (!pairArmed || impact <= kMinImpactSpeed) continue;
            const Outcome o = outcome(ra, rb);
            if (!o.any()) continue;
            BMX_LOG_INFO("collision: players %d and %d, %.1f apart at speeds %.1f and %.1f (top %.1f and %.1f): %s",
                         a + 1, b + 1, distance, ra.speed, rb.speed, ra.topSpeed, rb.topSpeed,
                         o.aBails && o.bBails ? "both bail"
                         : o.aBails           ? "the first bails"
                                              : "the second bails");
            if (o.aBails) bailPlayer(a, rb.velocity);
            if (o.bBails) bailPlayer(b, ra.velocity);
            ra.solid = !o.aBails;
            rb.solid = !o.bBails;
            pairArmed = false;
        }
    }
}

}  // namespace bmx::ridercollision
