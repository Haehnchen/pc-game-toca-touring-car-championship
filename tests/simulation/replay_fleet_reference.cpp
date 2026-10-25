#include "assets/store.hpp"
#include "fleet_reference/audio_replay.hpp"
#include "fleet_reference/binary_io.hpp"
#include "fleet_reference/combined_replay.hpp"
#include "fleet_reference/diagnostics.hpp"
#include "fleet_reference/driver_comparison.hpp"
#include "fleet_reference/physical_fleet.hpp"
#include "fleet_reference/record.hpp"
#include "fleet_reference/session_comparison.hpp"
#include "fleet_reference/startup.hpp"
#include "scene/catalog.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/race_release.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/source_race_progress.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

using namespace toca;
using namespace toca::simulation;
using namespace toca::test::fleet;

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--compare-qualifying-startup") {
            return replay_qualifying_startup(argv[2], argv[3], argv[4]);
        }
        if (argc == 5 && std::string(argv[1]) == "--compare-cold-grid-sequence") {
            return replay_cold_grid_initialization(argv[2], argv[3], argv[4], true);
        }
        if (argc == 5 && std::string(argv[1]) == "--compare-cold-grid-init") {
            return replay_cold_grid_initialization(argv[2], argv[3], argv[4]);
        }
        if (const auto request = combined_replay_arguments({argv, std::size_t(argc)})) {
            return replay_combined_rng(request->paths, request->options);
        }
        if (argc == 4 && std::string(argv[1]) == "--compare-skid-dispatch") {
            return replay_skid_dispatch(argv[2], argv[3]);
        }
        if (argc == 4 && std::string(argv[1]) == "--compare-wheel-queue") {
            return replay_wheel_queue(argv[2], argv[3]);
        }
        if (argc == 4 && std::string(argv[1]) == "--compare-whine-rng") {
            return replay_whine_rng(argv[2], argv[3]);
        }
        if (argc < 6 || argc > 15) {
            throw std::runtime_error(
                "Usage: toca-fleet-reference-replay CANONICAL_ROOT TRACK_ID INPUT_BIN OUTPUT_CSV "
                "independent-physics|continuous-physics-inputs|continuous-derived-body-selection [--compare-ai-route] [--compare-ai-timing] [--compare-ai-finish] "
                "[--compare-local-handoffs] [--local-handoffs-only] [--compare-session-boundaries] [--compare-traffic-controls] [--compare-nearest-recovery] "
                "[--compare-joined-ai]");
        }
        bool compare_routes = false, compare_timing = false, compare_finish = false, compare_handoffs = false, handoffs_only = false, compare_sessions = false,
             compare_traffic = false, compare_nearest = false, compare_joined = false;
        for (int index = 6; index < argc; ++index) {
            const std::string flag = argv[index];
            bool* enabled = flag == "--compare-ai-route"             ? &compare_routes
                            : flag == "--compare-ai-timing"          ? &compare_timing
                            : flag == "--compare-ai-finish"          ? &compare_finish
                            : flag == "--compare-local-handoffs"     ? &compare_handoffs
                            : flag == "--local-handoffs-only"        ? &handoffs_only
                            : flag == "--compare-session-boundaries" ? &compare_sessions
                            : flag == "--compare-traffic-controls"   ? &compare_traffic
                            : flag == "--compare-nearest-recovery"   ? &compare_nearest
                            : flag == "--compare-joined-ai"          ? &compare_joined
                                                                     : nullptr;
            if (!enabled || *enabled) {
                throw std::runtime_error("Unknown or duplicate original fleet replay option");
            }
            *enabled = true;
        }
        if ((compare_timing && !compare_routes) || (compare_finish && !compare_timing) || (handoffs_only && (!compare_handoffs || compare_routes))
            || (compare_sessions && (compare_routes || compare_timing || compare_finish || handoffs_only))
            || (compare_traffic && (compare_routes || compare_timing || compare_finish || handoffs_only || compare_sessions || compare_handoffs))
            || (compare_nearest && (compare_routes || compare_timing || compare_finish || handoffs_only || compare_sessions || compare_handoffs || compare_traffic))
            || (compare_joined
                && (compare_routes || compare_timing || compare_finish || handoffs_only || compare_sessions || compare_handoffs || compare_traffic || compare_nearest))) {
            throw std::runtime_error("Original fleet diagnostic options have incompatible dependencies");
        }
        const std::string mode = argv[5];
        if (mode != "independent-physics" && mode != "continuous-physics-inputs" && mode != "continuous-derived-body-selection") {
            throw std::runtime_error("Unknown original fleet replay mode");
        }
        const bool independent = mode == "independent-physics";
        const bool derive_selection = mode == "continuous-derived-body-selection";
        std::ifstream input(argv[3], std::ios::binary);
        std::array<std::uint8_t, 8> magic;
        read(input, magic);
        if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'F', 'L', 'E', 'T'} || read_u32(input) != 13 || read_u32(input) != 16) {
            throw std::runtime_error("Unsupported original fleet replay stream");
        }
        const auto count = read_u32(input), local = read_u32(input), weather = read_u32(input), steering = read_u32(input);
        const auto ai_present = read_u32(input), timing_present = read_u32(input), finish_present = read_u32(input), handoffs_present = read_u32(input),
                   session_present = read_u32(input), traffic_present = read_u32(input), nearest_present = read_u32(input), joined_present = read_u32(input);
        if (ai_present > 1 || timing_present > 1 || finish_present > 1 || handoffs_present > 1 || session_present > 1 || traffic_present > 1 || nearest_present > 1
            || joined_present > 1 || (joined_present && (!ai_present || !traffic_present)) || (nearest_present && !ai_present) || (traffic_present && !ai_present)
            || (session_present && !finish_present) || (timing_present && !ai_present) || (finish_present && !timing_present)) {
            throw std::runtime_error("Invalid optional original AI state marker");
        }
        std::optional<AiInitialState> ai_initial;
        if (ai_present) {
            ai_initial = read_ai_initial(input);
        }
        if (traffic_present) {
            const auto points = read_u32(input);
            if (points != source_u32(ai_initial->route, 0)) {
                throw UnsupportedSourceState("Traffic brake-bypass array disagrees with initialized POINTS");
            }
            ai_initial->brake_bypass.resize(points);
            read(input, ai_initial->brake_bypass);
        }
        std::array<std::uint8_t, 256> captured_nearest_map{};
        if (nearest_present) {
            read(input, captured_nearest_map);
        }
        if (joined_present) {
            ai_initial->rank_threshold = std::bit_cast<std::int32_t>(read_u32(input));
            ai_initial->cumulative_route.resize(source_u32(ai_initial->route, 0));
            for (auto& distance : ai_initial->cumulative_route) {
                distance = std::bit_cast<std::int32_t>(read_u32(input));
            }
        }
        if (compare_joined && !joined_present) {
            throw UnsupportedSourceState("Joined AI comparison requires original grid/release/rank-table provenance");
        }
        if (compare_nearest && !nearest_present) {
            throw UnsupportedSourceState("Nearest recovery comparison requires explicit4046a3/404731 provenance");
        }
        if (compare_traffic && !traffic_present) {
            throw UnsupportedSourceState("Traffic/control comparison requires explicit404c1a PRE provenance");
        }
        if (compare_routes && !ai_initial) {
            throw UnsupportedSourceState("AI route comparison requires explicit403dd0 entry/return and initialized AI/POINTS provenance");
        }
        if (compare_timing && !timing_present) {
            throw UnsupportedSourceState("Checkpoint timing comparison requires explicit PRE table/clock provenance");
        }
        if (compare_finish && !finish_present) {
            throw UnsupportedSourceState("Finish/display timing requires explicit PRE lifecycle/display provenance");
        }
        if (compare_handoffs && !handoffs_present) {
            throw UnsupportedSourceState("Local handoff comparison requires explicit call entry/return provenance");
        }
        if (compare_sessions && (!session_present || !ai_initial)) {
            throw UnsupportedSourceState("Session boundaries require captured PRE AI/timing/finish and controller provenance");
        }
        const auto track = std::stoul(argv[2]);
        if (!count || count > 100000 || local >= 16 || weather < 1 || weather > 5 || steering < 1 || steering > 3 || track > 9) {
            throw std::runtime_error("Invalid original fleet metadata");
        }
        std::ofstream handoffs;
        if (compare_handoffs) {
            handoffs.open(std::string(argv[4]) + ".local-handoffs.csv");
            handoffs.exceptions(std::ios::failbit | std::ios::badbit);
            write_handoff_header(handoffs);
        }
        if (handoffs_only) {
            std::size_t event_count = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto record = read_record(input, steering, local, ai_present != 0, timing_present != 0, finish_present != 0, handoffs_present != 0, session_present != 0,
                    traffic_present != 0, nearest_present != 0, joined_present != 0);
                if (record.id != index) {
                    throw std::runtime_error("Missing or reordered original local-handoff record");
                }
                compare_local_handoffs(record, handoffs);
                event_count += record.handoffs.size();
            }
            if (input.peek() != std::char_traits<char>::eof()) {
                throw std::runtime_error("Original handoff stream has trailing bytes");
            }
            std::cout << "Compared " << event_count << " captured local controller entry/return transactions; physical and whole403dd0 execution were not performed\n";
            return 0;
        }
        if (compare_sessions) {
            const bool compare_timing = true, compare_finish = true;
            std::ofstream ai_timing, checkpoint_timing, finish_output, display_output;
            if (compare_timing) {
                ai_timing.open(std::string(argv[4]) + ".ai-timing.csv");
                ai_timing.exceptions(std::ios::failbit | std::ios::badbit);
                ai_timing
                    << "id,actor,lap,record_flag,best_ms,current_ms,lap_start_ms,best_minutes,best_seconds,best_hundredths,current_minutes,current_seconds,current_hundredths,"
                       "best_notice";
                if (compare_finish) {
                    ai_timing << ",finished,total_time_hundredths,notice";
                }
                ai_timing << '\n';
                checkpoint_timing.open(std::string(argv[4]) + ".checkpoint.csv");
                checkpoint_timing.exceptions(std::ios::failbit | std::ios::badbit);
                checkpoint_timing << "id,slot,clock,lap,actor,race_flags\n";
            }
            if (compare_finish) {
                finish_output.open(std::string(argv[4]) + ".finish.csv");
                display_output.open(std::string(argv[4]) + ".split-display.csv");
                finish_output.exceptions(std::ios::failbit | std::ios::badbit);
                display_output.exceptions(std::ios::failbit | std::ios::badbit);
                finish_output << "id,finish_count,local_finish_count,timer2,timer3,pending_state,race_stop";
                for (unsigned player = 0; player < 4; ++player) {
                    finish_output << ",local_finished_" << player;
                }
                for (unsigned actor = 0; actor < 16; ++actor) {
                    finish_output << ",finish_order_" << actor;
                }
                finish_output << '\n';
                display_output << "id,player,mode,leader,active,ticks,minutes,seconds,hundredths,reference_0,reference_1,reference_2,current_0,current_1,current_2,display,leader_"
                                  "display,persisted_0,persisted_1,persisted_2\n";
            }
            std::ofstream session_output(std::string(argv[4]) + ".session.csv"), controller_output(std::string(argv[4]) + ".session-controller.csv"),
                classification_output(std::string(argv[4]) + ".session-classification.csv"), unsupported(std::string(argv[4]) + ".session-unsupported.csv");
            for (auto* stream : {&session_output, &controller_output, &classification_output, &unsupported}) {
                stream->exceptions(std::ios::failbit | std::ios::badbit);
            }
            session_output << "id,release_counter,release_epoch,race_running,launch_tire_0,launch_tire_1,launch_tire_2,launch_tire_3\n";
            controller_output << "id,actor,human,reverse_fallback,countdown,player,gear,queued_shift,mode_steering,mode_throttle,mode_brake,mode_transmission,"
                                 "saved_steering,saved_throttle,saved_brake,saved_transmission";
            for (unsigned index = 0; index < 8; ++index) {
                controller_output << ",tire_offset_" << index;
            }
            controller_output << ",raw_steering_left,raw_steering_right,steering_angle\n";
            classification_output << "id,release_marker";
            for (const auto& [name, size] : {std::pair{"release_flag_", 4}, {"split_enable_", 4}, {"classification_order_", 16}, {"score_", 16}, {"podium_", 3}}) {
                for (int index = 0; index < size; ++index) {
                    classification_output << ',' << name << index;
                }
            }
            classification_output << ",configuration_624\n";
            unsupported << "id,reason\n";
            std::size_t supported = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto record = read_record(input, steering, local, ai_present != 0, timing_present != 0, finish_present != 0, handoffs_present != 0, session_present != 0,
                    traffic_present != 0, nearest_present != 0, joined_present != 0);
                if (record.id != index) {
                    throw std::runtime_error("Missing or reordered original session-boundary record");
                }
                std::array<std::ostringstream, 7> pending;
                try {
                    compare_session_boundaries(*ai_initial, record, local, pending[0], pending[1], pending[2], pending[3], pending[4], pending[5], pending[6]);
                    std::array<std::ostream*, 7> outputs{
                        &ai_timing, &checkpoint_timing, &finish_output, &display_output, &session_output, &controller_output, &classification_output};
                    for (std::size_t stream = 0; stream < outputs.size(); ++stream) {
                        *outputs[stream] << pending[stream].str();
                    }
                    if (compare_handoffs) {
                        compare_local_handoffs(record, handoffs);
                    }
                    ++supported;
                } catch (const UnsupportedSourceState& error) {
                    std::string reason = error.what();
                    for (std::size_t at = 0; (at = reason.find('"', at)) != std::string::npos; at += 2) {
                        reason.insert(at, 1, '"');
                    }
                    unsupported << index << ",\"" << reason << "\"\n";
                }
            }
            if (input.peek() != std::char_traits<char>::eof()) {
                throw std::runtime_error("Original session-boundary stream has trailing bytes");
            }
            std::cout << "Compared " << supported << " independent session-output updates; " << count - supported
                      << " unsupported. Physics, traffic, route targets and shared RNG were not executed\n";
            return 0;
        }
        if (compare_nearest) {
            std::vector<fixed::Vector> targets(source_u32(ai_initial->route, 0));
            for (std::size_t point = 0; point < targets.size(); ++point) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    targets[point][axis] = source_i32(ai_initial->route, 4 + point * 64 + 12 + axis * 4);
                }
            }
            const NearestRouteMap nearest(targets);
            std::ofstream map_output(std::string(argv[4]) + ".nearest-map.csv"), events(std::string(argv[4]) + ".nearest-recovery.csv");
            map_output.exceptions(std::ios::failbit | std::ios::badbit);
            events.exceptions(std::ios::failbit | std::ios::badbit);
            map_output << "index,candidate\n";
            for (std::size_t point = 0; point < targets.size(); ++point) {
                if (captured_nearest_map[point] >= targets.size()) {
                    throw UnsupportedSourceState("Captured nearest-map candidate exceeds initialized POINTS");
                }
                map_output << point << ',' << unsigned(nearest.candidates()[point]) << '\n';
            }
            events << "id,event,actor,route_next,route_previous,rng_final\n";
            std::size_t event_count = 0, recovered_count = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto record = read_record(input, steering, local, ai_present != 0, timing_present != 0, finish_present != 0, handoffs_present != 0, session_present != 0,
                    traffic_present != 0, nearest_present != 0, joined_present != 0);
                if (record.id != index) {
                    throw std::runtime_error("Missing or reordered nearest-recovery update");
                }
                std::vector<std::uint32_t> expected;
                for (std::uint32_t actor = 0; actor < 16; ++actor) {
                    const auto& game = record.ai_actors[actor].gameplay;
                    if (game[0x8d] >= 16 || game[0x86] > 127) {
                        throw UnsupportedSourceState("Nearest recovery has an invalid resolved driver/cadence");
                    }
                    const auto period = ai_initial->resolved[game[0x8d] * 40 + 19];
                    if (period > 127) {
                        throw UnsupportedSourceState("Nearest recovery requires a nonnegative source period");
                    }
                    if (source_u32(game, 4) != 0 && source_u32(game, 8) != 0 && game[0x86] >= period) {
                        expected.push_back(actor);
                    }
                }
                if (record.nearest_recoveries.size() != expected.size()) {
                    throw UnsupportedSourceState("Nearest recovery transactions do not cover every expected human refresh");
                }
                for (std::size_t event_index = 0; event_index < expected.size(); ++event_index) {
                    const auto& event = record.nearest_recoveries[event_index];
                    if (event.actor != expected[event_index] || event.human != 1 || event.refresh != 1) {
                        throw UnsupportedSourceState("Nearest recovery event has invalid source actor order/gates");
                    }
                    const auto recovery = nearest.recover(event.cursor, event.position);
                    events << record.id << ',' << event_index << ',' << event.actor << ',' << recovery.cursor.next << ',' << recovery.cursor.previous << ',' << event.rng << '\n';
                    ++event_count;
                    recovered_count += recovery.recovered ? 1u : 0u;
                }
            }
            if (input.peek() != std::char_traits<char>::eof()) {
                throw std::runtime_error("Nearest recovery stream has trailing bytes");
            }
            std::cout << "Compared " << event_count << " complete human nearest-route checks with " << recovered_count
                      << " cursor jumps; gate movement, physics and upstream RNG were not executed\n";
            return 0;
        }
        if (compare_traffic || compare_joined) {
            std::ofstream traffic_output(std::string(argv[4]) + ".traffic.csv"), traffic_world(std::string(argv[4]) + ".traffic-world.csv"),
                control_output(std::string(argv[4]) + ".ai-controls.csv"), control_world(std::string(argv[4]) + ".ai-control-world.csv");
            for (auto* stream : {&traffic_output, &traffic_world, &control_output, &control_world}) {
                stream->exceptions(std::ios::failbit | std::ios::badbit);
            }
            traffic_output << "id,actor,mode,blocked,brake,passing,steering_cached,forced_brake,passed_actor,blocker_actor,corridor_0,corridor_1,corridor_2,corridor_3,"
                              "corner_0,corner_1,obstacle_angle_0,obstacle_angle_1,headlight_ticks,headlight_on_ticks,headlight_off_ticks,headlights,reverse,reverse_ticks,gear,"
                              "reverse_pedal,raw_left,raw_right\n";
            traffic_world << "id,rng_final,traffic_counter\n";
            control_output << "id,actor,raw_left,raw_right,raw_throttle,raw_brake,reverse,reverse_ticks,reaction_ticks,brake_ticks,gear,reverse_pedal,mode,"
                              "steering_cached,obstacle_angle_0,obstacle_angle_1,headlights,brake_lights\n";
            control_world << "id,rng_final,steering_scratch\n";
            std::ofstream route_output, progress_output, joined_world;
            RaceReleaseCounter release;
            if (compare_joined) {
                route_output.open(std::string(argv[4]) + ".ai-route.csv");
                progress_output.open(std::string(argv[4]) + ".joined-progress.csv");
                joined_world.open(std::string(argv[4]) + ".joined-world.csv");
                for (auto* stream : {&route_output, &progress_output, &joined_world}) {
                    stream->exceptions(std::ios::failbit | std::ios::badbit);
                }
                route_output << "id,actor,cache_point,target_x,target_y,target_z,alternate_x,alternate_y,alternate_z,desired_speed,alternate_speed,"
                                "speed_variation,distance,projected_distance,angle,alternate_angle,route_angle,rng_final,route_next,route_previous,backwards,current_gate,"
                                "previous_gate,refresh_ticks,refresh_requested,checkpoint,checkpoint_marker\n";
                progress_output << "id,actor,route_distance,distance_delta,rank,spatial_rank,display_rank,rank_ticks,ahead,behind,contact_ahead,contact_behind,traffic_flag,"
                                   "tire_countdown,tire_offset_0\n";
                joined_world << "id,release_counter\n";
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto record = read_record(input, steering, local, ai_present != 0, timing_present != 0, finish_present != 0, handoffs_present != 0, session_present != 0,
                    traffic_present != 0, nearest_present != 0, joined_present != 0);
                if (record.id != index) {
                    throw std::runtime_error("Missing or reordered original traffic/control record");
                }
                if (compare_joined) {
                    compare_joined_ai(
                        *ai_initial, record, local, release, route_output, progress_output, joined_world, traffic_output, traffic_world, control_output, control_world);
                } else {
                    compare_traffic_controls(*ai_initial, record, traffic_output, traffic_world, control_output, control_world);
                }
            }
            if (input.peek() != std::char_traits<char>::eof()) {
                throw std::runtime_error("Original traffic stream has trailing bytes");
            }
            std::cout << "Compared " << count
                      << (compare_joined ? " complete joined AI updates from403dd0 PRE; intermediate route/ring/RNG states are derived and physics was not executed\n"
                                         : " complete independent traffic/control updates; upstream route/ring inputs are observed and physics was not executed\n");
            return 0;
        }
        const AssetStore store(argv[1]);
        const auto prefix = std::string(scene::track_content(std::uint32_t(track)).archive_prefix);
        const auto keep = store.open_archive(prefix + "keep.bfp"), free = store.open_archive(prefix + "free.bfp");
        const auto terrain = parse_terrain(free.member(1).bytes());
        const HpcSurface surface(keep.member(0).bytes());
        const scene::BarrierData barriers(keep.member(1).bytes(), terrain.sector_centers);
        const auto cones = scene::decode_props(free.member(8).bytes(), scene::PropKind::cones);
        const auto posts = track == 9 ? scene::PropResources{} : scene::decode_props(free.member(9).bytes(), scene::PropKind::posts);
        const auto signs = track == 9 ? scene::PropResources{} : scene::decode_props(free.member(10).bytes(), scene::PropKind::signs);
        Fleet fleet(surface, barriers, cones, posts, signs, terrain, local, weather);
        std::ofstream output(argv[4]);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        std::ofstream detached(std::string(argv[4]) + ".detached.csv");
        detached.exceptions(std::ios::failbit | std::ios::badbit);
        detached << "id,phase,slot,count,active,free,type,source_slot";
        for (const auto prefix : {"basis_", "rotation_"}) {
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    detached << ',' << prefix << row << axis;
                }
            }
        }
        detached << ",position_x,position_y,position_z,velocity_x,velocity_y,velocity_z,initial_y,timer,sector,triangle,resting\n";
        std::ofstream launch(std::string(argv[4]) + ".launch.csv");
        launch.exceptions(std::ios::failbit | std::ios::badbit);
        launch << "id,actor,phase,launch,engine_speed,start_hold";
        for (std::size_t index = 0; index < 8; ++index) {
            launch << ",tire_offset_" << index;
        }
        launch << '\n';
        std::ofstream selection(std::string(argv[4]) + ".selection.csv");
        selection.exceptions(std::ios::failbit | std::ios::badbit);
        selection << "id,actor,mode\n";
        std::ofstream ai_routes, ai_unsupported, ai_timing, checkpoint_timing, finish_output, display_output;
        std::uint32_t ai_supported_updates = 0, ai_unsupported_updates = 0;
        if (compare_routes) {
            ai_routes.open(std::string(argv[4]) + ".ai-route.csv");
            ai_routes.exceptions(std::ios::failbit | std::ios::badbit);
            ai_unsupported.open(std::string(argv[4]) + ".ai-route-unsupported.csv");
            ai_unsupported.exceptions(std::ios::failbit | std::ios::badbit);
            ai_unsupported << "id,reason\n";
            ai_routes << "id,actor,cache_point,target_x,target_y,target_z,alternate_x,alternate_y,alternate_z,desired_speed,alternate_speed,speed_variation,distance,projected_"
                         "distance,angle,alternate_angle,route_angle,rng_final,route_next,route_previous,backwards,current_gate,previous_gate,refresh_ticks,refresh_requested,"
                         "checkpoint,checkpoint_marker\n";
        }
        if (compare_timing) {
            ai_timing.open(std::string(argv[4]) + ".ai-timing.csv");
            ai_timing.exceptions(std::ios::failbit | std::ios::badbit);
            ai_timing << "id,actor,lap,record_flag,best_ms,current_ms,lap_start_ms,best_minutes,best_seconds,best_hundredths,current_minutes,current_seconds,current_hundredths,"
                         "best_notice";
            if (compare_finish) {
                ai_timing << ",finished,total_time_hundredths,notice";
            }
            ai_timing << '\n';
            checkpoint_timing.open(std::string(argv[4]) + ".checkpoint.csv");
            checkpoint_timing.exceptions(std::ios::failbit | std::ios::badbit);
            checkpoint_timing << "id,slot,clock,lap,actor,race_flags\n";
        }
        if (compare_finish) {
            finish_output.open(std::string(argv[4]) + ".finish.csv");
            display_output.open(std::string(argv[4]) + ".split-display.csv");
            finish_output.exceptions(std::ios::failbit | std::ios::badbit);
            display_output.exceptions(std::ios::failbit | std::ios::badbit);
            finish_output << "id,finish_count,local_finish_count,timer2,timer3,pending_state,race_stop";
            for (unsigned player = 0; player < 4; ++player) {
                finish_output << ",local_finished_" << player;
            }
            for (unsigned actor = 0; actor < 16; ++actor) {
                finish_output << ",finish_order_" << actor;
            }
            finish_output << '\n';
            display_output << "id,player,mode,leader,active,ticks,minutes,seconds,hundredths,reference_0,reference_1,reference_2,current_0,current_1,current_2,display,leader_"
                              "display,persisted_0,persisted_1,persisted_2\n";
        }
        std::array<SourceSnapshot, 16> initial_configuration;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto record = read_record(input, steering, local, ai_present != 0, timing_present != 0, finish_present != 0, handoffs_present != 0, session_present != 0,
                traffic_present != 0, nearest_present != 0, joined_present != 0);
            if (record.id != index) {
                throw std::runtime_error("Missing or reordered original fleet record");
            }
            if (compare_handoffs) {
                compare_local_handoffs(record, handoffs);
            }
            if (compare_routes) {
                try {
                    compare_ai_route(*ai_initial, record, ai_routes, compare_timing ? &ai_timing : nullptr, compare_timing ? &checkpoint_timing : nullptr,
                        compare_finish ? &finish_output : nullptr, compare_finish ? &display_output : nullptr);
                    ++ai_supported_updates;
                } catch (const UnsupportedSourceState& error) {
                    //No partial actor results were written. The next update
                    //starts independently at its own captured403dd0 RNG.
                    std::string reason = error.what();
                    for (std::size_t at = 0; (at = reason.find('"', at)) != std::string::npos; at += 2) {
                        reason.insert(at, 1, '"');
                    }
                    ai_unsupported << record.id << ",\"" << reason << "\"\n";
                    ++ai_unsupported_updates;
                }
            }
            for (std::uint32_t actor = 0; actor < 16; ++actor) {
                if (index == 0 || independent) {
                    initial_configuration[actor] = record.actors[actor].snapshot;
                } else {
                    validate_configuration(initial_configuration[actor], record.actors[actor].snapshot, index, actor, local);
                }
            }
            if (index == 0 || independent) {
                fleet.restore(record);
            }
            fleet.step(record, output, detached, launch, selection, index == 0 || independent, derive_selection);
        }
        if (input.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Original fleet stream has trailing bytes");
        }
        std::cout << "Replayed " << count << " complete sixteen-actor physical phases (" << mode << "); initial state imports " << (independent ? count : 1)
                  << "; observed all-actor controls/registration and PRE race ring; "
                  << (derive_selection ? "retained native body selection" : "observed dispatch with independent captured-PRE selection")
                  << "; autonomous AI and process RNG are not replayed\n";
        if (compare_routes) {
            std::cout << "Independent AI route comparisons " << ai_supported_updates << "; unsupported updates " << ai_unsupported_updates
                      << "; traffic and descending controls are outside this comparison\n";
        }
        return 0;
    } catch (const UnsupportedSourceState& error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
