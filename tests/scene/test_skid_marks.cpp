#include "assets/store.hpp"
#include "core/random.hpp"
#include "scene/barriers.hpp"
#include "scene/body.hpp"
#include "scene/skid_marks.hpp"
#include "simulation/fixed.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
namespace fixed = toca::simulation::fixed;
constexpr auto one = fixed::one;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SceneSetup setup(std::uint32_t weather = 1, std::uint32_t track = 0) {
    return resolve_setup({.track_id = track, .weather_choice = weather}, 1);
}

SkidSectorQuery sector() {
    return [](const auto&) {
        return std::uint16_t(7);
    };
}

SurfaceEffectVehicle car() {
    SurfaceEffectVehicle result;
    result.velocity = {one, 0, 0};
    result.wheels[0].longitudinal_slip = one;
    return result;
}

void move(SurfaceEffectVehicle& vehicle, std::int32_t x, std::int32_t z = 0) {
    for (auto& wheel : vehicle.wheels) {
        wheel.position[0] = x;
        wheel.position[2] = z;
    }
}

void finish(SkidMarks& marks, std::array<SurfaceEffectVehicle, 1>& cars) {
    cars[0].wheels[0].longitudinal_slip = 0;
    marks.update(cars);
}

void geometry_and_gates() {
    std::size_t sector_queries = 0;
    SkidMarks marks(setup(), [&](const auto& vertex) {
        ++sector_queries;
        expect(vertex[1] == 0xa3d, "Sector query must use the authored first quad vertex with the original ground lift");
        return std::uint16_t(7);
    });
    std::array cars{car()};
    cars[0].wheels[0].longitudinal_slip = 0x4ccc;
    marks.update(cars);
    expect(marks.frame().quads.empty(), "Slip exactly0x4ccc cannot start a skid");
    cars[0].wheels[0].longitudinal_slip = -0x2666;
    cars[0].wheels[0].lateral_slip = -0x2667;
    marks.update(cars);
    expect(marks.frame().quads.empty() && sector_queries == 0, "Strict slip-sum crossing starts a degenerate strip without yet submitting it");
    move(cars[0], one);
    marks.update(cars);
    const auto quad = marks.frame().quads.at(0);
    expect(marks.frame().active == 1 && marks.frame().persistent == 0 && quad.sector == 7 && quad.visibility == 1 && quad.color == 0xa0000000,
        "A continuing dry road skid supplies one current black-alpha160 quad in its source sector");
    expect(quad.vertices == std::array<SkidMarkVector, 4>{{{0, 2621, -6551}, {65536, 2621, -6551}, {65536, 2621, 6551}, {0, 2621, 6551}}},
        "44b690 must retain lookup-normalized body velocity,0x1999 half-width and HPC+0xa3d lift");
    cars[0].wheels[0].airborne = true;
    marks.update(cars);
    expect(marks.frame().persistent == 1 && marks.frame().active == 0 && marks.frame().quads[0].vertices == quad.vertices,
        "The cached airborne flag ends the prior segment without extending it to a corrected later wheel position");
    cars[0].wheels[0].airborne = false;
    cars[0].wheels[0].position[1] = 4 * one;
    marks.update(cars);
    move(cars[0], 2 * one);
    marks.update(cars);
    expect(marks.frame().active == 1, "44b690 must consume the cached contact flag rather than infer contact again from final wheel coordinates");
    const auto queries = sector_queries;
    for (int draw = 0; draw < 144; ++draw) {
        (void)marks.frame();
    }
    expect(sector_queries == queries && marks.frame().updates == 6, "Frame access cannot mutate sectors, geometry, timing or fading");
}

