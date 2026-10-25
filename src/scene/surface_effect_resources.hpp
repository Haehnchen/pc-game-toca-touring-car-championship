#pragma once

#include "scene/setup.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace toca {
class AssetStore;
}

namespace toca::scene {
enum class SurfaceEffectTexture : std::uint8_t {
    smoke,
    dust,
    spray,
    mud,
    gravel,
    grass
};

struct SurfaceEffectResources {
    std::array<std::vector<std::uint8_t>, 6> textures;
    std::uint32_t terrain_group = 0;
};

// 4463e0's IMAGE640 selection. The supported GPU capability branch uses
// SPRAY2, while the source's older surface fallback selects SPRAY instead.
SurfaceEffectResources load_surface_effect_resources(const AssetStore& store, const SceneSetup& setup);
} // namespace toca::scene
