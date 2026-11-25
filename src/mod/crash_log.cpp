#include "mod/crash_log.hpp"

#include <cstdint>
#include <cstdio>

#include <tlhelp32.h>

#include "common/log.hpp"
#include "game/addresses.hpp"
#include "game/engine.hpp"

namespace bmx::crashlog {
namespace {

// The mod's code range.
uintptr_t g_selfBegin = 0, g_selfEnd = 0;
volatile LONG g_reported = 0;       // a crash in the exe or the mod was reported
volatile LONG g_reportedOther = 0;  // an exception in another module was reported

// The hang watch: how often the game's window is checked, and how many times
// its thread is sampled, how far apart, once Windows reports the window as
// not responding; then every other thread is sampled once.
constexpr DWORD kWatchMs = 500;
constexpr int kHangSamples = 4;
constexpr DWORD kSampleMs = 250;

// The words of a stack that are searched for return addresses.
constexpr int kStackWords = 256;

bool inExe(uintptr_t a) { return a >= game::addr::kCodeBegin && a < game::addr::kCodeEnd; }
bool inSelf(uintptr_t a) { return a >= g_selfBegin && a < g_selfEnd; }

// True for an address in executable memory of a loaded module.
bool inModuleCode(uintptr_t a) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(reinterpret_cast<const void*>(a), &m, sizeof m)) return false;
    return m.State == MEM_COMMIT && m.Type == MEM_IMAGE &&
           (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

// The name of the export of `module` nearest below `rva`, and the distance
// to it; null when the module exports nothing below it.
const char* nearestExport(HMODULE module, uint32_t rva, uint32_t& distance) {
    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return nullptr;
    const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto* functions = reinterpret_cast<const uint32_t*>(base + exp->AddressOfFunctions);
    const auto* names = reinterpret_cast<const uint32_t*>(base + exp->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const uint16_t*>(base + exp->AddressOfNameOrdinals);
    const char* best = nullptr;
    uint32_t bestRva = 0;
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const uint32_t f = functions[ordinals[i]];
        if (f <= rva && f >= bestRva && (f < dir.VirtualAddress || f >= dir.VirtualAddress + dir.Size)) {
            best = reinterpret_cast<const char*>(base + names[i]);
            bestRva = f;
        }
    }
    distance = rva - bestRva;
    return best;
}

// Formats a code address: relative to the mod in the mod, plain in the exe,
// relative to its module elsewhere, with the nearest export below it.
void describe(uintptr_t a, char* out, size_t n) {
    if (inSelf(a)) {
        std::snprintf(out, n, "BmxSplitscreen.asi+0x%X", static_cast<unsigned>(a - g_selfBegin));
        return;
    }
    HMODULE module = nullptr;
    char path[MAX_PATH] = "";
    if (!inExe(a) &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(a), &module) &&
        GetModuleFileNameA(module, path, sizeof path)) {
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        const auto rva = static_cast<uint32_t>(a - reinterpret_cast<uintptr_t>(module));
        uint32_t distance = 0;
        if (const char* symbol = nearestExport(module, rva, distance))
            std::snprintf(out, n, "%s+0x%X(%s+0x%X)", name, rva, symbol, distance);
        else
            std::snprintf(out, n, "%s+0x%X", name, rva);
        return;
    }
    std::snprintf(out, n, "0x%08X", static_cast<unsigned>(a));
}

// The words readable from `p` within its memory region, up to kStackWords.
int readableWords(const uintptr_t* p) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    const uintptr_t end = reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
    const auto words = static_cast<int>((end - reinterpret_cast<uintptr_t>(p)) / sizeof *p);
    return words < kStackWords ? words : kStackWords;
}

// Formats the return addresses on a stack: the values that point into the
// code of a module, up to 16 of the first kStackWords.
void describeStack(const uintptr_t* sp, char* out, size_t n) {
    int len = 0, found = 0;
    out[0] = '\0';
    const int words = readableWords(sp);
    for (int i = 0; i < words && found < 16; ++i) {
        const uintptr_t v = sp[i];
        if (!inExe(v) && !inSelf(v) && !inModuleCode(v)) continue;
        char d[96];
        describe(v, d, sizeof d);
        len += std::snprintf(out + len, n - len, " %s", d);
        ++found;
        if (len > static_cast<int>(n) - 96) break;
    }
}

