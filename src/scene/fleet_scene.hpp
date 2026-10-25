#pragma once

#include "scene/visibility.hpp"

#include <optional>

namespace toca::scene {
struct CarSceneContext {
    RootSectorRegistration physical;
    bool sector_visible = true;
    std::uint8_t model_detail = 0;
    // +138 geometry selection is independent of manual +140 model eligibility.
    std::uint8_t render_mask = 0, contact_effect_mask = 0;
    // Single-view manual eligibility. Simulation applies the existing full-body
    // gate (4ed968) when consuming wheel/exhaust masks, before the next refresh.
    std::uint8_t wheel_effect_mask = 0, engine_effect_mask = 0;
};

struct FleetSceneContext {
    std::array<CarSceneContext, 16> actors;
    VisibilityFrame visibility;
};

struct CarScenePose {
    std::uint32_t actor = 0;
    std::array<float, 3> position{};
    std::uint8_t render_mask = 0;
    bool present = true;
    std::uint8_t manual_mask = 1;
    std::optional<std::array<std::int32_t, 3>> source_position = std::nullopt;
};

struct FleetSceneView {
    std::array<float, 3> observer{};
    std::uint32_t graphics_detail = 3;
    bool player_model_visible = true;
    //Ordinary single-player427170 also suppresses local manual effects in View1.
    bool source_local_visibility = false;
    std::optional<std::array<std::int32_t, 3>> source_observer = std::nullopt;
};

std::int32_t source_view_factor(std::uint16_t view);
//4271d0's fixed observer, including the separate View1 forward displacement.
std::array<std::int32_t, 3> source_fleet_scene_observer(std::uint16_t view, const std::array<float, 3>& camera_position, const std::array<std::int32_t, 3>& local_body_position,
    const std::array<std::int32_t, 3>& local_forward, std::optional<std::array<float, 3>> local_parent_position = std::nullopt);
std::array<float, 3> fleet_scene_observer(std::uint16_t view, bool authored_camera, const std::array<float, 3>& camera_position, const std::array<float, 3>& local_body_position,
    const std::array<std::int32_t, 3>& local_forward = {65536, 0, 0}, std::optional<std::array<float, 3>> local_parent_position = std::nullopt);

// CPU owner shared by rendering and simulation. Actual camera/selection refresh
// boundaries remain caller inputs; presentation copies never advance this owner.
class FleetScene {
    CarSceneContext player_;
    std::array<CarSceneContext, 16> rivals_;
    bool player_initialized_ = false;
    std::array<bool, 16> rival_initialized_{};
    std::array<std::uint8_t, 16> lod_holds_{};
    VisibilityFrame visibility_;

public:
    // Reset physical registration only; model hysteresis retains its lifetime.
    void reset_registration();
    void select_models(const FleetSceneView&, std::span<const CarScenePose> rivals, bool force = false);
    void update(const VisibilityData&, const VisibilityFrame&, const FleetSceneView&, const CarScenePose& player, std::span<const CarScenePose> rivals, bool force_models = false);
    const CarSceneContext& player() const;
    const CarSceneContext& rival(std::uint32_t actor) const;
    // SceneView's player role is bound here to the session's permanent actor ID.
    FleetSceneContext context(std::uint32_t local_actor) const;
};
} // namespace toca::scene
