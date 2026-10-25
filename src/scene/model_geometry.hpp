#pragma once

#include "formats/model.hpp"
#include "scene/model_meshes.hpp"

namespace toca::scene {
struct ModelMaterialRun {
    std::int16_t texture;
    std::uint32_t first, count;
};

struct ModelGeometry {
    std::shared_ptr<const gpu::WorldMesh> mesh;
    std::vector<ModelMaterialRun> runs;
};

// Expand immutable face corners once, retaining material/normal/UV seams and
// original face order. Animated hierarchy matrices remain separate instances.
inline ModelGeometry prepare_model_geometry(const ModelMesh& source, ModelMeshes& meshes) {
    ModelGeometry result;
    if (source.faces.empty()) {
        return result;
    }
    if (source.faces.size() > UINT32_MAX / 6) {
        throw std::runtime_error("Model exceeds resident triangle capacity");
    }
    std::vector<gpu::WorldVertex> vertices;
    std::vector<gpu::ModelAttribute> attributes;
    vertices.reserve(source.faces.size() * 3);
    attributes.reserve(source.faces.size() * 3);
    for (const auto& face : source.faces) {
        const auto first = std::uint32_t(vertices.size() * 2);
        const auto color = face.texture >= 0
                               ? 0xffffffffu
                               : (std::uint32_t(face.color[0]) << 16) | (std::uint32_t(face.color[1]) << 8) | std::uint32_t(face.color[2]) | (std::uint32_t(face.color[3]) << 24);
        for (const auto index : face.indices) {
            const auto& vertex = source.vertices.at(index);
            vertices.push_back({vertex.position, color, vertex.u, vertex.v});
            attributes.push_back({vertex.normal, face.ghost_hidden ? 1u : 0u, face.ghost_hidden ? source.ghost_positions.at(index) : vertex.position});
        }
        if (!result.runs.empty() && result.runs.back().texture == face.texture) {
            result.runs.back().count += 6;
        } else {
            result.runs.push_back({face.texture, first, 6});
        }
    }
    result.mesh = meshes.get(std::move(vertices), std::move(attributes));
    return result;
}
} // namespace toca::scene
