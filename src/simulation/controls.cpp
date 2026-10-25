#include "simulation/controls.hpp"

#include "simulation/fixed.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace toca::simulation {
void publish_source_frame_brake_pedals(std::span<const std::int32_t> raw, std::span<std::int32_t> published) {
    if (raw.size() != published.size() || raw.size() > 16) {
        throw std::runtime_error("Source frame brake publication requires matching actor counts up to16");
    }
    for (auto actor = raw.size(); actor-- > 0;) {
        published[actor] = raw[actor];
    }
}

namespace {
constexpr std::int32_t one = 65536, maximum_engine = 0x9851e;
constexpr std::array<std::int32_t, 8> ratios{0, 0x2bd70, 0x20f5c, 0x1a3d7, 0x14f5c, 0x11eb8, 0x10000, -0x2e147};

std::int32_t bits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

std::int32_t add(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(a) + std::uint32_t(b));
}

std::int32_t sub(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(a) - std::uint32_t(b));
}

std::int32_t mul(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(std::uint64_t(std::int64_t(a) * b) >> 16));
}

std::int32_t absolute(std::int32_t value) {
    return value < 0 ? sub(0, value) : value;
}

std::int32_t sine(std::int32_t angle) {
    return fixed::sine(angle);
}
} // namespace

ControllerHandoffState apply_controller_handoff(ControllerHandoffState state, ControllerHandoffKind kind, std::int32_t ai_tire_base_q16) {
    if (state.human > 1 || state.reverse_fallback > 1 || state.countdown > 255 || state.player < -1 || state.player > 3 || state.gear > 7) {
        throw std::runtime_error("Controller handoff requires original actor/gear/flag bounds");
    }
    if (kind == ControllerHandoffKind::ai) {
        if (state.gear == 7) {
            state.reverse_fallback = 1;
        }
        if (state.human != 0) {
            state.saved_modes = state.modes;
        }
        state.modes = {1, 1, 0, 0};
        state.human = 0;
        state.countdown = 25;
        state.tires.fill(ai_tire_base_q16);
    } else if (kind == ControllerHandoffKind::player) {
        if (state.player != -1) {
            state.human = 1;
            state.modes = state.saved_modes;
            state.queued_shift &= 0xffffff00u; //402ce2 clears only the byte at+14c.
            state.tires = {0, 0, 0, 0, 0xccc, 0, 0, 0};
        }
    } else {
        throw std::runtime_error("Unknown original controller handoff kind");
    }
    return state;
}

Controls::Controls(std::uint32_t car_id, bool manual, std::uint32_t steering_profile) : steering_profile_(steering_profile), manual_(manual) {
    // 40a770: throttle maximum +0c, front drive +1c, brake maximum +30.
    constexpr std::array<std::int32_t, 10> throttle{0x68f5, 0x5eb8, 0x5eb8, 0x6666, 0x5c28, 0x5eb8, 0x5c28, 0x6666, 0x5999, 0x8000};
    constexpr std::array<std::int32_t, 10> brake{0x9999, 0x8ccc, 0xcccc, 0xd999, 0x7333, 0x5999, 0x9999, 0x8000, 0x6666, 0x9999};
    if (car_id >= throttle.size() || steering_profile < 1 || steering_profile > 3) {
        throw std::runtime_error("Local controls require original car 0..9 and keyboard profile 1..3");
    }
    maximum_throttle_ = throttle[car_id];
    state_.base_throttle_q16 = state_.maximum_throttle_q16 = maximum_throttle_;
    maximum_brake_ = brake[car_id];
    front_drive_ = car_id == 1 ? 0xa666 : car_id == 8 ? 0x8000 : car_id == 9 ? 0 : one;
    handoff_.modes[3] = handoff_.saved_modes[3] = manual ? 1 : 2;
}

void Controls::publish_source_calibration(SourceSnapshot& snapshot) const {
    const std::array<std::pair<std::size_t, std::int32_t>, 8> fields{{{0xc, maximum_throttle_}, {0x10, state_.base_throttle_q16}, {0x18, throttle_base_}, {0x1c, front_drive_},
        {0x24, state_.rpm_scale_q16}, {0x30, maximum_brake_}, {0x3c, brake_base_}, {0x48, steering_return_base_}}};
    for (const auto& [offset, value] : fields) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            snapshot.dynamics[offset + byte] = std::uint8_t(std::uint32_t(value) >> (byte * 8));
        }
    }
}

void Controls::set_start_hold(bool held) {
    if (held == start_held_) {
        return;
    }
    set_source_start_hold(held);
    if (!held) {
        begin_launch_adjustment();
    }
}

