#include "scene/pit_route.hpp"

#include <bit>
#include <stdexcept>

namespace toca::scene {
namespace {
std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("PITS: truncated original field");
    }
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return u16(bytes, offset) | (std::uint32_t(u16(bytes, offset + 2)) << 16);
}
} // namespace

PitRoute decode_pit_route(std::span<const std::uint8_t> bytes) {
    // 401790 stores only the first 16 bits; runtime consumers sign-extend the
    // short count. Validate before allocation and preserve the ignored word.
    const auto count = std::bit_cast<std::int16_t>(u16(bytes, 0));
    const auto reserved = u16(bytes, 2);
    if (count <= 0 || bytes.size() != 4 + std::size_t(count) * 64) {
        throw std::runtime_error("PITS: expected a positive signed 16-bit count and complete 64-byte records");
    }
    PitRoute result;
    result.reserved = reserved;
    result.points.resize(std::size_t(count));
    for (std::size_t index = 0; index < result.points.size(); ++index) {
        const auto offset = 4 + index * 64;
        auto& point = result.points[index];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            point.left[axis] = std::bit_cast<std::int32_t>(u32(bytes, offset + axis * 4));
            point.best[axis] = std::bit_cast<std::int32_t>(u32(bytes, offset + 12 + axis * 4));
            point.over[axis] = std::bit_cast<std::int32_t>(u32(bytes, offset + 24 + axis * 4));
            point.right[axis] = std::bit_cast<std::int32_t>(u32(bytes, offset + 36 + axis * 4));
        }
        for (std::size_t field = 0; field < point.metadata.size(); ++field) {
            point.metadata[field] = u32(bytes, offset + 48 + field * 4);
        }
    }
    return result;
}
} // namespace toca::scene
