#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "simulation/motion.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/surface_roughness.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {
using namespace toca::simulation;

void read(std::istream& input, std::span<std::uint8_t> bytes) {
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!input) {
        throw std::runtime_error("Truncated original replay stream");
    }
}

std::uint32_t read_u32(std::istream& input) {
    std::array<std::uint8_t, 4> bytes;
    read(input, bytes);
    return source_u32(bytes, 0);
}

struct Column {
    std::string name;
    std::int32_t value;
};

std::vector<Column> columns(std::uint32_t id, const Motion& motion) {
    const auto& state = motion.state();
    const auto& control = motion.controls();
    std::vector<Column> result{{"id", std::int32_t(id)}, {"boundary", std::int32_t(state.boundary)}};
    const auto scalar = [&](std::string name, std::int32_t value) {
        result.push_back({std::move(name), value});
    };
    const auto vector = [&](const std::string& prefix, const auto& values) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            scalar(prefix + std::to_string(i), values[i]);
        }
    };
    for (std::size_t i = 0; i < 3; ++i) {
        scalar("position_" + std::string(1, "xyz"[i]), state.body.position[i]);
        scalar("velocity_" + std::string(1, "xyz"[i]), state.velocity_q16[i]);
    }
    vector("angular_velocity_", state.angular_velocity_q16);
    scalar("speed", state.speed_q16);
    scalar("speed_integer", state.speed_integer);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            scalar("basis_" + std::to_string(i) + std::to_string(j), state.body.basis[i][j]);
        }
    }
    vector("wheel_speed_", state.wheel_speed_q16);
    vector("longitudinal_slip_", state.longitudinal_slip_q16);
    vector("lateral_slip_", state.lateral_slip_q16);
    vector("contact_", state.wheel_contact);
    vector("surface_", state.surface_ids);
    vector("rumble_", state.rumble);
    vector("previous_rumble_", state.previous_rumble);
    for (std::size_t i = 0; i < 8; ++i) {
        vector("contact_position_" + std::to_string(i) + "_", state.body.contacts[i]);
        scalar("height_" + std::to_string(i), state.body.surfaces[i].height);
        scalar("triangle_" + std::to_string(i), state.body.surfaces[i].triangle);
    }
    scalar("contact_count", std::int32_t(state.contact_count));
    scalar("raw_throttle", control.raw_throttle_q16);
    scalar("raw_brake", control.raw_brake_q16);
    scalar("raw_left", control.raw_left_q16);
    scalar("raw_right", control.raw_right_q16);
    scalar("throttle", control.throttle_q16);
    scalar("brake", control.brake_q16);
    scalar("steering", control.steering_q16);
    scalar("steering_angle", control.steering_angle);
    scalar("visual_local_x", state.car_visual_angles.local_x);
    scalar("visual_absolute_z", state.car_visual_angles.absolute_z);
    scalar("gear", control.gear);
    scalar("gear_pending", control.gear_pending);
    scalar("reverse_pedal", control.reverse_pedal);
    scalar("engine_speed", control.engine_speed_q16);
    scalar("engine_torque", control.engine_torque_q16);
    scalar("engine_drive", control.engine_drive_q16);
    vector("wheel_drive_", control.wheel_drive_q16);
    vector("tire_offset_", control.tire_offsets_q16);
    return result;
}

