#pragma once
// Button prompt icons. The buttons in the HUD's tips are glyphs of its button
// font, whose texture is replaced with the icons of each player's controller:
// PlayStation, Xbox, Nintendo or default, by the pad's type or by Icons.Type
// in the ini.

#include "common/config.hpp"

namespace bmx::buttonicons {

// Hooks the HUD texture setup, where the button font's slot is found.
bool install(const Config& cfg);

// Puts `player`'s icons in the button font's texture slot. Texts take the
// slot's texture when they are made, so the HUD calls this before it runs a
// player's HUD; a negative player means player 1.
void setActivePlayer(int player);

// Puts the active player's icons in the slot again, for a pad connected or
// changed since; runs once per frame.
void update();

// Puts the game's own texture back in the slot.
void restoreOriginal();

}  // namespace bmx::buttonicons
