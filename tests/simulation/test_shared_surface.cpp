#include "simulation/ai_driver.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/surface_roughness.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;
constexpr std::array<std::int32_t, 15> offsets{655, -1310, -1966, 655, -1310, 0, -1310, -1966, 655, -1310, 655, 1966, -1310, -655, 1966};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void word(auto& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::uint8_t(std::uint32_t(value) >> (i * 8));
    }
}

HpcSurface plane(std::uint16_t flags) {
    // Original HPC layout: two clockwise triangles covering a128-unit square.
    std::vector<std::uint8_t> bytes(106);
    const auto halfword = [&](std::size_t offset, std::uint16_t value) {
        bytes.at(offset) = std::uint8_t(value);
        bytes.at(offset + 1) = std::uint8_t(value >> 8);
    };
    word(bytes, 0, -128 * one);
    word(bytes, 4, -128 * one);
    halfword(8, 1);
    halfword(20, 2);
    halfword(24, 4);
    constexpr std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * one, -64 * one}, {-64 * one, 64 * one}, {64 * one, -64 * one}, {64 * one, 64 * one}}};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        word(bytes, 28 + i * 12, vertices[i][0]);
        word(bytes, 32 + i * 12, 2 * one);
        word(bytes, 36 + i * 12, vertices[i][1]);
    }
    halfword(76, 2);
    constexpr std::array<std::uint16_t, 6> faces{0, 1, 2, 2, 1, 3};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        halfword(80 + (i / 3) * 8 + (i % 3) * 2, faces[i]);
    }
    halfword(86, flags);
    halfword(94, flags);
    halfword(96, 2);
    halfword(100, 1);
    halfword(102, 1);
    halfword(104, 1);
    return HpcSurface(bytes);
}

SourceSnapshot snapshot(const scene::BodyPose& body, std::int32_t speed, std::uint16_t material) {
    SourceSnapshot result;
    const auto vector = [&](std::size_t offset, const Vector& value) {
        for (std::size_t i = 0; i < 3; ++i) {
            word(result.body, offset + i * 4, value[i]);
        }
    };
    vector(0xbc, body.position);
    vector(0x268, body.position);
    vector(0xc8, {speed, 0, 0});
    vector(0x1e4, {0, one, 0});
    word(result.body, 0xe0, speed);
    for (std::size_t i = 0; i < 3; ++i) {
        vector(0x150 + i * 12, body.basis[i]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        vector(0xf0 + i * 12, body.contacts[i]);
        word(result.body, 0x1c4 + i * 4, 2 * one);
        result.body[0x517 + i] = i < 4 ? 0 : 1;
        result.dynamics[0x328 + i] = std::uint8_t(material);
    }
    result.body[0x51f] = 4;
    for (std::size_t i = 0; i < 4; ++i) {
        result.body[0x521 + i] = std::uint8_t(3 - i);
    }
    word(result.dynamics, 0x27c, 1);
    result.roughness_cursor = 14; // Vehicle import must not import this global.
    return result;
}

scene::BodyPose body(const HpcSurface& surface, std::int32_t x = 0) {
    return scene::initial_body_pose(surface, {{x, 0}, 0, 0}, 0);
}

std::array<std::int32_t, 8> heights(const Motion& motion) {
    std::array<std::int32_t, 8> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = motion.state().body.surfaces[i].height;
    }
    return result;
}

std::array<std::int32_t, 8> expected(std::size_t first, bool rough = true) {
    std::array<std::int32_t, 8> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = 2 * one + (rough ? offsets[(first + i) % offsets.size()] : 0);
    }
    return result;
}

void sequence_wrap() {
    SurfaceRoughness roughness;
    for (const auto offset : offsets) {
        expect(roughness.next() == offset, "The shared sequence must retain all fifteen original signed Q16 height values in order");
    }
    expect(roughness.current() == 0, "The original fifteenth draw must wrap the shared cursor to zero");
    roughness.import_cursor(14);
    expect(roughness.next() == 1966 && roughness.next() == 655 && roughness.current() == 1,
        "An explicit initial diagnostic import must continue through the source wrap without dropping an entry");
    bool rejected = false;
    try {
        roughness.import_cursor(15);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected && roughness.current() == 1, "An out-of-range diagnostic import must preserve the valid process-owned cursor");
}

