#include "mod/crash_log.hpp"

#include <cstdint>
#include <cstdio>

#include "common/log.hpp"
#include "game/addresses.hpp"

namespace bmx::crashlog {
namespace {

// The mod's code range.
uintptr_t g_selfBegin = 0, g_selfEnd = 0;
volatile LONG g_reported = 0;

bool inExe(uintptr_t a) { return a >= game::addr::kCodeBegin && a < game::addr::kCodeEnd; }
bool inSelf(uintptr_t a) { return a >= g_selfBegin && a < g_selfEnd; }

// Formats a code address, relative to the mod when it is in the mod.
void describe(uintptr_t a, char* out, size_t n) {
    if (inSelf(a)) std::snprintf(out, n, "BmxSplitscreen.asi+0x%X", static_cast<unsigned>(a - g_selfBegin));
    else std::snprintf(out, n, "0x%08X", static_cast<unsigned>(a));
}

// Logs the first fatal exception raised in the exe or the mod and lets the
// game's own handling go on.
LONG CALLBACK handler(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
                       code == EXCEPTION_STACK_OVERFLOW;
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    if (!fatal || !(inExe(at) || inSelf(at))) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&g_reported, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

    char where[64];
    describe(at, where, sizeof where);
    const CONTEXT* c = e->ContextRecord;
    if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        BMX_LOG_ERROR("CRASH at %s: %s address 0x%08X", where,
                      e->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                      static_cast<unsigned>(e->ExceptionRecord->ExceptionInformation[1]));
    } else {
        BMX_LOG_ERROR("CRASH at %s: exception 0x%08X", where, static_cast<unsigned>(code));
    }
#if defined(_M_IX86) || defined(__i386__)
    BMX_LOG_ERROR("  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
                  c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // The return addresses on the stack: values that point into the code.
    char line[512] = "";
    int len = 0, found = 0;
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(c->Esp);
    for (int i = 0; i < 256 && found < 16; ++i) {
        uintptr_t v = 0;
        if (IsBadReadPtr(sp + i, sizeof v)) break;
        v = sp[i];
        if (!inExe(v) && !inSelf(v)) continue;
        char d[64];
        describe(v, d, sizeof d);
        len += std::snprintf(line + len, sizeof line - len, " %s", d);
        ++found;
        if (len > 400) break;
    }
    BMX_LOG_ERROR("  stack:%s", line);
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install(HMODULE self) {
    const auto base = reinterpret_cast<uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    g_selfBegin = base;
    g_selfEnd = base + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, &handler);
}

}  // namespace bmx::crashlog
