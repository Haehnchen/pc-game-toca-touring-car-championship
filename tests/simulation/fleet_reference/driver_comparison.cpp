#include "driver_comparison.hpp"

#include "binary_io.hpp"
#include "diagnostics.hpp"
#include "session_comparison.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

void compare_ai_route(const AiInitialState& initial, const Record& record, std::ostream& output, std::ostream* actor_timing, std::ostream* shared_timing,
    std::ostream* finish_output, std::ostream* display_output, AiDrivers* retained_drivers, SourceRandom* shared_random, AiRouteStepResults* retained_results,
    bool retain_driver_state, RetainedRaceTiming* retained_timing, Record* generated) {
    const bool timing = actor_timing != nullptr;
    const bool compare_finish = finish_output != nullptr;
    if (timing
        && ((record.timing_stage != 2 && record.timing_stage != 4)
            || (record.timing_state != 6 && record.timing_state != 9
                && !(retained_timing && retained_timing->local_session && (record.timing_state == 10 || record.timing_state == 13)))
            || record.timing_lap_limit < 1 || record.timing_lap_limit > 99 || record.timing_running > 1 || ((record.timing_flags >> 16) & 0xff) > 1)) {
        throw UnsupportedSourceState("Checkpoint timing requires ordinary race clocks/stage and excludes qualifying/finish state transitions");
    }
    if (compare_finish && (record.source_mode > 1 || (record.finish.count & 0xff) > 16 || (record.finish.local_count & 0xff) > 4 || record.finish.stopped > 1)) {
        throw UnsupportedSourceState("Finish timing requires ordinary local mode and valid finish counters");
    }
    auto finish = record.finish;
    auto displays = record.displays;
    std::array<ActorFinishState, 16> actor_finishes;
    auto checkpoint_times = record.checkpoint_times;
    auto timing_flags = record.timing_flags;
    std::array<LapTimingState, 16> timing_results;
    if (record.ai_release_counter > 0x7fffffff) {
        throw UnsupportedSourceState("AI route release counter is outside the nonnegative source interval");
    }
    SourceRandom independent_random(shared_random ? 0u : record.ai_rng);
    auto& random = shared_random ? *shared_random : independent_random;
    //403dd0 completes every421390 call before its separate target/RNG pass.
    //The interleaved adapter loop below must stamp local402be0 at that earlier
    //call boundary without changing the target stream's consumption order.
    const auto lap_pass_rng = random.state();
    std::array<std::vector<std::int16_t>, 3> distributions;
    //420460: native distribution order is negative, positive, lateral.
    constexpr std::array<std::size_t, 3> counts{0x53d, 0x53c, 0x53e}, arrays{0x348, 0x280, 0x410};
    for (std::size_t distribution = 0; distribution < distributions.size(); ++distribution) {
        const auto count = initial.resolved[counts[distribution]];
        if (count > 100) {
            throw UnsupportedSourceState("Resolved AI distribution count exceeds its original100-short allocation");
        }
        for (std::size_t index = 0; index < count; ++index) {
            distributions[distribution].push_back(std::bit_cast<std::int16_t>(source_u16(initial.resolved, arrays[distribution] + index * 2)));
        }
    }
    std::array<AiRouteState, 16> results;
    std::array<AiRouteProgress, 16> progress_results;
    std::array<AiRouteCadence, 16> cadences;
    for (std::size_t actor = 0; actor < record.ai_actors.size(); ++actor) {
        const auto& input = record.ai_actors[actor];
        const auto& game = input.gameplay;
        const auto reject = [&](const std::string& reason) {
            throw UnsupportedSourceState("AI route update " + std::to_string(record.id) + " actor " + std::to_string(actor) + ": " + reason);
        };
        if (source_u32(game, 4) != 1 || game[0x81] < 1 || game[0x81] > 5 || input.body_mode > 4 || game[0x8d] >= 16) {
            reject("unsupported active route/body mode or resolved-driver index");
        }
        for (const auto offset : {0x8, 0xc, 0x28, 0x3c, 0x50, 0x54}) {
            if (source_u32(game, offset) > 1) {
                reject("unknown source route flag at gameplay+" + std::to_string(offset));
            }
        }
        if (source_u32(game, 0x30) != 0) {
            reject("pit route is outside the independent target-cache slice");
        }
        const auto player = std::bit_cast<std::int8_t>(game[0x80]);
        if (player < -1 || player > 3) {
            reject("unknown permanent-player identity");
        }
        ActorFinishState actor_finish{source_u32(game, 0x54), source_i32(game, 0x120), game[0x8c], game[0x8a]};
        if (player >= 0 && actor_finish.notice != 0 && !retained_timing) {
            --actor_finish.notice;
        }
        const auto driver_row = std::span<const std::uint8_t>(initial.resolved).subspan(std::size_t(game[0x8d]) * 40, 40);
        std::optional<AiDriver> independent_driver;
        auto& destination = retained_drivers ? (*retained_drivers)[actor] : independent_driver;
        const auto initialize = !retain_driver_state || !destination;
        if (initialize) {
            destination.emplace(initial.route, std::span<const std::uint8_t, 40>(driver_row.data(), 40), distributions, retained_drivers ? record.traffic.weather_penalty : 0, 0,
                random, false, AiRouteEncoding::source_initialized);
        }
        auto& driver = *destination;
        if (game[0x86] > 127 || driver_row[19] > 127) {
            reject("unsupported signed route-cadence counter or resolved period");
        }
        if (initialize && retain_driver_state) {
            driver.initialize_source_route_cadence(game[0x86]);
        }
        const auto cadence = retain_driver_state ? driver.advance_source_route_cadence() : driver.advance_source_route_cadence(game[0x86]);
        AiRouteState state;
        state.cached_point = game[0x9e];
        state.traffic_mode = source_ai_route_mode(game);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            state.target[axis] = source_i32(game, 0x108 + axis * 4);
            state.alternate_target[axis] = source_i32(game, 0x114 + axis * 4);
        }
        state.desired_speed_q16 = source_i32(game, 0xd0);
        state.alternate_speed_q16 = source_i32(game, 0xd4);
        state.speed_variation_q16 = source_i32(game, 0xd8);
        state.distance_q16 = source_i32(game, 0xe0);
        state.projected_distance_q16 = source_i32(game, 0xe4);
        state.angle = std::bit_cast<std::int16_t>(source_u16(game, 0xfc));
        state.alternate_angle = std::bit_cast<std::int16_t>(source_u16(game, 0xfe));
        state.route_angle = std::bit_cast<std::int16_t>(source_u16(game, 0x100));
        state.primary_boundary_angle = std::bit_cast<std::int16_t>(source_u16(game, 0x102));
        state.alternate_boundary_angle = std::bit_cast<std::int16_t>(source_u16(game, 0x104));
        if (initialize) {
            driver.restore_source_route(state);
        } else {
            state = driver.route_state();
        }
        const auto next = source_u16(game, 0xa2), previous = source_u16(game, 0xa4);
        if (next >= source_u32(initial.route, 0) || previous >= source_u32(initial.route, 0)) {
            reject("route index exceeds initialized POINTS");
        }
        //403dd0 bypasses the rear-boundary test for this ordinary AI case.
        const bool test_previous = !(player == -1 && source_u32(game, 0xc) == 0 && (source_u32(game, 0x28) == 0 || source_u32(game, 0x54) == 0));
        if (game[0xa6] >= 6 || game[0x9d] >= 6 || (game[0x9d] & 1) != 0) {
            reject("unknown source checkpoint/marker index");
        }
        const AiRouteProgressInput progress_input{
            {next, previous}, source_u32(game, 0xc) != 0, test_previous, source_u32(game, 8) != 0, game[0xa6], game[0x9d], source_u32(game, 0x54) != 0};
        const auto progress = driver.advance_source_route_progress(input.body, progress_input, cadence.refresh);
        if (progress.checkpoint_side_effect && !timing) {
            reject("lap/checkpoint timing side effects are outside the independent route slice");
        }
        if (timing) {
            LapTimingState clock;
            clock.lap = std::bit_cast<std::int16_t>(source_u16(game, 0xa8));
            clock.record_flag = source_u32(game, 0);
            clock.best_ms = source_u32(game, 0xc0);
            clock.current_ms = source_u32(game, 0xc4);
            clock.lap_start_ms = source_u32(game, 0xc8);
            clock.best_notice = game[0x8a];
            for (std::size_t component = 0; component < 3; ++component) {
                clock.best_display[component] = std::bit_cast<std::int16_t>(source_u16(game, 0xaa + component * 2));
                clock.current_display[component] = std::bit_cast<std::int16_t>(source_u16(game, 0xb0 + component * 2));
            }
            if (clock.lap < 0 || clock.lap > 99) {
                reject("unknown displayed lap in checkpoint timing input");
            }
            if (!retained_timing) {
                clock = observe_lap_clock(clock, record.timing_clock, record.timing_running != 0, player >= 0, record.timing_state);
            }
            if (progress.lap_event) {
                //421390 sets this byte before its state guard. Finish/qualifying
                //branches are explicit whole-update boundaries, never imports.
                if (((timing_flags >> 16) & 0xff) == 0) {
                    timing_flags |= 1u << 16;
                }
                const auto transition = advance_lap_timing(clock, {record.timing_clock, record.timing_lap_limit, record.timing_stage, record.timing_state, player >= 0,
                                                                      player >= 0 && finish.local_finished[std::size_t(player)] != 0, source_u32(game, 8) != 0, false});
                if (transition.action != LapTimingAction::none && !compare_finish) {
                    reject("race finish/handoff after lap timing requires explicit finish provenance");
                }
                clock = transition.state;
                actor_finish.best_notice = clock.best_notice;
                if (transition.action == LapTimingAction::qualifying_restart || transition.action == LapTimingAction::qualifying_finish) {
                    reject("local qualifying controller/release handoff is outside this timing slice");
                }
                if (transition.action == LapTimingAction::race_finish) {
                    if (player >= 0 && finish.local_finished[std::size_t(player)] == 0) {
                        if (!retained_timing || !retained_timing->local_session || !generated || player != 0 || game[0x84] >= 6) {
                            reject("local race finish requires retained ordinary controller/classification provenance without retirement fallback");
                        }
                        auto& raw = generated->traffic.actors[actor];
                        ControllerHandoffState handoff;
                        handoff.human = source_u32(raw.gameplay, 8);
                        handoff.reverse_fallback = source_u32(raw.gameplay, 0x28);
                        handoff.countdown = raw.gameplay[0x83];
                        handoff.player = player;
                        handoff.gear = raw.dynamics[0x330];
                        handoff.queued_shift = source_u32(raw.dynamics, 0x14c);
                        for (std::size_t part = 0; part < 4; ++part) {
                            handoff.modes[part] = source_i32(raw.dynamics, part == 3 ? 0x27c : 0x310 + part * 4);
                            handoff.saved_modes[part] = source_i32(raw.gameplay, 0x60 + part * 4);
                        }
                        for (std::size_t tire = 0; tire < 8; ++tire) {
                            handoff.tires[tire] = source_i32(raw.dynamics, 0xdc + tire * 4);
                        }
                        handoff = apply_controller_handoff(handoff, ControllerHandoffKind::ai, record.ai_tire_base);
                        source_store(raw.gameplay, 8, handoff.human);
                        source_store(raw.gameplay, 0x28, handoff.reverse_fallback);
                        raw.gameplay[0x83] = std::uint8_t(handoff.countdown);
                        for (std::size_t part = 0; part < 4; ++part) {
                            source_store(raw.dynamics, part == 3 ? 0x27c : 0x310 + part * 4, std::uint32_t(handoff.modes[part]));
                            source_store(raw.gameplay, 0x60 + part * 4, std::uint32_t(handoff.saved_modes[part]));
                        }
                        for (std::size_t tire = 0; tire < 8; ++tire) {
                            source_store(raw.dynamics, 0xdc + tire * 4, std::uint32_t(handoff.tires[tire]));
                        }
                        --clock.lap;
                        actor_finish = record_local_finish(finish, actor_finish, actor, player, 1, record.timing_clock, record.finish_release_clock);
                        retained_timing->handoff = handoff;
                        retained_timing->handoff_rng = lap_pass_rng;
                        std::fill_n(retained_timing->split_enable.begin(), 4, 0);
                        driver.handoff(std::int8_t(handoff.gear), true);
                        std::array<FinishClassificationActor, 16> totals;
                        for (std::size_t other = 0; other < 16; ++other) {
                            const auto& other_game = generated->traffic.actors[other].gameplay;
                            totals[other] = {source_i32(other_game, 0x120), source_i32(other_game, 0xdc), source_u32(other_game, 0xc0),
                                std::bit_cast<std::int16_t>(source_u16(other_game, 0xa8)), std::bit_cast<std::int8_t>(other_game[0x8f])};
                        }
                        totals[actor].total = actor_finish.total_hundredths;
                        totals[actor].best_ms = clock.best_ms;
                        totals[actor].lap = clock.lap;
                        classify_finish_totals(totals, finish.order, 16, actor, finish.count & 0xff, retained_timing->lap_distance);
                        for (std::size_t other = 0; other < 16; ++other) {
                            for (auto* output_game : {&generated->traffic.actors[other].gameplay, &generated->ai_actors[other].gameplay}) {
                                source_store(*output_game, 0x120, std::uint32_t(totals[other].total));
                                source_store(*output_game, 0xc0, totals[other].best_ms);
                            }
                            if (other < actor) {
                                actor_finishes[other].total_hundredths = totals[other].total;
                                timing_results[other].best_ms = totals[other].best_ms;
                            }
                        }
                        actor_finish.total_hundredths = totals[actor].total;
                        clock.best_ms = totals[actor].best_ms;
                        std::array<std::int16_t, 16> scores;
                        std::array<std::uint8_t, 3> podiums;
                        for (std::size_t other = 0; other < 16; ++other) {
                            scores[other] = std::bit_cast<std::int16_t>(source_u16(retained_timing->configuration_tail, 2 + other * 2));
                        }
                        std::copy_n(retained_timing->configuration_tail.begin() + 0x2c, 3, podiums.begin());
                        award_race_classification(scores, podiums, finish.order, actor, game[0x85]);
                        retained_timing->classification_order = classification_standings(scores);
                        for (std::size_t other = 0; other < 16; ++other) {
                            source_store(retained_timing->configuration_tail, 2 + other * 2, std::uint16_t(scores[other]), 2);
                        }
                        std::copy(podiums.begin(), podiums.end(), retained_timing->configuration_tail.begin() + 0x2c);
                    } else {
                        actor_finish = record_ai_finish(finish, actor_finish, game[0x8d], record.timing_clock, record.finish_release_clock);
                    }
                    clock.best_notice = actor_finish.best_notice;
                }
                if (actor_finish.finished == 0 && clock.lap == std::int32_t(record.timing_lap_limit) && record.timing_lap_limit >= 2) {
                    actor_finish.notice = 0x50;
                }
            }
            if (progress.split_event) {
                if (game[0x8f] < 1 || game[0x8f] > 16) {
                    reject("unknown source rank at checkpoint table write");
                }
                const auto split = record_checkpoint_time(checkpoint_times, std::uint32_t(actor), progress.split_sector, game[0x8f], clock.lap, record.timing_clock);
                if (retained_timing && !retained_timing->local_session && player >= 0 && split.written) {
                    reject("local split display production requires the unrecorded frame lifetime consumer");
                }
                if (compare_finish && player >= 0) {
                    consume_checkpoint_display(displays, std::uint32_t(player), progress.split_sector, split, clock.current_display);
                }
            }
            timing_results[actor] = clock;
            actor_finishes[actor] = actor_finish;
            if (retained_timing) {
                if (!generated) {
                    throw std::runtime_error("Retained timing requires generated gameplay output");
                }
                auto& game_output = generated->traffic.actors[actor].gameplay;
                source_store(game_output, 0, clock.record_flag);
                source_store(game_output, 0xa8, std::uint16_t(clock.lap), 2);
                source_store(game_output, 0xc0, clock.best_ms);
                source_store(game_output, 0xc4, clock.current_ms);
                source_store(game_output, 0xc8, clock.lap_start_ms);
                for (std::size_t part = 0; part < 3; ++part) {
                    source_store(game_output, 0xaa + part * 2, std::uint16_t(clock.best_display[part]), 2);
                    source_store(game_output, 0xb0 + part * 2, std::uint16_t(clock.current_display[part]), 2);
                }
                source_store(game_output, 0x54, actor_finish.finished);
                source_store(game_output, 0x120, std::uint32_t(actor_finish.total_hundredths));
                game_output[0x8a] = actor_finish.best_notice;
                game_output[0x8c] = actor_finish.notice;
                generated->ai_actors[actor].gameplay = game_output;
            }
        }
        auto context = initial.context;
        context.release_counter = record.ai_release_counter;
        context.full_body = input.body_mode == 0;
        context.player = player >= 0;
        context.finished = actor_finish.finished != 0;
        context.swap_start_lines = source_u32(game, 0x50) != 0;
        auto route_angle = state.route_angle;
        if (cadence.refresh) {
            route_angle = driver.source_route_angle(input.body, next);
        }
        const auto speed = fixed::mul(input.speed_q16, 0x640000);
        if (speed < -1000 * fixed::one || speed > 1000 * fixed::one) {
            reject("signed source speed is outside the existing production AI route range");
        }
        driver.observe_source_route(input.body, speed, progress.target_cursor.next, route_angle, context);
        results[actor] = driver.route_state();
        progress_results[actor] = progress;
        cadences[actor] = cadence;
    }
    if (retained_results) {
        *retained_results = {results, progress_results, cadences};
    }
    if (retained_timing) {
        retained_timing->finish = finish;
        retained_timing->flags = timing_flags;
        retained_timing->checkpoints = checkpoint_times;
        retained_timing->displays = displays;
    }
    for (std::size_t actor = 0; actor < results.size(); ++actor) {
        const auto& state = results[actor];
        output << record.id << ',' << actor << ',' << state.cached_point;
        for (const auto& vector : {state.target, state.alternate_target}) {
            for (const auto value : vector) {
                output << ',' << value;
            }
        }
        output << ',' << state.desired_speed_q16 << ',' << state.alternate_speed_q16 << ',' << state.speed_variation_q16 << ',' << state.distance_q16 << ','
               << state.projected_distance_q16 << ',' << state.angle << ',' << state.alternate_angle << ',' << state.route_angle << ',' << random.state();
        const auto& progress = progress_results[actor];
        output << ',' << progress.cursor.next << ',' << progress.cursor.previous << ',' << progress.backwards << ',' << unsigned(progress.current_gate) << ','
               << unsigned(progress.previous_gate) << ',' << unsigned(cadences[actor].ticks) << ',' << cadences[actor].refresh << ',' << unsigned(progress.checkpoint) << ','
               << unsigned(progress.checkpoint_marker) << '\n';
    }
    if (timing) {
        for (std::size_t actor = 0; actor < timing_results.size(); ++actor) {
            const auto& clock = timing_results[actor];
            *actor_timing << record.id << ',' << actor << ',' << clock.lap << ',' << clock.record_flag << ',' << clock.best_ms << ',' << clock.current_ms << ','
                          << clock.lap_start_ms;
            for (const auto& display : {clock.best_display, clock.current_display}) {
                for (const auto value : display) {
                    *actor_timing << ',' << value;
                }
            }
            *actor_timing << ',' << unsigned(clock.best_notice);
            if (compare_finish) {
                *actor_timing << ',' << actor_finishes[actor].finished << ',' << actor_finishes[actor].total_hundredths << ',' << unsigned(actor_finishes[actor].notice);
            }
            *actor_timing << '\n';
        }
        for (std::size_t slot = 0; slot < checkpoint_times.size(); ++slot) {
            const auto& time = checkpoint_times[slot];
            *shared_timing << record.id << ',' << slot << ',' << time.clock << ',' << time.lap << ',' << time.actor << ',' << timing_flags << '\n';
        }
    }
    if (compare_finish) {
        *finish_output << record.id << ',' << finish.count << ',' << finish.local_count << ',' << finish.timer2 << ',' << finish.timer3 << ',' << finish.pending_state << ','
                       << finish.stopped;
        for (const auto flag : finish.local_finished) {
            *finish_output << ',' << unsigned(flag);
        }
        for (const auto actor : finish.order) {
            *finish_output << ',' << unsigned(actor);
        }
        *finish_output << '\n';
        for (std::size_t player = 0; player < displays.players.size(); ++player) {
            const auto& display = displays.players[player];
            *display_output << record.id << ',' << player << ',' << display.mode << ',' << display.leader << ',' << display.active << ',' << display.ticks;
            for (const auto component : display.components) {
                *display_output << ',' << component;
            }
            for (const auto& values : {display.reference, display.current}) {
                for (const auto value : values) {
                    *display_output << ',' << value;
                }
            }
            for (const auto& text : {display.text, display.leader_text}) {
                const auto end = std::find(text.begin(), text.end(), '\0');
                *display_output << ',' << std::string(text.begin(), end);
            }
            for (const auto value : displays.persisted) {
                *display_output << ',' << value;
            }
            *display_output << '\n';
        }
    }
}

