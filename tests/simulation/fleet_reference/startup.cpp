#include "startup.hpp"

#include "binary_io.hpp"
#include "simulation/source_session_start.hpp"
#include "simulation/time.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

ColdGridInitInputs read_grid_configuration(std::istream& input) {
    const auto track = read_u32(input), car = read_u32(input), weather = read_u32(input), difficulty = read_u32(input), manual = read_u32(input), steering = read_u32(input),
               seed = read_u32(input);
    if (track >= 9 || car >= 8 || weather < 1 || weather > 5 || difficulty > 2 || manual > 1 || steering < 1 || steering > 3) {
        throw UnsupportedSourceState("Cold-grid input requires ordinary authored track/car/weather/difficulty/controls");
    }
    if (weather == 2 && (track == 1 || track == 4 || track == 5 || track == 7)) {
        throw UnsupportedSourceState("Cold-grid cloudy track requires an independently observed horizon");
    }
    ColdGridInitInputs request;
    request.setup = scene::resolve_setup({.track_id = track, .car_id = car, .weather_choice = weather, .detail = 3});
    request.difficulty = difficulty;
    request.manual = manual != 0;
    request.steering_profile = steering;
    request.seed = seed;
    for (auto& points : request.entry_points) {
        std::array<std::uint8_t, 2> raw;
        read(input, raw);
        points = std::bit_cast<std::int16_t>(std::uint16_t(raw[0] | std::uint16_t(raw[1]) << 8));
    }
    std::array<std::uint8_t, 10> name;
    read(input, name);
    const auto end = std::ranges::find(name, 0);
    if (end == name.begin() || end == name.end()
        || !std::ranges::all_of(end, name.end(),
            [](auto value) {
                return value == 0;
            })) {
        throw std::runtime_error("Invalid cold-grid name");
    }
    request.player_name.assign(name.begin(), end);

    return request;
}

ColdGridInitInputs read_cold_grid_inputs(const char* input_path) {
    std::ifstream input(input_path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'C', 'G', 'I', 'N'} || read_u32(input) != 1) {
        throw std::runtime_error("Unsupported cold-grid initialization input stream");
    }
    auto request = read_grid_configuration(input);
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Trailing observed-state payload in cold-grid inputs");
    }
    return request;
}

struct QualifyingStartupInputs {
    ColdGridInitInputs configuration;
    std::uint32_t creation_clock, caller_clock;
    std::int32_t divisor;
};

QualifyingStartupInputs read_qualifying_startup_inputs(const char* input_path) {
    std::ifstream input(input_path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'G', 'R', 'D', '1'} || read_u32(input) != 2 || read_u32(input) != 0x443f60) {
        throw std::runtime_error("Unsupported qualifying startup input stream");
    }
    QualifyingStartupInputs result;
    result.configuration = read_grid_configuration(input);
    result.creation_clock = read_u32(input);
    result.caller_clock = read_u32(input);
    result.divisor = std::bit_cast<std::int32_t>(read_u32(input));
    if (result.divisor <= 0 || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Invalid qualifying startup clock or trailing observed-state payload");
    }
    return result;
}

struct ActorField {
    const char* group;
    const char* name;
    unsigned block;
    std::size_t offset;
    char format;
};