void Controls::set_source_start_hold(bool held) {
    if (held == start_held_) {
        return;
    }
    start_held_ = held;
    if (held) {
        state_.gear = 0;
    } else if (!manual_ || handoff_.player == -1) {
        //401260 also selects first for actors without a permanent player.
        state_.gear = 1;
    }
}

void Controls::begin_launch_adjustment() {
    state_.launch_tire_adjustment_q16 = source_launch_adjustment(state_.engine_speed_q16, maximum_engine);
}

std::int32_t source_launch_adjustment(std::int32_t speed, std::int32_t maximum) {
    //448130 writes the separate local correction while the clutch may remain
    //held. Keep max/100 truncation before the60/70-percent thresholds.
    if (maximum < 100) {
        throw std::runtime_error("Source launch requires a positive initialized engine maximum");
    }
    const auto percent = maximum / 100;
    if (speed < percent * 60) {
        return 0;
    }
    const auto numerator = speed > percent * 70 ? sub(speed, percent * 70) : sub(speed, percent * 60);
    const auto denominator = speed > percent * 70 ? maximum - percent * 70 : percent * 10;
    const auto ratio = bits(std::uint32_t(std::int64_t(numerator) * one / denominator));
    return mul(ratio, speed > percent * 70 ? -0x147a : 0x147a);
}

void Controls::reset_player_tire_offsets() {
    state_.tire_offsets_q16 = {0, 0, 0, 0, 0xccc, 0, 0, 0};
}

void Controls::advance_launch_adjustment() {
    advance_source_launch_tires(state_.launch_tire_adjustment_q16, state_.tire_offsets_q16);
}

void advance_source_launch_tires(std::int32_t& launch, std::array<std::int32_t, 8>& tires) {
    if (launch == 0) {
        return;
    }
    const bool negative = launch < 0;
    launch = add(launch, negative ? 0x73 : -0x73);
    tires.fill(launch);
    if ((negative && launch >= 0) || (!negative && launch <= 0)) {
        launch = 0;
        tires = {0, 0, 0, 0, 0xccc, 0, 0, 0};
    }
}

bool Controls::start_held() const {
    return start_held_;
}

void Controls::update(const ControlsInput& input, const TimeStep& time, const DrivetrainInputs& drivetrain) {
    begin_update(input, time, drivetrain);
    finish_update(drivetrain);
}

ControllerHandoffState Controls::controller_handoff_state() const {
    auto state = handoff_;
    state.gear = std::uint32_t(state_.gear);
    state.tires = state_.tire_offsets_q16;
    return state;
}

void Controls::handoff_to_ai(std::int32_t tire_offset_q16) {
    handoff_ = apply_controller_handoff(controller_handoff_state(), ControllerHandoffKind::ai, tire_offset_q16);
    ai_ = true;
    manual_ = false;
    state_.tire_offsets_q16 = handoff_.tires;
    //402be0 does not write the separate448130 player launch scalar.
}

void Controls::restore_player_control() {
    handoff_ = apply_controller_handoff(controller_handoff_state(), ControllerHandoffKind::player, 0);
    if (handoff_.player == -1) {
        return;
    }
    ai_ = false;
    manual_ = handoff_.modes[3] == 1;
    //43b1e0: source+310/+314/+318 correspond to bits26/24/25.
    network_filter_flags_ = std::uint32_t(handoff_.modes[0] != 0) << 26 | std::uint32_t(handoff_.modes[1] != 0) << 24 | std::uint32_t(handoff_.modes[2] != 0) << 25;
    source_gear_press_ = 0;
    state_.tire_offsets_q16 = handoff_.tires;
}

void Controls::restore_qualifying_control() {
    restore_player_control();
    reset_qualifying_steering(state_.raw_left_q16, state_.raw_right_q16, state_.steering_angle);
}

void Controls::begin_ai_update(const DriverAxes& axes, const TimeStep& time, const DrivetrainInputs& drivetrain, bool full_engine) {
    if (!ai_) {
        throw std::runtime_error("AI controls require the original ownership handoff");
    }
    begin_update_impl({}, time, drivetrain, &axes, false, full_engine);
}

void Controls::begin_update(const ControlsInput& input, const TimeStep& time, const DrivetrainInputs& drivetrain) {
    begin_update_impl(input, time, drivetrain, nullptr);
}

void Controls::publish_source_cold_context(SourceSnapshot& snapshot) const {
    const std::array<std::pair<std::size_t, std::int32_t>, 5> fields{
        {{0x11c, state_.throttle_q16}, {0x138, state_.raw_left_q16}, {0x13c, state_.raw_right_q16}, {0x140, state_.raw_throttle_q16}, {0x144, state_.raw_brake_q16}}};
    for (const auto& [offset, value] : fields) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            snapshot.dynamics[offset + byte] = std::uint8_t(std::uint32_t(value) >> (byte * 8));
        }
    }
    snapshot.limiter_active = state_.limiter_active;
    snapshot.limiter_load_q16 = limiter_load_;
}

