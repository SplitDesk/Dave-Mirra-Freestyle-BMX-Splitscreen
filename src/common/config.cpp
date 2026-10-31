#include "common/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace bmx {
namespace {

// Maps "section.key" (lower case) to its value.
using IniMap = std::unordered_map<std::string, std::string>;

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void warn(std::string* w, const std::string& msg) {
    if (w) { *w += msg; *w += '\n'; }
}

// Reads every key of an ini text. Comment lines start with ';' or '#', and a
// value ends at the first ';'.
IniMap parseIni(std::string_view text) {
    IniMap map;
    std::string section;
    while (!text.empty()) {
        size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            size_t end = line.find(']');
            section = lower(trim(line.substr(1, end == std::string_view::npos ? line.size() - 1 : end - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view value = trim(line.substr(eq + 1));
        if (size_t c = value.find(';'); c != std::string_view::npos) value = trim(value.substr(0, c));
        map[section + "." + lower(trim(line.substr(0, eq)))] = std::string(value);
    }
    return map;
}

// One line of an ini text: its position, the section it is in, and whether it
// is a section header or a key line.
struct IniLine {
    size_t begin, next;
    std::string section;
    bool header = false;
    std::string key;  // lower case; empty for other lines
    bool blank = false;
};

std::vector<IniLine> iniLines(std::string_view text) {
    std::vector<IniLine> lines;
    std::string current;
    for (size_t pos = 0; pos < text.size();) {
        const size_t nl = text.find('\n', pos);
        const size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        const std::string_view line = trim(text.substr(pos, next - pos));
        IniLine l{pos, next, current};
        if (line.empty()) {
            l.blank = true;
        } else if (line.front() == '[') {
            const size_t close = line.find(']');
            current = lower(trim(line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1)));
            l.section = current;
            l.header = true;
        } else if (line.front() != ';' && line.front() != '#') {
            const size_t eq = line.find('=');
            if (eq != std::string_view::npos) l.key = lower(trim(line.substr(0, eq)));
        }
        lines.push_back(l);
        pos = next;
    }
    return lines;
}


}  // namespace

const char* splitModeName(SplitMode mode) {
    switch (mode) {
        case SplitMode::Horizontal: return "horizontal";
        case SplitMode::Vertical: return "vertical";
        case SplitMode::Grid: return "grid";
        default: return "auto";
    }
}

std::string setIniValue(std::string_view text, std::string_view section, std::string_view key,
                        std::string_view value) {
    const std::string wantSection = lower(section), wantKey = lower(key);
    const std::string newLine = std::string(key) + "=" + std::string(value);
    std::string current;
    size_t sectionEnd = std::string_view::npos;
    for (size_t pos = 0; pos < text.size();) {
        const size_t nl = text.find('\n', pos);
        const size_t end = nl == std::string_view::npos ? text.size() : nl;
        const size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        std::string_view line = trim(text.substr(pos, end - pos));
        if (!line.empty() && line.front() == '[') {
            const size_t close = line.find(']');
            current = lower(trim(line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1)));
            if (current == wantSection) sectionEnd = next;
        } else if (current == wantSection && !line.empty()) {
            sectionEnd = next;
            const size_t eq = line.find('=');
            if (line.front() != ';' && line.front() != '#' && eq != std::string_view::npos &&
                lower(trim(line.substr(0, eq))) == wantKey) {
                const bool cr = end > pos && text[end - 1] == '\r';
                return std::string(text.substr(0, pos)) + newLine + (cr ? "\r" : "") + std::string(text.substr(end));
            }
        }
        pos = next;
    }
    const bool crlf = text.find("\r\n") != std::string_view::npos;
    const char* eol = crlf ? "\r\n" : "\n";
    if (sectionEnd != std::string_view::npos)
        return std::string(text.substr(0, sectionEnd)) + newLine + eol + std::string(text.substr(sectionEnd));
    std::string out(text);
    if (!out.empty() && out.back() != '\n') out += eol;
    const std::string blank = std::string(eol) + eol;
    if (!out.empty() && (out.size() < blank.size() || out.compare(out.size() - blank.size(), blank.size(), blank) != 0))
        out += eol;
    return out + "[" + std::string(section) + "]" + eol + newLine + eol;
}

std::string removeIniValue(std::string_view text, std::string_view section, std::string_view key) {
    const std::string wantSection = lower(section), wantKey = lower(key);
    const std::vector<IniLine> lines = iniLines(text);
    std::vector<bool> drop(lines.size(), false);
    bool keysLeft = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].section != wantSection || lines[i].header || lines[i].key.empty()) continue;
        if (lines[i].key == wantKey) drop[i] = true;
        else keysLeft = true;
    }
    if (!keysLeft) {
        // The section's lines go, and one blank line around it: the blank
        // lines that end it stay to separate the next section, so the blank
        // line before it goes.
        size_t first = lines.size(), last = 0;
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i].section == wantSection) {
                first = std::min(first, i);
                last = i;
            }
        if (first < lines.size()) {
            const bool atEnd = last + 1 == lines.size();
            size_t end = last + 1;
            if (!atEnd)
                while (end > first && lines[end - 1].blank) --end;
            for (size_t i = first; i < end; ++i) drop[i] = true;
            const bool keptBlank = !atEnd && end <= last;
            if ((atEnd || keptBlank) && first > 0 && lines[first - 1].blank) drop[first - 1] = true;
        }
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
        if (!drop[i]) out.append(text.substr(lines[i].begin, lines[i].next - lines[i].begin));
    return out;
}