void surface_gates() {
    for (const std::uint16_t material : {0, 1, 2, 3, 0x82, 0x83}) {
        const auto surface = plane(material);
        DamageContext damage(surface);
        for (const auto speed : {0x8000, 0x8001}) {
            SurfaceRoughness roughness;
            roughness.import_cursor(3);
            const auto start = body(surface);
            Motion motion(surface, start, 0, true, 1, false, damage, roughness);
            motion.restore_source_snapshot(snapshot(start, speed, material));
            expect(roughness.current() == 3, "Constructing/importing a car must not replace the process-owned surface sequence");
            motion.finish_ground();
            const bool consumes = (material & 0x7f) >= 2 && speed > 0x8000;
            expect(motion.state().boundary == MotionBoundary::none && heights(motion) == expected(3, consumes),
                "Real HPC grass/gravel contacts must use the strict source speed gate and water-flag material mask for all eight samples");
            expect(roughness.current() == (consumes ? 11 : 3), "Asphalt, curbs and slow rough contacts must not consume shared sequence entries");
        }
    }
}

void shared_actor_order() {
    for (const auto material : {2u, 3u}) {
        const auto surface = plane(std::uint16_t(material));
        DamageContext damage(surface);
        SurfaceRoughness shared, alone_first, alone_second;
        const auto first_body = body(surface, -10 * one), second_body = body(surface, 10 * one);
        Motion first(surface, first_body, 0, true, 1, false, damage, shared), second(surface, second_body, 0, true, 1, false, damage, shared);
        Motion isolated_first(surface, first_body, 0, true, 1, false, damage, alone_first), isolated_second(surface, second_body, 0, true, 1, false, damage, alone_second);
        for (auto* motion : {&first, &isolated_first}) {
            motion->restore_source_snapshot(snapshot(first_body, one, std::uint16_t(material)));
        }
        for (auto* motion : {&second, &isolated_second}) {
            motion->restore_source_snapshot(snapshot(second_body, one, std::uint16_t(material)));
        }
        //415240 visits normal actor identities ascending, after integration.
        first.finish_ground();
        second.finish_ground();
        isolated_first.finish_ground();
        isolated_second.finish_ground();
        expect(heights(first) == expected(0) && heights(second) == expected(8) && shared.current() == 1,
            "Two actual Motion bodies must consume consecutive blocks from the one shared fifteen-entry sequence, wrapping between actors");
        expect(heights(isolated_first) == heights(first) && heights(isolated_second) == heights(first) && heights(second) != heights(isolated_second),
            "Independent worlds repeat their own first block, while another car in the same world changes the following actor's real sampled heights");
        SurfaceRoughness reversed;
        Motion reversed_first(surface, first_body, 0, true, 1, false, damage, reversed), reversed_second(surface, second_body, 0, true, 1, false, damage, reversed);
        reversed_first.restore_source_snapshot(snapshot(first_body, one, std::uint16_t(material)));
        reversed_second.restore_source_snapshot(snapshot(second_body, one, std::uint16_t(material)));
        reversed_second.finish_ground();
        reversed_first.finish_ground();
        expect(heights(reversed_second) == heights(first) && heights(reversed_first) == heights(second),
            "Ground-dispatch order must select each car's sequence block; actor-local cursors must not conceal an order change");
        first.reset_for_race(first_body);
        expect(shared.current() == 1, "Individual car/grid reconstruction must not reset the shared process-lifetime roughness cursor");
        first.restore_source_snapshot(snapshot(first_body, one, std::uint16_t(material)));
        first.finish_ground();
        expect(heights(first) == expected(1) && shared.current() == 9, "A reset car must continue from the world's retained sequence position");
    }
}

