#include "scene/surface_effect_resources.hpp"

#include "assets/store.hpp"

#include <stdexcept>

namespace toca::scene {
SurfaceEffectResources load_surface_effect_resources(const AssetStore& store, const SceneSetup& setup) {
    if (setup.selection.track_id > 9 || setup.weather < 1 || setup.weather > 5) {
        throw std::runtime_error("Surface effects require an original track and resolved weather");
    }
    // 4463e0 groups by track, independently of weather. Weather changes the
    // emitted pools and their source shade bytes, not these resource indices.
    constexpr std::array<std::uint32_t, 10> groups{2, 1, 2, 0, 0, 2, 3, 2, 0, 3};
    SurfaceEffectResources result;
    result.terrain_group = groups[setup.selection.track_id];
    const std::array<std::uint32_t, 6> indices{0, 1 + result.terrain_group, 5, 7, 8 + result.terrain_group, 12};
    const auto images = store.open_archive("data/image640.bfp");
    for (std::size_t texture = 0; texture < indices.size(); ++texture) {
        const auto bytes = images.member(indices[texture]).bytes();
        result.textures[texture].assign(bytes.begin(), bytes.end());
    }
    return result;
}
} // namespace toca::scene
