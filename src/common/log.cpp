#include "common/log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace bmx::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
LogLevel g_level = LogLevel::Info;

const char* tag(LogLevel l) {
    switch (l) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Info:  return "INFO ";
        default:              return "DEBUG";
    }
}

}  // namespace

void open(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
    g_file = _wfopen(path.c_str(), L"w");
}

void setLevel(LogLevel level) { g_level = level; }
bool enabled(LogLevel level) { return static_cast<int>(level) <= static_cast<int>(g_level); }

void close() {
    std::lock_guard lock(g_mutex);
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
}

void write(LogLevel level, const char* fmt, ...) {
    if (static_cast<int>(level) > static_cast<int>(g_level)) return;

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);

    std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fprintf(g_file, "%02d:%02d:%02d.%03d [%s] %s\n",
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, tag(level), msg);
        std::fflush(g_file);
    }
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

}  // namespace bmx::log