constexpr std::array<ActorField, 117> fields{{{"rank_ring", "rank", 2, 0xbc, 'h'}, {"rank_ring", "grid_rank", 2, 0x8e, 'B'}, {"rank_ring", "race_rank", 2, 0x8f, 'B'},
    {"rank_ring", "spatial_rank", 2, 0x90, 'B'}, {"rank_ring", "ahead", 2, 0x96, 'B'}, {"rank_ring", "behind", 2, 0x97, 'B'}, {"identity", "active", 2, 0x4, 'I'},
    {"identity", "human", 2, 0x8, 'I'}, {"identity", "player", 2, 0x80, 'b'}, {"identity", "driver", 2, 0x8d, 'B'}, {"identity", "actor", 0, 0x515, 'B'},
    {"identity", "car", 0, 0x516, 'B'}, {"route_cache", "direction", 2, 0x10, 'I'}, {"route_cache", "passing", 2, 0x2c, 'I'}, {"route_cache", "refresh", 2, 0x3c, 'I'},
    {"route_cache", "swap_start_lines", 2, 0x50, 'I'}, {"route_cache", "traffic_mode", 2, 0x81, 'B'}, {"route_cache", "tire_cooldown", 2, 0x83, 'B'},
    {"route_cache", "cadence", 2, 0x86, 'B'}, {"route_cache", "current_gate", 2, 0x9a, 'B'}, {"route_cache", "previous_gate", 2, 0x9b, 'B'},
    {"route_cache", "marker", 2, 0x9d, 'B'}, {"route_cache", "cached_point", 2, 0x9e, 'B'}, {"route_cache", "next_point", 2, 0xa2, 'h'},
    {"route_cache", "previous_point", 2, 0xa4, 'h'}, {"route_cache", "checkpoint", 2, 0xa6, 'B'}, {"route_cache", "lap", 2, 0xa8, 'h'}, {"route_cache", "game_speed", 2, 0xcc, 'i'},
    {"route_cache", "desired_speed", 2, 0xd0, 'i'}, {"route_cache", "alternate_speed", 2, 0xd4, 'i'}, {"route_cache", "speed_variation", 2, 0xd8, 'i'},
    {"route_cache", "race_distance", 2, 0xdc, 'i'}, {"route_cache", "target_distance", 2, 0xe0, 'i'}, {"route_cache", "projected_distance", 2, 0xe4, 'i'},
    {"route_cache", "projected_alias", 2, 0xe8, 'i'}, {"route_cache", "stopped_distance", 2, 0xec, 'i'}, {"route_cache", "forward_gap", 2, 0xf0, 'i'},
    {"route_cache", "distance_delta", 2, 0xf4, 'i'}, {"route_cache", "target_x", 2, 0x108, 'i'}, {"route_cache", "target_y", 2, 0x10c, 'i'},
    {"route_cache", "target_z", 2, 0x110, 'i'}, {"route_cache", "alternate_target_x", 2, 0x114, 'i'}, {"route_cache", "alternate_target_y", 2, 0x118, 'i'},
    {"route_cache", "alternate_target_z", 2, 0x11c, 'i'}, {"pose_support", "position_x", 0, 0xbc, 'i'}, {"pose_support", "position_y", 0, 0xc0, 'i'},
    {"pose_support", "position_z", 0, 0xc4, 'i'}, {"pose_support", "basis_00", 0, 0x150, 'i'}, {"pose_support", "basis_01", 0, 0x154, 'i'},
    {"pose_support", "basis_02", 0, 0x158, 'i'}, {"pose_support", "basis_10", 0, 0x15c, 'i'}, {"pose_support", "basis_11", 0, 0x160, 'i'},
    {"pose_support", "basis_12", 0, 0x164, 'i'}, {"pose_support", "basis_20", 0, 0x168, 'i'}, {"pose_support", "basis_21", 0, 0x16c, 'i'},
    {"pose_support", "basis_22", 0, 0x170, 'i'}, {"pose_support", "ground_dot_0", 0, 0xb0, 'i'}, {"pose_support", "ground_dot_1", 0, 0xb4, 'i'},
    {"pose_support", "ground_dot_2", 0, 0xb8, 'i'}, {"pose_support", "support_count", 0, 0x51f, 'B'}, {"pose_support", "top_support_flag_0", 0, 0x517, 'B'},
    {"pose_support", "top_support_flag_1", 0, 0x518, 'B'}, {"pose_support", "top_support_flag_2", 0, 0x519, 'B'}, {"pose_support", "top_support_flag_3", 0, 0x51a, 'B'},
    {"pose_support", "top_support_index_0", 0, 0x521, 'B'}, {"pose_support", "top_support_index_1", 0, 0x522, 'B'}, {"pose_support", "top_support_index_2", 0, 0x523, 'B'},
    {"pose_support", "top_support_index_3", 0, 0x524, 'B'}, {"pose_support", "contact_0_x", 0, 0xf0, 'i'}, {"pose_support", "contact_0_y", 0, 0xf4, 'i'},
    {"pose_support", "contact_0_z", 0, 0xf8, 'i'}, {"pose_support", "contact_1_x", 0, 0xfc, 'i'}, {"pose_support", "contact_1_y", 0, 0x100, 'i'},
    {"pose_support", "contact_1_z", 0, 0x104, 'i'}, {"pose_support", "contact_2_x", 0, 0x108, 'i'}, {"pose_support", "contact_2_y", 0, 0x10c, 'i'},
    {"pose_support", "contact_2_z", 0, 0x110, 'i'}, {"pose_support", "contact_3_x", 0, 0x114, 'i'}, {"pose_support", "contact_3_y", 0, 0x118, 'i'},
    {"pose_support", "contact_3_z", 0, 0x11c, 'i'}, {"pose_support", "contact_4_x", 0, 0x120, 'i'}, {"pose_support", "contact_4_y", 0, 0x124, 'i'},
    {"pose_support", "contact_4_z", 0, 0x128, 'i'}, {"pose_support", "contact_5_x", 0, 0x12c, 'i'}, {"pose_support", "contact_5_y", 0, 0x130, 'i'},
    {"pose_support", "contact_5_z", 0, 0x134, 'i'}, {"pose_support", "contact_6_x", 0, 0x138, 'i'}, {"pose_support", "contact_6_y", 0, 0x13c, 'i'},
    {"pose_support", "contact_6_z", 0, 0x140, 'i'}, {"pose_support", "contact_7_x", 0, 0x144, 'i'}, {"pose_support", "contact_7_y", 0, 0x148, 'i'},
    {"pose_support", "contact_7_z", 0, 0x14c, 'i'}, {"modes_controls", "body_mode", 0, 0x4f0, 'I'}, {"modes_controls", "published_brake", 0, 0x4e8, 'i'},
    {"modes_controls", "clutch", 1, 0x148, 'I'}, {"modes_controls", "queued_shift", 1, 0x14c, 'b'}, {"modes_controls", "engine_speed", 1, 0x124, 'i'},
    {"modes_controls", "throttle_step", 1, 0x14, 'i'}, {"modes_controls", "brake_step", 1, 0x38, 'i'}, {"modes_controls", "throttle_ramp", 1, 0x120, 'i'},
    {"modes_controls", "brake", 1, 0x130, 'i'}, {"modes_controls", "brake_ramp", 1, 0x134, 'i'}, {"modes_controls", "steering_return", 1, 0x44, 'i'},
    {"modes_controls", "steering_accumulator", 1, 0x150, 'i'}, {"modes_controls", "steering_angle", 1, 0x274, 'h'}, {"modes_controls", "gear", 1, 0x330, 'B'},
    {"modes_controls", "pending_flag", 1, 0x30c, 'I'}, {"modes_controls", "reverse_pedal", 1, 0x304, 'i'}, {"modes_controls", "transmission_mode", 1, 0x27c, 'I'},
    {"modes_controls", "tire_0", 1, 0xdc, 'i'}, {"modes_controls", "tire_1", 1, 0xe0, 'i'}, {"modes_controls", "tire_2", 1, 0xe4, 'i'}, {"modes_controls", "tire_3", 1, 0xe8, 'i'},
    {"modes_controls", "tire_4", 1, 0xec, 'i'}, {"modes_controls", "tire_5", 1, 0xf0, 'i'}, {"modes_controls", "tire_6", 1, 0xf4, 'i'},
    {"modes_controls", "tire_7", 1, 0xf8, 'i'}}};