void visibility_lifecycle() {
    std::size_t sector_queries = 0;
    SkidMarks marks(setup(3), [&](const auto&) {
        ++sector_queries;
        return std::uint16_t(7);
    });
    std::array cars{car()};
    auto& vehicle = cars[0];
    vehicle.actor = 3;
    vehicle.visibility = 2;
    vehicle.wheels[0].ground_height = 2 * one;
    marks.update(cars);
    move(vehicle, one);
    marks.update(cars);
    const auto visible = marks.frame().quads.at(0);
    expect(marks.frame().active == 1 && visible.visibility == 2, "A visible opponent must have a live segment before leaving the view");

    // Distant bodies move their wheel contacts without refreshing these four
    // HPC heights. 446b50 dispatches44c430 before those stale values can emit.
    vehicle.visibility = 0;
    move(vehicle, 100 * one, 100 * one);
    vehicle.wheels[0].position[1] = 40 * one;
    marks.update(cars);
    const auto hidden = marks.frame();
    expect(hidden.active == 0 && hidden.persistent == 1 && hidden.emitted == 1 && marks.cursor() == 1 && hidden.quads[0].visibility == 0
               && hidden.quads[0].vertices == visible.vertices,
        "An invisible opponent must close its previous segment with mask zero before consuming distant wheel coordinates or stale HPC heights");
    expect(marks.persistent_state().records[0].delay == 49, "Closing an invisible wet-road segment must count its commit tick toward the original fade delay");
    const auto hidden_queries = sector_queries;
    for (int tick = 0; tick < 3; ++tick) {
        move(vehicle, (120 + tick * 20) * one, (120 + tick * 20) * one);
        marks.update(cars);
    }
    expect(marks.frame().active == 0 && marks.frame().persistent == 1 && marks.frame().emitted == 1 && marks.cursor() == 1 && sector_queries == hidden_queries
               && marks.frame().quads[0].vertices == visible.vertices && marks.persistent_state().records[0].delay == 46,
        "Repeated invisible updates must only age the closed strip, without generating floating trails or advancing the ring again");

    vehicle.visibility = 2;
    move(vehicle, 200 * one);
    vehicle.wheels[0].ground_height = 10 * one;
    marks.update(cars);
    expect(marks.frame().active == 0 && marks.frame().persistent == 1 && sector_queries == hidden_queries,
        "An opponent returning to view must start a new strip rather than bridge across its invisible travel");
    move(vehicle, 201 * one);
    marks.update(cars);
    const auto resumed = marks.frame();
    expect(resumed.active == 1 && resumed.persistent == 1 && resumed.quads.back().visibility == 2 && resumed.quads.back().vertices[0][0] == 200 * one
               && resumed.quads.back().vertices[3][0] == 200 * one && resumed.quads.back().vertices[1][0] == 201 * one,
        "The resumed skid must use the newly visible wheel path with no endpoint retained from the distant body");
    for (const auto& vertex : resumed.quads.back().vertices) {
        expect(vertex[1] == 10 * one + 0xa3d, "Every resumed skid corner must use the newly sampled ground height");
    }
}

