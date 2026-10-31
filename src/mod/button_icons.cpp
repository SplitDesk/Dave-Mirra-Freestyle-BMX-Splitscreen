#include "mod/button_icons.hpp"

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "common/log.hpp"
#include "core/limits.hpp"
#include "game/addresses.hpp"
#include "game/engine.hpp"
#include "hook/hook.hpp"
#include "input/gamepads.hpp"
#include "mod/input.hpp"

namespace bmx::buttonicons {
namespace {

using VoidFn = void(__cdecl*)();
using MatCreateFn = void*(__cdecl*)(const void* data, uint32_t size, const char* name, int type);

hook::Hook<VoidFn> g_hudTexturesInit;

Config g_cfg;
void* g_matDefault = nullptr;
void* g_matPS = nullptr;
void* g_matXbox = nullptr;
void* g_matNintendo = nullptr;
void* g_origMaterial = nullptr;
uint16_t g_lastSlot = 0;
bool g_loaded = false;
// The player whose icons are in the slot, and each player's icons as last
// applied (Auto before the first time).
int g_player = 0;
IconType g_typeOf[kMaxPlayers]{};

// The game's folder, with a trailing separator.
std::wstring getGameDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) return p.substr(0, slash + 1);
    return L"";
}

// Reads a whole file; empty when it cannot be read.
std::vector<uint8_t> loadFile(const std::wstring& path) {
    std::vector<uint8_t> buf;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return buf;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz > 0) {
        buf.resize(static_cast<size_t>(sz));
        fseek(f, 0, SEEK_SET);
        fread(buf.data(), 1, buf.size(), f);
    }
    fclose(f);
    return buf;
}

// Creates a material from assets\ButtonIcon\<folder>\button_atlas.png.
void* loadMaterial(const wchar_t* folder, const char* debugName) {
    const std::vector<uint8_t> bytes = loadFile(getGameDir() + L"assets\\ButtonIcon\\" + folder + L"\\button_atlas.png");
    if (bytes.empty()) {
        BMX_LOG_WARN("buttonicons: button atlas missing at assets/ButtonIcon/%ls/button_atlas.png", folder);
        return nullptr;
    }
    const auto MatCreateFromImage = reinterpret_cast<MatCreateFn>(game::fn::MatCreateFromImage);
    void* mat = MatCreateFromImage(bytes.data(), static_cast<uint32_t>(bytes.size()), debugName, 1);
    if (!mat) {
        BMX_LOG_WARN("buttonicons: MatCreateFromImage failed for assets/ButtonIcon/%ls/", folder);
        return nullptr;
    }
    BMX_LOG_INFO("buttonicons: loaded atlas for %s (%u bytes)", debugName, static_cast<unsigned>(bytes.size()));
    return mat;
}

// Loads the four icon materials on first use.
void ensureMaterials() {
    if (g_loaded) return;
    g_loaded = true;
    g_matDefault = loadMaterial(L"default", "default");
    g_matPS = loadMaterial(L"PS", "PS");
    g_matXbox = loadMaterial(L"Xbox", "Xbox");
    g_matNintendo = loadMaterial(L"Nintendo", "Nintendo");
}

// The icons for `player`: the ini's choice, else his pad's type.
IconType resolveType(int player) {
    if (g_cfg.iconType != IconType::Auto) return g_cfg.iconType;
    const int pad = input::padOf(player);
    if (pad < 0) return IconType::Default;
    switch (gamepads::type(pad)) {
        case gamepads::PadType::PlayStation: return IconType::PS;
        case gamepads::PadType::Xbox:        return IconType::Xbox;
        case gamepads::PadType::Nintendo:    return IconType::Nintendo;
        default:                             return IconType::Default;
    }
}

const char* typeName(IconType t) {
    switch (t) {
        case IconType::PS: return "PS";
        case IconType::Xbox: return "Xbox";
        case IconType::Nintendo: return "Nintendo";
        case IconType::Auto: return "auto";
        default: return "default";
    }
}

// The material of `type`, else the default icons, else the game's own.
void* materialFor(IconType type) {
    ensureMaterials();
    void* mat = type == IconType::PS ? g_matPS
              : type == IconType::Xbox ? g_matXbox
              : type == IconType::Nintendo ? g_matNintendo
              : g_matDefault;
    if (mat) return mat;
    return g_matDefault ? g_matDefault : g_origMaterial;
}

// The material pointer of the button font's texture slot, or nullptr before
// the HUD textures exist.
void** slotEntry() {
    const uint16_t slot = *reinterpret_cast<const uint16_t*>(game::addr::kButtonTextureSlot);
    if (slot == 0) return nullptr;
    return reinterpret_cast<void**>(game::addr::kTextureTableMaterial + slot * game::addr::kTextureTableStride);
}

// Puts the icons of `player`'s current pad in the button font's slot.
void apply(int player) {
    void** entry = slotEntry();
    if (!entry) return;
    const IconType type = resolveType(player);
    if (type != g_typeOf[player]) {
        g_typeOf[player] = type;
        BMX_LOG_INFO("buttonicons: player %d has %s icons", player + 1, typeName(type));
    }
    *entry = materialFor(type);
}

// HudTexturesInit: remembers the slot and the game's material, then shows
// player 1's icons.
void __cdecl onHudTexturesInit() {
    g_hudTexturesInit.original();
    void** entry = slotEntry();
    if (!entry) return;
    g_lastSlot = *reinterpret_cast<const uint16_t*>(game::addr::kButtonTextureSlot);
    g_origMaterial = *entry;
    BMX_LOG_INFO("buttonicons: button texture slot %u hooked", g_lastSlot);
    g_player = 0;
    apply(0);
}

}  // namespace

void setActivePlayer(int player) {
    g_player = player >= 0 && player < kMaxPlayers ? player : 0;
    apply(g_player);
}

void update() { apply(g_player); }

void restoreOriginal() {
    if (g_lastSlot == 0 || !g_origMaterial) return;
    *reinterpret_cast<void**>(game::addr::kTextureTableMaterial + g_lastSlot * game::addr::kTextureTableStride) = g_origMaterial;
}

bool install(const Config& cfg) {
    g_cfg = cfg;
    bool ok = true;
    ok &= g_hudTexturesInit.install(game::fn::HudTexturesInit, &onHudTexturesInit, "HudTexturesInit");
    if (ok) BMX_LOG_INFO("buttonicons: ready (type=%s)", typeName(cfg.iconType));
    return ok;
}

}  // namespace bmx::buttonicons
