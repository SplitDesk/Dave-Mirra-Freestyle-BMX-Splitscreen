#pragma once
// A health line in the log once a minute, to catch leaks and slowdowns over
// a long session: frame times, the process's memory and handles, and the
// game pools the mod adds to (cameras, GUI asset handles, menu primitives).

namespace bmx::perf {

// Counts a rendered frame and writes the line when a minute has passed.
void frame();

}  // namespace bmx::perf
