#include "simulation/source_scene_frame.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class F> bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

std::vector<std::uint8_t> ground_plane() {
    constexpr std::int32_t one = 65536;
    std::vector<std::uint8_t> bytes(106);
    const auto put16 = [&](std::size_t at, std::uint16_t value) {
        bytes.at(at) = std::uint8_t(value);
        bytes.at(at + 1) = std::uint8_t(value >> 8);
    };
    const auto put32 = [&](std::size_t at, std::int32_t value) {
        put16(at, std::uint16_t(value));
        put16(at + 2, std::uint16_t(std::uint32_t(value) >> 16));
    };
    put32(0, -128 * one);
    put32(4, -128 * one);
    put16(8, 1);
    put16(20, 2);
    put16(24, 4);
    constexpr std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * one, -64 * one}, {-64 * one, 64 * one}, {64 * one, -64 * one}, {64 * one, 64 * one}}};
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        put32(28 + vertex * 12, vertices[vertex][0]);
        put32(32 + vertex * 12, 2 * one);
        put32(36 + vertex * 12, vertices[vertex][1]);
    }
    put16(76, 2);
    constexpr std::array<std::array<std::uint16_t, 3>, 2> faces{{{0, 1, 2}, {2, 1, 3}}};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put16(80 + face * 8 + vertex * 2, faces[face][vertex]);
        }
    }
    put16(96, 2);
    put16(100, 1);
    put16(102, 1);
    put16(104, 1);
    return bytes;
}

