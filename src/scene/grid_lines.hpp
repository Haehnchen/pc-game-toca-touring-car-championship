#pragma once

#include "formats/hpc.hpp"
#include "scene/resources.hpp"
#include "scene/world_lines.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <span>

namespace toca::scene {
// 44ab90 prepares sixteen SGRID heights with a fresh cached face0 per
// endpoint. POINTS[0]'s left/right endpoints retain their authored heights.
inline std::vector<QueuedWorldLine> prepare_grid_lines(const SceneResources& resources) {
    if (resources.setup.selection.track_id == 9 || resources.collision_hpc.empty() || resources.route_points.empty()) {
        return {};
    }
    const HpcSurface surface(resources.collision_hpc);
    std::vector<QueuedWorldLine> result;
    result.reserve(17);
    for (std::size_t index = 0; index < 16; ++index) {
        const auto& slot = resources.grid[index];
        const auto endpoint = [&](const auto& xz) {
            const auto sample = surface.prop_sample_retaining_cached(xz[0], xz[1], 0);
            return std::array<std::int32_t, 3>{xz[0], sample ? sample->height : 0, xz[1]};
        };
        result.push_back({endpoint(slot.first_xz), endpoint(slot.second_xz)});
    }
    const auto& bytes = resources.route_points;
    const auto word = [&](std::size_t offset) {
        if (offset > bytes.size() || bytes.size() - offset < 4) {
            throw std::runtime_error("Grid lines require complete POINTS[0]");
        }
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    if (word(0) == 0 || bytes.size() < 68) {
        throw std::runtime_error("Grid lines require complete POINTS[0]");
    }
    QueuedWorldLine start;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        start.from[axis] = source_fixed::bits(word(4 + axis * 4));
        start.to[axis] = source_fixed::bits(word(40 + axis * 4));
    }
    result.push_back(start);
    return result;
}

// 44c5e0/44c940: strict10..90 horizontal distances inside the90-unit
// component box; outside that box the far color remains visible instead.
inline std::vector<QueuedWorldLine> queued_grid_lines(std::span<const QueuedWorldLine> geometry, const SceneSetup& setup, const std::array<std::int32_t, 3>& camera) {
    using namespace simulation::fixed;
    if (setup.selection.track_id == 9) {
        return {};
    }
    constexpr std::array<std::array<int, 3>, 10> near_colors{
        {{170, 170, 170}, {174, 174, 174}, {174, 174, 174}, {150, 150, 150}, {200, 200, 200}, {170, 170, 170}, {150, 150, 150}, {150, 150, 150}, {162, 161, 96}, {255, 255, 255}}};
    constexpr std::array<std::array<int, 3>, 10> far_colors{
        {{24, 24, 24}, {41, 41, 41}, {49, 49, 49}, {16, 16, 16}, {104, 104, 86}, {24, 24, 24}, {19, 17, 14}, {16, 16, 16}, {49, 49, 49}, {255, 255, 255}}};
    const auto gain = setup.weather == 5 ? 0xb333 : (setup.weather == 3 || setup.weather == 4) ? 0xcccc : one;
    std::array<std::int32_t, 3> near{}, far{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        near[channel] = mul(near_colors.at(setup.selection.track_id)[channel] * one, gain);
        far[channel] = far_colors.at(setup.selection.track_id)[channel] * one;
    }
    std::vector<QueuedWorldLine> result;
    result.reserve(geometry.size());
    for (auto line : geometry) {
        const auto first = sub(line.from, camera), second = sub(line.to, camera);
        const auto outside = [](const auto& point) {
            return std::ranges::any_of(point, [](auto value) {
                return simulation::fixed::abs(value) > 90 * one;
            });
        };
        std::int32_t fraction = -one;
        if (!outside(first) && !outside(second)) {
            const auto distance = [](const auto& point) {
                return sqrt(std::uint32_t(add(mul(point[0], point[0]), mul(point[2], point[2]))));
            };
            const auto from_distance = distance(first), to_distance = distance(second);
            if (from_distance <= 10 * one || from_distance >= 90 * one || to_distance <= 10 * one || to_distance >= 90 * one) {
                continue;
            }
            fraction = from_distance <= 60 * one ? one : std::min(mul(sub(from_distance, 60 * one), 0x872), one);
        }
        line.color = 0xff000000;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto value = fraction == -one ? far[channel] : fraction == one ? near[channel] : add(near[channel], mul(sub(far[channel], near[channel]), fraction));
            line.color |= std::uint32_t(std::uint8_t(value / one)) << ((2 - channel) * 8);
        }
        result.push_back(line);
    }
    return result;
}
} // namespace toca::scene
