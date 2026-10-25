#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "scene/qualifying.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/controls.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/rivals.hpp"
#include "simulation/session.hpp"
#include "simulation/source_race_progress.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;
using Bytes = std::vector<std::uint8_t>;
constexpr auto one = fixed::one;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void put32(std::span<std::uint8_t> bytes, std::size_t offset, std::int32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::uint8_t(std::uint32_t(value) >> (i * 8));
    }
}

Bytes route(bool left) {
    Bytes bytes(4 + 3 * 64);
    put32(bytes, 0, 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto base = 4 + i * 64;
        put32(bytes, base, 20 * one);
        put32(bytes, base + 8, 10 * one);
        put32(bytes, base + 12, 20 * one);
        put32(bytes, base + 20, (left ? 10 : -10) * one);
        put32(bytes, base + 36, 20 * one);
        put32(bytes, base + 44, -10 * one);
        put32(bytes, base + 52, 100);
    }
    return bytes;
}

void synthetic() {
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    std::array<std::uint8_t, 40> record{};
    put32(record, 28, 0x8000);
    std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(1);
    AiDriver left(route(true), record, distributions, 0, 0x1eb8, random);
    left.observe(body, 0, 0);
    const auto axes = left.axes(body, 0, 1);
    expect(axes.left_q16 > 0 && axes.right_q16 == 0 && axes.throttle_q16 == 0x8000 && axes.brake_q16 == 0,
        "Actual source route target must produce analog left steering and AIBIN throttle");
    AiDriver right(route(false), record, distributions, 0, 0x1eb8, random);
    right.observe(body, 0, 0);
    const auto opposite = right.axes(body, 0, 1);
    expect(opposite.right_q16 > 0 && opposite.left_q16 == 0, "Mirrored route target must reverse the steering sign");
    AiDriver repeated_handoff(route(true), record, distributions, 0, 0, random);
    const auto before_handoff_rng = random.state();
    repeated_handoff.handoff(7, false);
    repeated_handoff.handoff(1, true);
    expect(repeated_handoff.traffic_state(body, 0, 0).reversing && random.state() == before_handoff_rng,
        "402be0's repeated ownership handoff must retain an existing reverse flag without route refresh or RNG consumption");
    SourceRandom normal_random(1), impossible_random(1);
    AiDriver normal_speed(route(true), record, distributions, 0, 0, normal_random);
    AiDriver impossible_speed(route(true), record, distributions, 0, 0x3333, impossible_random, true);
    normal_speed.observe(body, 103 * one, 0);
    impossible_speed.observe(body, 103 * one, 0);
    expect(
        normal_speed.axes(body, 103 * one, 1).throttle_q16 == 0 && impossible_speed.axes(body, 103 * one, 1).throttle_q16 == 0x8000 && impossible_speed.tire_offset_q16() == 0x3333,
        "Impossible must tune original integer POINTS speeds before Q16 conversion and retain its AI tire offset");
    auto twin_speeds = route(true);
    for (std::size_t point = 0; point < 3; ++point) {
        put32(twin_speeds, 4 + point * 64 + 56, 120);
    }
    AiDriver impossible_lines(twin_speeds, record, distributions, 0, 0x3333, impossible_random, true);
    impossible_lines.observe(body, 0, 0);
    expect(impossible_lines.route_state().desired_speed_q16 == 104 * one && impossible_lines.route_state().alternate_speed_q16 == 125 * one,
        "CMIMPOSS must increase both original route-line speed fields with integer truncation");
    impossible_lines.observe(body, 0, 2);
    expect(impossible_lines.route_state().desired_speed_q16 == 100 * one && impossible_lines.route_state().alternate_speed_q16 == 120 * one,
        "CMIMPOSS must leave the original loop's point0 speeds unchanged on both lines");
    expect(left.axes(body, 400 * one, 1).throttle_q16 == 0 && left.axes(body, 400 * one, 1).brake_q16 == 0,
        "Finished local AI coasts above route speed, without inventing an emergency brake");
    auto straight = route(true);
    for (std::size_t point = 0; point < 3; ++point) {
        put32(straight, 4 + point * 64 + 20, 0);
    }
    AiDriver rollout(straight, record, distributions, 0, 0, random);
    rollout.handoff(1, false);
    rollout.observe(body, 170 * one, 0);
    const auto slowing = rollout.axes(body, 170 * one, 1);
    expect(slowing.throttle_q16 == 0 && slowing.brake_q16 == one, "Initial rollout must retain4051e0's stopping-distance brake before a slower route target");
    auto distant_body = body;
    distant_body.position[0] = -200 * one;
    rollout.observe(distant_body, 170 * one, 0);
    const auto approaching = rollout.axes(distant_body, 170 * one, 1);
    expect(approaching.throttle_q16 == 0x8000 && approaching.brake_q16 == 0, "The source braking-distance threshold must preserve gas until the slower target is near enough");
    auto oblique = straight;
    for (std::size_t point = 0; point < 3; ++point) {
        const auto base = 4 + point * 64;
        put32(oblique, base + 12, 30 * one);
        put32(oblique, base + 36, 40 * one);
        put32(oblique, base + 44, 10 * one);
    }
    put32(oblique, 4 + 64 + 36, 20 * one);
    put32(oblique, 4 + 64 + 44, -10 * one);
    AiDriver projected(oblique, record, distributions, 0, 0, random);
    projected.observe(body, 80 * one, 0);
    expect(projected.target_point() == 2, "403dd0 must project distance against4218e0's route normal before choosing the fast two-point lookahead");
    projected.observe(body, 80 * one, 1);
    expect(projected.target_point() == 0, "The route crossing update must retain4218e0's pre-advance normal while selecting the new lookahead point");
    projected.observe(body, 80 * one, 1);
    expect(projected.target_point() == 2, "The following update must use its current boundary normal without delaying an extra route point");

    auto changed = route(true);
    put32(changed, 4 + 20, 0);
    AiDriver cached(changed, record, distributions, 0, 0, random);
    cached.observe(body, 0, 0);
    const auto old_angle = cached.steering_error();
    cached.observe(body, 0, 0);
    expect(old_angle < 10 && cached.steering_error() > 100, "403dd0 one-ahead refresh must retain the old main-line angle until the following update");

    Controls controls(0, true);
    controls.handoff_to_ai(0x1234);
    const DrivetrainInputs drivetrain{.speed_q16 = one};
    controls.begin_ai_update({.left_q16 = 0x8000, .throttle_q16 = 0x8000}, {}, drivetrain);
    expect(controls.state().steering_angle == -199 && controls.state().throttle_q16 == fixed::mul(0x68f5, 0x8000),
        "402be0 must select direct4190f0 steering and direct418cd0 gas, bypassing keyboard ramps");
    expect(controls.state().tire_offsets_q16[7] == 0x1234, "Handoff must apply412890 to all eight tire parameters");
    controls.begin_ai_update({.brake_q16 = one}, {}, {});
    expect(controls.state().gear == 1 && !controls.state().reverse_pedal && controls.state().brake_q16 > 0 && controls.state().brake_q16 < 0x9999,
        "AI mode0 retains filtered brake and must not select reverse from the human brake pedal convention");

    for (std::size_t i = 21; i < 25; ++i) {
        record[i] = 1;
    }
    for (auto& distribution : distributions) {
        distribution = {0};
    }
    SourceRandom shared(19), expected(19);
    AiDriver draws(route(true), record, distributions, 0, 0, shared);
    draws.observe(body, 0, 0);
    draws.observe(body, 0, 0);
    for (int i = 0; i < 4; ++i) {
        (void)expected.next();
    }
    expect(shared.next() == expected.next(), "Target refresh must consume all four source distributions once and share the caller RNG stream");
    bool rejected = false;
    try {
        draws.observe(body, 0, 3);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Route target lookup must reject an invalid caller index");
}

void source_player_gap() {
    //Actual storm64 update32: the ring changes actor2's ahead3 to
    //player14.404b07 retains actor15's body pointer from404811.
    const fixed::Vector last_active{-16172121, 328091, -4069091};
    const fixed::Vector self{-17083786, 314746, -3947761};
    const fixed::Vector player{-10261176, 375937, -5191391};
    const auto gap = source_forward_gap(0, 20 * one, 1000 * one, last_active, player, true);
    expect(gap == 6017152 && gap != source_forward_gap(0, 20 * one, 1000 * one, self, player, true),
        "404b07 player gap must preserve the last route actor's physical pointer after the spatial ring changes");
    expect(
        source_forward_gap(0, 20 * one, 1000 * one, last_active, player, false) == 20 * one && source_forward_gap(0, 100 * one, 1000 * one, last_active, player, true) == 100 * one,
        "Only a player ahead with a route gap strictly below100 may replace the old-ahead route distance");
    expect(source_forward_gap(0, 20 * one, 1000 * one, last_active, player, true, true) == 1000 * one,
        "The source self-ring override must follow the short player-distance calculation");
}

void source_race_grid() {
    auto points = route(true);
    put32(points, 4 + 24, 30 * one);
    put32(points, 4 + 32, -10 * one);
    put32(points, 4 + 56, 120);
    std::array<std::uint8_t, 40> record{};
    record[23] = record[24] = 1;
    const std::array<std::vector<std::int16_t>, 3> distributions{{{0}, {0}, {1}}};
    SourceRandom random(91);
    AiDriver driver(points, record, distributions, 0, 0x1eb8, random);
    const auto initial_rng = random.state();
    driver.initialize_race_grid();
    const auto state = driver.route_state();
    const auto traffic = driver.traffic_decision_state();
    const auto mapped = driver.mapped_control_state();
    expect(state.cached_point == 0 && state.desired_speed_q16 == 100 * one && state.alternate_speed_q16 == 120 * one && state.target == fixed::Vector{20 * one, 0, 10 * one}
               && state.alternate_target == fixed::Vector{30 * one, 0, -10 * one} && state.speed_variation_q16 == 0 && traffic.mode == 1 && !traffic.passing && !traffic.blocked
               && traffic.corners == std::array<std::uint8_t, 2>{2, 3} && mapped.gear == 0 && !mapped.reversing && mapped.reaction_ticks == 0 && mapped.axes.left_q16 == 0
               && mapped.axes.right_q16 == 0 && mapped.axes.throttle_q16 == 0 && mapped.axes.brake_q16 == 0 && driver.tire_offset_q16() == 0x1eb8 && random.state() == initial_rng,
        "401370 must initialize route-zero race caches and traffic defaults without target sampling or CRT draws");
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    driver.observe_route(body, 0, 2);
    expect(random.state() == initial_rng, "The first race observation must retain the initialized route-zero target until the source target index changes");
    SourceRandom expected(initial_rng);
    expected.next();
    expected.next();
    driver.observe_route(body, 0, 0);
    expect(driver.route_state().cached_point == 1 && random.state() == expected.state(),
        "A changed race lookahead must sample the authored driver distributions on the same retained CRT stream");
}

void source_route_cache() {
    Bytes points(4 + 40 * 64);
    put32(points, 0, 40);
    for (std::size_t point = 0; point < 40; ++point) {
        const auto at = 4 + point * 64;
        put32(points, at, 20 * one);
        put32(points, at + 8, 10 * one);
        put32(points, at + 12, 20 * one);
        put32(points, at + 24, 20 * one);
        put32(points, at + 32, 30 * one);
        put32(points, at + 36, 20 * one);
        put32(points, at + 44, -10 * one);
        put32(points, at + 48, fixed::bits(0x80000000));
        put32(points, at + 52, 200);
        put32(points, at + 56, 220);
        put32(points, at + 60, fixed::bits(0xffff0000));
    }
    put32(points, 4 + 39 * 64, -20 * one);
    put32(points, 4 + 39 * 64 + 36, -20 * one);
    std::array<std::uint8_t, 40> record{};
    put32(record, 28, 0x8000);
    record[19] = 8;
    for (std::size_t count = 21; count <= 24; ++count) {
        record[count] = 1;
    }
    const std::array<std::vector<std::int16_t>, 3> distributions{{{-2}, {3}, {4}}};
    SourceRandom random(7);
    AiDriver driver(points, record, distributions, 0, 0, random, false, AiRouteEncoding::source_initialized);
    auto cadence = driver.advance_source_route_cadence(7);
    expect(cadence.ticks == 8 && !cadence.refresh, "403dd0 must increment the route counter below the resolved signed-byte period");
    cadence = driver.advance_source_route_cadence(cadence.ticks);
    expect(cadence.ticks == 0 && cadence.refresh, "403dd0 must request route-heading refresh at period equality and reset the counter");
    cadence = driver.advance_source_route_cadence(cadence.ticks);
    expect(cadence.ticks == 1 && !cadence.refresh, "A previous tick's route refresh must clear before the next target/projection pass");
    AiRouteState warm;
    warm.cached_point = 1;
    std::array<std::uint8_t, 0x124> gameplay{};
    put32(gameplay, 4, 1); //Active record is independent of selected route line.
    gameplay[0x81] = 2;
    warm.traffic_mode = source_ai_route_mode(gameplay);
    warm.target = {20 * one, 0, 0};
    warm.alternate_target = {20 * one, 0, 30 * one};
    warm.desired_speed_q16 = 123 * one;
    warm.alternate_speed_q16 = 147 * one;
    warm.speed_variation_q16 = 99;
    {
        AiDriver persistent(points, record, distributions, 0, 0, random, false, AiRouteEncoding::source_initialized);
        auto retained_warm = warm;
        retained_warm.traffic_mode = 1;
        retained_warm.route_angle = 400;
        persistent.restore_source_route(retained_warm);
        persistent.initialize_source_route_cadence(7);
        scene::BodyPose heading_body;
        heading_body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
        const auto stream = random.state();
        persistent.observe_route(heading_body, 0, 0);
        expect(persistent.route_state().route_angle == 400 && random.state() == stream,
            "Native observation must retain its preceding route heading while the source cadence increments below equality");
        persistent.observe_route(heading_body, 0, 0);
        expect(persistent.route_state().route_angle == persistent.source_route_angle(heading_body, 0) && random.state() == stream,
            "The same retained driver must refresh route heading at cadence equality without resampling an unchanged target");
        AiMappedControlState controls;
        controls.axes.left_q16 = one;
        controls.reversing = true;
        controls.reaction_ticks = 7;
        controls.reverse_ticks = 3;
        controls.brake_ticks = 5;
        controls.brake_lights = true;
        persistent.restore_source_controls(controls);
        persistent.synchronize_source_physical_controls(2, 1);
        persistent.apply_source_motion_steering_gate(-0xccb, true);
        const auto synced = persistent.mapped_control_state();
        expect(synced.gear == 2 && synced.reverse_pedal == 1 && synced.axes.left_q16 == 0 && synced.reversing && synced.reaction_ticks == 7 && synced.reverse_ticks == 3
                   && synced.brake_ticks == 5 && synced.brake_lights,
            "Physical gearbox synchronization and40b360's signed-speed steering clear must preserve all retained driver-owned counters and lamp/reverse latches");
        persistent.retain_control_axes(controls.axes);
        persistent.apply_source_motion_steering_gate(-0xccc, true);
        persistent.apply_source_motion_steering_gate(0, false);
        expect(persistent.mapped_control_state().axes.left_q16 == one, "Exact moving threshold and distant-body physical dispatch must preserve retained mapped steering");
    }
    driver.restore_source_route(warm);
    AiRouteContext context;
    context.end_bound = 32;
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    expect(driver.advance_source_route_progress(body, {{0, 39}}, false).crossing == RouteCrossing::none,
        "The independent route-cache slice must accept a body between its retained current and previous gates");
    driver.observe_source_route(body, 0, 0, 0, context);
    const auto retained = driver.route_state();
    expect(retained.cached_point == 1 && retained.target == warm.target && retained.alternate_target == warm.alternate_target && random.state() == 7
               && retained.desired_speed_q16 == warm.desired_speed_q16 && retained.speed_variation_q16 == warm.speed_variation_q16,
        "Warm source cache hits must preserve sampled targets/speeds and consume no RNG");
    expect(retained.distance_q16 == fixed::length({20 * one, 0, 30 * one}),
        "403dd0 must decode game+81 independently of active+4 and measure the alternate POINTS line before projection");
    body.position[0] = 21 * one;
    expect(driver.advance_source_route_progress(body, {{0, 39}}, false).crossing == RouteCrossing::forward,
        "The independent cache comparison must identify a real forward route crossing before claiming parity");
    body.position[0] = 0;
    warm.cached_point = 0;
    warm.traffic_mode = 1;
    driver.restore_source_route(warm);
    context.release_counter = 1;
    context.finished = true;
    context.swap_start_lines = true;
    context.start_primary[1] = {7 * one, 8 * one, 9 * one};
    context.start_alternate[1] = {-7 * one, -8 * one, -9 * one};
    driver.observe_source_route(body, 0, 0, 0, context);
    SourceRandom expected(7);
    for (unsigned draw = 0; draw < 4; ++draw) {
        expected.next();
    }
    const auto sampled = driver.route_state();
    const auto offset = fixed::mul(4 * one, 0x28f);
    expect(random.state() == expected.state() && sampled.speed_variation_q16 == fixed::mul(5 * one, 0x28f) && sampled.desired_speed_q16 == 160 * one
               && sampled.alternate_speed_q16 == 170 * one,
        "4024c0 must use the shared full-body four-draw order and source finished-nonplayer speed cap");
    expect(sampled.target == fixed::Vector{fixed::add(-7 * one, fixed::mul(0x8000, offset)), -8 * one, fixed::add(-9 * one, fixed::mul(-1, offset))}
               && sampled.alternate_target == fixed::Vector{fixed::add(7 * one, fixed::mul(0x8000, offset)), 8 * one, fixed::add(9 * one, fixed::mul(-1, offset))},
        "Initialized start-line swap and packed highword lateral metadata must drive both targets, including positive8000 equality");
    warm.cached_point = 0;
    driver.restore_source_route(warm);
    context.full_body = false;
    context.release_counter = 0;
    context.player = true;
    driver.observe_source_route(body, 0, 0, 0, context);
    expected.next();
    expected.next();
    expect(random.state() == expected.state() && driver.route_state().speed_variation_q16 == 0 && driver.route_state().desired_speed_q16 == 200 * one,
        "Non-full source bodies consume only the two lateral draws; a permanent player retains uncapped live route speeds");
    //403dd0 tests signed game+cc against70, so even fast reverse travel uses
    //one-ahead. At exactly70 while near its target it selects two.
    warm.cached_point = 0;
    warm.traffic_mode = 1;
    body.position[0] = 10 * one;
    for (const auto speed : {-100, 0, 69, 70, 100}) {
        driver.restore_source_route(warm);
        driver.observe_source_route(body, speed * one, 0, 0, context);
        expect(driver.target_point() == (speed < 70 ? 1u : 2u), "Signed reverse/zero/forward route speeds must preserve the original70 threshold without taking a magnitude");
    }
    driver.restore_source_route(warm);
    driver.observe_route(body, -100 * one, 0);
    expect(driver.target_point() == 1, "The shared production route observer must accept bounded negative source speed");
}

void source_route_progress() {
    Bytes points(4 + 80 * 64);
    put32(points, 0, 80);
    for (std::size_t point = 0; point < 80; ++point) {
        const auto at = 4 + point * 64;
        put32(points, at, 200 * one);
        put32(points, at + 8, 10 * one);
        put32(points, at + 36, 200 * one);
        put32(points, at + 44, -10 * one);
        put32(points, at + 12, 4000 * one);
        put32(points, at + 20, 4000 * one);
    }
    const auto target = [&](std::size_t point, std::int32_t x) {
        put32(points, 4 + point * 64 + 12, x);
        put32(points, 4 + point * 64 + 20, 0);
    };
    target(0, 0);
    target(22, one); //Excluded inside the forward23-point window.
    target(23, 100 * one + 3 * one / 4);
    target(24, 100 * one + one / 4); //Same integer setup distance: first wins.
    target(56, 100 * one + one / 4);
    target(57, one / 2); //401860 excludes the wrapped -23 endpoint too.
    put32(points, 4 + 79 * 64, -200 * one);
    put32(points, 4 + 79 * 64 + 36, -200 * one);
    std::array<std::uint8_t, 40> record{};
    std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(1);
    AiDriver driver(points, record, distributions, 0, 0, random, false, AiRouteEncoding::source_initialized);
    scene::BodyPose body;
    body.position[0] = 100 * one + 3 * one / 4;
    AiRouteProgressInput input;
    input.cursor = {0, 79};
    input.human = true;
    auto progress = driver.advance_source_route_progress(body, input, true);
    expect(progress.recovered && progress.cursor.next == 23 && progress.cursor.previous == 22 && progress.target_cursor.next == 0,
        "4019f0's exact exclusion, shifted integer distances and ascending tie-break must feed403dd0 recovery while retaining the earlier lookahead cursor");
    progress = driver.advance_source_route_progress(body, input, false);
    expect(!progress.recovered && progress.cursor.next == 0, "Human nearest-route recovery must execute only on the current route-refresh request");
    body.position[0] = 50 * one + 3 * one / 4;
    progress = driver.advance_source_route_progress(body, input, true);
    expect(!progress.recovered && progress.cursor.next == 0, "Equal shifted integer squared distances must preserve the current cursor rather than take the precomputed candidate");
    body.position[0] = 50 * one + 7 * one / 8;
    progress = driver.advance_source_route_progress(body, input, true);
    expect(!progress.recovered, "Nearest-route selection must shift each coordinate difference before squaring, retaining sub-unit ties");

    input.human = false;
    input.cursor = {0, 0};
    body.position[0] = 0;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.crossing == RouteCrossing::backward && progress.cursor.next == 0 && progress.cursor.previous == 79 && progress.backwards && progress.current_gate == 1
               && progress.previous_gate == 1,
        "403dd0 initial rear-gate correction must wrap previous alone and set the start/backwards flag");
    input.test_previous = false;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.crossing == RouteCrossing::none && progress.previous_gate == 2, "The source ordinary-AI rear-gate bypass must publish side2 and preserve its cursor");
    input.test_previous = true;
    input.cursor = {79, 78};
    body.position[0] = 201 * one;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.crossing == RouteCrossing::forward && progress.cursor.next == 0 && progress.cursor.previous == 79,
        "Forward cursor progression must wrap once without consuming additional passed gates");
    input.cursor = {0, 79};
    input.backwards = true;
    input.checkpoint = 0;
    input.checkpoint_marker = 4;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(!progress.backwards && !progress.checkpoint_side_effect && progress.cursor.next == 1 && progress.checkpoint == 0 && progress.checkpoint_marker == 4,
        "A first forward start crossing must clear backwards before checkpoint processing and preserve retained checkpoint markers");
    input.backwards = false;
    input.checkpoint = 1;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.checkpoint == 1 && progress.checkpoint_marker == 2 && !progress.checkpoint_side_effect,
        "An unmatched forward start crossing must restore the original first-checkpoint marker");
    input.cursor = {13, 12};
    input.checkpoint_marker = progress.checkpoint_marker; //Retain the preceding start reset.
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.checkpoint == 2 && progress.checkpoint_marker == 2 && !progress.checkpoint_side_effect,
        "A checkpoint with no matching split marker must advance its index without inventing a timer call");
    input.cursor = {26, 25};
    input.checkpoint = 2;
    progress = driver.advance_source_route_progress(body, input, false);
    expect(progress.checkpoint_side_effect && progress.split_event && progress.split_sector == 1 && !progress.lap_event && progress.checkpoint_marker == 4,
        "An actual matching checkpoint must expose its lap/sector timer boundary rather than silently claim complete race-state parity");
}