void finish_results_and_warm_scene() {
    using namespace toca;
    using namespace toca::simulation;
    constexpr std::int32_t one = 65536;
    std::array<scene::SceneCamera, 2> cameras{};
    cameras[0].transform[12] = -30 * one;
    cameras[1].transform[12] = 30 * one;
    cameras[0].transform[13] = cameras[1].transform[13] = 10 * one;
    const auto roots = source_lifecycle_scene_roots({}, cameras);
    Terrain ground;
    for (std::int32_t row = -1; row <= 1; ++row) {
        for (std::int32_t column = -1; column <= 1; ++column) {
            ground.sector_centers.push_back({column * 64 * one, -row * 64 * one});
        }
    }
    SourceSceneFrame frames(scene::Visibility(ground), roots, 0, 15, 7, 3, 400000, {}, cameras, ground_plane());
    expect(frames.authored_root(0) == 0 && frames.authored_root(1) == 1 && frames.car_root(0) == 2 && frames.car_root(15) == 17,
        "Full lifecycle roots must retain510bf's authored actors before physical cars and camera anchors");
    expect(std::ranges::all_of(frames.retained_context().actors,
               [](const auto& actor) {
                   return actor.contact_effect_mask == 0 && actor.wheel_effect_mask == 1;
               }),
        "Cold HIGH nodes must remain hidden despite4469a0's independent producer masks initialized to1");
    ScenePhysicalBoundary body;
    body.before.basis = body.local_body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    body.positions[15] = {25 * one, 2 * one, 8 * one};
    std::uint32_t counter = 0;
    const auto physical = [&](SceneCameraControls controls = {}) {
        frames.begin_physical(body.before, body.session_state);
        frames.camera_controls(controls);
        frames.complete_physical(body);
    };
    const auto primary = [&](SceneLifecycleBoundary lifecycle, SceneCameraControls controls = {}, std::uint32_t elapsed = 400000) {
        frames.set_lifecycle(lifecycle);
        body.session_state = lifecycle.state;
        const auto count = frames.step().update_count();
        physical(controls);
        frames.publish_primary();
        for (std::uint32_t pass = 1; pass < count; ++pass) {
            physical(controls);
        }
        counter += elapsed;
        frames.finish(counter);
    };
    primary({6, 0}, {true, false});
    primary({6, 0});
    primary({6, 0}, {true, false});
    primary({6, 0});
    primary({6, 0}, {true, false});
    expect(frames.selected_view() == 1 && frames.car_masks()[15].car == 0, "The lifecycle must enter finish with the retained human cockpit and gates");
    primary({10, 0x1ffff}, {false, true});
    expect(!frames.published_state().authored && frames.selected_view() == 1 && !frames.rear_held() && frames.car_masks()[15].car == 0,
        "State10 before two seconds must keep the ordinary camera; its AI mapper cannot process C/Z");
    primary({10, 0x20000});
    expect(frames.published_state().authored && frames.published_state().authored_actor == 1 && frames.ancestor_root() == frames.authored_root(1) && frames.car_masks()[15].car == 1
               && frames.car_masks()[15].full == 1 && frames.retained_context().actors[15].wheel_effect_mask == 1,
        "The generated two-second finish threshold must select the nearest authored actor and expose the local model independently of retained View1");
    const auto cockpit_projection = scene::selected_view_projection(3, 640, 480, 1);
    expect(frames.projection().x == cockpit_projection.x && frames.projection().y == cockpit_projection.y,
        "448e90 must retain422f60's last ordinary projection when enabling the finish camera");
    const auto distant = frames.camera();
    primary({13, 0});
    const auto close = frames.camera();
    expect(frames.published_state().authored_actor == 1 && close.world_position != distant.world_position && close.world_position[1] >= 5.f,
        "Results must close the retained authored actor to fifteen units and clamp above source ground");
    expect(frames.projection().x == cockpit_projection.x && frames.projection().y == cockpit_projection.y,
        "45ed30 case13 must preserve the prior projection through the close camera transition");
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            expect(close.camera_to_world[row * 4 + column] == distant.camera_to_world[row * 4 + column], "Closing results must retain the authored look direction");
        }
    }
    body.positions[15][0] = -25 * one;
    for (unsigned call = 0; call < 60; ++call) {
        primary({13, 0});
        expect(frames.published_state().authored_actor == 1, "Phase13 must retain its outer hold despite the phase-change selector reset");
    }
    primary({13, 0}, {}, 800000);
    expect(frames.published_state().authored_actor == 0 && frames.step().extra_step,
        "After the actual results hold, phase13 must select the new nearest camera and preserve the source extra-pass clock");
    frames.set_lifecycle({13, 0});
    physical();
    frames.publish_primary();
    const auto published = frames.camera();
    body.positions[15][0] = 25 * one;
    physical();
    frames.finish(counter += 400000);
    expect(frames.camera().camera_to_world == published.camera_to_world, "The extra body must remain unpublished until the following actual renderer");
    frames.set_lifecycle({13, 0});
    frames.publish_retained();
    const auto retained_publication = frames.camera();
    expect(retained_publication.camera_to_world != published.camera_to_world,
        "A State13 menu-only renderer must consume the latest generated extra body without advancing physics or head state");
    frames.sample_idle(counter += 800000);
    expect(frames.camera().camera_to_world == retained_publication.camera_to_world && frames.step().extra_step,
        "Menu-only QPC must advance the clock while retaining its preceding renderer publication");
    const auto old_context = frames.retained_context();
    frames.warm_reset();
    std::array<scene::BodyPose, 16> grid{};
    grid[0].position = {1000 * one, 0, 0};
    grid[15].position = {12 * one, 2 * one, 8 * one};
    frames.warm_grid(grid, 1024, 400000);
    expect(frames.selected_view() == 1 && !frames.rear_held() && frames.step().extra_step && frames.retained_context().actors[15].wheel_effect_mask == 1
               && frames.car_masks()[15].full == 1,
        "Warm grid must retain view/input/head and current extra step while restoring4469a0 gates and allocated roots");
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& context = frames.retained_context().actors[actor];
        expect(context.contact_effect_mask == old_context.actors[actor].contact_effect_mask && frames.car_masks()[actor].full == 1,
            "Warm4469a0 must reset derived full-body masks while preserving each HIGH node's separate manual eligibility");
        expect(context.sector_visible == old_context.visibility.sector_visible(context.physical.sector),
            "Warm scene consumers must query the retained PVS using each newly generated grid registration");
    }
    expect(old_context.actors[0].sector_visible && !frames.retained_context().actors[0].sector_visible,
        "The warm visibility regression must cross from a selected physical sector to an unregistered grid position");
    body.positions[15] = grid[15].position;
    primary({9, 0}, {true, false});
    expect(!frames.published_state().authored && frames.selected_view() == 1 && frames.car_masks()[15].car == 0 && frames.step().extra_step,
        "Warm reset must clear authored selection, retain the held C edge, and preserve412a80's extra latch on its first sample");
    primary({9, 0});
    primary({6, 0}, {true, false});
    expect(frames.selected_view() == 2, "Warm human controls must resume from the retained view after a real release and new C edge");
    scene::FollowCamera expected(1024, false, 7, false, 3);
    //Two retained-extra passes in each of the first two warm frames, then one.
    for (unsigned pass = 0; pass < 5; ++pass) {
        expected.update(body.before, false);
    }
    auto expected_body = body.local_body;
    expected_body.position = body.positions[15];
    expect(frames.camera().camera_to_world == expected.frame(expected_body).camera_to_world,
        "460170 must reseed the follow heading from its generated grid while preserving the existing camera controller");

    scene::FinishCamera retained(cameras);
    const std::array<std::int32_t, 3> left{-25 * one, 2 * one, 8 * one}, right{25 * one, 2 * one, 8 * one};
    for (unsigned call = 0; call < 60; ++call) {
        retained.update(left, true);
    }
    retained.reset_session();
    retained.update(right, true);
    expect(retained.selected() == 1, "449f50 must retain case13's saturated results hold while resetting only the selected actor and selector countdown");
    scene::FinishCamera translated(cameras);
    translated.update(left, true);
    const HpcSurface surface(ground_plane());
    const auto original = translated.position();
    (void)translated.publish(left, &surface, true);
    const auto moved = translated.position();
    translated.reset_session();
    expect(moved != original && translated.position() == moved, "Warm reset must preserve the last26830 close node translation and restoration scratch");
    translated.update(left, true);
    expect(translated.position() == original, "The next actual267c0 results publication must restore its saved origin before selecting and closing again");
}

