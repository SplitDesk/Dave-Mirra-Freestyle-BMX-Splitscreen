#pragma once
// The mod's version: written to the log at startup and to the .asi's version
// resource (src/BmxSplitscreen.rc). The resource compiler reads this file
// too, so the version is plain #defines; the string and the numbers change
// together.

#define BMX_VERSION_MAJOR 1
#define BMX_VERSION_MINOR 7
#define BMX_VERSION_PATCH 0
#define BMX_VERSION_STRING "1.7"

#ifndef RC_INVOKED
namespace bmx {

inline constexpr const char* kVersion = BMX_VERSION_STRING;

}  // namespace bmx
#endif