void emit_cold_grid(std::ostream& output, const ColdGridInitInputs& request, const ColdGridInitResult& result, bool sequence, std::string_view phase,
    bool qualifying) {
    const auto row = [&](const char* group, int actor, const std::string& field, std::int64_t value) {
        if (sequence) {
            output << phase << ',';
        }
        output << group << ',' << actor << ',' << field << ',' << value << '\n';
    };
    for (std::size_t actor = 0; actor < result.actors.size(); ++actor) {
        const auto& snapshot = result.actors[actor].snapshot;
        for (const auto& field : fields) {
            if (std::string_view(field.name) == "queued_shift" && (qualifying ? actor != result.data.local_actor : !result.actors[actor].human)) {
                continue; //402c80 owns this byte only for the local player.
            }
            const std::span<const std::uint8_t> raw = field.block == 0   ? std::span<const std::uint8_t>(snapshot.body)
                                                      : field.block == 1 ? std::span<const std::uint8_t>(snapshot.dynamics)
                                                                         : std::span<const std::uint8_t>(snapshot.gameplay);
            std::int64_t value = 0;
            switch (field.format) {
                case 'i':
                    value = source_i32(raw, field.offset);
                    break;
                case 'I':
                    value = source_u32(raw, field.offset);
                    break;
                case 'h':
                    value = std::bit_cast<std::int16_t>(source_u16(raw, field.offset));
                    break;
                case 'B':
                    value = raw[field.offset];
                    break;
                case 'b':
                    value = std::bit_cast<std::int8_t>(raw[field.offset]);
                    break;
                default:
                    throw std::logic_error("Unknown cold-grid output field width");
            }
            row(field.group, int(actor), field.name, value);
        }
        row("identity", int(actor), "ownership", result.ownership[actor]);
        if (qualifying) {
            row("qualifying_motion", int(actor), "speed", source_i32(snapshot.body, 0xe0));
            constexpr std::array axes{"x", "y", "z"};
            for (std::size_t axis = 0; axis < axes.size(); ++axis) {
                row("qualifying_motion", int(actor), std::string("velocity_") + axes[axis], source_i32(snapshot.body, 0xc8 + axis * 4));
            }
        }
    }
    const auto local = result.data.local_actor;
    if (qualifying) {
        for (std::size_t part = 0; part < 4; ++part) {
            row("local_controller", int(local), "saved_mode_" + std::to_string(part), source_i32(result.actors[local].snapshot.gameplay, 0x60 + part * 4));
        }
    } else {
        for (std::size_t actor = 0; actor < result.points.size(); ++actor) {
            row("configuration_points", int(actor), "points", result.points[actor]);
        }
        row("configuration_points", -1, "pole_actor", result.pole_actor);
        for (std::size_t byte = 0; byte <= request.player_name.size(); ++byte) {
            row("local_name", int(local), "name_" + std::to_string(byte), result.data.drivers[local].raw[byte]);
        }
    }
    row("reset", -1, "full_body_budget", result.full_body_budget);
    row("reset", -1, "dispatch_reset", result.dispatch_reset);
    row("reset", -1, "release_counter", result.release_counter);
    for (std::size_t word = 0; word < result.status.size(); ++word) {
        row("reset", -1, "status_" + std::to_string(word), result.status[word]);
    }
    for (std::size_t word = 0; word < result.ai_setup_resets.size(); ++word) {
        row("reset", -1, "ai_setup_reset_" + std::to_string(word), result.ai_setup_resets[word]);
    }
    for (std::size_t pair = 0; pair < result.pair_handles.size(); ++pair) {
        row("pair_reset", int(pair), "handle", result.pair_handles[pair]);
        row("pair_reset", int(pair), "touching", result.pair_touching[pair]);
    }
    row("clock_contract", -1, "first_sample", result.clock_first_sample);
    row("rng", -1, "final_seed", result.rng_final);
}