bool iniHasSection(std::string_view text, std::string_view section) {
    const std::string want = lower(section);
    for (const IniLine& l : iniLines(text))
        if (l.header && l.section == want) return true;
    return false;
}

Config configFromIni(std::string_view text, std::string* warnings) {
    Config cfg;
    const IniMap ini = parseIni(text);
    auto get = [&](const char* key) -> const std::string* {
        auto it = ini.find(key);
        return it == ini.end() ? nullptr : &it->second;
    };

    if (auto v = get("general.enabled")) cfg.enabled = (*v != "0" && lower(*v) != "false");

    if (auto v = get("general.loglevel")) {
        const std::string s = lower(*v);
        if (s == "error") cfg.logLevel = LogLevel::Error;
        else if (s == "warn") cfg.logLevel = LogLevel::Warn;
        else if (s == "info") cfg.logLevel = LogLevel::Info;
        else if (s == "debug") cfg.logLevel = LogLevel::Debug;
        else warn(warnings, "LogLevel '" + *v + "' unknown, using info");
    }

    if (auto v = get("split.mode")) {
        const std::string s = lower(*v);
        if (s == "auto") cfg.splitMode = SplitMode::Auto;
        else if (s == "horizontal") cfg.splitMode = SplitMode::Horizontal;
        else if (s == "vertical") cfg.splitMode = SplitMode::Vertical;
        else if (s == "grid") cfg.splitMode = SplitMode::Grid;
        else warn(warnings, "Split.Mode '" + *v + "' unknown, using auto");
    }

    if (auto v = get("icons.type")) {
        const std::string s = lower(*v);
        if (s == "auto") cfg.iconType = IconType::Auto;
        else if (s == "default") cfg.iconType = IconType::Default;
        else if (s == "ps" || s == "playstation") cfg.iconType = IconType::PS;
        else if (s == "xbox") cfg.iconType = IconType::Xbox;
        else if (s == "nintendo" || s == "switch") cfg.iconType = IconType::Nintendo;
        else warn(warnings, "Icons.Type '" + *v + "' unknown, using auto");
    }

    for (int a = 0; a < kPadActions; ++a) {
        const char* key = padActionKey(static_cast<PadAction>(a));
        if (auto v = get(("input." + lower(key)).c_str())) {
            if (!parsePadBinding(*v, cfg.padLayout.bindings[a]))
                warn(warnings, std::string("Input.") + key + " '" + *v + "' unknown, using " +
                                   formatPadBinding(cfg.padLayout.bindings[a]));
        }
    }

    return cfg;
}

float widescreenFixFovFactor(std::string_view fixIniText) {
    const IniMap ini = parseIni(fixIniText);
    auto it = ini.find("settings.fovfactor");
    if (it == ini.end()) return 1.0f;
    char* end = nullptr;
    const float f = std::strtof(it->second.c_str(), &end);
    return (end && *end == '\0' && f >= 0.1f && f <= 10.0f) ? f : 1.0f;
}

}  // namespace bmx
