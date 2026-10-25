#include "assets/store.hpp"
#include "formats/terrain.hpp"
#include "race/race_progress.hpp"
#include "scene/catalog.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/rivals.hpp"
#include "simulation/session.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace toca;
using namespace toca::simulation;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void qualifying_startup_consumers(const std::filesystem::path& root) {
    const AssetStore assets(root);
    ColdGridInitInputs input;
    input.setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 1});
    input.seed = 12345;
    input.manual = true;
    input.entry_points[3] = 9;
    const auto generated = initialize_qualifying_start(assets, input);
    const auto local = generated.repeated.data.local_actor;
    const auto keep = assets.open_archive(std::string(scene::track_content(0).archive_prefix) + "keep.bfp");
    const HpcSurface surface(keep.member(0).bytes());
    const auto route = initialize_ai_route_heights(keep.member(1).bytes(), surface);
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());
    expect(generated.camera_heading == slots[local].heading, "460070 binds the qualifying camera to the local actor's SGRID slot before map slew");
    for (const auto* phase : {&generated.first, &generated.repeated}) {
        expect(phase->rng_final == input.seed && phase->points == input.entry_points,
            "Neither qualifying443f60 call may classify, award a pole point or consume RNG");
        expect(phase->status == std::array<std::uint32_t, 4>{1, 0, 0, 1} && phase->release_counter == 0 && phase->clock_first_sample,
            "Qualifying initializes running/controller-release flags without starting a measured lap");
        std::uint32_t active = 0;
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto& entry = phase->actors[actor];
            const auto& raw = entry.snapshot;
            const bool present = actor % 4 == local % 4;
            active += present;
            expect(entry.mode == (present ? BodyMode::full : BodyMode::removed) && source_u32(raw.body, 0x4f0) == std::uint32_t(entry.mode),
                "Qualifying keeps the local actor's four-way group and removes the other twelve physical bodies");
            expect(source_u32(raw.gameplay, 4) == std::uint32_t(present) && source_u32(raw.gameplay, 8) == 0 && source_u32(raw.gameplay, 0xc) == 0,
                "Qualifying approaches are AI-owned and have no race start-line latch");
            expect(source_u16(raw.gameplay, 0xbc) == actor && source_u32(raw.gameplay, 0x50) == (actor & 1) && raw.gameplay[0xa6] == 0
                       && source_u16(raw.gameplay, 0xa8) == std::uint32_t(present),
                "Qualifying retains460070 actor grid bindings through401370 and the later route/checkpoint writers");
            const auto start = scene::qualifying_start(route, local, actor);
            expect(source_u16(raw.gameplay, 0xa2) == start.source_index + 1 && source_u16(raw.gameplay, 0xa4) == start.source_index,
                "Every qualifying actor must own its rotated POINTS approach cursors");
            expect(raw.gameplay[0x8e] == actor + 1 && raw.gameplay[0x8f] == entry.rank + 1 && raw.gameplay[0x90] == entry.rank,
                "401650 removal updates live ranks while retaining the original qualifying rank byte");
            expect(source_u32(raw.dynamics, 0x148) == 0 && source_u32(raw.dynamics, 0x310) == 1 && source_u32(raw.dynamics, 0x314) == 1
                       && source_u32(raw.dynamics, 0x27c) == 0 && raw.dynamics[0x330] == 1,
                "401260 must release the AI-owned qualifying clutch in first gear without a player launch correction");
            const auto support = scene::initial_body_support(surface, {slots[actor].first_xz, slots[actor].heading, actor}, phase->data.drivers[actor].car_id);
            const auto body = cold_qualifying_body(surface, route, local, actor, phase->data.drivers[actor].car_id, &support, actor == 15);
            expect(entry.body.position == body.position && entry.body.basis == body.basis,
                "Qualifying bodies must derive from canonical POINTS and the actual loader support owner");
            if (present) {
                expect(entry.rank < 4 && phase->actors[entry.ahead].mode == BodyMode::full && phase->actors[entry.behind].mode == BodyMode::full,
                    "Removed qualifying actors must be spliced out of the four-car traffic ring");
            }
            expect(raw.body == generated.first.actors[actor].snapshot.body && raw.dynamics == generated.first.actors[actor].snapshot.dynamics
                       && raw.gameplay == generated.first.actors[actor].snapshot.gameplay,
                "The first State4 wrapper repeats initialization without importing preceding initialized bytes");
        }
        expect(active == 4, "Single-player qualifying must retain exactly four physical actors");
    }
    const auto& raw = generated.repeated.actors[local].snapshot;
    expect(source_u32(raw.gameplay, 0x60) == 0 && source_u32(raw.gameplay, 0x64) == 0 && source_u32(raw.gameplay, 0x68) == 0
               && source_u32(raw.gameplay, 0x6c) == 1,
        "AI-owned manual qualifying must retain the local player's saved transmission selection");
    DamageContext damage(surface);
    SurfaceRoughness roughness;
    const auto preview = scene::initial_body_pose(surface, scene::single_qualifying_start(route), input.setup.selection.car_id);
    Motion motion(surface, preview, input.setup.selection.car_id, true, input.setup.weather, true, damage, roughness);
    SourceRandom random(generated.repeated.rng_final);
    motion.prepare_ai(make_race_grid_driver(route, generated.repeated.data, local, random));
    motion.handoff_to_ai(false);
    const auto controls = motion.controls();
    motion.prepare_first_qualifying_step(generated.repeated.actors[local].body);
    expect(motion.controls().gear == controls.gear && motion.state().speed_q16 == 0,
        "First qualifying deadline must retain control and unpublished speed history");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expect(std::uint32_t(motion.state().velocity_q16[axis]) == source_u32(raw.body, 0xc8 + axis * 4),
            "First qualifying velocity must use the generated body's basis rather than the grounded preview basis");
    }
    //412040 adds mul(normal,-dot), rather than subtracting mul(normal,dot).
    //This slope crosses the midpoint sqrt bucket when that rounding is lost.
    auto bucket_body = preview;
    bucket_body.basis[0] = {-53990, 719, -37143};
    bucket_body.basis[1] = {-217, 65517, 1582};
    const std::array<std::int32_t, 3> bucket_velocity{-43184, 574, -29709};
    Motion bucket_motion(surface, bucket_body, input.setup.selection.car_id, true, input.setup.weather, true, damage, roughness);
    expect(bucket_motion.state().velocity_q16 == bucket_velocity,
        "412040 initial velocity must preserve the negative-product rounding before normalization");
    motion.prepare_first_qualifying_step(bucket_body);
    expect(motion.state().velocity_q16 == bucket_velocity,
        "First qualifying deadline must share412040's exact negative-product and sqrt bucket order");
    SourceTimerBank timers;
    timers.create(1000);
    SourceQualifyingApproach approach;
    expect(approach.begin_primary(timers) && approach.state() == 4, "Only the first qualifying primary owns the repeated443f60 transaction");
    timers.clock = 1234; //44a1d0 cached primary clock.
    approach.queue_release(timers.clock);
    timers.clock = 1274; //Optional extra updates the clock without committing State6.
    expect(approach.state() == 4 && approach.pending_state() == 6 && approach.release_clock() == 1234,
        "421390 release queues State6 while the extra physical pass remains in State4");
    expect(!approach.begin_primary(timers) && approach.state() == 6 && timers.origins[0] == 1274,
        "44a370 commits State6 and resets timer0 using the previous cached wrapper clock");
    timers.clock = 1350;
    expect(timers.read(0) == 0x1000, "The new primary caller clock must follow the cached-clock state commit");
}

