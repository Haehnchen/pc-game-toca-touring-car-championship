#include "simulation/cold_grid.hpp"

#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::simulation {
namespace {
template <class Bytes> void store(Bytes& bytes, std::size_t offset, std::uint32_t value, std::size_t size = 4) {
    for (std::size_t byte = 0; byte < size; ++byte) {
        bytes.at(offset + byte) = std::uint8_t(value >> (byte * 8));
    }
}

void publish_controls(SourceSnapshot& snapshot, const Controls& controls, bool human) {
    controls.publish_source_calibration(snapshot);
    const auto& state = controls.state();
    auto& raw = snapshot.dynamics;
    const auto pedal = controls.pedal_filter_state();
    const auto steering = controls.steering_filter_state();
    for (const auto& [offset, value] :
        std::array<std::pair<std::size_t, std::int32_t>, 13>{{{0x14, pedal.throttle_step_q16}, {0x38, pedal.brake_step_q16}, {0x44, steering.return_step_q16},
            {0x120, pedal.throttle_ramp_q16}, {0x124, state.engine_speed_q16}, {0x130, pedal.brake_q16}, {0x134, pedal.brake_ramp_q16}, {0x148, controls.start_held()},
            {0x150, steering.accumulator_q16}, {0x304, state.reverse_pedal}, {0x30c, state.gear_pending}, {0x310, human ? 0 : 1}, {0x314, human ? 0 : 1}}}) {
        store(raw, offset, std::uint32_t(value));
    }
    const auto handoff = controls.controller_handoff_state();
    store(raw, 0x318, std::uint32_t(handoff.modes[2]));
    store(raw, 0x27c, std::uint32_t(handoff.modes[3]));
    store(raw, 0x274, std::uint16_t(steering.angle), 2);
    raw[0x330] = std::uint8_t(state.gear);
    for (std::size_t tire = 0; tire < state.tire_offsets_q16.size(); ++tire) {
        store(raw, 0xdc + tire * 4, std::uint32_t(state.tire_offsets_q16[tire]));
    }
    //44ffe1 calls the actual4608b0 publication inside this initializer.
    //Its raw144 input is inherited cold-zero context, not an init reset.
    store(snapshot.body, 0x4e8, std::uint32_t(state.raw_brake_q16));
}

void publish_gameplay(
    ColdGridActor& actor, std::uint32_t index, std::uint32_t track, const AiRouteState& route, const AiTrafficDecisionState& traffic, const ControllerHandoffState& control) {
    auto& raw = actor.snapshot.gameplay;
    //401370 owns these defaults; pointer/render storage remains unpublished.
    store(raw, 4, 1);
    store(raw, 0xc, 1); //460170's post401370 player/rank loop, before the first route crossing.
    store(raw, 8, actor.human);
    store(raw, 0x3c, (index & 1) == 0);
    store(raw, 0x50, ((actor.rank & 1) != 0) != (track == 1));
    store(raw, 0xdc, UINT32_MAX);
    raw[0x80] = std::uint8_t(actor.player);
    raw[0x81] = traffic.mode;
    raw[0x83] = std::uint8_t(control.countdown);
    raw[0x8d] = std::uint8_t(index);
    raw[0x8e] = raw[0x8f] = std::uint8_t(actor.rank + 1);
    raw[0x90] = std::uint8_t(actor.rank);
    raw[0x96] = std::uint8_t(actor.ahead);
    raw[0x97] = std::uint8_t(actor.behind);
    raw[0x9b] = 1;
    raw[0x9d] = 2;
    raw[0x9e] = std::uint8_t(route.cached_point);
    raw[0x9f] = traffic.corners[0];
    raw[0xa0] = traffic.corners[1];
    raw[0xa6] = 1;
    store(raw, 0xa8, 1, 2);
    store(raw, 0xbc, actor.rank, 2);
    store(raw, 0xd0, std::uint32_t(route.desired_speed_q16));
    store(raw, 0xd4, std::uint32_t(route.alternate_speed_q16));
    for (std::size_t axis = 0; axis < 3; ++axis) {
        store(raw, 0x108 + axis * 4, std::uint32_t(route.target[axis]));
        store(raw, 0x114 + axis * 4, std::uint32_t(route.alternate_target[axis]));
    }
}

//The generated candidate contains40a770 ->401370 ->412040 publications.
//Copy only their writers: warm process tails and input/physical history do
//not acquire the fresh constructor's defaults.
SourceSnapshot merge_warm_actor(const SourceSnapshot& retained, const SourceSnapshot& generated, bool human, std::int32_t tire_base, std::uint8_t support_prefix) {
    auto result = retained;
    //44d91f ->41a280 repairs actor damage, independently of the retained
    //419ad0 scratch and415d50 detached-slot tails in the shared world.
    result.damage_levels.fill(0);
    result.broken_features.fill(0);
    result.has_damage = true;
    //408690 clears only+30 of the actor-local4bdbd8 recovery record.
    //The independent40d3b0 limiter flag/load retain until their consumer.
    store(result.barrier_recovery, 0x30, 0);
    const auto copy = [](auto& target, const auto& source, std::size_t offset, std::size_t size) {
        std::copy_n(source.begin() + offset, size, target.begin() + offset);
    };
    for (const auto& [offset, size] : std::array<std::pair<std::size_t, std::size_t>, 11>{
             {{0xb0, 36}, {0xe0, 16}, {0xf0, 96}, {0x150, 36}, {0x1c4, 68}, {0x4e8, 6}, {0x4f0, 4}, {0x508, 4}, {0x514, 3}, {0x517, 10}, {0x521, 8}}}) {
        copy(result.body, generated.body, offset, size);
    }
    result.body[0x4f4] = 0xf;
    store(result.body, 0x4f8, 0);
    store(result.body, 0x4fc, 0);
    store(result.body, 0x50c, 1);
    store(result.body, 0x510, 1);
    for (const auto offset : {0xc, 0x10, 0x14, 0x18, 0x1c, 0x24, 0x30, 0x38, 0x3c, 0x44, 0x48, 0x148, 0x150, 0x304, 0x30c}) {
        copy(result.dynamics, generated.dynamics, offset, 4);
    }
    for (const auto& [offset, size] : std::array<std::pair<std::size_t, std::size_t>, 10>{
             {{0xdc, 32}, {0xfc, 20}, {0x120, 8}, {0x130, 8}, {0x1e4, 32}, {0x264, 16}, {0x274, 2}, {0x278, 8}, {0x280, 44}, {0x330, 4}}}) {
        copy(result.dynamics, generated.dynamics, offset, size);
    }
    copy(result.dynamics, generated.dynamics, 0x328, 8); //42e6f0's material cache, separate from its triangle words.
    for (std::size_t index = std::max<std::size_t>(4, support_prefix); index < 8; ++index) {
        result.body[0x521 + index] = retained.body[0x521 + index];
    }
    //44ffe1 ->4469a0 ->44c3f0 clears the eight wheel-producer gate words.
    for (std::size_t offset = 0x2c4; offset < 0x2e4; offset += 4) {
        store(result.dynamics, offset, 0);
    }
    //401370's DWORD, WORD and BYTE stores. In particular gameplay+5c,
    //saved modes+60..6c and padding retain.444270's new qualifying times
    //survive401370 and are published separately below.
    for (const auto offset : {0x00, 0x04, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x3c, 0x40, 0x44, 0x4c, 0x50, 0x54, 0x58, 0x70, 0x74, 0x78, 0x7c, 0xc0,
             0xc4, 0xc8, 0xcc, 0xd0, 0xd4, 0xd8, 0xdc, 0xe0, 0xe4, 0xe8, 0xec, 0xf0, 0xf4, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c, 0x120}) {
        copy(result.gameplay, generated.gameplay, offset, 4);
    }
    for (const auto offset : {0xa2, 0xa4, 0xa6, 0xa8, 0xaa, 0xac, 0xae, 0xb0, 0xb2, 0xb4, 0xb6, 0xb8, 0xba, 0xbc, 0xf8, 0xfa, 0xfc, 0xfe, 0x100, 0x102, 0x104}) {
        copy(result.gameplay, generated.gameplay, offset, 2);
    }
    for (const auto offset : {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
             0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0}) {
        copy(result.gameplay, generated.gameplay, offset, 1);
    }
    ControllerHandoffState handoff;
    handoff.human = source_u32(retained.gameplay, 8);
    handoff.player = human ? 0 : -1;
    handoff.gear = generated.dynamics[0x330];
    handoff.queued_shift = source_u32(retained.dynamics, 0x14c);
    for (std::size_t part = 0; part < 4; ++part) {
        handoff.modes[part] = source_i32(retained.dynamics, part == 3 ? 0x27c : 0x310 + part * 4);
        handoff.saved_modes[part] = source_i32(retained.gameplay, 0x60 + part * 4);
    }
    handoff = apply_controller_handoff(handoff, human ? ControllerHandoffKind::player : ControllerHandoffKind::ai, tire_base);
    store(result.gameplay, 8, handoff.human);
    store(result.gameplay, 0x28, handoff.reverse_fallback);
    result.gameplay[0x83] = std::uint8_t(handoff.countdown);
    for (std::size_t part = 0; part < 4; ++part) {
        store(result.dynamics, part == 3 ? 0x27c : 0x310 + part * 4, std::uint32_t(handoff.modes[part]));
        store(result.gameplay, 0x60 + part * 4, std::uint32_t(handoff.saved_modes[part]));
    }
    if (human) {
        store(result.dynamics, 0x14c, handoff.queued_shift);
    }
    for (std::size_t tire = 0; tire < handoff.tires.size(); ++tire) {
        store(result.dynamics, 0xdc + tire * 4, std::uint32_t(handoff.tires[tire]));
    }
    //4608b0 publishes the inherited raw brake, not a constructor-zero pedal.
    store(result.body, 0x4e8, source_u32(retained.dynamics, 0x144));
    return result;
}
} // namespace

