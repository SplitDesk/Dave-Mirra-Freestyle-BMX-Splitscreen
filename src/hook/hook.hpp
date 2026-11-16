#pragma once
// Function hooks built on Microsoft Detours.
//
// A Hook redirects one game function to a replacement in the mod. Hooks are
// recorded by install() and attached together by enableAll(); until then,
// original() calls the game function itself.

#include <cstdint>
#include <span>

// The return address of the current function. Inside a replacement it is the
// address in the game code that called the hooked function.
#if defined(_MSC_VER)
#include <intrin.h>
#define BMX_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())
#else
#define BMX_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(__builtin_return_address(0))
#endif

namespace bmx::hook {

// Records a hook from `target` to `detour`. `original` holds the target until
// the hooks are attached, and the trampoline to the original code after.
bool create(uintptr_t target, void* detour, void** original, const char* name);

// Attaches every recorded hook in one transaction.
bool enableAll();

// Detaches every attached hook.
void shutdown();

// A 32-bit value in the game's code: its address, the game's value and the
// mod's.
struct Patch32 {
    uintptr_t at;
    uint32_t game, ours;
};

// Returns whether every patch's address holds the game's value; logs the
// first that does not.
bool matches(std::span<const Patch32> patches, const char* what);

// Writes the mod's values; returns false when the code cannot be written.
bool apply(std::span<const Patch32> patches);

// One hooked game function of type Fn.
template <typename Fn>
class Hook {
public:
    bool install(uintptr_t target, Fn detour, const char* name) {
        name_ = name;
        return create(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original_), name);
    }
    template <typename... Args>
    auto original(Args... args) const { return original_(args...); }
    Fn trampoline() const { return original_; }
    const char* name() const { return name_; }

private:
    Fn original_ = nullptr;
    const char* name_ = "";
};

}  // namespace bmx::hook