void compare_traffic_controls(const AiInitialState& initial, const Record& record, std::ostream& traffic_output, std::ostream& traffic_world, std::ostream& control_output,
    std::ostream& control_world, AiDrivers* retained_drivers, SourceRandom* shared_random, bool retain_driver_state, Record* generated, const RetainedRaceTiming* timing) {
    const auto& source = record.traffic;
    if (source.source_mode != 0 || source.race_stop > 1 || (source.session_state != 6 && source.session_state != 9 && source.session_state != 10 && source.session_state != 13)) {
        throw UnsupportedSourceState("Traffic/control comparison requires an ordinary active race or finish interval");
    }
    const auto points = source_u32(initial.route, 0);
    const auto route_length = source_i32(source.tuning, 0);
    if (route_length <= 0 || initial.brake_bypass.size() != points) {
        throw UnsupportedSourceState("Traffic requires original positive route length and live brake-bypass provenance");
    }
    SourceRandom independent_random(shared_random ? 0u : source.rng);
    auto& random = shared_random ? *shared_random : independent_random;
    AiDrivers independent_drivers;
    auto& drivers = retained_drivers ? *retained_drivers : independent_drivers;
    std::array<AiTraffic, 16> fleet;
    std::array<bool, 16> ai{}, controls{};
    const std::array<std::vector<std::int16_t>, 3> unused_distributions;
    AiTrafficContext traffic_context;
    traffic_context.release_counter = source.release_counter;
    traffic_context.qualifying = source_u32(source.status, 0) != 0;
    traffic_context.headlight_gates = std::ranges::any_of(source.headlight_gates, [](auto flag) {
        return flag != 0;
    });
    traffic_context.start_lines = &initial.context;
    std::array<AiControlContext, 16> control_contexts;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& raw = source.actors[actor];
        const auto& game = raw.gameplay;
        const auto& body = raw.body;
        const auto& dynamics = raw.dynamics;
        if (source_u32(game, 4) != 1 || source_u32(game, 8) > 1 || source_u32(game, 0x30) != 0 || game[0x8d] != actor || game[0x81] > 6 || game[0x96] >= 16 || game[0x97] >= 16
            || game[0x9f] >= 4 || game[0xa0] >= 4 || dynamics[0x330] > 7) {
            throw UnsupportedSourceState("Traffic requires sixteen active ordinary no-pit actor records with original indices");
        }
        auto& observed = fleet[actor];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            observed.body.position[axis] = source_i32(body, 0xbc + axis * 4);
            for (std::size_t row = 0; row < 3; ++row) {
                observed.body.basis[row][axis] = source_i32(body, 0x150 + row * 12 + axis * 4);
            }
            for (std::size_t corner = 0; corner < 8; ++corner) {
                observed.body.contacts[corner][axis] = source_i32(body, 0xf0 + corner * 12 + axis * 4);
            }
        }
        observed.actor_id = std::uint32_t(actor);
        observed.ahead_id = game[0x96];
        observed.behind_id = game[0x97];
        observed.next_point = source_u16(game, 0xa2);
        observed.speed_q16 = source_i32(game, 0xcc);
        observed.route_distance_q16 = source_i32(game, 0xdc);
        observed.forward_gap_q16 = source_i32(game, 0xf0);
        observed.player = std::bit_cast<std::int8_t>(game[0x80]) >= 0;
        observed.human = source_u32(game, 8) != 0;
        observed.reversing = source_u32(game, 0x28) != 0;
        observed.opposing_direction = source_u32(game, 0x10) != 0;
        observed.route_unavailable = source_u32(game, 0x20) != 0 || source_u32(game, 0x24) != 0;
        observed.blocked = source_u32(game, 0x18) != 0;
        observed.passing = source_u32(game, 0x2c) != 0;
        observed.blocker_id = game[0x99];
        observed.mode = game[0x81];
        observed.route_angle = std::bit_cast<std::int16_t>(source_u16(game, 0x100));
        observed.lap = std::bit_cast<std::int16_t>(source_u16(game, 0xa8));
        observed.refresh = source_u32(game, 0x3c) != 0;
        observed.swap_start_lines = source_u32(game, 0x50) != 0;
        observed.obstacle = source_u32(game, 0x40) != 0;
        if (observed.next_point >= points || game[0x9e] >= points) {
            throw UnsupportedSourceState("Traffic target exceeds initialized POINTS");
        }
        const auto row = std::span<const std::uint8_t>(initial.resolved).subspan(actor * 40, 40);
        if (!retained_drivers) {
            drivers[actor].emplace(initial.route, std::span<const std::uint8_t, 40>(row.data(), 40), unused_distributions, source.weather_penalty, 0, random, false,
                AiRouteEncoding::source_initialized);
        }
        auto& driver = *drivers[actor];
        driver.set_rival(true, source_u32(body, 0x4f0) != 0);
        if (!retain_driver_state || record.id == 0) {
            driver.handoff(std::int8_t(dynamics[0x330]), source_u32(game, 0x54) != 0);
        }
        AiRouteState route;
        route.cached_point = game[0x9e];
        route.traffic_mode = game[0x81];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            route.target[axis] = source_i32(game, 0x108 + axis * 4);
            route.alternate_target[axis] = source_i32(game, 0x114 + axis * 4);
        }
        route.desired_speed_q16 = source_i32(game, 0xd0);
        route.alternate_speed_q16 = source_i32(game, 0xd4);
        route.speed_variation_q16 = source_i32(game, 0xd8);
        route.distance_q16 = source_i32(game, 0xe0);
        route.projected_distance_q16 = source_i32(game, 0xe8);
        route.angle = std::bit_cast<std::int16_t>(source_u16(game, 0xfc));
        route.alternate_angle = std::bit_cast<std::int16_t>(source_u16(game, 0xfe));
        route.route_angle = observed.route_angle;
        if (!retained_drivers) {
            driver.restore_source_route(route);
        }
        AiTrafficDecisionState decision;
        decision.mode = observed.mode;
        decision.blocked = observed.blocked;
        decision.brake = source_u32(game, 0x1c) != 0;
        decision.passing = observed.passing;
        decision.steering_cached = source_u32(game, 0x34) != 0;
        decision.forced_brake = source_u32(game, 0x48) != 0;
        decision.passed_actor = game[0x98];
        decision.blocker_actor = game[0x99];
        for (std::size_t index = 0; index < 4; ++index) {
            decision.corridor[index] = source_u32(game, 0x70 + index * 4) != 0;
        }
        decision.corners = {game[0x9f], game[0xa0]};
        decision.obstacle_angles = {std::bit_cast<std::int16_t>(source_u16(game, 0xf8)), std::bit_cast<std::int16_t>(source_u16(game, 0xfa))};
        decision.headlight_ticks = {game[0x93], game[0x94], game[0x95]};
        decision.headlights = source_u32(body, 0x500) != 0;
        if (!retain_driver_state || record.id == 0) {
            driver.restore_source_traffic(decision);
        }
        driver.set_traffic_context(traffic_context);
        if (!retain_driver_state || record.id == 0) {
            driver.restore_source_braking_memory(source.braking_memory);
        } else {
            driver.advance_source_braking_notice();
        }
        AiMappedControlState mapped;
        mapped.axes = {source_i32(dynamics, 0x138), source_i32(dynamics, 0x13c), source_i32(dynamics, 0x140), source_i32(dynamics, 0x144)};
        mapped.reversing = observed.reversing;
        mapped.reverse_ticks = game[0x82];
        mapped.reaction_ticks = game[0x87];
        mapped.brake_ticks = game[0x92];
        mapped.gear = std::int8_t(dynamics[0x330]);
        mapped.reverse_pedal = source_i32(dynamics, 0x304);
        mapped.brake_lights = source_u32(body, 0x508) != 0;
        if (!retain_driver_state || record.id == 0) {
            driver.restore_source_controls(mapped);
        } else {
            driver.synchronize_source_physical_controls(mapped.gear, mapped.reverse_pedal, observed.human ? &mapped.axes : nullptr);
        }
        auto& context = control_contexts[actor];
        context.started = source_u32(source.status, 0xc) != 0;
        context.player = observed.player;
        context.suppressed = source_u32(dynamics, 0x2a0) != 0 || source_u32(dynamics, 0x2a4) != 0 || source_u32(dynamics, 0x2a8) != 0;
        context.force_acceleration = source.force_acceleration != 0;
        context.tyre_brake_disabled = source_u32(dynamics, 0x2c4) != 0 || source_u32(dynamics, 0x2c8) != 0 || source_u32(dynamics, 0x2cc) != 0 || source_u32(dynamics, 0x2d0) != 0;
        context.brake_bypass = initial.brake_bypass[observed.next_point] != 0;
        context.obstacle = observed.obstacle;
        context.barrier_hit = source_u32(body, 0x4f8) != 0;
        context.steering_gains = {source_i32(source.tuning, 8), source_i32(source.tuning, 12), source_i32(source.tuning, 24), source_i32(source.tuning, 28)};
        driver.set_control_context(context);
        ai[actor] = !observed.human;
        controls[actor] = ai[actor] || source.race_stop != 0;
    }
    const auto refresh = [&](std::size_t actor) {
        const auto state = drivers[actor]->traffic_decision_state();
        fleet[actor].blocked = state.blocked;
        fleet[actor].passing = state.passing;
        fleet[actor].mode = state.mode;
        fleet[actor].blocker_id = state.blocker_actor;
    };
    //403dd0's stopping inputs are fixed throughout these ascending passes.
    for (std::size_t actor = 0; actor < 16; ++actor) {
        if (ai[actor]) {
            drivers[actor]->decide_traffic(fleet[actor].body, fleet[actor].speed_q16, fleet[actor], fleet, route_length);
            refresh(actor);
        }
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        if (ai[actor]) {
            drivers[actor]->decide_traffic_brake(fleet[actor].speed_q16, fleet[actor], fleet);
            refresh(actor);
        }
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        auto& driver = *drivers[actor];
        if (ai[actor]) {
            driver.advance_source_reverse_recovery(fleet[actor].speed_q16, control_contexts[actor].barrier_hit);
        }
        if (timing && timing->local_session && timing->state == 13 && actor == timing->local_actor && driver.mapped_control_state().reversing) {
            //44a020 calls422a90/422f60 even after local AI handoff. Its analog
            //device scratch is not captured;405530 overwrites it before use
            //only on the non-reversing branch supported by this profile.
            throw UnsupportedSourceState("Local state13 reverse steering requires the uncaptured input-device scratch");
        }
        const auto state = driver.traffic_decision_state();
        const auto mapped = driver.mapped_control_state();
        traffic_output << record.id << ',' << actor << ',' << unsigned(state.mode) << ',' << state.blocked << ',' << state.brake << ',' << state.passing << ','
                       << state.steering_cached << ',' << state.forced_brake << ',' << state.passed_actor << ',' << state.blocker_actor;
        for (const auto value : state.corridor) {
            traffic_output << ',' << value;
        }
        for (const auto value : state.corners) {
            traffic_output << ',' << unsigned(value);
        }
        for (const auto value : state.obstacle_angles) {
            traffic_output << ',' << value;
        }
        for (const auto value : state.headlight_ticks) {
            traffic_output << ',' << unsigned(value);
        }
        traffic_output << ',' << state.headlights << ',' << mapped.reversing << ',' << unsigned(mapped.reverse_ticks) << ',' << unsigned(mapped.gear) << ',' << mapped.reverse_pedal
                       << ',' << mapped.axes.left_q16 << ',' << mapped.axes.right_q16 << '\n';
    }
    traffic_world << record.id << ',' << random.state() << ',' << source_u32(source.status, 20) + 1u << '\n';
    auto steering_scratch = source_u32(source.status, 16);
    for (std::size_t actor = 16; actor-- > 0;) {
        if (controls[actor]) {
            const auto state = drivers[actor]->mapped_control_state();
            const auto& context = control_contexts[actor];
            if (state.reaction_ticks == 0 && context.started && !context.suppressed) {
                steering_scratch = 0;
            }
            drivers[actor]->map_source_controls(fleet[actor].body, fleet[actor].speed_q16);
        }
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        if (!controls[actor]) {
            continue;
        }
        const auto state = drivers[actor]->traffic_decision_state();
        const auto mapped = drivers[actor]->mapped_control_state();
        const auto& axes = mapped.axes;
        control_output << record.id << ',' << actor << ',' << axes.left_q16 << ',' << axes.right_q16 << ',' << axes.throttle_q16 << ',' << axes.brake_q16 << ',' << mapped.reversing
                       << ',' << unsigned(mapped.reverse_ticks) << ',' << unsigned(mapped.reaction_ticks) << ',' << unsigned(mapped.brake_ticks) << ',' << unsigned(mapped.gear)
                       << ',' << mapped.reverse_pedal << ',' << unsigned(state.mode) << ',' << state.steering_cached << ',' << state.obstacle_angles[0] << ','
                       << state.obstacle_angles[1] << ',' << state.headlights << ',' << mapped.brake_lights << '\n';
    }
    control_world << record.id << ',' << random.state() << ',' << steering_scratch << '\n';
    if (generated) {
        source_store(generated->traffic.status, 16, steering_scratch);
        source_store(generated->traffic.status, 20, source_u32(source.status, 20) + 1u);
        for (std::size_t actor = 0; actor < 16; ++actor) {
            auto& raw = generated->traffic.actors[actor];
            auto& game = raw.gameplay;
            const auto traffic = drivers[actor]->traffic_decision_state();
            const auto control = drivers[actor]->mapped_control_state();
            const auto route = drivers[actor]->route_state();
            if (ai[actor]) {
                source_store(game, 0x18, traffic.blocked);
                source_store(game, 0x1c, traffic.brake);
                source_store(game, 0x28, control.reversing);
                source_store(game, 0x2c, traffic.passing);
                source_store(game, 0x34, traffic.steering_cached);
                source_store(game, 0x48, traffic.forced_brake);
                game[0x81] = traffic.mode;
                game[0x82] = control.reverse_ticks;
                for (std::size_t index = 0; index < 4; ++index) {
                    source_store(game, 0x70 + index * 4, traffic.corridor[index]);
                }
                game[0x98] = std::uint8_t(traffic.passed_actor);
                game[0x99] = std::uint8_t(traffic.blocker_actor);
                game[0x9f] = traffic.corners[0];
                game[0xa0] = traffic.corners[1];
                for (std::size_t index = 0; index < 3; ++index) {
                    game[0x93 + index] = traffic.headlight_ticks[index];
                }
                source_store(raw.body, 0x500, traffic.headlights);
            }
            if (controls[actor]) {
                game[0x87] = control.reaction_ticks;
                game[0x92] = control.brake_ticks;
                source_store(game, 0x34, traffic.steering_cached);
                source_store(game, 0xf8, std::uint16_t(traffic.obstacle_angles[0]), 2);
                source_store(game, 0xfa, std::uint16_t(traffic.obstacle_angles[1]), 2);
                source_store(game, 0x102, std::uint16_t(route.primary_boundary_angle), 2);
                source_store(game, 0x104, std::uint16_t(route.alternate_boundary_angle), 2);
                for (const auto& [offset, value] : std::array<std::pair<std::size_t, std::int32_t>, 5>{{{0x138, control.axes.left_q16}, {0x13c, control.axes.right_q16},
                         {0x140, control.axes.throttle_q16}, {0x144, control.axes.brake_q16}, {0x304, control.reverse_pedal}}}) {
                    source_store(raw.dynamics, offset, std::uint32_t(value));
                }
                raw.dynamics[0x330] = std::uint8_t(control.gear);
                source_store(raw.body, 0x508, control.brake_lights);
            }
        }
    }
}