void traffic() {
    auto points = route(true);
    for (std::size_t point = 0; point < 3; ++point) {
        const auto base = 4 + point * 64;
        put32(points, base, 100 * one);
        put32(points, base + 8, 20 * one);
        put32(points, base + 12, 100 * one);
        put32(points, base + 20, 0);
        put32(points, base + 24, 100 * one);
        put32(points, base + 32, 8 * one);
        put32(points, base + 36, 100 * one);
        put32(points, base + 44, -20 * one);
        put32(points, base + 52, 120);
        put32(points, base + 56, 130);
    }
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    scene::refresh_body_contacts(body, 0);
    std::array<std::uint8_t, 40> record{};
    put32(record, 28, one);
    std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(1);
    AiDriver driver(points, record, distributions, 0, 0, random);
    driver.set_rival(true, false);
    std::array<AiTraffic, 4> fleet;
    for (std::uint32_t actor = 0; actor < fleet.size(); ++actor) {
        fleet[actor].actor_id = actor;
        fleet[actor].ahead_id = (actor + 1) % fleet.size();
        fleet[actor].behind_id = (actor + fleet.size() - 1) % fleet.size();
        fleet[actor].body = body;
        fleet[actor].body.position[0] = std::int32_t(actor) * 20 * one;
        scene::refresh_body_contacts(fleet[actor].body, 0);
        fleet[actor].route_distance_q16 = std::int32_t(actor) * 20 * one;
        fleet[actor].forward_gap_q16 = 20 * one;
    }
    fleet[0].speed_q16 = 80 * one;
    fleet[1].speed_q16 = 20 * one;
    driver.observe_route(body, fleet[0].speed_q16, 0);
    driver.decide_traffic(body, fleet[0].speed_q16, fleet[0], fleet, 1000 * one);
    auto decision = driver.traffic_state(body, fleet[0].speed_q16, 0);
    expect(decision.blocked && decision.blocker_id == 1, "4036f0 must record an actual slow blocking actor inside200 units");
    expect(driver.axes(body, fleet[0].speed_q16, 1).throttle_q16 == 0, "4051e0 must apply the source60 speed cap to a blocked finished actor");

    expect(decision.passing, "4036f0 must select a real corridor or corner passing maneuver for the slower adjacent actor");
    driver.observe_route(body, fleet[0].speed_q16, 0);
    driver.decide_traffic(body, fleet[0].speed_q16, fleet[0], fleet, 1000 * one);
    expect(!driver.traffic_state(body, fleet[0].speed_q16, 0).passing && driver.traffic_mode() == 1, "4036f0 must cancel an active maneuver for a finished actor above60");

    AiDriver passing_driver(points, record, distributions, 0, 0, random);
    passing_driver.set_rival(true, false);
    passing_driver.handoff(1, false);
    auto passing_fleet = fleet;
    passing_fleet[1].body.position[2] = 4 * one;
    scene::refresh_body_contacts(passing_fleet[1].body, 0);
    passing_driver.observe_route(body, 100 * one, 0);
    passing_driver.decide_traffic(body, 100 * one, passing_fleet[0], passing_fleet, 1000 * one);
    expect(passing_driver.traffic_state(body, 100 * one, 0).passing, "An unfinished actor must retain its selected passing identity");
    const auto selected_mode = passing_driver.traffic_mode();
    passing_fleet[0].forward_gap_q16 = 200 * one;
    passing_driver.observe_route(body, 100 * one, 0);
    passing_driver.decide_traffic(body, 100 * one, passing_fleet[0], passing_fleet, 1000 * one);
    expect(passing_driver.traffic_state(body, 100 * one, 0).passing && passing_driver.traffic_mode() == selected_mode,
        "403490/4036f0 must retain cached corridor decisions and the selected mode outside the stopping gap");
    const auto retained_passing = passing_driver.traffic_decision_state();
    passing_fleet[0].ahead_id = 3;
    passing_driver.decide_traffic(body, 100 * one, passing_fleet[0], passing_fleet, 1000 * one);
    expect(!passing_driver.traffic_state(body, 100 * one, 0).passing && passing_driver.traffic_mode() == 1,
        "4036f0 must cancel a maneuver when its actual target is no longer the adjacent actor");
    passing_fleet[0].ahead_id = 1;
    passing_fleet[0].forward_gap_q16 = 20 * one;
    passing_driver.restore_source_traffic(retained_passing);
    auto finish_controls = passing_driver.mapped_control_state();
    finish_controls.gear = 7;
    finish_controls.reversing = false;
    passing_driver.restore_source_controls(finish_controls);
    RaceFinishState finish_world;
    const auto completed = record_ai_finish(finish_world, {}, 0, 22000, 1000);
    const auto finish_rng = random.state();
    passing_driver.synchronize_source_finish(completed.finished != 0);
    const auto synchronized = passing_driver.mapped_control_state();
    expect(synchronized.gear == 7 && !synchronized.reversing && synchronized.reverse_ticks == finish_controls.reverse_ticks && random.state() == finish_rng,
        "A generated AI finish must synchronize its retained finish latch without402be0's gear7 reverse write or an RNG draw");
    passing_driver.decide_traffic(body, 100 * one, passing_fleet[0], passing_fleet, 1000 * one);
    expect(!passing_driver.traffic_state(body, 100 * one, 0).passing && passing_driver.traffic_mode() == 1,
        "The next retained traffic consumer must cancel the unfinished passing maneuver after the actual AI finish");
    driver.handoff(1, false);
    fleet[1].blocked = true;
    fleet[1].blocker_id = 2;
    fleet[1].speed_q16 = 60 * one;
    fleet[2].speed_q16 = 0;
    driver.observe_route(body, 100 * one, 0);
    driver.decide_traffic(body, 100 * one, fleet[0], fleet, 1000 * one);
    decision = driver.traffic_state(body, 100 * one, 0);
    expect(decision.blocked && decision.blocker_id == 2 && (decision.mode == 4 || decision.mode == 5),
        "4036f0 must inherit the queue's blocking identity and aim around that actual body");
    const auto queue_side = decision.mode;
    // Crossing the opposite corner angle must not reverse an existing side.
    fleet[2].body.position[2] = -4 * one;
    scene::refresh_body_contacts(fleet[2].body, 0);
    driver.observe_route(body, 100 * one, 0);
    driver.decide_traffic(body, 100 * one, fleet[0], fleet, 1000 * one);
    expect(driver.traffic_mode() == queue_side, "403690 must retain the existing passing side while following a blocker chain");

    // A fast follower in a queue just behind a human must brake even if
    // neither adjacent AI has yet reached that human's collision body.
    fleet[2].human = true;
    fleet[2].player = true;
    fleet[1].forward_gap_q16 = 10 * one;
    fleet[0].forward_gap_q16 = 16 * one;
    driver.decide_traffic_brake(100 * one, fleet[0], fleet);
    auto axes = driver.axes(body, 100 * one, 1);
    expect(axes.throttle_q16 == 0, "403aa0 must suppress gas for a queue closing immediately behind the human");
    // 404ea0 may release the brake under large steering; its gas decision is
    // still observable independently of the physical controls filter.
    fleet[1].route_unavailable = true;
    driver.observe_route(body, 40 * one, 0);
    driver.decide_traffic(body, 40 * one, fleet[0], fleet, 1000 * one);
    decision = driver.traffic_state(body, 40 * one, 0);
    expect(!decision.blocked && !decision.passing && decision.mode == 1, "4036f0 must clear passing/queue state when the adjacent actor's source route flags disallow traffic");

    //The native seed896387043 failure followed1 ->15 ->14 after actor14
    //left the active body/ring view.4036f0 still addresses its allocated body
    //through game+99; body retirement does not deallocate that actor slot.
    AiDriver retained_queue(points, record, distributions, 0, 0, random);
    retained_queue.set_rival(true, false);
    retained_queue.handoff(1, false);
    std::array<AiTraffic, 16> allocated;
    for (std::uint32_t actor = 0; actor < allocated.size(); ++actor) {
        allocated[actor].actor_id = actor;
        allocated[actor].body = body;
    }
    auto& follower = allocated[1];
    follower.ahead_id = 15;
    follower.behind_id = 0;
    follower.speed_q16 = 100 * one;
    follower.forward_gap_q16 = 20 * one;
    auto& ahead = allocated[15];
    ahead.ahead_id = 0;
    ahead.blocked = true;
    ahead.blocker_id = 14;
    ahead.speed_q16 = 60 * one;
    ahead.route_distance_q16 = 20 * one;
    ahead.body.position[0] = 20 * one;
    scene::refresh_body_contacts(ahead.body, 0);
    auto& retired_blocker = allocated[14];
    retired_blocker.route_distance_q16 = 40 * one;
    retired_blocker.body.position[0] = 40 * one;
    retired_blocker.body.position[2] = 4 * one;
    scene::refresh_body_contacts(retired_blocker.body, 0);
    std::vector<AiTraffic> active;
    for (const auto& actor : allocated) {
        if (actor.actor_id != 14) {
            active.push_back(actor);
        }
    }
    retained_queue.observe_route(body, follower.speed_q16, 0);
    bool rejected_missing_body = false;
    try {
        retained_queue.decide_traffic(body, follower.speed_q16, follower, active, 1000 * one);
    } catch (const std::runtime_error& error) {
        rejected_missing_body = std::string(error.what()).find("Traffic blocker chain requires an allocated actor") != std::string::npos;
    }
    expect(rejected_missing_body, "A filtered traffic view must reject an allocated blocker omitted from the caller's body lookup, rather than invent a fallback");
    const auto queue_rng = random.state();
    retained_queue.decide_traffic(body, follower.speed_q16, follower, allocated, 1000 * one);
    const auto retained = retained_queue.traffic_decision_state();
    expect(retained.blocked && retained.blocker_actor == 14 && (retained.mode == 4 || retained.mode == 5) && random.state() == queue_rng,
        "4036f0 must follow1 ->15 ->14 through the retained allocated rival body, independently of its active ring membership and without an RNG draw");
}