void qualifying_fleet_consumers(const std::filesystem::path& root) {
    const AssetStore assets(root);
    ColdGridInitInputs input;
    input.setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 1});
    input.seed = 739;
    const auto startup = initialize_qualifying_start(assets, input);
    const auto& data = startup.repeated.data;
    const auto local_actor = data.local_actor;
    const auto prefix = std::string(scene::track_content(0).archive_prefix);
    const auto keep = assets.open_archive(prefix + "keep.bfp");
    const HpcSurface surface(keep.member(0).bytes());
    const auto route = initialize_ai_route_heights(keep.member(1).bytes(), surface);
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());
    DamageContext damage(surface);
    SurfaceRoughness roughness;
    SourceRandom random(startup.repeated.rng_final);
    const auto preview = scene::initial_body_pose(surface, scene::qualifying_start(route, local_actor, local_actor), data.drivers[local_actor].car_id);
    Motion local(surface, preview, data.drivers[local_actor].car_id, false, input.setup.weather, true, damage, roughness);
    local.prepare_ai(make_race_grid_driver(route, data, local_actor, random));
    local.handoff_to_ai(false);
    local.restore_generated_grid_controls(startup.repeated.actors[local_actor].snapshot);
    Rivals rivals(surface, slots, route, startup, input.setup.weather, random, damage, roughness);
    expect(rivals.qualifying() && random.state() == startup.repeated.rng_final,
        "Qualifying fleet must reuse the generated startup RNG without another classification or control draw");
    std::array<scene::BodyPose, 16> bodies;
    std::array<std::uint32_t, 16> next;
    std::array<bool, 16> finished{};
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        bodies[actor] = startup.repeated.actors[actor].body;
        next[actor] = source_u16(startup.repeated.actors[actor].snapshot.gameplay, 0xa2);
        if (actor != local_actor) {
            if (actor != 15) {
                expect(rivals.actor(actor).body.position[1] != bodies[actor].position[1], "Qualifying rival previews must stay grounded until the first actual physical deadline");
            }
            expect(rivals.controls(actor).gear == 1 && rivals.controls(actor).launch_tire_adjustment_q16 == 0, "Qualifying AI starts in first with no NoQual launch correction");
        }
    }
    rivals.prepare_first_qualifying_step(bodies, local);
    const auto initial = rivals.positions(local.state().body);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& state = actor == local_actor ? local.state() : rivals.actor(actor);
        expect(state.body.position == bodies[actor].position, "First qualifying deadline must adopt all sixteen generated body records");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(std::uint32_t(state.velocity_q16[axis]) == source_u32(startup.repeated.actors[actor].snapshot.body, 0xc8 + axis * 4),
                "Every qualifying actor must enter with the generated cccc initial velocity");
        }
    }
    //The next Donington target is faster than160. Qualifying caps only the
    //nonplayer actor and must do so even with its release counter still zero.
    SourceRandom cap_random(input.seed);
    const auto target_speed = source_u32(route, 4 + 64 + 52), alternate_speed = source_u32(route, 4 + 64 + 56);
    expect(target_speed > 160, "Qualifying cap regression requires an authored route target above160");
    for (const bool player : {false, true}) {
        auto driver = make_race_grid_driver(route, data, player ? local_actor : (local_actor + 4) % 16, cap_random);
        driver.set_rival(!player, false);
        AiTrafficContext context;
        context.qualifying = true;
        driver.set_traffic_context(context);
        driver.observe_route(bodies[local_actor], 0, 0);
        expect(driver.route_state().desired_speed_q16 == std::int32_t((player ? target_speed : 160) * 65536)
                   && driver.route_state().alternate_speed_q16 == std::int32_t((player ? alternate_speed : 170) * 65536),
            "4024c0 qualifying speed caps must distinguish permanent player identity from AI ownership");
    }
    const auto free_archive = assets.open_archive(prefix + "free.bfp");
    const auto ground = parse_terrain(free_archive.member(1).bytes());
    const scene::Visibility visibility(ground);
    scene::FleetSceneContext context;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto& scene_actor = context.actors[actor];
        scene_actor.physical =
            scene::initial_root_registration(visibility.data(), {float(initial[actor][0]) / 65536.f, float(initial[actor][1]) / 65536.f, float(initial[actor][2]) / 65536.f});
        scene_actor.model_detail = 2;
        scene_actor.sector_visible = false;
        scene_actor.render_mask = scene_actor.contact_effect_mask = scene_actor.wheel_effect_mask = scene_actor.engine_effect_mask = 0;
    }
    //An invisible geometry/effect context cannot remove full qualifying bodies.
    //The twelve mode4 records remain allocated but never enter physical dispatch.
    for (std::uint32_t tick = 0; tick < 60; ++tick) {
        rivals.set_scene_context(context);
        rivals.update({65536, 65536, false}, local, next, finished, false);
        expect(rivals.integration_order().size() == 4, "The qualifying dispatcher must integrate only its four active actors");
        for (const auto actor : rivals.integration_order()) {
            expect(actor % 4 == local_actor % 4, "A removed qualifying actor must never enter physical dispatch");
        }
        const auto positions = rivals.positions(local.state().body);
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            if (actor % 4 != local_actor % 4) {
                expect(rivals.body_mode(actor) == BodyMode::removed && positions[actor] == initial[actor],
                    "Removed qualifying actors must preserve their generated body positions across physical updates");
            }
            scene::update_root_registration(visibility.data(), context.actors[actor].physical,
                {float(positions[actor][0]) / 65536.f, float(positions[actor][1]) / 65536.f, float(positions[actor][2]) / 65536.f}, 1);
        }
    }
    const auto final = rivals.positions(local.state().body);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (actor % 4 == local_actor % 4) {
            expect(final[actor][0] != initial[actor][0] || final[actor][2] != initial[actor][2],
                "The local qualifying approach and all three physical AI rivals must leave their authored starts");
        }
    }
    //A fresh443f60 restart restores exactly the same four slots without a draw.
    const auto restarted = initialize_qualifying_start(assets, input);
    SourceRandom restarted_random(restarted.repeated.rng_final);
    Rivals again(surface, slots, route, restarted, input.setup.weather, restarted_random, damage, roughness);
    std::array<std::int32_t, 16> retained_distances;
    retained_distances.fill(-1);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (again.body_mode(actor) != BodyMode::removed) {
            retained_distances[actor] = std::int32_t(actor) * 65536;
        }
    }
    again.set_route_distances(retained_distances, 16 * 65536);
    auto invalid_distances = retained_distances;
    invalid_distances[local_actor] = -1;
    bool rejected_active_distance = false;
    try {
        again.set_route_distances(invalid_distances, 16 * 65536);
    } catch (const std::runtime_error&) {
        rejected_active_distance = true;
    }
    expect(rejected_active_distance, "Removed qualifying distances retain-1 while active route distances still require the source bounds");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect(again.body_mode(actor) == startup.repeated.actors[actor].mode, "A qualifying restart must restore all four active and twelve removed actor slots");
    }
    //The earned grid replaces the qualifying owner: all sixteen participate
    //again, using the existing ordinary constructor and its start hold.
    SourceRandom race_random(random.state());
    const auto results = scene::classify_qualifying(data, 90000, race_random);
    const auto race_grid = make_race_grid(data, race_random, &results);
    Rivals race(surface, slots, route, data, input.setup.weather, race_random, race_grid, damage, roughness);
    expect(!race.qualifying(), "The earned race grid must not retain qualifying traffic gates");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (actor != local_actor) {
            expect(race.body_mode(actor) == BodyMode::distant && race.controls(actor).gear == 0,
                "All fifteen race rivals must return from qualifying removal to the ordinary held grid");
        }
    }
}

