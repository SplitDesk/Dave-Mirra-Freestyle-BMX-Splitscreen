#pragma once
// The mod's log file, BmxSplitscreen.log next to the .asi. Lines are also
// sent to the debugger output.

#include <string>

#include "common/config.hpp"

namespace bmx::log {

void open(const std::wstring& path);
void setLevel(LogLevel level);
void close();

// Returns whether a line at `level` would be written.
bool enabled(LogLevel level);

// Writes one printf-style line with a time stamp and the level's tag.
void write(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace bmx::log

#define BMX_LOG_ERROR(...) ::bmx::log::write(::bmx::LogLevel::Error, __VA_ARGS__)
#define BMX_LOG_WARN(...)  ::bmx::log::write(::bmx::LogLevel::Warn,  __VA_ARGS__)
#define BMX_LOG_INFO(...)  ::bmx::log::write(::bmx::LogLevel::Info,  __VA_ARGS__)
#define BMX_LOG_DEBUG(...) ::bmx::log::write(::bmx::LogLevel::Debug, __VA_ARGS__)