void splits() {
    SkidMarks marks(setup(), sector());
    std::array cars{car()};
    marks.update(cars);
    move(cars[0], 2 * one);
    marks.update(cars);
    expect(marks.frame().active == 1, "A straight two-unit strip stays current");
    move(cars[0], 3 * one);
    marks.update(cars);
    expect(marks.frame().persistent == 1 && marks.frame().active == 0, "The original lookup length at three exact units exceeds0x30000 and commits the strip");
    const auto end_left = marks.frame().quads[0].vertices[1], end_right = marks.frame().quads[0].vertices[2];
    move(cars[0], 3 * one + one / 2);
    marks.update(cars);
    expect(marks.frame().active == 1 && marks.frame().quads[1].vertices[0] == end_left && marks.frame().quads[1].vertices[3] == end_right,
        "A length-split segment reuses the exact old end vertices without a new rounding seam");
    move(cars[0], 3 * one + one / 2, one / 2);
    marks.update(cars);
    expect(marks.frame().persistent == 2 && marks.frame().active == 0, "A bend crossing sin[1080] commits the old segment");
    move(cars[0], 4 * one, one);
    marks.update(cars);
    const auto old_material = marks.frame().quads.back();
    cars[0].wheels[0].surface = 2;
    cars[0].wheels[0].ground_height = 1000;
    marks.update(cars);
    expect(marks.frame().persistent == 3 && marks.frame().active == 0 && marks.frame().quads[2].color == old_material.color
               && marks.frame().quads[2].vertices == old_material.vertices,
        "Surface changes close the old material at its previous endpoint before starting the new surface");
    move(cars[0], 4 * one + one / 2, one + one / 2);
    marks.update(cars);
    expect(marks.frame().active == 1 && marks.frame().quads.back().color == 0x781e1405 && marks.frame().quads.back().vertices[0][1] == 3621,
        "New grass material begins at the new HPC plane and uses the source brown-alpha120 color");

    SkidMarks normal_split(setup(), sector());
    cars = {car()};
    normal_split.update(cars);
    move(cars[0], one);
    normal_split.update(cars);
    const auto saved = normal_split.frame().quads.at(0);
    cars[0].support_normal = {0, 0, one};
    normal_split.update(cars);
    expect(normal_split.frame().persistent == 1 && normal_split.frame().quads[0].vertices == saved.vertices,
        "A support-plane turn commits the old quad before a new plane samples this tick's wheel");
}

void fade_and_colors() {
    constexpr std::array<std::uint32_t, 10> dry_gravel{0xdc5a4229, 0xb4784229, 0xb45a4229, 0xb48a7355, 0xb4705d44, 0xdc5a4229, 0xb45a4229, 0xb45a4229, 0xb4705d44, 0xb45a4229};
    for (std::uint32_t track = 0; track < 10; ++track) {
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            const auto config = setup(weather, track);
            SkidMarks marks(config, sector());
            std::array cars{car()};
            cars[0].wheels[0].surface = 3;
            marks.update(cars);
            move(cars[0], one);
            marks.update(cars);
            finish(marks, cars);
            const auto color = config.weather <= 2 ? dry_gravel[track] : track == 1 ? 0x963b1c11u : 0x96271c11u;
            expect(marks.frame().persistent == 1 && marks.frame().quads[0].color == color, "Every map/weather must retain its44ab90 gravel color, including forced-dry Lavaland");
            for (int tick = 0; tick < 80; ++tick) {
                marks.update({});
            }
            expect(marks.frame().quads[0].color == color, "Gravel strips persist even in rain; only road and curb have wet fading");
        }
    }
    for (const auto weather : {1u, 3u, 4u, 5u}) {
        SkidMarks marks(setup(weather), sector());
        std::array cars{car()};
        marks.update(cars);
        move(cars[0], one);
        marks.update(cars);
        finish(marks, cars);
        const auto initial = marks.frame().quads[0].color;
        expect(initial == (weather == 1 ? 0xa0000000 : 0x14969696), "Snow/rain/storm share modern wet colors while dry road is black");
        for (int tick = 0; tick < 49; ++tick) {
            marks.update({});
        }
        expect(marks.frame().quads[0].color == initial, "Wet road keeps full initial alpha for fifty updates including the commit tick");
        marks.update({});
        expect(marks.frame().quads[0].color == initial - (weather == 3 || weather == 5 ? 0x01000000 : 0),
            "Rain/storm fade by one alpha unit after the delay; snow preserves its wet-colored road strips");
        for (int tick = 0; tick < 40; ++tick) {
            marks.update({});
        }
        expect(marks.frame().persistent == 1 && (marks.frame().quads[0].color >> 24) == (weather == 3 || weather == 5 ? 0u : initial >> 24),
            "Transparent wet slots remain counted until overwritten, while non-rain strips never expire");
    }
}

