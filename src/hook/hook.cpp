#include "hook/hook.hpp"

#include <windows.h>

#include <detours.h>

#include <vector>

#include "common/log.hpp"

namespace bmx::hook {
namespace {

struct Record {
    void** original;
    void* detour;
    const char* name;
    uintptr_t target;
};

std::vector<Record> g_records;
bool g_attached = false;

}  // namespace

bool create(uintptr_t target, void* detour, void** original, const char* name) {
    if (g_attached) {
        BMX_LOG_ERROR("hook %s @ 0x%08X: hooks are already attached", name, static_cast<unsigned>(target));
        return false;
    }
    *original = reinterpret_cast<void*>(target);
    g_records.push_back({original, detour, name, target});
    BMX_LOG_DEBUG("hook %s @ 0x%08X recorded", name, static_cast<unsigned>(target));
    return true;
}

bool enableAll() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (const Record& r : g_records) {
        const LONG error = DetourAttach(r.original, r.detour);
        if (error != NO_ERROR) {
            BMX_LOG_ERROR("hook %s @ 0x%08X failed (error %ld)", r.name, static_cast<unsigned>(r.target), error);
            DetourTransactionAbort();
            return false;
        }
    }
    const LONG error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        BMX_LOG_ERROR("attaching hooks failed (error %ld)", error);
        return false;
    }
    g_attached = true;
    BMX_LOG_INFO("%u hooks attached", static_cast<unsigned>(g_records.size()));
    return true;
}

void shutdown() {
    if (!g_attached) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (const Record& r : g_records) DetourDetach(r.original, r.detour);
    DetourTransactionCommit();
    g_attached = false;
}

}  // namespace bmx::hook
