#pragma once

#include "core/random.hpp"
#include "scene/body.hpp"
#include "simulation/route_progress.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace toca::simulation {
//4019f0 publishes POINTS heights through42e9f0, in records1..N-1 then0
//order with one retained triangle cache. Input remains authored archive data.
std::vector<std::uint8_t> initialize_ai_route_heights(std::span<const std::uint8_t>, const HpcSurface&);

// The four source +138/+13c/+140/+144 fields, before 418cd0 filtering.
struct DriverAxes {
    std::int32_t left_q16 = 0, right_q16 = 0, throttle_q16 = 0, brake_q16 = 0;
    //403dd0 writes byte330 without canceling the queued418cd0 commit.
    std::int8_t forced_gear = -1;
    bool reset_reverse_pedal = false; //Only an actual403dd0 exit writes304=0.
};

struct AiTraffic {
    scene::BodyPose body;
    std::int32_t speed_q16 = 0;
    std::uint32_t next_point = 0;
    std::uint32_t actor_id = UINT32_MAX, ahead_id = UINT32_MAX, behind_id = UINT32_MAX;
    std::int32_t route_distance_q16 = 0, forward_gap_q16 = 0;
    bool player = false, human = false, reversing = false, route_unavailable = false;
    bool blocked = false, passing = false;
    std::uint32_t blocker_id = UINT32_MAX;
    std::uint8_t mode = 1;
    std::int16_t route_angle = 0, lap = 1;
    bool refresh = false, swap_start_lines = false, obstacle = false;
    //Source game+10 direction flag is distinct from+28 reverse recovery.
    bool opposing_direction = false;
};

struct AiRouteContext;

//4036f0/403aa0 retained fields, before mapped axes or physical filtering.
struct AiTrafficDecisionState {
    std::uint8_t mode = 1;
    bool passing = false, blocked = false, forced_brake = false, brake = false, steering_cached = false;
    std::uint32_t passed_actor = UINT32_MAX, blocker_actor = UINT32_MAX;
    std::array<bool, 4> corridor{};
    std::array<std::uint8_t, 2> corners{};
    std::array<std::int16_t, 2> obstacle_angles{};
    std::array<std::uint8_t, 3> headlight_ticks{};
    bool headlights = false;
};

struct AiTrafficContext {
    std::uint32_t release_counter = 0;
    bool qualifying = false, headlight_gates = false;
    const AiRouteContext* start_lines = nullptr;
};

struct AiMappedControlState {
    DriverAxes axes;
    bool reversing = false, brake_lights = false;
    std::uint8_t reverse_ticks = 0, reaction_ticks = 0, brake_ticks = 0;
    std::int8_t gear = 1;
    std::int32_t reverse_pedal = 0;
};

struct AiControlContext {
    bool started = true, suppressed = false, force_acceleration = false, tyre_brake_disabled = false;
    bool player = true, brake_bypass = false, obstacle = false, barrier_hit = false;
    //405530 small distant/full, then large full/distant source gains.
    std::array<std::int32_t, 4> steering_gains{0x141, 0x17d, 0x4a7, 0x306};
};

// State18's shared actor0 forward-neighbor record (cc608/cc5ae).
struct FinaleTraffic {
    std::int32_t truck_forward_gap_q16 = 0;
    std::uint32_t truck_ahead_actor = 1;
};

enum class AiRouteEncoding {
    authored,
    source_initialized
};

// Named 403dd0 route-cache state, independent of process pointers and traffic.
struct AiRouteState {
    std::uint32_t cached_point = UINT32_MAX;
    std::uint8_t traffic_mode = 1;
    std::array<std::int32_t, 3> target{}, alternate_target{};
    std::int32_t desired_speed_q16 = 0, alternate_speed_q16 = 0, speed_variation_q16 = 0;
    std::int32_t distance_q16 = 0, projected_distance_q16 = 0;
    std::int16_t angle = 0, alternate_angle = 0, route_angle = 0;
    std::int16_t primary_boundary_angle = 0, alternate_boundary_angle = 0;
};

struct AiRouteCadence {
    std::uint8_t ticks = 0;
    bool refresh = false;
};

struct AiRouteProgressInput {
    RouteCursor cursor;
    bool backwards = false, test_previous = true, human = false;
    std::uint8_t checkpoint = 1, checkpoint_marker = 2;
    bool finished = false;
};