void ring_and_lifecycle() {
    SkidMarks marks(setup(3), sector());
    std::array cars{car()};
    for (int strip = 0; strip < 101; ++strip) {
        cars[0].wheels[0].longitudinal_slip = one;
        move(cars[0], strip * 2 * one);
        marks.update(cars);
        move(cars[0], (strip * 2 + 1) * one);
        marks.update(cars);
        finish(marks, cars);
        if (strip == 99) {
            expect(marks.cursor() == 100, "Source ring cursor remains100 after the last write and wraps only at the next store");
            SkidMarks wrapped(setup(), sector(), marks.persistent_state());
            expect(wrapped.cursor() == 100, "Scene persistence must accept the source's between-writes cursor100");
        }
    }
    expect(marks.frame().persistent == 100 && marks.cursor() == 1 && marks.frame().emitted == 101 && marks.frame().quads[0].vertices[0][0] == 200 * one
               && marks.frame().quads[1].vertices[0][0] == 2 * one,
        "The hundred-slot ring overwrites slot zero and draws physical slot order rather than chronological order");
    const std::array<float, 16> source_pose{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto moved_pose = source_pose;
    moved_pose[12] = -50;
    const std::array poses{effect_pose(0, source_pose, moved_pose)};
    const auto rendered_ring = marks.presentation_frame(poses);
    for (std::size_t slot = 0; slot < 100; ++slot) {
        expect(rendered_ring.quads[slot].vertices == marks.frame().quads[slot].vertices && rendered_ring.quads[slot].color == marks.frame().quads[slot].color,
            "Overwriting a physical skid ring slot must not turn any saved quad into a moving presentation endpoint");
    }
    const auto old_slot = marks.frame().quads[0];
    const auto old_snapshot = marks.persistent_state();
    marks.reset();
    expect(marks.cursor() == 1 && marks.frame().quads.empty(), "44c3f0 grid reset clears counts but deliberately retains the ring write cursor");
    cars[0] = car();
    marks.update(cars);
    move(cars[0], one);
    marks.update(cars);
    finish(marks, cars);
    expect(marks.frame().persistent == 1 && marks.cursor() == 2 && marks.frame().quads[0].vertices == old_slot.vertices,
        "Source reset quirk: a retained cursor writes slot one while count one draws the old physical slot zero");
    SkidMarks next(setup(4), sector(), old_snapshot);
    expect(next.cursor() == 1 && next.frame().quads.empty() && next.persistent_state().records[0].quad.vertices == old_slot.vertices
               && next.persistent_state().records[0].quad.sector == 0xffff && !next.persistent_state().records[0].fading && next.persistent_state().records[0].delay == 0,
        "New-scene setup retains source cursor/geometry bytes, invalidates sectors and clears aging metadata");
}

void cadence() {
    SourceRandom random(7), untouched(7);
    SkidMarks first(setup(), sector()), second(setup(), sector());
    std::array cars{car()};
    for (int tick = 0; tick < 80; ++tick) {
        move(cars[0], tick * (one / 4));
        first.update(cars);
        second.update(cars);
        for (int draws = 0; draws < tick % 7; ++draws) {
            (void)second.frame();
        }
    }
    expect(random.next() == untouched.next() && first.frame().updates == second.frame().updates && first.frame().emitted == second.frame().emitted
               && first.frame().quads.size() == second.frame().quads.size(),
        "Skid updates use no RNG, and additional render frames or zero-update pauses cannot change history");
    for (std::size_t quad = 0; quad < first.frame().quads.size(); ++quad) {
        expect(first.frame().quads[quad].vertices == second.frame().quads[quad].vertices && first.frame().quads[quad].color == second.frame().quads[quad].color,
            "Identical simulation steps preserve every skid vertex and alpha across presentation rates");
    }
}

void presentation() {
    std::size_t sector_queries = 0;
    SkidMarks marks(setup(), [&](const auto&) {
        ++sector_queries;
        return std::uint16_t(7);
    });
    std::array cars{car()};
    marks.update(cars);
    move(cars[0], one);
    marks.update(cars);
    finish(marks, cars);
    cars[0].wheels[0].longitudinal_slip = one;
    move(cars[0], 4 * one);
    marks.update(cars);
    move(cars[0], 5 * one);
    marks.update(cars);
    const auto authoritative = marks.frame();
    const auto history = marks.persistent_state();
    const auto queries = sector_queries;
    const auto& active = authoritative.quads.back();
    SkidMarkFrame rendered;
    marks.presentation_frame_into(rendered, {});
    const auto storage = rendered.quads.data();
    expect(authoritative.persistent == 1 && authoritative.active == 1, "Skid presentation regression requires both a committed strip and a live wheel endpoint");
    const std::array<float, 16> source{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            auto displayed = source;
            displayed[12] = alpha - 1;
            displayed[13] = 50;
            const std::array poses{effect_pose(0, source, displayed)};
            marks.presentation_frame_into(rendered, poses);
            expect(rendered.quads.data() == storage, "Skid interpolation must reuse its warmed descriptor storage");
            const auto& current = rendered.quads.back();
            for (const auto endpoint : {1u, 2u}) {
                expect(std::abs(double(current.vertices[endpoint][0]) - (double(active.vertices[endpoint][0]) + double(alpha - 1) * one)) <= 1
                           && current.vertices[endpoint][1] == active.vertices[endpoint][1] && current.vertices[endpoint][2] == active.vertices[endpoint][2],
                    "Live skid endpoints must follow60/144Hz wheel displacement while staying on the existing ground plane");
            }
            expect(rendered.quads[0].vertices == authoritative.quads[0].vertices && current.vertices[0] == active.vertices[0] && current.vertices[3] == active.vertices[3]
                       && current.color == active.color && current.sector == active.sector && current.visibility == active.visibility,
                "Skid presentation must preserve stored marks, live segment starts, colors, sectors and view masks");
        }
    }
    const std::array<float, 16> rotated{0, 0, -2, 0, 0, 3, 0, 0, 4, 0, 0, 0, 10, 20, 30, 1};
    const std::array rotated_pose{effect_pose(0, source, rotated)};
    const auto turned = marks.presentation_frame(rotated_pose);
    for (const auto endpoint : {1u, 2u}) {
        const auto& point = active.vertices[endpoint];
        expect(turned.quads.back().vertices[endpoint] == SkidMarkVector{10 * one + 4 * point[2], point[1], 30 * one - 2 * point[0]},
            "Translated, rotated, scaled display poses must move only the live skid edge along its original support plane");
    }
    const auto after = marks.persistent_state();
    expect(
        marks.frame().quads.back().vertices == active.vertices && marks.frame().updates == authoritative.updates && marks.cursor() == history.cursor && sector_queries == queries,
        "Skid presentation must not update source queues, ring cursor, tick count or sector registration");
    for (std::size_t slot = 0; slot < history.records.size(); ++slot) {
        expect(after.records[slot].quad.vertices == history.records[slot].quad.vertices && after.records[slot].quad.color == history.records[slot].quad.color
                   && after.records[slot].delay == history.records[slot].delay && after.records[slot].fading == history.records[slot].fading,
            "Every saved skid ring slot and its rain-fade metadata must stay unchanged across presentation samples");
    }
    marks.reset();
    marks.presentation_frame_into(rendered, rotated_pose);
    expect(rendered.quads.empty() && rendered.active == 0 && rendered.persistent == 0 && rendered.quads.data() == storage,
        "Grid reset must clear reused skid geometry and counts while retaining capacity");

    SkidMarks sloped(setup(), sector());
    cars = {car()};
    cars[0].support_normal = {0, 0xcccc, 0x9999};
    sloped.update(cars);
    move(cars[0], one);
    sloped.update(cars);
    const auto original = sloped.frame().quads.front();
    auto displayed = source;
    displayed[13] = 50;
    displayed[14] = 1;
    const std::array slope_pose{effect_pose(0, source, displayed)};
    sloped.presentation_frame_into(rendered, slope_pose);
    expect(rendered.quads.size() == 1 && rendered.active == 1 && rendered.persistent == 0 && rendered.quads.data() == storage,
        "An empty reused skid destination must accept a smaller live queue without stale stored descriptors or reallocation");
    const auto projected = rendered.quads.front();
    for (const auto endpoint : {1u, 2u}) {
        const auto expected_lift = std::int32_t(std::llround(double(one) * cars[0].support_normal[2] / cars[0].support_normal[1]));
        expect(projected.vertices[endpoint][0] == original.vertices[endpoint][0] && projected.vertices[endpoint][2] == original.vertices[endpoint][2] + one
                   && projected.vertices[endpoint][1] == original.vertices[endpoint][1] - expected_lift,
            "Moving a live endpoint across a sloped surface must project onto its captured plane rather than following body height");
    }
}

