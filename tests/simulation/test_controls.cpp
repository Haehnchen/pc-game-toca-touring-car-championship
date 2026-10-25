#include "simulation/controls.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca::simulation;
        const TimeStep time;
        Controls calibration(1, true, 3);
        SourceSnapshot published_calibration;
        published_calibration.dynamics.fill(0x5a);
        calibration.publish_source_calibration(published_calibration);
        const std::array<std::pair<std::size_t, std::int32_t>, 8> expected_calibration{
            {{0xc, 0x5eb8}, {0x10, 0x5eb8}, {0x18, 0xccc}, {0x1c, 0xa666}, {0x24, 0x1ae4}, {0x30, 0x8ccc}, {0x3c, 0xccc}, {0x48, 0x3333}}};
        for (const auto& [offset, value] : expected_calibration) {
            expect(source_i32(published_calibration.dynamics, offset) == value, "40a770 calibration publication must expose the constructed car controls");
        }
        expect(published_calibration.dynamics[0x20] == 0x5a && published_calibration.dynamics[0x14] == 0x5a,
            "Calibration publication must preserve unrelated configuration and per-tick filter output");
        CarBoostResult boosted_calibration;
        boosted_calibration.state.maximum_throttle_q16 = 0x7000;
        calibration.apply_car_boost(boosted_calibration);
        calibration.publish_source_calibration(published_calibration);
        expect(source_i32(published_calibration.dynamics, 0xc) == 0x7000 && source_i32(published_calibration.dynamics, 0x10) == 0x5eb8,
            "428440 changes current throttle calibration while preserving its40a770 baseline");
        ControllerHandoffState original;
        original.player = 2;
        original.gear = 7;
        original.queued_shift = 0xaabbccdd;
        original.modes = {1, 0, 1, 2};
        original.saved_modes = {0, 1, 0, 1};
        original.tires.fill(-1234);
        const auto ai_handoff = apply_controller_handoff(original, ControllerHandoffKind::ai, 0x1234);
        expect(ai_handoff.human == 0 && ai_handoff.reverse_fallback == 1 && ai_handoff.countdown == 25 && ai_handoff.modes == std::array<std::int32_t, 4>{1, 1, 0, 0}
                   && ai_handoff.saved_modes == original.modes && ai_handoff.gear == original.gear && ai_handoff.queued_shift == original.queued_shift
                   && std::all_of(ai_handoff.tires.begin(), ai_handoff.tires.end(),
                       [](auto value) {
                           return value == 0x1234;
                       }),
            "402be0 must save all four human modes, set reverse only when needed and assign the uniform AI tires without consuming queued input");
        auto repeated_input = ai_handoff;
        repeated_input.gear = 1;
        repeated_input.countdown = 3;
        repeated_input.tires.fill(-55);
        const auto repeated = apply_controller_handoff(repeated_input, ControllerHandoffKind::ai, 0x2345);
        expect(repeated.saved_modes == original.modes && repeated.reverse_fallback == 1 && repeated.countdown == 25 && repeated.tires[0] == 0x2345,
            "Repeated402be0 must refresh its counter/tires without replacing the saved human modes or clearing an existing reverse fallback");
        const auto player_handoff = apply_controller_handoff(repeated, ControllerHandoffKind::player, 0);
        expect(player_handoff.human == 1 && player_handoff.modes == original.modes && player_handoff.queued_shift == 0xaabbcc00 && player_handoff.gear == repeated.gear
                   && player_handoff.reverse_fallback == 1 && player_handoff.countdown == 25 && player_handoff.tires == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
            "402c80 must restore saved modes and tire defaults, clear only the shift-request byte and retain adjacent bytes and unrelated flags");
        auto nonplayer = repeated;
        nonplayer.player = -1;
        expect(
            apply_controller_handoff(nonplayer, ControllerHandoffKind::player, 0) == nonplayer, "402c80 must be a complete no-op for an actor without permanent player ownership");
        //One analog channel at a time distinguishes source STEERING/GAS/BRAKE
        //order from the packet's GAS/BRAKE/STEERING flag bit order.
        constexpr std::array<unsigned, 3> source_mode_bits{26, 24, 25};
        for (std::size_t channel = 0; channel < source_mode_bits.size(); ++channel) {
            toca::AnalogControls analog;
            analog.steering_analog = channel == 0;
            analog.throttle_analog = channel == 1;
            analog.brake_analog = channel == 2;
            const std::array<std::int32_t, 4> expected{channel == 0 ? 1 : 0, channel == 1 ? 1 : 0, channel == 2 ? 1 : 0, 1};
            const auto flag = std::uint32_t(1) << source_mode_bits[channel];
            Controls mixed(0, true);
            mixed.update({.analog = analog}, time);
            expect(mixed.controller_handoff_state().modes == expected && (mixed.network_controls().source_flags & 0x07000000u) == flag,
                "Native mixed analog controls must expose raw modes in source steering/gas/brake order and retain the corresponding distinct network flag");
            mixed.handoff_to_ai(0x1234);
            expect(mixed.controller_handoff_state().saved_modes == expected && (mixed.network_controls().source_flags & 0x07000000u) == ((1u << 26) | (1u << 24)),
                "AI handoff must save the asymmetric human source modes and publish direct steering/gas with filtered brake");
            mixed.restore_player_control();
            expect(mixed.controller_handoff_state().modes == expected && (mixed.network_controls().source_flags & 0x07000000u) == flag,
                "Player handback must restore the original analog channel in both raw source mode order and packet flag order");
            Controls remote(0);
            RemoteControls packet;
            packet.source_flags = flag;
            remote.apply_network_controls(packet);
            auto automatic_expected = expected;
            automatic_expected[3] = 2;
            expect(remote.controller_handoff_state().modes == automatic_expected, "Incoming network mode flags must map independently to source310 steering,314 gas and318 brake");
        }
        Controls source_inputs(7, true);
        source_inputs.update({.throttle = true}, time);
        const auto retained_throttle = source_inputs.state().throttle_q16;
        source_inputs.set_source_input({0x8000, 0x4000, 0, 0x10000, -1});
        expect(source_inputs.state().raw_throttle_q16 == 0x8000 && source_inputs.state().raw_brake_q16 == 0x4000 && source_inputs.state().raw_right_q16 == 0x10000
                   && source_inputs.state().throttle_q16 == retained_throttle,
            "Sequential source inputs must preserve filtered state and retain recorded fractional axes");
        for (const auto invalid : {SourceInput{-1}, SourceInput{0x10001}, SourceInput{0, -1}, SourceInput{0, 0, -1}, SourceInput{0, 0, 0, -1}, SourceInput{0, 0, 0, 0, 2}}) {
            bool rejected = false;
            try {
                source_inputs.set_source_input(invalid);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            expect(rejected && source_inputs.state().raw_throttle_q16 == 0x8000 && source_inputs.state().throttle_q16 == retained_throttle,
                "An invalid source axis or gear edge must be rejected before mutating native input or filtered state");
        }
        SourceSnapshot ai_snapshot;
        const auto ai_word = [&](std::size_t offset, std::int32_t value) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                ai_snapshot.dynamics.at(offset + byte) = std::uint8_t(std::uint32_t(value) >> (byte * 8));
            }
        };
        ai_word(0xc, 0x68f5);
        ai_word(0x1c, 65536);
        ai_word(0x24, 0x1ae4);
        ai_word(0x30, 0x9999);
        ai_word(0x18, 0xccc);
        ai_word(0x3c, 0xccc);
        ai_word(0x48, 0x3333);
        ai_word(0x38, 0xccc);
        ai_word(0x310, 1);
        ai_word(0x314, 1);
        ai_word(0x150, 0x1234);
        ai_snapshot.dynamics[0x330] = ai_snapshot.dynamics[0x334] = 1;
        Controls imported_ai(0);
        imported_ai.restore_source_snapshot(ai_snapshot);
        imported_ai.set_source_input({0x8000, 0, 0x8000, 0, 0});
        imported_ai.begin_source_update(time, {{65536, 65536, 65536, 65536}, 65536, 65536});
        expect(imported_ai.state().throttle_q16 == 0x347a && imported_ai.state().steering_angle == -0xc7 && imported_ai.state().steering_q16 == 0x1234,
            "A captured AI0 with1/1/0 filters must consume fractional gas/direct steering without advancing human ramps or keyboard steering");
        imported_ai.set_source_input({0, 65536});
        imported_ai.begin_source_update(time, {});
        expect(!imported_ai.state().reverse_pedal && !imported_ai.state().gear_pending && imported_ai.state().gear == 1,
            "Captured AI braking must retain automatic mode0 without inventing the human pedal-to-reverse convention");
        imported_ai.set_source_input({.forced_gear = 7});
        expect(imported_ai.state().gear == 7 && !imported_ai.state().gear_pending, "Observed AI reverse recovery is a direct forced gear, distinct from a human gear edge");
        //Synthetic source-state combinations exercise403dd0 ->418cd0 store
        //order; the captured fleet intervals do not cover AI recovery itself.
        ai_word(0x30c, 1);
        ai_word(0x304, 1);
        ai_snapshot.dynamics[0x334] = 2;
        imported_ai.restore_source_snapshot(ai_snapshot);
        imported_ai.set_source_input({.brake_q16 = 65536, .forced_gear = 7});
        expect(imported_ai.state().gear == 7 && imported_ai.state().pending_gear == 2 && imported_ai.state().gear_pending && imported_ai.state().reverse_pedal,
            "403dd0 reverse entry writes330 only and must preserve pending334/30c and the reverse pedal304");
        imported_ai.begin_source_update(time, {});
        expect(imported_ai.state().gear == 2 && imported_ai.state().pending_gear == 2 && !imported_ai.state().gear_pending && imported_ai.state().gear_committed
                   && imported_ai.state().reverse_pedal && imported_ai.state().brake_q16 == 0,
            "418cd0 must commit the earlier pending gear after the AI recovery store, retaining304 for its pedal consumer");
        ai_snapshot.dynamics[0x330] = ai_snapshot.dynamics[0x334] = 7;
        imported_ai.restore_source_snapshot(ai_snapshot);
        imported_ai.set_source_input({.brake_q16 = 65536, .forced_gear = 1, .reset_reverse_pedal = true});
        expect(imported_ai.state().gear == 1 && imported_ai.state().pending_gear == 7 && imported_ai.state().gear_pending && !imported_ai.state().reverse_pedal,
            "403dd0 reverse exit clears304 without canceling a pending reverse gear commit");
        imported_ai.begin_source_update(time, {}, false);
        expect(imported_ai.state().gear == 7 && imported_ai.state().gear_committed && !imported_ai.state().reverse_pedal && imported_ai.state().brake_q16 > 0,
            "Distant418cd0 must commit pending334 after recovery exit and consume cleared304 in the same brake pass");
        imported_ai.restore_source_snapshot(ai_snapshot);
        imported_ai.begin_ai_update({.brake_q16 = 65536, .forced_gear = 1, .reset_reverse_pedal = true}, time, {});
        expect(imported_ai.state().gear == 7 && imported_ai.state().pending_gear == 7 && imported_ai.state().gear_committed && !imported_ai.state().reverse_pedal
                   && imported_ai.state().brake_q16 > 0,
            "Ordinary AI controls must share the mapped source recovery/pending-commit and reverse-pedal ordering");
        ai_word(0x30c, 0);
        ai_word(0x304, 1);
        ai_snapshot.dynamics[0x330] = ai_snapshot.dynamics[0x334] = 1;
        imported_ai.restore_source_snapshot(ai_snapshot);
        imported_ai.set_source_input({.gear_edge = -1});
        imported_ai.begin_source_update(time, {});
        expect(imported_ai.state().gear == 1 && imported_ai.state().pending_gear == 7 && imported_ai.state().gear_pending && imported_ai.state().reverse_pedal,
            "418e70 AI mode0 accepts either retained shift edge as a first/reverse toggle without writing304");
        ai_word(0x304, 0);
        //444830 calls only418cd0; neutral AI does not run40d3b0/40d5f0.
        ai_snapshot.dynamics[0x330] = ai_snapshot.dynamics[0x334] = 0;
        ai_word(0x12c, 1234);
        Controls distant_controls(0);
        distant_controls.restore_source_snapshot(ai_snapshot);
        distant_controls.set_source_input({.throttle_q16 = 65536});
        distant_controls.begin_source_update(time, {}, false);
        distant_controls.finish_distant_update(0);
        expect(distant_controls.state().throttle_q16 == 0x68f5 && distant_controls.state().engine_speed_q16 == 0 && distant_controls.state().engine_drive_q16 == 1234,
            "444830/4448b0 must filter AI gas and couple neutral RPM without the full-body torque/drive or disconnected-engine consumers");
        {
            const auto saved_snapshot = ai_snapshot;
            ai_word(0x14, 0x222);
            ai_word(0x38, 0x333);
            ai_word(0x120, 0x2345);
            ai_word(0x134, 0x3456);
            ai_word(0x11c, 0x4567);
            ai_word(0x130, 0x5678);
            Controls retained_filters(0);
            retained_filters.restore_source_snapshot(ai_snapshot);
            expect(retained_filters.pedal_filter_state() == PedalFilterState{0x222, 0x333, 0x2345, 0x3456, 0x4567, 0x5678},
                "A warm filter snapshot must expose the six retained source words without advancing them");
            retained_filters.set_source_input({.throttle_q16 = 65536, .brake_q16 = 65536});
            retained_filters.begin_source_update({0x20000, 0x8000}, {}, false);
            const auto distant = retained_filters.pedal_filter_state();
            expect(distant.throttle_step_q16 == 0x222 && distant.brake_step_q16 == 0x333 && distant.throttle_ramp_q16 == 0x2345 && distant.brake_ramp_q16 == 0x3789
                       && distant.throttle_q16 == 0x68f5 && distant.brake_q16 == retained_filters.state().brake_q16,
                "A distant update must retain cached increments despite its new delta, retain the direct-gas ramp and advance the filtered brake ramp");
            retained_filters.begin_source_update({0x18000, 0xaaaa}, {}, true);
            const auto full = retained_filters.pedal_filter_state();
            expect(full.throttle_step_q16 == 0x1332 && full.brake_step_q16 == 0x1332 && full.throttle_ramp_q16 == distant.throttle_ramp_q16 && full.brake_ramp_q16 == 0x4abb,
                "The following full prepass must refresh both cached increments from their bases and advance brake from the retained distant ramp");
            ai_snapshot = saved_snapshot;
        }
        {
            const auto saved_snapshot = ai_snapshot;
            ai_word(0x44, 0x111);
            ai_word(0x150, 0x4000);
            ai_snapshot.dynamics[0x274] = 0xbf;
            ai_snapshot.dynamics[0x275] = 0xfe;
            Controls retained_steering(0);
            retained_steering.restore_source_snapshot(ai_snapshot);
            const SteeringFilterState warm{0x111, 0x4000, -321};
            expect(retained_steering.steering_filter_state() == warm,
                "A warm steering snapshot must expose cached44, accumulator150 and signed int16 angle274 without advancing them");
            retained_steering.handoff_to_ai(0x1234);
            retained_steering.restore_player_control();
            expect(retained_steering.steering_filter_state() == warm, "Ownership handoff must retain the entire steering filter state before its next control consumer");
            toca::AnalogControls analog;
            analog.steering_analog = true;
            retained_steering.begin_update({.analog = analog}, {0x8000, 0x20000}, {{}, 65536, 65536});
            expect(retained_steering.steering_filter_state() == SteeringFilterState{0x1999, 0x4000, -231},
                "Human4190f0 must slew the retained signed angle by90 while retaining the keyboard accumulator and refreshing only the full-update return cache");
            retained_steering.clear_steering_angle();
            expect(retained_steering.steering_filter_state() == SteeringFilterState{0x1999, 0x4000, 0}, "445360's angle reset must retain cached44 and accumulator150");

            ai_word(0x27c, 1);
            ai_word(0x310, 0);
            ai_word(0x314, 0);
            Controls keyboard_steering(0);
            keyboard_steering.restore_source_snapshot(ai_snapshot);
            keyboard_steering.set_source_input({});
            keyboard_steering.begin_source_update({0x20000, 0x8000}, {}, false);
            const auto distant = keyboard_steering.steering_filter_state();
            expect(
                distant.return_step_q16 == 0x111 && distant.accumulator_q16 == 0x3eef, "Distant419230 must recenter from the retained cached44 despite a different current delta");
            keyboard_steering.begin_source_update({0x8000, 0x20000}, {});
            const auto full = keyboard_steering.steering_filter_state();
            expect(full.return_step_q16 == 0x1999 && full.accumulator_q16 == 0x2556,
                "Following full412c90/419230 must refresh cached44 and recenter from the accumulator retained through the distant step");
            ai_snapshot = saved_snapshot;
        }
        {
            auto limiter_snapshot = ai_snapshot;
            limiter_snapshot.limiter_active = true;
            limiter_snapshot.limiter_load_q16 = 0x2345;
            Controls retained_limiter(0);
            retained_limiter.restore_source_snapshot(limiter_snapshot);
            retained_limiter.begin_source_update(time, {}, false);
            retained_limiter.finish_distant_update(0);
            expect(retained_limiter.state().limiter_active && retained_limiter.limiter_load_q16() == 0x2345,
                "444830/4448b0 must retain the actor limiter flag and load across distant physics");
            retained_limiter.begin_source_update(time, {});
            expect(retained_limiter.state().engine_torque_q16 == -0x2345, "Following40d3b0 must consume the retained actor limiter load after distant physics");
            retained_limiter.finish_update({});
            expect(!retained_limiter.state().limiter_active && retained_limiter.limiter_load_q16() == 0x2345 && retained_limiter.state().engine_speed_q16 == 0,
                "40d5f0 negative-RPM clamp must clear the limiter flag while preserving its conditional load state");
            retained_limiter.begin_source_update(time, {});
            retained_limiter.finish_update({});
            expect(!retained_limiter.state().limiter_active && retained_limiter.limiter_load_q16() == 0x2345,
                "A following ordinary full update must retain the last limiter load without reactivating its flag");
        }
        imported_ai.set_source_input({.ai_tire_offset_q16 = -5560});
        expect(std::all_of(imported_ai.state().tire_offsets_q16.begin(), imported_ai.state().tire_offsets_q16.end(),
                   [](auto value) {
                       return value == -5560;
                   }),
            "Observed402b10 AI tire output must apply its uniform12890 assignment without restoring body or filtered controls");
        bool rejected_ai_tire = false;
        try {
            source_inputs.set_source_input({.ai_tire_offset_q16 = -5560});
        } catch (const std::runtime_error&) {
            rejected_ai_tire = true;
        }
        expect(rejected_ai_tire && source_inputs.state().tire_offsets_q16[0] == 0, "The AI tire-output boundary must retain the human controller's original tire configuration");
        {
            auto captured_handoff = ai_snapshot;
            captured_handoff.gameplay[0x80] = 1;
            const auto field = [](auto& bytes, std::size_t offset, std::uint32_t value) {
                for (unsigned byte = 0; byte < 4; ++byte) {
                    bytes.at(offset + byte) = std::uint8_t(value >> (byte * 8));
                }
            };
            field(captured_handoff.dynamics, 0x14c, 0xaabbccdd);
            for (std::size_t index = 0; index < 4; ++index) {
                field(captured_handoff.gameplay, 0x60 + index * 4, std::uint32_t(original.modes[index]));
            }
            Controls imported_handoff(0);
            imported_handoff.restore_source_snapshot(captured_handoff);
            const auto imported_state = imported_handoff.controller_handoff_state();
            const auto rpm_before = imported_handoff.state().engine_speed_q16;
            expect(imported_state.human == 0 && imported_state.player == 1 && imported_state.queued_shift == 0xaabbccdd
                       && imported_state.modes == std::array<std::int32_t, 4>{1, 1, 0, 0} && imported_state.saved_modes == original.modes,
                "Warm Controls handoff state must retain captured human, modes, saved words and the complete queued request rather than reconstructing them from native flags");
            imported_handoff.set_source_input({.gear_edge = -1});
            expect(imported_handoff.controller_handoff_state().queued_shift == 0xaabbccff,
                "A mapped shift request must replace only dyn+14c while retaining the three neighboring raw bytes");
            imported_handoff.restore_player_control();
            expect(imported_handoff.controller_handoff_state().modes == original.modes && imported_handoff.controller_handoff_state().queued_shift == 0xaabbcc00
                       && imported_handoff.state().engine_speed_q16 == rpm_before,
                "The ordinary Controls handback must apply the captured mode transaction without an engine or input-filter update");
            field(captured_handoff.dynamics, 0x138, 1234);
            field(captured_handoff.dynamics, 0x13c, 5678);
            field(captured_handoff.dynamics, 0x150, 9012);
            captured_handoff.dynamics[0x274] = 0x34;
            captured_handoff.dynamics[0x275] = 0x12;
            imported_handoff.restore_source_snapshot(captured_handoff);
            imported_handoff.restore_qualifying_control();
            expect(imported_handoff.state().raw_left_q16 == 0 && imported_handoff.state().raw_right_q16 == 0 && imported_handoff.state().steering_angle == 0
                       && imported_handoff.state().steering_q16 == 9012 && imported_handoff.state().engine_speed_q16 == rpm_before
                       && imported_handoff.controller_handoff_state().modes == original.modes,
                "421390 rollout must clear raw steering axes and angle while retaining the filtered steering and engine");
        }
        Controls brake_attack(7, true);
        brake_attack.update({.brake = true}, {0x18000, 0xaaaa});
        expect(brake_attack.state().brake_q16 == 3811, "40a770's0xccc brake ramp scaled by1.5 must produce the original car7 filtered brake3811");
        for (std::uint32_t car = 0; car < 10; ++car) {
            const Controls local(car);
            expect(local.state().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
                "460170 local-player setup must apply402b90 before driving, removing40a770's temporary tire boost");
        }
        for (const auto car : {8u, 9u}) {
            Controls bonus(car, true);
            for (unsigned tick = 0; tick < 24; ++tick) {
                bonus.update({.throttle = true, .brake = true}, time);
            }
            expect(bonus.state().throttle_q16 == (car == 8 ? 0x5999 : 0x8000) && bonus.state().brake_q16 == (car == 8 ? 0x6666 : 0x9999),
                "Bonus car controls must retain original per-car pedal maxima");
            expect(car == 8 ? bonus.state().wheel_drive_q16[0] > 0 && bonus.state().wheel_drive_q16[2] > 0
                            : bonus.state().wheel_drive_q16[0] == 0 && bonus.state().wheel_drive_q16[2] > 0,
                "Bonus cars must retain original four-wheel or rear-wheel drive allocation");
        }
        Controls manual(0, true);
        for (unsigned tick = 0; tick < 24; ++tick) {
            manual.update({.throttle = true, .brake = true, .left = true}, time);
        }
        const auto full = manual.state();
        expect(full.throttle_q16 == 0x68f5 && full.brake_q16 == 0x9999 && full.engine_torque_q16 == 0x68f5,
            "Honda pedal filters must reach original per-car maxima with independent held pedals");
        expect(full.raw_left_q16 == 0 && full.steering_angle == 0, "Original low-speed gate must suppress steering at rest");
        expect(full.engine_speed_q16 == 0 && full.wheel_drive_q16[0] > 0 && full.wheel_drive_q16[2] == 0,
            "Fixed zero wheel inputs must not invent RPM or movement despite real front drive torque");
        manual.update({}, time);
        expect(manual.state().brake_q16 == 0 && manual.state().throttle_q16 > 0 && manual.state().throttle_q16 < full.throttle_q16,
            "Brake release resets immediately while throttle retains its three-times-faster release ramp");
        for (unsigned tick = 0; tick < 8; ++tick) {
            manual.update({}, time);
        }
        expect(manual.state().throttle_q16 == 0, "Released throttle must settle at zero");

        manual.update({.gear_up = true}, time);
        expect(manual.state().gear == 1 && manual.state().gear_pending && !manual.state().gear_committed, "Manual gear edge must queue, not immediately commit a cockpit shift");
        manual.update({.gear_down = true}, time);
        expect(manual.state().gear == 2 && !manual.state().gear_pending && manual.state().gear_committed,
            "Pending commit must emit one cockpit shift and consume the update before another gear edge");
        manual.update({}, time);
        expect(!manual.state().gear_committed, "The cockpit shift event must clear on the next control update");
        manual.set_start_hold(false);
        expect(manual.state().gear == 2, "Release without an active start hold must preserve qualifying gear selection");
        Controls reverse(0, true);
        reverse.update({.gear_up = true, .gear_down = true}, time);
        reverse.update({}, time);
        expect(reverse.state().gear == 7, "Simultaneous manual edges give Down priority; first gear shifts directly to reverse");

        Controls automatic(0);
        automatic.update({.brake = true}, time);
        expect(automatic.state().gear == 1 && automatic.state().gear_pending && automatic.state().reverse_pedal && automatic.state().throttle_q16 > 0
                   && automatic.state().brake_q16 == 0,
            "Automatic standstill brake must queue reverse and feed its original reverse-pedal throttle");
        automatic.update({.brake = true}, time);
        expect(automatic.state().gear == 7 && automatic.state().gear_committed, "Queued automatic reverse must commit and emit the same cockpit shift next update");
        automatic.update({.throttle = true}, time);
        expect(automatic.state().gear_pending && !automatic.state().reverse_pedal, "Gas without brake must queue return to first gear");
        automatic.update({}, time);
        expect(automatic.state().gear == 1, "Automatic forward selection failed");

        DrivetrainInputs wheels{{65536, 65536, 65536, 65536}, 65536, 65536};
        Controls rolling(0, true);
        rolling.update({.left = true, .right = true}, time, wheels);
        expect(rolling.state().raw_left_q16 == 65536 && rolling.state().raw_right_q16 == 0 && rolling.state().steering_q16 == 0x4ccc && rolling.state().steering_angle < 0,
            "Normal projection must give Left priority and preserve the profile-two steering minimum");
        Controls right(0, true);
        right.update({.right = true}, time, wheels);
        expect(right.state().raw_right_q16 == 65536 && right.state().steering_q16 == -0x4ccc && right.state().steering_angle == -rolling.state().steering_angle,
            "Right must produce the opposite source steering angle to Left");
        Controls variable_left(0, true), variable_right(0, true);
        variable_left.update({.left = true}, time, wheels);
        variable_right.update({.right = true}, time, wheels);
        variable_left.update({}, {0x8000, 0x20000}, wheels);
        variable_right.update({}, {0x8000, 0x20000}, wheels);
        expect(variable_left.state().steering_q16 == 0x3333 && variable_right.state().steering_q16 == -0x3333,
            "412c90 must scale419230's centering correction by the supplied half-step for both steering signs");
        variable_left.update({}, {0x4000, 0x40000}, wheels);
        variable_right.update({}, {0x4000, 0x40000}, wheels);
        expect(variable_left.state().steering_q16 == 0x2667 && variable_right.state().steering_q16 == -0x2667,
            "A following quarter-step must retain the original separately truncated steering correction");
        expect(rolling.state().engine_speed_q16 == 0x2bd70, "Engine must couple to explicit actual wheel rotation through the original gear ratio");
        wheels.wheel_speed_q16 = {4 * 65536, 4 * 65536, 4 * 65536, 4 * 65536};
        rolling.update({}, time, wheels);
        expect(rolling.state().engine_speed_q16 == 0x9851e && rolling.state().limiter_active && rolling.limiter_load_q16() == 0x346d,
            "Wheel over-speed must trigger the source engine limiter, proportional load and RPM clamp");
        rolling.update({}, time, wheels);
        expect(rolling.state().engine_torque_q16 == -0x346d, "Active source limiter must feed the previous proportional load as negative engine torque");
        wheels.wheel_speed_q16.fill(5 * 65536);
        rolling.update({}, time, wheels);
        expect(rolling.state().limiter_active && rolling.limiter_load_q16() == 0x51eb, "Larger wheel over-speed must saturate the source limiter load");

        Controls disconnected(0, true);
        DrivetrainInputs clutch;
        clutch.clutch_disengaged = true; // Explicit source record input, not a new keyboard binding.
        for (unsigned tick = 0; tick < 12; ++tick) {
            disconnected.update({.throttle = true}, time, clutch);
        }
        expect(disconnected.state().engine_speed_q16 > 0 && disconnected.state().wheel_drive_q16 == std::array<std::int32_t, 4>{},
            "The actual disconnected-engine branch must rev without inventing wheel motion");

        Controls start(0);
        start.set_start_hold(true);
        for (unsigned tick = 0; tick < 12; ++tick) {
            start.update({.throttle = true}, time);
        }
        expect(start.start_held() && start.state().gear == 0 && start.state().engine_speed_q16 > 0 && start.state().wheel_drive_q16 == std::array<std::int32_t, 4>{},
            "NoQual start hold must open the source clutch and rev the engine without suppressing gas or driving wheels");
        start.set_start_hold(false);
        start.update({.throttle = true}, time);
        expect(!start.start_held() && start.state().gear == 1 && start.state().wheel_drive_q16[0] > 0,
            "Automatic NoQual release must select source first gear and reconnect the actual drive torque");
        Controls manual_start(0, true);
        manual_start.set_start_hold(true);
        manual_start.update({.throttle = true, .gear_up = true}, time);
        manual_start.set_start_hold(true);
        manual_start.update({.throttle = true}, time);
        manual_start.set_start_hold(true);
        expect(manual_start.state().gear == 1 && manual_start.state().wheel_drive_q16 == std::array<std::int32_t, 4>{},
            "Start hold is an entry transition: manual preselection must survive while the open clutch still disconnects wheels");
        manual_start.set_start_hold(false);
        manual_start.update({.throttle = true}, time);
        expect(manual_start.state().gear == 1 && manual_start.state().wheel_drive_q16[0] > 0, "Manual release must reconnect the driver-selected gear");
        Controls neutral_start(0, true);
        neutral_start.set_start_hold(true);
        neutral_start.set_start_hold(false);
        expect(neutral_start.state().gear == 0, "Source manual release must preserve neutral until the driver selects a gear");

        expect(neutral_start.state().launch_tire_adjustment_q16 == 0 && neutral_start.state().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
            "A below60-percent launch must retain the local-player tire values without resetting the gas ramp");
        expect(source_launch_adjustment(599, 1001) == 0 && source_launch_adjustment(650, 1001) == 0xa3d && source_launch_adjustment(700, 1001) == 0x147a
                   && source_launch_adjustment(1001, 1001) == -0x147a,
            "448130 must truncate the physical maximum before percent thresholds and retain its positive70-percent endpoint");
        //Original lamp64 release starts at the physical limiter623902;
        //the first active wrapper resets tires between the second and third
        //human mapper calls without clearing the separate launch scalar.
        auto traced_launch = source_launch_adjustment(623902, 623902);
        ControllerHandoffState traced_player;
        advance_source_launch_tires(traced_launch, traced_player.tires);
        expect(traced_launch == -5127
                   && std::ranges::all_of(traced_player.tires,
                       [](auto value) {
                           return value == -5127;
                       }),
            "The actual held-clutch release must derive and publish all eight source tire corrections without importing a later scalar");
        advance_source_launch_tires(traced_launch, traced_player.tires);
        traced_player = apply_controller_handoff(traced_player, ControllerHandoffKind::player, 0);
        expect(traced_launch == -5012 && traced_player.tires == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
            "4483da controller handback resets tire fields while preserving the launch scalar independently of401260 clutch release");
        advance_source_launch_tires(traced_launch, traced_player.tires);
        expect(traced_launch == -4897
                   && std::ranges::all_of(traced_player.tires,
                       [](auto value) {
                           return value == -4897;
                       }),
            "The following human mapper must resume the retained launch decay after the wrapper tire reset");
        traced_launch = 0;
        traced_player.tires.fill(77);
        advance_source_launch_tires(traced_launch, traced_player.tires);
        expect(std::ranges::all_of(traced_player.tires,
                   [](auto value) {
                       return value == 77;
                   }),
            "A zero launch scalar must leave existing tire outputs unchanged");
        Controls band_start(0);
        DrivetrainInputs launch_wheels;
        launch_wheels.wheel_speed_q16.fill(std::int32_t(std::int64_t((0x9851e / 100) * 65) * 65536 / 0x2bd70));
        band_start.finish_update(launch_wheels);
        band_start.set_start_hold(true);
        band_start.set_start_hold(false);
        expect(band_start.state().launch_tire_adjustment_q16 > 0 && band_start.state().launch_tire_adjustment_q16 <= 0x147a,
            "Actual65-percent engine speed must select the source positive launch tire correction");
        Controls maximum_start(0);
        maximum_start.set_start_hold(true);
        for (unsigned tick = 0; tick < 32 && maximum_start.state().engine_speed_q16 < 0x9851e; ++tick) {
            maximum_start.update({.throttle = true}, time);
        }
        expect(maximum_start.state().engine_speed_q16 == 0x9851e, "The source open-clutch engine must reach its limiter under held Gas");
        auto separate_start = maximum_start;
        const auto before_launch = separate_start.state();
        separate_start.begin_launch_adjustment();
        expect(separate_start.start_held() && separate_start.state().gear == 0 && separate_start.state().launch_tire_adjustment_q16 == -0x147a
                   && separate_start.state().engine_speed_q16 == before_launch.engine_speed_q16 && separate_start.state().tire_offsets_q16 == before_launch.tire_offsets_q16,
            "448130 starts its RPM-derived correction while the clutch remains held, without updating engine, gear or tire outputs");
        separate_start.advance_launch_adjustment();
        const auto separate_launch = separate_start.state().launch_tire_adjustment_q16;
        separate_start.set_source_start_hold(false);
        expect(!separate_start.start_held() && separate_start.state().gear == 1 && separate_launch == -0x147a + 0x73
                   && separate_start.state().launch_tire_adjustment_q16 == separate_launch && separate_start.state().engine_speed_q16 == before_launch.engine_speed_q16
                   && separate_start.state().throttle_q16 == before_launch.throttle_q16 && separate_start.state().wheel_drive_q16 == before_launch.wheel_drive_q16,
            "401260 reconnects the clutch independently, retaining the decayed launch scalar and all filtered/engine/drive state");
        maximum_start.set_start_hold(false);
        expect(maximum_start.state().launch_tire_adjustment_q16 == -0x147a, "A limiter-speed launch must select the exact negative tire correction");
        maximum_start.update({}, time);
        const auto first_launch = -0x147a + 0x73;
        expect(maximum_start.state().launch_tire_adjustment_q16 == first_launch
                   && maximum_start.state().tire_offsets_q16
                          == std::array<std::int32_t, 8>{first_launch, first_launch, first_launch, first_launch, first_launch, first_launch, first_launch, first_launch},
            "422f60 must decay launch adjustment by0x73 before412890 publishes all eight tire fields");
        for (unsigned tick = 0; tick < 64 && maximum_start.state().launch_tire_adjustment_q16 != 0; ++tick) {
            maximum_start.update({}, time);
        }
        expect(maximum_start.state().launch_tire_adjustment_q16 == 0 && maximum_start.state().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
            "The launch correction crossing zero must use402b90's exact reset, including the front longitudinal offset");
        //Import a correction already inside its final115-unit decay interval.
        //Mapped physics must consume the resulting tires without decaying twice.
        auto launch_snapshot = ai_snapshot;
        const auto launch_word = [&](std::size_t offset, std::int32_t value) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                launch_snapshot.dynamics.at(offset + byte) = std::uint8_t(std::uint32_t(value) >> (byte * 8));
            }
        };
        launch_word(0x27c, 2);
        launch_word(0x310, 0);
        launch_word(0x314, 0);
        launch_word(0x124, 0x60000);
        launch_snapshot.dynamics[0x330] = launch_snapshot.dynamics[0x334] = 1;
        launch_snapshot.gameplay[8] = 1;
        for (const auto initial : {-200, 200}) {
            launch_snapshot.launch_tire_adjustment_q16 = initial;
            for (std::size_t i = 0; i < 8; ++i) {
                launch_word(0xdc + i * 4, initial);
            }
            Controls warm_launch(0);
            warm_launch.restore_source_snapshot(launch_snapshot);
            expect(warm_launch.state().launch_tire_adjustment_q16 == initial && warm_launch.state().tire_offsets_q16[0] == initial,
                "A warm consumed snapshot imports an already-running launch correction once without applying its earlier prephase");
            warm_launch.advance_launch_adjustment();
            const auto decayed = initial < 0 ? initial + 0x73 : initial - 0x73;
            expect(warm_launch.state().launch_tire_adjustment_q16 == decayed && warm_launch.state().engine_speed_q16 == 0x60000,
                "Positive and negative source launch decay changes only the retained correction and eight tire offsets");
            warm_launch.set_source_input({.throttle_q16 = 65536});
            warm_launch.begin_source_update(time, {});
            expect(warm_launch.state().launch_tire_adjustment_q16 == decayed && warm_launch.state().tire_offsets_q16[0] == decayed,
                "The mapped physics consumer must not repeat422f60 launch decay after its explicit local input boundary");
            warm_launch.reset_player_tire_offsets();
            expect(warm_launch.state().launch_tire_adjustment_q16 == decayed && warm_launch.state().tire_offsets_q16[4] == 0xccc,
                "402b90 handback resets tire parameters without erasing the still-running local launch scalar");
            warm_launch.handoff_to_ai(0x1234);
            warm_launch.begin_ai_update({}, time, {}, false);
            expect(warm_launch.state().launch_tire_adjustment_q16 == decayed && warm_launch.state().tire_offsets_q16[0] == 0x1234,
                "402be0 and later AI updates must retain the separate player launch scalar without applying human422f60 decay to AI tires");
            warm_launch.restore_player_control();
            expect(warm_launch.state().launch_tire_adjustment_q16 == decayed && warm_launch.state().tire_offsets_q16[4] == 0xccc,
                "402c80 resets tires while leaving the player's suspended launch scalar ready for its next real human mapping pass");
            warm_launch.advance_launch_adjustment();
            expect(warm_launch.state().launch_tire_adjustment_q16 == 0 && warm_launch.state().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
                "Either sign crossing zero restores the exact402b90 tire defaults through the shared production consumer");
        }
        for (std::uint32_t car = 0; car < 8; ++car) {
            for (const bool selected_manual : {false, true}) {
                Controls restored(car, selected_manual);
                const DrivetrainInputs moving{{65536, 65536, 65536, 65536}, 65536, 65536};
                restored.update({.right = true}, time, moving);
                const auto human_steering = restored.state().steering_q16;
                restored.handoff_to_ai(0x1234);
                restored.handoff_to_ai(0x1234);
                for (unsigned tick = 0; tick < 24; ++tick) {
                    restored.begin_ai_update({.left_q16 = 0x8000, .throttle_q16 = 0x8000}, time, moving);
                    restored.finish_update(moving);
                }
                const auto owned = restored.state();
                expect(owned.steering_q16 == human_steering && owned.steering_angle == -0xc7, "AI4190f0 must steer directly without changing the saved keyboard accumulator+150");
                restored.restore_player_control();
                restored.restore_player_control();
                expect(restored.state().gear == owned.gear && restored.state().engine_speed_q16 == owned.engine_speed_q16 && restored.state().steering_q16 == human_steering
                           && restored.state().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
                    "402c80 must restore modes/reset tires while preserving the moving car's keyboard accumulator, gear and RPM");
                restored.begin_update({.gear_up = true}, time, moving);
                expect(restored.state().gear_pending == selected_manual && restored.state().throttle_q16 == 0,
                    "Every car must regain its selected transmission and human gas filter without inheriting the directAI throttle ramp");
            }
        }
        std::cout << "PASS: source keyboard filters/gears, stationary torque and explicit wheel/engine coupling\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