void source_release_context() {
    Bytes points(4 + 40 * 64);
    put32(points, 0, 40);
    for (std::size_t point = 0; point < 40; ++point) {
        const auto at = 4 + point * 64;
        put32(points, at, 20 * one);
        put32(points, at + 36, -20 * one);
        put32(points, at + 12, 333 * one);
        put32(points, at + 24, -333 * one);
        put32(points, at + 48, 64);
        put32(points, at + 60, 192);
        put32(points, at + 52, 200);
        put32(points, at + 56, 200);
    }
    std::array<std::uint8_t, 40> record{};
    put32(record, 28, one);
    std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(42);
    AiDriver driver(points, record, distributions, 0, 0, random);
    driver.set_rival(true, false);
    driver.set_release_counter(360);
    scene::BodyPose body;
    driver.observe_route(body, 0, 0);
    auto state = driver.route_state();
    expect(state.target[0] == 10 * one && state.alternate_target[0] == -10 * one,
        "Native release must feed4018f0's initialized low-byte start lanes through the same4024c0 target consumer");
    auto mapped = driver.mapped_control_state();
    mapped.reaction_ticks = 19;
    driver.restore_source_controls(mapped);
    driver.set_start_wait(true, true);
    const auto before = random.state();
    driver.release_start();
    expect(driver.mapped_control_state().reaction_ticks == 0 && random.state() == before, "Ordinary401260(0) must clear a retained reaction delay without sampling the shared RNG");
    const auto axes = driver.axes(body, 0, 1);
    expect(axes.throttle_q16 == one + 0x9999 && axes.brake_q16 == 0 && random.state() == before,
        "The native post-release mapper must consume the positive launch interval immediately without a fabricated reaction draw");
    driver.set_release_counter(0);
    driver.observe_route(body, 0, 1);
    state = driver.route_state();
    expect(state.target[0] == 333 * one && state.alternate_target[0] == -333 * one, "After release counter expiry a new target must use the ordinary POINTS lanes");
}

