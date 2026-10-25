#include "formats/terrain.hpp"

#include "formats/cm3d_bake.hpp"

#include <utility>

namespace toca {
Terrain parse_terrain(std::span<const std::uint8_t> bytes) {
    auto model = parse_cm3d(bytes); // Container, reference, index and section validation.
    detail::FixedCm3dBaker source(bytes, model, "Terrain: truncated original field");
    Terrain result;
    result.textures = std::move(model.textures);
    auto position = std::size_t(source.u32(20));
    for (std::size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        const auto& node = model.nodes[node_index];
        const auto matrix = source.node_matrix(position);
        // Original terrain nodes contain their complete placement matrix.
        // FUN_004962e0 does not multiply it through the scene parent hierarchy.
        if (node_index != 0) {
            result.sector_centers.push_back({matrix[12], matrix[14]});
        }
        for (std::size_t variant = 0; variant < node.variants.size(); ++variant) {
            const auto mesh_index = node.variants[variant];
            if (mesh_index == 0xffff) {
                continue;
            }
            auto mesh = source.bake_mesh(mesh_index, matrix);
            TerrainChunk chunk;
            chunk.node_index = std::uint32_t(node_index);
            chunk.variant_index = std::uint32_t(variant);
            chunk.origin_xz = {matrix[12], matrix[14]};
            // The high bit changes initial per-vertex lighting in 004962e0;
            // preserve it for that draw consumer rather than discarding it.
            chunk.face_flags = source.face_flags(mesh_index);
            chunk.mesh = std::move(mesh);
            result.chunks.push_back(std::move(chunk));
        }
        position += source.u32(position);
    }
    return result;
}
} // namespace toca