//The startup consumer boundary is403dd0 PRE, after414e20 selection. Its
//body4f0 mode is observed; all other named initializer writers survive there.
void restore_startup_fields(SourceSnapshot& target, const ColdGridActor& generated) {
    for (const auto& field : fields) {
        if (std::string_view(field.name) == "body_mode" || (std::string_view(field.name) == "queued_shift" && !generated.human)) {
            continue;
        }
        const auto width = field.format == 'i' || field.format == 'I' ? 4u : field.format == 'h' ? 2u : 1u;
        const std::span<std::uint8_t> raw = field.block == 0   ? std::span<std::uint8_t>(target.body)
                                            : field.block == 1 ? std::span<std::uint8_t>(target.dynamics)
                                                               : std::span<std::uint8_t>(target.gameplay);
        const std::span<const std::uint8_t> source = field.block == 0   ? std::span<const std::uint8_t>(generated.snapshot.body)
                                                     : field.block == 1 ? std::span<const std::uint8_t>(generated.snapshot.dynamics)
                                                                        : std::span<const std::uint8_t>(generated.snapshot.gameplay);
        for (std::size_t byte = field.offset; byte < field.offset + width; ++byte) {
            if (raw[byte]) {
                throw std::runtime_error("Observed startup-owned actor field cannot enter joined replay");
            }
            raw[byte] = source[byte];
        }
    }
}