void Controls::restore_cold_source_snapshot(const SourceSnapshot& snapshot) {
    auto generated = snapshot;
    publish_source_cold_context(generated);
    restore_source_snapshot(generated);
}

void Controls::restore_source_snapshot(const SourceSnapshot& snapshot) {
    const auto& raw = snapshot.dynamics;
    const auto mode = source_i32(raw, 0x27c);
    const bool direct_ai = mode == 0 && source_i32(raw, 0x310) == 1 && source_i32(raw, 0x314) == 1 && source_i32(raw, 0x318) == 0;
    const bool keyboard = (mode == 1 || mode == 2) && source_i32(raw, 0x310) == 0 && source_i32(raw, 0x314) == 0 && source_i32(raw, 0x318) == 0;
    if (!direct_ai && !keyboard) {
        throw std::runtime_error("Original snapshot replay requires keyboard1/2 or source AI0 with1/1/0 filters");
    }
    if (snapshot.steering_profile < 1 || snapshot.steering_profile > 3 || raw[0x330] > 7 || raw[0x334] > 7) {
        throw std::runtime_error("Original snapshot has unsupported steering profile or gear state");
    }
    if (snapshot.launch_tire_adjustment_q16 < -0x147a || snapshot.launch_tire_adjustment_q16 > 0x147a) {
        throw std::runtime_error("Original local launch correction exceeds448130 range");
    }
    state_ = {};
    state_.launch_tire_adjustment_q16 = snapshot.launch_tire_adjustment_q16;
    maximum_throttle_ = source_i32(raw, 0xc);
    state_.base_throttle_q16 = source_i32(raw, 0x10);
    state_.maximum_throttle_q16 = maximum_throttle_;
    pending_boost_tires_.reset();
    maximum_brake_ = source_i32(raw, 0x30);
    front_drive_ = source_i32(raw, 0x1c);
    steering_profile_ = snapshot.steering_profile;
    manual_ = mode == 1;
    ai_ = direct_ai;
    network_filter_flags_ = 0;
    handoff_ = {};
    handoff_.human = source_u32(snapshot.gameplay, 8);
    handoff_.modes = {source_i32(raw, 0x310), source_i32(raw, 0x314), source_i32(raw, 0x318), mode};
    handoff_.queued_shift = source_u32(raw, 0x14c);
    handoff_.player = std::bit_cast<std::int8_t>(snapshot.gameplay[0x80]);
    handoff_.reverse_fallback = source_u32(snapshot.gameplay, 0x28);
    handoff_.countdown = snapshot.gameplay[0x83];
    for (std::size_t index = 0; index < handoff_.saved_modes.size(); ++index) {
        handoff_.saved_modes[index] = source_i32(snapshot.gameplay, 0x60 + index * 4);
    }
    start_held_ = source_i32(raw, 0x148) != 0;
    throttle_step_ = source_i32(raw, 0x14);
    throttle_base_ = source_i32(raw, 0x18);
    brake_step_ = source_i32(raw, 0x38);
    brake_base_ = source_i32(raw, 0x3c);
    steering_return_step_ = source_i32(raw, 0x44);
    steering_return_base_ = source_i32(raw, 0x48);
    throttle_ramp_ = source_i32(raw, 0x120);
    brake_ramp_ = source_i32(raw, 0x134);
    limiter_load_ = snapshot.limiter_load_q16;
    state_.raw_throttle_q16 = source_i32(raw, 0x140);
    state_.raw_brake_q16 = source_i32(raw, 0x144);
    state_.raw_left_q16 = source_i32(raw, 0x138);
    state_.raw_right_q16 = source_i32(raw, 0x13c);
    state_.throttle_q16 = source_i32(raw, 0x11c);
    state_.brake_q16 = source_i32(raw, 0x130);
    state_.steering_q16 = source_i32(raw, 0x150);
    state_.steering_angle = std::bit_cast<std::int16_t>(source_u16(raw, 0x274));
    state_.gear = std::int8_t(raw[0x330]);
    state_.pending_gear = std::int8_t(raw[0x334]);
    source_gear_press_ = std::bit_cast<std::int8_t>(raw[0x14c]);
    state_.gear_pending = source_i32(raw, 0x30c) != 0;
    state_.reverse_pedal = source_i32(raw, 0x304) != 0;
    state_.engine_speed_q16 = source_i32(raw, 0x124);
    state_.rpm_scale_q16 = source_i32(raw, 0x24);
    state_.engine_drive_q16 = source_i32(raw, 0x12c);
    state_.limiter_active = snapshot.limiter_active;
    for (std::size_t i = 0; i < 8; ++i) {
        state_.tire_offsets_q16[i] = source_i32(raw, 0xdc + i * 4);
    }
}

