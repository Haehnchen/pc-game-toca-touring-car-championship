#include "diagnostics.hpp"

#include "binary_io.hpp"
#include "session_comparison.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

constexpr std::array<std::pair<std::size_t, std::size_t>, 15> physical_body_ranges{{{0xb0, 12}, {0xbc, 12}, {0xc8, 12}, {0xe0, 16}, {0xf0, 96}, {0x150, 36}, {0x1c4, 32},
    {0x1e4, 12}, {0x1f0, 24}, {0x268, 12}, {0x4ec, 2}, {0x514, 1}, {0x517, 8}, {0x51f, 2}, {0x521, 8}}};
constexpr std::array<std::pair<std::size_t, std::size_t>, 12> physical_dynamics_ranges{
    {{0xfc, 16}, {0x1b4, 48}, {0x1e4, 32}, {0x240, 36}, {0x264, 16}, {0x278, 4}, {0x280, 32}, {0x2a0, 12}, {0x2bc, 8}, {0x31c, 4}, {0x328, 8}, {0x331, 3}}};

bool valid_observed_ai_tire_offset(std::int32_t offset, std::int32_t previous, std::int32_t base) {
    //443f60 initializes ranks1..16;402e90 swaps them. Later actors can change
    //an earlier actor's rank after its402b10 call, so the consumed snapshot
    //does not reveal that call's actual rank. Validate the observed output
    //against the complete source range without claiming rank reconstruction.
    constexpr auto first_contact = -0x147 - 0x28f, last_contact = -16 * 0x147 - 0x28f;
    const auto low = std::min(std::int64_t(last_contact), std::int64_t(base));
    const auto high = std::max(std::int64_t(first_contact), std::int64_t(base)) + 0x61;
    if (offset < low || offset > high) {
        return false;
    }
    if (offset == previous || offset == base || (previous < base && std::int64_t(offset) == std::int64_t(previous) + 0x62)) {
        return true;
    }
    for (std::int32_t rank = 1; rank <= 16; ++rank) {
        if (offset == -rank * 0x147 - 0x28f) {
            return true;
        }
    }
    return false;
}

void validate_configuration(const SourceSnapshot& initial, const SourceSnapshot& current, std::uint32_t id, std::uint32_t actor, std::uint32_t local) {
    const auto reject = [&](std::size_t offset, const char* block) {
        throw UnsupportedSourceState(
            "Fleet update " + std::to_string(id) + " actor " + std::to_string(actor) + " has an unsupported external change to " + block + "+" + std::to_string(offset));
    };
    for (const auto offset : {0xc, 0x18, 0x1c, 0x20, 0x24, 0x30, 0x34, 0x3c, 0x48, 0x27c, 0x310, 0x314, 0x318}) {
        if (source_u32(initial.dynamics, offset) != source_u32(current.dynamics, offset)) {
            reject(offset, "dynamics");
        }
    }
    for (const auto offset : {0x20, 0x24, 0x28, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x510}) {
        if (source_u32(initial.body, offset) != source_u32(current.body, offset)) {
            reject(offset, "body");
        }
    }
    for (std::size_t offset = 0xdc; actor != local && source_i32(current.dynamics, 0x27c) != 0 && offset <= 0xf8; offset += 4) {
        if (source_u32(initial.dynamics, offset) != source_u32(current.dynamics, offset)) {
            throw UnsupportedSourceState("Fleet update " + std::to_string(id) + " actor " + std::to_string(actor) + " requires an upstream local-input tire adjustment at dynamics+"
                                         + std::to_string(offset) + "; local launch replay supports only the captured local actor");
        }
    }
    if (initial.body[0x53b] != current.body[0x53b]) {
        reject(0x53b, "sound-slot assignment");
    }
    if (source_u16(initial.dynamics, 0x276) != source_u16(current.dynamics, 0x276) || initial.dynamics[0x335] != current.dynamics[0x335]) {
        reject(0x276, "steering configuration");
    }
}