void restore_startup_record(Record& record, const ColdGridInitResult& startup, bool first_selection) {
    if (record.selection_context.full_budget) {
        throw std::runtime_error("Observed startup body budget cannot enter joined replay");
    }
    record.selection_context.full_budget = startup.full_body_budget;
    for (std::size_t byte = 0; byte < 16; ++byte) {
        if (record.traffic.status[byte]) {
            throw std::runtime_error("Observed startup status prefix cannot enter joined replay");
        }
        record.traffic.status[byte] = std::uint8_t(startup.status[byte / 4] >> (8 * (byte % 4)));
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& generated = startup.actors[actor];
        if (first_selection) {
            if (source_u32(record.traffic.actors[actor].body, 0x4f0) || source_u32(record.actors[actor].snapshot.body, 0x4f0) || record.ai_actors[actor].body_mode
                || record.selection[actor].previous != BodyMode::full || record.selection[actor].rollover.upside_down || record.selection[actor].rollover.sideways) {
                throw std::runtime_error("Observed first body-mode/rollover aliases cannot enter startup selection");
            }
            auto& selection = record.selection[actor];
            //This alias describes the mode BEFORE414e20, unlike the three
            //AI-consumer mode copies published after the selector below.
            selection.previous = BodyMode(source_u32(generated.snapshot.body, 0x4f0));
            selection.rollover.upside_down = source_i32(generated.snapshot.dynamics, 0x2a0) != 0;
            selection.rollover.sideways = source_i32(generated.snapshot.dynamics, 0x2a4) != 0;
            for (const auto offset : {0x2a0, 0x2a4}) {
                if (source_u32(record.traffic.actors[actor].dynamics, offset) || source_u32(record.actors[actor].snapshot.dynamics, offset)) {
                    throw std::runtime_error("Observed first rollover flags cannot enter startup selection");
                }
                //40a770 resets these words; Motion's generated publication
                //already carries their source-owned cold values.
                const auto value = source_u32(generated.snapshot.dynamics, offset);
                source_store(record.traffic.actors[actor].dynamics, offset, value);
                source_store(record.actors[actor].snapshot.dynamics, offset, value);
            }
        }
        //These are inherited cold prerequisites, not initializer writers.
        //In particular4e8 is published from inherited raw brake+144.
        for (const auto offset : {0x11c, 0x138, 0x13c, 0x140, 0x144}) {
            if (source_u32(record.traffic.actors[actor].dynamics, offset) || source_u32(record.actors[actor].snapshot.dynamics, offset)) {
                throw std::runtime_error("Startup join requires zero inherited cold throttle/raw axes");
            }
        }
        restore_startup_fields(record.traffic.actors[actor], generated);
        restore_startup_fields(record.actors[actor].snapshot, generated);
        auto& ai = record.ai_actors[actor];
        //Compact route inputs duplicate only gameplay and pose, not a full
        //Motion. Validate their owned ranges through the same field table.
        SourceSnapshot compact{};
        compact.gameplay = ai.gameplay;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            source_store(compact.body, 0xbc + axis * 4, std::uint32_t(ai.body.position[axis]));
            for (std::size_t row = 0; row < 3; ++row) {
                source_store(compact.body, 0x150 + (row * 3 + axis) * 4, std::uint32_t(ai.body.basis[row][axis]));
            }
        }
        restore_startup_fields(compact, generated);
        ai.gameplay = compact.gameplay;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            ai.body.position[axis] = source_i32(compact.body, 0xbc + axis * 4);
            for (std::size_t row = 0; row < 3; ++row) {
                ai.body.basis[row][axis] = source_i32(compact.body, 0x150 + (row * 3 + axis) * 4);
            }
        }
        auto& selection = record.selection[actor];
        if (selection.human_control || selection.player_identity || selection.support_count || selection.first_tire_q16 || selection.normal_up_q16 || selection.ahead
            || selection.behind || selection.forward_gap_q16) {
            throw std::runtime_error("Observed startup selector aliases cannot enter joined replay");
        }
        selection.human_control = generated.human;
        selection.player_identity = generated.player;
        selection.support_count = generated.snapshot.body[0x51f];
        selection.first_tire_q16 = source_i32(generated.snapshot.dynamics, 0xdc);
        selection.normal_up_q16 = source_i32(generated.snapshot.body, 0xb4);
        selection.ahead = generated.ahead;
        selection.behind = generated.behind;
        selection.forward_gap_q16 = source_i32(generated.snapshot.gameplay, 0xf0);
    }
}

void prepare_startup_selection_context(Record& record, const ColdGridInitResult& startup) {
    if (record.ai_tire_base || record.selection_context.tire_base_q16 || record.selection_context.alternate_direction) {
        throw std::runtime_error("Observed first selector context cannot enter startup selection");
    }
    record.ai_tire_base = record.selection_context.tire_base_q16 = startup.data.tire_offset_q16;
    record.selection_context.alternate_direction = source_i32(startup.actors[startup.data.local_actor].snapshot.gameplay, 0x10) != 0;
}

void emit_startup_selection(std::ostream& output, std::span<const BodySelectionActor, 16> inputs, const BodySelectionResult& result, std::uint32_t before, std::uint32_t after) {
    output << "group,actor,field,value\n";
    for (std::size_t actor = 0; actor < result.modes.size(); ++actor) {
        const auto& input = inputs[actor];
        output << "selection," << actor << ",previous," << std::uint32_t(input.previous) << '\n';
        output << "selection," << actor << ",upside_down," << input.rollover.upside_down << '\n';
        output << "selection," << actor << ",sideways," << input.rollover.sideways << '\n';
        output << "selection," << actor << ",mode," << std::uint32_t(result.modes[actor]) << '\n';
    }
    output << "selection,-1,full_count," << result.full_count << '\n';
    output << "rng,-1,before," << before << '\n';
    output << "rng,-1,after," << after << '\n';
}

void select_startup_bodies(Record& record, const ColdGridInitResult& startup, const SourceRandom& random, std::ostream& output) {
    prepare_startup_selection_context(record, startup);
    const auto before = random.state();
    const auto result = select_fleet_body_modes(record.selection, record.selection_context);
    emit_startup_selection(output, record.selection, result, before, random.state());
    for (std::size_t actor = 0; actor < result.modes.size(); ++actor) {
        source_store(record.traffic.actors[actor].body, 0x4f0, std::uint32_t(result.modes[actor]));
        source_store(record.actors[actor].snapshot.body, 0x4f0, std::uint32_t(result.modes[actor]));
        record.ai_actors[actor].body_mode = std::uint32_t(result.modes[actor]);
    }
}