ControlsInput Controls::begin_source_update(const TimeStep& time, const DrivetrainInputs& drivetrain, bool full_engine) {
    ControlsInput input{state_.raw_throttle_q16 != 0, state_.raw_brake_q16 != 0, state_.raw_left_q16 != 0, state_.raw_right_q16 != 0};
    input.gear_up = source_gear_press_ == 1;
    input.gear_down = source_gear_press_ == -1;
    const DriverAxes axes{state_.raw_left_q16, state_.raw_right_q16, state_.raw_throttle_q16, state_.raw_brake_q16};
    begin_update_impl(input, time, drivetrain, ai_ ? &axes : nullptr, true, full_engine);
    return {state_.raw_throttle_q16 != 0, state_.raw_brake_q16 != 0, state_.raw_left_q16 != 0, state_.raw_right_q16 != 0};
}

void Controls::set_source_input(const SourceInput& input) {
    for (const auto axis : {input.throttle_q16, input.brake_q16, input.left_q16, input.right_q16}) {
        if (axis < 0 || axis > (ai_ ? 2 * one : one)) {
            throw std::runtime_error("Original input axis exceeds its human/AI source range");
        }
    }
    if (input.gear_edge < -1 || input.gear_edge > 1) {
        throw std::runtime_error("Original local gear edge is outside -1..1");
    }
    if (input.forced_gear < -1 || input.forced_gear > 7 || (!ai_ && input.forced_gear != -1)) {
        throw std::runtime_error("Original forced gear is not an AI control output in0..7");
    }
    if (input.reset_reverse_pedal && !ai_) {
        throw std::runtime_error("Original reverse-pedal reset requires source AI control");
    }
    if (input.ai_tire_offset_q16 && !ai_) {
        throw std::runtime_error("Original AI tire output requires source controller mode0");
    }
    if (input.ai_tire_offset_q16) {
        state_.tire_offsets_q16.fill(*input.ai_tire_offset_q16);
    }
    if (input.forced_gear >= 0) {
        state_.gear = input.forced_gear;
    }
    if (input.reset_reverse_pedal) {
        state_.reverse_pedal = false;
    }
    state_.raw_throttle_q16 = input.throttle_q16;
    state_.raw_brake_q16 = input.brake_q16;
    state_.raw_left_q16 = input.left_q16;
    state_.raw_right_q16 = input.right_q16;
    source_gear_press_ = input.gear_edge;
    handoff_.queued_shift = (handoff_.queued_shift & 0xffffff00u) | std::uint8_t(input.gear_edge);
}