void warm_grid_consumer(const AssetStore& assets, const ColdGridInitInputs& configuration, const ColdGridInitResult& startup) {
    WarmGridInitInputs input;
    input.configuration = configuration;
    input.configuration.seed = 123; //The live stream, rather than this cold seed, must decide the new grid.
    input.data = startup.data;
    input.points = startup.points;
    const auto store = [](auto& bytes, std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    for (std::size_t actor = 0; actor < input.actors.size(); ++actor) {
        input.actors[actor] = startup.actors[actor].snapshot;
        auto& raw = input.actors[actor];
        //A real finished-controller shape: AI owns every car, while the
        //permanent player's saved keyboard/transmission modes survive.
        store(raw.gameplay, 8, 0);
        store(raw.gameplay, 0x54, 1);
        store(raw.gameplay, 0xc4, 45000 + actor);
        store(raw.dynamics, 0x310, 1);
        store(raw.dynamics, 0x314, 1);
        store(raw.dynamics, 0x318, 0);
        store(raw.dynamics, 0x27c, 0);
        store(raw.gameplay, 0x6c, actor == startup.data.local_actor ? 2 : 0);
        store(raw.dynamics, 0x11c, 0x3210 + actor);
        store(raw.dynamics, 0x128, 0x5432 + actor);
        store(raw.dynamics, 0x12c, 0x2345 + actor);
        store(raw.dynamics, 0x138, 0x1200 + actor);
        store(raw.dynamics, 0x13c, 0x2300 + actor);
        store(raw.dynamics, 0x140, 0x3400 + actor);
        store(raw.dynamics, 0x144, 0x4500 + actor);
        store(raw.dynamics, 0x14c, 0x12345601);
        store(raw.dynamics, 0x2a0, 1);
        store(raw.dynamics, 0x120, 0x6789);
        store(raw.body, 0x268, 0x76543210 + actor);
        raw.damage_levels[0] = 3;
        raw.broken_features[0] = 1;
        raw.has_damage = true;
    }
    std::swap(input.actors[0].gameplay[0x8d], input.actors[1].gameplay[0x8d]);
    std::array<std::uint8_t, 16> bindings;
    for (std::size_t actor = 0; actor < bindings.size(); ++actor) {
        bindings[actor] = input.actors[actor].gameplay[0x8d];
    }
    SourceRandom classification_random(startup.rng_final);
    scene::QualifyingResults classification;
    const auto classified_grid = make_race_grid(input.data, classification_random, nullptr, &classification, &bindings);
    SourceRandom random(startup.rng_final);
    auto expected_seed = random.state();
    for (unsigned draw = 0; draw < 15; ++draw) {
        expected_seed = expected_seed * 214013u + 2531011u;
    }
    const auto warm = initialize_warm_grid(assets, input, random);
    expect(random.state() == expected_seed && warm.rng_final == expected_seed, "Warm restart must classify exactly once on the continuing fifteen-draw stream");
    expect(warm.grid.rank_to_actor == classified_grid.rank_to_actor, "Warm classification must consume retained driver bindings rather than replace them before sorting");
    auto expected_points = input.points;
    expected_points[warm.pole_actor] = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(expected_points[warm.pole_actor]) + 1u));
    expect(warm.points == expected_points && warm.grid.rank(startup.data.local_actor) == 15,
        "Warm classification must retain accumulated points, award one pole and place local last");
    for (std::size_t actor = 0; actor < warm.actors.size(); ++actor) {
        const auto& raw = warm.actors[actor].snapshot;
        const auto& before = input.actors[actor];
        const auto& qualifying = *std::ranges::find(classification.rows, actor, &scene::QualifyingEntry::actor_id);
        expect(source_u16(raw.gameplay, 0xb6) == qualifying.time_ms / 60000 && source_u16(raw.gameplay, 0xb8) == (qualifying.time_ms % 60000) / 1000
                   && source_u16(raw.gameplay, 0xba) == (qualifying.time_ms % 1000) / 10,
            "The newly generated qualifying record must survive401370 into the warm grid");
        for (const auto offset : {0x11c, 0x128, 0x12c, 0x138, 0x13c, 0x140, 0x144}) {
            expect(source_u32(raw.dynamics, offset) == source_u32(before.dynamics, offset), "Warm initializer must retain nonwritten throttle/RPM and raw input history");
        }
        expect(source_u32(raw.gameplay, 0xc) == 1, "460170 must mark the first start-line crossing as backwards before the warm route pass");
        for (std::size_t tail = 0; tail < 4; ++tail) {
            expect(raw.body[0x51b + tail] == 0 && raw.body[0x525 + tail] == 3 - tail,
                "The Donington Y0 loader query must retain its upper support flags and descending list tail after412040");
        }
        expect(source_u32(raw.body, 0x268) == source_u32(before.body, 0x268) && source_u32(raw.body, 0x4e8) == source_u32(before.dynamics, 0x144),
            "Warm placement retains previous position and republishes the inherited raw brake");
        expect(source_u32(raw.gameplay, 0x54) == 0 && source_u32(raw.gameplay, 0xc4) == 0 && source_u32(raw.dynamics, 0x2a0) == 0 && source_u32(raw.dynamics, 0x120) == 0
                   && raw.damage_levels[0] == 0 && raw.broken_features[0] == 0,
            "Warm production writers must reset finish clocks, rollover/filter state and repaired actor damage");
        const bool local = actor == startup.data.local_actor;
        expect(source_u32(raw.gameplay, 8) == std::uint32_t(local) && source_u32(raw.dynamics, 0x27c) == (local ? 2u : 0u)
                   && source_u32(raw.dynamics, 0x14c) == (local ? 0x12345600u : 0x12345601u),
            "Warm handoff restores saved local modes and clears only its queued-shift byte");
        expect(warm.actors[actor].controls.raw_throttle_q16 == source_i32(before.dynamics, 0x140), "Warm typed controls must expose retained raw input, not fresh defaults");
        expect(warm.actors[actor].controls.engine_drive_q16 == source_i32(before.dynamics, 0x12c), "Warm typed controls must retain the generated prior engine drive");
    }
}

