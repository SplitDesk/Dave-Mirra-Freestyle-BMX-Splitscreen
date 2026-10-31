#pragma once
// Settings read from BmxSplitscreen.ini, and the ini helpers they need.

#include <string>
#include <string_view>

#include "core/pad_layout.hpp"

namespace bmx {

enum class LogLevel { Error, Warn, Info, Debug };
enum class SplitMode { Auto, Horizontal, Vertical, Grid };
enum class IconType { Auto, Default, PS, Xbox, Nintendo };

// The mod's settings. The split mode can also be changed in the start menu's
// OPTIONS and the pad layout in the main menu's controller options;
// widescreenFixFov is the FOVFactor of DaveMirraFreestyleBMXWidescreenFix.
struct Config {
    bool enabled = true;
    LogLevel logLevel = LogLevel::Info;
    SplitMode splitMode = SplitMode::Auto;
    IconType iconType = IconType::Auto;
    PadLayout padLayout = PadLayout::defaults();
    float widescreenFixFov = 1.0f;
};

// Builds a Config from ini text. Unknown or invalid values keep their
// defaults, and each is reported as one line in `warnings` when it is given.
Config configFromIni(std::string_view text, std::string* warnings = nullptr);

// Returns the ini spelling of a split mode ("auto", "horizontal", ...).
const char* splitModeName(SplitMode mode);

// Returns `text` with key=value set in [section]. An existing key line is
// rewritten in place; otherwise the line is added after the section's last
// line, or a new section is appended after a blank line. Names match case-insensitively.
std::string setIniValue(std::string_view text, std::string_view section, std::string_view key,
                        std::string_view value);

// Returns `text` without the key line of [section]. A section left without
// keys goes too, with its comments and the blank line before it.
std::string removeIniValue(std::string_view text, std::string_view section, std::string_view key);

// Returns whether `text` has [section].
bool iniHasSection(std::string_view text, std::string_view section);

// Returns the FOVFactor from DaveMirraFreestyleBMXWidescreenFix.ini
// ([Settings]), or 1 when it is missing or invalid. The fix scales the game's
// view window by it; the split views replace that window and apply it too.
float widescreenFixFovFactor(std::string_view fixIniText);

}  // namespace bmx
