#include "game/game_version.hpp"

#include <windows.h>

namespace bmx::game {

const KnownBuild* detectBuild() {
    auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    static constexpr const KnownBuild* kBuilds[] = {&kUsRetail};
    for (const KnownBuild* b : kBuilds) {
        if (nt->FileHeader.TimeDateStamp == b->peTimestamp &&
            nt->OptionalHeader.SizeOfImage == b->sizeOfImage &&
            nt->OptionalHeader.AddressOfEntryPoint == b->entryPointRva)
            return b;
    }
    return nullptr;
}

}  // namespace bmx::game
