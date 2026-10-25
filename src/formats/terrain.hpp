#pragma once

#include "formats/model.hpp"

#include <span>

namespace toca {
struct TerrainChunk {
    std::uint32_t node_index = 0, variant_index = 0;
    std::array<std::int32_t, 2> origin_xz{};
    ModelMesh mesh;
    std::vector<std::uint8_t> face_flags;
};

struct Terrain {
    std::vector<TerrainChunk> chunks;
    std::vector<ModelTexture> textures;
    // 4962e0: every non-root node, including nodes with no drawable variant.
    std::vector<std::array<std::int32_t, 2>> sector_centers;
};

// FUN_004962e0/00496980: direct node matrices, original fixed transforms,
// and terrain UV clamping. This is not the generic hierarchy draw consumer.
Terrain parse_terrain(std::span<const std::uint8_t> bytes);
} // namespace toca
