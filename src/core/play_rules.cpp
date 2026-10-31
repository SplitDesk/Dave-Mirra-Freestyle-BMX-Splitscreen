#include "core/play_rules.hpp"

namespace bmx {

GameType gameTypeFromSubMode(int subMode) {
    switch (subMode) {
        case 0: return GameType::ProQuest;
        case 1: return GameType::Session;
        case 2: return GameType::Freeride;
        default: return GameType::Other;
    }
}

PlayRules playRules(GameType type) { return type == GameType::ProQuest ? PlayRules::Coop : PlayRules::Versus; }

const char* gameTypeName(GameType type) {
    switch (type) {
        case GameType::ProQuest: return "ProQuest";
        case GameType::Session: return "Session";
        case GameType::Freeride: return "Freeride";
        default: return "other";
    }
}

}  // namespace bmx