void hidden_body_keeps_physical_registration() {
    using namespace toca;
    using namespace toca::simulation;
    Terrain ground;
    for (std::int32_t row = -1; row <= 1; ++row) {
        for (std::int32_t column = -1; column <= 1; ++column) {
            ground.sector_centers.push_back({column * 64 * 65536, -row * 64 * 65536});
        }
    }
    SourceSceneFrame frames(scene::Visibility(ground), source_scene_roots({}), 0, 15, 0, 3, 400000);
    const auto root = frames.car_root(0);
    const auto initial_sector = frames.scene().root_registration(root).sector;
    ScenePhysicalBoundary body;
    body.before.basis = body.local_body.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    body.positions[0] = {64 * 65536, 0, 0};
    body.modes[0] = 4;
    frames.physical(body);
    expect(frames.scene().root_registration(root).sector == initial_sector && frames.car_masks()[0].car == 0,
        "Mode4's cleared manual parent must retain its previous physical registration during47f440(mode0)");
    frames.finish(400000);
    body.modes[0] = 0;
    frames.physical(body);
    expect(frames.scene().root_registration(root).sector != initial_sector && frames.car_masks()[0].car == 1,
        "A visible source body must resume migration from the retained sector without reallocating its root");
}

void qualifying_scene_frames() {
    using namespace toca;
    using namespace toca::simulation;
    constexpr std::int32_t one = 65536;
    std::array<scene::SceneCamera, 2> cameras{};
    cameras[0].transform[12] = -30 * one;
    cameras[1].transform[12] = 30 * one;
    cameras[0].transform[13] = cameras[1].transform[13] = 10 * one;
    Terrain ground;
    for (std::int32_t row = -1; row <= 1; ++row) {
        for (std::int32_t column = -1; column <= 1; ++column) {
            ground.sector_centers.push_back({column * 64 * one, -row * 64 * one});
        }
    }
    const auto roots = source_lifecycle_scene_roots({}, cameras);
    SourceSceneFrame frames(scene::Visibility(ground), roots, 0, 14, 7, 3, std::nullopt, {}, cameras);
    std::array<scene::BodyPose, 16> approach{};
    for (auto& body : approach) {
        body.position = {64 * one, 2 * one, 8 * one};
    }
    approach[14].position = {25 * one, 2 * one, 8 * one};
    const auto old_sector = frames.scene().root_registration(frames.car_root(0)).sector;
    frames.initialize_qualifying(approach, 0, -376);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const bool active = actor % 4 == 2;
        expect((frames.scene().root_manual_mask(frames.car_root(actor)) & 1) == active && frames.car_masks()[actor].full == 1,
            "443f60 must hide inactive parents independently of4469a0's initially enabled producer arrays");
    }
    expect(frames.scene().root_registration(frames.car_root(0)).sector != old_sector && frames.scene().root_registration(frames.car_root(2)).sector != old_sector,
        "44ffe1's explicit force flag must relocate every initial parent even after qualifying hides its inactive actor quarter");
    const auto hidden_sector = frames.scene().root_registration(frames.car_root(0)).sector;
    ScenePhysicalBoundary body;
    body.session_state = 4;
    body.before.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    body.local_body.basis = {{{0, 0, one}, {0, one, 0}, {-one, 0, 0}}};
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        body.positions[actor] = approach[actor].position;
        body.modes[actor] = actor % 4 == 2 ? 0 : 4;
    }
    body.positions[0] = {}; //A later hidden physical pose cannot migrate its root.
    const auto pass = [&](SceneCameraControls controls = {}) {
        frames.begin_physical(body.before, body.session_state);
        frames.camera_controls(controls);
        frames.complete_physical(body);
    };
    const auto frame = [&](SceneLifecycleBoundary lifecycle, SceneCameraControls controls = {}) {
        frames.set_lifecycle(lifecycle);
        body.session_state = lifecycle.state;
        frames.begin_observed_frame({});
        pass(controls);
        frames.publish_primary();
        frames.finish_observed_frame();
    };
    expect(rejects([&] {
        pass();
    }) && rejects([&] {
        frames.sample_idle(400000);
    }) && rejects([&] {
        frames.finish_observed_frame();
    }),
        "Caller-clocked components must require an observed frame and reject fabricated QPC samples");
    frames.begin_observed_frame({one, one, true});
    pass();
    frames.publish_primary();
    scene::FollowCamera expected(scene::qualifying_camera_heading(0), false, 7, false, 3);
    auto expected_body = body.local_body;
    expected_body.position = body.positions[14];
    expected.update(expected_body, true);
    expect(frames.camera().camera_to_world == expected.frame(expected_body).camera_to_world,
        "The first State4 renderer must replace placement heading with the authored track heading, then slew from the completed primary body");
    expect(frames.scene().root_registration(frames.car_root(0)).sector == hidden_sector,
        "After the forced qualifying initializer, ordinary Mode4 publication must retain the authored approach registration");
    expect(rejects([&] {
        frames.finish_observed_frame();
    }),
        "Observed extra-step cadence must still require the actual extra physical pass");
    const auto primary = frames.camera();
    body.local_body.basis = {{{-one, 0, 0}, {0, one, 0}, {0, 0, -one}}};
    pass();
    expect(frames.camera().camera_to_world == primary.camera_to_world, "State4 extras must advance neither the published camera nor45eb60's heading");
    frames.finish_observed_frame();
    frame({4, 0});
    expected_body.basis = body.local_body.basis;
    expected.update(expected_body, true);
    expect(frames.camera().camera_to_world == expected.frame(expected_body).camera_to_world,
        "Later State4 renderers must slew once from the primary post-body without repeating the map-heading reset");
    frame({6, 0}, {true, false});
    frame({6, 0});
    frame({6, 0}, {true, false});
    expect(frames.selected_view() == 4, "Qualifying controls must share the retained ordinary camera cycle after handoff");
    const auto projection = frames.projection();
    body.before.position = {-25 * one, 2 * one, 8 * one};
    frame({7, 0}, {false, true});
    expect(!frames.published_state().authored && frames.selected_view() == 4 && !frames.rear_held(), "State7 must initially retain the ordinary camera and ignore C/Z");
    frame({7, 0x20000});
    expect(frames.published_state().authored && frames.published_state().authored_actor == 0 && frames.camera().world_position == std::array<float, 3>{-30, 10, 0},
        "State7's first pre-physics selector must seed the thirty-call cache before the two-second renderer sees the later body");
    frame({8, 0}, {false, true});
    expect(frames.published_state().authored_actor == 1 && frames.camera().world_position == std::array<float, 3>{30, 10, 0} && frames.projection().x == projection.x
               && frames.projection().y == projection.y,
        "State8 must reset nearest selection on entry and use the regular authored camera without the State13 close branch");
    body.positions[14][0] = -25 * one;
    for (unsigned call = 0; call < 30; ++call) {
        frame({8, 0});
        expect(frames.published_state().authored_actor == 1, "Qualifying results must retain426b10's thirty-call cache");
    }
    frame({8, 0});
    expect(frames.published_state().authored_actor == 0, "Qualifying results must reselect after thirty calls without the separate State13 sixty-call hold");
    const auto old_context = frames.retained_context();
    frames.set_lifecycle({9, 0});
    frames.race_grid(approach, 1024);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect((frames.scene().root_manual_mask(frames.car_root(actor)) & 1) == 1 && frames.car_masks()[actor].full == 1
                   && frames.retained_context().actors[actor].contact_effect_mask == old_context.actors[actor].contact_effect_mask,
            "The earned race grid must restore every parent and producer gate while retaining mesh HIGH eligibility");
    }
    expect(frames.selected_view() == 4 && frames.projection().x == projection.x && frames.projection().y == projection.y,
        "Qualifying-to-grid must preserve view, projection and human input latches instead of constructing a new camera");
    body.modes.fill(0);
    body.before.position = approach[14].position;
    body.positions[14] = approach[14].position;
    frame({9, 0}, {true, false});
    scene::FollowCamera race_expected(1024, false, 7, false, 3);
    race_expected.cycle_view();
    race_expected.cycle_view();
    race_expected.update(body.before, false);
    expected_body.position = body.positions[14];
    expect(!frames.published_state().authored && frames.selected_view() == 4 && frames.camera().camera_to_world == race_expected.frame(expected_body).camera_to_world,
        "The first race-grid primary must clear authored mode, reseed grid heading and retain the held-C edge from qualifying");
    SourceSceneFrame owned(scene::Visibility(Terrain{}), source_scene_roots({}), 0, 14, 7, 3, 400000);
    expect(rejects([&] {
        owned.begin_observed_frame({});
    }) && rejects([&] {
        owned.finish_observed_frame();
    }),
        "Generated NoQual QPC ownership must reject the separate observed-frame component API");

    scene::SceneResources resources;
    resources.setup.selection.car_id = 7;
    resources.cameras.assign(cameras.begin(), cameras.end());
    for (std::int32_t row = -2; row <= 2; ++row) {
        for (std::int32_t column = -1; column <= 3; ++column) {
            resources.ground.sector_centers.push_back({column * 64 * one, -row * 64 * one});
        }
    }
    resources.route_points.resize(4 + 130 * 64);
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            resources.route_points[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    put32(0, 130);
    for (std::uint32_t point = 0; point < 130; ++point) {
        put32(4 + point * 64 + 12, point * one);
    }
    const auto actor0 = scene::qualifying_start(resources.route_points, 14, 0);
    const auto actor1 = scene::qualifying_start(resources.route_points, 14, 1);
    const auto local = scene::qualifying_start(resources.route_points, 14, 14);
    expect(actor0.source_index == 98 && actor0.xz[0] == 98 * one && actor1.source_index == 91 && local.source_index == 112,
        "443d30 must rotate POINTS starts by permanent actor identity, stepping seven records from count-18");
    SourceSceneFrame authored(resources, 14, std::nullopt, {}, true);
    for (auto& pose : approach) {
        pose.position = {-64 * one, 0, 0}; //Not an input for hidden canonical XZ.
    }
    authored.initialize_qualifying(approach, 0, 0);
    scene::Visibility expected_registration(resources.ground);
    const auto expected_root = expected_registration.register_root({98, 0, 0}, 0);
    expect(authored.scene().root_registration(authored.car_root(0)).sector == expected_registration.root_registration(expected_root).sector,
        "Hidden qualifying parent registration must come from canonical POINTS even when a supplied component pose contains another XZ");
}