void source_traffic_controls() {
    auto points = route(true);
    std::array<std::uint8_t, 40> record{};
    put32(record, 28, one);
    const std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(42);
    AiDriver driver(points, record, distributions, 0, 0, random, false, AiRouteEncoding::source_initialized);
    driver.set_rival(true, false);
    driver.initialize_headlights(true);
    driver.handoff(1, false);
    expect(driver.headlights_requested(), "402be0 ownership changes must retain434f00's weather-initialized headlights");
    driver.initialize_headlights(false);
    AiRouteState route_state;
    route_state.cached_point = 0;
    route_state.desired_speed_q16 = 20 * one;
    route_state.alternate_speed_q16 = 30 * one;
    route_state.projected_distance_q16 = one;
    driver.restore_source_route(route_state);
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    scene::refresh_body_contacts(body, 0);
    std::array<AiTraffic, 3> fleet;
    for (std::uint32_t actor = 0; actor < fleet.size(); ++actor) {
        fleet[actor].actor_id = actor;
        fleet[actor].ahead_id = (actor + 1) % 3;
        fleet[actor].behind_id = (actor + 2) % 3;
        fleet[actor].body = body;
        fleet[actor].speed_q16 = 20 * one;
        fleet[actor].forward_gap_q16 = 200 * one;
    }
    auto direction = fleet[1];
    direction.reversing = true;
    direction.opposing_direction = false;
    const auto following_stop = driver.traffic_stopping_distance(100 * one, direction);
    direction.reversing = false;
    direction.opposing_direction = true;
    const auto opposing_stop = driver.traffic_stopping_distance(100 * one, direction);
    //At100/20 the two source table-index pairs are120/20 and20/120;
    //their absolute delta is equal, with an extra8 only for game+10==0.
    expect(following_stop == opposing_stop + 8 * one, "4051a0 traffic call arguments must use game+10 direction independently of the opposite game+28 reverse-recovery flag");
    direction.reversing = true;
    expect(driver.traffic_stopping_distance(100 * one, direction) == opposing_stop, "Changing reverse recovery alone must retain the stopping-distance branch selected by game+10");
    AiTrafficDecisionState traffic;
    traffic.passing = true;
    traffic.passed_actor = 1;
    traffic.corridor = {true, false, false, true};
    driver.restore_source_traffic(traffic);
    fleet[1].route_angle = -1500;
    driver.decide_traffic(body, 100 * one, fleet[0], fleet, 1000 * one);
    const auto retained = driver.traffic_decision_state();
    expect(retained.passing && retained.corridor == traffic.corridor && retained.corners == std::array<std::uint8_t, 2>{1, 2},
        "403490 must retain all four corridor flags outside the stopping gap while4033d0 uses the leader's retained route angle");
    traffic = {};
    traffic.headlight_ticks = {255, 7, 8};
    driver.restore_source_traffic(traffic);
    fleet[0].forward_gap_q16 = 20 * one;
    SourceRandom expected(random.state());
    const auto on = expected.next() % 15 + 5, off = expected.next() % 30 + 5;
    driver.decide_traffic(body, 80 * one, fleet[0], fleet, 1000 * one);
    const auto flash = driver.traffic_decision_state();
    expect(flash.blocked && flash.headlights && flash.headlight_ticks == std::array<std::uint8_t, 3>{0, std::uint8_t(on), std::uint8_t(off)} && random.state() == expected.state(),
        "402d00 headlight cadence must consume exactly two shared RNG draws on its byte wrap");
    driver.restore_source_traffic({});
    fleet[0].obstacle = true;
    fleet[1].speed_q16 = 100 * one;
    fleet[0].forward_gap_q16 = 200 * one;
    driver.decide_traffic(body, 100 * one, fleet[0], fleet, 1000 * one);
    driver.decide_traffic_brake(100 * one, fleet[0], fleet);
    expect(driver.traffic_decision_state().brake, "403aa0 game+40 must force traffic braking above80 outside source release");
    driver.set_traffic_context({.release_counter = 1});
    driver.decide_traffic_brake(100 * one, fleet[0], fleet);
    expect(!driver.traffic_decision_state().brake, "403aa0 release must bypass the ordinary obstacle-braking branch");
    driver.set_traffic_context({});
    driver.restore_source_traffic({});
    //4051a0 uses signed indices into adjacent source memory. A reversed
    //actor at-21 reads index-1 (start-line bound), not a clamped table0.
    AiDriver negative(points, record, distributions, 0, 0, random, false, AiRouteEncoding::source_initialized);
    negative.set_rival(true, false);
    negative.handoff(1, false);
    std::array<std::uint8_t, 1212> braking_memory{};
    put32(braking_memory, 260, 123 * one);
    negative.restore_source_braking_memory(braking_memory);
    fleet[1].speed_q16 = 20 * one;
    fleet[0].forward_gap_q16 = 100 * one;
    auto retained_passing = AiTrafficDecisionState{};
    retained_passing.mode = 2;
    negative.restore_source_traffic(retained_passing);
    negative.decide_traffic(body, -21 * one, fleet[0], fleet, 1000 * one);
    expect(negative.traffic_mode() == 1, "4051a0 signed negative actual-speed index must read the retained pre-table bound word instead of clamping to zero");
    AiMappedControlState state;
    state.reaction_ticks = 2;
    state.brake_ticks = 7;
    state.brake_lights = true;
    driver.restore_source_controls(state);
    driver.set_control_context({.started = false});
    const auto rng_before = random.state();
    expect(driver.map_source_controls(body, 80 * one).brake_q16 == one && driver.mapped_control_state().reaction_ticks == 1 && !driver.brake_lights_requested()
               && random.state() == rng_before,
        "404ea0 reaction delay must precede prestart RNG, clear brake lights and decrement once");
    driver.map_source_controls(body, 80 * one);
    expected = SourceRandom(random.state());
    const auto gas = expected.next() <= 0x7ff ? one : 0;
    expect(driver.map_source_controls(body, 80 * one).throttle_q16 == gas && random.state() == expected.state() && driver.mapped_control_state().brake_ticks == 10,
        "404ea0 prestart gas must consume one shared RNG draw only after the reaction delay expires");
    driver.restore_source_route(route_state);
    state = {};
    state.brake_ticks = 15;
    driver.restore_source_controls(state);
    driver.set_control_context({});
    const auto braking = driver.map_source_controls(body, 80 * one);
    expect(braking.throttle_q16 == 0 && braking.brake_q16 == one && driver.brake_lights_requested() && driver.mapped_control_state().brake_ticks == 0,
        "404ea0 must publish a full route brake on its sixteenth cadence tick");
    auto forced = AiControlContext{};
    forced.force_acceleration = true;
    driver.set_control_context(forced);
    const auto accelerating = driver.map_source_controls(body, 80 * one);
    expect(accelerating.throttle_q16 == one && accelerating.brake_q16 == 0 && driver.brake_lights_requested(),
        "Force acceleration changes mapped pedals while the brake-light latch retains between cadence ticks");
    state.brake_ticks = 15;
    driver.restore_source_controls(state);
    forced.suppressed = true;
    driver.set_control_context(forced);
    const auto suppressed = driver.map_source_controls(body, 80 * one);
    expect(suppressed.throttle_q16 == 0 && suppressed.brake_q16 == 0 && !driver.brake_lights_requested() && driver.mapped_control_state().brake_ticks == 16,
        "Rollover suppression must precede force acceleration and retain the incremented cadence without normal publication");
    for (const auto mode : {4u, 5u}) {
        AiTrafficDecisionState obstacle;
        obstacle.mode = std::uint8_t(mode);
        obstacle.corners = {2, 3};
        obstacle.obstacle_angles = {219, -858};
        driver.restore_source_traffic(obstacle);
        driver.restore_source_controls({});
        driver.set_control_context({.obstacle = true});
        const auto fixed_steering = driver.map_source_controls(body, 60 * one);
        const auto retained_obstacle = driver.traffic_decision_state();
        expect(fixed_steering.left_q16 == (mode == 4 ? 0xe666 : 0) && fixed_steering.right_q16 == (mode == 5 ? 0xe666 : 0) && !retained_obstacle.steering_cached
                   && retained_obstacle.obstacle_angles == obstacle.obstacle_angles,
            "405530 low-speed LPAST/RPAST obstacle steering must return before refreshing its cached corner angles");
        driver.restore_source_traffic(obstacle);
        driver.restore_source_controls({});
        driver.map_source_controls(body, 65 * one);
        expect(driver.traffic_decision_state().steering_cached, "405530 must refresh uncached obstacle angles at the exact65-speed boundary");
        driver.restore_source_traffic(obstacle);
        AiMappedControlState reversing;
        reversing.reversing = true;
        reversing.gear = 7;
        reversing.axes.left_q16 = one;
        driver.restore_source_controls(reversing);
        const auto reverse_steering = driver.map_source_controls(body, 60 * one);
        expect(reverse_steering.left_q16 == one && reverse_steering.right_q16 == 0 && !driver.traffic_decision_state().steering_cached
                   && driver.traffic_decision_state().obstacle_angles == obstacle.obstacle_angles,
            "405530 retained reverse steering must bypass obstacle-mode angle refresh entirely");
    }
    //405530's >=150, nearby-target band reads game+1c traffic braking,
    //independently of game+40 obstacle. These actual source axes were exposed
    //by the retained warm wall interval at updates49/55, actor0.
    driver.set_rival(true, true);
    route_state.projected_distance_q16 = 5 * one;
    route_state.angle = 31;
    driver.restore_source_route(route_state);
    AiTrafficDecisionState braking_turn;
    braking_turn.brake = true;
    driver.restore_source_traffic(braking_turn);
    driver.restore_source_controls({});
    driver.set_control_context({});
    const auto tight_braking = driver.map_source_controls(body, 155 * one);
    expect(tight_braking.left_q16 == 2108 && tight_braking.right_q16 == 0, "405530 nearby high-speed traffic braking must select large distant gain even without game+40 obstacle");
    route_state.angle = -31;
    driver.restore_source_route(route_state);
    driver.restore_source_traffic({});
    driver.restore_source_controls({});
    driver.set_control_context({.obstacle = true});
    const auto obstacle_only = driver.map_source_controls(body, 157 * one);
    expect(obstacle_only.left_q16 == 0 && obstacle_only.right_q16 == 875,
        "405530 nearby high-speed obstacle alone must retain small distant gain while game+1c traffic brake is clear");
    driver.set_rival(true, false);
    driver.set_control_context({});
    route_state.angle = 1500;
    driver.restore_source_route(route_state);
    AiMappedControlState retained_pedal;
    retained_pedal.reverse_pedal = 1;
    driver.restore_source_controls(retained_pedal);
    driver.advance_source_reverse_recovery(one, false);
    expect(driver.mapped_control_state().reversing && driver.mapped_control_state().gear == 7 && driver.mapped_control_state().reverse_pedal == 1
               && !driver.mapped_control_state().axes.reset_reverse_pedal,
        "403dd0 reverse recovery uses signed source speed below10 and a target angle beyond a quarter turn, retaining physical304 on entry");
    auto reverse = driver.mapped_control_state();
    reverse.axes.right_q16 = one;
    reverse.reverse_ticks = 2;
    reverse.reverse_pedal = 1;
    driver.restore_source_controls(reverse);
    driver.advance_source_reverse_recovery(one, true);
    const auto recovered = driver.mapped_control_state();
    expect(!recovered.reversing && recovered.gear == 1 && recovered.reverse_ticks == 3 && recovered.reverse_pedal == 0 && recovered.axes.left_q16 == 0
               && recovered.axes.right_q16 == 0 && recovered.axes.forced_gear == 1 && recovered.axes.reset_reverse_pedal,
        "The third retained barrier recovery tick must toggle forward, clear raw steering and the reverse-pedal latch");
    const auto recovery_axes = driver.map_source_controls(body, one);
    expect(recovery_axes.forced_gear == 1 && recovery_axes.reset_reverse_pedal,
        "The descending control mapper must retain both actual403dd0 recovery stores for physical consumption");
    driver.advance_source_reverse_recovery(one, false);
    const auto retained_recovery = driver.mapped_control_state();
    expect(retained_recovery.axes.forced_gear == -1 && !retained_recovery.axes.reset_reverse_pedal && !retained_recovery.reversing && retained_recovery.gear == 1,
        "A recovery cooldown update must emit no gear or304 store even while gameplay+28 remains forward");
    //No-barrier exit has no steering clear, unlike the third barrier tick.
    route_state.angle = 100;
    driver.restore_source_route(route_state);
    reverse.axes.left_q16 = one;
    reverse.reverse_ticks = 0;
    driver.restore_source_controls(reverse);
    driver.advance_source_reverse_recovery(one, false);
    const auto straightened = driver.mapped_control_state();
    expect(!straightened.reversing && straightened.axes.forced_gear == 1 && straightened.axes.reset_reverse_pedal && straightened.reverse_pedal == 0
               && straightened.axes.left_q16 == one,
        "403dd0 angle recovery exit writes330/304 and gameplay+28 without the barrier branch's steering stores");

    //Actual403dd0 writer vectors: lap-original64 trace CRC32 e51dcb2c,
    //id33/actor0, id37/actor3 and id45/actor5. PRE supplies retained state;
    //angle/speed are explicit upstream route outputs, unchanged by recovery
    //or the later mapper. Expectations are source ai_post/consumed outputs.
    //This isolates the writer; it does not replay the interval chronology.
    struct RecoveryVector {
        AiMappedControlState before;
        std::int16_t angle;
        std::int32_t speed;
        bool barrier;
        AiMappedControlState after;
    };

    const std::array source_recovery{RecoveryVector{{.axes = {one, 0}, .reversing = true, .gear = 7}, 397, -3364300, false, {.axes = {one, 0, 0, 0, 1, true}, .gear = 1}},
        RecoveryVector{{.axes = {one, 0}, .reversing = true, .gear = 7}, -22, -1765200, false, {.axes = {one, 0, 0, 0, 1, true}, .gear = 1}},
        RecoveryVector{
            {.axes = {0, 8980}, .reverse_ticks = 2, .gear = 2}, -133, 6341900, true, {.axes = {0, 0, 0, 0, 7, false}, .reversing = true, .reverse_ticks = 3, .gear = 7}}};
    for (const auto& sample : source_recovery) {
        route_state.angle = sample.angle;
        driver.restore_source_route(route_state);
        driver.restore_source_controls(sample.before);
        driver.advance_source_reverse_recovery(sample.speed, sample.barrier);
        const auto actual = driver.mapped_control_state();
        const auto& expected = sample.after;
        expect(actual.gear == expected.gear && actual.reverse_pedal == expected.reverse_pedal && actual.reversing == expected.reversing
                   && actual.reverse_ticks == expected.reverse_ticks && actual.axes.left_q16 == expected.axes.left_q16 && actual.axes.right_q16 == expected.axes.right_q16
                   && actual.axes.forced_gear == expected.axes.forced_gear && actual.axes.reset_reverse_pedal == expected.axes.reset_reverse_pedal,
            "Captured403dd0 recovery inputs must reproduce actual source330/304/game28/ticks and steering stores");
    }
}