struct Transcript {
    std::array<std::array<std::int32_t, 3>, 16> positions{}, velocity{}, angular_velocity{};
    std::array<std::array<std::array<std::int32_t, 3>, 3>, 16> basis{};
    std::array<std::uint32_t, 16> completed{}, next_points{}, rank{}, random_tail{};
    std::uint32_t ticks = 0, contacts = 0, traffic_modes = 0;
    bool operator==(const Transcript&) const = default;
};

void dispatch_worklists(Rivals& rivals, Motion& local, const ColdGridInitResult& startup) {
    expect(startup.data.local_actor == 14, "Dispatch fixture must retain the selected nonzero local actor");
    // Put actor0 immediately ahead of the player so the ordinary selector
    // keeps it full during the physical rollover below. The ring is not ID order.
    rivals.set_order({14, 6, 2, 9, 15, 4, 10, 1, 8, 3, 12, 5, 13, 7, 11, 0});
    rivals.set_car_contacts_enabled(false);
    const auto expect_dispatch = [&] {
        std::vector<std::uint32_t> expected;
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            if (rivals.body_mode(actor) == BodyMode::distant) {
                expected.push_back(actor);
            }
        }
        for (std::uint32_t actor = 16; actor-- > 0;) {
            if (rivals.body_mode(actor) == BodyMode::full) {
                expected.push_back(actor);
            }
        }
        expect(std::ranges::equal(rivals.integration_order(), expected), "Rivals must integrate precisely distant actors ascending, then full actors descending");
    };
    rivals.update({}, local, {}, {}, true);
    expect_dispatch();
    expect(rivals.integration_order().size() == 16 && rivals.body_mode(0) == BodyMode::full && rivals.body_mode(15) == BodyMode::distant,
        "The permuted ring must exercise both full and distant dispatch with a later distant actor");

    std::array<scene::BodyPose, 16> poses;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        poses[actor] = (actor == startup.data.local_actor ? local.state() : rivals.actor(actor)).body;
    }
    auto& overturned = poses[0];
    overturned.position[1] += 5 * 65536;
    for (const auto axis : {1u, 2u}) {
        for (auto& component : overturned.basis[axis]) {
            component = -component;
        }
    }
    scene::refresh_body_contacts(overturned, rivals.car_id(0));
    rivals.prepare_first_race_step(poses, local);
    scene::FleetSceneContext context;
    context.actors[0].sector_visible = false;
    rivals.set_scene_context(context);
    // As in vehicle-rollover, actual roof supports latch the request; no
    // private body modes or synthetic retirement outcome are injected.
    for (unsigned tick = 0; tick < 160 && rivals.body_mode(0) != BodyMode::removed; ++tick) {
        rivals.update({}, local, {}, {}, true);
    }
    expect(rivals.body_mode(0) == BodyMode::removed && rivals.actor(0).boundary == MotionBoundary::none,
        "A physically overturned, invisible rival must retire through the ordinary lifecycle");
    for (unsigned tick = 0; tick < 2; ++tick) {
        rivals.update({}, local, {}, {}, true);
        // Removing actor0 links the player's preceding ring to11,7,13.
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto expected = actor == 0 ? BodyMode::removed : actor == 7 || actor == 11 || actor == 13 || actor == 14 ? BodyMode::full : BodyMode::distant;
            expect(rivals.body_mode(actor) == expected, "The shortened ring must promote the expected surviving neighbors, not its retired tail");
        }
        expect_dispatch();
        expect(rivals.integration_order().size() == 15 && std::ranges::find(rivals.integration_order(), 0u) == rivals.integration_order().end(),
            "The shortened active ring must exclude its retired tail across repeated updates");
    }

    // Import a valid locally published physical state with just the player
    // rollover request changed, exercising415090's early return after other
    // full bodies have already been prepared in the ascending pass.
    auto abort = startup.actors[startup.data.local_actor].snapshot;
    local.publish_source_physical_state(abort);
    abort.dynamics[0x2a0] = 1;
    local.restore_source_snapshot(abort);
    rivals.update({}, local, {}, {}, true);
    std::vector<std::uint32_t> expected_prefix;
    bool prepared_full = false;
    for (std::uint32_t actor = 0; actor < startup.data.local_actor; ++actor) {
        prepared_full |= rivals.body_mode(actor) == BodyMode::full;
        if (rivals.body_mode(actor) == BodyMode::distant) {
            expected_prefix.push_back(actor);
        }
    }
    expect(prepared_full && rivals.body_mode(15) == BodyMode::distant && local.state().rollover.player_righting && !local.state().rollover.upside_down,
        "The player abort must occur after preparing full entries and before the later distant actor");
    expect(!expected_prefix.empty() && std::ranges::equal(rivals.integration_order(), expected_prefix),
        "Player righting must retain only earlier distant integrations, discard every prepared full body and skip later actors");
}

