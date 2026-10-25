#pragma once

#include <cstdint>
#include <span>

namespace toca {
// IEEE/zlib CRC32, shared by native records and ghost files.
constexpr std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffff;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
} // namespace toca