void canonical_rollouts(const AssetStore& store) {
    double total_left_yaw = 0, total_right_yaw = 0;
    for (std::uint32_t track = 0; track < scene::track_catalog.size(); ++track) {
        const auto setup = scene::resolve_setup({.track_id = track});
        const auto& content = scene::track_content(track);
        const auto data = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
        const auto keep = store.open_archive(std::string(content.archive_prefix) + "keep.bfp");
        const auto free = store.open_archive(std::string(content.archive_prefix) + "free.bfp");
        const auto route_bytes = keep.member(1).bytes();
        const auto terrain = parse_terrain(free.member(1).bytes());
        const scene::BarrierData barriers(route_bytes, terrain.sector_centers);
        const HpcSurface surface(keep.member(0).bytes());
        const auto initial = scene::initial_body_pose(surface, scene::single_qualifying_start(route_bytes), 0);
        SourceRandom random(7);
        SurfaceRoughness roughness;
        DamageContext damage(surface, &barriers);
        Motion motion(surface, initial, 0, true, setup.weather, true, damage, roughness, &barriers);
        motion.prepare_ai(AiDriver(route_bytes, data.drivers[data.local_actor].raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random));
        motion.handoff_to_ai(false); // 443f60: initial qualifying actor ownership.
        Session session(route_bytes, true);
        std::uint32_t ticks = 0, advanced = 0;
        auto previous_point = session.next_point();
        double left_yaw = 0, right_yaw = 0;
        for (; ticks < 3000 && !session.released(); ++ticks) {
            (void)session.update(std::uint64_t(ticks + 1) * 40, motion.state().body.position);
            const auto point = session.next_point();
            advanced += point != previous_point ? 1u : 0u;
            previous_point = point;
            motion.set_ai_route_point(point);
            if (session.released()) {
                const auto owned = motion.state();
                const auto owned_controls = motion.controls();
                motion.restore_player_control();
                expect(!motion.ai_controlled() && motion.state().body.position == owned.body.position && motion.state().velocity_q16 == owned.velocity_q16
                           && motion.controls().gear == owned_controls.gear && motion.controls().engine_speed_q16 == owned_controls.engine_speed_q16,
                    "421390/402c80 must return the actual moving rollout actor to player control at the first valid line");
                break;
            }
            const auto before = motion.state().body.basis[0];
            // No player action can substitute for the original autonomous controls.
            motion.update({.brake = true, .right = true, .gear_down = true}, {});
            damage.advance();
            if (motion.state().boundary != MotionBoundary::none) {
                throw std::runtime_error("Canonical rollout left HPC coverage on track " + std::to_string(track) + " at tick " + std::to_string(ticks));
            }
            const auto after = motion.state().body.basis[0];
            const auto turn = std::atan2(double(before[0]) * after[2] - double(before[2]) * after[0], double(before[0]) * after[0] + double(before[2]) * after[2]);
            left_yaw += std::max(turn, 0.0);
            right_yaw += std::max(-turn, 0.0);
        }
        std::cout << "Rollout track " << track << ": " << ticks << " ticks, " << advanced << " route advances, left/right yaw " << left_yaw << '/' << right_yaw << '\n';
        expect(session.released() && ticks > 32 && advanced >= 16,
            "Original autonomous rollout must follow the authored route through to the first start line, beyond the old32-tick continuation fixture");
        total_left_yaw += left_yaw;
        total_right_yaw += right_yaw;
    }
    expect(total_left_yaw > .25 && total_right_yaw > .25, "Canonical route following must produce actual full-physics turns in both directions");
}

