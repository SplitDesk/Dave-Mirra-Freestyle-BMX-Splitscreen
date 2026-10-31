#pragma once
// Memory the game's PlayStation-style primitive lists can link to. The links
// hold 28 bits of address, so the memory must lie below 0x10000000.

#include <cstddef>
#include <cstdint>

namespace bmx::lowmem {

// Returns `size` zeroed read/write bytes below 0x10000000, or nullptr when
// there is no room that low.
uint8_t* alloc(size_t size);

}  // namespace bmx::lowmem
