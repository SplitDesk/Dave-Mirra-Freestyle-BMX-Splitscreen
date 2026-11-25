#pragma once
// Writes a crash report to the log when the game crashes in the exe or in the
// mod: the address, the memory it accessed, the registers and the return
// addresses on the stack. One report per run. Also logs when the game's
// window gains or loses the focus, and where the game's threads are when the
// window stops responding.

#include <windows.h>

namespace bmx::crashlog {

// Adds the exception handler and starts the window watch; `self` is the
// mod's module.
void install(HMODULE self);

}  // namespace bmx::crashlog
