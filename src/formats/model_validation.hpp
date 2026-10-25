#pragma once

#include "formats/model.hpp"

#include <algorithm>
#include <span>

namespace toca {
enum class ModelTextureSlots {
    exact,
    preserve_prefix
};

// Replacements retain source slot identities; scenery may append materials.
// Geometry, transforms and decoded images have separate consumer policies.
inline bool model_structure_matches(const Model& source, const Model& replacement, ModelTextureSlots textures = ModelTextureSlots::exact) {
    if (source.textures.size() > replacement.textures.size() || (textures == ModelTextureSlots::exact && source.textures.size() != replacement.textures.size())) {
        return false;
    }
    return std::ranges::equal(source.nodes, replacement.nodes,
               [](const ModelNode& a, const ModelNode& b) {
                   return a.parent == b.parent && a.id_flags == b.id_flags && a.variants == b.variants;
               })
           && std::ranges::equal(source.meshes, replacement.meshes, {}, &ModelMesh::name, &ModelMesh::name)
           && std::ranges::equal(source.textures, std::span(replacement.textures).first(source.textures.size()), {}, &ModelTexture::name, &ModelTexture::name);
}

inline bool model_node_transforms_match(const Model& source, const Model& replacement) {
    return std::ranges::equal(source.nodes, replacement.nodes, {}, &ModelNode::transform, &ModelNode::transform);
}
} // namespace toca
