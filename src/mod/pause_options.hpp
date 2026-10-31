#pragma once
// The OPTIONS screen of the pause menu (not the main menu's). The game keeps
// "camera type" and "tutorial hints" per player and the screen changes player
// 1's; with more players they are set for everyone. A "split screen" row
// picks Split.Mode, saved to BmxSplitscreen.ini.

#include <string>

namespace bmx::pauseoptions {

// Hooks the OPTIONS screen; `iniPath` is the BmxSplitscreen.ini the split row
// saves to.
bool install(const std::wstring& iniPath);

// Gives players 2 and up player 1's camera type and tutorial hints; runs at
// level start, once every rider exists, and when OPTIONS changes them.
void shareWithPlayers();

}  // namespace bmx::pauseoptions
