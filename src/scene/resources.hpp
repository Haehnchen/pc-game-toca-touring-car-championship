#pragma once

#include "formats/terrain.hpp"
#include "frontend/resources.hpp"
#include "scene/cockpit.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/pit_route.hpp"
#include "scene/props.hpp"
#include "scene/setup.hpp"
#include "scene/sun.hpp"

namespace toca::scene {
struct GridSlot {
    std::array<std::int32_t, 2> first_xz{}, second_xz{};
    std::int16_t heading = 0;
    std::uint16_t reserved = 0;
};

struct SceneCamera {
    std::array<std::int32_t, 16> transform{};
};

struct SceneResources {
    SceneSetup setup;
    // HPC and POINTS/LOWPOINT retain bytes for their separate query consumers.
    std::vector<std::uint8_t> collision_hpc, route_points;
    PitRoute pits;
    bool low_detail_points = false;
    std::array<GridSlot, 32> grid;
    Terrain ground;
    Model trees, fence, objects;
    Terrain trees_baked, fence_baked, objects_baked;
    PropResources cones, posts, signs, flags;
    std::vector<SceneCamera> cameras;
    frontend::CarModels car;
    CockpitResources cockpit;
    std::vector<std::uint8_t> car_shadow_color_bmp, car_shadow_alpha_bmp;
    // 434da0: IMAGE640 BGLOW/HGLOW, shared by every car and detail level.
    std::array<std::vector<std::uint8_t>, 2> car_glow_bmps;
    std::vector<std::uint8_t> horizon_actors, horizon_auxiliary;
    HorizonActorResources moving_horizon;
    Model horizon;
    SunResources sun;
};

std::array<GridSlot, 32> decode_start_grid(std::span<const std::uint8_t> bytes);
std::vector<SceneCamera> decode_scene_cameras(std::span<const std::uint8_t> bytes);
// Mega presentation assets retain all original physics, actor and sector data.
// Shared texture keys are the eight-digit CRC32 of the canonical BMP bytes;
// track-scoped GLBs retain original node/variant and mesh identities.
void apply_mega_scene_resources(SceneResources& resources, const std::filesystem::path& mega_models);
SceneResources load_scene_resources(const AssetStore& store, const SceneSetup& setup, const std::filesystem::path& mega_models = {});
} // namespace toca::scene
