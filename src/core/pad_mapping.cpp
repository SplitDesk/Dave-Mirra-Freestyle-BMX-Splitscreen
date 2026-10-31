#include "core/pad_mapping.hpp"

namespace bmx {

uint32_t keyTableToGameBits(const uint8_t* table, const uint8_t* keys) {
    // The game bit of each input index of the device poll.
    static constexpr struct { uint8_t index; uint32_t bit; } kBits[] = {
        {4, gamebits::kLeft},   {5, gamebits::kRight}, {6, gamebits::kUp},     {7, gamebits::kDown},
        {8, gamebits::kTriangle}, {9, gamebits::kSquare}, {10, gamebits::kCircle}, {11, gamebits::kCross},
        {12, gamebits::kEsc},   {13, gamebits::kEnter},         {14, gamebits::kL1},    {15, gamebits::kR1},
        {16, gamebits::kL2},    {17, gamebits::kR2},   {20, gamebits::kStart}, {21, gamebits::kSelect},
    };
    uint32_t bits = 0;
    for (const uint8_t* k = table; k[0] != 0xFF; k += 2) {
        if (!(keys[k[0]] & 0x80)) continue;
        for (const auto& b : kBits)
            if (b.index == k[1]) bits |= b.bit;
    }
    return bits;
}

}  // namespace bmx