scene::BodyPose cold_grid_body(const HpcSurface& surface, const std::array<scene::GridSlot, 32>& slots, const RaceGrid& grid, std::uint32_t actor, std::uint32_t car,
    const scene::InitialBodySupport* retained_support, bool current_body) {
    return scene::initial_body_pose(surface, scene::race_grid_start(slots, grid.rank(actor)), car, retained_support, current_body);
}

scene::BodyPose cold_qualifying_body(const HpcSurface& surface, std::span<const std::uint8_t> route, std::uint32_t local, std::uint32_t actor, std::uint32_t car,
    const scene::InitialBodySupport* support, bool current_body) {
    return scene::initial_body_pose(surface, scene::qualifying_start(route, local, actor), car, support, current_body);
}

namespace {
ColdGridInitResult initialize_grid(const AssetStore& assets, const ColdGridInitInputs& input, const ColdGridInitResult* previous, bool qualifying = false) {
    if (input.setup.selection.car_id >= 8 || input.setup.selection.track_id >= 10 || input.difficulty > 2 || input.steering_profile < 1 || input.steering_profile > 3
        || input.setup.selection.championship_meeting || input.setup.selection.stars || input.setup.selection.toon || input.setup.selection.rain_up || input.setup.selection.cat_dog
        || input.player_name.empty() || input.player_name.size() > 9 || input.player_name.find('\0') != std::string::npos) {
        throw std::runtime_error("Cold grid requires ordinary one-player NoQual configuration without bonus/cheat/championship setup");
    }
    const auto checked = scene::resolve_setup(input.setup.selection, input.setup.horizon_index);
    if (checked.weather != input.setup.weather || checked.horizon_index != input.setup.horizon_index) {
        throw std::runtime_error("Cold-grid authored setup disagrees with resolved caller weather/horizon");
    }
    ColdGridInitResult result;
    result.data = previous ? previous->data : scene::load_qualifying_data(assets, input.setup, input.difficulty, input.player_name);
    SourceRandom random(previous ? previous->rng_final : input.seed);
    scene::QualifyingResults classification;
    //444270 precedes401370: fresh loader gameplay+8d still binds every actor
    //to driver0. Identity publication below changes the bindings afterward.
    std::array<std::uint8_t, 16> bindings{};
    if (previous) {
        for (std::size_t actor = 0; actor < bindings.size(); ++actor) {
            bindings[actor] = previous->actors[actor].driver_index;
        }
    }
    result.points = previous ? previous->points : input.entry_points;
    //444270 awards pole before460170's separate local-last swap. Preserve
    //the original16-bit store, including its signed-short wrap semantics.
    if (qualifying) {
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            result.grid.rank_to_actor[actor] = std::uint8_t(actor);
        }
        result.status = {1, 0, 0, 1}; //443f60 then401260(0).
    } else {
        result.grid = make_race_grid(result.data, random, nullptr, &classification, &bindings);
        result.pole_actor = classification.rows.front().actor_id;
        result.points[result.pole_actor] = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(result.points[result.pole_actor]) + 1u));
    }
    const auto local = result.data.local_actor;
    auto& local_driver = result.data.drivers[local].raw;
    std::copy(input.player_name.begin(), input.player_name.end(), local_driver.begin());
    local_driver[input.player_name.size()] = 0;
    result.pair_handles.fill(-1);

    const auto prefix = std::string(scene::track_content(input.setup.selection.track_id).archive_prefix);
    const auto keep = assets.open_archive(prefix + "keep.bfp");
    const HpcSurface surface(keep.member(0).bytes());
    const auto route = initialize_ai_route_heights(keep.member(1).bytes(), surface);
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());
    DamageContext damage(surface);
    SurfaceRoughness roughness;
    for (std::uint32_t index = 0; index < result.actors.size(); ++index) {
        auto& actor = result.actors[index];
        const auto car = result.data.drivers[index].car_id;
        actor.rank = result.grid.rank(index);
        actor.driver_index = std::uint8_t(index);
        actor.ahead = result.grid.rank_to_actor[(actor.rank + 15) % 16];
        actor.behind = result.grid.rank_to_actor[(actor.rank + 1) % 16];
        actor.human = index == local;
        actor.player = actor.human ? 0 : -1;
        result.ownership[index] = std::int8_t(actor.player);
        const auto loader_support = scene::initial_body_support(surface, {slots[index].first_xz, slots[index].heading, index}, car);
        //The preceding40a770 loop leaves the process current-body on15.
        //412040 does not rebind it when placing the other actor records.
        actor.body = qualifying ? cold_qualifying_body(surface, route, local, index, car, &loader_support, index == result.actors.size() - 1)
                                : cold_grid_body(surface, slots, result.grid, index, car, &loader_support, index == result.actors.size() - 1);
        Motion motion(surface, actor.body, car, actor.human && input.manual, input.setup.weather, qualifying, damage, roughness);
        motion.set_actor_identity(index, actor.human);
        motion.publish_source_physical_state(actor.snapshot);
        // Publish only named consumed cold context from the same real owners
        // used by native driving. Warm merges below retain their own histories.
        motion.publish_source_cold_context(actor.snapshot);
        //40a770 runs410110 at the original first SGRID edge/Y0.412040
        //then resets the first four flags/indices only; the upper flags and
        //descending list tail still describe that preceding support query.
        const auto half_height = scene::car_half_dimensions(car)[1];
        for (std::size_t remaining = 8; remaining != 0; --remaining) {
            const auto contact = remaining - 1;
            const auto y = contact < 4 ? -half_height : half_height;
            const bool active = std::int64_t(loader_support.surfaces[contact].height) + 0x1999 >= y;
            actor.snapshot.body[0x517 + contact] = std::uint8_t(!active);
            if (active) {
                actor.snapshot.body[0x521 + actor.initialized_support_indices++] = std::uint8_t(contact);
            }
        }
        for (std::size_t contact = 0; contact < 4; ++contact) {
            actor.snapshot.body[0x517 + contact] = 0;
            actor.snapshot.body[0x521 + contact] = std::uint8_t(contact);
        }
        actor.snapshot.body[0x515] = std::uint8_t(index);
        actor.snapshot.body[0x516] = std::uint8_t(car);
        store(actor.snapshot.body, 0x4f0, std::uint32_t(actor.mode));
        Controls controls(car, actor.human && input.manual, input.steering_profile);
        if (qualifying || !actor.human) {
            controls.handoff_to_ai(result.data.tire_offset_q16);
        }
        //401260(0) opens the qualifying clutch and selects first for the
        //AI-owned local actor too. No448130 launch correction is calculated.
        controls.set_source_start_hold(true);
        if (qualifying) {
            controls.set_source_start_hold(false);
        }
        actor.controls = controls.state();
        publish_controls(actor.snapshot, controls, actor.human && !qualifying);
        auto driver = make_race_grid_driver(route, result.data, index, random);
        publish_gameplay(actor, actor.driver_index, input.setup.selection.track_id, driver.route_state(), driver.traffic_decision_state(), controls.controller_handoff_state());
        if (qualifying) {
            auto& game = actor.snapshot.gameplay;
            const auto start = scene::qualifying_start(route, local, index);
            store(game, 8, 0); //402be0 owns every controller during the approach.
            store(game, 0xc, 0); //443d30 precedes401370, which retains this field.
            //460070 assigns actor IDs to+bc before either443f60 call;
            //401370 retains those bindings when selecting the lane parity.
            store(game, 0x50, ((index & 1) != 0) != (input.setup.selection.track_id == 1));
            store(game, 0xbc, index, 2);
            store(game, 0xa2, start.source_index + 1, 2);
            store(game, 0xa4, start.source_index, 2);
            game[0xa6] = 0;
            if (actor.human) {
                const auto saved = controls.controller_handoff_state().saved_modes;
                for (std::size_t mode = 0; mode < saved.size(); ++mode) {
                    store(game, 0x60 + mode * 4, std::uint32_t(saved[mode]));
                }
            }
        } else {
            const auto& entry = *std::ranges::find(classification.rows, index, &scene::QualifyingEntry::actor_id);
            store(actor.snapshot.gameplay, 0xb6, entry.time_ms / 60000, 2);
            store(actor.snapshot.gameplay, 0xb8, (entry.time_ms % 60000) / 1000, 2);
            store(actor.snapshot.gameplay, 0xba, (entry.time_ms % 1000) / 10, 2);
        }
    }
    if (qualifying) {
        //443f60 calls401650 in ascending actor order. Each removal splices
        //the traffic ring and moves that actor behind all later removals.
        for (std::uint32_t removed = 0; removed < 16; ++removed) {
            if (removed % 4 == local % 4) {
                result.actors[removed].mode = BodyMode::full;
                store(result.actors[removed].snapshot.body, 0x4f0, 0);
                continue;
            }
            auto& actor = result.actors[removed];
            auto& game = actor.snapshot.gameplay;
            result.actors[actor.behind].ahead = actor.ahead;
            result.actors[actor.ahead].behind = actor.behind;
            for (std::uint32_t other = 0; other < 16; ++other) {
                if (result.actors[other].rank > actor.rank) {
                    --result.actors[other].rank;
                }
            }
            actor.rank = 15;
            actor.mode = BodyMode::removed;
            store(game, 4, 0);
            store(game, 0x58, 1);
            store(game, 0xa8, 0, 2);
            store(actor.snapshot.body, 0x4f0, 4);
        }
        for (std::uint32_t index = 0; index < 16; ++index) {
            auto& actor = result.actors[index];
            auto& game = actor.snapshot.gameplay;
            game[0x8f] = std::uint8_t(actor.rank + 1);
            game[0x90] = std::uint8_t(actor.rank);
            game[0x96] = std::uint8_t(actor.ahead);
            game[0x97] = std::uint8_t(actor.behind);
            result.grid.rank_to_actor[actor.rank] = std::uint8_t(index);
        }
    }
    result.rng_final = random.state();
    return result;
}
} // namespace

