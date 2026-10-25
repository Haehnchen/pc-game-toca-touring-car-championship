#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace toca {
// Complete an already identified 255-entry original BMP from its matching
// 256-entry BGR0 bank. Both decoding and CRC32 lookup need these exact bytes.
inline std::vector<std::uint8_t> restore_bmp_palette(std::span<const std::uint8_t> bytes, std::span<const std::uint8_t> complete_palette, const char* invalid_message) {
    const auto u32 = [&](std::size_t offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    if (complete_palette.size() != 1024 || bytes.size() < 1074 || u32(14) != 40 || u32(30) != 0 || u32(10) != 1074
        || !std::equal(bytes.begin() + 54, bytes.begin() + 1074, complete_palette.begin())) {
        throw std::runtime_error(invalid_message);
    }
    // 00483261 accepts indices above biClrUsed. Restore the supplied bank;
    // never derive missing colors from bitmap data or change original pixels.
    std::vector<std::uint8_t> restored(bytes.begin(), bytes.begin() + 54);
    restored.insert(restored.end(), complete_palette.begin(), complete_palette.end());
    restored.insert(restored.end(), bytes.begin() + 1074, bytes.end());
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            restored[offset + shift / 8] = std::uint8_t(value >> shift);
        }
    };
    put32(2, std::uint32_t(restored.size()));
    put32(10, 1078);
    put32(46, 256);
    return restored;
}
} // namespace toca
