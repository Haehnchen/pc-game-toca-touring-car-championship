#pragma once

#include "formats/terrain.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/camera.hpp"
#include "scene/car_damage.hpp"
#include "scene/car_light_glow.hpp"
#include "scene/car_lights.hpp"
#include "scene/car_shadow.hpp"
#include "scene/car_visual.hpp"
#include "scene/contact_effects.hpp"
#include "scene/fleet_scene.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/lighting.hpp"
#include "scene/model_pose.hpp"
#include "scene/prop_fragments.hpp"
#include "scene/render_geometry.hpp"
#include "scene/resources.hpp"
#include "scene/terrain_view.hpp"
#include "scene/visibility.hpp"

#include <optional>

namespace toca::simulation {
struct DetachedPart;
}

namespace toca::scene {
struct WeatherResources;
struct WeatherFrame;
struct SurfaceEffectResources;
struct SurfaceEffectFrame;
struct SkidMarkFrame;
struct EngineEffectResources;
struct EngineEffectFrame;
struct VisibilityFrame;
class ModelTextures;
class ModelMeshes;

struct RivalModel {
    std::uint32_t actor_id;
    const frontend::CarModels* car;
    // Stable resource identity, including after the moving actor disappears.
    // Split players keep their explicit personal-model role, independent of ID.
    bool second_driver = false;
};

struct RivalPose {
    std::uint32_t actor_id;
    CarPose car;
};

// CPU projection consumers of 0047e730/00487ee9; no GPU context needed.
std::uint32_t append_projected_triangle(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const CameraInputs& camera);
void append_contact_sparks(gpu::Frame& frame, std::span<const ContactSparkLine> lines, const CameraInputs& camera);
// Validate the source camera for streamed prelit world geometry.
gpu::WorldProjection checked_world_projection(const CameraInputs& camera);

// Local car damage/parts, scenery, camera-relative horizon and moving HOR actors.
// Resources must outlive the view; simulation supplies the body/camera poses.
class SceneView {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    SceneView(gpu::Renderer& renderer, const SceneResources& resources, const WeatherResources* weather = nullptr, const SurfaceEffectResources* surface_effects = nullptr,
        const EngineEffectResources* engine_effects = nullptr, std::span<const RivalModel> rivals = {}, const frontend::CarModels* local_car = nullptr,
        const CockpitResources* local_cockpit = nullptr, bool sun_enabled = true, std::shared_ptr<ModelTextures> textures = {}, std::shared_ptr<ModelMeshes> meshes = {});
    ~SceneView();
    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;
    std::shared_ptr<ModelTextures> model_textures() const;
    std::shared_ptr<ModelMeshes> model_meshes() const;
    // Personal 128x16 name bitmaps (256x32 for Mega) replace the existing name
    // faces. Null restores the team name; source atlases remain immutable.
    void set_player_name(gpu::Renderer& renderer, SDL_Surface* bitmap);
    void set_rival_name(gpu::Renderer& renderer, std::uint32_t actor, SDL_Surface* bitmap);
    void set_ghost_name(gpu::Renderer& renderer, SDL_Surface* bitmap);
    void set_ghost_model(gpu::Renderer& renderer, const frontend::CarModels& model);
    void gear_changed();
    std::uint8_t cockpit_shift_phase() const;
    // Advance source root registration and effect masks once per active tick.
    // Frame preparation uses a copy, so presentation cannot mutate this state.
    void update_visibility(const CameraInputs& camera, const CarPose& car, const HorizonFrame& actors, const PropFrame& props = {}, const CarPose* ghost = nullptr,
        std::span<const RivalPose> rivals = {});
    const VisibilityFrame& visibility() const;
    FleetSceneContext scene_context(std::uint32_t local_actor) const;
    std::uint8_t player_visibility() const;
    std::uint8_t rival_visibility(std::uint32_t actor) const;
    bool rival_sector_visible(std::uint32_t actor) const;
    void reset_contact_registration();
    RootSectorRegistration player_contact_registration() const;
    RootSectorRegistration rival_contact_registration(std::uint32_t actor) const;
    std::uint8_t rival_model_detail(std::uint32_t actor) const;
    gpu::Frame frame(const CameraInputs& camera, const Lighting& lighting, const CarPose& car, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors,
        const PropFrame& props = {}, std::span<const PropFragmentTriangle> fragments = {}, const WeatherFrame* weather = nullptr,
        std::span<const simulation::DetachedPart> damage_parts = {}, std::span<const ContactSparkLine> sparks = {}, const SurfaceEffectFrame* surface_effects = nullptr,
        const SkidMarkFrame* skid_marks = nullptr, const CarPose* ghost = nullptr, bool ghost_uses_player_car = false, const EngineEffectFrame* engine_effects = nullptr,
        std::span<const RivalPose> rivals = {}, float presentation_alpha = 1) const;
    // Replace the output while retaining its buffers for the next presentation.
    void frame_into(gpu::Frame& output, const CameraInputs& camera, const Lighting& lighting, const CarPose& car, std::span<const std::uint8_t> flag_variants,
        const HorizonFrame& actors, const PropFrame& props = {}, std::span<const PropFragmentTriangle> fragments = {}, const WeatherFrame* weather = nullptr,
        std::span<const simulation::DetachedPart> damage_parts = {}, std::span<const ContactSparkLine> sparks = {}, const SurfaceEffectFrame* surface_effects = nullptr,
        const SkidMarkFrame* skid_marks = nullptr, const CarPose* ghost = nullptr, bool ghost_uses_player_car = false, const EngineEffectFrame* engine_effects = nullptr,
        std::span<const RivalPose> rivals = {}, float presentation_alpha = 1) const;
};
} // namespace toca::scene
