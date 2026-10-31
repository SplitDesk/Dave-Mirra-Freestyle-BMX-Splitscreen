#include "mod/scoring.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "common/log.hpp"
#include "core/goal_states.hpp"
#include "core/limits.hpp"
#include "core/play_rules.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "mod/extra_riders.hpp"
#include "mod/hud.hpp"
#include "mod/riders.hpp"

namespace bmx::scoring {
namespace {

using namespace bmx::game;

// Slot 2. The extra riders take turns in it, so "the slot-2 rider" is
// whoever rides there at the time (extras::slotPlayer()).
constexpr int kRider2 = 1;

// RecordCheck's category of the run score.
constexpr uint32_t kRecordRunScore = 1;

struct State {
    bool inPass = false;  // running the slot-2 rider's trick tracking
    PlayRules rules = PlayRules::Versus;
    int lastSubMode = -1;
    bool announced = false;
    // Co-op ProQuest, per player from 2 up: his own goal attempts (in a zone,
    // tip shown and so on), and the objects his goal tip makes glow; the
    // game keeps one list, for the one tip it shows.
    std::vector<RiderGoals> goals;
    std::vector<std::array<uint8_t, addr::kGoalObjectListsSize>> objects;
    int goalLevel = -1;
    bool wasActive = false;
    bool redirectGoals = false;  // the slot-2 rider's goal block lookups go to player 1's
};
State g;

using SlotFn = void(__cdecl*)(int slot);
using BankFn = void(__cdecl*)(int player, int landed);
using RecordFn = void(__cdecl*)(int player, uint32_t category, uint32_t value, int display, uint32_t flags);
using GoalSetFn = void(__cdecl*)(int player, int goal, uint32_t item, int state);
using HudSetFn = void(__cdecl*)(int id, void* data);
using GoalCheckFn = int(__cdecl*)(int player, int a, int b, int c);

hook::Hook<VoidFn> g_runUpdate;
hook::Hook<BankFn> g_bankCombo;
hook::Hook<RecordFn> g_recordCheck;
hook::Hook<GoalSetFn> g_goalSet;
hook::Hook<HudSetFn> g_hudSet;
hook::Hook<IntFn> g_goalHighlights;
hook::Hook<SlotFn> g_goalsRunStart;
hook::Hook<uint8_t*(__cdecl*)(int, uint32_t)> g_goalBlockFor;
hook::Hook<GoalCheckFn> g_goalCheckLand, g_goalCheckObject, g_goalCheckProp;
hook::Hook<IntFn> g_save;

const auto TrickTracker = reinterpret_cast<SlotFn>(fn::TrickTracker);
const auto PositionGoals = reinterpret_cast<SlotFn>(fn::PositionGoals);
const auto TutorialTips = reinterpret_cast<SlotFn>(fn::TutorialTips);
const auto RiderSubState = reinterpret_cast<int(__cdecl*)(void* rider, int level)>(fn::RiderSubState);

uint8_t* career(int character) {
    return reinterpret_cast<uint8_t*>(addr::kCareerArray + character * addr::kCareerStride);
}

// Player 1's goal block for this level, where the co-op goals are, or nullptr
// on a level without goals. A new level resets every player's attempts.
uint8_t* goalBlock() {
    const int level = global<uint8_t>(addr::kLevel);
    if (level >= addr::kGoalLevels) return nullptr;
    const int character = playerConfig(0)[addr::kPlayerCfgCharacter];
    uint8_t* block = reinterpret_cast<uint8_t*>(addr::kGoalBlocks + character * addr::kCareerStride +
                                                level * kGoalBlockSize);
    if (level != g.goalLevel) {
        g.goalLevel = level;
        for (RiderGoals& goals : g.goals) goals.resetFrom(block);
        for (auto& obj : g.objects) obj = {};
    }
    return block;
}

// Makes room for `player`'s goal attempts, starting from `block`.
void ensureGoalPlayer(int player, const uint8_t* block) {
    if (player < 0) return;
    const int oldSize = static_cast<int>(g.goals.size());
    if (player >= oldSize) {
        g.goals.resize(player + 1);
        g.objects.resize(player + 1);
        if (block) {
            for (int i = oldSize; i <= player; ++i) g.goals[i].resetFrom(block);
        }
    }
}

void forgetGoals() {
    g.goals.clear();
    g.objects.clear();
    g.goalLevel = -1;
}

int32_t runScore(int slot) { return field<int32_t>(riderState(slot), addr::rider::kRunScore); }

GameType currentGameType() { return gameTypeFromSubMode(global<uint8_t>(addr::kSessionSubMode)); }

// Swaps a player's goal object lists with the live ones; a second call
// undoes it.
void swapGoalObjects(int player) {
    auto* live = reinterpret_cast<uint8_t*>(addr::kGoalObjectLists);
    auto& own = g.objects[player];
    for (size_t i = 0; i < own.size(); ++i) std::swap(live[i], own[i]);
}

// Returns whether the riders share player 1's goals: co-op ProQuest.
bool coopGoals() { return g.rules == PlayRules::Coop && currentGameType() == GameType::ProQuest; }

// Puts the slot-2 rider on player 1's goals for the scope's lifetime (co-op
// ProQuest): his goal block lookups go to player 1's block, with his own
// attempts and glow lists put in. With `keepChanges` what changed is kept
// (his attempts, and the finished items for everyone); otherwise the block is
// put back as it was.
struct SlotGoalScope {
    uint8_t* goals = nullptr;
    int player = extras::slotPlayer();
    bool keep = true;
    std::array<uint8_t, kGoalBlockSize> before{};

