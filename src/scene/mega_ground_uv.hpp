#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace toca::scene {
struct MegaGroundUvMapping {
    float tile_world_units;
    bool repeat = true;

    // Keep these coordinates unwrapped: fractionalizing individual corners
    // would stretch triangles across tile boundaries instead of repeating them.
    std::array<float, 2> coordinates(const std::array<float, 3>& position) const noexcept;

    // Near-vertical closure faces need their authored UVs. World XZ mapping
    // applies only to nondegenerate ground within60 degrees of horizontal.
    bool supports_triangle(const std::array<std::array<float, 3>, 3>& positions) const noexcept;
};

// Rendering only. The canonical Terrain, collision and authored markings keep
// their original UVs; callers apply this mapping to their copied draw corners.
std::optional<MegaGroundUvMapping> mega_ground_uv_mapping(bool mega, std::uint32_t track_id, std::string_view material_name) noexcept;
} // namespace toca::scene