void validate_sequential_configuration(const SourceSnapshot& initial, const SourceSnapshot& current, std::uint32_t id) {
    const auto reject = [&](const std::string& field) {
        throw std::runtime_error("Sequential update " + std::to_string(id) + " has an unsupported external change to" + field);
    };
    for (const auto offset : {0xc, 0x18, 0x1c, 0x20, 0x24, 0x30, 0x34, 0x3c, 0x48, 0x148, 0x27c, 0x310, 0x314, 0x318}) {
        if (source_u32(initial.dynamics, offset) != source_u32(current.dynamics, offset)) {
            reject(" dynamics+" + std::to_string(offset));
        }
    }
    for (const auto offset : {0x20, 0x24, 0x28, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x510}) {
        if (source_u32(initial.body, offset) != source_u32(current.body, offset)) {
            reject(" body+" + std::to_string(offset));
        }
    }
    for (std::size_t offset = 0xdc; offset <= 0xf8; offset += 4) {
        if (source_u32(initial.dynamics, offset) != source_u32(current.dynamics, offset)) {
            reject(" tire parameters");
        }
    }
    if (initial.steering_profile != current.steering_profile || source_u16(initial.dynamics, 0x276) != source_u16(current.dynamics, 0x276)
        || initial.dynamics[0x335] != current.dynamics[0x335] || source_u32(initial.gameplay, 8) != source_u32(current.gameplay, 8)) {
        reject(" driver ownership or steering configuration");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5 && argc != 6) {
            throw std::runtime_error("Usage: toca-reference-replay CANONICAL_ROOT TRACK_ID INPUT_BIN OUTPUT_CSV [independent|sequential]");
        }
        const auto mode = argc == 6 ? std::string(argv[5]) : "sequential";
        if (mode != "independent" && mode != "sequential") {
            throw std::runtime_error("Original replay mode must be independent or sequential");
        }
        const bool sequential = mode == "sequential";
        std::ifstream input(argv[3], std::ios::binary);
        std::array<std::uint8_t, 8> magic;
        read(input, magic);
        if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'R', 'E', 'P', 'L'} || read_u32(input) != 1) {
            throw std::runtime_error("Unsupported original replay stream schema");
        }
        const auto car = read_u32(input), weather = read_u32(input), manual = read_u32(input), count = read_u32(input);
        if (car > 9 || weather < 1 || weather > 5 || manual > 1 || count == 0 || count > 100000) {
            throw std::runtime_error("Original replay stream has invalid vehicle metadata or update count");
        }
        const auto track = std::stoul(argv[2]);
        if (track > 9) {
            throw std::runtime_error("Original replay track must be 0..9");
        }
        const toca::AssetStore store(argv[1]);
        const auto prefix = std::string(toca::scene::track_content(std::uint32_t(track)).archive_prefix);
        const auto keep = store.open_archive(prefix + "keep.bfp");
        const auto free = store.open_archive(prefix + "free.bfp");
        const auto terrain = toca::parse_terrain(free.member(1).bytes());
        const toca::HpcSurface surface(keep.member(0).bytes());
        const toca::scene::BarrierData barriers(keep.member(1).bytes(), terrain.sector_centers);
        std::unique_ptr<PropContacts> props;
        if (sequential) {
            const auto cones = toca::scene::decode_props(free.member(8).bytes(), toca::scene::PropKind::cones);
            const auto posts = track == 9 ? toca::scene::PropResources{} : toca::scene::decode_props(free.member(9).bytes(), toca::scene::PropKind::posts);
            const auto signs = track == 9 ? toca::scene::PropResources{} : toca::scene::decode_props(free.member(10).bytes(), toca::scene::PropKind::signs);
            props = std::make_unique<PropContacts>(cones, posts, signs, terrain.sector_centers, surface);
        }
        DamageContext damage(surface, &barriers);
        SurfaceRoughness roughness;
        Motion motion(surface, {}, car, manual != 0, weather, false, damage, roughness, &barriers);
        std::ofstream output(argv[4]);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        SourceSnapshot initial;
        for (std::uint32_t record = 0; record < count; ++record) {
            const auto id = read_u32(input);
            TimeStep time;
            time.delta_q16 = std::bit_cast<std::int32_t>(read_u32(input));
            time.reciprocal_q16 = std::bit_cast<std::int32_t>(read_u32(input));
            time.extra_step = read_u32(input) != 0;
            (void)read_u32(input); // RNG is recorded; this bounded consumer draws none.
            SourceSnapshot snapshot;
            snapshot.limiter_active = read_u32(input) != 0;
            snapshot.limiter_load_q16 = std::bit_cast<std::int32_t>(read_u32(input));
            snapshot.roughness_cursor = read_u32(input);
            snapshot.steering_profile = read_u32(input);
            read(input, snapshot.body);
            read(input, snapshot.dynamics);
            read(input, snapshot.gameplay);
            if (!sequential || record == 0) {
                roughness.import_cursor(snapshot.roughness_cursor);
                motion.restore_source_snapshot(snapshot);
                initial = snapshot;
            } else {
                validate_sequential_configuration(initial, snapshot, id);
            }
            if (sequential) {
                const auto& raw = snapshot.dynamics;
                motion.update_source_input(
                    {source_i32(raw, 0x140), source_i32(raw, 0x144), source_i32(raw, 0x138), source_i32(raw, 0x13c), std::bit_cast<std::int8_t>(raw[0x14c])}, time, true);
                props->advance();
                motion.apply_prop_response(props->contact({motion.state().body, motion.state().velocity_q16, car, true}));
                motion.finish_ground();
                damage.advance();
            } else {
                motion.update_source_snapshot(time);
            }
            const auto values = columns(id, motion);
            if (record == 0) {
                for (std::size_t i = 0; i < values.size(); ++i) {
                    output << (i ? "," : "") << values[i].name;
                }
                output << '\n';
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                output << (i ? "," : "") << values[i].value;
            }
            output << '\n';
        }
        if (input.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Original replay stream has trailing records");
        }
        std::cout << "Replayed " << count << " original updates through native motion (" << mode << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