    SlotGoalScope(bool on, bool keepChanges) : keep(keepChanges) {
        if (!on || g.redirectGoals || !(goals = goalBlock())) return;
        ensureGoalPlayer(player, goals);
        std::memcpy(before.data(), goals, kGoalBlockSize);
        g.goals[player].load(goals);
        swapGoalObjects(player);
        g.redirectGoals = true;
    }
    ~SlotGoalScope() {
        if (!goals) return;
        g.redirectGoals = false;
        swapGoalObjects(player);
        if (keep) {
            g.goals[player].store(goals, before.data());
        } else {
            std::memcpy(goals, before.data(), kGoalBlockSize);
        }
    }
    SlotGoalScope(const SlotGoalScope&) = delete;
    SlotGoalScope& operator=(const SlotGoalScope&) = delete;
};

// Reads the game type and its rules, logging a change.
void noteRules() {
    const int sub = global<uint8_t>(addr::kSessionSubMode);
    const GameType type = gameTypeFromSubMode(sub);
    const PlayRules rules = playRules(type);
    if (sub == g.lastSubMode && rules == g.rules && g.announced) return;
    g.lastSubMode = sub;
    g.announced = true;
    g.rules = rules;
    BMX_LOG_INFO("scoring: %s, %s - the other riders' tricks %s", gameTypeName(type),
                 g.rules == PlayRules::Coop ? "co-op" : "versus",
                 g.rules == PlayRules::Coop ? "count toward player 1's career" : "score for player 2 only");
}

// Keeps the slot-2 rider's own career for the guard's lifetime: in versus,
// and in co-op when his character is not player 1's. Only player 1's tricks
// reach the save, so whatever lands in the rider's career (the trick book,
// written while banking a combo) is put back.
class CareerGuard {
public:
    explicit CareerGuard(bool coop)
        : character_(playerConfig(kRider2)[addr::kPlayerCfgCharacter]),
          active_(!coop || character_ != playerConfig(0)[addr::kPlayerCfgCharacter]) {
        if (active_) std::memcpy(saved_.data(), career(character_), saved_.size());
    }
    ~CareerGuard() {
        if (active_) std::memcpy(career(character_), saved_.data(), saved_.size());
    }
    CareerGuard(const CareerGuard&) = delete;
    CareerGuard& operator=(const CareerGuard&) = delete;

private:
    uint8_t character_;
    bool active_;
    std::array<uint8_t, addr::kCareerStride> saved_{};
};

// Runs the slot-2 rider's trick tracking, goals, tips and trick name lines,
// as RunUpdate does for the turn rider, with his HUD in. The tracker banks
// combos for the turn player, so the turn is his meanwhile. In co-op ProQuest
// both riders work on player 1's goals, each with his own attempts; only the
// finished items are shared. Tutorial tips show in session mode 0 only.
void trackRider2(bool coop) {
    SlotGoalScope goals(coop && coopGoals(), true);
    uint8_t& turn = global<uint8_t>(addr::kCurrentTurnPlayer);
    const uint8_t savedTurn = turn;
    turn = kRider2;
    g.inPass = true;
    hud::beginSlot2();
    TrickTracker(kRider2);
    if (coop && currentGameType() == GameType::ProQuest) PositionGoals(kRider2);
    if (hud::slot2Ready()) {
        if (global<uint8_t>(addr::kSessionMode) == 0) TutorialTips(kRider2);
        uint8_t* r = riderState(kRider2);
        uint8_t name = r[addr::rider::kTrickName];
        g_hudSet.original(8, &name);
        uint8_t name2 = RiderSubState(r, 0) == 6 ? r[addr::rider::kTrickName2] : 0xFF;
        g_hudSet.original(7, &name2);
    }
    hud::endSlot2();
    g.inPass = false;
    turn = savedTurn;
}

// The slot-2 rider's pass. In co-op his goal and record writes land in
// player 1's goals through the redirected goal block lookups; his character
// stays his own, as trick names and checks go by character.
void runSlotRider() {
    noteRules();
    const bool coop = g.rules == PlayRules::Coop;
    CareerGuard keepCareer(coop);
    trackRider2(coop);
}

// RunUpdate: runs every slot-2 rider's pass after the turn rider's, then the
// other players' HUDs.
void __cdecl onRunUpdate() {
    g_runUpdate.original();
    const bool active = riders::secondActive();
    if (active && !g.wasActive) forgetGoals();
    g.wasActive = active;
    if (active) {
        runSlotRider();
        extras::forEach(runSlotRider);
    }
    hud::update();
}

// The sum of every rider's run score.
int32_t teamRunScore() {
    int32_t total = 0;
    for (int p = 0; p < 2 + extras::count(); ++p)
        if (const uint8_t* r = extras::riderOf(p)) total += field<int32_t>(const_cast<uint8_t*>(r), addr::rider::kRunScore);
    return total;
}

// BankCombo: logs every banked combo with the rider's new run score.
void __cdecl onBankCombo(int player, int landed) {
    const int slot = g.inPass ? kRider2 : player;
    const int32_t combo = field<int32_t>(riderState(slot), addr::rider::kComboValue);
    g_bankCombo.original(player, landed);
    if (!riders::secondActive()) return;
    const int32_t score = runScore(slot);
    const int rider = slot == kRider2 ? extras::slotPlayer() + 1 : slot + 1;
    if (landed)
        BMX_LOG_INFO("scoring: rider %d landed a combo worth %d, run score %d", rider, combo, score);
    else
        BMX_LOG_INFO("scoring: rider %d bailed, run score %d", rider, score);
}

// RecordCheck: in versus the other riders set no level records and complete
// no goals. In co-op ProQuest the run score counts as the team's: after each
// banked combo the game checks the run score against the level's high score
// and the "score over N points" goals, and gets the team's instead.
void __cdecl onRecordCheck(int player, uint32_t category, uint32_t value, int display, uint32_t flags) {
    if (g.inPass && g.rules == PlayRules::Versus) return;
    if (category == kRecordRunScore && riders::secondActive() && coopGoals()) {
        value = static_cast<uint32_t>(teamRunScore());
        BMX_LOG_DEBUG("scoring: the team's run score %u counts for records and goals", value);
    }
    g_recordCheck.original(player, category, value, display, flags);
}

// GoalSet: in versus the other riders complete no goals.
void __cdecl onGoalSet(int player, int goal, uint32_t item, int state) {
    if (g.inPass && g.rules == PlayRules::Versus) return;
    if (g.inPass && state == 5)
        BMX_LOG_INFO("scoring: rider %d completed a goal for player 1 (goal %d, item %u)", extras::slotPlayer() + 1,
                     goal, item);
    g_goalSet.original(player, goal, item, state);
}

// Returns whether a HudSet call is a co-op ProQuest goal message ("TWO
// CHALLENGES COMPLETE!", "TWO MORE TO GO...", "AMATEUR CHALLENGES
// COMPLETE!"), which PositionGoals sends for the rider who finished the item.
// The goals are shared, so every view shows them; the other messages on that
// line (big air, combos, records) stay with the rider who earned them.
bool sharedGoalMessage(int id, uintptr_t caller) {
    return id == fn::kHudMessage && caller >= fn::PositionGoals && caller < fn::PositionGoalsEnd &&
           riders::secondActive() && coopGoals();
}

// Sends a HudSet call to every player's HUD but the one in now.
void toOtherHuds(int id, void* data) {
    const int sentTo = hud::live();
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (p == sentTo || !hud::ready(p)) continue;
        hud::begin(p);
        g_hudSet.original(id, data);
        hud::end();
    }
}

// HudSet: the slot-2 rider's calls go to his own HUD, and are dropped while
// he has none; the announcer stays on player 1. Goal messages go to every
// HUD, and the run-start reset of rider 2's score and trick name to every
// slot-2 rider's HUD.
void __cdecl onHudSet(int id, void* data) {
    if (sharedGoalMessage(id, BMX_RETURN_ADDRESS())) {
        g_hudSet.original(id, data);
        toOtherHuds(id, data);
        return;
    }
    if (!g.inPass && hud::live() == 0 && hud::slot2Ready() && hud::takeRoute(id)) {
        toOtherHuds(id, data);
        return;
    }
    if (g.inPass && !hud::slot2Ready()) return;
    g_hudSet.original(id, data);
}

// Makes the slot-2 rider's goal highlights (the red glow on the objects of a
// goal whose tip shows) from his own attempts, into his HUD so they show in
// his view. They are 3-D primitives in the HUD's per-frame buffer, made once
// per frame for the turn player; making them also steps the glow's pulse,
// which keeps the game's speed by being put back.
void slotRiderHighlights() {
    if (!hud::slot2Ready()) return;
    SlotGoalScope goals(true, false);
    uint8_t& turn = global<uint8_t>(addr::kCurrentTurnPlayer);
    const uint8_t savedTurn = turn;
    turn = kRider2;
    uint32_t& pulse = global<uint32_t>(addr::kGoalGlowPulse);
    const uint32_t savedPulse = pulse;
    hud::beginSlot2();
    g_goalHighlights.original();
    hud::endSlot2();
    pulse = savedPulse;
    turn = savedTurn;
}

// GoalHighlights: every slot-2 rider's highlights after the turn player's.
int __cdecl onGoalHighlights() {
    const int result = g_goalHighlights.original();
    if (!riders::secondActive() || !coopGoals() || !goalBlock()) return result;
    slotRiderHighlights();
    extras::forEach(slotRiderHighlights);
    return result;
}

// GoalCheckLanding, GoalCheckObject and GoalCheckProp: the goal checks the
// rider and prop code run with the rider's own slot, outside the trick
// tracking (landing on, grinding or hitting a goal object). For the slot-2
// rider they run on player 1's goals with his own attempts in co-op, with his
// own career kept, and their pop-ups go to his HUD.
template <hook::Hook<GoalCheckFn>& H>
int __cdecl onGoalCheck(int player, int a, int b, int c) {
    if (player != kRider2 || !riders::secondActive() || g.inPass || g.redirectGoals)
        return H.original(player, a, b, c);
    noteRules();
    const bool coop = g.rules == PlayRules::Coop;
    CareerGuard keepCareer(coop);
    SlotGoalScope goals(coop && coopGoals(), true);
    hud::beginSlot2();
    const int result = H.original(player, a, b, c);
    hud::endSlot2();
    return result;
}

// GoalBlockFor: while the slot-2 rider works on player 1's goals, his goal
// block lookups go to player 1's block.
uint8_t* __cdecl onGoalBlockFor(int player, uint32_t level) {
    if (g.redirectGoals && player == kRider2) player = 0;
    return g_goalBlockFor.original(player, level);
}

// GoalsRunStart: each rider's run start (0x40C190) resets the level's props
// (gates and the like), then opens those his career has unlocked. The
// slot-2 riders' starts come after player 1's, so the props follow player 1's
// career for them too. The rider's attempts start over from the fresh items.
void __cdecl onGoalsRunStart(int player) {
    if (player != kRider2 || !riders::secondActive()) return g_goalsRunStart.original(player);
    g.redirectGoals = true;
    g_goalsRunStart.original(player);
    g.redirectGoals = false;
    const int p = extras::slotPlayer();
    uint8_t* block = goalBlock();
    ensureGoalPlayer(p, block);
    if (block) g.goals[p].resetFrom(block);
    g.objects[p] = {};
}

// SaveCareer: logged, as co-op progress depends on reaching it.
int __cdecl onSave() {
    const int result = g_save.original();
    if (riders::secondActive()) BMX_LOG_INFO("scoring: career saved (SAVE.DAT, result %d)", result);
    return result;
}

}  // namespace

