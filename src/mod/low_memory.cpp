#include "mod/low_memory.hpp"

#include <windows.h>

namespace bmx::lowmem {

// Takes what the system gives when it lies low enough; otherwise searches
// upwards from 8 MB in 64 KB steps.
uint8_t* alloc(size_t size) {
    constexpr uintptr_t kLimit = 0x10000000;
    void* p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p && reinterpret_cast<uintptr_t>(p) + size <= kLimit) return static_cast<uint8_t*>(p);
    if (p) VirtualFree(p, 0, MEM_RELEASE);
    for (uintptr_t a = 0x00800000; a + size <= kLimit; a += 0x10000)
        if ((p = VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
            return static_cast<uint8_t*>(p);
    return nullptr;
}

}  // namespace bmx::lowmem
