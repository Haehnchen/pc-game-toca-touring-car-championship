#include "scene/placement.hpp"

#include "core/source_fixed.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using source_fixed::add;
using source_fixed::bits;
using source_fixed::mul;
using source_fixed::sub;

std::array<std::int32_t, 3> normalized(std::array<std::int32_t, 3> vector) {
    const auto square = add(add(mul(vector[0], vector[0]), mul(vector[1], vector[1])), mul(vector[2], vector[2]));
    const auto length = source_fixed::sqrt(std::uint32_t(square));
    if (length == 0) {
        return {};
    }
    const auto inverse = bits(std::uint32_t(INT64_C(0x100000000) / length));
    for (auto& component : vector) {
        component = mul(component, inverse);
    }
    return vector;
}

std::int16_t route_heading(std::array<std::int32_t, 3> direction) {
    direction = normalized(direction);
    // 443d30 supplies (1,0,1); 4265b0 normalizes this reference again.
    const auto reference = normalized({0x10000, 0, 0x10000});
    auto dot = add(add(mul(reference[0], direction[0]), mul(reference[1], direction[1])), mul(reference[2], direction[2]));
    if (dot > 0x10000) {
        dot = sub(0x20000, dot);
    }
    if (dot < -0x10000) {
        dot = sub(sub(0, dot), 0x20000);
    }
    const bool negative = dot < 0;
    const auto magnitude = std::min(negative ? std::uint32_t(0) - std::uint32_t(dot) : std::uint32_t(dot), UINT32_C(0xffff));
    // 478b20's second table: trunc(asin(i/4095)*65536)*651 >> 16.
    const auto radians = std::int32_t(std::asin(double(magnitude >> 4) / 4095.0) * 65536.0);
    auto angle = std::int32_t((std::int64_t(radians) * 0x28b) >> 16);
    if (negative) {
        angle = -angle;
    }
    angle = 0x400 - angle;
    const auto cross = add(mul(sub(0, reference[2]), direction[0]), mul(reference[0], direction[2]));
    if (cross > 0) {
        angle = -angle;
    }
    return std::bit_cast<std::int16_t>(std::uint16_t(angle - 0x200));
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Placement: truncated POINTS field");
    }
    return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
} // namespace

HorizontalPlacement single_qualifying_start(std::span<const std::uint8_t> route_points) {
    return qualifying_start(route_points, 0, 0);
}

HorizontalPlacement qualifying_start(std::span<const std::uint8_t> route_points, std::uint32_t local, std::uint32_t actor) {
    const auto count = u32(route_points, 0);
    if (local >= 16 || actor >= 16) {
        throw std::runtime_error("Placement: qualifying actor slots require permanent actor IDs0..15");
    }
    const auto offset = 18 + 7 * ((actor + 16 - local) % 16);
    if (count < offset || count > (route_points.size() - 4) / 64) {
        throw std::runtime_error("Placement: POINTS cannot supply the qualifying start records");
    }
    const auto index = count - offset;
    const auto position = std::size_t(4) + std::size_t(index) * 64 + 12;
    std::array<std::int32_t, 3> point{}, direction{};
    for (std::size_t component = 0; component < 3; ++component) {
        point[component] = bits(u32(route_points, position + component * 4));
        direction[component] = sub(bits(u32(route_points, position + 64 + component * 4)), point[component]);
    }
    return {{point[0], point[2]}, route_heading(direction), index};
}

HorizontalPlacement race_grid_start(const std::array<GridSlot, 32>& grid, std::uint32_t rank) {
    if (rank >= 16) {
        throw std::runtime_error("Placement: race rank must select an original geometric SGRID record");
    }
    const auto& slot = grid[rank];
    const auto dx = sub(slot.first_xz[0], slot.second_xz[0]);
    const auto dz = sub(slot.first_xz[1], slot.second_xz[1]);
    const auto side = normalized({sub(0, dz), 0, dx});
    const auto x = add(add(dx / 2, slot.second_xz[0]), mul(side[0], 0x28000));
    const auto z = add(add(dz / 2, slot.second_xz[1]), mul(side[2], 0x28000));
    return {{x, z}, slot.heading, rank};
}

HorizontalPlacement single_race_start(const std::array<GridSlot, 32>& grid) {
    return race_grid_start(grid, 15);
}
} // namespace toca::scene
