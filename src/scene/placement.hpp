#pragma once

#include "scene/resources.hpp"

namespace toca::scene {
struct HorizontalPlacement {
    std::array<std::int32_t, 2> xz{};
    std::int16_t heading = 0;
    // POINTS record for qualifying; SGRID record for race.
    std::uint32_t source_index = 0;
};

// Local single-player qualifying slot after 443d30's actor-slot permutation.
// Accepts POINTS or LOWPOINT; only the route record prefix is consumed.
HorizontalPlacement single_qualifying_start(std::span<const std::uint8_t> route_points);
//443d30 rotates all sixteen actor slots around the permanent local actor,
//spacing their starting POINTS records by seven before qualifying removal.
HorizontalPlacement qualifying_start(std::span<const std::uint8_t> route_points, std::uint32_t local_actor, std::uint32_t actor);
// 401190/401000: midpoint and 2.5-unit sideways offset, original SGRID heading.
HorizontalPlacement race_grid_start(const std::array<GridSlot, 32>& grid, std::uint32_t rank);
// 460170: fresh single race with qualifying disabled puts the player last.
HorizontalPlacement single_race_start(const std::array<GridSlot, 32>& grid);
} // namespace toca::scene