std::vector<Column> columns(const Motion& motion) {
    const auto& state = motion.state();
    const auto& control = motion.controls();
    std::vector<Column> result{{"boundary", std::int32_t(state.boundary)}};
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
    vector("pending_acceleration_", motion.source_pending_acceleration());
    vector("pending_moment_", motion.source_pending_moment());
    vector("damage_level_", state.damage.levels);
    vector("broken_feature_", state.damage.broken_features);
    scalar("damage_feedback_side", state.damage.feedback_side);
    scalar("damage_feedback_strength", state.damage.feedback_strength);
    return result;
}

void copy_physical_fields(SourceSnapshot& destination, const SourceSnapshot& source) {
    const auto copy = [](auto& to, const auto& from, const auto& ranges) {
        for (const auto& [offset, size] : ranges) {
            std::copy_n(from.begin() + offset, size, to.begin() + offset);
        }
    };
    copy(destination.body, source.body, physical_body_ranges);
    copy(destination.dynamics, source.dynamics, physical_dynamics_ranges);
}

void reject_physical_fields(const SourceSnapshot& source) {
    SourceSnapshot fields;
    copy_physical_fields(fields, source);
    const auto nonzero = [](const auto& bytes) {
        return std::ranges::any_of(bytes, [](auto value) {
            return value != 0;
        });
    };
    if (nonzero(fields.body) || nonzero(fields.dynamics)) {
        throw std::runtime_error("Later body/wheel feedback cannot enter retained replay");
    }
}

void reject_physical_world(const Record& record, bool cold_world) {
    const auto populated = [](const auto& bytes) {
        return std::ranges::any_of(bytes, [](auto value) {
            return value != 0;
        });
    };
    bool observed = record.roughness || record.damage_cycle || record.detached_count || populated(record.pair_history) || populated(record.damage_selection)
                    || populated(record.damage_levels) || populated(record.broken_features) || populated(record.detached_pool) || populated(record.detached_free);
    for (const auto& pool : record.props) {
        observed |= populated(pool);
    }
    for (const auto& model : record.sign_models) {
        observed |= populated(model);
    }
    for (const auto& actor : record.actors) {
        observed |= populated(actor.snapshot.barrier_recovery);
        if (cold_world) {
            observed |= source_u16(actor.snapshot.body, 0x4ec) || actor.snapshot.body[0x514] || actor.snapshot.body[0x529];
        }
    }
    if (cold_world) {
        for (const auto& actor : record.traffic.actors) {
            observed |= source_u16(actor.body, 0x4ec) || actor.body[0x514] || actor.body[0x529];
        }
    }
    if (observed) {
        throw std::runtime_error("Observed physical world state cannot enter the generated world");
    }
}

void reject_scene_inputs(const Record& record) {
    const auto populated = [](const auto& bytes) {
        return std::ranges::any_of(bytes, [](auto value) {
            return value != 0;
        });
    };
    if (record.contacts_enabled || record.visibility_control || record.visibility_override || populated(record.visibility)) {
        throw std::runtime_error("Observed contact/visibility scene gates cannot enter generated scene replay");
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& input = record.actors[actor];
        if (input.registration.sector || populated(input.registration.neighbors) || input.mask || input.detail || input.visible || source_u16(input.snapshot.body, 0x4ec)
            || source_u16(record.traffic.actors[actor].body, 0x4ec)) {
            throw std::runtime_error("Observed physical-root registration, detail or effect gates cannot enter generated scene replay");
        }
    }
}

void physical_feedback_columns(std::ostream& output, const SourceSnapshot& source, bool header) {
    const auto write = [&](const auto& raw, const auto& ranges, const char* prefix) {
        for (const auto& [offset, size] : ranges) {
            for (std::size_t byte = offset; byte < offset + size; ++byte) {
                if (header) {
                    output << ',' << prefix << byte;
                } else {
                    output << ',' << unsigned(raw[byte]);
                }
            }
        }
    };
    write(source.body, physical_body_ranges, "body_");
    write(source.dynamics, physical_dynamics_ranges, "dynamics_");
}

} // namespace toca::test::fleet