struct AiRouteProgress {
    RouteCursor cursor, target_cursor;
    bool backwards = false, checkpoint_side_effect = false, recovered = false;
    bool lap_event = false, split_event = false;
    std::uint8_t split_sector = 0;
    std::uint8_t current_gate = 1, previous_gate = 1, checkpoint = 1, checkpoint_marker = 2;
    RouteCrossing crossing = RouteCrossing::none;
};

// 4019f0's initialized start-line targets; 4024c0 selects them during release.
struct AiRouteContext {
    std::uint32_t release_counter = 0, start_bound = 25, end_bound = 0;
    std::array<std::array<std::int32_t, 3>, 33> start_primary{}, start_alternate{};
    bool qualifying = false, swap_start_lines = false, full_body = true, player = false, finished = false;
};

// Original POINTS/AIBIN controls for local402be0 handoff and rival traffic.
// Owns decoded POINTS and AIBIN values. RNG is the original CRT generator with
// the host-owned source stream. Its owner must outlive this driver.
class AiDriver {
    struct Point {
        std::array<std::int32_t, 3> first{}, target{}, alternate{}, second{};
        std::int32_t speed_q16 = 0, alternate_speed_q16 = 0;
        std::array<std::int32_t, 2> packed_lateral{};
    };

    std::vector<Point> points_;
    NearestRouteMap nearest_route_;
    AiRouteEncoding route_encoding_;
    std::array<std::vector<std::int16_t>, 3> distributions_;
    std::array<std::uint8_t, 4> variation_counts_{};
    std::uint8_t route_refresh_period_ = 0;
    std::uint8_t route_ticks_ = 0;
    SourceRandom* random_;
    std::int32_t throttle_q16_, weather_penalty_q16_, tire_offset_q16_;
    std::int32_t speed_variation_q16_ = 0, desired_speed_q16_ = 0, alternate_speed_q16_ = 0, distance_q16_ = 0;
    std::array<std::int32_t, 2> distant_variation_q16_{};
    std::array<std::int32_t, 3> target_{}, alternate_target_{};
    std::uint8_t traffic_mode_ = 1;
    std::array<bool, 4> corridor_blocked_{};
    std::array<std::uint8_t, 2> traffic_corners_{};
    std::array<std::int16_t, 2> obstacle_angles_{};
    std::array<std::uint8_t, 3> headlight_ticks_{};
    bool headlights_ = false, traffic_steering_cached_ = false;
    AiTrafficContext traffic_context_;
    std::optional<AiRouteContext> native_start_lines_;
    AiControlContext control_context_;
    AiMappedControlState mapped_controls_;
    std::optional<AiTraffic> traffic_ahead_, traffic_behind_;
    AiTraffic traffic_self_;
    std::array<std::uint8_t, 1212> braking_memory_{};
    bool passing_ = false, blocked_ = false, force_traffic_brake_ = false, traffic_brake_ = false, grid_wait_ = false, reaction_wait_ = false;
    std::uint32_t passed_actor_ = UINT32_MAX, blocker_actor_ = UINT32_MAX;
    std::int32_t traffic_stopping_q16_ = 0;
    std::int16_t alternate_angle_ = 0;
    std::optional<std::array<std::int32_t, 3>> pit_target_;
    std::uint32_t cached_point_ = UINT32_MAX;
    std::uint32_t previous_route_point_ = UINT32_MAX;
    std::int16_t angle_ = 0, route_angle_ = 0;
    std::int16_t primary_boundary_angle_ = 0, alternate_boundary_angle_ = 0;
    std::int32_t route_distance_q16_ = 0;
    bool reversing_ = false;
    bool finished_ = true;
    bool rival_ = false, distant_ = false;
    const FinaleTraffic* finale_ = nullptr;
    std::uint32_t finale_actor_ = 0;
    std::int32_t sample(std::size_t distribution, std::uint8_t count);
    std::int32_t braking_distance(std::int32_t speed, std::int32_t target) const;
    void select_target(std::uint32_t point, const AiRouteContext* source = nullptr);
    void observe_route_core(const scene::BodyPose&, std::int32_t game_speed_q16, std::uint32_t next_point, std::int16_t route_angle, const AiRouteContext*);
    void select_traffic_angle(const scene::BodyPose&, const AiTraffic&, std::int32_t gap);
    void advance_headlights();

public:
    AiDriver(std::span<const std::uint8_t> route, std::span<const std::uint8_t, 40> driver, const std::array<std::vector<std::int16_t>, 3>& distributions,
        std::int32_t weather_speed_penalty_q16, std::int32_t tire_offset_q16, SourceRandom& random, bool impossible = false, AiRouteEncoding encoding = AiRouteEncoding::authored);
    //401370's fresh race-grid caches, before route observation or RNG draws.
    //Qualifying continuation and later controller handoffs do not call this.
    void initialize_race_grid();
    // 403dd0 target refresh happens before controls, also while human-owned.
    void observe(const scene::BodyPose& body, std::int32_t game_speed_q16, std::uint32_t next_point);
    // Set before observe. 403030 substitutes PITS[index+1].Best while normal
    // POINTS target selection and its source RNG chronology continue.
    void set_pit_target(std::optional<std::array<std::int32_t, 3>> target);
    void handoff(std::int8_t gear, bool finished = true);
    //421390 can finish an AI actor without402be0's controller/reverse writes.
    void synchronize_source_finish(bool finished);
    // 415090 selects the original nonplayer full/distant AI gains.
    void set_rival(bool rival, bool distant);
    // 403dd0 observes all routes, decides all passing modes, then all brakes.
    // The caller updates fleet records after each ascending actor decision.
    void observe_route(const scene::BodyPose&, std::int32_t game_speed_q16, std::uint32_t next_point);
    // Bounded diagnostic boundary uses the same production target/projection
    // consumer. Inputs precede traffic and control filtering; no later outputs.
    void restore_source_route(const AiRouteState&);
    //403dd0 updates game+86/+3c before its4218e0 route-heading pass.
    AiRouteCadence advance_source_route_cadence(std::uint8_t ticks) const;
    void initialize_source_route_cadence(std::uint8_t ticks);
    AiRouteCadence advance_source_route_cadence();
    AiRouteState route_state() const;
    AiRouteProgress advance_source_route_progress(const scene::BodyPose&, const AiRouteProgressInput&, bool refresh) const;
    std::array<bool, 2> source_route_corridor(const scene::BodyPose&, std::uint32_t target_cursor, std::uint32_t previous_cursor) const;
    std::int16_t source_route_angle(const scene::BodyPose&, std::uint32_t point) const;
    void observe_source_route(const scene::BodyPose&, std::int32_t game_speed_q16, std::uint32_t next_point, std::int16_t route_angle, const AiRouteContext&);
    void restore_source_braking_memory(std::span<const std::uint8_t, 1212>);
    void advance_source_braking_notice();
    void restore_source_traffic(const AiTrafficDecisionState&);
    AiTrafficDecisionState traffic_decision_state() const;
    void set_traffic_context(const AiTrafficContext&);
    void set_release_counter(std::uint32_t);
    void release_start();
    void initialize_headlights(bool active);
    bool headlights_requested() const;
    bool brake_lights_requested() const;
    void restore_source_controls(const AiMappedControlState&);
    //Physics owns gearbox/reverse pedal and may clear steering at rest; the
    //driver's reaction/recovery/cadence/lamp latches survive this synchronization.
    void synchronize_source_physical_controls(std::int8_t gear, std::int32_t reverse_pedal, const DriverAxes* axes = nullptr);
    void apply_source_motion_steering_gate(std::int32_t signed_speed_q16, bool full_body);
    AiMappedControlState mapped_control_state() const;
    void set_control_context(const AiControlContext&);
    void retain_control_axes(const DriverAxes&);
    void advance_source_reverse_recovery(std::int32_t speed_q16, bool barrier);
    DriverAxes map_source_controls(const scene::BodyPose&, std::int32_t speed_q16);
    std::int32_t traffic_stopping_distance(std::int32_t speed_q16, const AiTraffic& ahead) const;
    //The lookup view includes allocated retired actors; active ring membership
    //only determines which actors receive these traffic passes.
    void decide_traffic(const scene::BodyPose&, std::int32_t game_speed_q16, const AiTraffic& self, std::span<const AiTraffic> fleet, std::int32_t route_length_q16);
    void decide_traffic_brake(std::int32_t game_speed_q16, const AiTraffic& self, std::span<const AiTraffic> fleet);
    AiTraffic traffic_state(const scene::BodyPose&, std::int32_t game_speed_q16, std::uint32_t next_point) const;
    void set_start_wait(bool grid, bool reaction);
    //449790/4051e0. Borrowed shared record must outlive this driver.
    void set_finale(std::uint32_t actor, const FinaleTraffic& traffic);
    std::uint8_t traffic_mode() const;
    DriverAxes axes(const scene::BodyPose& body, std::int32_t game_speed_q16, std::int8_t gear, bool advance_recovery = true);
    std::int32_t tire_offset_q16() const;
    std::uint32_t target_point() const;
    std::int16_t steering_error() const;
};
} // namespace toca::simulation
