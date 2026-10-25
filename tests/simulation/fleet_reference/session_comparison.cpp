#include "session_comparison.hpp"

#include "binary_io.hpp"
#include "diagnostics.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

void write_handoff_header(std::ostream& output) {
    output << "id,event,actor,kind,return_address,rng_final,human,reverse_fallback,countdown,player,gear,queued_shift,mode_steering,mode_throttle,mode_brake,mode_transmission,"
              "saved_steering,saved_throttle,saved_brake,saved_transmission";
    for (unsigned index = 0; index < 8; ++index) {
        output << ",tire_offset_" << index;
    }
    output << '\n';
}

void compare_local_handoffs(const Record& record, std::ostream& output) {
    for (std::size_t index = 0; index < record.handoffs.size(); ++index) {
        const auto& event = record.handoffs[index];
        //These exact return PCs distinguish421390's three local transactions
        //from startup, pit, removed-player and other402be0/402c80 callers.
        const bool verified = (event.kind == 0 && (event.return_address == 0x42160b || event.return_address == 0x4217aa)) || (event.kind == 1 && event.return_address == 0x421827);
        if (!verified || event.actor >= 16 || event.before.player < 0 || event.before.player > 3) {
            throw UnsupportedSourceState("Local handoff lacks a supported421390 call/actor provenance");
        }
        ControllerHandoffState state;
        try {
            state = apply_controller_handoff(event.before, ControllerHandoffKind(event.kind), event.ai_tire_base);
        } catch (const std::runtime_error& error) {
            throw UnsupportedSourceState(std::string("Unsupported local controller transaction: ") + error.what());
        }
        //The two source functions draw no RNG and do no filter/engine update.
        output << record.id << ',' << index << ',' << event.actor << ',' << event.kind << ',' << event.return_address << ',' << event.rng << ',' << state.human << ','
               << state.reverse_fallback << ',' << state.countdown << ',' << state.player << ',' << state.gear << ',' << state.queued_shift;
        for (const auto& modes : {state.modes, state.saved_modes}) {
            for (const auto value : modes) {
                output << ',' << value;
            }
        }
        for (const auto value : state.tires) {
            output << ',' << value;
        }
        output << '\n';
    }
}

