#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/fixed.hpp"
#include "simulation/pit_lane.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

scene::PitRoute synthetic_route() {
    scene::PitRoute route;
    route.points.resize(12);
    for (std::size_t i = 0; i < route.points.size(); ++i) {
        auto& point = route.points[i];
        point.left = {-5 * one, 0, std::int32_t(i) * 10 * one};
        point.right = {5 * one, 0, std::int32_t(i) * 10 * one};
        point.best = {0, 0, std::int32_t(i) * 10 * one};
    }
    return route;
}

Vector gate_position(const scene::PitRoutePoint& point, std::int32_t side) {
    Vector midpoint{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        midpoint[axis] = std::int32_t((std::int64_t(point.left[axis]) + point.right[axis]) / 2);
    }
    const Vector normal{sub(point.left[2], point.right[2]), 0, sub(point.right[0], point.left[0])};
    return add(midpoint, scale(normalized(normal), side));
}

void traverse(const scene::PitRoute& route, std::uint32_t track) {
    PitLane pit(route, track);
    const auto before = gate_position(route.points[0], -one), after = gate_position(route.points[0], one);
    expect(!pit.update(before, true).crossed_entry, "Initial gate observation must not fabricate an entry");
    const auto entry = pit.update(after, true);
    if (track == 9) {
        expect(!entry.crossed_entry && !pit.active() && !pit.target(), "Lavaland must ignore its original PITS route");
        return;
    }
    expect(entry.crossed_entry && entry.entered && !entry.exited && pit.active() && pit.point() == 1 && pit.target() == route.points[2].best,
        "Human entry must activate index1 and steer to the following original Best point");
    for (std::uint32_t point = 1; point < route.points.size() - 2; ++point) {
        const auto transition = pit.update(gate_position(route.points[point], one), false);
        const auto next = point + 1;
        expect(!transition.entered && !transition.crossed_entry, "Pit progression must not replay the entry event");
        if (next == route.points.size() - 2) {
            expect(transition.exited && !pit.active() && pit.point() == 0 && pit.collision_enabled() && !pit.target(),
                "Count-2 must return ownership, clear cursor and restore static-barrier contacts");
        } else {
            expect(!transition.exited && pit.point() == next && pit.target() == route.points[next + 1].best, "Each update must consume one ordered PITS gate");
            expect(pit.collision_enabled() == (next < route.points.size() - 4), "Only the final count-4 stage disables static-barrier contacts");
            expect(pit.exiting_hint() == (next > route.points.size() - 8), "Original pit exit hint has a strict count-8 threshold");
        }
    }
    pit.reset();
    expect(!pit.active() && pit.point() == 0 && pit.collision_enabled(), "Grid reset must restore inactive source state");
    pit.update(before, false);
    const auto ai = pit.update(after, false);
    expect(ai.crossed_entry && !ai.entered && !pit.active() && pit.point() == 1, "A prior AI actor crosses the geometry without a new human takeover");
}

void put32(std::span<std::uint8_t> bytes, std::size_t offset, std::int32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::uint8_t(std::uint32_t(value) >> (8 * i));
    }
}

void ai_override() {
    std::vector<std::uint8_t> points(4 + 3 * 64);
    put32(points, 0, 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto base = 4 + i * 64;
        put32(points, base, 20 * one);
        put32(points, base + 8, 10 * one);
        put32(points, base + 12, 20 * one);
        put32(points, base + 20, 10 * one);
        put32(points, base + 36, 20 * one);
        put32(points, base + 44, -10 * one);
        put32(points, base + 52, 50);
    }
    std::array<std::uint8_t, 40> driver{};
    put32(driver, 28, 0x8000);
    for (std::size_t i = 21; i < 25; ++i) {
        driver[i] = 1;
    }
    std::array<std::vector<std::int16_t>, 3> distributions{{{0}, {0}, {0}}};
    SourceRandom random(19), normal_random(19);
    AiDriver ai(points, driver, distributions, 0, 0, random), normal(points, driver, distributions, 0, 0, normal_random);
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    ai.set_pit_target(Vector{20 * one, 0, -10 * one});
    ai.observe(body, 0, 0);
    normal.observe(body, 0, 0);
    const auto turning = ai.axes(body, 0, 1);
    expect(turning.right_q16 > 0 && turning.left_q16 == 0 && turning.throttle_q16 == one, "Pit Best must override POINTS steering and normal AIBIN throttle");
    expect(random.next() == normal_random.next() && ai.target_point() == normal.target_point(), "PITS steering must preserve normal POINTS refresh and shared RNG draws");
    ai.set_pit_target(Vector{20 * one, 0, 0});
    ai.observe(body, 0, 0);
    for (const auto speed : {0, 80, 99, 100, 101, 120, 121}) {
        const auto axes = ai.axes(body, speed * one, 1);
        const auto throttle = speed > 120 ? 0 : speed > 100 ? 0xccc : std::clamp(mul((100 - speed) * one, 0xccc), 0xccc, one);
        expect(axes.throttle_q16 == throttle && axes.brake_q16 == (speed > 120 ? one : 0), "Pit100/120 governor must keep exact source threshold equality and minimum gas");
    }
    ai.set_pit_target(Vector{20 * one, 0, -10 * one});
    ai.observe(body, 121 * one, 0);
    expect(ai.axes(body, 121 * one, 1).brake_q16 == 0, "404ea0 must release the full pit brake during strong steering above70");
    ai.set_pit_target(std::nullopt);
    ai.observe(body, 0, 0);
    expect(ai.axes(body, 0, 1).throttle_q16 == 0x8000, "Clearing pit ownership must restore normal route throttle");
}

void synthetic() {
    const auto route = synthetic_route();
    traverse(route, 0);
    traverse(route, 9);
    PitLane pit(route, 0);
    pit.update({0, 0, one}, true);
    expect(!pit.update({0, 0, -one}, true).entered, "Reverse traversal must not enter the pits");
    expect(!pit.update({6 * one, 0, one}, true).entered, "Crossing outside original endpoint planes must not enter");
    pit.update({0, 0, -one}, true);
    expect(!pit.update({0, 0, 71 * one}, true).entered, "A distant plane crossing must fail the original strict integer-distance gate");
    pit.update({0, 0, -one}, true);
    expect(pit.update({0, 0, 0}, true).entered, "Equality on the gate plane counts as the passed side");
    pit.update({0, 0, 1000 * one}, false);
    expect(pit.point() == 2, "A distant active position must advance at most one gate per simulation tick");
    ai_override();
}

void reference(const char* root) {
    const AssetStore store(root);
    for (std::uint32_t track = 0; track < scene::track_catalog.size(); ++track) {
        const auto& content = scene::track_content(track);
        const auto archive = store.open_archive(std::string(content.archive_prefix) + "keep.bfp");
        traverse(scene::decode_pit_route(archive.member(2).bytes()), track);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-pit-lane-tests [canonical-root]");
        }
        std::cout << "PASS: original PITS entry, ordered gates, AI target/governor and exit stages\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