bool teamScore() { return riders::secondActive() && g.announced && g.rules == PlayRules::Coop; }

bool install() {
    bool ok = true;
    ok &= g_runUpdate.install(fn::RunUpdate, &onRunUpdate, "RunUpdate");
    ok &= g_bankCombo.install(fn::BankCombo, &onBankCombo, "BankCombo");
    ok &= g_recordCheck.install(fn::RecordCheck, &onRecordCheck, "RecordCheck");
    ok &= g_goalSet.install(fn::GoalSet, &onGoalSet, "GoalSet");
    ok &= g_hudSet.install(fn::HudSet, &onHudSet, "HudSet");
    ok &= g_goalHighlights.install(fn::GoalHighlights, &onGoalHighlights, "GoalHighlights");
    ok &= g_goalsRunStart.install(fn::GoalsRunStart, &onGoalsRunStart, "GoalsRunStart");
    ok &= g_goalBlockFor.install(fn::GoalBlockFor, &onGoalBlockFor, "GoalBlockFor");
    ok &= g_goalCheckLand.install(fn::GoalCheckLanding, &onGoalCheck<g_goalCheckLand>, "GoalCheckLanding");
    ok &= g_goalCheckObject.install(fn::GoalCheckObject, &onGoalCheck<g_goalCheckObject>, "GoalCheckObject");
    ok &= g_goalCheckProp.install(fn::GoalCheckProp, &onGoalCheck<g_goalCheckProp>, "GoalCheckProp");
    ok &= g_save.install(fn::SaveCareer, &onSave, "SaveCareer");
    if (ok)
        BMX_LOG_INFO("scoring: ready (ProQuest co-op, Session/Freeride versus)");
    return ok;
}

}  // namespace bmx::scoring
