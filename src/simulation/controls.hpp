#pragma once

#include "core/input_settings.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/car_boost.hpp"
#include "simulation/remote_state.hpp"
#include "simulation/time.hpp"

#include <array>
#include <optional>
#include <span>

namespace toca::simulation {
struct SourceSnapshot;
struct SourceInput;

//4608b0/4610ce publishes every actor's retained raw+144 to body+4e8,
//descending by actor, independently of ownership and body/render gates.
void publish_source_frame_brake_pedals(std::span<const std::int32_t> raw, std::span<std::int32_t> published);

//446b50 updates human lamps only in view0, before its render/effect gates.
//The brake input is4608b0's published body+4e8, not filtered brake torque.
inline std::uint32_t source_frame_brake_lights(std::uint32_t retained, std::int32_t human, std::int32_t published_brake, std::int32_t reverse_pedal, std::uint32_t view) {
    return view == 0 && human != 0 ? std::uint32_t(published_brake >= 0x1999 && reverse_pedal == 0) : retained;
}

//448130 initializes the local launch scalar from physical engine values.
std::int32_t source_launch_adjustment(std::int32_t engine_speed_q16, std::int32_t maximum_engine_q16);
//422f60 advances that retained scalar and publishes all eight tire words.
void advance_source_launch_tires(std::int32_t& launch_q16, std::array<std::int32_t, 8>& tires_q16);

struct ControlsInput {
    bool throttle = false, brake = false, left = false, right = false;
    bool gear_up = false, gear_down = false;
    std::optional<AnalogControls> analog = std::nullopt;
};

enum class ControllerHandoffKind : std::uint32_t {
    ai,
    player
};

//Only the scalar fields read/written by402be0/402c80 and their tire helpers.
//Source mode order is steering, throttle, brake, transmission. Engine/filter values,
//live input axes, queued transmission commits and launch scalar are separate.
struct ControllerHandoffState {
    std::uint32_t human = 1, reverse_fallback = 0, countdown = 0;
    std::int32_t player = 0;
    std::uint32_t gear = 1;
    //Raw+14c word window: the request is its low byte; retain+14d..14f.
    std::uint32_t queued_shift = 0;
    std::array<std::int32_t, 4> modes{0, 0, 0, 2}, saved_modes{0, 0, 0, 2};
    std::array<std::int32_t, 8> tires{0, 0, 0, 0, 0xccc, 0, 0, 0};
    bool operator==(const ControllerHandoffState&) const = default;
};

ControllerHandoffState apply_controller_handoff(ControllerHandoffState, ControllerHandoffKind, std::int32_t ai_tire_base_q16);

// Actual upstream wheel/body values. This consumer never invents wheel motion.
// Motion supplies the integrated wheel/body records; isolated consumers may
// inject explicit source values without introducing replacement dynamics.
struct DrivetrainInputs {
    std::array<std::int32_t, 4> wheel_speed_q16{};
    std::int32_t speed_q16 = 0, signed_speed_q16 = 0;
    bool clutch_disengaged = false;
};

//Retained dynamics+14/+38 increments, +120/+134 ramps and+11c/+130 outputs.
//412c90 refreshes increments only for full updates; direct modes retain ramps.
struct PedalFilterState {
    std::int32_t throttle_step_q16, brake_step_q16;
    std::int32_t throttle_ramp_q16, brake_ramp_q16;
    std::int32_t throttle_q16, brake_q16;
    bool operator==(const PedalFilterState&) const = default;
};

//Retained dynamics+44 return increment, +150 keyboard accumulator and signed+274.
//Direct steering retains the accumulator; only full412c90 refreshes+44.
struct SteeringFilterState {
    std::int32_t return_step_q16, accumulator_q16;
    std::int16_t angle;
    bool operator==(const SteeringFilterState&) const = default;
};

struct VehicleState {
    std::int32_t raw_throttle_q16 = 0, raw_brake_q16 = 0;
    std::int32_t raw_left_q16 = 0, raw_right_q16 = 0;
    std::int32_t throttle_q16 = 0, brake_q16 = 0, steering_q16 = 0;
    std::int16_t steering_angle = 0;
    std::int8_t gear = 1, pending_gear = 1; //Source byte330 and byte334.
    bool gear_pending = false, reverse_pedal = false, limiter_active = false;
    // 418cd0 calls the cockpit gesture only when the queued gear commits.
    bool gear_committed = false;
    std::int32_t engine_speed_q16 = 0, engine_torque_q16 = 0, engine_drive_q16 = 0;
    std::array<std::int32_t, 4> wheel_drive_q16{};
    // 460170 -> 402c80 -> 402b90 installs the local player's tire values
    // after 40a770's temporary initialization. Launch/AI may override them.
    std::int32_t launch_tire_adjustment_q16 = 0;
    std::array<std::int32_t, 8> tire_offsets_q16{0, 0, 0, 0, 0xccc, 0, 0, 0};
    std::int32_t rpm_scale_q16 = 0x1ae4;
    //40a770 dynamics+10 retains the car baseline;428440 changes+0c only.
    std::int32_t base_throttle_q16 = 0, maximum_throttle_q16 = 0;
};

// Keyboard mapping/filtering 422f60/418cd0, engine 40d3b0/40d5f0.
// Does not integrate wheel forces, body velocity or scene transforms.
class Controls {
    VehicleState state_;
    std::int32_t maximum_throttle_, front_drive_, maximum_brake_;
    std::int32_t throttle_ramp_ = 0, brake_ramp_ = 0, limiter_load_ = 0;
    std::int32_t throttle_step_ = 0xccc, brake_step_ = 0xccc, steering_return_step_ = 0x3333;
    std::int32_t throttle_base_ = 0xccc, brake_base_ = 0xccc, steering_return_base_ = 0x3333;
    std::uint32_t steering_profile_;
    bool manual_;
    ControllerHandoffState handoff_;
    bool ai_ = false;
    void begin_update_impl(ControlsInput input, const TimeStep& time, const DrivetrainInputs& drivetrain, const DriverAxes* axes, bool mapped = false, bool full_engine = true);
    bool start_held_ = false;
    std::int8_t source_gear_press_ = 0;
    std::uint32_t network_filter_flags_ = 0;
    std::optional<std::array<std::int32_t, 8>> pending_boost_tires_;

public:
    explicit Controls(std::uint32_t car_id, bool manual = false, std::uint32_t steering_profile = 2);
    // 401320 / 401260: entry neutral/open clutch, release automatic gear1.
    // Manual selection remains driver-owned, including shifts before release.
    //The ordinary native session pairs release with448130 launch setup;
    //diagnostic replay invokes those original boundaries independently.
    void set_start_hold(bool held);
    //Source401320/401260 clutch/gear transition independently of448130.
    void set_source_start_hold(bool held);
    //448130 and422f60 are distinct boundaries; neither updates input filters,
    //engine speed or wheel forces. Diagnostic mapped replay calls them once.
    void begin_launch_adjustment();
    void advance_launch_adjustment();
    //402b90 resets local tyre parameters without discarding retained launch.
    void reset_player_tire_offsets();
    bool start_held() const;
    void handoff_to_ai(std::int32_t tire_offset_q16);
    void restore_player_control();
    void restore_qualifying_control();
    ControllerHandoffState controller_handoff_state() const;
    void begin_ai_update(const DriverAxes&, const TimeStep&, const DrivetrainInputs&, bool full_engine = true);
    void begin_update(const ControlsInput& input, const TimeStep& time, const DrivetrainInputs& drivetrain = {});
    // Diagnostic415090 boundary: restore warm state and consume already mapped
    // source controls, without repeating422f60 or its launch-tire decay.
    void restore_source_snapshot(const SourceSnapshot&);
    // Fresh-process actor boundary only: retain this new owner's throttle/raw
    // axes and limiter context while adopting authored controls/handoff fields.
    void restore_cold_source_snapshot(const SourceSnapshot&);
    void publish_source_cold_context(SourceSnapshot&) const;
    void set_source_input(const SourceInput&);
    ControlsInput begin_source_update(const TimeStep&, const DrivetrainInputs&, bool full_engine = true);
    //4448b0 couples the simplified wheel directly, retaining drive/limiter state.
    void finish_distant_update(std::int32_t wheel_speed);
    void finish_update(const DrivetrainInputs& drivetrain);
    void clear_steering_angle();
    RemoteControls network_controls() const;
    void apply_network_controls(const RemoteControls&);
    void apply_network_state(const RemoteMotionState&, bool human = true);
    void apply_network_gear(std::uint8_t gear, bool reverse);
    ControlsInput begin_network_update(const TimeStep&, const DrivetrainInputs&);
    void set_steering_profile(std::uint32_t profile);
    void apply_car_boost(const CarBoostResult&);
    void update(const ControlsInput& input, const TimeStep& time, const DrivetrainInputs& drivetrain = {});
    const VehicleState& state() const;
    //40a770 calibration consumed by initial physical restore. Publication
    //reads current values:428440 may change+0c while baseline+10 survives.
    void publish_source_calibration(SourceSnapshot&) const;
    //4be108[actor] is written only on full-update limiter activation.
    std::int32_t limiter_load_q16() const;
    PedalFilterState pedal_filter_state() const;
    SteeringFilterState steering_filter_state() const;
};
} // namespace toca::simulation