void restore_startup_physical_body(Record& physical, const Record& driver, const ColdGridInitResult& startup) {
    if (physical.ai_tire_base || physical.selection_context.tire_base_q16 || physical.selection_context.full_budget || physical.selection_context.alternate_direction) {
        throw std::runtime_error("Observed first physical selector context cannot enter startup BODY restore");
    }
    for (std::size_t actor = 0; actor < startup.actors.size(); ++actor) {
        const auto& input = physical.selection[actor];
        if (input.previous != BodyMode::full || input.human_control || input.player_identity || input.rollover.upside_down || input.rollover.sideways || input.support_count
            || input.first_tire_q16 || input.normal_up_q16 || input.ahead || input.behind || input.forward_gap_q16) {
            throw std::runtime_error("Observed first physical selector aliases cannot enter startup BODY restore");
        }
        auto& raw = physical.actors[actor].snapshot.body;
        const auto& generated = startup.actors[actor].snapshot.body;
        for (const auto& field : fields) {
            if (field.block != 0) {
                continue;
            }
            const auto width = field.format == 'B' ? 1u : 4u;
            for (std::size_t byte = field.offset; byte < field.offset + width; ++byte) {
                if (raw[byte]) {
                    throw std::runtime_error("Observed initial BODY writer cannot enter startup physical restore");
                }
                raw[byte] = generated[byte];
            }
        }
    }
    //These are the same generated PRE inputs, not the later selected modes.
    physical.selection = driver.selection;
    physical.selection_context = driver.selection_context;
    physical.ai_tire_base = driver.ai_tire_base;
}

void emit_startup_physical_body(std::ostream& output, const std::array<SourceSnapshot, 16>& restored) {
    output << "group,actor,field,value\n";
    for (std::size_t actor = 0; actor < restored.size(); ++actor) {
        for (const auto& field : fields) {
            if (field.block != 0) {
                continue;
            }
            const auto& raw = restored[actor].body;
            const auto value = field.format == 'B'   ? std::int64_t(raw[field.offset])
                               : field.format == 'I' ? std::int64_t(source_u32(raw, field.offset))
                                                     : std::int64_t(source_i32(raw, field.offset));
            output << "body," << actor << ',' << field.name << ',' << value << '\n';
        }
    }
}

constexpr std::array<std::pair<const char*, std::size_t>, 8> calibration_fields{{{"maximum_throttle", 0xc}, {"base_throttle", 0x10}, {"throttle_base", 0x18}, {"front_drive", 0x1c},
    {"rpm_scale", 0x24}, {"maximum_brake", 0x30}, {"brake_base", 0x3c}, {"steering_return_base", 0x48}}};

void restore_startup_control_calibration(SourceSnapshot& target, const ColdGridActor& generated) {
    for (const auto& [name, offset] : calibration_fields) {
        if (source_u32(target.dynamics, offset)) {
            throw std::runtime_error(std::string("Observed initial control calibration cannot enter startup replay: ") + name);
        }
        source_store(target.dynamics, offset, source_u32(generated.snapshot.dynamics, offset));
    }
}

void emit_startup_control_calibration(std::ostream& output, std::string_view phase, const std::array<SourceSnapshot, 16>& snapshots) {
    for (std::size_t actor = 0; actor < snapshots.size(); ++actor) {
        for (const auto& [name, offset] : calibration_fields) {
            output << phase << ',' << actor << ',' << name << ',' << source_i32(snapshots[actor].dynamics, offset) << '\n';
        }
    }
}

//42e6f0 writes eight heights, triangle shorts and material bytes;40bbc0
//writes the retained normal. Adjacent support flags/index tails are separate.
constexpr std::array<ActorField, 27> contact_cache_fields{
    {{"contact_cache", "height_0", 0, 0x1c4, 'i'}, {"contact_cache", "height_1", 0, 0x1c8, 'i'}, {"contact_cache", "height_2", 0, 0x1cc, 'i'},
        {"contact_cache", "height_3", 0, 0x1d0, 'i'}, {"contact_cache", "height_4", 0, 0x1d4, 'i'}, {"contact_cache", "height_5", 0, 0x1d8, 'i'},
        {"contact_cache", "height_6", 0, 0x1dc, 'i'}, {"contact_cache", "height_7", 0, 0x1e0, 'i'}, {"contact_cache", "normal_0", 0, 0x1e4, 'i'},
        {"contact_cache", "normal_1", 0, 0x1e8, 'i'}, {"contact_cache", "normal_2", 0, 0x1ec, 'i'}, {"contact_cache", "triangle_0", 1, 0x264, 'H'},
        {"contact_cache", "triangle_1", 1, 0x266, 'H'}, {"contact_cache", "triangle_2", 1, 0x268, 'H'}, {"contact_cache", "triangle_3", 1, 0x26a, 'H'},
        {"contact_cache", "triangle_4", 1, 0x26c, 'H'}, {"contact_cache", "triangle_5", 1, 0x26e, 'H'}, {"contact_cache", "triangle_6", 1, 0x270, 'H'},
        {"contact_cache", "triangle_7", 1, 0x272, 'H'}, {"contact_cache", "surface_0", 1, 0x328, 'B'}, {"contact_cache", "surface_1", 1, 0x329, 'B'},
        {"contact_cache", "surface_2", 1, 0x32a, 'B'}, {"contact_cache", "surface_3", 1, 0x32b, 'B'}, {"contact_cache", "surface_4", 1, 0x32c, 'B'},
        {"contact_cache", "surface_5", 1, 0x32d, 'B'}, {"contact_cache", "surface_6", 1, 0x32e, 'B'}, {"contact_cache", "surface_7", 1, 0x32f, 'B'}}};

