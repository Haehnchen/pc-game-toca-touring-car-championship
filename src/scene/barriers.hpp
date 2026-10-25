#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace toca::scene {
struct BarrierSegment {
    std::array<std::int32_t, 3> first{}, second{}, normal{};
    std::uint32_t source_index = 0;
    std::uint8_t kind = 0;
    // 478d10: +24..44 line/projection coefficients, +50 axis, +54 side.
    std::array<std::int32_t, 9> line{};
    std::uint8_t axis = 0;
    bool orientation = false;
    // 479182 reconstructs circular kind2/3 groups; never retain file pointers.
    std::uint32_t next_index = UINT32_MAX;
};

// 479182/478d10: owning static segments from retained POINTS or LOWPOINT.
// The route prefix and trailing route data stay with their existing consumers.
class BarrierData {
    std::vector<BarrierSegment> segments_;
    std::vector<std::vector<std::uint32_t>> sector_candidates_;
    std::int32_t half_size_ = 0, minimum_x_ = 0, maximum_z_ = 0;
    std::uint32_t width_ = 0, height_ = 0;

public:
    BarrierData(std::span<const std::uint8_t> route_bytes, std::span<const std::array<std::int32_t, 2>> sector_centers);
    std::span<const BarrierSegment> segments() const;
    std::optional<std::uint32_t> sector(const std::array<std::int32_t, 3>& position) const;
    std::span<const std::uint32_t> candidates(const std::array<std::int32_t, 3>& position) const;
    std::size_t sector_count() const;
};
} // namespace toca::scene