void later_sample_failure() {
    const auto surface = plane(2);
    DamageContext damage(surface);
    SurfaceRoughness roughness;
    auto start = body(surface);
    // The HPC domain is[-128,128), wider than its two mesh triangles. The
    //source retains cached triangle0 while crossing that margin. This real
    //body footprint starts inside the domain and moves its rear corners out.
    start.position[0] = -125 * one - one / 2;
    start.position[1] += 4 * one;
    scene::refresh_body_contacts(start, 0);
    auto captured = snapshot(start, one, 2);
    word(captured.body, 0xc8, -one);
    for (std::size_t i = 0; i < 8; ++i) {
        captured.body[0x517 + i] = 1;
    }
    captured.body[0x51f] = 0;
    Motion falling(surface, start, 0, true, 1, false, damage, roughness);
    falling.restore_source_snapshot(captured);
    roughness.import_cursor(12);
    falling.update_source_snapshot({}, true);
    expect(falling.state().boundary == MotionBoundary::none && falling.state().speed_q16 > 0x8000 && falling.state().body.position[0] < start.position[0],
        "The failure fixture must physically integrate the airborne car toward the real HPC domain edge before ground dispatch");
    falling.finish_ground();
    expect(falling.state().boundary == MotionBoundary::missing_surface && falling.state().body.position == start.position && roughness.current() == 14,
        "A later failed HPC sample must roll back that car while retaining the shared entries already consumed by its first two valid contacts");
    const auto next_body = body(surface, 10 * one);
    Motion next(surface, next_body, 0, true, 1, false, damage, roughness);
    next.restore_source_snapshot(snapshot(next_body, one, 2));
    next.finish_ground();
    expect(next.state().boundary == MotionBoundary::none && heights(next) == expected(14) && roughness.current() == 7,
        "The next actor must observe the consumed prefix after another body's ground failure instead of replaying reverted global state");
}

void body_mode_phases() {
    const auto surface = plane(2);
    DamageContext damage(surface);
    SurfaceRoughness roughness;
    roughness.import_cursor(3);
    const auto start = body(surface);
    Motion distant(surface, start, 0, true, 1, false, damage, roughness);
    distant.restore_source_snapshot(snapshot(start, one, 2));
    std::vector<std::uint8_t> route(4 + 3 * 64);
    word(route, 0, 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto at = 4 + i * 64;
        word(route, at, 20 * one);
        word(route, at + 8, 10 * one);
        word(route, at + 12, 20 * one);
        word(route, at + 20, 0);
        word(route, at + 36, 20 * one);
        word(route, at + 44, -10 * one);
        word(route, at + 52, 100);
    }
    std::array<std::uint8_t, 40> driver{};
    word(driver, 28, 0x8000);
    const std::array<std::vector<std::int16_t>, 3> distributions;
    SourceRandom random(1);
    distant.prepare_ai(AiDriver(route, driver, distributions, 0, 0x1eb8, random));
    distant.handoff_to_ai(false);
    distant.set_rival(true);
    auto retained_heights = heights(distant);
    distant.update_distant({}, true);
    expect(distant.state().boundary == MotionBoundary::none && distant.state().speed_q16 > 0x8000 && distant.state().body.position != start.position && roughness.current() == 3,
        "A real fast distant-body integration on grass must leave the shared full-body roughness sequence untouched");
    distant.finish_ground(BodyMode::distant);
    //444ee0 aliases body+1d8 (height5) with its signed vertical corner.
    retained_heights[5] = distant.state().body.contacts[1][1];
    expect(distant.state().boundary == MotionBoundary::none && roughness.current() == 3 && heights(distant) == retained_heights,
        "444ee0 retains full-body heights except its explicit height5 alias, without consuming42e6f0's shared sequence");
    distant.finish_ground(BodyMode::crashing);
    expect(distant.state().boundary == MotionBoundary::none && roughness.current() == 3, "445360 crash settlement must not consume the full-body rough-surface sequence");

    Motion full(surface, start, 0, true, 1, false, damage, roughness);
    full.restore_source_snapshot(snapshot(start, one, 2));
    full.update_source_snapshot({}, true);
    expect(full.state().boundary == MotionBoundary::none && full.state().speed_q16 > 0x8000 && full.state().body.position != start.position && roughness.current() == 3,
        "Deferred full-body integration must not consume roughness before the separate415240 ground phase");
    full.finish_ground();
    expect(full.state().boundary == MotionBoundary::none && heights(full) == expected(3) && roughness.current() == 11,
        "The following full-body ground phase must consume exactly the next eight shared entries after non-consuming distant/crash phases");
}
} // namespace

int main() {
    try {
        sequence_wrap();
        surface_gates();
        shared_actor_order();
        later_sample_failure();
        body_mode_phases();
        std::cout << "PASS: actual shared multi-car rough surface samples, material/speed gates, actor order, wrap and reset continuity\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