void restore_startup_contact_cache(SourceSnapshot& target, const ColdGridActor& generated) {
    for (const auto& field : contact_cache_fields) {
        const auto width = field.format == 'i' ? 4u : field.format == 'H' ? 2u : 1u;
        const auto raw = field.block == 0 ? std::span<std::uint8_t>(target.body) : std::span<std::uint8_t>(target.dynamics);
        const auto source = field.block == 0 ? std::span<const std::uint8_t>(generated.snapshot.body) : std::span<const std::uint8_t>(generated.snapshot.dynamics);
        for (std::size_t byte = field.offset; byte < field.offset + width; ++byte) {
            if (raw[byte]) {
                throw std::runtime_error(std::string("Observed initial contact cache cannot enter startup replay: ") + field.name);
            }
            raw[byte] = source[byte];
        }
    }
}

void emit_startup_contact_cache(std::ostream& output, std::string_view phase, const std::array<SourceSnapshot, 16>& snapshots) {
    for (std::size_t actor = 0; actor < snapshots.size(); ++actor) {
        for (const auto& field : contact_cache_fields) {
            const auto raw = field.block == 0 ? std::span<const std::uint8_t>(snapshots[actor].body) : std::span<const std::uint8_t>(snapshots[actor].dynamics);
            const auto value = field.format == 'i'   ? std::int64_t(source_i32(raw, field.offset))
                               : field.format == 'H' ? std::int64_t(source_u16(raw, field.offset))
                                                     : std::int64_t(raw[field.offset]);
            output << phase << ',' << actor << ',' << field.name << ',' << value << '\n';
        }
    }
}

void restore_startup_cold_actor_context(Record& record, const ColdGridInitResult& startup, bool driver_aliases) {
    for (const auto owner : record.ownership) {
        if (owner) throw std::runtime_error("Observed initial actor ownership cannot enter cold replay");
    }
    const auto restore = [](SourceSnapshot& target, const SourceSnapshot& generated) {
        for (const auto& field : source_cold_actor_fields) {
            const auto raw = field.block == 0 ? std::span<const std::uint8_t>(target.body) : std::span<const std::uint8_t>(target.dynamics);
            if (std::any_of(raw.begin() + field.offset, raw.begin() + field.offset + field.size, [](auto value) { return value != 0; })) {
                throw std::runtime_error(std::string("Observed initial actor context cannot enter cold replay: ") + field.name);
            }
        }
        if (target.limiter_active || target.limiter_load_q16) throw std::runtime_error("Observed initial limiter context cannot enter cold replay");
        copy_source_cold_actor_context(target, generated);
    };
    for (std::size_t actor = 0; actor < record.actors.size(); ++actor) {
        record.ownership[actor] = std::uint8_t(startup.ownership[actor]);
        restore(record.actors[actor].snapshot, startup.actors[actor].snapshot);
        if (driver_aliases) restore(record.traffic.actors[actor], startup.actors[actor].snapshot);
    }
}

void emit_startup_world_reset(std::ostream& output, const std::vector<InitialWorldValue>& initialized, const std::vector<InitialWorldValue>& restored) {
    output << "phase,pair,field,value\n";
    for (const auto& value : initialized) {
        output << "first_pre," << value.index << ',' << value.field << ',' << value.value << '\n';
    }
    for (const auto& value : restored) {
        output << "restored," << value.index << ',' << value.field << ',' << value.value << '\n';
    }
}