void Controls::begin_update_impl(ControlsInput input, const TimeStep& time, const DrivetrainInputs& drivetrain, const DriverAxes* axes, bool mapped, bool full_engine) {
    if (time.delta_q16 <= 0 || time.delta_q16 > 0x30000 || drivetrain.speed_q16 < 0 || drivetrain.speed_q16 > 1000 * one || absolute(drivetrain.signed_speed_q16) < 0
        || absolute(drivetrain.signed_speed_q16) > 1000 * one) {
        throw std::runtime_error("Local controls need a positive bounded source step and bounded actual body speed");
    }
    const auto* analog = !axes && input.analog ? &*input.analog : nullptr;
    if (!mapped) {
        network_filter_flags_ =
            analog ? (std::uint32_t(analog->throttle_analog) << 24 | std::uint32_t(analog->brake_analog) << 25 | std::uint32_t(analog->steering_analog) << 26) : 0;
        handoff_.modes = {ai_ ? 1 : std::int32_t((network_filter_flags_ >> 26) & 1), ai_ ? 1 : std::int32_t((network_filter_flags_ >> 24) & 1),
            ai_ ? 0 : std::int32_t((network_filter_flags_ >> 25) & 1),
            ai_       ? 0
            : manual_ ? 1
                      : 2};
    }
    if (analog) {
        if (analog->profile < 1 || analog->profile > 3 || analog->steering_q16 < -one || analog->steering_q16 > one || analog->throttle_q16 < 0 || analog->throttle_q16 > one
            || analog->brake_q16 < 0 || analog->brake_q16 > one) {
            throw std::runtime_error("Local analog controls require bounded Q16 axes and sensitivity1..3");
        }
        steering_profile_ = analog->profile;
    }
    if (full_engine) {
        //412c90 precedes the full body batch, but444830 skips it and
        //retains these per-actor increments from initialization/last full pass.
        throttle_step_ = mul(throttle_base_, time.delta_q16);
        brake_step_ = mul(brake_base_, time.delta_q16);
        steering_return_step_ = mul(steering_return_base_, time.delta_q16);
    }
    state_.gear_committed = false;
    // 422f60: consume the local launch tire correction before new controls.
    // At zero402b90 resets all eight fields, with the source's +ec exception.
    if (!mapped && !ai_) {
        advance_launch_adjustment();
    }
    //Native controls consume422f60 inside the body update, whereas428440 ran
    //after that source prephase. Its catch-up tire writes must win over launch.
    if (pending_boost_tires_) {
        state_.tire_offsets_q16 = *pending_boost_tires_;
        pending_boost_tires_.reset();
    }
    // 422f60: pedal fields are independent. With normal projection flags,
    // Left (+138) wins over Right (+13c). Config +24/+28 are mirror flags,
    // not screen dimensions. 40b360 clears steering input at rest.
    if (!mapped) {
        state_.raw_throttle_q16 = input.throttle ? one : 0;
        state_.raw_brake_q16 = input.brake ? one : 0;
        state_.raw_left_q16 = input.left ? one : 0;
        state_.raw_right_q16 = input.right && !input.left ? one : 0;
    }
    if (analog) {
        if (analog->throttle_analog) {
            state_.raw_throttle_q16 = analog->throttle_q16;
            input.throttle = state_.raw_throttle_q16 != 0;
        }
        if (analog->brake_analog) {
            state_.raw_brake_q16 = analog->brake_q16;
            input.brake = state_.raw_brake_q16 != 0;
        }
        if (analog->steering_analog) {
            // 422f60's HUMAN analog branch deliberately uses negative raw
            // direction fields. AI's positive raw axes have a different path.
            state_.raw_left_q16 = analog->steering_q16 > 0 ? -analog->steering_q16 : 0;
            state_.raw_right_q16 = analog->steering_q16 < 0 ? analog->steering_q16 : 0;
        }
    }
    const bool moving = !full_engine || drivetrain.speed_q16 >= 0xccc;
    if (!moving) {
        state_.raw_left_q16 = 0;
        state_.raw_right_q16 = 0;
    }

    if (axes) {
        state_.raw_throttle_q16 = axes->throttle_q16;
        state_.raw_brake_q16 = axes->brake_q16;
        state_.raw_left_q16 = moving ? axes->left_q16 : 0;
        state_.raw_right_q16 = moving ? axes->right_q16 : 0;
        input.throttle = axes->throttle_q16 != 0;
        input.brake = axes->brake_q16 != 0;
        if (axes->forced_gear >= 0) {
            state_.gear = axes->forced_gear;
        }
        if (axes->reset_reverse_pedal) {
            state_.reverse_pedal = false;
        }
    }

    //403dd0 stores330/304 before418cd0 and leaves334/30c untouched.
    // 418cd0/418f60/418fd0. A pending change commits next update; edges during
    // that commit are not processed again. Down wins simultaneous gear edges.
    if (state_.gear_pending) {
        state_.gear_pending = false;
        state_.gear = state_.pending_gear;
        state_.gear_committed = true;
    } else if (manual_) {
        const auto edge = input.gear_down ? -1 : input.gear_up ? 1 : 0;
        if (edge > 0 && (state_.gear < 6 || state_.gear == 7)) {
            state_.pending_gear = state_.gear == 7 ? 1 : std::int8_t(state_.gear + 1);
            state_.gear_pending = true;
        } else if (edge < 0 && state_.gear < 7) {
            state_.pending_gear = state_.gear <= 1 ? 7 : std::int8_t(state_.gear - 1);
            state_.gear_pending = true;
        }
    } else {
        constexpr std::array<std::int32_t, 5> up{0xfae1, 0xfae1, 0xfae1, 0xfae1, 0xfae1};
        constexpr std::array<std::int32_t, 5> down{0x8000, 0xa8f5, 0xa8f5, 0xa8f5, 0xa8f5};
        if (state_.gear >= 1 && state_.gear <= 5 && state_.engine_speed_q16 >= mul(up[std::size_t(state_.gear - 1)], maximum_engine)) {
            state_.pending_gear = std::int8_t(state_.gear + 1);
            state_.gear_pending = true;
        } else if (state_.gear >= 2 && state_.gear <= 6 && state_.engine_speed_q16 <= mul(down[std::size_t(state_.gear - 2)], maximum_engine)) {
            state_.pending_gear = std::int8_t(state_.gear - 1);
            state_.gear_pending = true;
        }
        if (ai_ && (input.gear_up || input.gear_down) && (state_.gear == 1 || state_.gear == 7)) {
            state_.pending_gear = state_.gear == 7 ? 1 : 7;
            state_.gear_pending = true;
        }
        if (!ai_ && input.brake && state_.gear == 1 && drivetrain.speed_q16 < 0xa3d) {
            state_.pending_gear = 7;
            state_.gear_pending = true;
            state_.reverse_pedal = true;
        } else if (!ai_ && input.throttle && state_.gear == 7 && !input.brake) {
            state_.pending_gear = 1;
            state_.gear_pending = true;
            state_.reverse_pedal = false;
        }
    }

    // 419630/4197a0 and 412c90: throttle ramp release is three times attack;
    // brake release resets immediately. Reverse maps brake to throttle.
    if (axes || (analog && analog->throttle_analog)) {
        state_.throttle_q16 = mul(maximum_throttle_, state_.raw_throttle_q16);
    } else {
        const auto throttle_increment = throttle_step_;
        if (!input.throttle && (!state_.reverse_pedal || !input.brake)) {
            throttle_ramp_ = std::max(0, sub(throttle_ramp_, mul(throttle_increment, 0x30000)));
        } else {
            throttle_ramp_ = std::min(one, add(throttle_ramp_, throttle_increment));
        }
        state_.throttle_q16 = mul(maximum_throttle_, sine(mul(throttle_ramp_, 0x5a0000) / 0x1680));
    }
    if (analog && analog->brake_analog) {
        // 418cd0:+318 direct pedal, including automatic brake-as-reverse-gas.
        state_.brake_q16 = 0;
        if (state_.raw_brake_q16 != 0) {
            if (state_.reverse_pedal) {
                state_.throttle_q16 = mul(maximum_throttle_, state_.raw_brake_q16);
            } else {
                state_.brake_q16 = mul(maximum_brake_, state_.raw_brake_q16);
            }
        }
    } else if (input.brake && !state_.reverse_pedal) {
        // 40a770 initializes+3c to0xccc;412c90 scales it into+38.
        brake_ramp_ = std::min(one, add(brake_ramp_, brake_step_));
        state_.brake_q16 = mul(maximum_brake_, sine(mul(brake_ramp_, 0x5a0000) / 0x1680));
    } else {
        brake_ramp_ = 0;
        state_.brake_q16 = 0;
    }

    if (axes) {
        // 418cd0 dispatches4190f0 instead of419230 for AI direct steering.
        // The keyboard accumulator+150 remains untouched across ownership.
        state_.steering_angle = std::int16_t(mul(0x18e, sub(state_.raw_right_q16, state_.raw_left_q16)));
    } else if (analog && analog->steering_analog) {
        // 4190f0 local-player branch: sensitivity, speed reduction and the
        // source90-angle-unit slew. It does not advance keyboard accumulator+150.
        constexpr std::array<std::int32_t, 3> gain{0x10000, 0x18000, 0x20000};
        const auto value = std::clamp(mul(sub(state_.raw_right_q16, state_.raw_left_q16), gain[steering_profile_ - 1]), -one, one);
        constexpr auto reciprocal = std::int32_t(INT64_C(0x100000000) / 0x1b333);
        const auto reduction = std::min(mul(absolute(drivetrain.signed_speed_q16), reciprocal), 0xb333);
        const auto scale = bits(std::uint32_t(mul(sub(one, reduction), 0x18e)) * 0x1680);
        const auto target = std::int16_t(mul(scale, value) / 0x1680);
        auto difference = std::int16_t(state_.steering_angle - target);
        if (difference > 0x800) {
            difference = std::int16_t(0x1000 - difference);
        }
        state_.steering_angle = std::int16_t(state_.steering_angle - std::clamp(std::int32_t(difference), -90, 90));
    } else {
        // 419230/4193b0/419550, normal local player's configured flags 3.
        constexpr std::array<std::int32_t, 3> steering_increment{0x1333, 0x170a, 0x1ae1};
        constexpr std::array<std::int32_t, 3> minimum{0x3333, 0x4ccc, 0x7333};
        auto increment = (state_.raw_left_q16 || state_.raw_right_q16) ? mul(steering_increment[steering_profile_ - 1], time.delta_q16) : 0;
        const auto remaining = sub(absolute(state_.steering_q16), one);
        increment = mul(increment, mul(remaining, remaining));
        if (state_.raw_right_q16) {
            increment = sub(0, increment);
        }
        constexpr auto speed_reciprocal = std::int32_t(INT64_C(0x100000000) / 0x1b333);
        const auto reduction = std::min(mul(absolute(drivetrain.signed_speed_q16), speed_reciprocal), 0xb333);
        const auto angle_scale = bits(std::uint32_t(mul(sub(one, reduction), 0x18e)) * 0x1680);
        // 412c90 writes+44 before419230 consumes its signed return correction.
        const auto return_step = steering_return_step_;
        const auto return_bias = mul(angle_scale, state_.steering_q16) >= 0 ? sub(0, return_step) : return_step;
        const bool first = state_.raw_left_q16 != 0, second = state_.raw_right_q16 != 0;
        if ((!first && !second) || (first && return_bias > 0) || (second && return_bias < 0)) {
            const auto correction = add(return_bias, increment);
            state_.steering_q16 = absolute(correction) < absolute(state_.steering_q16) ? add(state_.steering_q16, correction) : 0;
        }
        if (second) {
            state_.steering_q16 = std::clamp(add(state_.steering_q16, increment), -one, -minimum[steering_profile_ - 1]);
        } else if (first) {
            state_.steering_q16 = std::clamp(add(state_.steering_q16, increment), minimum[steering_profile_ - 1], one);
        }
        state_.steering_angle = std::int16_t(mul(angle_scale, state_.steering_q16) / -0x1680);
    }

    if (!full_engine) {
        return; //444830 calls418cd0 without40d3b0.
    }

    // 40d3b0: engine torque and distribution, before the separate wheel-force
    // consumer 40db10. These are drive moments, not integrated wheel velocities.
    state_.engine_drive_q16 = 0;
    const auto drag = mul(0x83, mul(state_.engine_speed_q16, state_.engine_speed_q16));
    if (state_.limiter_active) {
        state_.engine_torque_q16 = sub(0, limiter_load_);
    } else if (state_.engine_speed_q16 < maximum_engine) {
        if (state_.gear == 0 || !state_.gear_pending) {
            state_.engine_torque_q16 = sub(state_.throttle_q16, drag);
            state_.engine_drive_q16 = state_.throttle_q16;
        } else {
            state_.engine_torque_q16 = drag;
        }
    } else if (state_.gear == 0 || drivetrain.clutch_disengaged || start_held_) {
        state_.engine_torque_q16 = input.throttle ? 0 : sub(0, drag);
        state_.engine_speed_q16 = maximum_engine;
    } else {
        state_.engine_torque_q16 = 0;
    }
    const auto ratio = ratios[std::size_t(state_.gear)];
    if (state_.gear != 0 && !state_.gear_pending && !drivetrain.clutch_disengaged && !start_held_) {
        const auto drive = mul(state_.engine_torque_q16, ratio);
        const auto front = mul(drive, front_drive_) / 2;
        const auto rear = sub(drive / 2, front);
        state_.wheel_drive_q16 = {front, front, rear, rear};
    } else {
        state_.wheel_drive_q16 = {};
    }
}

