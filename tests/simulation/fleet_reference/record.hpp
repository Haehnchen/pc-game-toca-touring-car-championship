#pragma once

#include "scene/catalog.hpp"
#include "scene/visibility.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/controls.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/rollover.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/time.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace toca::test::fleet {
using namespace toca::simulation;

struct InitialWorldValue {
    std::string field;
    std::uint32_t index;
    std::int64_t value;
};

struct ActorInput {
    scene::RootSectorRegistration registration;
    bool visible = false;
    std::uint8_t mask = 0, detail = 0;
    SourceSnapshot snapshot;
};

struct AiInitialState {
    std::array<std::uint8_t, 0x540> resolved;
    std::vector<std::uint8_t> route;
    AiRouteContext context;
    std::vector<std::uint8_t> brake_bypass;
    std::int32_t rank_threshold = 0;
    std::vector<std::int32_t> cumulative_route;
    std::optional<scene::PitRoutePoint> pit_entry;
};

struct AiRouteActorInput {
    std::uint32_t body_mode = 0;
    std::int32_t speed_q16 = 0;
    scene::BodyPose body;
    std::array<std::uint8_t, 0x124> gameplay;
};

struct LocalHandoffEvent {
    std::uint32_t kind = 0, actor = 0, return_address = 0, rng = 0;
    std::int32_t ai_tire_base = 0;
    ControllerHandoffState before;
};

struct ReleaseEvent {
    std::uint32_t return_address = 0, source_mode = 0, stage = 0, local_players = 0;
};

struct NearestRecoveryEvent {
    std::uint32_t actor = 0, rng = 0, refresh = 0, human = 0;
    fixed::Vector position{};
    RouteCursor cursor;
};

struct TrafficRecord {
    std::uint32_t rng = 0, source_mode = 0, race_stop = 0, release_counter = 0, force_acceleration = 0;
    std::int32_t session_state = 0, weather_penalty = 0;
    std::array<std::uint8_t, 32> tuning{};
    std::array<std::uint8_t, 24> status{};
    std::array<std::uint8_t, 1212> braking_memory{};
    std::array<std::uint32_t, 3> headlight_gates{};
    std::array<SourceSnapshot, 16> actors;
};

struct Record {
    std::uint32_t id = 0, rng = 0, roughness = 0, damage_cycle = 0, detached_count = 0;
    bool contacts_enabled = false;
    TimeStep time;
    std::array<std::uint8_t, 0x800> pair_history;
    std::array<std::uint8_t, 16> ownership;
    std::array<std::uint8_t, 15> damage_selection;
    std::array<std::uint8_t, 272> damage_levels;
    std::array<std::uint8_t, 256> broken_features, visibility;
    std::array<std::uint8_t, 10 * 0x11c> detached_pool;
    std::array<std::uint8_t, 40> detached_free;
    std::uint32_t visibility_control = 0, visibility_override = 0;
    std::int32_t ai_tire_base = 0;
    std::uint32_t launch_flags = 0, pre_start_hold = 0, release_epoch = 0, pre_session_state = 0;
    std::int32_t consumed_launch = 0, pre_launch = 0, pre_rpm = 0, pre_controller_owner = -1;
    BodySelectionContext selection_context;
    std::array<BodySelectionActor, 16> selection;
    std::uint32_t ai_release_counter = 0, ai_rng = 0;
    std::array<AiRouteActorInput, 16> ai_actors;
    std::uint32_t timing_clock = 0, timing_lap_limit = 0, timing_flags = 0, timing_running = 0;
    std::int32_t timing_stage = 0, timing_state = 0;
    CheckpointTimes checkpoint_times;
    std::uint32_t source_mode = 0, record_playback = 0, finish_release_clock = 0;
    RaceFinishState finish;
    SplitDisplays displays;
    std::vector<LocalHandoffEvent> handoffs;
    bool session_extended = false;
    std::array<std::uint8_t, 0x338> session_dynamics{};
    std::int32_t session_ai_tire_base = 0;
    std::array<std::int32_t, 4> session_launch{};
    std::array<std::uint8_t, 16> classification_order{}, split_enable_raw{};
    std::array<std::uint8_t, 48> configuration_tail{};
    std::int32_t lap_distance = 0;
    TrafficRecord traffic;
    std::vector<NearestRecoveryEvent> nearest_recoveries;
    std::uint32_t update_caller = 0;
    std::vector<ReleaseEvent> releases;
    std::uint32_t release_marker = 0;
    std::array<std::uint32_t, 4> release_flags{};
    std::array<std::vector<std::uint8_t>, 3> props;
    std::vector<std::array<std::uint8_t, 68>> sign_models;
    std::array<ActorInput, 16> actors;
};

AiInitialState read_ai_initial(std::istream& input);
Record read_record(std::istream& input, std::uint32_t steering_profile, std::uint32_t local, bool has_ai, bool has_timing, bool has_finish, bool has_handoffs, bool has_session,
    bool has_traffic, bool has_nearest, bool has_joined, bool retained_limiter = false, bool startup_initial = false, bool generated_time = false);

} // namespace toca::test::fleet
