#include "mod/perf.hpp"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <iterator>
#include <cstdint>

#include "common/log.hpp"
#include "game/engine.hpp"
#include "mod/gui_buffers.hpp"

namespace bmx::perf {
namespace {

using namespace bmx::game;

constexpr double kIntervalSeconds = 60.0;
constexpr double kSlowFrameMs = 50.0;  // under 20 frames a second
constexpr int kMaxListWalk = 4096;     // the longest list counted

struct State {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER intervalStart{}, lastFrame{};
    bool started = false;
    int frames = 0, slowFrames = 0;
    double worstMs = 0;
    int guiPeak = 0;  // the most menu primitives in one frame
    double phaseMs[static_cast<int>(Phase::Count)] = {};
    int depth[static_cast<int>(Phase::Count)] = {};  // nested timers of a phase count once
};
State g;

double secondsBetween(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    return static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(g.frequency.QuadPart);
}

// Counts the nodes of a game list linked at `nextOffset`.
int listLength(const uint8_t* head, uint32_t nextOffset) {
    int n = 0;
    for (const uint8_t* p = head; p && n < kMaxListWalk; p = *reinterpret_cast<const uint8_t* const*>(p + nextOffset))
        ++n;
    return n;
}

// Counts the cameras in the camera pool: its first node at +4 (0x486250),
// each node's next at +0 (0x486260).
int cameraCount() {
    const uint8_t* pool = global<const uint8_t*>(addr::kCameraPool);
    if (!pool) return 0;
    return listLength(*reinterpret_cast<const uint8_t* const*>(pool + 4), 0);
}

// Writes the health line for the last `seconds`.
void report(double seconds) {
    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof mem;
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof mem);
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    constexpr double kMB = 1024.0 * 1024.0;
    const double frames = std::max(g.frames, 1);
    auto perFrame = [&](Phase p) { return g.phaseMs[static_cast<int>(p)] / frames; };
    BMX_LOG_INFO("perf: %.0f s, %d frames (%.1f fps), worst %.0f ms, %d over %.0f ms | a frame: %.1f ms, riders "
                 "%.1f ms, cameras %.1f ms, render %.1f ms (shadows %.1f, HUD %.1f) | memory %.1f MB (private "
                 "%.1f MB) | handles %lu, GDI %lu, USER %lu | cameras %d, GUI assets %d (free %d) | menu "
                 "primitives peak %d of %d",
                 seconds, g.frames, g.frames / seconds, g.worstMs, g.slowFrames, kSlowFrameMs,
                 seconds * 1000.0 / frames, perFrame(Phase::Riders), perFrame(Phase::Cameras),
                 perFrame(Phase::Render), perFrame(Phase::Shadows), perFrame(Phase::Hud),
                 mem.WorkingSetSize / kMB, mem.PrivateUsage / kMB, static_cast<unsigned long>(handles),
                 static_cast<unsigned long>(gdi), static_cast<unsigned long>(user), cameraCount(),
                 listLength(global<const uint8_t*>(addr::kGuiAssetList), 4),
                 listLength(global<const uint8_t*>(addr::kGuiAssetFree), 4), g.guiPeak, guibuffers::capacity());
}

}  // namespace

void frame() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g.started) {
        QueryPerformanceFrequency(&g.frequency);
        g.intervalStart = g.lastFrame = now;
        g.started = true;
        return;
    }
    const double ms = secondsBetween(g.lastFrame, now) * 1000.0;
    g.lastFrame = now;
    ++g.frames;
    g.worstMs = std::max(g.worstMs, ms);
    if (ms > kSlowFrameMs) ++g.slowFrames;
    g.guiPeak = std::max(g.guiPeak, guibuffers::used());

    const double seconds = secondsBetween(g.intervalStart, now);
    if (seconds < kIntervalSeconds) return;
    report(seconds);
    g.intervalStart = now;
    g.frames = g.slowFrames = 0;
    g.worstMs = 0;
    g.guiPeak = 0;
    std::fill(std::begin(g.phaseMs), std::end(g.phaseMs), 0.0);
}

Timer::Timer(Phase phase) : phase_(phase), start_(0) {
    if (g.depth[static_cast<int>(phase_)]++ > 0) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    start_ = now.QuadPart;
}

Timer::~Timer() {
    if (--g.depth[static_cast<int>(phase_)] > 0 || !g.started) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g.phaseMs[static_cast<int>(phase_)] +=
        static_cast<double>(now.QuadPart - start_) * 1000.0 / static_cast<double>(g.frequency.QuadPart);
}

}  // namespace bmx::perf
