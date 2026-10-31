#pragma once
// Sound for every player. The game hears the world from one listener, its
// camera; while split, each sound is heard from the view whose camera is
// nearest to it, and every extra rider gets bike sounds of his own.

namespace bmx::audio {

// Hooks the listener transform and the rider sound event.
bool install();

}  // namespace bmx::audio
