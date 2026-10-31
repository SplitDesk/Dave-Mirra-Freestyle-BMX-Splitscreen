#pragma once
// Reading and updating BmxSplitscreen.ini on disk.

#include <string>
#include <string_view>

namespace bmx {

// Reads a whole file; empty when it cannot be read.
std::string readTextFile(const std::wstring& path);

// Writes a whole file; returns false when it cannot be written.
bool writeTextFile(const std::wstring& path, std::string_view text);

// Sets key=value in [section] of the ini file at `path`, keeping the rest of
// the file as it is (setIniValue). Returns false when it cannot be written.
bool saveIniValue(const std::wstring& path, std::string_view section, std::string_view key,
                  std::string_view value);

}  // namespace bmx
