#pragma once
// The main menu's controller options screen, for gamepads. Its JOYPAD column
// shows and changes the pad layout of BmxSplitscreen.ini ([Input]) instead
// of the joystick buttons of the game's save. Changing a row waits for a
// press on any pad; OK writes the layout to the ini, back and CANCEL drop the
// changes. The KEYBOARD column stays the game's.

#include <string>

namespace bmx::controlleroptions {

// Hooks the screen; `iniPath` is the BmxSplitscreen.ini the layout is saved
// to.
bool install(const std::wstring& iniPath);

}  // namespace bmx::controlleroptions
