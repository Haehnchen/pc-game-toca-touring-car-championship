#include "scene/mega_ground_uv.hpp"

#include <cmath>

namespace toca::scene {
std::array<float, 2> MegaGroundUvMapping::coordinates(const std::array<float, 3>& position) const noexcept {
    // One world basis across every chunk/triangle removes discontinuities at
    // their shared positions and avoids the original per-triangle stretching.
    return {position[0] / tile_world_units, position[2] / tile_world_units};
}

bool MegaGroundUvMapping::supports_triangle(const std::array<std::array<float, 3>, 3>& positions) const noexcept {
    std::array<double, 3> first, second;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        first[axis] = double(positions[1][axis]) - positions[0][axis];
        second[axis] = double(positions[2][axis]) - positions[0][axis];
    }
    const std::array normal{first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]};
    const auto area_squared = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
    // cos(slope)>=.5 limits the projection's largest stretch to2. Source
    // vertical closure/sliver faces retain their UVs and clamp sampler.
    return std::isfinite(area_squared) && area_squared > 1.e-12 && normal[1] * normal[1] * 4 >= area_squared;
}

std::optional<MegaGroundUvMapping> mega_ground_uv_mapping(bool mega, std::uint32_t track_id, std::string_view material_name) noexcept {
    if (!mega) {
        return {};
    }

    struct Material {
        std::uint32_t track;
        std::string_view name;
        float tile;
    };

    // Canonical GROUND.C3D: median sqrt(world triangle area / UV triangle
    // area), after the original .02/.99 clamp. Keep rounded ordinary grain
    // sizes; only unpainted, opaque grass/gravel/sand images are eligible.
    // Each row records its source BMP CRC32 and measured median tile size.
    constexpr std::array materials{
        Material{0, "GRASS", 26.f}, // 5d240fdc:26.06024
        Material{0, "GRAVEL", 16.f}, // 158c84e7:15.95256
        Material{1, "GRASS2", 26.f}, // 175151aa:25.91437
        Material{1, "GRAVEL", 17.f}, // 33e4f2fc:16.84483
        Material{2, "NUGRASS", 26.f}, // 068940dc:25.94579
        Material{2, "DARKGRS", 37.f}, // 9fe85b3b:36.64377
        Material{2, "GRAVEL", 17.5f}, // 8a5ca97f:17.47279
        Material{3, "GRASS2", 26.f}, // 64d79b84:26.04171
        Material{3, "NLSAND", 13.f}, // cb894113:13.05766
        Material{4, "GRASS", 26.f}, // 293f24c1:25.98013
        Material{4, "GRAVEL", 18.f}, // 25f693e1:17.77876
        Material{5, "GRASS", 26.f}, // 5d240fdc:26.06022
        Material{5, "GRAVEL", 16.f}, // 158c84e7:15.95256
        Material{6, "GRASS2", 26.f}, // dae32e54:25.90986
        Material{7, "GRASS2", 26.f}, // 698762a2:25.99683
        Material{7, "gravel", 16.f}, // b0db5854:16.17578
        Material{8, "GRASS3", 26.f}, // 6846b068:25.92564
        Material{8, "GRAVEL", 12.5f}, // 36c27e29:12.32966
    };
    for (const auto& material : materials) {
        if (track_id == material.track && material_name == material.name) {
            return MegaGroundUvMapping{material.tile};
        }
    }
    return {};
}
} // namespace toca::scene