void startup_consumers(const std::filesystem::path& root) {
    const AssetStore assets(root);
    ColdGridInitInputs input;
    input.setup = scene::resolve_setup({.track_id = 0, .car_id = 7, .weather_choice = 1});
    input.seed = 2838336238u;
    const auto generated = initialize_start_grid(assets, input);
    auto expected_seed = input.seed;
    for (unsigned draw = 0; draw < 30; ++draw) {
        expected_seed = expected_seed * 214013u + 2531011u;
    }
    expect(generated.repeated.rng_final == expected_seed, "Two startup classifications must consume exactly thirty source draws");
    const auto& startup = generated.repeated;
    warm_grid_consumer(assets, input, startup);
    expect(startup.grid.rank(startup.data.local_actor) == 15, "Generated NoQual must place the selected local actor last");
    const auto keep = assets.open_archive(std::string(scene::track_content(0).archive_prefix) + "keep.bfp");
    const HpcSurface surface(keep.member(0).bytes());
    const auto route = keep.member(1).bytes();
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());
    const auto initialized_route = initialize_ai_route_heights(route, surface);
    SourceRandom random(startup.rng_final);
    auto local_driver = make_race_grid_driver(initialized_route, startup.data, startup.data.local_actor, random);
    const auto local_route = local_driver.route_state();
    const auto& published = startup.actors[startup.data.local_actor].snapshot.gameplay;
    expect(local_route.cached_point == 0 && local_route.speed_variation_q16 == 0 && local_route.distance_q16 == 0,
        "The local live race driver must consume the same initialized401370 route cache as opponents");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expect(local_route.target[axis] == source_i32(published, 0x108 + axis * 4) && local_route.alternate_target[axis] == source_i32(published, 0x114 + axis * 4),
            "Local route targets must match the generated canonical POINTS initialization");
    }
    expect(local_route.desired_speed_q16 == source_i32(published, 0xd0) && local_route.alternate_speed_q16 == source_i32(published, 0xd4),
        "Local route speed caches must match the generated startup driver");
    const auto traffic = local_driver.traffic_decision_state();
    expect(traffic.passed_actor == 0 && traffic.blocker_actor == 0 && traffic.corners == std::array<std::uint8_t, 2>{2, 3},
        "The local race driver must receive actual401370 traffic defaults");
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    Rivals rivals(surface, slots, route, startup, input.setup.weather, random, damage, roughness);
    expect(random.state() == startup.rng_final, "Live local/opponent construction must continue startup RNG without another draw or classification");
    expect(rivals.grid().rank_to_actor == startup.grid.rank_to_actor, "The live fleet must consume the generated second grid, not classify another one");
    std::array<scene::BodyPose, 16> initial;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect(rivals.body_mode(actor) == startup.actors[actor].mode, "All sixteen previous body modes must come from generated startup");
        if (actor == startup.data.local_actor) {
            continue;
        }
        const auto placement = scene::race_grid_start(slots, startup.grid.rank(actor));
        const auto& body = rivals.actor(actor).body;
        expect(body.position[0] == placement.xz[0] && body.position[2] == placement.xz[1] && std::uint32_t(body.position[1]) != 0x83000000u && body.ground_normal[1] > 0,
            "Every opponent must use its generated grid slot with explicit grounded native placement, not a source PRE sentinel");
        expect(rivals.car_id(actor) == startup.data.drivers[actor].car_id, "Live opponent car bindings must survive the generated data handoff");
        initial[actor] = body;
    }
    const auto local_body = cold_grid_body(surface, slots, startup.grid, startup.data.local_actor, input.setup.selection.car_id);
    Motion local(surface, local_body, input.setup.selection.car_id, input.manual, input.setup.weather, false, damage, roughness);
    local.prepare_ai(std::move(local_driver));
    local.set_start_hold(true);
    std::array<scene::BodyPose, 16> first_step;
    std::array<SourceSnapshot, 16> before_handoff;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        first_step[actor] = startup.actors[actor].body;
        (actor == startup.data.local_actor ? local : rivals.motion(actor)).publish_source_physical_state(before_handoff[actor]);
    }
    const auto local_pedals = local.pedal_filter_state();
    const auto local_steering = local.steering_filter_state();
    const auto handoff_random = random.state();
    const auto handoff_roughness = roughness.current();
    rivals.prepare_first_race_step(first_step, local);
    expect(random.state() == handoff_random && roughness.current() == handoff_roughness, "First scheduled-step preparation must not simulate or consume process RNG/roughness");
    expect(local.pedal_filter_state() == local_pedals && local.steering_filter_state() == local_steering && local.start_held(),
        "Installing the generated physical body must retain prepared local controls and start hold");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& motion = actor == startup.data.local_actor ? local : rivals.motion(actor);
        const auto& body = motion.state().body;
        const auto& expected = first_step[actor];
        expect(body.position == expected.position && body.basis == expected.basis && body.contacts == expected.contacts && body.ground_normal == expected.ground_normal
                   && body.object_to_world == expected.object_to_world,
            "All sixteen first-step bodies must retain generated source pose/contact chronology, including temporary negative heights");
        SourceSnapshot actual;
        motion.publish_source_physical_state(actual);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(source_i32(actual.body, 0x1e4 + axis * 4) == expected.ground_normal[axis], "First-step forces must consume the generated retained contact normal");
        }
        for (std::size_t contact = 0; contact < 8; ++contact) {
            const auto& sample = body.surfaces[contact];
            const auto& expected_sample = expected.surfaces[contact];
            expect(sample.height == expected_sample.height && sample.triangle == expected_sample.triangle && sample.flags == expected_sample.flags
                       && sample.normal_ratio == expected_sample.normal_ratio && actual.dynamics[0x328 + contact] == startup.actors[actor].snapshot.dynamics[0x328 + contact],
                "The generated body handoff must also replace all eight cached HPC samples and material consumers");
        }
        for (std::size_t byte = 0; byte < actual.dynamics.size(); ++byte) {
            if ((byte >= 0x264 && byte < 0x274) || (byte >= 0x328 && byte < 0x330)) {
                continue;
            }
            expect(actual.dynamics[byte] == before_handoff[actor].dynamics[byte], "Body preparation must preserve existing control, wheel and inherited dynamics state");
        }
        expect(motion.ai_traffic_state().human == (actor == startup.data.local_actor) && rivals.body_mode(actor) == BodyMode::distant,
            "Physical preparation must preserve driver ownership and the previous modes for the ordinary selector");
    }
    rivals.set_release_counter(360);
    rivals.update(TimeStep{}, local, {}, {}, true);
    expect(!rivals.integration_order().empty(), "Restart validation must follow actual fleet execution, not only fresh constructors");
    expect(rivals.integration_order().size() == 16, "The first ordinary scheduled update must integrate every actor once");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& motion = actor == startup.data.local_actor ? local : rivals.motion(actor);
        expect(motion.state().boundary == MotionBoundary::none && std::uint32_t(motion.state().body.position[1]) != 0x83000000u,
            "The ordinary first physical/contact/ground pass must resolve every temporary source height without an extra settling tick");
    }
    (void)roughness.next();
    const auto retained_roughness = roughness.current();
    SourceRandom restart_random(startup.rng_final);
    Rivals restarted(surface, slots, route, startup, input.setup.weather, restart_random, damage, roughness);
    expect(restarted.integration_order().empty() && restarted.contacts().empty() && roughness.current() == retained_roughness,
        "Rebuilding the native grid must discard previous fleet/contact work without resetting process roughness");
    expect(restart_random.state() == startup.rng_final, "Rebuilding from the same generated boundary must not draw another grid");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect(restarted.body_mode(actor) == BodyMode::distant, "Restart must restore all previous modes before the next selector");
        if (actor != startup.data.local_actor) {
            const auto& body = restarted.actor(actor).body;
            expect(body.position == initial[actor].position && body.basis == initial[actor].basis && body.contacts == initial[actor].contacts,
                "The same generated restart boundary must restore all fifteen complete grounded poses");
        }
    }
    const auto rejects = [&](const ColdGridInitResult& invalid, std::uint32_t seed) {
        SourceRandom rejected_random(seed);
        bool rejected = false;
        try {
            Rivals ignored(surface, slots, route, invalid, input.setup.weather, rejected_random, damage, roughness);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected && rejected_random.state() == seed, "A stale RNG or noninitial startup history must fail before consuming live state");
    };
    rejects(startup, input.seed);
    auto invalid = startup;
    invalid.release_counter = 1;
    rejects(invalid, startup.rng_final);
    invalid = startup;
    invalid.pair_touching[7] = 1;
    rejects(invalid, startup.rng_final);
    invalid = startup;
    invalid.actors[0].mode = BodyMode::full;
    rejects(invalid, startup.rng_final);
    dispatch_worklists(rivals, local, startup);
}

