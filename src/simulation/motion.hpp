#pragma once

#include "scene/body.hpp"
#include "scene/car_visual.hpp"
#include "scene/ground_scrape.hpp"
#include "simulation/barrier_contact.hpp"
#include "simulation/controls.hpp"
#include "simulation/damage_parts.hpp"
#include "simulation/rollover.hpp"
#include "simulation/surface_roughness.hpp"

#include <memory>
#include <optional>

namespace toca::scene {
struct SurfaceEffectVehicle;
struct EngineEffectVehicle;
struct CarLightGroundInput;
} // namespace toca::scene

namespace toca::simulation {
struct PropResponse;
struct SourceSnapshot;
struct SourceInput;
struct SourceLocalInputBoundary;

enum class MotionBoundary {
    none,
    missing_surface,
    unsupported_contact
};

struct MotionState {
    scene::BodyPose body;
    scene::CarVisualAngles car_visual_angles;
    std::array<std::int32_t, 3> previous_position_q16{};
    std::array<std::int32_t, 3> velocity_q16{}, angular_velocity_q16{};
    std::int32_t speed_q16 = 0, signed_speed_q16 = 0;
    std::int32_t speed_integer = 0;
    // 40b360 retains controls+128 before this tick's engine update.
    std::int32_t previous_engine_speed_q16 = 0;
    //4608b0 frame publication from raw controls+144 into body+4e8.
    std::int32_t published_brake_q16 = 0;
    std::array<std::int32_t, 4> rumble_strength_q16{};
    std::array<std::int32_t, 4> wheel_speed_q16{}, longitudinal_slip_q16{}, lateral_slip_q16{};
    // 40db10's wheel tangents precede body integration/ground correction.
    // Effects consume these retained axes rather than a later render pose.
    std::array<std::array<std::int32_t, 3>, 4> wheel_forward_q16{}, wheel_side_q16{};
    std::array<std::uint8_t, 4> surface_ids{};
    std::array<bool, 4> wheel_contact{}, previous_rumble{}, rumble{};
    //Controls+2c0 begins in process zero-fill.40b360 writes it before full
    //integration; distant bodies retain it for432210's surface-effect gate.
    bool tyre_gate = false;
    std::uint32_t contact_count = 0;
    std::uint32_t barrier_contact_count = 0;
    std::int32_t barrier_sector = -1;
    BarrierRecovery barrier_recovery;
    std::uint64_t recovery_entry_count = 0, recovery_exit_count = 0;
    std::uint64_t impact_count = 0;
    // 409fa0 barrier cues and40f5c0 landing event5 (segmentUINT32_MAX).
    std::vector<BarrierImpact> impacts;
    // Native haptic-only local prop response, cleared each physical tick.
    std::int32_t prop_impact_q16 = 0;
    //40f5c0 consumes body+514/+4ec before clearing its feedback direction.
    std::uint8_t wheel_feedback_side = 0;
    std::int16_t wheel_feedback_strength = 0;
    DamageState damage;
    RolloverState rollover;
    BarrierEffects barrier_effects;
    MotionBoundary boundary = MotionBoundary::none;
};

// One original wheel/ground/body actor. Surface and barriers must outlive
// Motion; DamageContext and process-lifetime SurfaceRoughness are borrowed.
// Shared props are advanced and dispatched by the owning scene.
// Unsupported contact/height coverage stops explicitly, preserving the last pose.
class Motion {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void reset(const scene::BodyPose& initial_body, bool qualifying);

public:
    Motion(const HpcSurface& surface, const scene::BodyPose& initial_body, std::uint32_t car_id, bool manual, std::uint32_t weather, bool qualifying, DamageContext& damage,
        SurfaceRoughness& roughness, const scene::BarrierData* barriers = nullptr, bool low_gravity = false);
    ~Motion();
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
    void set_start_hold(bool held);
    bool start_held() const;
    void prepare_ai(AiDriver driver);
    void set_ai_route_point(std::uint32_t next_point);
    void set_pit_target(std::optional<std::array<std::int32_t, 3>> target);
    void set_barrier_contacts_enabled(bool enabled);
    void handoff_to_ai(bool finished = true);
    //Source replay holds its AiDriver externally; apply only402be0's controls.
    void source_handoff_to_ai(std::int32_t tire_base_q16);
    void restore_player_control();
    void restore_qualifying_control();
    bool ai_controlled() const;
    bool ai_headlights() const;
    bool brake_lights() const;
    //4608b0 copies the raw pedal into the retained physical body field.
    void publish_frame_brake_pedal();
    //446b50 view0 consumes that published pedal and updates human lamps.
    //Other views and control changes without publication retain the output.
    void publish_frame_brake_lights(std::uint32_t view = 0);
    void reset_for_race(const scene::BodyPose& initial_body);
    //443f60→412040 starts the AI-owned qualifying approach atcccc speed,
    //without the stationary NoQual grid clutch. Caller installs its route AI.
    void reset_for_qualifying(const scene::BodyPose& initial_body);
    //Install the generated PRE pose/support cache at the first scheduled race
    //step, before selection/AI. Preserve prepared controls/AI and unrelated
    //history; this neither advances simulation nor grounds the body early.
    void prepare_first_race_step(const scene::BodyPose&);
    //Use the generated443f60 pose at the first real deadline, retaining the
    //preview's control/engine history but deriving cccc velocity from this basis.
    void prepare_first_qualifying_step(const scene::BodyPose&);
    //415e10 resets per-frame contact/effect queues even for frozen actors.
    void begin_tick();
    void update(const ControlsInput& input, const TimeStep& time, bool defer_ground = false);
    //415090 prepass: false means its upside-down player branch aborts the
    //fullbody dispatcher; earlier distant updates remain valid.
    bool prepare_body_update(bool player_identity);
    //421980 calls415240 after car/prop contacts. Mode3 settles the original
    //wreck pose; mode2/4 have no physical update. Caller owns mode transitions.
    void finish_ground(BodyMode mode = BodyMode::full);
    void settle_rollover();
    // 444830/444ee0: original distant nonplayer body, never the player.
    void update_distant(const TimeStep& time, bool defer_ground = false);
    void set_rival(bool distant);
    // Source ascending observation/decision passes precede rival movement.
    void observe_rival_ai();
    void observe_ai_route();
    void recover_ai_controls();
    void prepare_ai_controls();
    void decide_rival_traffic(const AiTraffic& self, std::span<const AiTraffic> fleet, std::int32_t route_length_q16);
    void decide_rival_traffic_brake(const AiTraffic& self, std::span<const AiTraffic> fleet);
    AiTraffic ai_traffic_state() const;
    void set_ai_start_wait(bool grid, bool reaction);
    void release_ai_start();
    void set_ai_release_counter(std::uint32_t);
    std::uint8_t traffic_mode() const;
    // 428b50 writes pending velocity/moment after body integration.
    void add_car_contact(
        const std::array<std::int32_t, 3>& velocity_delta, const std::array<std::int32_t, 3>& local_moment, const std::array<std::int32_t, 3>& position_delta = {});
    void shift_car_contact(const std::array<std::int32_t, 3>& position_delta);
    const std::array<std::int32_t, 3>& pending_car_moment() const;
    DamageResult apply_car_damage(const std::array<std::int32_t, 3>& local_arm, bool feedback_enabled);
    void set_actor_identity(std::uint32_t actor, bool human);
    //419ad0 feedback requires an assigned source FFB slot (body+53b!=-1).
    void set_damage_feedback_enabled(bool enabled);
    bool damage_feedback_enabled() const;
    void set_model_detail(std::uint8_t detail);
    void set_damage_registration(std::uint16_t sector);
    //45b900 queues acceleration/body-local moment for the next integration.
    void apply_prop_response(const PropResponse& response);
    // Diagnostic415090-entry to421aa0-return replay. Rehydrates captured state
    // for one update through the same wheel/body/contact consumers as driving.
    // The caller imports SurfaceRoughness separately; this is actor state only.
    void restore_source_snapshot(const SourceSnapshot&);
    // Only for a newly constructed cold actor. Preserve its production-owned
    // first-read context while restoring authored pose/support/control context.
    // Warm grids and diagnostic observed snapshots use their existing writers.
    void restore_cold_source_snapshot(const SourceSnapshot&);
    void publish_source_cold_context(SourceSnapshot&) const;
    ControllerHandoffState controller_handoff_state() const;
    // Publish the grouped native physical state at its actual integration/ground
    // boundary. Driver controls, actor identity and external scene inputs retain
    // the caller's values; this never imports state into the physical actor.
    void publish_source_physical_state(SourceSnapshot&) const;
    //Read the calibration actually retained by this actor's Controls.
    void publish_source_control_calibration(SourceSnapshot&) const;
    //Retained fields consumed by the shared warm460170 writer.
    void publish_warm_grid_context(SourceSnapshot&) const;
    //Use generated warm controls with the native grounded preview/AI owner.
    void restore_generated_grid_controls(const SourceSnapshot&);
    //448130/402b90/401260/422f60 prephase only; mapped physics still runs once.
    void prepare_source_local_input(const SourceLocalInputBoundary&);
    void set_source_start_hold(bool held);
    void update_source_snapshot(const TimeStep&, bool defer_ground = false);
    // Sequential replay supplies mapped controls only after the initial import.
    void update_source_input(const SourceInput&, const TimeStep&, bool defer_ground = false);
    //Diagnostic415090 mapped AI output through the same444830 body kernel.
    void update_source_distant(const SourceInput&, const TimeStep&, bool defer_ground = false);
    std::array<std::int32_t, 3> source_pending_acceleration() const;
    std::array<std::int32_t, 3> source_pending_moment() const;
    scene::SurfaceEffectVehicle surface_effect_vehicle() const;
    scene::EngineEffectVehicle engine_effect_vehicle() const;
    scene::CarLightGroundInput car_light_ground_input(bool distant_body = false) const;
    scene::GroundScrapeInput ground_scrape_input() const;
    const MotionState& state() const;
    //Retained body+b4, written before stabilization changes the current basis.
    std::int32_t retained_ground_up_q16() const;
    const VehicleState& controls() const;
    std::int32_t limiter_load_q16() const;
    PedalFilterState pedal_filter_state() const;
    SteeringFilterState steering_filter_state() const;
    const DamageState& damage() const;
    RemoteControls network_controls() const;
    RemoteMotionState network_state() const;
    void apply_network_controls(const RemoteControls&);
    void apply_network_state(const RemoteMotionState&);
    void apply_network_gear(std::uint8_t gear, bool reverse);
    void set_steering_profile(std::uint32_t profile);
    void set_network_participant();
    void apply_car_boost(const CarBoostResult&);
    void apply_network_damage(const std::array<std::uint8_t, 17>&);
};
} // namespace toca::simulation