void controlled_camera_frames() {
    using namespace toca;
    using namespace toca::simulation;
    const auto roots = source_scene_roots({});
    SourceSceneFrame frames(scene::Visibility(Terrain{}), roots, 0, 15, 7, 3, 400000);
    ScenePhysicalBoundary body;
    body.before.basis = body.local_body.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    body.positions[15] = {4 * 65536, 2 * 65536, 0};
    body.velocity = {65536, 0, 0};
    std::uint32_t counter = 0;
    const auto pass = [&](SceneCameraControls controls, std::uint16_t view, std::uint16_t projection, std::uint32_t sample_delta = 400000) {
        frames.begin_physical(body.before, 9);
        frames.camera_controls(controls);
        expect(frames.selected_view() == view && frames.projection_view() == projection && frames.rear_held() == (view == 5),
            "External C/Z state must select the source camera before physics callbacks");
        frames.complete_physical(body);
        frames.publish_primary();
        const auto ancestor = view == 1 ? 17u : view == 2 || view == 3 ? 16u : 15u;
        expect(frames.published_state().view == view && frames.published_state().projection_view == projection && frames.published_state().ancestor_root == ancestor,
            "Published camera controller must retain its actual head/follow/body ancestor");
        expect(frames.car_masks()[15].car == (view != 1) && frames.car_masks()[15].full == (view != 1) && frames.retained_context().actors[15].wheel_effect_mask == (view != 1)
                   && frames.retained_context().actors[15].render_mask != 0,
            "Cockpit model hiding must suppress local manual effects without hiding its physical parent");
        const auto expected = scene::selected_view_projection(3, 640, 480, projection);
        expect(frames.projection().x == expected.x && frames.projection().y == expected.y, "Rear hold must preserve the prior projection factor");
        counter += sample_delta;
        frames.finish(counter);
    };
    pass({true, false}, 3, 3);
    pass({true, false}, 3, 3); //Held C is not another source edge.
    pass({false, true}, 5, 3);
    pass({true, true}, 5, 3); //C while rear is held is ignored.
    pass({true, false}, 3, 3);
    pass({}, 3, 3);
    pass({true, false}, 4, 4);
    pass({}, 4, 4);
    pass({true, false}, 1, 1);
    pass({}, 1, 1);
    pass({false, true}, 5, 1);
    pass({true, false}, 1, 1); //Release Z restores the exact cockpit selection.
    pass({}, 1, 1);
    pass({true, false}, 2, 2);
    pass({}, 2, 2);
    pass({true, false}, 3, 3);
    pass({}, 3, 3, 800000); //Next primary has an extra physical pass.
    expect(frames.step().extra_step, "The controller snapshot case needs a generated extra pass");
    frames.begin_physical(body.before, 9);
    frames.camera_controls({});
    frames.complete_physical(body);
    frames.publish_primary();
    const auto primary = frames.camera();
    const auto primary_projection = frames.projection();
    frames.begin_physical(body.before, 9);
    frames.camera_controls({true, false});
    expect(frames.selected_view() == 4 && frames.published_state().view == 3 && frames.projection().x == primary_projection.x,
        "Extra-pass controls must update current callbacks while preserving the published controller and projection");
    frames.complete_physical(body);
    expect(frames.camera().camera_to_world == primary.camera_to_world, "Extra-pass cockpit/control work cannot republish the primary camera");
    frames.finish(counter + 400000);
}

} // namespace