Transcript canonical(const std::filesystem::path& root, std::uint32_t seed = 1, std::uint32_t lap_limit = 3, std::uint32_t continuation_ticks = 0, std::uint32_t track_id = 0,
    std::uint32_t weather_choice = 1) {
    const AssetStore store(root);
    SourceRandom random(seed);
    const auto setup = scene::resolve_setup({.track_id = track_id, .car_id = 0, .weather_choice = weather_choice}, random);
    const auto data = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
    const auto prefix = std::string(scene::track_content(track_id).archive_prefix);
    const auto keep = store.open_archive(prefix + "keep.bfp");
    const auto route_member = keep.member(1);
    const auto route = route_member.bytes();
    const HpcSurface surface(keep.member(0).bytes());
    const auto free_archive = store.open_archive(prefix + "free.bfp");
    const auto ground = parse_terrain(free_archive.member(1).bytes());
    const scene::BarrierData barriers(route, ground.sector_centers);
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());
    const auto grid = make_race_grid(data, random);
    expect(grid.rank(data.local_actor) == 15, "NoQual must place the actual selected driver last");
    const auto classified = scene::classify_qualifying(data, 90000, random);
    const auto classified_grid = make_race_grid(data, random, &classified);
    expect(classified_grid.rank(data.local_actor) == classified.local_rank, "Classified race must preserve actual qualifying actor order");
    for (std::uint32_t id = 0; id < 16; ++id) {
        expect(grid.rank_to_actor[grid.rank(id)] == id, "Every original actor must own exactly one grid place");
    }
    const auto body = scene::initial_body_pose(surface, scene::race_grid_start(slots, 15), 0);
    SurfaceRoughness roughness;
    DamageContext damage(surface, &barriers);
    Motion local(surface, body, 0, false, setup.weather, false, damage, roughness, &barriers);
    local.set_start_hold(true);
    Rivals rivals(surface, slots, route, data, setup.weather, random, grid, damage, roughness, &barriers);
    const auto initial = rivals.positions(body);
    const scene::Visibility visibility(ground);
    std::array<scene::RootSectorRegistration, 16> registrations;
    std::array<bool, 16> visible;
    visible.fill(true); // Explicit CPU camera: all sectors visible, highest-detail models.
    const std::array<std::uint8_t, 16> detail{};
    const auto root_position = [](const std::array<std::int32_t, 3>& position) {
        //4608b0 publishes the physical parent, independently of camera-hidden LOD children.
        return std::array<float, 3>{float(position[0]) / 65536.f, float(position[1]) / 65536.f, float(position[2]) / 65536.f};
    };
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        registrations[actor] = scene::initial_root_registration(visibility.data(), root_position(initial[actor]));
        scene::update_root_registration(visibility.data(), registrations[actor], root_position(initial[actor]), 1);
    }
    Session local_session(route, false, GameMode::single_race, lap_limit);
    race::RaceProgress progress(route, lap_limit, data.local_actor, grid.rank_to_actor);
    std::array<std::uint32_t, 16> next{};
    std::array<bool, 16> finished{};
    std::array<std::uint32_t, 16> crossed{};
    std::array<std::uint32_t, 16> last_progress{};
    std::uint32_t contact_count = 0, traffic_modes = 0;
    bool published_ai_brake = false;
    Transcript transcript;
    for (std::uint32_t tick = 1; tick <= 12000; ++tick) {
        std::array<std::int32_t, 16> previous_pedals;
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            previous_pedals[actor] = (actor == data.local_actor ? local.state() : rivals.actor(actor)).published_brake_q16;
        }
        const auto positions = rivals.positions(local.state().body);
        (void)local_session.update(tick * 40, local.state().body.position);
        progress.update(tick * 40, local_session, positions);
        const auto new_points = progress.next_points(local_session);
        for (std::size_t actor = 0; actor < 16; ++actor) {
            crossed[actor] += next[actor] != new_points[actor] ? 1u : 0u;
            if (next[actor] != new_points[actor]) {
                last_progress[actor] = tick;
            }
            finished[actor] = progress.finished(std::uint32_t(actor));
        }
        next = new_points;
        const bool held = !local_session.released();
        local.set_start_hold(held);
        rivals.set_release_counter(local_session.release_counter());
        rivals.set_order(progress.spatial_order());
        rivals.set_route_distances(progress.route_distances(), progress.total_route_distance());
        //42bc70 consumes the retained registration from the preceding scene update.
        scene::FleetSceneContext context;
        for (std::size_t actor = 0; actor < context.actors.size(); ++actor) {
            context.actors[actor].physical = registrations[actor];
            context.actors[actor].sector_visible = visible[actor];
            context.actors[actor].model_detail = detail[actor];
            context.actors[actor].contact_effect_mask = visible[actor] ? 1 : 0;
        }
        rivals.set_scene_context(context);
        rivals.update({}, local, next, finished, held);
        damage.advance(); //415e10 advances the one shared detached-part world once.
        std::array<bool, 16> previous_lamps;
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto& motion = actor == data.local_actor ? local : rivals.motion(actor);
            expect(motion.state().published_brake_q16 == previous_pedals[actor], "Driver and physics updates must retain the previous frame's published brake pedal");
            previous_lamps[actor] = motion.brake_lights();
        }
        //The source publication is independent of visibility, body mode and
        //human ownership; the scene's local Motion must participate too.
        auto hidden = context;
        for (auto& actor : hidden.actors) actor.sector_visible = false;
        rivals.set_scene_context(hidden);
        const auto publication_rng = random.state();
        rivals.publish_frame_brake_pedals(local);
        rivals.set_scene_context(context);
        expect(random.state() == publication_rng, "4608b0 brake publication must not consume the shared source RNG");
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto& motion = actor == data.local_actor ? local : rivals.motion(actor);
            expect(motion.state().published_brake_q16 == motion.controls().raw_brake_q16,
                "The complete frame pass must publish every local/AI actor's retained raw brake even when hidden or distant");
            expect(motion.brake_lights() == previous_lamps[actor], "Frame pedal publication must preserve the separate human/AI lamp outputs");
            published_ai_brake = published_ai_brake || (actor != data.local_actor && motion.state().published_brake_q16 != 0);
        }
        const auto published_positions = rivals.positions(local.state().body);
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto mask = rivals.body_mode(actor) == BodyMode::removed ? 0u : 1u;
            scene::update_root_registration(visibility.data(), registrations[actor], root_position(published_positions[actor]), std::uint8_t(mask));
        }
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto& state = actor == data.local_actor ? local.state() : rivals.actor(actor);
            if (state.boundary != MotionBoundary::none) {
                const auto gear = actor == data.local_actor ? local.controls().gear : rivals.controls(actor).gear;
                throw std::runtime_error("Rival motion boundary: track " + std::to_string(track_id) + " weather " + std::to_string(setup.weather) + " seed " + std::to_string(seed)
                                         + " actor " + std::to_string(actor) + " tick " + std::to_string(tick) + " next " + std::to_string(next[actor]) + " laps "
                                         + std::to_string(progress.completed_laps(actor)) + " gates " + std::to_string(crossed[actor]) + " stalled_ticks "
                                         + std::to_string(tick - last_progress[actor]) + " boundary " + std::to_string(int(state.boundary)) + " position "
                                         + std::to_string(state.body.position[0]) + "," + std::to_string(state.body.position[1]) + "," + std::to_string(state.body.position[2])
                                         + " gear " + std::to_string(gear) + " finished " + std::to_string(finished[actor]) + " distant " + std::to_string(rivals.distant(actor)));
            }
            if (actor != data.local_actor) {
                traffic_modes += rivals.traffic_mode(actor) != 1 ? 1u : 0u;
            }
        }
        contact_count += std::uint32_t(rivals.contacts().size());
        if (tick == 100) {
            for (std::uint32_t actor = 0; actor < 16; ++actor) {
                const auto held_position = rivals.positions(local.state().body)[actor];
                expect(held_position[0] == initial[actor][0] && held_position[2] == initial[actor][2], "Grid hold must preserve all actual starting XZ placements");
            }
        }
        transcript.ticks = tick;
        if (std::count(finished.begin(), finished.end(), true) == 15 && tick >= continuation_ticks) {
            std::cout << "All fifteen rivals finished " << lap_limit << " physical laps, track " << track_id << " weather " << setup.weather << " seed " << seed << ", at tick "
                      << tick << '\n';
            break;
        }
    }
    std::uint32_t moving = 0, full_laps = 0;
    const auto final = rivals.positions(local.state().body);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (actor == data.local_actor) {
            continue;
        }
        moving += final[actor] != initial[actor] ? 1u : 0u;
        full_laps += progress.completed_laps(actor) >= lap_limit ? 1u : 0u;
        expect(rivals.actor(actor).boundary == MotionBoundary::none, "Finished race bodies must stay in original HPC coverage");
        std::cout << "actor " << actor << " car " << rivals.car_id(actor) << " gates " << crossed[actor] << " laps " << progress.completed_laps(actor) << " boundary "
                  << int(rivals.actor(actor).boundary) << " next " << next[actor] << " stalled_ticks " << transcript.ticks - last_progress[actor] << " speed "
                  << rivals.actor(actor).speed_integer << " gear " << int(rivals.controls(actor).gear) << " distant " << rivals.distant(actor) << " contacts "
                  << rivals.actor(actor).contact_count << " recovery " << rivals.actor(actor).barrier_recovery.active << '\n';
    }
    expect(full_laps == 15, "All fifteen physical rival bodies must complete their configured ordered actual laps");
    expect(moving == 15, "All fifteen source driver bodies must physically leave the grid");
    transcript.positions = final;
    transcript.next_points = progress.next_points(local_session);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& state = actor == data.local_actor ? local.state() : rivals.actor(actor);
        transcript.basis[actor] = state.body.basis;
        transcript.velocity[actor] = state.velocity_q16;
        transcript.angular_velocity[actor] = state.angular_velocity_q16;
        transcript.completed[actor] = progress.completed_laps(actor);
        transcript.rank[actor] = progress.position(actor);
        transcript.random_tail[actor] = random.next();
    }
    transcript.contacts = contact_count;
    transcript.traffic_modes = traffic_modes;
    expect(contact_count > 0 && traffic_modes > 0, "The actual grid must exercise car contacts and traffic modes");
    expect(published_ai_brake, "The actual fleet must positively publish a nonlocal AI brake pedal through the shared frame consumer");
    std::cout << "rivals moved " << moving << " completed physical laps " << full_laps << " contacts " << contact_count << " traffic " << traffic_modes << '\n';
    return transcript;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 5) {
            throw std::runtime_error("Usage: rivals-tests canonical-root [track-id [weather-choice [seed]]]");
        }
        startup_consumers(argv[1]);
        qualifying_startup_consumers(argv[1]);
        qualifying_fleet_consumers(argv[1]);
        if (argc >= 3) {
            const auto number = [](const char* text) {
                std::uint32_t value = 0;
                const auto end = text + std::char_traits<char>::length(text);
                const auto parsed = std::from_chars(text, end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != end) {
                    throw std::runtime_error("Fleet arguments must be unsigned integers");
                }
                return value;
            };
            const auto track = number(argv[2]), weather = argc >= 4 ? number(argv[3]) : 1u, seed = argc >= 5 ? number(argv[4]) : 1u;
            (void)canonical(argv[1], seed, 1, 0, track, weather);
            std::cout << "PASS: actual sixteen-car fleet, selected track and original weather\n";
            return 0;
        }
        const auto first = canonical(argv[1]);
        const auto repeat = canonical(argv[1]);
        expect(first == repeat, "Repeated source seed and40ms updates must preserve all16 poses, velocities, progress, finish tick and RNG tail");
        for (const auto seed : {2u, 7u, 896116572u}) {
            (void)canonical(argv[1], seed, 1, 6500);
        }
        std::cout << "PASS: deterministic three-lap fleet and three seeds of finished one-lap traffic through260s\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