ColdGridInitResult initialize_cold_grid(const AssetStore& assets, const ColdGridInitInputs& input) {
    return initialize_grid(assets, input, nullptr);
}

StartGridInitResult initialize_start_grid(const AssetStore& assets, const ColdGridInitInputs& input) {
    StartGridInitResult result;
    result.first = initialize_grid(assets, input, nullptr);
    result.repeated = initialize_grid(assets, input, &result.first);
    return result;
}

QualifyingStartInitResult initialize_qualifying_start(const AssetStore& assets, const ColdGridInitInputs& input) {
    QualifyingStartInitResult result;
    result.first = initialize_grid(assets, input, nullptr, true);
    result.repeated = initialize_grid(assets, input, &result.first, true);
    const auto prefix = std::string(scene::track_content(input.setup.selection.track_id).archive_prefix);
    result.camera_heading = scene::decode_start_grid(assets.open_archive(prefix + "keep.bfp").member(3).bytes())[result.repeated.data.local_actor].heading;
    return result;
}

ColdGridInitResult initialize_warm_grid(const AssetStore& assets, const WarmGridInitInputs& input, SourceRandom& random) {
    ColdGridInitResult previous;
    previous.data = input.data;
    previous.points = input.points;
    previous.rng_final = random.state();
    for (std::size_t actor = 0; actor < input.actors.size(); ++actor) {
        const auto& raw = input.actors[actor];
        if (source_u32(raw.gameplay, 4) != 1 || raw.gameplay[0x8d] >= 16 || source_u32(raw.gameplay, 8) > 1) {
            throw std::runtime_error("Warm grid requires sixteen retained active actors with valid driver/controller bindings");
        }
        previous.actors[actor].driver_index = raw.gameplay[0x8d];
    }
    auto result = initialize_grid(assets, input.configuration, &previous);
    for (std::size_t actor = 0; actor < result.actors.size(); ++actor) {
        result.actors[actor].snapshot = merge_warm_actor(
            input.actors[actor], result.actors[actor].snapshot, actor == result.data.local_actor, result.data.tire_offset_q16, result.actors[actor].initialized_support_indices);
        Controls controls(result.data.drivers[actor].car_id, actor == result.data.local_actor && input.configuration.manual, input.configuration.steering_profile);
        controls.restore_source_snapshot(result.actors[actor].snapshot);
        result.actors[actor].controls = controls.state();
    }
    random = SourceRandom(result.rng_final);
    return result;
}
} // namespace toca::simulation