int main() {
    try {
        using namespace toca;
        controlled_camera_frames();
        finish_results_and_warm_scene();
        hidden_body_keeps_physical_registration();
        qualifying_scene_frames();
        std::array<scene::GridSlot, 32> grid{};
        grid[0].first_xz = {-16861688, -3673931};
        grid[0].second_xz = {-16822344, -3451131};
        const auto authored = simulation::source_scene_roots(grid);
        expect(authored[0].local_position == std::array<float, 3>{-257.2889404296875f, 0, -56.05973815917969f},
            "Scene allocation must use the first SGRID edge, before later body midpoint/side/support placement");
        std::array<scene::SceneJournalRoot, 17> roots;
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            roots[actor].role = scene::SceneJournalRootRole::car_parent;
            roots[actor].actor = actor;
        }
        roots[16].role = scene::SceneJournalRootRole::follow_anchor;
        simulation::SourceSceneFrame frames(scene::Visibility(Terrain{}), roots, 0, 15, 0, 3, 400000);
        std::array<scene::BodyPose, 16> placed{};
        placed[15].position = {3 * 65536, -32000 * 65536, 7 * 65536};
        frames.initialize_grid(placed);
        expect(frames.retained_context().actors[15].render_mask == 1 && frames.car_masks()[15].full == 1 && frames.retained_context().actors[15].contact_effect_mask == 0,
            "The cold grid must retain4469a0's active render/effect gates before the first model refresh");
        simulation::ScenePhysicalBoundary body;
        body.before.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
        body.positions[15] = {4 * 65536, 2 * 65536, 0};
        frames.physical(body);
        const auto initial = frames.camera();
        expect(initial.world_position[0] != 0, "Primary scene publication must use the completed physical translation");
        expect(rejects([&] {
            frames.physical(body);
        }),
            "The initial cold frame cannot invent an extra physical pass");
        frames.finish(1000000); //412ab0 first sample retains the one-step clock.
        frames.physical(body);
        frames.finish(1800000); //Two nominal ticks yield two half-delta passes.
        expect(frames.step().extra_step && frames.step().delta_q16 == 65536, "External QPC samples must generate the extra-pass decision");
        frames.physical(body);
        const auto primary = frames.camera();
        const auto retained = frames.scene().context();
        expect(rejects([&] {
            frames.finish(2200000);
        }),
            "The source frame cannot finish before its generated extra pass");
        body.positions[15] = {100 * 65536, 2 * 65536, 0};
        frames.physical(body);
        expect(frames.camera().camera_to_world == primary.camera_to_world && frames.scene().context().actors[15].physical.sector == retained.actors[15].physical.sector,
            "The extra physical pass follows publication and must preserve its camera and scene snapshot");
        frames.finish(2200000);
        expect(!frames.step().extra_step, "A later ordinary QPC interval must clear the extra pass");
        frames.physical(body);
        expect(frames.camera().world_position[0] != primary.world_position[0], "The next primary frame must publish the new body position");
        std::cout << "Source scene frames passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