// Logs the first fatal exception raised in the exe or the mod, and the first
// raised elsewhere (another module may handle its own), and lets the game's
// own handling go on.
LONG CALLBACK handler(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
                       code == EXCEPTION_STACK_OVERFLOW;
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    if (!fatal) return EXCEPTION_CONTINUE_SEARCH;
    const bool ours = inExe(at) || inSelf(at);
    if (InterlockedExchange(ours ? &g_reported : &g_reportedOther, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
    const char* what = ours ? "CRASH" : "exception (it may be handled)";

    char where[96];
    describe(at, where, sizeof where);
    const CONTEXT* c = e->ContextRecord;
    if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        BMX_LOG_ERROR("%s at %s: %s address 0x%08X", what, where,
                      e->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                      static_cast<unsigned>(e->ExceptionRecord->ExceptionInformation[1]));
    } else {
        BMX_LOG_ERROR("%s at %s: exception 0x%08X", what, where, static_cast<unsigned>(code));
    }
#if defined(_M_IX86) || defined(__i386__)
    BMX_LOG_ERROR("  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
                  c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    char line[768];
    describeStack(reinterpret_cast<const uintptr_t*>(c->Esp), line, sizeof line);
    BMX_LOG_ERROR("  stack:%s", line);
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

// Copies the top of a stack into `out`, zero past its readable end. Runs
// while the stack's thread is suspended, so it takes no lock.
void copyStack(uintptr_t esp, uintptr_t (&out)[kStackWords]) {
    const auto* sp = reinterpret_cast<const uintptr_t*>(esp);
    const int words = readableWords(sp);
    for (int i = 0; i < kStackWords; ++i) out[i] = i < words ? sp[i] : 0;
}

// Logs where a thread is: its instruction and the return addresses on its
// stack, headed by `title`. The thread is suspended only for the copy; the
// addresses are described after it runs again, as that takes the loader
// lock, which it may hold.
void sampleThread(DWORD threadId, const char* title) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (!thread) {
        BMX_LOG_ERROR("  %s: could not be opened", title);
        return;
    }
    static uintptr_t stack[kStackWords];
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL;
    bool ok = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        ok = GetThreadContext(thread, &c) != FALSE;
        if (ok) copyStack(c.Esp, stack);
        ResumeThread(thread);
    }
    CloseHandle(thread);
    if (!ok) {
        BMX_LOG_ERROR("  %s: could not be read", title);
        return;
    }
    char at[96], line[768];
    describe(c.Eip, at, sizeof at);
    describeStack(stack, line, sizeof line);
    BMX_LOG_ERROR("  %s at %s, stack:%s", title, at, line);
}

// The address a thread started at, or 0.
uintptr_t threadStart(DWORD threadId) {
    using QueryFn = LONG(WINAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread")));
    constexpr int kThreadQuerySetWin32StartAddress = 9;
    uintptr_t start = 0;
    if (HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, threadId)) {
        if (query) query(thread, kThreadQuerySetWin32StartAddress, &start, sizeof start, nullptr);
        CloseHandle(thread);
    }
    return start;
}

// Logs where the window's thread is, kHangSamples times, then where each
// other thread of the game is, but the watch's own.
void reportHang(HWND window) {
    const DWORD mainThread = GetWindowThreadProcessId(window, nullptr);
    BMX_LOG_ERROR("HANG: the game's window is not responding; its thread %lu, %d times %lu ms apart:", mainThread,
                  kHangSamples, kSampleMs);
    for (int i = 0; i < kHangSamples; ++i) {
        if (i) Sleep(kSampleMs);
        sampleThread(mainThread, "main");
    }
    BMX_LOG_ERROR("  the other threads:");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 t{};
    t.dwSize = sizeof t;
    for (BOOL more = Thread32First(snapshot, &t); more; more = Thread32Next(snapshot, &t)) {
        if (t.th32OwnerProcessID != GetCurrentProcessId() || t.th32ThreadID == mainThread ||
            t.th32ThreadID == GetCurrentThreadId())
            continue;
        char start[96], title[128];
        describe(threadStart(t.th32ThreadID), start, sizeof start);
        std::snprintf(title, sizeof title, "thread %lu (from %s)", t.th32ThreadID, start);
        sampleThread(t.th32ThreadID, title);
    }
    CloseHandle(snapshot);
}

// Watches the game's window: logs when it gains or loses the focus, and
// reports a hang when Windows marks it as not responding.
DWORD WINAPI watchWindow(void*) {
    bool focused = false, hung = false;
    for (;;) {
        Sleep(kWatchMs);
        const HWND window = game::global<HWND>(game::addr::kMainWindow);
        if (!window) continue;
        const bool nowFocused = GetForegroundWindow() == window;
        if (nowFocused != focused) BMX_LOG_INFO("window: %s the focus", nowFocused ? "has" : "lost");
        focused = nowFocused;
        const bool nowHung = IsHungAppWindow(window) != FALSE;
        if (nowHung && !hung) reportHang(window);
        if (!nowHung && hung) BMX_LOG_ERROR("the game's window responds again");
        hung = nowHung;
    }
}

}  // namespace

void install(HMODULE self) {
    const auto base = reinterpret_cast<uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    g_selfBegin = base;
    g_selfEnd = base + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, &handler);
    if (HANDLE watcher = CreateThread(nullptr, 0, &watchWindow, nullptr, 0, nullptr)) CloseHandle(watcher);
}

}  // namespace bmx::crashlog
