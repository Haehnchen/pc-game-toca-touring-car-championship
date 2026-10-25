#pragma once

#include "scene/finish_camera.hpp"
#include "scene/follow_camera.hpp"
#include "scene/frame_journal.hpp"
#include "simulation/time.hpp"

namespace toca::simulation {
std::array<scene::SceneJournalRoot, 18> source_scene_roots(const std::array<scene::GridSlot, 32>&);
//510bf authored actors precede car/follow/head allocation. The ordinary18-root
//subset keeps its existing identities; this complete role list is opt-in.
std::vector<scene::SceneJournalRoot> source_lifecycle_scene_roots(const std::array<scene::GridSlot, 32>&, std::span<const scene::SceneCamera>);

struct SceneLifecycleBoundary {
    std::uint32_t state = 9;
    std::int32_t timer0_q16 = 0;
};

struct SceneCameraControls {
    bool cycle_held = false, rear_held = false;
};

struct SceneCameraState {
    std::uint16_t view = 2, projection_view = 2;
    bool rear_held = false;
    std::uint32_t ancestor_root = scene::no_visibility_entry;
    bool authored = false;
    std::uint32_t authored_actor = scene::no_visibility_entry;
};

struct ScenePhysicalBoundary {
    std::uint32_t session_state = 9;
    scene::BodyPose before;
    scene::BodyPose local_body;
    std::array<std::int32_t, 3> previous_velocity{}, velocity{};
    std::int32_t angular_velocity_y = 0;
    std::array<std::array<std::int32_t, 3>, 16> positions{};
    std::array<std::uint8_t, 16> modes{};
};

// One-player NoQual views1..5, local finish/results and retained warm scene reset.
// Physical bodies and the generated session timer enter at explicit boundaries;
// camera selection/matrices and scene calls are owned. A QPC owner generates
// extra passes; clockless component callers supply observed TimeStep boundaries.
class SourceSceneFrame {
    std::optional<SourceClock> clock_;
    TimeStep step_;
    scene::FollowCamera camera_;
    std::optional<scene::FinishCamera> finish_camera_;
    std::optional<HpcSurface> surface_;
    std::vector<std::uint32_t> authored_roots_;
    SceneLifecycleBoundary lifecycle_;
    bool authored_ = false, grid_gates_ = true, warm_entry_ = false;
    bool first_qualifying_render_ = false, qualifying_finish_seed_ = false, observed_frame_ = false;
    std::optional<std::uint32_t> qualifying_track_;
    std::vector<std::uint8_t> route_points_;
    scene::SceneFrameJournal scene_;
    scene::Projection projection_;
    scene::Projection published_projection_;
    SceneCameraState published_state_;
    scene::FollowCameraFrame published_;
    scene::FleetSceneContext initial_context_;
    std::array<scene::SceneJournalCarMasks, 16> initial_masks_{};
    std::array<std::uint32_t, 16> car_roots_{};
    std::uint32_t follow_root_ = scene::no_visibility_entry, cockpit_root_ = scene::no_visibility_entry, root_count_ = 0, local_, detail_, physical_ = 0;
    std::uint64_t order_ = 0;
    bool has_publication_ = false, physical_begun_ = false, controls_consumed_ = false, cycle_held_ = false;
    std::optional<ScenePhysicalBoundary> pending_primary_;
    std::optional<ScenePhysicalBoundary> last_physical_;
    void refresh(const ScenePhysicalBoundary&);
    void reset_clock(std::optional<std::int32_t> divisor);
    void restore_grid(std::span<const scene::BodyPose, 16>, std::int16_t heading, std::optional<std::int32_t> divisor);

public:
    SourceSceneFrame(scene::Visibility, std::span<const scene::SceneJournalRoot>, std::int16_t authored_heading, std::uint32_t local_actor, std::uint32_t car, std::uint32_t detail,
        std::optional<std::int32_t> divisor, TimeStep initial = {}, std::span<const scene::SceneCamera> cameras = {}, std::span<const std::uint8_t> collision_hpc = {});
    //44e7bb/47de00 allocation-order roots and canonical static registrations.
    SourceSceneFrame(const scene::SceneResources&, std::uint32_t local_actor, std::optional<std::int32_t> divisor, TimeStep initial = {}, bool lifecycle = false);
    //44a370 commits generated pending state before the primary input wrapper.
    //Timer0 is the generated session timer, never a camera-selection input.
    void set_lifecycle(SceneLifecycleBoundary);
    //449f50 and the later447980/460170 are distinct warm boundaries.
    void warm_reset();
    void warm_grid(std::span<const scene::BodyPose, 16>, std::int16_t heading, std::optional<std::int32_t> divisor = {});
    //443f60/44ffe1 forces every POINTS parent to its initial position, even
    //the hidden quarter exclusions.45eb60 later owns the track/map heading.
    void initialize_qualifying(std::span<const scene::BodyPose, 16>, std::uint32_t track, std::int16_t placement_heading, std::optional<std::int32_t> divisor = {});
    //448790/447980 enters the earned race grid without449f50: authored index,
    //view/head/input/projection and mesh eligibility retain their lifetimes.
    void race_grid(std::span<const scene::BodyPose, 16>, std::int16_t heading, std::optional<std::int32_t> divisor = {});
    //Explicit component boundary for recordings without QPC transactions.
    //Only a constructor with divisor=nullopt accepts caller-owned TimeSteps.
    void begin_observed_frame(TimeStep);
    void finish_observed_frame();
    //460170 updates all car parents and both camera anchors from generated grids.
    void initialize_grid(std::span<const scene::BodyPose, 16>);
    //421aa0 slews using the old camera selection before422f60 maps controls.
    void begin_physical(const scene::BodyPose& before, std::uint32_t session_state);
    void camera_controls(const SceneCameraControls&);
    //421980/42f950 updates the cockpit after the generated ground pass.
    void complete_physical(const ScenePhysicalBoundary&);
    //4871b2 selects camera/PVS/models before420420's wheel callback.
    void publish_primary();
    //44a370 still dispatches the State13 renderer when a menu overlay prevents
    //421aa0. Render the latest generated bodies, including the preceding extra.
    void publish_retained();
    //421aa0 slews before physics;41f520 publishes after the primary pass.
    void physical(const ScenePhysicalBoundary&);
    //44a370 samples after publication and the optional extra physical pass.
    void finish(std::uint32_t next_counter);
    //Menu-only primary clock samples retain the previous camera/scene snapshot.
    void sample_idle(std::uint32_t next_counter);
    const TimeStep& step() const;
    const scene::FollowCameraFrame& camera() const;
    const scene::Projection& projection() const;
    const scene::SceneFrameJournal& scene() const;
    std::uint16_t selected_view() const;
    std::uint16_t projection_view() const;
    bool rear_held() const;
    std::uint32_t ancestor_root() const;
    const SceneCameraState& published_state() const;
    std::uint32_t car_root(std::uint32_t actor) const;
    std::uint32_t authored_root(std::uint32_t actor) const;
    const scene::FleetSceneContext& retained_context() const;
    const std::array<scene::SceneJournalCarMasks, 16>& car_masks() const;
};
} // namespace toca::simulation
