#include "record.hpp"

#include "binary_io.hpp"

#include <algorithm>
#include <bit>
#include <istream>

namespace toca::test::fleet {
using namespace toca::simulation;

AiInitialState read_ai_initial(std::istream& input) {
    AiInitialState initial;
    const auto points = read_u32(input);
    initial.context.start_bound = read_u32(input);
    initial.context.end_bound = read_u32(input);
    if (points < 33 || points > 256 || initial.context.start_bound != 25 || initial.context.end_bound != points - 8) {
        throw UnsupportedSourceState("AI route comparison requires4019f0's initialized33-vector start-line bounds");
    }
    read(input, initial.resolved);
    initial.route.resize(4 + points * 64);
    read(input, initial.route);
    if (source_u32(initial.route, 0) != points) {
        throw std::runtime_error("Initialized POINTS header disagrees with AI route count");
    }
    for (auto* table : {&initial.context.start_primary, &initial.context.start_alternate}) {
        for (auto& vector : *table) {
            for (auto& value : vector) {
                value = std::bit_cast<std::int32_t>(read_u32(input));
            }
        }
    }
    return initial;
}

Record read_record(std::istream& input, std::uint32_t steering_profile, std::uint32_t local, bool has_ai, bool has_timing, bool has_finish, bool has_handoffs, bool has_session,
    bool has_traffic, bool has_nearest, bool has_joined, bool retained_limiter, bool startup_initial, bool generated_time) {
    Record record;
    record.id = read_u32(input);
    record.time.delta_q16 = std::bit_cast<std::int32_t>(read_u32(input));
    record.time.reciprocal_q16 = std::bit_cast<std::int32_t>(read_u32(input));
    const auto extra = read_u32(input);
    if (generated_time && (record.time.delta_q16 != 0 || record.time.reciprocal_q16 != 0 || extra != 0)) {
        throw std::runtime_error("Observed per-update timing cannot enter source clock transactions");
    }
    record.time.extra_step = extra != 0;
    record.rng = read_u32(input);
    record.roughness = read_u32(input);
    const bool limiter = read_u32(input) != 0;
    const auto limiter_load = std::bit_cast<std::int32_t>(read_u32(input));
    if (retained_limiter && (limiter || limiter_load != 0)) {
        throw std::runtime_error("Retained limiter cannot import local limiter metadata");
    }
    record.damage_cycle = read_u32(input);
    record.detached_count = read_u32(input);
    record.contacts_enabled = read_u32(input) != 0;
    read(input, record.pair_history);
    read(input, record.ownership);
    read(input, record.damage_selection);
    read(input, record.damage_levels);
    read(input, record.broken_features);
    read(input, record.detached_pool);
    read(input, record.detached_free);
    read(input, record.visibility);
    record.visibility_control = read_u32(input);
    record.visibility_override = read_u32(input);
    record.ai_tire_base = std::bit_cast<std::int32_t>(read_u32(input));
    record.launch_flags = read_u32(input);
    record.consumed_launch = std::bit_cast<std::int32_t>(read_u32(input));
    record.pre_launch = std::bit_cast<std::int32_t>(read_u32(input));
    record.pre_rpm = std::bit_cast<std::int32_t>(read_u32(input));
    record.pre_start_hold = read_u32(input);
    record.release_epoch = read_u32(input);
    record.pre_session_state = read_u32(input);
    record.pre_controller_owner = std::bit_cast<std::int32_t>(read_u32(input));
    if (record.launch_flags > 0xff || record.pre_start_hold > 1 || record.pre_controller_owner < -1 || record.pre_controller_owner > 3) {
        throw std::runtime_error("Invalid original local launch boundary metadata");
    }
    const auto selection_count = read_u32(input);
    record.selection_context.local_players = read_u32(input);
    record.selection_context.local_actor = read_u32(input);
    record.selection_context.tire_base_q16 = std::bit_cast<std::int32_t>(read_u32(input));
    record.selection_context.full_budget = read_u32(input);
    const auto alternate = read_u32(input);
    if (selection_count != 16 || record.selection_context.local_actor != local || alternate > 1) {
        throw std::runtime_error("Invalid original body-selection context");
    }
    if (record.selection_context.local_players != 1 || record.selection_context.full_budget != (startup_initial ? 0u : 4u)) {
        throw UnsupportedSourceState("Body-selection replay requires the ordinary one-local-player four-body budget");
    }
    record.selection_context.alternate_direction = alternate != 0;
    for (auto& actor : record.selection) {
        const auto previous = read_u32(input), human = read_u32(input);
        actor.player_identity = std::bit_cast<std::int32_t>(read_u32(input));
        const auto rollover = read_u32(input);
        actor.support_count = read_u32(input);
        actor.first_tire_q16 = std::bit_cast<std::int32_t>(read_u32(input));
        actor.normal_up_q16 = std::bit_cast<std::int32_t>(read_u32(input));
        const auto ring = read_u32(input);
        actor.forward_gap_q16 = std::bit_cast<std::int32_t>(read_u32(input));
        if (previous > 4 || human > 1 || rollover > 3 || actor.support_count > 8 || actor.player_identity < -1 || actor.player_identity > 3 || ring > 0xffff) {
            throw std::runtime_error("Invalid original body-selection actor metadata");
        }
        actor.previous = BodyMode(previous);
        actor.human_control = human != 0;
        actor.rollover.upside_down = (rollover & 1) != 0;
        actor.rollover.sideways = (rollover & 2) != 0;
        actor.ahead = ring & 0xff;
        actor.behind = (ring >> 8) & 0xff;
    }
    if (has_ai) {
        record.ai_release_counter = read_u32(input);
        record.ai_rng = read_u32(input);
        for (auto& actor : record.ai_actors) {
            actor.body_mode = read_u32(input);
            actor.speed_q16 = std::bit_cast<std::int32_t>(read_u32(input));
            for (auto& value : actor.body.position) {
                value = std::bit_cast<std::int32_t>(read_u32(input));
            }
            for (auto& row : actor.body.basis) {
                for (auto& value : row) {
                    value = std::bit_cast<std::int32_t>(read_u32(input));
                }
            }
            read(input, actor.gameplay);
        }
    }
    if (has_timing) {
        record.timing_clock = read_u32(input);
        record.timing_stage = std::bit_cast<std::int32_t>(read_u32(input));
        record.timing_state = std::bit_cast<std::int32_t>(read_u32(input));
        record.timing_lap_limit = read_u32(input);
        record.timing_flags = read_u32(input);
        record.timing_running = read_u32(input);
        for (auto& time : record.checkpoint_times) {
            time.clock = read_u32(input);
            time.lap = std::bit_cast<std::int32_t>(read_u32(input));
            time.actor = std::bit_cast<std::int32_t>(read_u32(input));
        }
    }
    if (has_finish) {
        record.source_mode = read_u32(input);
        record.record_playback = read_u32(input);
        record.finish_release_clock = read_u32(input);
        record.finish.count = read_u32(input);
        record.finish.local_count = read_u32(input);
        record.finish.timer2 = read_u32(input);
        record.finish.timer3 = read_u32(input);
        record.finish.pending_state = read_u32(input);
        record.finish.stopped = read_u32(input);
        read(input, record.finish.local_finished);
        read(input, record.finish.order);
        const auto short_value = [&] {
            std::array<std::uint8_t, 2> bytes;
            read(input, bytes);
            return std::bit_cast<std::int16_t>(source_u16(bytes, 0));
        };
        for (auto& display : record.displays.players) {
            display.mode = std::bit_cast<std::int32_t>(read_u32(input));
            display.leader = std::bit_cast<std::int32_t>(read_u32(input));
            display.active = std::bit_cast<std::int32_t>(read_u32(input));
            display.ticks = short_value();
            for (auto& component : display.components) {
                component = short_value();
            }
            for (auto* values : {&display.reference, &display.current}) {
                for (auto& value : *values) {
                    value = std::bit_cast<std::int32_t>(read_u32(input));
                }
            }
            for (auto* text : {&display.text, &display.leader_text}) {
                std::array<std::uint8_t, 16> bytes;
                read(input, bytes);
                std::copy(bytes.begin(), bytes.end(), text->begin());
            }
        }
        for (auto& value : record.displays.persisted) {
            value = std::bit_cast<std::int32_t>(read_u32(input));
        }
    }
    if (has_handoffs) {
        const auto count = read_u32(input);
        if (count > 16) {
            throw UnsupportedSourceState("Local handoff event count exceeds the bounded actor pass");
        }
        record.handoffs.resize(count);
        for (auto& event : record.handoffs) {
            event.kind = read_u32(input);
            event.actor = read_u32(input);
            event.return_address = read_u32(input);
            event.rng = read_u32(input);
            event.ai_tire_base = std::bit_cast<std::int32_t>(read_u32(input));
            auto& state = event.before;
            state.human = read_u32(input);
            state.reverse_fallback = read_u32(input);
            state.countdown = read_u32(input);
            state.player = std::bit_cast<std::int32_t>(read_u32(input));
            state.gear = read_u32(input);
            state.queued_shift = read_u32(input);
            for (auto* values : {&state.modes, &state.saved_modes}) {
                for (auto& value : *values) {
                    value = std::bit_cast<std::int32_t>(read_u32(input));
                }
            }
            for (auto& value : state.tires) {
                value = std::bit_cast<std::int32_t>(read_u32(input));
            }
        }
    }
    if (has_session) {
        const auto extended = read_u32(input);
        if (extended > 1) {
            throw std::runtime_error("Invalid session-boundary provenance marker");
        }
        record.session_extended = extended != 0;
        read(input, record.session_dynamics);
        record.session_ai_tire_base = std::bit_cast<std::int32_t>(read_u32(input));
        for (auto& value : record.session_launch) {
            value = std::bit_cast<std::int32_t>(read_u32(input));
        }
        if (record.session_extended) {
            read(input, record.classification_order);
            read(input, record.split_enable_raw);
            record.lap_distance = std::bit_cast<std::int32_t>(read_u32(input));
            record.release_marker = read_u32(input);
            read(input, record.configuration_tail);
            for (auto& value : record.release_flags) {
                value = read_u32(input);
            }
        }
    }
    if (has_traffic) {
        auto& traffic = record.traffic;
        traffic.rng = read_u32(input);
        traffic.source_mode = read_u32(input);
        traffic.session_state = std::bit_cast<std::int32_t>(read_u32(input));
        traffic.race_stop = read_u32(input);
        traffic.release_counter = read_u32(input);
        traffic.weather_penalty = std::bit_cast<std::int32_t>(read_u32(input));
        read(input, traffic.tuning);
        read(input, traffic.status);
        if (generated_time && (traffic.session_state != 0 || std::ranges::any_of(std::span(traffic.status).first(16), [](auto value) {
                return value != 0;
            }))) {
            throw std::runtime_error("Observed session state or NoQual flags cannot enter source lifecycle transactions");
        }
        for (auto& value : traffic.headlight_gates) {
            value = read_u32(input);
        }
        traffic.force_acceleration = read_u32(input);
        read(input, traffic.braking_memory);
        for (auto& actor : traffic.actors) {
            read(input, actor.body);
            read(input, actor.dynamics);
            read(input, actor.gameplay);
        }
    }
    if (has_nearest) {
        const auto count = read_u32(input);
        if (count > 16) {
            throw UnsupportedSourceState("Nearest recovery event count exceeds the bounded actor pass");
        }
        record.nearest_recoveries.resize(count);
        for (auto& event : record.nearest_recoveries) {
            event.actor = read_u32(input);
            event.rng = read_u32(input);
            event.position[0] = std::bit_cast<std::int32_t>(read_u32(input));
            event.position[2] = std::bit_cast<std::int32_t>(read_u32(input));
            event.cursor.next = read_u32(input);
            event.cursor.previous = read_u32(input);
            event.refresh = read_u32(input);
            event.human = read_u32(input);
        }
    }
    if (has_joined) {
        record.update_caller = read_u32(input);
        const auto count = read_u32(input);
        if (generated_time && (record.update_caller != 0 || count != 0)) {
            throw std::runtime_error("Observed wrapper or release events cannot enter source lifecycle transactions");
        }
        if (count > 1) {
            throw UnsupportedSourceState("Joined cold-grid interval has more than one release event per update");
        }
        record.releases.resize(count);
        for (auto& event : record.releases) {
            event.return_address = read_u32(input);
            event.source_mode = read_u32(input);
            event.stage = read_u32(input);
            event.local_players = read_u32(input);
        }
    }
    std::array<std::uint32_t, 3> prop_counts;
    for (auto& count : prop_counts) {
        count = read_u32(input);
    }
    constexpr std::array<std::size_t, 3> strides{0xcc, 0x13c, 0x40};
    for (std::size_t i = 0; i < prop_counts.size(); ++i) {
        if (prop_counts[i] > 4096) {
            throw std::runtime_error("Captured prop pool exceeds diagnostic allocation bound");
        }
        record.props[i].resize(prop_counts[i] * strides[i]);
        read(input, record.props[i]);
    }
    record.sign_models.resize(prop_counts[2]);
    for (auto& model : record.sign_models) {
        read(input, model);
    }
    for (auto& actor : record.actors) {
        std::array<std::uint8_t, 12> root;
        read(input, root);
        actor.registration.sector = source_u16(root, 0);
        for (std::size_t i = 0; i < 3; ++i) {
            actor.registration.neighbors[i] = source_u16(root, 2 + i * 2);
        }
        actor.mask = root[8];
        actor.detail = root[9];
        actor.visible = root[10] != 0;
        if (root[11] != 0 || actor.detail > 2) {
            throw std::runtime_error("Invalid original physical-root metadata");
        }
        auto& snapshot = actor.snapshot;
        snapshot.has_barrier_recovery = true;
        snapshot.limiter_active = read_u32(input) != 0;
        snapshot.limiter_load_q16 = std::bit_cast<std::int32_t>(read_u32(input));
        snapshot.steering_profile = steering_profile;
        read(input, snapshot.barrier_recovery);
        read(input, snapshot.body);
        read(input, snapshot.dynamics);
        read(input, snapshot.gameplay);
    }
    for (std::size_t actor = 0; actor < 16; ++actor) {
        auto& snapshot = record.actors[actor].snapshot;
        snapshot.has_damage = true;
        for (std::size_t level = 0; level < 17; ++level) {
            snapshot.damage_levels[level] = record.damage_levels[level * 16 + actor];
        }
        std::copy_n(record.broken_features.begin() + actor * 16, 16, snapshot.broken_features.begin());
    }
    record.actors[local].snapshot.launch_tire_adjustment_q16 = record.consumed_launch;
    if (!retained_limiter && (record.actors[local].snapshot.limiter_active != limiter || record.actors[local].snapshot.limiter_load_q16 != limiter_load)) {
        throw std::runtime_error("Fleet local limiter metadata disagrees with its actor record");
    }
    return record;
}

} // namespace toca::test::fleet
