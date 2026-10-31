// The mod's entry point. An ASI loader loads the mod into NgBMX.exe before
// the game's WinMain runs.

#include <windows.h>

#include <string>

#include "common/config.hpp"
#include "common/ini_file.hpp"
#include "common/log.hpp"
#include "common/version.hpp"
#include "game/addresses.hpp"
#include "game/game_version.hpp"
#include "hook/hook.hpp"
#include "mod/audio.hpp"
#include "mod/button_icons.hpp"
#include "mod/controller_options.hpp"
#include "mod/crash_log.hpp"
#include "mod/frontend.hpp"
#include "mod/gui_buffers.hpp"
#include "mod/hud.hpp"
#include "mod/input.hpp"
#include "mod/pause_options.hpp"
#include "mod/players_score.hpp"
#include "mod/render_state.hpp"
#include "mod/rider_select.hpp"
#include "mod/riders.hpp"
#include "mod/scoring.hpp"
#include "mod/splitscreen.hpp"

namespace {

HMODULE g_module = nullptr;
bmx::Config g_config;

// The path of `fileName` in the mod's folder.
std::wstring siblingPath(const wchar_t* fileName) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L"\\/");
    return path.substr(0, slash + 1) + fileName;
}

// Opens the log, reads the settings, checks the game build and installs the
// hooks. The game is single-threaded at this point.
void startup() {
    bmx::log::open(siblingPath(L"BmxSplitscreen.log"));
    BMX_LOG_INFO("BmxSplitscreen %s loading", bmx::kVersion);

    std::string warnings;
    const std::string ini = bmx::readTextFile(siblingPath(L"BmxSplitscreen.ini"));
    if (ini.empty()) BMX_LOG_WARN("BmxSplitscreen.ini not found, using defaults");
    g_config = bmx::configFromIni(ini, &warnings);
    const std::string fixIni = bmx::readTextFile(siblingPath(L"DaveMirraFreestyleBMXWidescreenFix.ini"));
    if (!fixIni.empty()) g_config.widescreenFixFov = bmx::widescreenFixFovFactor(fixIni);
    bmx::log::setLevel(g_config.logLevel);
    for (size_t pos = 0; pos < warnings.size();) {
        size_t nl = warnings.find('\n', pos);
        BMX_LOG_WARN("config: %s", warnings.substr(pos, nl - pos).c_str());
        pos = nl + 1;
    }

    bmx::crashlog::install(g_module);

    const bmx::game::KnownBuild* build = bmx::game::detectBuild();
    if (!build) {
        BMX_LOG_ERROR("Unsupported NgBMX.exe build - mod disabled. See README for the supported version.");
        return;
    }
    BMX_LOG_INFO("Game build: %s (riders at 0x%08X, %d slots in exe)", build->name,
                 static_cast<unsigned>(bmx::game::addr::kRiderArray), bmx::game::addr::kRiderSlotsInExe);

    if (!g_config.enabled) {
        BMX_LOG_INFO("Enabled=0 in ini, not installing hooks");
        return;
    }

    if (g_config.widescreenFixFov != 1.0f)
        BMX_LOG_INFO("Split views use the widescreen fix's FOVFactor %.2f", g_config.widescreenFixFov);
    bmx::input::setLayout(g_config.padLayout);
    if (!bmx::split::install(g_config) || !bmx::riders::install() || !bmx::input::install() ||
        !bmx::scoring::install() || !bmx::playersscore::install() || !bmx::hud::install() ||
        !bmx::frontend::install() || !bmx::riderselect::install() || !bmx::audio::install() ||
        !bmx::buttonicons::install(g_config) || !bmx::render::install() || !bmx::guibuffers::install() ||
        !bmx::pauseoptions::install(siblingPath(L"BmxSplitscreen.ini")) ||
        !bmx::controlleroptions::install(siblingPath(L"BmxSplitscreen.ini"))) {
        BMX_LOG_ERROR("Hook setup failed, mod disabled");
        return;
    }
    if (!bmx::hook::enableAll()) return;
    BMX_LOG_INFO("Ready");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = module;
            DisableThreadLibraryCalls(module);
            startup();
            break;
        case DLL_PROCESS_DETACH:
            bmx::hook::shutdown();
            bmx::log::close();
            break;
    }
    return TRUE;
}
