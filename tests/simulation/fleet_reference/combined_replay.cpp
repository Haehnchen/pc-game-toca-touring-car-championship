#include "combined_replay.hpp"

#include "../../scene/frame_replay_output.hpp"

#include "assets/store.hpp"
#include "audio/contact_state.hpp"
#include "audio/engine_state.hpp"
#include "audio/material_state.hpp"
#include "audio/skid.hpp"
#include "audio/wheel_queue.hpp"
#include "binary_io.hpp"
#include "clock_transactions.hpp"
#include "diagnostics.hpp"
#include "driver_comparison.hpp"
#include "physical_fleet.hpp"
#include "retained_state.hpp"
#include "scene/catalog.hpp"
#include "session_comparison.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/car_contacts.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/controls.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/fixed.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/race_release.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/source_race_progress.hpp"
#include "simulation/source_scene_frame.hpp"
#include "simulation/source_session_start.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/time.hpp"
#include "startup.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace toca::test::fleet {
using namespace toca::simulation;

int replay_combined_rng(const CombinedReplayPaths& paths, const CombinedReplayOptions& options) {
    validate_combined_replay_options(paths, options);
    const bool scene_owned = options.scene_owned;
    const bool scene_lifecycle = options.scene_lifecycle;
    const AssetStore assets(paths.canonical_root);
    std::optional<ColdGridInitInputs> startup_request;
    std::optional<ColdGridInitResult> startup;
    std::array<scene::BodyPose, 16> first_scene_grid, repeated_scene_grid;
    if (paths.startup) {
        startup_request = read_cold_grid_inputs(paths.startup);
        const auto generated = initialize_start_grid(assets, *startup_request);
        std::ofstream startup_output(std::string(paths.output_prefix) + ".startup.csv");
        startup_output.exceptions(std::ios::failbit | std::ios::badbit);
        startup_output << "phase,group,actor,field,value\n";
        emit_cold_grid(startup_output, *startup_request, generated.first, true, "first_return");
        emit_cold_grid(startup_output, *startup_request, generated.repeated, true, "second_return");
        emit_cold_grid(startup_output, *startup_request, generated.repeated, true, "first_pre");
        startup = generated.repeated;
        if (scene_owned) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                first_scene_grid[actor] = generated.first.actors[actor].body;
                repeated_scene_grid[actor] = generated.repeated.actors[actor].body;
            }
        }
    }
    const auto track = std::uint32_t(std::stoul(paths.track_id_text));
    const auto& track_content = scene::track_content(track);
    std::ifstream input(paths.fleet, std::ios::binary), journal(paths.journal, std::ios::binary);
    const auto check_magic = [&](std::istream& stream, const std::array<std::uint8_t, 8>& expected, std::uint32_t version) {
        std::array<std::uint8_t, 8> magic;
        read(stream, magic);
        if (magic != expected || read_u32(stream) != version) {
            throw std::runtime_error("Unsupported combined original replay stream");
        }
    };
    check_magic(input, {'T', 'O', 'C', 'A', 'F', 'L', 'E', 'T'}, 13);
    if (read_u32(input) != 16) {
        throw std::runtime_error("Combined replay requires the original sixteen-car grid");
    }
    const auto count = read_u32(input), local = read_u32(input), weather = read_u32(input), steering = read_u32(input);
    std::optional<ClockTransactions> clock;
    if (paths.clock) {
        clock.emplace(paths.clock, count, scene_owned && !scene_lifecycle);
    }
    std::unique_ptr<SourceSceneFrame> scene_frames;
    std::array<scene::GridSlot, 32> scene_grid{};
    std::ofstream scene_output;
    std::optional<ScenePhysicalBoundary> current_scene;
    bool pending_scene = false;
    std::uint32_t scene_frame_index = 0, scene_physical_count = 0;
    const auto initialize_scene = [&](bool include_cockpit) {
        const auto resources = scene::load_scene_resources(assets, startup_request->setup);
        scene_grid = resources.grid;
        scene_frames = std::make_unique<SourceSceneFrame>(resources, local, clock->divisor(), clock->initial_step(), scene_lifecycle);
        scene_output.open(std::string(paths.output_prefix) + (scene_lifecycle ? ".scene-lifecycle.csv" : ".scene.csv"));
        scene_output.exceptions(std::ios::failbit | std::ios::badbit);
        scene_output << "order,group,index,field,value\n";
        if (!scene_lifecycle) {
            scene_test::write_initial_roots(scene_output, *scene_frames, resources.grid, include_cockpit);
        }
        scene_frames->initialize_grid(first_scene_grid);
        scene_frames->initialize_grid(repeated_scene_grid);
    };
    const auto flush_scene = [&] {
        if (pending_scene) {
            scene_frames->publish_primary();
            pending_scene = false;
        }
    };
    std::ifstream physical_input;
    std::unique_ptr<HpcSurface> physical_surface;
    std::unique_ptr<scene::BarrierData> physical_barriers;
    std::unique_ptr<Fleet> physical_fleet;
    std::ofstream physical_output, physical_detached, physical_launch, physical_selection;
    if (paths.physical) {
        physical_input.open(paths.physical, std::ios::binary);
        check_magic(physical_input, {'T', 'O', 'C', 'A', 'F', 'L', 'E', 'T'}, 13);
        for (const auto expected : {16u, count, local, weather, steering}) {
            if (read_u32(physical_input) != expected) {
                throw std::runtime_error("Driver and physical streams disagree on interval/configuration");
            }
        }
        for (std::size_t flag = 0; flag < 8; ++flag) {
            if (read_u32(physical_input) != 0) {
                throw std::runtime_error("Physical stream cannot contain additional AI/RNG consumers");
            }
        }
        const auto keep = assets.open_archive(std::string(track_content.archive_prefix) + "keep.bfp");
        const auto free = assets.open_archive(std::string(track_content.archive_prefix) + "free.bfp");
        const auto terrain = parse_terrain(free.member(1).bytes());
        physical_surface = std::make_unique<HpcSurface>(keep.member(0).bytes());
        physical_barriers = std::make_unique<scene::BarrierData>(keep.member(1).bytes(), terrain.sector_centers);
        const auto cones = scene::decode_props(free.member(8).bytes(), scene::PropKind::cones);
        const auto posts = track == 9 ? scene::PropResources{} : scene::decode_props(free.member(9).bytes(), scene::PropKind::posts);
        const auto signs = track == 9 ? scene::PropResources{} : scene::decode_props(free.member(10).bytes(), scene::PropKind::signs);
        physical_fleet = std::make_unique<Fleet>(*physical_surface, *physical_barriers, cones, posts, signs, terrain, local, weather);
        physical_output.open(std::string(paths.output_prefix) + ".physical.csv");
        physical_detached.open(std::string(paths.output_prefix) + ".physical.csv.detached.csv");
        physical_launch.open(std::string(paths.output_prefix) + ".physical.csv.launch.csv");
        physical_selection.open(std::string(paths.output_prefix) + ".physical.csv.selection.csv");
        for (auto* stream : {&physical_output, &physical_detached, &physical_launch, &physical_selection}) {
            stream->exceptions(std::ios::failbit | std::ios::badbit);
        }
        physical_detached << "id,phase,slot,count,active,free,type,source_slot";
        for (const auto prefix : {"basis_", "rotation_"}) {
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    physical_detached << ',' << prefix << row << axis;
                }
            }
        }
        physical_detached << ",position_x,position_y,position_z,velocity_x,velocity_y,velocity_z,initial_y,timer,sector,triangle,resting\n";
        physical_launch << "id,actor,phase,launch,engine_speed,start_hold";
        for (std::size_t tire = 0; tire < 8; ++tire) {
            physical_launch << ",tire_offset_" << tire;
        }
        physical_launch << '\n';
        physical_selection << "id,actor,mode\n";
    }
    std::array<std::uint32_t, 8> flags;
    for (auto& flag : flags) {
        flag = read_u32(input);
        if (flag > 1) {
            throw std::runtime_error("Combined fleet optional state marker is invalid");
        }
    }
    if (!flags[0] || !flags[5] || !flags[7]) {
        throw UnsupportedSourceState("Combined replay requires initialized joined-AI provenance");
    }
    if (clock && flags != std::array<std::uint32_t, 8>{1, 0, 0, 0, 0, 1, 0, 1} && flags != std::array<std::uint32_t, 8>{1, 1, 1, 1, 1, 1, 0, 1}) {
        throw UnsupportedSourceState("Source startup lifecycle requires the ordinary joined countdown profile");
    }
    auto initial = read_ai_initial(input);
    if (startup) {
        if (track != startup_request->setup.selection.track_id || local != startup->data.local_actor || steering != startup_request->steering_profile
            || weather != startup_request->setup.weather) {
            throw std::runtime_error("Startup entry and joined interval configuration disagree");
        }
        for (std::size_t byte = 0; byte <= startup_request->player_name.size(); ++byte) {
            auto& observed = initial.resolved[local * 40 + byte];
            if (observed) {
                throw std::runtime_error("Observed startup local name cannot enter joined replay");
            }
            observed = startup->data.drivers[local].raw[byte];
        }
    }
    const auto points = read_u32(input);
    if (points != source_u32(initial.route, 0)) {
        throw std::runtime_error("Combined traffic table disagrees with initialized POINTS");
    }
    initial.brake_bypass.resize(points);
    read(input, initial.brake_bypass);
    if (flags[6]) {
        std::array<std::uint8_t, 256> nearest;
        read(input, nearest);
    }
    initial.rank_threshold = std::bit_cast<std::int32_t>(read_u32(input));
    initial.cumulative_route.resize(points);
    for (auto& distance : initial.cumulative_route) {
        distance = std::bit_cast<std::int32_t>(read_u32(input));
    }
    std::array<std::uint8_t, 8> journal_magic;
    read(journal, journal_magic);
    const auto version = read_u32(journal);
    if (journal_magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'C', 'O', 'M', 'B'} || (version < 1 || version > 9)) {
        throw std::runtime_error("Unsupported combined original journal version");
    }
    if (read_u32(journal) != count || count == 0) {
        throw std::runtime_error("Combined original streams disagree on their complete interval");
    }
    const auto seed = read_u32(journal);
    if (startup && (seed != 0 || version < 3)) {
        throw std::runtime_error("Startup join rejects a second seed and requires retained drivers");
    }
    SourceRandom random(startup ? startup->rng_final : seed); //One shared stream, continued from generated startup when joined.
    const auto retained_driver_mode = version >= 3;
    const auto retained_lamps_mode = version >= 4;
    const auto publication_mode = version >= 5;
    const bool local_session = version >= 6;
    const bool warm_restart = version >= 7;
    const bool camera_controls = version == 9;
    if (camera_controls && !scene_owned) {
        throw std::runtime_error("Camera control transactions require generated physical/scene ownership");
    }
    if ((version >= 8) != (clock && clock->created_origins())) {
        throw std::runtime_error("Complete source clock operations require their v8 combined journal and v3 clock stream together");
    }
    if (scene_owned && version < 8) {
        throw std::runtime_error("Generated scene replay requires actual primary and external QPC transactions");
    }
    if (scene_owned) {
        initialize_scene(camera_controls);
    }
    std::optional<RetainedRaceTiming> race_timing;
    if (flags[1] || flags[2]) {
        if (!flags[1] || !flags[2] || !retained_driver_mode || (local_session ? !flags[3] || !flags[4] : flags[3] || flags[4]) || flags[6]) {
            throw UnsupportedSourceState("Retained lap timing requires both timing/finish blocks and excludes local handoff/session/recovery consumers");
        }
        race_timing.emplace();
        race_timing->local_session = local_session;
        race_timing->local_actor = local;
        race_timing->warm_restart = warm_restart;
        race_timing->cold_start = race_timing->cold_first = race_timing->cold_active_entry = options.startup_world_reset;
        race_timing->created_origins = clock && clock->created_origins();
    }
    if (local_session && !race_timing) {
        throw UnsupportedSourceState("Local lifecycle journal requires retained timing and typed session state");
    }
    if (paths.physical && !retained_driver_mode) {
        throw std::runtime_error("Joined driver physics requires retained driver chronology");
    }
    if (retained_driver_mode && track != 9) {
        //401790 loads the canonical KEEP PITS once; this is initialized
        //authored geometry, not a later observed gameplay gate.
        const auto keep = assets.open_archive(std::string(track_content.archive_prefix) + "keep.bfp");
        const auto pits = scene::decode_pit_route(keep.member(2).bytes());
        if (pits.points.empty()) {
            throw UnsupportedSourceState("Retained drivers require the initialized PITS entry boundary");
        }
        initial.pit_entry = pits.points.front();
    }
    const auto contact_profile = retained_driver_mode ? read_u32(journal) : version == 2 ? 1u : 0u;
    if (contact_profile > 1) {
        throw std::runtime_error("Invalid retained-driver contact profile flags");
    }
    const auto integer = [&] {
        return std::bit_cast<std::int32_t>(read_u32(journal));
    };
    std::uint32_t initial_release = 0, camera_scale = std::bit_cast<std::uint32_t>(65536.f);
    audio::SourceContactGains contact_gains;
    if (version >= 2) {
        initial_release = read_u32(journal);
        camera_scale = read_u32(journal);
        for (auto& gain : contact_gains.local) {
            gain = integer();
        }
        for (auto& gain : contact_gains.remote) {
            gain = integer();
        }
        if (read_u32(journal) != 0) {
            throw UnsupportedSourceState("Combined contact interval must begin without a pending event27 spatial override");
        }
    }
    if (startup) {
        if (initial_release) {
            throw std::runtime_error("Observed initial release cannot enter startup join");
        }
        initial_release = startup->release_counter;
    }
    auto retained_launch = retained_lamps_mode ? integer() : 0;
    if (retained_launch < -0x147a || retained_launch > 0x147a) {
        throw UnsupportedSourceState("Initial retained local launch scalar exceeds source range");
    }
    std::array<std::int32_t, 16> initial_published{};
    if (publication_mode) {
        for (auto& value : initial_published) {
            value = integer();
        }
    }
    if (local_session) {
        for (auto& origin : race_timing->timers.origins) {
            origin = read_u32(journal);
        }
        race_timing->timers.clock = read_u32(journal);
        race_timing->release_flags[1] = read_u32(journal);
        if (race_timing->release_flags[1] != 1) {
            throw UnsupportedSourceState("Ordinary local finish requires the observed armed state10 entry flag");
        }
        if (clock) {
            clock->share_lifecycle(race_timing->timers, race_timing->start_phase, race_timing->state, race_timing->release_clock, race_timing->release_due);
        }
    }
    if (startup && publication_mode) {
        for (std::size_t actor = 0; actor < 16; ++actor) {
            if (initial_published[actor]) {
                throw std::runtime_error("Observed initial brake publication cannot enter startup join");
            }
            initial_published[actor] = source_i32(startup->actors[actor].snapshot.body, 0x4e8);
        }
    }
    const auto short_integer = [&] {
        std::array<std::uint8_t, 2> bytes;
        read(journal, bytes);
        return std::bit_cast<std::int16_t>(std::uint16_t(bytes[0] | std::uint16_t(bytes[1]) << 8));
    };
    std::array<audio::SourceWheelQueueActor, 16> wheels;
    std::array<audio::SourceSkidActor, 16> skids;
    std::array<std::array<std::uint8_t, 4>, 16> materials;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        wheels[actor].identity = skids[actor].identity = std::uint8_t(actor);
        for (auto& event : wheels[actor].queue.events) {
            event = short_integer();
        }
    }
    for (auto& actor : wheels) {
        for (auto& value : actor.queue.strengths) {
            value = integer();
        }
    }
    for (auto& actor : wheels) {
        for (auto& value : actor.state.current) {
            value = integer();
        }
    }
    for (auto& actor : wheels) {
        for (auto& value : actor.state.previous) {
            value = integer();
        }
    }
    for (auto& actor : skids) {
        actor.state.reset = integer();
    }
    std::array<std::uint8_t, 16> raw;
    read(journal, raw);
    for (std::size_t actor = 0; actor < 16; ++actor) {
        skids[actor].state.surface = raw[actor];
    }
    read(journal, raw);
    for (std::size_t actor = 0; actor < 16; ++actor) {
        skids[actor].state.mask = raw[actor];
    }
    for (auto& actor : materials) {
        read(journal, actor);
    }
    for (auto& actor : wheels) {
        actor.pending_mute = integer();
    }
    audio::SourceCameraMix camera{integer(), integer()};
    audio::SourceEngineState engine;
    engine.previous_gear = integer();
    engine.phase = read_u32(journal);
    engine.shift = integer();
    engine.whine_throttle = integer();
    engine.throttle = integer();
    engine.throttle_mirror = integer();
    engine.whine_volume = integer();
    const auto intervals = integer(), minimum = integer(), maximum = integer();
    if (intervals < 1 || intervals > 1024 || maximum <= minimum) {
        throw std::runtime_error("Combined original WHINE curve is invalid");
    }
    std::vector<std::uint16_t> rates(std::size_t(intervals) + 1);
    std::vector<std::uint8_t> gains(rates.size());
    for (auto& rate : rates) {
        rate = std::bit_cast<std::uint16_t>(short_integer());
    }
    read(journal, gains);
    std::array<std::int32_t, 24> offsets;
    for (auto& offset : offsets) {
        offset = integer();
    }
    const audio::SourceWhineCurve curve{minimum, maximum, rates, gains, offsets};
    const auto path = [&](const char* suffix) {
        return std::string(paths.output_prefix) + suffix;
    };
    std::ofstream rng_out(path(".combined-rng.csv")), audio_out(path(".combined-audio.csv")), engine_out(path(".combined-engine.csv"));
    std::ofstream route(path(".ai-route.csv")), progress(path(".joined-progress.csv")), joined(path(".joined-world.csv")), traffic(path(".traffic.csv")),
        traffic_world(path(".traffic-world.csv")), controls(path(".ai-controls.csv")), control_world(path(".ai-control-world.csv"));
    std::ofstream wheel_out(path(".wheel-queue.csv")), producer_out(path(".wheel-producers.csv")), order_out(path(".wheel-call-order.csv")), mute_out(path(".wheel-mute.csv")),
        dispatch_queue_out(path(".wheel-dispatch-queue.csv"));
    std::ofstream skid_out(path(".skid-events.csv")), skid_state_out(path(".skid-dispatch.csv")), whine_values(path(".whine-inputs.csv")), whine_state(path(".whine-state.csv")),
        whine_offsets(path(".whine-offset.csv")), whine_commands(path(".whine-commands.csv")), whine_updates(path(".whine-updates.csv"));
    std::ofstream contact_events(path(".contact-events.csv")), contact_commands(path(".contact-commands.csv"));
    std::ofstream driver_game(path(".driver-gameplay.csv")), driver_controls(path(".driver-controls.csv")), driver_world(path(".driver-world.csv"));
    std::ofstream actor_timing, checkpoint_timing, finish_timing, split_timing;
    std::ofstream local_session_output, local_controller_output, local_classification_output, local_handoff_output;
    if (local_session) {
        local_session_output.open(path(".session.csv"));
        local_controller_output.open(path(".session-controller.csv"));
        local_classification_output.open(path(".session-classification.csv"));
        local_handoff_output.open(path(".local-handoffs.csv"));
        for (auto* output : {&local_session_output, &local_controller_output, &local_classification_output, &local_handoff_output}) {
            output->exceptions(std::ios::failbit | std::ios::badbit);
        }
        local_session_output << "id,release_counter,release_epoch,race_running,launch_tire_0,launch_tire_1,launch_tire_2,launch_tire_3\n";
        local_controller_output << "id,actor,human,reverse_fallback,countdown,player,gear,queued_shift,mode_steering,mode_throttle,mode_brake,mode_transmission,"
                                   "saved_steering,saved_throttle,saved_brake,saved_transmission";
        for (unsigned tire = 0; tire < 8; ++tire) {
            local_controller_output << ",tire_offset_" << tire;
        }
        local_controller_output << ",raw_steering_left,raw_steering_right,steering_angle\n";
        local_classification_output << "id,release_marker";
        for (const auto& [name, size] : {std::pair{"release_flag_", 4}, {"split_enable_", 4}, {"classification_order_", 16}, {"score_", 16}, {"podium_", 3}}) {
            for (int value = 0; value < size; ++value) {
                local_classification_output << ',' << name << value;
            }
        }
        local_classification_output << ",configuration_624\n";
        write_handoff_header(local_handoff_output);
    }
    if (race_timing) {
        actor_timing.open(path(".ai-timing.csv"));
        checkpoint_timing.open(path(".checkpoint.csv"));
        finish_timing.open(path(".finish.csv"));
        split_timing.open(path(".split-display.csv"));
        for (auto* output : {&actor_timing, &checkpoint_timing, &finish_timing, &split_timing}) {
            output->exceptions(std::ios::failbit | std::ios::badbit);
        }
        actor_timing << "id,actor,lap,record_flag,best_ms,current_ms,lap_start_ms,best_minutes,best_seconds,best_hundredths,current_minutes,current_seconds,current_hundredths,"
                        "best_notice,finished,total_time_hundredths,notice\n";
        checkpoint_timing << "id,slot,clock,lap,actor,race_flags\n";
        finish_timing << "id,finish_count,local_finish_count,timer2,timer3,pending_state,race_stop";
        for (unsigned player = 0; player < 4; ++player) {
            finish_timing << ",local_finished_" << player;
        }
        for (unsigned actor = 0; actor < 16; ++actor) {
            finish_timing << ",finish_order_" << actor;
        }
        finish_timing << '\n';
        split_timing << "id,player,mode,leader,active,ticks,minutes,seconds,hundredths,reference_0,reference_1,reference_2,current_0,current_1,current_2,display,leader_"
                        "display,persisted_0,persisted_1,persisted_2\n";
    }
    std::ofstream human_lamps(path(".human-lamps.csv")), publications(path(".brake-publication.csv")), feedback(path(".drivetrain-feedback.csv")),
        rpm_feedback_output(path(".rpm-feedback.csv")), limiter_feedback_output(path(".limiter-feedback.csv")), pedal_feedback_output(path(".pedal-feedback.csv")),
        control_feedback_output(path(".control-feedback.csv"));
    for (auto* output : {&rng_out, &audio_out, &engine_out, &route, &progress, &joined, &traffic, &traffic_world, &controls, &control_world, &wheel_out, &producer_out, &order_out,
             &mute_out, &dispatch_queue_out, &skid_out, &skid_state_out, &whine_values, &whine_state, &whine_offsets, &whine_commands, &whine_updates, &contact_events,
             &contact_commands, &driver_game, &driver_controls, &driver_world, &human_lamps, &publications, &feedback, &rpm_feedback_output, &limiter_feedback_output,
             &pedal_feedback_output, &control_feedback_output}) {
        output->exceptions(std::ios::failbit | std::ios::badbit);
        if (!*output) {
            throw std::runtime_error("Cannot open combined original replay outputs");
        }
    }
    rng_out << "id,event,kind,rng\n";
    audio_out << "id,event,actor,dom,reset,mask,pending,material0,material1,material2,material3,current0,current1,current2,current3,previous0,previous1,previous2,previous3,event0,"
                 "event1,event2,event3,strength0,strength1,strength2,strength3,primary,secondary\n";
    engine_out << "id,event,previous_gear,phase,shift,ramp,throttle,mirror,volume\n";
    route << "id,actor,cache_point,target_x,target_y,target_z,alternate_x,alternate_y,alternate_z,desired_speed,alternate_speed,speed_variation,distance,projected_distance,angle,"
             "alternate_angle,route_angle,rng_final,route_next,route_previous,backwards,current_gate,previous_gate,refresh_ticks,refresh_requested,checkpoint,checkpoint_marker\n";
    progress
        << "id,actor,route_distance,distance_delta,rank,spatial_rank,display_rank,rank_ticks,ahead,behind,contact_ahead,contact_behind,traffic_flag,tire_countdown,tire_offset_0\n";
    joined << "id,release_counter\n";
    traffic << "id,actor,mode,blocked,brake,passing,steering_cached,forced_brake,passed_actor,blocker_actor,corridor_0,corridor_1,corridor_2,corridor_3,corner_0,corner_1,obstacle_"
               "angle_0,obstacle_angle_1,headlight_ticks,headlight_on_ticks,headlight_off_ticks,headlights,reverse,reverse_ticks,gear,reverse_pedal,raw_left,raw_right\n";
    traffic_world << "id,rng_final,traffic_counter\n";
    controls << "id,actor,raw_left,raw_right,raw_throttle,raw_brake,reverse,reverse_ticks,reaction_ticks,brake_ticks,gear,reverse_pedal,mode,steering_cached,obstacle_angle_0,"
                "obstacle_angle_1,headlights,brake_lights\n";
    control_world << "id,rng_final,steering_scratch\n";
    wheel_out << "id,callback,actor,wheel,event,strength,current,previous\n";
    producer_out << "id,callback,ordinal,actor,wheel,event,strength,current,previous\n";
    order_out << "id,callback,ordinal,actor\n";
    mute_out << "id,callback,actor,pending\n";
    dispatch_queue_out << "id,actor,wheel,event,strength,current,previous\n";
    skid_out << "id,actor,wheel,kind,sample,dispatch,restart,fade,rng,mask,reset\n";
    skid_state_out << "id,actor,mask,reset,rng\n";
    whine_values << "id,callback,player,stage,rpm,speed,gear,throttle,flag0,flag1\n";
    whine_state << "id,callback,player,stage,previous_gear,phase,shift_ticks,ramp,throttle,throttle_mirror,last_volume,rng\n";
    whine_offsets << "id,callback,player,offset\n";
    whine_commands << "id,callback,player,ordinal,kind,value\n";
    whine_updates << "id,previous_gear,phase,shift_ticks,ramp,throttle,throttle_mirror,last_volume,rng\n";
    contact_events << "id,event,consumer,kind,spatial,accepted,drew,sample,spatial_volume,volume,pan,angle,rng,primary,secondary,action\n";
    contact_commands << "id,event,ordinal,kind,sample,loops,volume,offset,pan,target,value\n";
    feedback << "id,event,actor,gear,pending_gear,pending_flag,reverse_pedal\n";
    rpm_feedback_output << "id,event,actor,engine_speed,previous_engine_speed\n";
    limiter_feedback_output << "id,event,actor,limiter_active,limiter_load_q16\n";
    pedal_feedback_output << "id,event,actor,throttle_step_q16,brake_step_q16,throttle_ramp_q16,brake_ramp_q16,throttle_q16,brake_q16\n";
    control_feedback_output << "id,event,actor,return_step_q16,accumulator_q16,angle\n";
    driver_game << "id,event,actor,word,value\n";
    driver_controls << "id,event,actor,left,right,throttle,brake,gear,reverse_pedal,headlights,brake_lights,tire0,tire1,tire2,tire3,tire4,tire5,tire6,tire7\n";
    driver_world << "id,event,kind,word,value\n";
    human_lamps << "id,event,actor,view,human_pre,human_post,published_pre,published_post,pedal_pre,pedal_post,lamp_pre,lamp_post,rng_pre,rng_post\n";
    const auto engine_columns = [&](std::ostream& output, bool with_rng) {
        output << engine.previous_gear << ',' << engine.phase << ',' << engine.shift << ',' << engine.whine_throttle << ',' << engine.throttle << ',' << engine.throttle_mirror
               << ',' << engine.whine_volume;
        if (with_rng) {
            output << ',' << random.state();
        }
        output << '\n';
    };
    publications << "id,event,call,stage,actor,raw_brake,published_brake,rng\n";
    std::optional<std::uint32_t> active_publication;
    RaceReleaseCounter release(initial_release);
    RetainedDrivers driver_state;
    driver_state.retain_published_brake = publication_mode;
    driver_state.retain_drivetrain = options.drivetrain_feedback;
    driver_state.retain_rpm = options.rpm_feedback;
    driver_state.retain_pedals = options.pedal_feedback;
    driver_state.retain_steering = options.control_feedback;
    driver_state.retain_physical = options.physical_feedback;
    driver_state.retain_dispatch = options.dispatch_feedback;
    std::array<bool, 16> limiter_active{};
    std::array<std::int32_t, 16> limiter_load{};
    std::ofstream physical_feedback_output;
    if (options.physical_feedback) {
        physical_feedback_output.open(path(".physical-feedback.csv"));
        physical_feedback_output.exceptions(std::ios::failbit | std::ios::badbit);
        physical_feedback_output << "id,event,actor";
        physical_feedback_columns(physical_feedback_output, SourceSnapshot{}, true);
        physical_feedback_output << '\n';
    }
    std::ofstream dispatch_output;
    if (options.dispatch_feedback) {
        dispatch_output.open(path(".dispatch-feedback.csv"));
        dispatch_output.exceptions(std::ios::failbit | std::ios::badbit);
        dispatch_output << "id,event,actor,mode\n";
    }
    const auto reject_dispatch = [](const Record& record, bool initial_physical, bool has_ai) {
        if (record.selection_context.alternate_direction) {
            throw std::runtime_error("Observed selector direction cannot enter dispatch feedback");
        }
        for (std::size_t actor = 0; actor < 16; ++actor) {
            const auto& input = record.selection[actor];
            const auto populated = [](const auto& raw) {
                return std::ranges::any_of(raw, [](auto value) {
                    return value != 0;
                });
            };
            if ((has_ai && populated(record.ai_actors[actor].gameplay)) || populated(record.actors[actor].snapshot.gameplay)) {
                throw std::runtime_error("Unused driver initialization copies cannot enter dispatch replay");
            }
            if (input.previous != BodyMode::full || input.human_control || input.player_identity || input.rollover.upside_down || input.rollover.sideways || input.support_count
                || input.first_tire_q16 || input.normal_up_q16 || input.ahead || input.behind || input.forward_gap_q16 || record.ai_actors[actor].body_mode
                || source_u32(record.traffic.actors[actor].body, 0x4f0) || (!initial_physical && source_u32(record.actors[actor].snapshot.body, 0x4f0))) {
                throw std::runtime_error("Observed body mode/selector feedback cannot enter dispatch replay");
            }
        }
    };
    std::array<SourceSnapshot, 16> physical_configuration;
    std::optional<WarmGridInitInputs> warm_context;
    std::size_t event_total = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        auto record = read_record(input, steering, local, flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], flags[6], flags[7], options.limiter_feedback,
            startup && index == 0, clock.has_value());
        if (scene_owned) {
            reject_scene_inputs(record);
        }
        if (race_timing) {
            prepare_retained_race_timing(record, *race_timing);
        }
        if (warm_restart && index == 0) {
            if (!clock && record.timing_clock != race_timing->timers.clock) {
                throw std::runtime_error("Initial FLET caller clock disagrees with lifecycle cached-clock context");
            }
            warm_context = warm_grid_context(assets, initial, record, track, local, weather, steering);
        }
        if (local_session) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                for (const auto offset : {0x27c, 0x310, 0x314, 0x318}) {
                    if (source_u32(record.actors[actor].snapshot.dynamics, offset) || (index != 0 && source_u32(record.traffic.actors[actor].dynamics, offset))) {
                        throw std::runtime_error("Controller mode outputs must initialize only in the first retained driver vector");
                    }
                }
            }
        }
        if (clock) {
            record.time = clock->step(record.id);
            if (local_session) {
                record.timing_clock = clock->caller_clock();
            } else {
                record.traffic.session_state = std::int32_t(clock->session_state());
                record.update_caller = clock->update_caller();
                if (clock->release_due()) {
                    record.releases.push_back({0x4220a6, 0, 2, 1});
                }
            }
        }
        if (options.dispatch_feedback) {
            reject_dispatch(record, false, true);
        }
        if (options.physical_feedback) {
            reject_physical_world(record, options.startup_world_reset);
            for (std::size_t actor = 0; actor < 16; ++actor) {
                reject_physical_fields(record.actors[actor].snapshot);
                if (index != 0) {
                    reject_physical_fields(record.traffic.actors[actor]);
                }
                const auto& ai = record.ai_actors[actor];
                bool populated = ai.speed_q16 != 0;
                for (auto value : ai.body.position) {
                    populated |= value != 0;
                }
                for (const auto& row : ai.body.basis) {
                    for (auto value : row) {
                        populated |= value != 0;
                    }
                }
                if (populated) {
                    throw std::runtime_error("Observed route body feedback cannot enter retained replay");
                }
            }
        }
        if (options.control_feedback) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                const auto populated = [](const auto& raw) {
                    return source_u32(raw, 0x44) || source_u32(raw, 0x150) || source_u16(raw, 0x274);
                };
                if (populated(record.actors[actor].snapshot.dynamics) || (index != 0 && populated(record.traffic.actors[actor].dynamics))) {
                    throw std::runtime_error("Later steering feedback cannot enter retained replay");
                }
            }
        }
        if (options.pedal_feedback) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                for (const auto offset : {0x14, 0x38, 0x120, 0x134, 0x11c, 0x130}) {
                    if (source_u32(record.actors[actor].snapshot.dynamics, offset) != 0 || (index != 0 && source_u32(record.traffic.actors[actor].dynamics, offset) != 0)) {
                        throw std::runtime_error("Later pedal feedback cannot enter retained replay");
                    }
                }
            }
        }
        if (options.limiter_feedback) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                const auto& snapshot = record.actors[actor].snapshot;
                if (index == 0) {
                    if (options.startup_world_reset && (snapshot.limiter_active || snapshot.limiter_load_q16)) {
                        throw std::runtime_error("Observed initial limiter context cannot enter cold replay");
                    }
                    limiter_active[actor] = options.startup_world_reset ? startup->actors[actor].snapshot.limiter_active : snapshot.limiter_active;
                    limiter_load[actor] = options.startup_world_reset ? startup->actors[actor].snapshot.limiter_load_q16 : snapshot.limiter_load_q16;
                } else if (snapshot.limiter_active || snapshot.limiter_load_q16 != 0) {
                    throw std::runtime_error("Later limiter feedback cannot enter retained replay");
                }
            }
        }
        if (record.rng != 0 || record.ai_rng != 0 || record.traffic.rng != 0) {
            throw std::runtime_error("Combined fleet input must omit all intermediate random seeds");
        }
        if (startup && (record.ai_release_counter || record.traffic.release_counter)) {
            throw std::runtime_error("Observed per-update release cannot enter startup join");
        }
        if (retained_lamps_mode
            && (record.launch_flags != 0 || record.consumed_launch != 0 || record.pre_launch != 0 || record.pre_rpm != 0 || record.pre_start_hold != 0 || record.release_epoch != 0
                || record.pre_session_state != 0 || record.pre_controller_owner != 0)) {
            throw std::runtime_error("Retained launch cannot import per-update launch metadata");
        }
        const auto id = read_u32(journal), events = read_u32(journal);
        if (id != index || record.id != id || events > 10000) {
            throw std::runtime_error("Combined original update order/capacity is invalid");
        }
        if (publication_mode) {
            for (std::size_t actor = 0; actor < 16; ++actor) {
                if (source_u32(record.traffic.actors[actor].body, 0x4e8) != 0 || source_u32(record.actors[actor].snapshot.body, 0x4e8) != 0) {
                    throw std::runtime_error("Published brake must initialize only from the first PRE sidecar vector");
                }
            }
        }
        if (startup && index == 0) {
            if (options.startup_world_reset) restore_startup_cold_actor_context(record, *startup, true);
            restore_startup_record(record, *startup, options.startup_selection);
            if (options.startup_control_calibration) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    restore_startup_control_calibration(record.traffic.actors[actor], startup->actors[actor]);
                    restore_startup_control_calibration(record.actors[actor].snapshot, startup->actors[actor]);
                }
            }
            if (options.startup_contact_cache) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    restore_startup_contact_cache(record.traffic.actors[actor], startup->actors[actor]);
                    restore_startup_contact_cache(record.actors[actor].snapshot, startup->actors[actor]);
                }
            }
            if (options.startup_physical_body) {
                prepare_startup_selection_context(record, *startup);
            } else if (options.startup_selection) {
                std::ofstream selection_output(std::string(paths.output_prefix) + ".startup-selection.csv");
                selection_output.exceptions(std::ios::failbit | std::ios::badbit);
                select_startup_bodies(record, *startup, random, selection_output);
            }
        }
        if (retained_driver_mode) {
            if (!driver_state.initialized) {
                driver_state.actors = record.traffic.actors;
                driver_state.status = record.traffic.status;
                driver_state.braking = record.traffic.braking_memory;
                driver_state.initialized = true;
                if (publication_mode) {
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        source_store(driver_state.actors[actor].body, 0x4e8, std::uint32_t(initial_published[actor]));
                    }
                }
            } else {
                const auto zero = [](std::span<const std::uint8_t> bytes) {
                    return std::ranges::all_of(bytes, [](auto value) {
                        return value == 0;
                    });
                };
                if (!zero(record.traffic.status) || !zero(record.traffic.braking_memory)) {
                    throw std::runtime_error("Later driver globals cannot enter a retained replay");
                }
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    const auto& current = record.traffic.actors[actor];
                    auto& retained = driver_state.actors[actor];
                    if (!zero(current.gameplay) || !zero(record.ai_actors[actor].gameplay) || !zero(record.actors[actor].snapshot.gameplay) || source_u32(current.body, 0x500) != 0
                        || source_u32(current.body, 0x508) != 0) {
                        throw std::runtime_error("Later driver outputs cannot enter a retained replay");
                    }
                    if (publication_mode && (source_u32(current.dynamics, 0x144) != 0 || source_u32(record.actors[actor].snapshot.dynamics, 0x144) != 0)) {
                        throw std::runtime_error("Later human/AI raw brake cannot enter a retained publication replay");
                    }
                    if (!options.drivetrain_feedback) {
                        retained.dynamics[0x330] = current.dynamics[0x330]; //Observed physical gearbox.
                        source_store(retained.dynamics, 0x304, source_u32(current.dynamics, 0x304));
                    } else if (current.dynamics[0x330] || current.dynamics[0x334] || source_u32(current.dynamics, 0x30c) || source_u32(current.dynamics, 0x304)) {
                        throw std::runtime_error("Later drivetrain feedback cannot enter retained replay");
                    }
                    if (options.rpm_feedback
                        && (source_u32(current.dynamics, 0x124) || source_u32(current.dynamics, 0x128) || source_u32(record.actors[actor].snapshot.dynamics, 0x124)
                            || source_u32(record.actors[actor].snapshot.dynamics, 0x128))) {
                        throw std::runtime_error("Later RPM feedback cannot enter retained replay");
                    }
                    if (!zero(std::span(current.dynamics).subspan(0xdc, 32))) {
                        throw std::runtime_error("Later tire outputs cannot enter a retained replay");
                    }
                    if (source_u32(retained.gameplay, 8) == 0 || options.dispatch_feedback) {
                        //The current external human mapper is journalled only
                        //after403dd0; no snapshot may replace previous PRE axes.
                        if (!zero(std::span(current.dynamics).subspan(0x138, 16))) {
                            throw std::runtime_error("Later AI tire or mapped control outputs cannot enter a retained replay");
                        }
                    } else {
                        //Human input is explicit context, not AiDriver output.
                        for (std::size_t offset = 0x138; offset < 0x148; offset += 4) {
                            if (publication_mode && offset == 0x144) {
                                continue;
                            }
                            source_store(retained.dynamics, offset, source_u32(current.dynamics, offset));
                        }
                    }
                }
            }
        }
        std::optional<Record> continuous_physical;
        if (options.physical_feedback) {
            continuous_physical = read_record(physical_input, steering, local, false, false, false, false, false, false, false, false, options.limiter_feedback,
                options.startup_physical_body && index == 0, clock.has_value());
            if (scene_owned) {
                reject_scene_inputs(*continuous_physical);
            }
            if (clock) {
                continuous_physical->time = record.time;
            }
            if (options.dispatch_feedback) {
                reject_dispatch(*continuous_physical, index == 0, false);
            }
            if (continuous_physical->id != id || continuous_physical->rng) {
                throw std::runtime_error("Physical stream changed chronology or imported RNG");
            }
            if (local_session) {
                for (const auto& actor : continuous_physical->actors) {
                    for (const auto offset : {0x27c, 0x310, 0x314, 0x318}) {
                        if (source_u32(actor.snapshot.dynamics, offset)) {
                            throw std::runtime_error("Physical source modes cannot replace a generated controller handoff");
                        }
                    }
                }
            }
            if (options.startup_world_reset) {
                reject_physical_world(*continuous_physical, true);
                if (index == 0) restore_startup_cold_actor_context(*continuous_physical, *startup, false);
            }
            if (options.startup_control_calibration && index == 0) {
                //Regenerate this otherwise-discarded duplicate before retaining
                //the configuration checked against later physical records.
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    restore_startup_control_calibration(continuous_physical->actors[actor].snapshot, startup->actors[actor]);
                }
            }
            if (options.startup_contact_cache && index == 0) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    restore_startup_contact_cache(continuous_physical->actors[actor].snapshot, startup->actors[actor]);
                }
            }
            for (std::size_t actor = 0; actor < 16; ++actor) {
                if (index == 0) {
                    physical_configuration[actor] = continuous_physical->actors[actor].snapshot;
                } else {
                    validate_configuration(physical_configuration[actor], continuous_physical->actors[actor].snapshot, id, actor, local);
                }
            }
            if (index == 0) {
                auto initial_physical = *continuous_physical;
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    auto& snapshot = initial_physical.actors[actor].snapshot;
                    //One PRE world initializes Motion; all retained controls also
                    //initialize from that same driver PRE before source events.
                    snapshot.dynamics = driver_state.actors[actor].dynamics;
                    snapshot.gameplay = driver_state.actors[actor].gameplay;
                    snapshot.limiter_active = limiter_active[actor];
                    snapshot.limiter_load_q16 = limiter_load[actor];
                    snapshot.launch_tire_adjustment_q16 = actor == local ? retained_launch : 0;
                }
                if (options.startup_physical_body) {
                    restore_startup_physical_body(initial_physical, record, *startup);
                    //Keep the effective PRE record for later physical consumption;
                    //dynamics/gameplay above still come from retained drivers.
                    continuous_physical->selection = initial_physical.selection;
                    continuous_physical->selection_context = initial_physical.selection_context;
                    continuous_physical->ai_tire_base = initial_physical.ai_tire_base;
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        continuous_physical->actors[actor].snapshot.body = initial_physical.actors[actor].snapshot.body;
                    }
                }
                const auto initialized_world = options.startup_world_reset ? physical_fleet->initial_world_values(&*startup) : std::vector<InitialWorldValue>{};
                physical_fleet->restore(initial_physical, options.startup_world_reset);
                if (options.startup_world_reset) {
                    std::ofstream world_output(std::string(paths.output_prefix) + ".startup-world-reset.csv");
                    world_output.exceptions(std::ios::failbit | std::ios::badbit);
                    emit_startup_world_reset(world_output, initialized_world, physical_fleet->initial_world_values());
                }
                if (options.startup_contact_cache) {
                    std::array<SourceSnapshot, 16> generated, restored;
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        generated[actor] = startup->actors[actor].snapshot;
                    }
                    physical_fleet->publish_initial_body(restored);
                    std::ofstream cache_output(std::string(paths.output_prefix) + ".startup-contact-cache.csv");
                    cache_output.exceptions(std::ios::failbit | std::ios::badbit);
                    cache_output << "phase,actor,field,value\n";
                    emit_startup_contact_cache(cache_output, "first_pre", generated);
                    emit_startup_contact_cache(cache_output, "restored", restored);
                }
                if (options.startup_control_calibration) {
                    std::array<SourceSnapshot, 16> generated, restored;
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        generated[actor] = startup->actors[actor].snapshot;
                    }
                    physical_fleet->publish_initial_control_calibration(restored);
                    std::ofstream calibration_output(std::string(paths.output_prefix) + ".startup-control-calibration.csv");
                    calibration_output.exceptions(std::ios::failbit | std::ios::badbit);
                    calibration_output << "phase,actor,field,value\n";
                    emit_startup_control_calibration(calibration_output, "first_pre", generated);
                    emit_startup_control_calibration(calibration_output, "restored", restored);
                }
                if (options.startup_physical_body) {
                    std::array<SourceSnapshot, 16> restored;
                    physical_fleet->publish_initial_body(restored);
                    std::ofstream body_output(std::string(paths.output_prefix) + ".startup-physical-body.csv");
                    body_output.exceptions(std::ios::failbit | std::ios::badbit);
                    emit_startup_physical_body(body_output, restored);
                }
                if (options.dispatch_feedback) {
                    physical_fleet->publish_dispatch(driver_state.actors);
                }
            } else {
                reject_physical_world(*continuous_physical);
                for (const auto& actor : continuous_physical->actors) {
                    reject_physical_fields(actor.snapshot);
                }
            }
        }
        if (options.physical_feedback) {
            physical_fleet->copy_pair_history(record.pair_history);
        }
        std::uint32_t wheel_callback = 0, motor_callback = 0, ai_calls = 0, tire_calls = 0, physical_calls = 0, dispatch_calls = 0, release_callbacks = 0, camera_calls = 0;
        bool wrapper_clock_installed = false;
        const auto install_wrapper_clock = [&] {
            if ((!warm_restart && !(clock && local_session)) || wrapper_clock_installed) {
                return;
            }
            if (clock) {
                race_timing->primary_update = clock->primary();
            }
            race_timing->before_update(record.timing_clock);
            record.timing_state = race_timing->state;
            record.traffic.session_state = race_timing->state;
            record.traffic.race_stop = race_timing->finish.stopped;
            record.finish = race_timing->finish;
            record.finish_release_clock = race_timing->release_clock;
            record.timing_running = race_timing->running;
            record.timing_flags = race_timing->flags;
            record.displays = race_timing->displays;
            record.checkpoint_times = race_timing->checkpoints;
            if (clock) {
                record.update_caller = race_timing->state == 9    ? index == 0 || race_timing->warm_grid_first ? 0x4488a1u : 0x4488d0u
                                       : race_timing->state == 6  ? 0x448434u
                                       : race_timing->state == 10 ? 0x448c46u
                                                                  : 0x44a336u;
                if (race_timing->release_due) {
                    record.releases.push_back({0x4220a6, 0, std::uint32_t(race_timing->stage), 1});
                }
            }
            wrapper_clock_installed = true;
        };
        for (std::uint32_t event = 0; event < events; ++event) {
            const auto kind = read_u32(journal);
            bool driver_checkpoint = false, control_checkpoint = false;
            bool completed_wrapper = false;
            if (kind == 0) {
                const auto phase = read_u32(journal);
                if (phase > 6) {
                    throw std::runtime_error("Combined original boundary is invalid");
                }
                if (phase == 0) {
                    if (scene_owned) {
                        if (!clock->primary()) {
                            //Render RNG and the old-scene wheel callbacks in
                            //this record's prefix precede the optional extra.
                            flush_scene();
                        }
                        const auto& generated = scene_frames->step();
                        if (current_scene || generated.delta_q16 != record.time.delta_q16 || generated.reciprocal_q16 != record.time.reciprocal_q16
                            || generated.extra_step != record.time.extra_step) {
                            throw std::runtime_error("Generated scene and physical clocks disagree at421aa0 PRE");
                        }
                    }
                    install_wrapper_clock();
                    if (retained_driver_mode && record.update_caller == 0x448434 && source_u32(driver_state.status, 12) == 0) {
                        if (retained_lamps_mode) {
                            //4483da→402c80→402b90 precedes401260 on the
                            //first active wrapper. It preserves the launch scalar.
                            auto& raw = driver_state.actors[local];
                            ControllerHandoffState handoff;
                            handoff.player = std::bit_cast<std::int8_t>(raw.gameplay[0x80]);
                            if (options.physical_feedback) {
                                physical_fleet->local_boundary({source_i32(raw.dynamics, 0x148) != 0, false, false, true});
                            }
                            const auto restored = apply_controller_handoff(handoff, ControllerHandoffKind::player, 0);
                            for (std::size_t tire = 0; tire < restored.tires.size(); ++tire) {
                                source_store(raw.dynamics, 0xdc + tire * 4, std::uint32_t(restored.tires[tire]));
                            }
                        }
                        source_store(driver_state.status, 12, 1); //448411→401260(0), guarded by the source started latch.
                        for (std::size_t actor = 0; actor < 16; ++actor) {
                            driver_state.actors[actor].gameplay[0x87] = 0;
                            if (options.drivetrain_feedback
                                && (std::bit_cast<std::int8_t>(driver_state.actors[actor].gameplay[0x80]) == -1 || source_i32(record.traffic.actors[actor].dynamics, 0x27c) != 1)) {
                                driver_state.actors[actor].dynamics[0x330] = 1; //401260 automatic release store; pending and304 survive.
                            }
                            if (driver_state.drivers[actor]) {
                                driver_state.drivers[actor]->release_start();
                            }
                        }
                    }
                    whine_updates << id << ',';
                    engine_columns(whine_updates, true);
                    if (scene_owned) {
                        if (scene_lifecycle) {
                            if (clock->primary()) {
                                scene_frames->set_lifecycle({std::uint32_t(race_timing->state), std::bit_cast<std::int32_t>(race_timing->timers.read(0))});
                            }
                            scene_test::write_lifecycle_pre(scene_output, id, *scene_frames);
                        }
                        current_scene = ScenePhysicalBoundary{};
                        current_scene->session_state = std::uint32_t(record.traffic.session_state);
                        current_scene->before = physical_fleet->local_pose();
                        current_scene->previous_velocity = physical_fleet->local_velocity();
                        scene_frames->begin_physical(current_scene->before, current_scene->session_state);
                        if (!camera_controls) {
                            scene_frames->camera_controls({});
                        }
                    }
                }
                if (options.dispatch_feedback && phase == 1) {
                    if (++dispatch_calls != 1 || ai_calls != 0) {
                        throw std::runtime_error("Body selection must occur exactly once before the generated AI pass");
                    }
                    if (options.startup_physical_body && index == 0) {
                        std::array<BodySelectionActor, 16> inputs;
                        const auto before = random.state();
                        const auto selected = physical_fleet->select_dispatch(record.selection_context, driver_state.actors, &inputs);
                        std::ofstream selection_output(std::string(paths.output_prefix) + ".startup-selection.csv");
                        selection_output.exceptions(std::ios::failbit | std::ios::badbit);
                        emit_startup_selection(selection_output, inputs, selected, before, random.state());
                    } else {
                        physical_fleet->select_dispatch(record.selection_context, driver_state.actors);
                    }
                }
                if (retained_driver_mode && phase == 3) {
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        if (source_u32(driver_state.actors[actor].gameplay, 8) == 0) {
                            auto& driver = *driver_state.drivers[actor];
                            driver.apply_source_motion_steering_gate(
                                source_i32(options.physical_feedback ? driver_state.actors[actor].body : record.traffic.actors[actor].body, 0xe0),
                                source_u32(options.dispatch_feedback ? driver_state.actors[actor].body : record.traffic.actors[actor].body, 0x4f0) == 0);
                            const auto axes = driver.mapped_control_state().axes;
                            source_store(driver_state.actors[actor].dynamics, 0x138, std::uint32_t(axes.left_q16));
                            source_store(driver_state.actors[actor].dynamics, 0x13c, std::uint32_t(axes.right_q16));
                        }
                    }
                }
                if (options.physical_feedback && (phase == 3 || phase == 4)) {
                    physical_fleet->publish_physical(driver_state.actors, phase == 4, options.dispatch_feedback);
                }
                if (options.drivetrain_feedback && phase == 3) {
                    physical_fleet->copy_drivetrain(driver_state.actors, options.rpm_feedback);
                }
                if (options.control_feedback && phase == 3) {
                    physical_fleet->copy_steering(driver_state.actors);
                }
                if (options.pedal_feedback && phase == 3) {
                    physical_fleet->copy_pedals(driver_state.actors);
                }
                if (options.limiter_feedback && phase == 3) {
                    physical_fleet->copy_limiter(limiter_active, limiter_load);
                }
                if (paths.physical && phase == 2) {
                    if (++physical_calls != 1 || ai_calls != 1 || (options.dispatch_feedback && dispatch_calls != 1)) {
                        throw std::runtime_error("Physical entry must follow exactly one generated driver pass");
                    }
                    auto physical = options.physical_feedback
                                        ? *continuous_physical
                                        : read_record(physical_input, steering, local, false, false, false, false, false, false, false, false, options.limiter_feedback);
                    if (physical.id != id || physical.rng != 0) {
                        throw std::runtime_error("Physical stream imported a seed or changed update chronology");
                    }
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        auto& snapshot = physical.actors[actor].snapshot;
                        const auto& generated = driver_state.actors[actor];
                        const auto zero = [](auto bytes) {
                            return std::ranges::all_of(bytes, [](auto value) {
                                return value == 0;
                            });
                        };
                        if (!zero(snapshot.gameplay) || !zero(std::span(snapshot.dynamics).subspan(0xdc, 32)) || !zero(std::span(snapshot.dynamics).subspan(0x138, 16))
                            || snapshot.dynamics[0x330] != 0 || source_u32(snapshot.body, 0x500) != 0 || source_u32(snapshot.body, 0x508) != 0
                            || (publication_mode && source_u32(snapshot.body, 0x4e8) != 0)) {
                            throw std::runtime_error("Physical stream cannot import generated driver outputs");
                        }
                        if (options.drivetrain_feedback) {
                            if (snapshot.dynamics[0x334] || source_u32(snapshot.dynamics, 0x30c) || source_u32(snapshot.dynamics, 0x304)) {
                                throw std::runtime_error("Physical PRE cannot import drivetrain feedback");
                            }
                            snapshot.dynamics[0x334] = generated.dynamics[0x334];
                            source_store(snapshot.dynamics, 0x30c, source_u32(generated.dynamics, 0x30c));
                            source_store(snapshot.dynamics, 0x304, source_u32(generated.dynamics, 0x304));
                        }
                        if (options.rpm_feedback) {
                            if (source_u32(snapshot.dynamics, 0x124) || source_u32(snapshot.dynamics, 0x128)) {
                                throw std::runtime_error("Physical PRE cannot import RPM feedback");
                            }
                            source_store(snapshot.dynamics, 0x124, source_u32(generated.dynamics, 0x124));
                            source_store(snapshot.dynamics, 0x128, source_u32(generated.dynamics, 0x128));
                        }
                        if (options.limiter_feedback) {
                            if (snapshot.limiter_active || snapshot.limiter_load_q16 != 0) {
                                throw std::runtime_error("Physical PRE cannot import limiter feedback");
                            }
                            snapshot.limiter_active = limiter_active[actor];
                            snapshot.limiter_load_q16 = limiter_load[actor];
                        }
                        if (options.pedal_feedback) {
                            for (const auto offset : {0x14, 0x38, 0x120, 0x134, 0x11c, 0x130}) {
                                if (source_u32(snapshot.dynamics, offset) != 0) {
                                    throw std::runtime_error("Physical PRE cannot import pedal feedback");
                                }
                                source_store(snapshot.dynamics, offset, source_u32(generated.dynamics, offset));
                            }
                        }
                        if (options.control_feedback) {
                            if (source_u32(snapshot.dynamics, 0x44) || source_u32(snapshot.dynamics, 0x150) || source_u16(snapshot.dynamics, 0x274)) {
                                throw std::runtime_error("Physical PRE cannot import steering feedback");
                            }
                            source_store(snapshot.dynamics, 0x44, source_u32(generated.dynamics, 0x44));
                            source_store(snapshot.dynamics, 0x150, source_u32(generated.dynamics, 0x150));
                            source_store(snapshot.dynamics, 0x274, source_u16(generated.dynamics, 0x274), 2);
                        }
                        if (options.dispatch_feedback) {
                            source_store(snapshot.body, 0x4f0, source_u32(generated.body, 0x4f0));
                        }
                        snapshot.gameplay = generated.gameplay;
                        std::copy_n(generated.dynamics.begin() + 0xdc, 32, snapshot.dynamics.begin() + 0xdc);
                        std::copy_n(generated.dynamics.begin() + 0x138, 16, snapshot.dynamics.begin() + 0x138);
                        snapshot.dynamics[0x330] = generated.dynamics[0x330]; //Only the gear byte; preserve physical rollover neighbors.
                        if (local_session) {
                            for (const auto offset : {0x27c, 0x310, 0x314, 0x318}) {
                                source_store(snapshot.dynamics, offset, source_u32(generated.dynamics, offset));
                            }
                        }
                        for (const auto offset : {0x500u, 0x508u}) {
                            source_store(snapshot.body, offset, source_u32(generated.body, offset));
                        }
                        if (publication_mode) {
                            source_store(snapshot.body, 0x4e8, source_u32(generated.body, 0x4e8));
                        }
                    }
                    if (retained_lamps_mode) {
                        physical.actors[local].snapshot.launch_tire_adjustment_q16 = retained_launch;
                    }
                    //Independent physical transaction: every PRE pose/contact/force,
                    //other filter, inertia, damage, pair/prop and body-selection context stays observed.
                    //Mapped SourceInput does not call422f60 or any AI/audio RNG consumer.
                    if (!options.physical_feedback) {
                        physical_fleet->restore(physical);
                    }
                    if (options.physical_feedback && retained_lamps_mode && (!local_session || source_u32(driver_state.actors[local].gameplay, 8) != 0)) {
                        physical_fleet->local_boundary({source_i32(physical.actors[local].snapshot.dynamics, 0x148) != 0, false, true, false});
                    }
                    physical_fleet->step(
                        physical, physical_output, physical_detached, physical_launch, physical_selection, true, false, options.dispatch_feedback, clock ? &*clock : nullptr,
                        scene_owned ? &scene_frames->retained_context() : nullptr);
                }
                if (scene_owned && phase == 4) {
                    if (!current_scene || physical_calls != 1) {
                        throw std::runtime_error("Generated scene POST requires one retained physical pass and its generated PRE body");
                    }
                    physical_fleet->scene_boundary(*current_scene);
                    scene_frames->complete_physical(*current_scene);
                    ++scene_physical_count;
                    if (clock->primary()) {
                        if (pending_scene) {
                            throw std::runtime_error("A generated primary scene publication was left pending");
                        }
                        pending_scene = true;
                    }
                    current_scene.reset();
                }
                driver_checkpoint = phase == 0 || phase == 1 || phase == 3 || phase == 6;
                control_checkpoint = phase == 1;
                completed_wrapper = local_session && phase == 3;
            } else if (kind == 1) {
                if (options.dispatch_feedback && dispatch_calls != 1) {
                    throw std::runtime_error("Generated AI requires exactly one pre-AI body selection");
                }
                std::array<std::optional<DriverAxes>, 16> human_mapping;
                if (retained_driver_mode) {
                    const auto journal_clock = read_u32(journal);
                    if (warm_restart || (clock && local_session)) {
                        if (journal_clock != 0 || !wrapper_clock_installed) {
                            throw std::runtime_error("Warm caller clock must enter once through FLET before the wrapper's PRE");
                        }
                    } else {
                        record.timing_clock = journal_clock; //Observed wall-clock input, not a captured numeric driver output.
                    }
                    if (clock) {
                        if (!local_session && record.timing_clock != 0) {
                            throw std::runtime_error("Observed AI clock cannot enter source lifecycle transactions");
                        }
                        record.timing_clock = clock->caller_clock();
                    }
                    if (local_session) {
                        if (index == 0 && record.timing_clock != race_timing->timers.clock) {
                            throw std::runtime_error("Initial lifecycle cache disagrees with the first external AI caller clock");
                        }
                        if (!warm_restart && !clock) {
                            race_timing->before_update(record.timing_clock);
                        }
                        record.timing_state = race_timing->state;
                        record.traffic.session_state = race_timing->state;
                        record.traffic.race_stop = race_timing->finish.stopped;
                        record.finish = race_timing->finish;
                        record.displays = race_timing->displays;
                        record.checkpoint_times = race_timing->checkpoints;
                        record.timing_flags = race_timing->flags;
                    }
                    const auto mapped_actors = read_u32(journal);
                    if (mapped_actors > 4) {
                        throw std::runtime_error("Human input mapping exceeds source player allocation");
                    }
                    for (std::uint32_t mapped = 0; mapped < mapped_actors; ++mapped) {
                        const auto actor = read_u32(journal);
                        if (actor >= 16 || human_mapping[actor] || (!local_session && source_u32(driver_state.actors[actor].gameplay, 8) == 0)
                            || std::bit_cast<std::int8_t>(driver_state.actors[actor].gameplay[0x80]) < 0) {
                            throw std::runtime_error("External human mapper requires a retained human player identity");
                        }
                        human_mapping[actor] = DriverAxes{integer(), integer(), integer(), integer()};
                    }
                }
                if (++ai_calls != 1) {
                    throw std::runtime_error("Combined original update must have one joined AI pass");
                }
                compare_joined_ai(initial, record, local, release, route, progress, joined, traffic, traffic_world, controls, control_world, &random, version >= 2,
                    retained_driver_mode ? &driver_state : nullptr, retained_lamps_mode ? &retained_launch : nullptr, race_timing ? &*race_timing : nullptr,
                    race_timing ? &actor_timing : nullptr, race_timing ? &checkpoint_timing : nullptr, race_timing ? &finish_timing : nullptr,
                    race_timing ? &split_timing : nullptr);
                if (local_session) {
                    write_retained_local_session(
                        record, *race_timing, release.value(), local_session_output, local_controller_output, local_classification_output, local_handoff_output);
                    if (race_timing->handoff && options.physical_feedback) {
                        physical_fleet->local_handoff_to_ai(record.ai_tire_base);
                    }
                }
                for (std::size_t actor = 0; retained_driver_mode && actor < 16; ++actor) {
                    const auto& game = driver_state.actors[actor].gameplay;
                    const auto human_player = source_u32(game, 8) != 0 && std::bit_cast<std::int8_t>(game[0x80]) >= 0;
                    if (human_player != human_mapping[actor].has_value()) {
                        throw std::runtime_error("External human mapping must cover every generated human player exactly once");
                    }
                }
                for (std::size_t actor = 0; actor < human_mapping.size(); ++actor) {
                    if (human_mapping[actor]) {
                        //421aa0's descending actor loop calls422f60 for human
                        //input between403dd0 return and415090 entry. The four
                        //mapped axes are explicit external inputs, not driver outputs.
                        auto& raw = driver_state.actors[actor];
                        const auto axes = *human_mapping[actor];
                        source_store(raw.dynamics, 0x138, std::uint32_t(axes.left_q16));
                        source_store(raw.dynamics, 0x13c, std::uint32_t(axes.right_q16));
                        source_store(raw.dynamics, 0x140, std::uint32_t(axes.throttle_q16));
                        source_store(raw.dynamics, 0x144, std::uint32_t(axes.brake_q16));
                        driver_state.drivers[actor]->synchronize_source_physical_controls(std::int8_t(raw.dynamics[0x330]), source_i32(raw.dynamics, 0x304), &axes);
                    }
                }
                driver_checkpoint = control_checkpoint = true;
            } else if (kind == 18 && camera_controls) {
                const auto physical_id = read_u32(journal), cycle = read_u32(journal), rear = read_u32(journal);
                if (!current_scene || ai_calls != 1 || physical_calls || physical_id != id || cycle > 1 || rear > 1 || ++camera_calls != 1) {
                    throw std::runtime_error("External camera keys require one mapped input transaction before physical integration");
                }
                scene_frames->camera_controls({cycle != 0, rear != 0});
            } else if (kind == 2) {
                const auto actor = read_u32(journal), wheel = read_u32(journal), incoming = read_u32(journal), enabled = read_u32(journal), human = read_u32(journal),
                           wet = read_u32(journal), showcase = read_u32(journal);
                auto view = integer();
                if (scene_owned) {
                    if (view || (camera_controls && camera_calls != 1)) {
                        throw std::runtime_error("Observed selected camera cannot enter material control transactions");
                    }
                    view = scene_frames->selected_view();
                }
                if (actor >= 16) {
                    throw std::runtime_error("Combined material actor exceeds its source allocation");
                }
                (void)audio::advance_source_material(skids[actor].state, materials[actor], wheel, incoming, enabled != 0, human != 0, wet != 0, camera, view, showcase != 0);
            } else if (kind == 3) {
                if (++tire_calls != 1) {
                    throw std::runtime_error("Combined original update must have one tire dispatcher");
                }
                const auto enabled = read_u32(journal);
                read(journal, raw);
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    skids[actor].human = raw[actor] != 0xff;
                    skids[actor].events = wheels[actor].queue.events;
                    skids[actor].strengths = wheels[actor].queue.strengths;
                    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                        dispatch_queue_out << id << ',' << actor << ',' << wheel << ',' << wheels[actor].queue.events[wheel] << ',' << wheels[actor].queue.strengths[wheel] << ','
                                           << wheels[actor].state.current[wheel] << ',' << wheels[actor].state.previous[wheel] << '\n';
                    }
                }
                read(journal, raw);
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    skids[actor].identity = raw[actor];
                }
                for (auto& actor : skids) {
                    read(journal, actor.wheel_kinds);
                }
                for (const auto& call : audio::dispatch_source_skids(skids, enabled != 0, random)) {
                    const auto& decision = call.decision;
                    skid_out << id << ',' << call.actor << ',' << call.wheel << ',' << call.kind << ',' << decision.sample << ',' << unsigned(decision.action) << ','
                             << decision.restart << ',' << decision.fade << ',' << call.rng << ',' << unsigned(call.state.mask) << ',' << call.state.reset << '\n';
                }
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    skid_state_out << id << ',' << actor << ',' << unsigned(skids[actor].state.mask) << ',' << skids[actor].state.reset << ',' << random.state() << '\n';
                }
            } else if (kind == 4) {
                if (scene_owned) {
                    //4871b2 selects models before420420 invokes446b50.
                    //Earlier frame callbacks still consumed the old scene.
                    flush_scene();
                }
                if (scene_lifecycle) {
                    scene_test::write_lifecycle_wheels(scene_output, id, wheel_callback, *scene_frames);
                }
                audio::SourceWheelQueueContext context;
                context.view = read_u32(journal);
                context.local_players = read_u32(journal);
                context.disabled = read_u32(journal) != 0;
                if (scene_owned && (context.view != 0 || context.local_players != 1)) {
                    throw UnsupportedSourceState("Generated scene callbacks require local view slot0 and one player");
                }
                if (retained_lamps_mode && context.disabled && !(warm_restart && race_timing->state == 13 && ai_calls == 0)) {
                    throw UnsupportedSourceState("Retained launch/lamp replay excludes pause/menu bypass contexts");
                }
                if (read_u32(journal) != 16) {
                    throw std::runtime_error("Combined wheel callback actor count is invalid");
                }
                read(journal, raw);
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    wheels[actor].human = raw[actor] != 0xff;
                }
                read(journal, raw);
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    if (scene_owned && raw[actor]) {
                        throw std::runtime_error("Observed full-body effect masks cannot enter generated wheel callbacks");
                    }
                    wheels[actor].visibility = scene_owned ? scene_frames->car_masks()[actor].full : raw[actor];
                }
                read(journal, raw);
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    wheels[actor].identity = raw[actor];
                }
                for (auto& actor : wheels) {
                    read(journal, actor.input.airborne);
                }
                for (auto& actor : wheels) {
                    for (auto& value : actor.input.longitudinal_slip) {
                        value = integer();
                    }
                }
                for (auto& actor : wheels) {
                    for (auto& value : actor.input.lateral_slip) {
                        value = integer();
                    }
                }
                if (options.physical_feedback) {
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                            auto& input = wheels[actor].input;
                            if (input.airborne[wheel] || input.longitudinal_slip[wheel] || input.lateral_slip[wheel]) {
                                throw std::runtime_error("Callback cannot import body/wheel feedback");
                            }
                            const auto& snapshot = driver_state.actors[actor];
                            input.airborne[wheel] = snapshot.body[0x517 + wheel];
                            input.longitudinal_slip[wheel] = source_i32(snapshot.dynamics, 0x1e4 + wheel * 4);
                            input.lateral_slip[wheel] = source_i32(snapshot.dynamics, 0x1f4 + wheel * 4);
                        }
                    }
                }
                if (retained_lamps_mode) {
                    std::array<std::int32_t, 16> published, reverse;
                    if (publication_mode) {
                        for (std::size_t actor = 0; actor < 16; ++actor) {
                            published[actor] = source_i32(driver_state.actors[actor].body, 0x4e8);
                        }
                    } else {
                        for (auto& value : published) {
                            value = integer();
                        }
                    }
                    for (std::size_t actor = 0; actor < reverse.size(); ++actor) {
                        reverse[actor] = integer();
                        if (options.drivetrain_feedback) {
                            if (reverse[actor] != 0) {
                                throw std::runtime_error("Callback cannot import reverse-pedal feedback");
                            }
                            reverse[actor] = source_i32(driver_state.actors[actor].dynamics, 0x304);
                        }
                    }
                    //446b50's first actor loop precedes all visibility,
                    //render-bypass and wheel-producer admission decisions.
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        auto& retained = driver_state.actors[actor];
                        const auto human = source_i32(retained.gameplay, 8);
                        const auto before = source_u32(retained.body, 0x508);
                        const auto after = source_frame_brake_lights(before, human, published[actor], reverse[actor], context.view);
                        source_store(retained.body, 0x508, after);
                        human_lamps << id << ',' << event << ',' << actor << ',' << context.view << ',' << human << ',' << human << ',' << published[actor] << ','
                                    << published[actor] << ',' << reverse[actor] << ',' << reverse[actor] << ',' << before << ',' << after << ',' << random.state() << ','
                                    << random.state() << '\n';
                    }
                }
                const auto order = audio::prepare_source_wheel_queues(wheels, context);
                for (std::size_t ordinal = 0; ordinal < order.size(); ++ordinal) {
                    const auto actor = order[ordinal];
                    order_out << id << ',' << wheel_callback << ',' << ordinal << ',' << actor << '\n';
                    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                        producer_out << id << ',' << wheel_callback << ',' << ordinal << ',' << actor << ',' << wheel << ',' << wheels[actor].queue.events[wheel] << ','
                                     << wheels[actor].queue.strengths[wheel] << ',' << wheels[actor].state.current[wheel] << ',' << wheels[actor].state.previous[wheel] << '\n';
                    }
                }
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    mute_out << id << ',' << wheel_callback << ',' << actor << ',' << wheels[actor].pending_mute << '\n';
                    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                        wheel_out << id << ',' << wheel_callback << ',' << actor << ',' << wheel << ',' << wheels[actor].queue.events[wheel] << ','
                                  << wheels[actor].queue.strengths[wheel] << ',' << wheels[actor].state.current[wheel] << ',' << wheels[actor].state.previous[wheel] << '\n';
                    }
                }
                ++wheel_callback;
            } else if (kind == 5) {
                const auto enabled = read_u32(journal), players = read_u32(journal);
                auto session = integer();
                if (local_session) {
                    if (session != 0) {
                        throw std::runtime_error("Observed motor session state cannot replace generated local lifecycle");
                    }
                    session = race_timing->state;
                }
                std::array<std::uint8_t, 4> actors;
                read(journal, actors);
                if (players < 1 || players > 4) {
                    throw std::runtime_error("Combined motor callback player count is invalid");
                }
                for (std::uint32_t player = 0; player < players; ++player) {
                    auto speed = integer();
                    const auto scale = integer();
                    auto throttle = integer();
                    if (options.pedal_feedback) {
                        if (throttle != 0 || actors[player] >= 16) {
                            throw std::runtime_error("Motor callback cannot import pedal feedback");
                        }
                        throttle = source_i32(driver_state.actors[actors[player]].dynamics, 0x11c);
                    }
                    auto gear = integer();
                    if (options.drivetrain_feedback) {
                        if (gear != 0 || actors[player] >= 16) {
                            throw std::runtime_error("Motor callback cannot import gear feedback");
                        }
                        gear = std::bit_cast<std::int8_t>(driver_state.actors[actors[player]].dynamics[0x330]);
                    }
                    if (options.rpm_feedback) {
                        if (speed != 0 || actors[player] >= 16) {
                            throw std::runtime_error("Motor callback cannot import RPM feedback");
                        }
                        speed = source_i32(driver_state.actors[actors[player]].dynamics, 0x124);
                    }
                    auto physical_speed = integer();
                    if (retained_driver_mode) {
                        if (physical_speed != 0 || actors[player] >= 16) {
                            throw std::runtime_error("Retained WHINE cannot import a later driver-owned speed cache");
                        }
                        physical_speed = source_i32(driver_state.actors[actors[player]].gameplay, 0xcc);
                    }
                    audio::SourceEngineCall call;
                    call.values = audio::source_engine_caller(speed, scale, throttle, gear, physical_speed, session);
                    call.mix = integer();
                    call.playing = read_u32(journal) != 0;
                    call.enabled = enabled != 0;
                    call.local_players = players;
                    const auto pair = std::bit_cast<std::int8_t>(actors[0]) >> 1;
                    call.bonus = pair == 8 || pair == 9;
                    const auto stage = [&](std::uint32_t value) {
                        const auto& v = call.values;
                        whine_values << id << ',' << motor_callback << ',' << player << ',' << value << ',' << v.rpm << ',' << v.speed << ',' << v.gear << ',' << v.throttle << ','
                                     << v.flag0 << ',' << v.flag1 << '\n';
                        whine_state << id << ',' << motor_callback << ',' << player << ',' << value << ',';
                        engine_columns(whine_state, true);
                    };
                    stage(0);
                    if (audio::prepare_source_engine_update(engine, call)) {
                        stage(1);
                        const auto decision = audio::advance_source_whine(engine, call, curve, random);
                        stage(2);
                        if (!call.bonus) {
                            whine_offsets << id << ',' << motor_callback << ',' << player << ',' << decision.offset << '\n';
                        }
                        std::uint32_t ordinal = 0;
                        if (decision.rate_written) {
                            whine_commands << id << ',' << motor_callback << ',' << player << ',' << ordinal++ << ",0," << decision.rate << '\n';
                        }
                        if (decision.volume_written) {
                            whine_commands << id << ',' << motor_callback << ',' << player << ',' << ordinal << ",1," << decision.volume << '\n';
                        }
                    }
                    stage(3);
                }
                ++motor_callback;
                if (local_session && race_timing->state != 10) {
                    race_timing->displays.players[0].mode =
                        std::min(std::int32_t(std::bit_cast<std::int16_t>(source_u16(driver_state.actors[local].gameplay, 0xa8))), std::int32_t(race_timing->lap_limit));
                }
            } else if (kind == 6) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    (void)audio::settle_source_skid_mute(wheels[actor].pending_mute, skids[actor].state);
                }
            } else if ((kind == 8 || kind == 9) && contact_profile) {
                const auto source_kind = read_u32(journal);
                const auto strength = integer();
                const auto local_or_handle = read_u32(journal), enabled = read_u32(journal), stereo = read_u32(journal), listener_count = read_u32(journal);
                if (listener_count < 1 || listener_count > 4) {
                    throw std::runtime_error("Combined contact event requires one to four raw listeners");
                }
                const auto pool_or_playing = read_u32(journal);
                const auto selected_playing = kind == 8 ? read_u32(journal) : 0;
                const auto postplay_success = kind == 8 ? read_u32(journal) : 0;
                audio::SpatialVector position;
                for (auto& coordinate : position) {
                    coordinate = integer();
                }
                std::vector<audio::SourceContactListener> listeners;
                listeners.reserve(listener_count);
                for (std::uint32_t listener = 0; listener < listener_count; ++listener) {
                    const auto view = integer();
                    const auto showcase = read_u32(journal);
                    const auto heading = std::bit_cast<std::int16_t>(std::uint16_t(read_u32(journal)));
                    std::array<std::uint32_t, 3> raw_position;
                    for (auto& coordinate : raw_position) {
                        coordinate = read_u32(journal);
                    }
                    listeners.push_back(audio::source_contact_listener(raw_position, camera_scale, heading, stereo != 0, view, showcase != 0));
                }
                std::uint32_t ordinal = 0;
                const auto command = [&](std::uint32_t command_kind, std::int32_t sample, std::uint32_t loops, std::int32_t volume, std::uint32_t pan, std::int32_t target,
                                         std::int32_t value = 0) {
                    contact_commands << id << ',' << event << ',' << ordinal++ << ',' << command_kind << ',' << sample << ',' << loops << ',' << volume << ",0," << pan << ','
                                     << target << ',' << value << '\n';
                };
                if (kind == 8) {
                    if (pool_or_playing > 0xfff || selected_playing > 1 || postplay_success > 1) {
                        throw std::runtime_error("Combined impact contains an invalid observed external voice gate");
                    }
                    const auto decision =
                        audio::source_impact_event({position, source_kind, strength, local_or_handle != 0, enabled != 0}, listeners, camera, contact_gains, &random);
                    std::int32_t action = 0;
                    if (source_kind == 25 && decision.threshold) {
                        command(0, decision.sample, 1, decision.volume, 64, -1);
                        action = 1;
                    } else if (decision.spatial && audio::source_contact_gain(camera, contact_gains, local_or_handle != 0) != 0) {
                        std::int32_t target = 0;
                        while (target < 12 && (pool_or_playing & (1u << target)) != 0) {
                            ++target;
                        }
                        if (target < 12 && !selected_playing) {
                            command(0, decision.sample, 1, decision.volume, decision.pan, target);
                            action = 1;
                            if (source_kind == 26 && postplay_success) {
                                command(4, -1, 0, 0, 0, target, 0x9c4);
                            }
                        }
                    }
                    const auto queried = enabled != 0 && decision.threshold && source_kind != 25;
                    const auto accepted = source_kind == 25 ? decision.threshold : decision.spatial;
                    contact_events << id << ',' << event << ",0," << source_kind << ',' << queried << ',' << accepted << ',' << decision.drew << ',' << decision.sample << ','
                                   << decision.spatial_volume << ',' << decision.volume << ',' << decision.pan << ',' << decision.angle << ',' << random.state() << ','
                                   << camera.primary << ',' << camera.secondary << ',' << action << '\n';
                } else {
                    if (pool_or_playing > 1) {
                        throw std::runtime_error("Combined scrape contains an invalid observed voice gate");
                    }
                    const auto handle = std::bit_cast<std::int32_t>(local_or_handle);
                    const auto decision =
                        audio::source_scrape_event({position, handle, strength, source_kind, enabled != 0, pool_or_playing != 0}, listeners, camera, contact_gains, random);
                    if (decision.action == audio::SourceScrapeAction::play) {
                        command(0, decision.sample, 0, decision.volume, decision.pan, handle);
                    } else if (decision.action == audio::SourceScrapeAction::stop) {
                        command(1, -1, 0, 0, 0, handle);
                    } else if (decision.action == audio::SourceScrapeAction::update) {
                        command(2, -1, 0, 0, 0, handle, std::int32_t(decision.pan));
                        command(3, -1, 0, 0, 0, handle, decision.volume);
                    }
                    contact_events << id << ',' << event << ",1," << source_kind << ',' << (enabled != 0 && handle != -1) << ',' << decision.spatial << ',' << decision.drew << ','
                                   << decision.sample << ',' << decision.spatial_volume << ',' << decision.volume << ',' << decision.pan << ',' << decision.angle << ','
                                   << random.state() << ',' << camera.primary << ',' << camera.secondary << ',' << unsigned(decision.action) << '\n';
                }
            } else if ((kind == 10 || kind == 11) && publication_mode) {
                const auto call = read_u32(journal);
                const auto checkpoint = [&](std::uint32_t stage) {
                    for (std::size_t reverse_actor = 16; reverse_actor != 0; --reverse_actor) {
                        const auto actor = reverse_actor - 1;
                        const auto& raw = driver_state.actors[actor];
                        publications << id << ',' << event << ',' << call << ',' << stage << ',' << actor << ',' << source_i32(raw.dynamics, 0x144) << ','
                                     << source_i32(raw.body, 0x4e8) << ',' << random.state() << '\n';
                    }
                };
                if (kind == 10) {
                    if (active_publication) {
                        throw std::runtime_error("Nested publication transaction");
                    }
                    active_publication = call;
                    checkpoint(0);
                    std::array<std::int32_t, 16> raw, published;
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        raw[actor] = source_i32(driver_state.actors[actor].dynamics, 0x144);
                        published[actor] = source_i32(driver_state.actors[actor].body, 0x4e8);
                    }
                    publish_source_frame_brake_pedals(raw, published);
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        source_store(driver_state.actors[actor].body, 0x4e8, std::uint32_t(published[actor]));
                    }
                    checkpoint(1);
                } else {
                    if (active_publication != call) {
                        throw std::runtime_error("Unmatched publication return");
                    }
                    checkpoint(2);
                    active_publication.reset();
                    if (scene_lifecycle && scene_physical_count == 0 && race_timing->state == 13) {
                        //44a370 still runs44a090/45ed30 when the overlay skips
                        //421aa0; its4608b0 return precedes PVS/model selection.
                        scene_frames->publish_retained();
                    }
                }
            } else if (kind == 12 && local_session) {
                if (clock) {
                    clock->observe_operation(id, 0, 0);
                }
                race_timing->primary(read_u32(journal));
                if (scene_lifecycle) {
                    scene_frames->set_lifecycle({std::uint32_t(race_timing->state), std::bit_cast<std::int32_t>(race_timing->timers.read(0))});
                }
            } else if (kind == 13 && warm_restart) {
                if (read_u32(journal) != 1 || race_timing->state != 13 || race_timing->restart_requested || race_timing->warm_pending
                    || std::bit_cast<std::int32_t>(race_timing->timers.read(0)) <= 0x280000) {
                    throw UnsupportedSourceState("Warm RACE AGAIN requires one actual YES after the automatic result timeout");
                }
                race_timing->restart_requested = true;
            } else if (kind == 14 && warm_restart) {
                const auto creation_clock = read_u32(journal);
                if (!race_timing->restart_requested || race_timing->warm_pending || race_timing->state != 13 || ai_calls != 0) {
                    throw UnsupportedSourceState("449f50 restart must follow its actual menu YES before the next update");
                }
                if (scene_lifecycle) {
                    scene_frames->warm_reset();
                }
                race_timing->timers.create(creation_clock);
                race_timing->start_phase = 0; //41ec50.
                race_timing->release_flags.fill(1);
                race_timing->release_marker = 0;
                race_timing->flags = (race_timing->flags & 0xff000000u) | 0x80u; //440240's WORD volume and dcdb2 stores.
                race_timing->state = 9;
                race_timing->stage = 2;
                set_pending_session(race_timing->finish, 9);
                race_timing->warm_pending = true;
                release = RaceReleaseCounter{};
            } else if (kind == 15 && warm_restart) {
                const auto outer_return = read_u32(journal), initializer_return = read_u32(journal);
                if (outer_return != 0x4487d2 || initializer_return != 0x4479e5 || !race_timing->warm_pending || race_timing->state != 9 || !warm_context || ai_calls != 0) {
                    throw UnsupportedSourceState("Warm grid requires exactly the generated state9 entry's447980/460170 caller");
                }
                if (clock) {
                    clock->observe_operation(id, 2);
                }
                warm_context->actors = driver_state.actors;
                if (options.physical_feedback) {
                    physical_fleet->publish_warm_context(warm_context->actors);
                }
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    warm_context->points[actor] = std::bit_cast<std::int16_t>(source_u16(race_timing->configuration_tail, 2 + actor * 2));
                    warm_context->actors[actor].limiter_active = limiter_active[actor];
                    warm_context->actors[actor].limiter_load_q16 = limiter_load[actor];
                    warm_context->actors[actor].launch_tire_adjustment_q16 = actor == local ? retained_launch : 0;
                }
                const auto generated = initialize_warm_grid(assets, *warm_context, random);
                warm_context->data = generated.data;
                warm_context->points = generated.points;
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    driver_state.actors[actor] = generated.actors[actor].snapshot;
                    source_store(race_timing->configuration_tail, 2 + actor * 2, std::uint16_t(generated.points[actor]), 2);
                    const auto& row = generated.data.drivers[actor].raw;
                    driver_state.drivers[actor].emplace(initial.route, row, generated.data.distributions, generated.data.weather_speed_penalty_q16, generated.data.tire_offset_q16,
                        random, false, AiRouteEncoding::source_initialized);
                    driver_state.drivers[actor]->initialize_race_grid();
                    wheels[actor].state.current.fill(0); //44c3f0 clears current and previous producer gates.
                    wheels[actor].state.previous.fill(0);
                }
                for (std::size_t field = 0; field < generated.status.size(); ++field) {
                    source_store(driver_state.status, field * 4, generated.status[field]);
                }
                source_store(driver_state.braking, 0x100, 0); //401370's shared braking notice, not its inherited segment caches.
                driver_state.reset_controls = true;
                record.selection_context.full_budget = generated.full_body_budget;
                if (options.physical_feedback) {
                    physical_fleet->warm_grid(generated);
                    physical_fleet->copy_pair_history(record.pair_history);
                }
                if (scene_lifecycle) {
                    std::array<scene::BodyPose, 16> grid_bodies;
                    for (std::size_t actor = 0; actor < 16; ++actor) {
                        grid_bodies[actor] = generated.actors[actor].body;
                    }
                    const auto rank = generated.grid.rank(local);
                    const auto placement = scene::race_grid_start(scene_grid, rank);
                    scene_frames->warm_grid(grid_bodies, placement.heading, clock->divisor());
                }
                race_timing->timers.clock = record.timing_clock; //The current wrapper's sole external clock input.
                race_timing->timers.reset(3); //447980, before460170.
                race_timing->timers.reset(2); //44ffe1 ->421290.
                race_timing->release_flags[0] = race_timing->release_flags[2] = 0;
                race_timing->finish.count &= 0xffffff00u;
                race_timing->finish.local_count &= 0xffffff00u;
                race_timing->finish.local_finished[0] = 0; //420180 addresses only configured players.
                race_timing->finish.stopped = 0;
                race_timing->running = 0;
                source_store(race_timing->split_enable, 0, 1); //41a820.
                auto& display = race_timing->displays.players[0];
                display.active = display.mode = display.ticks = 0;
                display.leader = -1;
                display.components.fill(0);
                constexpr std::array<char, 7> blank{'-', '-', '-', '-', '-', '-', '\0'};
                for (auto& player : race_timing->displays.players) {
                    std::copy(blank.begin(), blank.end(), player.leader_text.begin()); //41a5d0's four strings, trailing bytes retain.
                }
                race_timing->warm_pending = false;
                race_timing->restart_requested = false;
                race_timing->warm_grid_first = true;
                ++race_timing->warm_generations;
            } else if (kind == 16 && version >= 8) {
                const auto counter = read_u32(journal);
                clock->observe_operation(id, 1, counter);
                if (scene_owned) {
                    flush_scene();
                    const auto used = scene_frames->step();
                    if (scene_lifecycle && scene_physical_count == 0) {
                        scene_frames->sample_idle(counter);
                    } else {
                        scene_frames->finish(counter);
                    }
                    scene_physical_count = 0;
                    if (!scene_lifecycle) {
                        scene_test::write_frame(scene_output, scene_frame_index, *scene_frames, used, scene_frames->step(), camera_controls);
                    }
                    ++scene_frame_index;
                }
            } else if (kind == 7 && retained_driver_mode) {
                auto release_clock = read_u32(journal);
                if (warm_restart || (clock && local_session)) {
                    install_wrapper_clock();
                    if (release_clock != 0 || !race_timing->release_due || ++release_callbacks != 1 || ai_calls != 0) {
                        throw std::runtime_error("Warm release callback must match the generated countdown before PRE");
                    }
                    release_clock = record.timing_clock;
                }
                if (clock && !local_session) {
                    if (release_clock != 0 || !clock->release_due() || ++release_callbacks != 1 || ai_calls != 0) {
                        throw std::runtime_error("Release callback must match the generated countdown before the AI pass");
                    }
                    release_clock = clock->caller_clock();
                }
                if (retained_lamps_mode) {
                    auto rpm = integer();
                    const auto maximum = integer();
                    if (options.rpm_feedback) {
                        if (rpm != 0) {
                            throw std::runtime_error("Launch callback cannot import RPM feedback");
                        }
                        rpm = source_i32(driver_state.actors[local].dynamics, 0x124);
                    }
                    //The ordinary grid wrapper returns from421f40/448130
                    //directly into421aa0. Retained RPM is available at this
                    //actual callback; maximum20 remains observed context.
                    retained_launch = source_launch_adjustment(rpm, maximum);
                    if (options.physical_feedback) {
                        physical_fleet->local_boundary({source_i32(driver_state.actors[local].dynamics, 0x148) != 0, true, false, false});
                    }
                }
                source_store(driver_state.actors[local].gameplay, 0xc8, release_clock); //448130→421a80.
                source_store(driver_state.status, 8, 1); //422095 before the ordinary countdown's448130 call.
                driver_checkpoint = true;
            } else if (kind != 7) {
                throw UnsupportedSourceState("Unmodeled combined source event; no observed draw counts or seeds can replace its consumer");
            }
            if (options.dispatch_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    dispatch_output << id << ',' << event << ',' << actor << ',' << source_u32(driver_state.actors[actor].body, 0x4f0) << '\n';
                }
            }
            if (options.physical_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    physical_feedback_output << id << ',' << event << ',' << actor;
                    physical_feedback_columns(physical_feedback_output, driver_state.actors[actor], false);
                    physical_feedback_output << '\n';
                }
            }
            if (options.drivetrain_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    const auto& raw = driver_state.actors[actor].dynamics;
                    feedback << id << ',' << event << ',' << actor << ',' << unsigned(raw[0x330]) << ',' << unsigned(raw[0x334]) << ',' << source_u32(raw, 0x30c) << ','
                             << source_i32(raw, 0x304) << '\n';
                }
            }
            if (options.rpm_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    const auto& raw = driver_state.actors[actor].dynamics;
                    rpm_feedback_output << id << ',' << event << ',' << actor << ',' << source_i32(raw, 0x124) << ',' << source_i32(raw, 0x128) << '\n';
                }
            }
            if (options.control_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    const auto& raw = driver_state.actors[actor].dynamics;
                    control_feedback_output << id << ',' << event << ',' << actor << ',' << source_i32(raw, 0x44) << ',' << source_i32(raw, 0x150) << ','
                                            << std::bit_cast<std::int16_t>(source_u16(raw, 0x274)) << '\n';
                }
            }
            if (options.pedal_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    pedal_feedback_output << id << ',' << event << ',' << actor;
                    for (const auto offset : {0x14, 0x38, 0x120, 0x134, 0x11c, 0x130}) {
                        pedal_feedback_output << ',' << source_i32(driver_state.actors[actor].dynamics, offset);
                    }
                    pedal_feedback_output << '\n';
                }
            }
            if (options.limiter_feedback && (kind == 0 || kind == 1)) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    limiter_feedback_output << id << ',' << event << ',' << actor << ',' << limiter_active[actor] << ',' << limiter_load[actor] << '\n';
                }
            }
            if (retained_driver_mode && driver_checkpoint) {
                for (std::size_t actor = 0; actor < 16; ++actor) {
                    const auto& raw = driver_state.actors[actor];
                    for (std::size_t word = 0; word < raw.gameplay.size() / 4; ++word) {
                        driver_game << id << ',' << event << ',' << actor << ',' << word << ',' << source_u32(raw.gameplay, word * 4) << '\n';
                    }
                    if (control_checkpoint) {
                        driver_controls << id << ',' << event << ',' << actor;
                        for (const auto offset : {0x138, 0x13c, 0x140, 0x144}) {
                            driver_controls << ',' << source_i32(raw.dynamics, offset);
                        }
                        driver_controls << ',' << unsigned(raw.dynamics[0x330]) << ',' << source_i32(raw.dynamics, 0x304) << ',' << source_u32(raw.body, 0x500) << ','
                                        << source_u32(raw.body, 0x508);
                        for (std::size_t offset = 0xdc; offset < 0xfc; offset += 4) {
                            driver_controls << ',' << source_i32(raw.dynamics, offset);
                        }
                        driver_controls << '\n';
                    }
                }
                for (std::size_t word = 0; word < driver_state.status.size() / 4; ++word) {
                    driver_world << id << ',' << event << ",0," << word << ',' << source_u32(driver_state.status, word * 4) << '\n';
                }
                for (std::size_t word = 0; word < driver_state.braking.size() / 4; ++word) {
                    driver_world << id << ',' << event << ",1," << word << ',' << source_u32(driver_state.braking, word * 4) << '\n';
                }
                if (retained_lamps_mode && kind != 7) {
                    driver_world << id << ',' << event << ",2,0," << std::uint32_t(retained_launch) << '\n';
                }
                if (local_session && kind == 1) {
                    std::array<std::uint32_t, 9> values;
                    std::copy(race_timing->timers.origins.begin(), race_timing->timers.origins.end(), values.begin());
                    values[5] = race_timing->timers.clock;
                    values[6] = race_timing->release_flags[1];
                    values[7] = std::uint32_t(race_timing->state);
                    values[8] = race_timing->finish.pending_state;
                    for (std::size_t word = 0; word < values.size(); ++word) {
                        driver_world << id << ',' << event << ",3," << word << ',' << values[word] << '\n';
                    }
                }
            }
            rng_out << id << ',' << event << ',' << kind << ',' << random.state() << '\n';
            engine_out << id << ',' << event << ',';
            engine_columns(engine_out, false);
            for (std::size_t actor = 0; actor < 16; ++actor) {
                const auto& wheel = wheels[actor];
                const auto& skid = skids[actor].state;
                audio_out << id << ',' << event << ',' << actor << ',' << unsigned(skid.surface) << ',' << skid.reset << ',' << unsigned(skid.mask) << ',' << wheel.pending_mute;
                for (auto value : materials[actor]) {
                    audio_out << ',' << unsigned(value);
                }
                for (auto value : wheel.state.current) {
                    audio_out << ',' << value;
                }
                for (auto value : wheel.state.previous) {
                    audio_out << ',' << value;
                }
                for (auto value : wheel.queue.events) {
                    audio_out << ',' << value;
                }
                for (auto value : wheel.queue.strengths) {
                    audio_out << ',' << value;
                }
                audio_out << ',' << camera.primary << ',' << camera.secondary << '\n';
            }
            ++event_total;
            if (completed_wrapper) {
                race_timing->after_update();
            }
        }
        if (ai_calls != 1 || tire_calls != 1 || (paths.physical && physical_calls != 1) || (options.dispatch_feedback && dispatch_calls != 1)
            || (camera_controls && camera_calls != 1)) {
            throw std::runtime_error("Combined original update omitted a required AI/tire phase");
        }
        if (clock) {
            if (release_callbacks != unsigned(clock->release_due())) {
                throw std::runtime_error("Source journal omitted its generated countdown release callback");
            }
            clock->finish_update();
        }
        if (warm_restart && release_callbacks != unsigned(race_timing->release_due)) {
            throw std::runtime_error("Warm countdown omitted or duplicated its generated release callback");
        }
    }
    if (warm_restart && (race_timing->warm_generations > 1 || race_timing->restart_requested || race_timing->warm_pending)) {
        throw UnsupportedSourceState("Warm restart interval duplicated or omitted an actual menu/grid transaction");
    }
    if (active_publication) {
        throw std::runtime_error("Missing actual publication return");
    }
    if (scene_owned && (current_scene || scene_frame_index == 0 || (!scene_lifecycle && pending_scene))) {
        throw std::runtime_error("Generated scene replay ends inside an unpublished or unsampled primary frame");
    }
    if (clock) {
        clock->finish();
    }
    if (input.peek() != std::char_traits<char>::eof() || journal.peek() != std::char_traits<char>::eof()
        || (paths.physical && physical_input.peek() != std::char_traits<char>::eof())) {
        throw std::runtime_error("Combined original streams have trailing bytes");
    }
    std::cout << "Replayed " << count << " complete combined random updates and " << event_total << " source events from one initial seed\n";
    return 0;
}

} // namespace toca::test::fleet
