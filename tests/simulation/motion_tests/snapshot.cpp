#include "snapshot.hpp"

#include <algorithm>

namespace toca::test::motion {
using namespace toca::simulation;

SnapshotFixture::SnapshotFixture() {
    ticks(live, 80, {.throttle = true});
    const auto& state = live.state();
    const auto& controls = live.controls();
    snapshot.roughness_cursor = live_roughness.current();
    const auto vector = [&](std::size_t offset, const auto& values) {
        for (std::size_t i = 0; i < 3; ++i) {
            word(snapshot.body, offset + i * 4, values[i]);
        }
    };
    vector(0xbc, state.body.position);
    vector(0xc8, state.velocity_q16);
    vector(0xe4, state.angular_velocity_q16);
    constexpr fixed::Vector normal{0, unit, 0};
    vector(0xb0, fixed::local(normal, state.body.basis));
    vector(0x1e4, normal);
    word(snapshot.body, 0xe0, state.speed_q16);
    word(snapshot.body, 0x510, 1);
    snapshot.body[0x51f] = 4;
    for (std::size_t i = 0; i < 4; ++i) {
        snapshot.body[0x521 + i] = std::uint8_t(3 - i);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        vector(0x150 + i * 12, state.body.basis[i]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        vector(0xf0 + i * 12, state.body.contacts[i]);
        word(snapshot.body, 0x1c4 + i * 4, state.body.surfaces[i].height);
        const auto triangle = state.body.surfaces[i].triangle;
        snapshot.dynamics[0x264 + i * 2] = std::uint8_t(triangle);
        snapshot.dynamics[0x265 + i * 2] = std::uint8_t(triangle >> 8);
        snapshot.body[0x517 + i] = i < 4 ? 0 : 1;
        word(snapshot.dynamics, 0xdc + i * 4, controls.tire_offsets_q16[i]);
        if (i < 4) {
            word(snapshot.dynamics, 0xfc + i * 4, state.wheel_speed_q16[i]);
            word(snapshot.dynamics, 0x1e4 + i * 4, state.longitudinal_slip_q16[i]);
            word(snapshot.dynamics, 0x1f4 + i * 4, state.lateral_slip_q16[i]);
        }
    }
    word(snapshot.dynamics, 0xc, 0x68f5);
    word(snapshot.dynamics, 0x14, 0xccc);
    word(snapshot.dynamics, 0x18, 0xccc);
    word(snapshot.dynamics, 0x38, 0xccc);
    word(snapshot.dynamics, 0x3c, 0xccc);
    word(snapshot.dynamics, 0x44, 0x3333);
    word(snapshot.dynamics, 0x48, 0x3333);
    word(snapshot.dynamics, 0x1c, unit);
    word(snapshot.dynamics, 0x30, 0x9999);
    word(snapshot.dynamics, 0x11c, controls.throttle_q16);
    word(snapshot.dynamics, 0x120, unit); // Fully attacked original gas ramp.
    word(snapshot.dynamics, 0x124, controls.engine_speed_q16);
    word(snapshot.dynamics, 0x140, unit);
    word(snapshot.dynamics, 0x13c, unit); // Already mapped at415090 entry.
    snapshot.dynamics[0x14c] = 1;
    word(snapshot.dynamics, 0x27c, 1);
    snapshot.dynamics[0x330] = std::uint8_t(controls.gear);
    snapshot.dynamics[0x335] = 3;
    replay_roughness.import_cursor(snapshot.roughness_cursor);
    snapshot.has_damage = true;
    snapshot.damage_levels[7] = 2;
    snapshot.broken_features[8] = 1;
    snapshot.body[0x514] = 3;
    snapshot.body[0x53b] = 0xff;
    snapshot.body[0x4ec] = 0xd2;
    snapshot.body[0x4ed] = 0x04;
}

void SnapshotFixture::damage_feedback() {
    {
        auto retained_snapshot = snapshot;
        word(retained_snapshot.body, 0xb4, 0xf332);
        replay.restore_source_snapshot(retained_snapshot);
        expect(fixed::dot(replay.state().body.ground_normal, replay.state().body.basis[1]) >= 0xf333
                   && select_body_mode(BodyMode::full, {}, false, 4, replay.retained_ground_up_q16()) == BodyMode::full,
            "Body selection must retain source b4 before stabilization, even when the imported current basis is upright");
    }
    replay.restore_source_snapshot(snapshot);
    expect(replay.state().damage.levels[7] == 2 && replay.state().damage.broken_features[2] && replay.state().damage.feedback_side == 3
               && replay.state().damage.feedback_strength == 1234 && !replay.damage_feedback_enabled(),
        "A warm Motion snapshot imports its actor-specific damage variants/features and source feedback fields");
    //Source+53b is independent of ownership.40f5c0 consumes the queued
    //direction only with that slot assigned; other ground modes retain it.
    auto feedback_snapshot = snapshot;
    word(feedback_snapshot.body, 0x4ec, 5000);
    feedback_snapshot.body[0x514] = 5;
    for (const bool human : {false, true}) {
        for (const bool assigned : {false, true}) {
            feedback_snapshot.body[0x53b] = assigned ? 0 : 0xff;
            for (const auto mode : {BodyMode::full, BodyMode::distant, BodyMode::crashed}) {
                SurfaceRoughness feedback_roughness;
                Motion feedback(floor, body, 0, true, 1, false, damage, feedback_roughness);
                feedback.restore_source_snapshot(feedback_snapshot);
                feedback.set_actor_identity(0, human);
                feedback.finish_ground(mode);
                expect(feedback.state().boundary == MotionBoundary::none && feedback.damage().feedback_strength == 5000
                           && feedback.damage().feedback_side == (assigned && mode == BodyMode::full ? 0 : 5),
                    "Full ground consumes only an assigned slot's feedback direction; distant/frozen dispatch and strength stay retained");
                expect(feedback.state().wheel_feedback_side == (assigned && mode == BodyMode::full ? 5 : 0)
                           && feedback.state().wheel_feedback_strength == (assigned && mode == BodyMode::full ? 5000 : 0),
                    "Assigned full-ground feedback must publish the original direction and magnitude before clearing damage state");
                feedback.begin_tick();
                expect(feedback.state().wheel_feedback_side == 0 && feedback.state().wheel_feedback_strength == 0, "A new physical tick cannot replay the previous wheel impulse");
                feedback.add_car_contact({unit / 2, 0, 0}, {});
                feedback.apply_car_damage({0, 0, unit}, feedback.damage_feedback_enabled());
                expect(feedback.damage().feedback_strength == (assigned ? 4096 : 5000) && feedback.damage().feedback_side == (assigned ? 1 : 5),
                    "Real car damage uses the imported feedback assignment independently of human ownership");
            }
        }
    }
}

SourceSnapshot SnapshotFixture::distant_seed() const {
    //412040 can retain eight zero support bytes with an independent count4;
    //444ee0 updates its centre cache and aliased height5, retaining other full-body fields.
    auto distant_snapshot = snapshot;
    word(distant_snapshot.body, 0x4f0, 1);
    distant_snapshot.body[0x51f] = 4;
    std::fill_n(distant_snapshot.body.begin() + 0x517, 8, 0);
    word(distant_snapshot.dynamics, 0x27c, 0);
    word(distant_snapshot.dynamics, 0x310, 1);
    word(distant_snapshot.dynamics, 0x314, 1);
    for (std::size_t i = 0; i < 8; ++i) {
        word(distant_snapshot.body, 0x1c4 + i * 4, 2 * unit + std::int32_t(i * 17));
    }
    return distant_snapshot;
}

void SnapshotFixture::prepare_continuation() {
    replay.restore_source_snapshot(snapshot);
}

void SnapshotFixture::handoff_launch() {
    SurfaceRoughness external_driver_roughness;
    Motion external_driver(floor, body, 0, true, 1, false, damage, external_driver_roughness);
    external_driver.restore_source_snapshot(snapshot);
    const auto before_handoff_position = external_driver.state().body.position;
    const auto before_handoff_rpm = external_driver.controls().engine_speed_q16;
    external_driver.source_handoff_to_ai(-0x1234);
    expect(external_driver.state().body.position == before_handoff_position && external_driver.controls().engine_speed_q16 == before_handoff_rpm
               && std::ranges::all_of(external_driver.controls().tire_offsets_q16,
                   [](auto value) {
                       return value == -0x1234;
                   }),
        "An externally retained AI handoff must use402be0's control consumer while preserving live body/RPM and replacing its eight tire offsets");
    external_driver.update_source_input({.throttle_q16 = unit + 0x1000, .forced_gear = 1}, half_step, true);
    expect(external_driver.state().boundary == MotionBoundary::none && external_driver.controls().raw_throttle_q16 == unit + 0x1000 && external_driver.controls().gear == 1,
        "The same retained physical actor must accept generated AI axes and forced gear after handoff without an internal duplicate AiDriver or snapshot reload");
    auto warm_context = snapshot;
    external_driver.publish_warm_grid_context(warm_context);
    expect(source_i32(warm_context.dynamics, 0x140) == unit + 0x1000 && source_i32(warm_context.dynamics, 0x11c) == external_driver.controls().throttle_q16
               && source_i32(warm_context.dynamics, 0x128) == external_driver.state().previous_engine_speed_q16
               && source_i32(warm_context.dynamics, 0x12c) == external_driver.controls().engine_drive_q16,
        "The live warm-grid context must retain actual mapped axes, filtered throttle and previous engine state after a physical step");
    //The shared initializer restores the local keyboard modes. Its generated
    //controls must not replace the separately grounded native preview body.
    warm_context.dynamics[0x27c] = 1;
    for (const auto offset : {0x310, 0x314, 0x318}) {
        std::fill_n(warm_context.dynamics.begin() + offset, 4, 0);
    }
    Motion preview(floor, body, 0, true, 1, false, damage, external_driver_roughness);
    preview.restore_generated_grid_controls(warm_context);
    expect(preview.state().body.position == body.position && preview.controls().raw_throttle_q16 == unit + 0x1000
               && preview.controls().throttle_q16 == external_driver.controls().throttle_q16 && !preview.ai_controlled(),
        "Generated warm controls must retain inherited pedals without importing the old race body or changing the native preview owner");
    //The explicit local prephase must reach the ordinary force/engine kernel
    //with exactly the same state as its415090 consumed snapshot.
    auto launch_snapshot = snapshot;
    word(launch_snapshot.dynamics, 0x148, 1);
    word(launch_snapshot.dynamics, 0x124, 0x9851e);
    launch_snapshot.launch_tire_adjustment_q16 = 0;
    SurfaceRoughness launch_roughness, consumed_launch_roughness;
    Motion launch_motion(floor, body, 0, true, 1, false, damage, launch_roughness);
    Motion consumed_launch_motion(floor, body, 0, true, 1, false, damage, consumed_launch_roughness);
    launch_motion.restore_source_snapshot(launch_snapshot);
    launch_motion.prepare_source_local_input({.start_held = true, .begin_launch = true});
    launch_motion.prepare_source_local_input({.start_held = true, .advance_launch = true});
    constexpr auto launch_value = -0x147a + 0x73;
    expect(launch_motion.start_held() && launch_motion.controls().engine_speed_q16 == 0x9851e && launch_motion.controls().launch_tire_adjustment_q16 == launch_value,
        "Motion448130/422f60 prephase must derive and decay launch state while retaining RPM and an open clutch");
    launch_snapshot.launch_tire_adjustment_q16 = launch_value;
    for (std::size_t i = 0; i < 8; ++i) {
        word(launch_snapshot.dynamics, 0xdc + i * 4, launch_value);
    }
    consumed_launch_motion.restore_source_snapshot(launch_snapshot);
    const SourceInput launch_input{.throttle_q16 = unit, .right_q16 = unit};
    launch_motion.update_source_input(launch_input, half_step);
    consumed_launch_motion.update_source_input(launch_input, half_step);
    expect(launch_motion.state().boundary == MotionBoundary::none && launch_motion.state().body.position == consumed_launch_motion.state().body.position
               && launch_motion.state().velocity_q16 == consumed_launch_motion.state().velocity_q16
               && launch_motion.controls().engine_speed_q16 == consumed_launch_motion.controls().engine_speed_q16
               && launch_motion.controls().throttle_q16 == consumed_launch_motion.controls().throttle_q16
               && launch_motion.controls().wheel_drive_q16 == consumed_launch_motion.controls().wheel_drive_q16
               && launch_motion.controls().launch_tire_adjustment_q16 == launch_value,
        "Explicit launch events and a warm consumed import must execute identical single physics/filter/engine updates without a second decay");
}

void SnapshotFixture::continued_inputs() {
    live.update({.throttle = true, .right = true, .gear_up = true}, half_step);
    replay.update_source_snapshot(half_step);
    expect(replay.state().body.position == live.state().body.position && replay.state().body.basis == live.state().body.basis
               && replay.state().velocity_q16 == live.state().velocity_q16 && replay.state().wheel_speed_q16 == live.state().wheel_speed_q16
               && replay.controls().engine_speed_q16 == live.controls().engine_speed_q16 && replay.controls().steering_q16 == live.controls().steering_q16
               && replay.controls().gear_pending && replay.controls().gear_pending == live.controls().gear_pending,
        "A warm415090 snapshot must consume its mapped input once through the live controls, force, body and contact chain");
    for (unsigned tick = 0; tick < 120; ++tick) {
        const ControlsInput input{.throttle = tick < 20, .brake = tick >= 30 && tick < 100, .left = tick >= 20 && tick < 30, .right = tick < 20, .gear_down = tick == 40};
        const TimeStep step = tick % 2 ? TimeStep{0x18000, 0xaaaa} : half_step;
        live.update(input, step);
        replay.update_source_input({input.throttle ? unit : 0, input.brake ? unit : 0, input.left ? unit : 0, input.right ? unit : 0, std::int8_t(input.gear_down ? -1 : 0)}, step);
        expect(replay.state().boundary == MotionBoundary::none && replay.state().body.position == live.state().body.position && replay.state().body.basis == live.state().body.basis
                   && replay.state().velocity_q16 == live.state().velocity_q16 && replay.state().angular_velocity_q16 == live.state().angular_velocity_q16
                   && replay.state().wheel_speed_q16 == live.state().wheel_speed_q16 && replay.controls().engine_speed_q16 == live.controls().engine_speed_q16
                   && replay.controls().throttle_q16 == live.controls().throttle_q16 && replay.controls().brake_q16 == live.controls().brake_q16
                   && replay.controls().gear == live.controls().gear && replay.controls().gear_pending == live.controls().gear_pending,
            "Sequential mapped inputs must retain native drivetrain, filters, limiter and contacts without further original-state imports");
    }
}

} // namespace toca::test::motion