void canonical(const std::filesystem::path& root) {
    const AssetStore assets(root);
    const auto resources = load_scene_resources(assets, setup());
    const HpcSurface surface(resources.collision_hpc);
    const BarrierData sectors(resources.route_points, resources.ground.sector_centers);
    const auto pose = initial_body_pose(surface, single_race_start(resources.grid), 0);
    std::size_t queries = 0;
    SkidMarks marks(resources.setup, [&](const auto& position) {
        ++queries;
        const auto found = sectors.sector(position);
        expect(found.has_value(), "Authored Donington grid skid vertices must be inside the source sector grid");
        return std::uint16_t(*found);
    });
    auto vehicle = car();
    vehicle.position = pose.position;
    vehicle.basis = pose.basis;
    vehicle.velocity = pose.basis[0];
    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
        vehicle.wheels[wheel].position = pose.contacts[wheel];
        vehicle.wheels[wheel].ground_height = pose.surfaces[wheel].height;
    }
    const auto start = surface.prop_sample(vehicle.wheels[0].position[0], vehicle.wheels[0].position[2]);
    expect(start.has_value(), "Canonical wheel origin must have an actual HPC plane");
    vehicle.support_normal = start->normal;
    std::array cars{vehicle};
    marks.update(cars);
    const auto original = cars[0].wheels[0];
    const auto delta = fixed::scale(pose.basis[0], one / 2);
    auto& wheel = cars[0].wheels[0];
    wheel.position = fixed::add(wheel.position, delta);
    const auto next = surface.prop_sample(wheel.position[0], wheel.position[2]);
    expect(next.has_value(), "Canonical half-unit grid movement must stay on authored HPC coverage");
    wheel.ground_height = next->height;
    cars[0].support_normal = next->normal;
    marks.update(cars);
    expect(marks.frame().active == 1, "A real Donington grid-plane wheel path must produce a current source skid quad");
    const auto quad = marks.frame().quads.at(0);
    const auto side = fixed::cross(fixed::normalized(vehicle.velocity), start->normal), offset = fixed::scale(side, 0x1999);
    const SkidMarkVector origin{original.position[0], fixed::add(original.ground_height, 0xa3d), original.position[2]};
    expect(quad.vertices[0] == fixed::sub(origin, offset) && quad.vertices[3] == fixed::add(origin, offset) && quad.sector == *sectors.sector(quad.vertices[0]) && queries == 1,
        "Canonical skid corners must retain actual wheel/HPC coordinates, fixed cross-products and47e370 sector identity");
    finish(marks, cars);
    expect(marks.frame().persistent == 1 && marks.frame().quads[0].vertices == quad.vertices && queries == 2,
        "Stopping canonical wheel slip finalizes the exact authored-ground quad through the same sector query");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            geometry_and_gates();
            visibility_lifecycle();
            splits();
            fade_and_colors();
            ring_and_lifecycle();
            cadence();
            presentation();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-skid-mark-tests [canonical-root]");
        }
        std::cout << "PASS skid-mark source geometry, gates, ring, fading, reset and cadence\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