void write_session_timing(const Record& record, const std::array<LapTimingState, 16>& timing_results, const std::array<ActorFinishState, 16>& actor_finishes,
    const CheckpointTimes& checkpoint_times, std::uint32_t timing_flags, const RaceFinishState& finish, const SplitDisplays& displays, std::ostream* actor_timing,
    std::ostream* shared_timing, std::ostream* finish_output, std::ostream* display_output) {
    const bool timing = true, compare_finish = true;
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

void write_retained_local_session(const Record& record, const RetainedRaceTiming& timing, std::uint32_t release_counter, std::ostream& session, std::ostream& controller,
    std::ostream& classification, std::ostream& handoffs) {
    const auto& raw = timing.controller_checkpoint;
    const auto& game = raw.gameplay;
    const auto& dynamics = raw.dynamics;
    session << record.id << ',' << release_counter << ',' << timing.release_clock << ',' << timing.running << ',' << timing.launch_at_ai_return << ",0,0,0\n";
    controller << record.id << ',' << timing.local_actor << ',' << source_u32(game, 8) << ',' << source_u32(game, 0x28) << ',' << unsigned(game[0x83]) << ','
               << int(std::bit_cast<std::int8_t>(game[0x80])) << ',' << unsigned(dynamics[0x330]) << ',' << source_u32(dynamics, 0x14c);
    for (std::size_t part = 0; part < 4; ++part) {
        controller << ',' << source_i32(dynamics, part == 3 ? 0x27c : 0x310 + part * 4);
    }
    for (std::size_t part = 0; part < 4; ++part) {
        controller << ',' << source_i32(game, 0x60 + part * 4);
    }
    for (std::size_t tire = 0; tire < 8; ++tire) {
        controller << ',' << source_i32(dynamics, 0xdc + tire * 4);
    }
    controller << ',' << source_i32(dynamics, 0x138) << ',' << source_i32(dynamics, 0x13c) << ',' << std::bit_cast<std::int16_t>(source_u16(dynamics, 0x274)) << '\n';
    classification << record.id << ',' << timing.release_marker;
    for (const auto flag : timing.release_flags) {
        classification << ',' << flag;
    }
    for (std::size_t player = 0; player < 4; ++player) {
        classification << ',' << source_u32(timing.split_enable, player * 4);
    }
    for (const auto actor : timing.classification_order) {
        classification << ',' << unsigned(actor);
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        classification << ',' << std::bit_cast<std::int16_t>(source_u16(timing.configuration_tail, 2 + actor * 2));
    }
    for (std::size_t podium = 0; podium < 3; ++podium) {
        classification << ',' << unsigned(timing.configuration_tail[0x2c + podium]);
    }
    classification << ',' << source_u32(timing.configuration_tail, 0x28) << '\n';
    if (record.handoffs.size() != unsigned(timing.handoff.has_value())) {
        throw UnsupportedSourceState("Observed controller calls disagree with generated local finish");
    }
    if (timing.handoff) {
        const auto& event = record.handoffs.front();
        if (event.kind != 0 || event.actor != timing.local_actor || event.return_address != 0x42160b) {
            throw UnsupportedSourceState("Generated local finish lacks its actual421390 to402be0 call chronology");
        }
        const auto& state = *timing.handoff;
        handoffs << record.id << ",0," << timing.local_actor << ",0," << event.return_address << ',' << timing.handoff_rng << ',' << state.human << ',' << state.reverse_fallback
                 << ',' << state.countdown << ',' << state.player << ',' << state.gear << ',' << state.queued_shift;
        for (const auto& modes : {state.modes, state.saved_modes}) {
            for (const auto value : modes) {
                handoffs << ',' << value;
            }
        }
        for (const auto value : state.tires) {
            handoffs << ',' << value;
        }
        handoffs << '\n';
    }
}

void prepare_retained_race_timing(Record& record, RetainedRaceTiming& timing) {
    if (record.timing_clock != 0 && (!timing.warm_restart || timing.cold_start)) {
        throw std::runtime_error("Retained race timing accepts its external clock only from the AI journal");
    }
    if (!timing.initialized) {
        const auto initial_state = timing.cold_start ? 9 : 6;
        if (record.timing_state != initial_state || (record.timing_stage != 2 && record.timing_stage != 4) || record.timing_lap_limit < 1 || record.timing_lap_limit > 99
            || record.timing_running != unsigned(!timing.cold_start) || ((record.timing_flags >> 16) & 0xff) > 1 || record.source_mode != 0 || record.record_playback != 0
            || (record.finish.count & 0xff) > 16 || (record.finish.local_count & 0xff) != 0 || record.finish.stopped != 0
            || (record.finish.pending_state & 0xffff) != unsigned(initial_state)
            || std::ranges::any_of(record.finish.local_finished,
                [](auto flag) {
                    return flag != 0;
                })
            || record.traffic.session_state != (timing.cold_start ? 0 : 6) || record.traffic.race_stop != 0
            || source_u32(record.traffic.status, 8) != unsigned(!timing.cold_start)) {
            throw UnsupportedSourceState("Retained race timing requires an ordinary running NoQual interval before local finish");
        }
        if (timing.cold_start
            && ((record.finish.count & 0xff) != 0 || record.finish.timer2 != 0 || record.finish.timer3 != 0 || record.finish_release_clock != 0 || record.release_marker != 0
                || record.release_flags != std::array<std::uint32_t, 4>{0, 1, 0, 1})) {
            throw std::runtime_error("Cold retained timing disagrees with the460170/421290 reset writers");
        }
        const auto& local_display = record.displays.players[0];
        if (timing.cold_start
            && (local_display.mode != 0 || local_display.leader != -1 || local_display.active != 0 || local_display.ticks != 0
                || local_display.components != std::array<std::int16_t, 3>{})) {
            throw std::runtime_error("Cold retained display disagrees with the41a5d0 reset writer");
        }
        if (std::ranges::any_of(record.displays.players, [](const auto& display) {
                return display.active != 0;
            })) {
            throw UnsupportedSourceState("Retained race timing excludes active frame-owned split display lifetimes");
        }
        timing.initialized = true;
        timing.stage = record.timing_stage;
        timing.state = record.timing_state;
        timing.lap_limit = record.timing_lap_limit;
        timing.flags = record.timing_flags;
        timing.running = record.timing_running;
        timing.release_clock = record.finish_release_clock;
        timing.finish = record.finish;
        timing.checkpoints = record.checkpoint_times;
        timing.displays = record.displays;
        if (timing.local_session) {
            if (!record.session_extended || record.lap_distance <= 0) {
                throw UnsupportedSourceState("Retained local finish requires initial typed classification state");
            }
            timing.classification_order = record.classification_order;
            timing.split_enable = record.split_enable_raw;
            timing.configuration_tail = record.configuration_tail;
            timing.lap_distance = record.lap_distance;
            timing.release_marker = record.release_marker;
            if (record.release_flags[1] != timing.release_flags[1]) {
                throw std::runtime_error("Initial typed af050 alias disagrees with lifecycle entry flag");
            }
            timing.release_flags = record.release_flags;
        }
    } else {
        const auto nonzero = [](const auto& values) {
            return std::ranges::any_of(values, [](auto value) {
                return value != 0;
            });
        };
        bool populated = record.timing_stage || record.timing_state || record.timing_lap_limit || record.timing_flags || record.timing_running || record.source_mode
                         || record.record_playback || record.finish_release_clock || record.finish.count || record.finish.local_count || record.finish.timer2
                         || record.finish.timer3 || record.finish.pending_state || record.finish.stopped || nonzero(record.finish.local_finished) || nonzero(record.finish.order)
                         || nonzero(record.displays.persisted) || record.traffic.session_state || record.traffic.race_stop;
        for (const auto& time : record.checkpoint_times) {
            populated |= time.clock != 0 || time.lap != 0 || time.actor != 0;
        }
        for (const auto& display : record.displays.players) {
            populated |= display.mode != 0 || display.leader != 0 || display.active != 0 || display.ticks != 0 || nonzero(display.components) || nonzero(display.reference)
                         || nonzero(display.current) || nonzero(display.text) || nonzero(display.leader_text);
        }
        if (populated) {
            throw std::runtime_error("Later timing/finish/display outputs cannot enter a retained race replay");
        }
    }
    record.timing_stage = timing.stage;
    record.timing_state = timing.state;
    record.timing_lap_limit = timing.lap_limit;
    record.timing_flags = timing.flags;
    record.timing_running = timing.running;
    record.finish_release_clock = timing.release_clock;
    record.finish = timing.finish;
    record.checkpoint_times = timing.checkpoints;
    record.displays = timing.displays;
    record.traffic.session_state = timing.state;
    record.traffic.race_stop = timing.finish.stopped;
    if (timing.local_session) {
        const auto nonzero = [](const auto& values) {
            return std::ranges::any_of(values, [](auto value) {
                return value != 0;
            });
        };
        if (nonzero(record.session_dynamics) || record.session_ai_tire_base || nonzero(record.session_launch)) {
            throw std::runtime_error("Retained local controller cannot import duplicated session dynamics/tire state");
        }
        if (record.id != 0 && record.session_extended
            && (nonzero(record.classification_order) || nonzero(record.split_enable_raw) || nonzero(record.configuration_tail) || record.lap_distance || record.release_marker
                || nonzero(record.release_flags))) {
            throw std::runtime_error("Later classification outputs cannot enter retained local finish");
        }
        for (const auto& event : record.handoffs) {
            const auto& before = event.before;
            if (event.rng || event.ai_tire_base || before.human || before.reverse_fallback || before.countdown || before.player || before.gear || before.queued_shift
                || nonzero(before.modes) || nonzero(before.saved_modes) || nonzero(before.tires)) {
                throw std::runtime_error("Observed controller handoff PRE/RNG cannot enter retained local finish");
            }
        }
    }
}

void compare_session_boundaries(const AiInitialState& initial, const Record& record, std::uint32_t local, std::ostream& actor_output, std::ostream& checkpoint_output,
    std::ostream& finish_output, std::ostream& display_output, std::ostream& session_output, std::ostream& controller_output, std::ostream& classification_output) {
    //403dd0 continues countdowns and clocks in results state13. Its component
    //formatting excludes only10/7, while421390 separately returns for13 before
    //lap/finish/classification writes (tourcars.c9106..9227,30013..30029).
    if (record.source_mode > 1 || record.selection_context.local_players != 1 || (record.timing_stage < 1 || record.timing_stage > 4)
        || (record.timing_state != 4 && record.timing_state != 6 && record.timing_state != 7 && record.timing_state != 8 && record.timing_state != 9 && record.timing_state != 10
            && record.timing_state != 13)
        || record.timing_running > 1 || record.timing_lap_limit < 1 || record.timing_lap_limit > 99 || (record.finish.count & 0xff) > 16) {
        throw UnsupportedSourceState("Session boundary requires ordinary one-player race/qualifying lifecycle inputs");
    }
    auto finish = record.finish;
    auto displays = record.displays;
    auto checkpoint_times = record.checkpoint_times;
    auto timing_flags = record.timing_flags;
    QualifyingReleaseState release{record.ai_release_counter, record.finish_release_clock, record.timing_running, record.release_marker, record.release_flags};
    auto classification_order = record.classification_order;
    auto split_enable = record.split_enable_raw;
    std::array<std::int16_t, 16> scores;
    std::array<std::uint8_t, 3> podiums;
    for (std::size_t actor = 0; actor < scores.size(); ++actor) {
        scores[actor] = std::bit_cast<std::int16_t>(source_u16(record.configuration_tail, 2 + actor * 2));
    }
    std::copy_n(record.configuration_tail.begin() + 0x2c, 3, podiums.begin());
    std::array<LapTimingState, 16> clocks;
    std::array<ActorFinishState, 16> finishes;
    std::array<FinishClassificationActor, 16> totals;
    const auto& dynamics = record.session_dynamics;
    const auto& local_game = record.ai_actors[local].gameplay;
    ControllerHandoffState controller;
    controller.human = source_u32(local_game, 8);
    controller.reverse_fallback = source_u32(local_game, 0x28);
    controller.countdown = local_game[0x83];
    controller.player = std::bit_cast<std::int8_t>(local_game[0x80]);
    controller.gear = dynamics[0x330];
    controller.queued_shift = source_u32(dynamics, 0x14c);
    for (std::size_t index = 0; index < 3; ++index) {
        controller.modes[index] = source_i32(dynamics, 0x310 + index * 4);
    }
    controller.modes[3] = source_i32(dynamics, 0x27c);
    for (std::size_t index = 0; index < 4; ++index) {
        controller.saved_modes[index] = source_i32(local_game, 0x60 + index * 4);
    }
    for (std::size_t index = 0; index < 8; ++index) {
        controller.tires[index] = source_i32(dynamics, 0xdc + index * 4);
    }
    auto steering_left = source_i32(dynamics, 0x138), steering_right = source_i32(dynamics, 0x13c);
    auto steering_angle = std::bit_cast<std::int16_t>(source_u16(dynamics, 0x274));
    //403dd0's very first pass applies402ac0 before any lap/controller call.
    if (source_u32(local_game, 4) != 0 && controller.human == 0) {
        if (controller.countdown != 0) {
            --controller.countdown;
        } else {
            const auto offset = controller.tires[0] < record.session_ai_tire_base ? fixed::add(controller.tires[0], 0x62) : record.session_ai_tire_base;
            controller.tires.fill(offset);
        }
    }
    //The complete clock pass precedes ALL lap calls, including448130.
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& game = record.ai_actors[actor].gameplay;
        auto& clock = clocks[actor];
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
        finishes[actor] = {source_u32(game, 0x54), source_i32(game, 0x120), game[0x8c], game[0x8a]};
        if (source_u32(game, 4) != 0) {
            const bool player = std::bit_cast<std::int8_t>(game[0x80]) >= 0;
            if (player && finishes[actor].notice != 0) {
                --finishes[actor].notice;
            }
            clock = observe_lap_clock(clock, record.timing_clock, record.timing_running != 0, player, record.timing_state);
            finishes[actor].best_notice = clock.best_notice;
        }
    }
    SourceRandom unused_random(0); //No route targets/traffic/RNG are advanced.
    const std::array<std::vector<std::int16_t>, 3> unused_distributions;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& input = record.ai_actors[actor];
        const auto& game = input.gameplay;
        if (source_u32(game, 4) == 0) {
            continue;
        }
        if (source_u32(game, 4) != 1 || source_u32(game, 0x30) != 0 || game[0x8d] != actor || game[0xa6] >= 6 || game[0x9d] >= 6 || (game[0x9d] & 1)) {
            throw UnsupportedSourceState("Session boundary excludes pit/unknown active checkpoint or actor identity");
        }
        const auto player = std::bit_cast<std::int8_t>(game[0x80]);
        if (player < -1 || player > 3 || (player >= 0 && (actor != local || player != 0))) {
            throw UnsupportedSourceState("Session boundary supports one permanent local player");
        }
        const auto row = std::span<const std::uint8_t>(initial.resolved).subspan(actor * 40, 40);
        AiDriver driver(initial.route, std::span<const std::uint8_t, 40>(row.data(), 40), unused_distributions, 0, 0, unused_random, false, AiRouteEncoding::source_initialized);
        if (game[0x86] > 127 || row[19] > 127) {
            throw UnsupportedSourceState("Session boundary requires bounded source cadence");
        }
        const auto cadence = driver.advance_source_route_cadence(game[0x86]);
        const bool previous = !(player == -1 && source_u32(game, 0xc) == 0 && (source_u32(game, 0x28) == 0 || source_u32(game, 0x54) == 0));
        const AiRouteProgressInput progress_input{
            {source_u16(game, 0xa2), source_u16(game, 0xa4)}, source_u32(game, 0xc) != 0, previous, source_u32(game, 8) != 0, game[0xa6], game[0x9d], source_u32(game, 0x54) != 0};
        const auto progress = driver.advance_source_route_progress(input.body, progress_input, cadence.refresh);
        auto& clock = clocks[actor];
        auto& actor_finish = finishes[actor];
        if (progress.lap_event) {
            if (((timing_flags >> 16) & 0xff) == 0) {
                timing_flags |= 1u << 16;
            }
            const auto result = advance_lap_timing(clock, {record.timing_clock, record.timing_lap_limit, record.timing_stage, record.timing_state, player >= 0,
                                                              player >= 0 && finish.local_finished[std::size_t(player)] != 0, source_u32(game, 8) != 0, false});
            clock = result.state;
            actor_finish.best_notice = clock.best_notice;
            if (result.action == LapTimingAction::qualifying_restart) {
                controller = apply_controller_handoff(controller, ControllerHandoffKind::player, record.session_ai_tire_base);
                if (record.timing_state == 4) {
                    if (!record.session_extended) {
                        throw UnsupportedSourceState("Qualifying release requires extended release-global provenance");
                    }
                    release_qualifying_session(release, finish, record.timing_clock);
                    clocks[local].lap_start_ms = record.timing_clock;
                }
                reset_qualifying_steering(steering_left, steering_right, steering_angle);
            } else if (result.action == LapTimingAction::qualifying_finish) {
                controller = apply_controller_handoff(controller, ControllerHandoffKind::ai, record.session_ai_tire_base);
                if (record_qualifying_finish(finish, 1)) {
                    clock.best_notice = actor_finish.best_notice = 0;
                }
            } else if (result.action == LapTimingAction::race_finish) {
                if (player >= 0 && finish.local_finished[std::size_t(player)] == 0) {
                    if (!record.session_extended) {
                        throw UnsupportedSourceState("Local race finish requires extended classification globals");
                    }
                    if (local_game[0x84] >= 6) {
                        throw UnsupportedSourceState("Local retirement classification requires4220c0 RNG fallback");
                    }
                    --clock.lap;
                    actor_finish = record_local_finish(finish, actor_finish, std::uint32_t(actor), std::uint32_t(player), 1, record.timing_clock, release.release_clock);
                    controller = apply_controller_handoff(controller, ControllerHandoffKind::ai, record.session_ai_tire_base);
                    std::fill_n(split_enable.begin() + player * 4, 4, 0);
                    for (std::size_t other = 0; other < 16; ++other) {
                        const auto& other_game = record.ai_actors[other].gameplay;
                        totals[other] = {
                            finishes[other].total_hundredths, source_i32(other_game, 0xdc), clocks[other].best_ms, clocks[other].lap, std::bit_cast<std::int8_t>(other_game[0x8f])};
                    }
                    classify_finish_totals(totals, finish.order, 16, local, finish.count & 0xff, record.lap_distance);
                    for (std::size_t other = 0; other < 16; ++other) {
                        finishes[other].total_hundredths = totals[other].total;
                        clocks[other].best_ms = totals[other].best_ms;
                    }
                    award_race_classification(scores, podiums, finish.order, local, local_game[0x85]);
                    classification_order = classification_standings(scores);
                } else {
                    actor_finish = record_ai_finish(finish, actor_finish, std::uint32_t(actor), record.timing_clock, release.release_clock);
                }
                clock.best_notice = actor_finish.best_notice;
            }
            if (actor_finish.finished != 0) {
                clock.best_notice = actor_finish.best_notice = actor_finish.notice = 0;
            }
            const bool last_lap =
                record.timing_stage == 1 || record.timing_stage == 3 ? clock.lap == 3 : clock.lap == std::int32_t(record.timing_lap_limit) && record.timing_lap_limit >= 2;
            if (actor_finish.finished == 0 && last_lap) {
                actor_finish.notice = 0x50;
            }
        }
        //4212d0 follows421390, using its restored/incremented lap and the
        //current shared table. Display lifetime advances later, outside403dd0.
        if (progress.split_event) {
            const auto split = record_checkpoint_time(checkpoint_times, std::uint32_t(actor), progress.split_sector, game[0x8f], clock.lap, record.timing_clock);
            if (player >= 0) {
                consume_checkpoint_display(displays, std::uint32_t(player), progress.split_sector, split, clock.current_display, false, record.record_playback != 0);
            }
        }
    }
    write_session_timing(record, clocks, finishes, checkpoint_times, timing_flags, finish, displays, &actor_output, &checkpoint_output, &finish_output, &display_output);
    session_output << record.id << ',' << release.release_counter << ',' << release.release_clock << ',' << release.running;
    for (const auto value : record.session_launch) {
        session_output << ',' << value;
    }
    session_output << '\n';
    controller_output << record.id << ',' << local << ',' << controller.human << ',' << controller.reverse_fallback << ',' << controller.countdown << ',' << controller.player
                      << ',' << controller.gear << ',' << controller.queued_shift;
    for (const auto& modes : {controller.modes, controller.saved_modes}) {
        for (const auto value : modes) {
            controller_output << ',' << value;
        }
    }
    for (const auto value : controller.tires) {
        controller_output << ',' << value;
    }
    controller_output << ',' << steering_left << ',' << steering_right << ',' << steering_angle << '\n';
    if (record.session_extended) {
        classification_output << record.id << ',' << release.marker;
        for (const auto flag : release.flags) {
            classification_output << ',' << flag;
        }
        for (std::size_t player = 0; player < 4; ++player) {
            classification_output << ',' << source_u32(split_enable, player * 4);
        }
        for (const auto value : classification_order) {
            classification_output << ',' << unsigned(value);
        }
        for (const auto value : scores) {
            classification_output << ',' << value;
        }
        for (const auto value : podiums) {
            classification_output << ',' << unsigned(value);
        }
        classification_output << ',' << source_u32(record.configuration_tail, 0x28) << '\n';
    }
}

} // namespace toca::test::fleet
