#include "mod/test_players.hpp"

#include <windows.h>

#include <array>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "core/pad_mapping.hpp"
#include "game/engine.hpp"

namespace bmx::testplayers {
namespace {

using namespace bmx::game;

// Each test player rides this many controller updates after the one before.
constexpr int kDelayFrames = 15;
// Player 1's buttons are kept for the last test player's delay.
constexpr int kHistory = kDelayFrames * kMaxPlayers;
// On the rider select, confirm goes down and up every kPulseMs.
constexpr DWORD kPulseMs = 120;
// DirectInput keys in the game's keyboard state (bit 7 down).
constexpr int kKeyF9 = 0x43, kKeyF10 = 0x44;
constexpr uint8_t kKeyDown = 0x80;
// kGameState values.
constexpr uint8_t kRiding = 0, kFrontEnd = 0xFF;

struct State {
    bool enabled = false;
    std::array<uint32_t, kHistory> history{};  // player 1's buttons, a ring
    int head = 0;                              // the next entry
    int recorded = 0;                          // entries kept, up to kHistory
};
State g;

bool keyDown(int key) { return enabled() && (reinterpret_cast<const uint8_t*>(addr::kKeyboardState)[key] & kKeyDown); }

}  // namespace

void setEnabled(bool on) {
    g.enabled = on;
    if (on) BMX_LOG_INFO("testplayers: on (F9 adds and F10 removes test players on the rider select)");
}

bool enabled() { return g.enabled; }

uint32_t buttons(int device) {
    if (!g.enabled || !isTestDevice(device)) return 0;
    const uint8_t state = global<uint8_t>(addr::kGameState);
    if (state == kFrontEnd) return (GetTickCount() / kPulseMs) % 2 ? gamebits::kCross : 0;
    if (state != kRiding) return 0;
    const int delay = (indexOf(device) + 1) * kDelayFrames;
    if (delay > g.recorded) return 0;
    const uint32_t bits = g.history[(g.head - delay + kHistory) % kHistory];
    return bits & ~(gamebits::kStart | gamebits::kSelect);
}

void recordPlayer1(uint32_t bits) {
    if (!g.enabled || global<uint8_t>(addr::kGameState) != kRiding) return;
    g.history[g.head] = bits;
    g.head = (g.head + 1) % kHistory;
    if (g.recorded < kHistory) ++g.recorded;
}

void levelStart() { g.recorded = 0; }

bool addHeld() { return keyDown(kKeyF9); }
bool removeHeld() { return keyDown(kKeyF10); }

int freeDevice(int players) {
    for (int k = 0;; ++k) {
        bool used = false;
        for (int slot = 1; slot < players && !used; ++slot) used = input::padOf(slot) == kFirstDevice + k;
        if (!used) return kFirstDevice + k;
    }
}

int lastTestSlot(int players) {
    for (int slot = players - 1; slot >= 1; --slot)
        if (isTestDevice(input::padOf(slot))) return slot;
    return -1;
}

}  // namespace bmx::testplayers
