#pragma once

#include "formats/model.hpp"
#include "scene/camera.hpp"
#include "scene/setup.hpp"

namespace toca {
class AssetStore;
}

namespace toca::scene {
struct SunCamera {
    std::array<float, 16> world_to_view;
    Projection projection;
};

// 44f0a3 loads IMAGE640 members40..46. WHITE2/SUNMASK/SUN are loaded
// by the original too, but490b90 deliberately suppresses their draw.
struct SunResources {
    std::vector<ModelTexture> textures;
};

inline constexpr std::array<std::uint32_t, 7> lens_dimensions{32, 32, 64, 16, 64, 64, 128};
SunResources load_sun_resources(const AssetStore& store, const SceneSetup& setup);

struct LensQuad {
    std::uint32_t texture;
    float x, y, width, height, u0, v0, u1, v1;
};

// 44eda9's source point after x/z*=32 and y*=10. No case9 existed;
// Lavaland uses the compiled initial(1,1,1), rather than previous-track state.
std::array<float, 3> sun_world_position(std::uint32_t track_id);
// 491950 projects without a far-plane test, rejecting W<=1.
// 434d70 tests the64px sun rectangle;434c70 then places seven lens images.
std::vector<LensQuad> lens_quads(const SunCamera& camera, std::uint32_t track_id);
} // namespace toca::scene