void canonical(const std::filesystem::path& root) {
    const AssetStore store(root);
    canonical_rollouts(store);
    const auto setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 1});
    const auto data = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
    const auto keep = store.open_archive("data/donlkeep.bfp");
    const auto free = store.open_archive("data/donlfree.bfp");
    const auto route_bytes = keep.member(1).bytes();
    const auto terrain = parse_terrain(free.member(1).bytes());
    const scene::BarrierData barriers(route_bytes, terrain.sector_centers);
    const HpcSurface surface(keep.member(0).bytes());
    const auto initial = scene::initial_body_pose(surface, scene::single_qualifying_start(route_bytes), 0);
    SourceRandom random(7);
    SurfaceRoughness roughness;
    DamageContext damage(surface, &barriers);
    const auto slots = scene::decode_start_grid(keep.member(3).bytes());

    struct CheatTuning {
        bool mayhem, impossible;
        std::int32_t tire;
    };

    for (const auto tuning : {CheatTuning{true, false, 0}, CheatTuning{false, true, 0x3333}, CheatTuning{true, true, 0}}) {
        const auto wet_setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 3});
        const auto tuned = scene::load_qualifying_data(store, wet_setup, 1, "PLAYER1", tuning.mayhem, tuning.impossible);
        SourceRandom tuned_random(7);
        const auto tuned_grid = make_race_grid(tuned, tuned_random);
        Rivals rivals(surface, slots, route_bytes, tuned, wet_setup.weather, tuned_random, tuned_grid, damage, roughness, &barriers, false, tuned.point_speed_boost);
        for (std::uint32_t actor = 1; actor < 16; ++actor) {
            expect(std::ranges::all_of(rivals.controls(actor).tire_offsets_q16,
                       [&](auto offset) {
                           return offset == tuning.tire;
                       }),
                "Mayhem and Impossible must replace wet-weather tuning in all eight tire parameters of all fifteen actual rival bodies, with Mayhem precedence");
        }
    }
    Motion motion(surface, initial, 0, true, setup.weather, true, damage, roughness, &barriers);
    motion.prepare_ai(AiDriver(route_bytes, data.drivers[data.local_actor].raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random));
    Session session(route_bytes, true);
    for (std::uint32_t tick = 1; tick <= 20; ++tick) {
        (void)session.update(tick * 40, motion.state().body.position);
        motion.set_ai_route_point(session.next_point());
        motion.update({.throttle = true}, {});
        damage.advance();
    }
    const auto before = motion.state();
    const auto rpm = motion.controls().engine_speed_q16;
    motion.handoff_to_ai();
    motion.handoff_to_ai();
    expect(motion.ai_controlled() && motion.state().body.position == before.body.position && motion.state().velocity_q16 == before.velocity_q16
               && motion.controls().engine_speed_q16 == rpm,
        "Handoff must preserve the actual full-physics actor, velocity and engine state");
    bool analog = false;
    for (std::uint32_t tick = 21; tick <= 52; ++tick) {
        (void)session.update(tick * 40, motion.state().body.position);
        motion.set_ai_route_point(session.next_point());
        // Deliberately conflicting human controls must not reach the AI actor.
        motion.update({.brake = true, .right = true, .gear_down = true}, {});
        damage.advance();
        expect(motion.state().boundary == MotionBoundary::none, "Canonical AI continuation must remain in real HPC coverage");
        analog = analog || motion.controls().raw_throttle_q16 != 0;
    }
    expect(analog && motion.state().body.position != before.body.position && motion.state().speed_q16 > 0,
        "Canonical handoff must continue actual source wheel/body integration from route-driven controls");
    const auto grid = scene::decode_start_grid(keep.member(3).bytes());
    const auto race_body = scene::initial_body_pose(surface, scene::race_grid_start(grid, 3), 0);
    motion.reset_for_race(race_body);
    expect(!motion.ai_controlled() && motion.start_held() && motion.controls().gear == 0 && motion.state().velocity_q16 == fixed::Vector{}
               && motion.state().body.position == race_body.position,
        "Classified grid reset must restore original local race initialization and open clutch");
    motion.set_start_hold(false);
    expect(motion.controls().gear == 0, "Race reconstruction must retain the selected manual transmission");
    //Two native actors share route draws ascending and cached control draws
    //descending. Physical dispatch must consume these axes without more draws.
    SourceRandom sequence(135);
    SourceRandom expected(135);
    auto lanes = route(true);
    std::array<std::uint8_t, 40> source_driver{};
    put32(source_driver, 28, one);
    source_driver[23] = source_driver[24] = 1;
    std::array<std::vector<std::int16_t>, 3> distributions;
    distributions[2] = {1, 2, 3};
    std::array<std::unique_ptr<Motion>, 2> pair;
    for (auto& car : pair) {
        car = std::make_unique<Motion>(surface, race_body, 0, false, 1, false, damage, roughness);
        car->prepare_ai(AiDriver(lanes, source_driver, distributions, 0, 0, sequence));
        car->handoff_to_ai(false);
        car->set_start_hold(true);
        car->set_ai_start_wait(true, false);
        car->begin_tick();
        car->observe_ai_route();
        car->recover_ai_controls();
        expected.next();
        expected.next();
    }
    expect(sequence.state() == expected.state(), "Native ascending route observation must consume both target variation draws for each actor");
    pair[1]->prepare_ai_controls();
    expected.next();
    pair[0]->prepare_ai_controls();
    expected.next();
    expect(sequence.state() == expected.state(), "Native descending prestart controls must follow all route draws on the same RNG stream");
    pair[0]->update_distant({});
    pair[1]->update_distant({});
    expect(sequence.state() == expected.state(), "Distant physical dispatch must consume cached AI axes without repeating a control or target RNG draw");
    std::cout << "Donington AI continuation " << before.body.position[0] << ',' << before.body.position[2] << " -> " << motion.state().body.position[0] << ','
              << motion.state().body.position[2] << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            source_race_grid();
            source_player_gap();
            source_route_cache();
            source_route_progress();
            traffic();
            source_traffic_controls();
            source_release_context();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: ai-driver-tests [canonical-root]");
        }
        std::cout << "PASS: source local AI handoff, analog route controls and full-physics continuation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
