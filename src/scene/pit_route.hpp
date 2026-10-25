#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace toca::scene {
struct PitRoutePoint {
    // 453f20's PIT EDIT names; each coordinate is signed Q16 world space.
    std::array<std::int32_t, 3> left{}, best{}, over{}, right{};
    // Preserve all four authored words. The PITS runtime consumers do not
    // establish POINTS speed/packed-direction meanings for these fields.
    std::array<std::uint32_t, 4> metadata{};
};

struct PitRoute {
    // 401790 reads the signed 16-bit count and skips this header word.
    std::uint16_t reserved = 0;
    std::vector<PitRoutePoint> points;
};

// KEEP member 2: four-byte header, followed by count complete 64-byte records.
// The decoded route owns its data and retains original integer precision.
PitRoute decode_pit_route(std::span<const std::uint8_t> bytes);
} // namespace toca::scene