void Controls::finish_distant_update(std::int32_t wheel_speed) {
    //4448b0 clamps the retained engine before coupling and does not execute
    //40d5f0's disconnected-engine integration or limiter reset.
    state_.engine_speed_q16 = std::clamp(state_.engine_speed_q16, 0, maximum_engine);
    if (!state_.gear_pending) {
        state_.engine_speed_q16 = mul(wheel_speed, ratios[std::size_t(state_.gear)]);
    }
}

void Controls::clear_steering_angle() {
    state_.steering_angle = 0;
}

void Controls::finish_update(const DrivetrainInputs& drivetrain) {
    const auto ratio = ratios[std::size_t(state_.gear)];
    // 40d5f0: actual new wheel velocities must be supplied after their own
    // consumers. A disconnected engine follows the original 5*torque branch.
    if (state_.gear == 0 || state_.gear_pending || drivetrain.clutch_disengaged || start_held_) {
        state_.engine_speed_q16 = add(state_.engine_speed_q16, mul(state_.engine_torque_q16, 0x50000));
    } else {
        const auto front = add(mul(drivetrain.wheel_speed_q16[0], ratio), mul(drivetrain.wheel_speed_q16[1], ratio)) >> 1;
        const auto rear = add(mul(drivetrain.wheel_speed_q16[2], ratio), mul(drivetrain.wheel_speed_q16[3], ratio)) >> 1;
        state_.engine_speed_q16 = add(mul(sub(front, rear), front_drive_), rear);
    }
    //40d5f0 clears4be150 on every non-activation branch;4be108 retains
    //its previous value unless the overspeed below activates the limiter.
    state_.limiter_active = false;
    if (state_.engine_speed_q16 < 0) {
        state_.engine_speed_q16 = 0;
    } else if (state_.engine_speed_q16 > maximum_engine) {
        const auto excess = sub(state_.engine_speed_q16, maximum_engine);
        if (excess > 0xcccc) {
            limiter_load_ = mul(std::min(sub(excess, 0xcccc), one), 0x51eb);
            state_.limiter_active = true;
        }
        state_.engine_speed_q16 = maximum_engine;
    }
}

