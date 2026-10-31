#pragma once
// The builds of NgBMX.exe the mod supports. The addresses in src/game/ hold
// for one exact build, so the mod checks the running exe before it patches
// anything and stays inactive on an unknown build.

#include <cstdint>

namespace bmx::game {

// A build, identified by fields of its PE headers.
struct KnownBuild {
    const char* name;
    uint32_t peTimestamp;   // IMAGE_FILE_HEADER::TimeDateStamp
    uint32_t sizeOfImage;   // IMAGE_OPTIONAL_HEADER::SizeOfImage
    uint32_t entryPointRva; // IMAGE_OPTIONAL_HEADER::AddressOfEntryPoint
};

// The US PC release (README dated November 7, 2000): NgBMX.exe, 1,597,496
// bytes, SHA-256 97aa9077501f264a1f96fc2ff1ff8d4326be094dd98f2a647c913de9d60d09a2.
inline constexpr KnownBuild kUsRetail = {"US retail (Nov 2000)", 0x3A1C5D01, 0x002B3000, 0x000A51F9};

// Returns the running exe's build, or nullptr when it is not supported.
const KnownBuild* detectBuild();

}  // namespace bmx::game
