#include "common/ini_file.hpp"

#include <cstdio>

#include "common/config.hpp"

namespace bmx {

std::string readTextFile(const std::wstring& path) {
    std::string out;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

bool writeTextFile(const std::wstring& path, std::string_view text) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool saveIniValue(const std::wstring& path, std::string_view section, std::string_view key,
                  std::string_view value) {
    return writeTextFile(path, setIniValue(readTextFile(path), section, key, value));
}

}  // namespace bmx