const VehicleState& Controls::state() const {
    return state_;
}

std::int32_t Controls::limiter_load_q16() const {
    return limiter_load_;
}

PedalFilterState Controls::pedal_filter_state() const {
    return {throttle_step_, brake_step_, throttle_ramp_, brake_ramp_, state_.throttle_q16, state_.brake_q16};
}

SteeringFilterState Controls::steering_filter_state() const {
    return {steering_return_step_, state_.steering_q16, state_.steering_angle};
}

RemoteControls Controls::network_controls() const {
    //43b2d0's magnitude-1 fields retain16 bits, including AI output above1.
    const auto magnitude = [](std::int32_t value) {
        if (value == 0) {
            return std::int32_t(0);
        }
        const auto absolute = value < 0 ? -std::int64_t(value) : std::int64_t(value);
        const auto packed = std::int32_t((std::uint64_t(absolute - 1) & 0xffff) + 1);
        return value < 0 ? -packed : packed;
    };
    const auto left = magnitude(state_.raw_left_q16);
    const auto filters = ai_ ? (1U << 24 | 1U << 26) : network_filter_flags_;
    return {left, left != 0 ? 0 : magnitude(state_.raw_right_q16), std::uint32_t(magnitude(state_.raw_throttle_q16)), std::uint32_t(magnitude(state_.raw_brake_q16)),
        filters | (std::uint32_t(start_held_) << 20)};
}