WarmGridInitInputs warm_grid_context(
    const AssetStore& assets, const AiInitialState& initial, const Record& first, std::uint32_t track, std::uint32_t local, std::uint32_t weather, std::uint32_t steering) {
    if (local >= 16 || local % 2 != 0) {
        throw UnsupportedSourceState("Warm ordinary grid requires the selected single player's normal car binding");
    }
    WarmGridInitInputs input;
    input.configuration.setup = scene::resolve_setup({.track_id = track, .car_id = local / 2, .weather_choice = weather});
    input.configuration.steering_profile = steering;
    input.configuration.manual = source_u32(first.traffic.actors[local].dynamics, 0x27c) == 1;
    const auto begin = initial.resolved.begin() + local * 40;
    const auto end = std::find(begin, begin + 16, 0);
    if (end == begin || end == begin + 16 || end - begin > 9) {
        throw UnsupportedSourceState("Warm grid requires the initial bounded local CONFIG name");
    }
    input.configuration.player_name.assign(begin, end);
    bool matched = false;
    for (std::uint32_t difficulty = 0; difficulty < 3; ++difficulty) {
        auto data = scene::load_qualifying_data(assets, input.configuration.setup, difficulty, input.configuration.player_name);
        bool agrees = true;
        for (std::size_t actor = 0; actor < 16; ++actor) {
            agrees &= std::equal(data.drivers[actor].raw.begin() + 16, data.drivers[actor].raw.end(), initial.resolved.begin() + actor * 40 + 16);
        }
        if (!agrees) {
            continue;
        }
        if (matched) {
            throw UnsupportedSourceState("Initial resolved driver bank does not uniquely identify canonical qualifying bounds");
        }
        matched = true;
        input.configuration.difficulty = difficulty;
        input.data = std::move(data);
    }
    if (!matched) {
        throw UnsupportedSourceState("Initial resolved drivers disagree with every ordinary canonical difficulty bank");
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        std::copy_n(initial.resolved.begin() + actor * 40, 40, input.data.drivers[actor].raw.begin());
    }
    return input;
}

int replay_qualifying_startup(const char* root, const char* input_path, const char* output_path) {
    const auto input = read_qualifying_startup_inputs(input_path);
    const auto generated = initialize_qualifying_start(AssetStore(root), input.configuration);
    std::ofstream output(output_path);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "phase,group,actor,field,value\n";
    const auto emit = [&](const ColdGridInitResult& grid, std::string_view phase) {
        emit_cold_grid(output, input.configuration, grid, true, phase, true);
        output << phase << ",camera,-1,heading," << generated.camera_heading << '\n';
    };
    emit(generated.first, "first_return");
    emit(generated.repeated, "second_return");
    emit(generated.repeated, "first_pre");
    SourceTimerBank timers;
    timers.create(input.creation_clock);
    SourceQualifyingApproach approach;
    if (!approach.begin_primary(timers)) {
        throw std::logic_error("Cold qualifying must initialize on its first primary");
    }
    timers.clock = input.caller_clock;
    timers.reset(2); //443f60→44ffe1→421290; State4 retains timer3.
    const auto clock = SourceClock::from_stored_divisor(input.divisor);
    (void)clock; //Validate the authentic stored divisor without inventing QPF.
    const TimeStep initial;
    const auto row = [&](const std::string& field, std::int64_t value) {
        output << "first_pre,clock,-1," << field << ',' << value << '\n';
    };
    for (std::size_t slot = 0; slot < timers.origins.size(); ++slot) {
        row("timer" + std::to_string(slot) + "_origin", timers.origins[slot]);
    }
    row("cached_clock", timers.clock);
    row("qpc_divisor", input.divisor);
    row("delta_q16", initial.delta_q16);
    row("reciprocal_q16", initial.reciprocal_q16);
    row("extra_update", initial.extra_step);
    std::cout << "Generated two cold qualifying initializations and their first physical handoff\n";
    return 0;
}

int replay_cold_grid_initialization(const char* root, const char* input_path, const char* output_path, bool sequence) {
    const auto request = read_cold_grid_inputs(input_path);

    //The factory sees canonical resources and typed entry values only. There
    //is no captured actor, resolved-driver, return or first-PRE state here.

    std::ofstream output(output_path);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << (sequence ? "phase,group,actor,field,value\n" : "group,actor,field,value\n");
    const auto emit = [&](const ColdGridInitResult& result, std::string_view phase) {
        emit_cold_grid(output, request, result, sequence, phase);
    };
    if (sequence) {
        const auto result = initialize_start_grid(AssetStore(root), request);
        emit(result.first, "first_return");
        emit(result.repeated, "second_return");
        //No source state is imported between these checks. The first-PRE
        //projection asserts survival of the named initializer writers only.
        emit(result.repeated, "first_pre");
        std::cout << "Published two canonical initializations and retained named first-PRE state\n";
    } else {
        emit(initialize_cold_grid(AssetStore(root), request), "");
        std::cout << "Published one canonical cold-grid initialization at460170 return\n";
    }
    return 0;
}

} // namespace toca::test::fleet
