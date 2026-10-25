#include "formats/static_model.hpp"

#include "formats/cm3d_bake.hpp"

#include <utility>

namespace toca {
StaticModel parse_static_model(std::span<const std::uint8_t> bytes) {
    StaticModel result;
    auto& model = result.model;
    model = parse_cm3d(bytes); // Container, reference, index and section validation.
    detail::FixedCm3dBaker source(bytes, model, "Static model: truncated original field");
    result.baked.textures = model.textures;
    auto position = std::size_t(source.u32(20));
    std::array<std::int32_t, 16> matrix{};
    bool baked = false;
    for (std::size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        const auto& node = model.nodes[node_index];
        if (node.parent == -1) {
            baked = (node.id_flags & 0x20000000u) != 0;
            matrix = source.node_matrix(position);
        }
        if (!baked) {
            position += source.u32(position);
            continue;
        }
        // 00494a40 static branch carries the last root matrix into child meshes.
        // Child transforms are not multiplied through a dynamic hierarchy.
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
            // Static models use 00496c50 without terrain face-flag overrides.
            chunk.face_flags.assign(mesh.faces.size(), 0);
            chunk.mesh = std::move(mesh);
            result.baked.chunks.push_back(std::move(chunk));
        }
        position += source.u32(position);
    }
    return result;
}
} // namespace toca