void Controls::apply_network_controls(const RemoteControls& input) {
    validate_remote_controls(input);
    state_.raw_left_q16 = input.left_q16;
    state_.raw_right_q16 = input.right_q16;
    state_.raw_throttle_q16 = std::int32_t(input.throttle_q16);
    state_.raw_brake_q16 = std::int32_t(input.brake_q16);
    start_held_ = (input.source_flags & (1U << 20)) != 0;
    network_filter_flags_ = input.source_flags & 0x07000000U;
    if (!ai_) {
        handoff_.modes = {
            std::int32_t((network_filter_flags_ >> 26) & 1), std::int32_t((network_filter_flags_ >> 24) & 1), std::int32_t((network_filter_flags_ >> 25) & 1), manual_ ? 1 : 2};
    }
}

void Controls::apply_network_state(const RemoteMotionState& input, bool human) {
    validate_remote_motion(input);
    if (human) {
        apply_network_controls(input.controls);
    }
    state_.engine_speed_q16 = std::int32_t(input.engine_speed_q16);
    state_.throttle_q16 = input.throttle_q16.value_or(0);
    state_.steering_angle = std::int16_t(input.steering_angle);
}

void Controls::apply_network_gear(std::uint8_t gear, bool reverse) {
    if (gear > 7) {
        throw std::runtime_error("Network gear exceeds original0..7 range");
    }
    state_.gear = std::int8_t(gear);
    state_.reverse_pedal = reverse;
}

ControlsInput Controls::begin_network_update(const TimeStep& time, const DrivetrainInputs& drivetrain) {
    ControlsInput input{state_.raw_throttle_q16 != 0, state_.raw_brake_q16 != 0, state_.raw_left_q16 != 0, state_.raw_right_q16 != 0};
    if (network_filter_flags_ != 0) {
        AnalogControls analog;
        analog.profile = steering_profile_;
        analog.steering_analog = (network_filter_flags_ & (1U << 26)) != 0;
        analog.throttle_analog = (network_filter_flags_ & (1U << 24)) != 0;
        analog.brake_analog = (network_filter_flags_ & (1U << 25)) != 0;
        analog.steering_q16 = state_.raw_left_q16 != 0 ? -state_.raw_left_q16 : state_.raw_right_q16;
        analog.throttle_q16 = state_.raw_throttle_q16;
        analog.brake_q16 = state_.raw_brake_q16;
        input.analog = analog;
    }
    begin_update_impl(input, time, drivetrain, nullptr, true);
    return input;
}

void Controls::set_steering_profile(std::uint32_t profile) {
    if (profile < 1 || profile > 3) {
        throw std::runtime_error("Network steering profile exceeds original1..3 range");
    }
    steering_profile_ = profile;
}

void Controls::apply_car_boost(const CarBoostResult& parameters) {
    maximum_throttle_ = parameters.state.maximum_throttle_q16;
    state_.maximum_throttle_q16 = maximum_throttle_;
    state_.tire_offsets_q16 = parameters.tire_offsets_q16;
    pending_boost_tires_ = parameters.tire_offsets_q16;
}

} // namespace toca::simulation