void compare_joined_ai(const AiInitialState& initial, const Record& record, std::uint32_t local, RaceReleaseCounter& release, std::ostream& route_output,
    std::ostream& progress_output, std::ostream& joined_world, std::ostream& traffic_output, std::ostream& traffic_world, std::ostream& control_output, std::ostream& control_world,
    SourceRandom* shared_random, bool warm_first, RetainedDrivers* retained, std::int32_t* retained_launch, RetainedRaceTiming* timing, std::ostream* actor_timing,
    std::ostream* checkpoint_output, std::ostream* finish_output, std::ostream* display_output) {
    const auto& before = record.traffic;
    const bool local_session = timing && timing->local_session;
    if (before.source_mode != 0 || (local_session ? before.race_stop > 1 : before.race_stop != 0)
        || (before.session_state != 6 && before.session_state != 9 && !(local_session && (before.session_state == 10 || before.session_state == 13)))
        || source_u32(before.status, 0) != 0 || initial.rank_threshold <= 0 || initial.cumulative_route.size() != source_u32(initial.route, 0)) {
        throw UnsupportedSourceState("Joined AI requires an ordinary active no-qualifying/no-finish race and initialized rank/distance data");
    }
    if (record.id == 0 && !warm_first && (before.session_state != 9 || record.update_caller != 0x4488a1)) {
        throw UnsupportedSourceState("Joined interval must start at the observed460170 cold-grid setup");
    }
    for (const auto& event : record.releases) {
        if (event.return_address != 0x4220a6 || event.source_mode != 0 || (event.stage != 2 && event.stage != 4) || event.local_players != 1 || release.value() != 0) {
            throw UnsupportedSourceState("Joined release requires the ordinary4220a6 countdown caller");
        }
        release.enter(std::int32_t(event.stage), event.source_mode, event.local_players);
    }
    if (local_session) {
        const auto expected = before.session_state == 9    ? timing->warm_grid_first || (timing->cold_start && record.id == 0) ? 0x4488a1u : 0x4488d0u
                              : before.session_state == 6  ? 0x448434u
                              : before.session_state == 10 ? 0x448c46u
                                                           : 0x44a336u;
        if (record.update_caller != expected) {
            throw UnsupportedSourceState("Observed wrapper disagrees with generated local session state");
        }
        if (record.id != 0 && before.session_state == 6) {
            release.before_update(6);
        }
    } else if (record.update_caller == 0x448434) {
        if (before.session_state != 6) {
            throw UnsupportedSourceState("4482d0 release decrement requires original state6 dispatch");
        }
        if (record.id != 0 || !warm_first) {
            release.before_update(6);
        }
    } else if (before.session_state != 9 || (record.update_caller != 0x4488a1 && record.update_caller != 0x4488d0)) {
        throw UnsupportedSourceState("Unknown joined grid/active wrapper caller");
    }
    joined_world << record.id << ',' << release.value() << '\n';
    auto working = record;
    if (retained) {
        if (!retained->initialized) {
            throw std::runtime_error("Retained drivers require one initial PRE state");
        }
        working.traffic.status = retained->status;
        working.traffic.braking_memory = retained->braking;
        for (std::size_t actor = 0; actor < 16; ++actor) {
            auto& current = working.traffic.actors[actor];
            const auto& previous = retained->actors[actor];
            if (retained->retain_drivetrain) {
                current.dynamics[0x330] = previous.dynamics[0x330];
                current.dynamics[0x334] = previous.dynamics[0x334];
                source_store(current.dynamics, 0x30c, source_u32(previous.dynamics, 0x30c));
                source_store(current.dynamics, 0x304, source_u32(previous.dynamics, 0x304));
            }
            if (retained->retain_rpm) {
                source_store(current.dynamics, 0x124, source_u32(previous.dynamics, 0x124));
                source_store(current.dynamics, 0x128, source_u32(previous.dynamics, 0x128));
            }
            if (retained->retain_pedals) {
                for (const auto offset : {0x14, 0x38, 0x120, 0x134, 0x11c, 0x130}) {
                    source_store(current.dynamics, offset, source_u32(previous.dynamics, offset));
                }
            }
            if (retained->retain_steering) {
                source_store(current.dynamics, 0x44, source_u32(previous.dynamics, 0x44));
                source_store(current.dynamics, 0x150, source_u32(previous.dynamics, 0x150));
                source_store(current.dynamics, 0x274, source_u16(previous.dynamics, 0x274), 2);
            }
            if (retained->retain_physical) {
                //The retained view carries native ground state into route and
                //traffic decisions. Dispatch is observed unless the pre-AI selector owns it.
                copy_physical_fields(current, previous);
                auto& ai = working.ai_actors[actor];
                ai.speed_q16 = source_i32(previous.body, 0xe0);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    ai.body.position[axis] = source_i32(previous.body, 0xbc + axis * 4);
                    for (std::size_t row = 0; row < 3; ++row) {
                        ai.body.basis[row][axis] = source_i32(previous.body, 0x150 + row * 12 + axis * 4);
                    }
                }
            }
            if (retained->retain_dispatch) {
                source_store(current.body, 0x4f0, source_u32(previous.body, 0x4f0));
                working.ai_actors[actor].body_mode = source_u32(previous.body, 0x4f0);
            }
            current.gameplay = previous.gameplay;
            if (local_session) {
                for (const auto offset : {0x27c, 0x310, 0x314, 0x318}) {
                    source_store(current.dynamics, offset, source_u32(previous.dynamics, offset));
                }
            }
            working.ai_actors[actor].gameplay = previous.gameplay;
            for (const auto offset : {0x500, 0x508}) {
                source_store(current.body, offset, source_u32(previous.body, offset));
            }
            for (std::size_t offset = 0xdc; offset < 0xfc; offset += 4) {
                source_store(current.dynamics, offset, source_u32(previous.dynamics, offset));
            }
            if (retained->retain_published_brake) {
                source_store(current.body, 0x4e8, source_u32(previous.body, 0x4e8));
                source_store(current.dynamics, 0x144, source_u32(previous.dynamics, 0x144));
            }
            if (source_u32(previous.gameplay, 8) == 0 || retained->retain_dispatch) {
                //Traffic runs inside403dd0, before this update's422f60 human
                //mapper. Its human axes are the previous retained PRE axes.
                for (std::size_t offset = 0x138; offset < 0x148; offset += 4) {
                    source_store(current.dynamics, offset, source_u32(previous.dynamics, offset));
                }
            }
        }
        if (working.traffic.braking_memory[256] != 0) {
            --working.traffic.braking_memory[256];
        }
    }
    working.ai_release_counter = release.value(); //Derived from initial setup/events, never an observed later value.
    working.traffic.release_counter = release.value();
    std::optional<SourceRandom> independent;
    if (!shared_random) {
        independent.emplace(before.rng); //The sole imported seed for an independent403dd0..415090 step.
    }
    auto& random = shared_random ? *shared_random : *independent;
    const auto tire_set = [](SourceSnapshot& raw, std::int32_t value) {
        for (std::size_t tire = 0; tire < 8; ++tire) {
            source_store(raw.dynamics, 0xdc + tire * 4, std::uint32_t(value));
        }
    };
    for (std::size_t actor = 0; actor < 16; ++actor) {
        auto& raw = working.traffic.actors[actor];
        auto& game = raw.gameplay;
        const auto player = std::bit_cast<std::int8_t>(game[0x80]);
        if (source_u32(game, 4) != 1 || source_u32(game, 8) > 1 || source_u32(game, 0x30) != 0
            || (timing ? source_u32(game, 0x54) > 1 || source_u16(game, 0xa8) > 99 || (!local_session && player >= 0 && source_u32(game, 0x54) != 0)
                       : source_u32(game, 0x54) != 0 || source_u16(game, 0xa8) != 1)
            || game[0x8d] != actor || player < -1 || player > 3) {
            throw UnsupportedSourceState("Joined AI requires sixteen active no-pit actors before local finish and explicit lap/finish provenance");
        }
        if (source_u32(game, 8) == 0) {
            if (game[0x83] == 0) {
                const auto old = source_i32(raw.dynamics, 0xdc);
                tire_set(raw, old < record.ai_tire_base ? fixed::add(old, 0x62) : record.ai_tire_base);
            } else {
                --game[0x83];
            }
        }
        if (player >= 0) {
            for (const auto offset : {0x88, 0x8b, 0x8c, 0x8a}) {
                if (game[offset] != 0) {
                    --game[offset];
                }
            }
            const auto& local_body = retained && retained->retain_physical ? working.traffic.actors[local].body : before.actors[local].body;
            const auto forward =
                fixed::add(fixed::mul(source_i32(local_body, 0xc8), source_i32(local_body, 0x150)), fixed::mul(source_i32(local_body, 0xd0), source_i32(local_body, 0x158)));
            if (source_u32(game, 0x10) == 0) {
                source_store(game, 0x4c, 0);
                game[0x89] = 0;
            } else if (forward > 0) {
                if (game[0x89] < 160) {
                    ++game[0x89];
                }
                if (source_i32(game, 0xcc) > 35 * fixed::one) {
                    source_store(game, 0x4c, 1);
                }
            }
            //403cf0 is a no-op in ordinary Single Race (source ad040==0).
        }
        source_store(game, 0x34, 0); //The active no-pit prepass clears the steering cache.
        if (retained) {
            LapTimingState clock;
            clock.record_flag = source_u32(game, 0);
            clock.current_ms = source_u32(game, 0xc4);
            clock.lap_start_ms = source_u32(game, 0xc8);
            for (std::size_t part = 0; part < 3; ++part) {
                clock.current_display[part] = std::bit_cast<std::int16_t>(source_u16(game, 0xb0 + part * 2));
            }
            clock = observe_lap_clock(clock, record.timing_clock, source_u32(working.traffic.status, 8) != 0, player >= 0, before.session_state);
            source_store(game, 0, clock.record_flag);
            source_store(game, 0xc4, clock.current_ms);
            for (std::size_t part = 0; part < 3; ++part) {
                source_store(game, 0xb0 + part * 2, std::uint16_t(clock.current_display[part]), 2);
            }
            if (timing) {
                //The complete clock/notice pass precedes421390. Publish only
                //those results; route cadence, heading and gates still use PRE.
                auto& route_game = working.ai_actors[actor].gameplay;
                source_store(route_game, 0, clock.record_flag);
                source_store(route_game, 0xc4, clock.current_ms);
                std::copy_n(game.begin() + 0xb0, 6, route_game.begin() + 0xb0);
                route_game[0x8a] = game[0x8a];
                route_game[0x8c] = game[0x8c];
            }
        }
    }
    //The route consumer retains PRE cadence/heading/gates; the optional timing
    //profile carries only the preceding clock/notice pass into its lap calls.
    AiDrivers independent_drivers;
    auto& drivers = retained ? retained->drivers : independent_drivers;
    AiRouteStepResults route;
    if (timing) {
        if (record.id != 0 || timing->cold_start) {
            ++timing->finish.timer3; //421aa0 increments once, before403dd0;421390 may reset it below.
        }
        working.finish = timing->finish;
    }
    compare_ai_route(initial, working, route_output, actor_timing, checkpoint_output, finish_output, display_output, &drivers, &random, &route, retained != nullptr, timing,
        timing ? &working : nullptr);
    const auto length = source_i32(before.tuning, 0);
    if (length <= 0 || initial.rank_threshold != length / 2) {
        throw UnsupportedSourceState("Joined rank threshold disagrees with original half-route setup");
    }
    std::array<SourceRaceOrderActor, 16> order;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        auto& raw = working.traffic.actors[actor];
        auto& game = raw.gameplay;
        const auto& target = route.routes[actor];
        const auto& progress = route.progress[actor];
        const auto& cadence = route.cadence[actor];
        source_store(game, 0xc, progress.backwards);
        source_store(game, 0x3c, cadence.refresh);
        game[0x86] = cadence.ticks;
        if (retained && initial.pit_entry && (cadence.refresh || source_u32(game, 8) != 0)) {
            //403dd0→402910 selector2 writes the entry gate even while
            //ordinary route driving remains outside the pit lane.
            game[0x9c] = PitLane::source_zone(*initial.pit_entry, working.ai_actors[actor].body.position);
        }
        game[0x9a] = progress.current_gate;
        game[0x9b] = progress.previous_gate;
        game[0xa6] = progress.checkpoint;
        game[0x9d] = progress.checkpoint_marker;
        source_store(game, 0xa2, progress.cursor.next, 2);
        source_store(game, 0xa4, progress.cursor.previous, 2);
        game[0x9e] = std::uint8_t(target.cached_point);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            source_store(game, 0x108 + axis * 4, std::uint32_t(target.target[axis]));
            source_store(game, 0x114 + axis * 4, std::uint32_t(target.alternate_target[axis]));
        }
        source_store(game, 0xd0, std::uint32_t(target.desired_speed_q16));
        source_store(game, 0xd4, std::uint32_t(target.alternate_speed_q16));
        source_store(game, 0xd8, std::uint32_t(target.speed_variation_q16));
        source_store(game, 0xe0, std::uint32_t(target.distance_q16));
        source_store(game, 0xe4, std::uint32_t(target.projected_distance_q16));
        source_store(game, 0xe8, std::uint32_t(target.projected_distance_q16));
        if (retained) {
            source_store(game, 0xec, 0);
        }
        source_store(game, 0xfc, std::uint16_t(target.angle), 2);
        source_store(game, 0xfe, std::uint16_t(target.alternate_angle), 2);
        source_store(game, 0x100, std::uint16_t(target.route_angle), 2);
        if (cadence.refresh) {
            if (retained) {
                const auto degrees = (std::int32_t(target.route_angle) * 0x1680) >> 16;
                source_store(game, 0x10, degrees >= 121 || degrees <= -121);
            }
            const auto corridor = drivers[actor]->source_route_corridor(working.ai_actors[actor].body, progress.target_cursor.next, progress.cursor.previous);
            source_store(game, 0x20, corridor[0]);
            source_store(game, 0x24, corridor[1]);
        }
        source_store(game, 0xcc, std::uint32_t(fixed::mul(working.ai_actors[actor].speed_q16, 0x640000)));
        const auto distance = source_route_distance(initial.cumulative_route.at(progress.target_cursor.next), target.projected_distance_q16, length);
        const auto delta = fixed::abs(fixed::sub(distance, source_i32(game, 0xdc)));
        source_store(game, 0xdc, std::uint32_t(distance));
        source_store(game, 0xf4, std::uint32_t(delta));
        if (game[0x8f] < 1 || game[0x8f] > 16 || game[0x90] >= 16 || game[0x96] >= 16 || game[0x97] >= 16) {
            throw UnsupportedSourceState("Joined race order exceeds the original rank/ring allocation");
        }
        order[actor] = {true, source_u32(game, 0x54) != 0, progress.backwards, std::bit_cast<std::int16_t>(source_u16(game, 0xa8)), game[0x8f], game[0x90], game[0x96], game[0x97],
            distance, delta};
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto& raw = working.traffic.actors[actor];
        auto& game = raw.gameplay;
        const auto old_rank = order[actor].rank;
        const auto old_ahead = advance_source_race_order(order, actor, initial.rank_threshold);
        if (old_rank == order[actor].rank) {
            ++game[0x91];
        }
        if (game[0x91] == 16) {
            if (before.session_state == 6) {
                game[0x8e] = std::uint8_t(order[actor].rank);
            }
            game[0x91] = 0;
        }
        source_store(game, 0x14, 0);
        const auto contact_ahead = source_u32(record.pair_history, (actor * 16 + order[actor].ahead) * 8 + 4) != 0;
        const auto contact_behind = source_u32(record.pair_history, (actor * 16 + order[actor].behind) * 8 + 4) != 0;
        source_store(game, 0x40, contact_ahead);
        source_store(game, 0x44, contact_behind);
        const auto human = [&](std::uint32_t id) {
            return source_u32(working.traffic.actors[id].gameplay, 8) != 0;
        };
        if (!human(actor) && (contact_behind ? human(order[actor].behind) : contact_ahead && human(old_ahead))) {
            game[0x83] = 25;
            tire_set(raw, -std::int32_t(order[actor].rank) * 0x147 - 0x28f);
        }
        const auto ahead = order[actor].ahead;
        //404811 leaves the last active route actor's body in [ebp-34].
        //404b07 reuses that pointer for the short player-gap distance.
        const auto& gap_position = working.ai_actors.back().body.position; //All sixteen actors are required active above.
        const auto gap = source_forward_gap(order[actor].distance_q16, order[old_ahead].distance_q16, length, gap_position, working.ai_actors[ahead].body.position,
            std::bit_cast<std::int8_t>(working.traffic.actors[ahead].gameplay[0x80]) >= 0, ahead == actor);
        source_store(game, 0xf0, std::uint32_t(gap));
    }
    //Later actors may change earlier actors' rank/ring; publish the final order
    //after the whole source pass while retaining each actor's earlier gap.
    for (std::size_t actor = 0; actor < 16; ++actor) {
        auto& raw = working.traffic.actors[actor];
        auto& game = raw.gameplay;
        game[0x8f] = std::uint8_t(order[actor].rank);
        game[0x90] = std::uint8_t(order[actor].spatial_rank);
        game[0x96] = std::uint8_t(order[actor].ahead);
        game[0x97] = std::uint8_t(order[actor].behind);
        progress_output << record.id << ',' << actor << ',' << source_i32(game, 0xdc) << ',' << source_i32(game, 0xf4) << ',' << unsigned(game[0x8f]) << ',' << unsigned(game[0x90])
                        << ',' << unsigned(game[0x8e]) << ',' << unsigned(game[0x91]) << ',' << unsigned(game[0x96]) << ',' << unsigned(game[0x97]) << ',' << source_u32(game, 0x40)
                        << ',' << source_u32(game, 0x44) << ',' << source_u32(game, 0x14) << ',' << unsigned(game[0x83]) << ',' << source_i32(raw.dynamics, 0xdc) << '\n';
    }
    //No source intermediate state or RNG is restored here. These SAME drivers
    //carry generated targets and the SAME stream into traffic and controls.
    if (timing) {
        working.traffic.race_stop = timing->finish.stopped;
        if (local_session) {
            timing->controller_checkpoint = working.traffic.actors[local];
            timing->launch_at_ai_return = retained_launch ? *retained_launch : 0;
        }
        for (std::size_t actor = 0; actor < 16; ++actor) {
            drivers[actor]->synchronize_source_finish(source_u32(working.traffic.actors[actor].gameplay, 0x54) != 0);
        }
    }
    compare_traffic_controls(initial, working, traffic_output, traffic_world, control_output, control_world, &drivers, &random, retained && !retained->reset_controls,
        retained ? &working : nullptr, timing);
    if (retained) {
        retained->reset_controls = false;
    }
    if (timing) {
        timing->consume_menu_input();
    }
    if (retained_launch && (!local_session || source_u32(working.traffic.actors[local].gameplay, 8) != 0)) {
        auto& raw = working.traffic.actors[local];
        std::array<std::int32_t, 8> tires;
        for (std::size_t tire = 0; tire < tires.size(); ++tire) {
            tires[tire] = source_i32(raw.dynamics, 0xdc + tire * 4);
        }
        //422f60's launch prephase precedes the separately observed human axes.
        //The selected keys-only interval excludes the pause/menu bypass gate.
        advance_source_launch_tires(*retained_launch, tires);
        for (std::size_t tire = 0; tire < tires.size(); ++tire) {
            source_store(raw.dynamics, 0xdc + tire * 4, std::uint32_t(tires[tire]));
        }
    }
    if (retained) {
        retained->actors = working.traffic.actors;
        retained->status = working.traffic.status;
        retained->braking = working.traffic.braking_memory;
    }
}

} // namespace toca::test::fleet
