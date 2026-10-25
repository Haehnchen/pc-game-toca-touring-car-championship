#include "assets/store.hpp"
#include "scene/barriers.hpp"
#include "scene/catalog.hpp"
#include "scene/fleet_scene.hpp"
#include "scene/frame_journal.hpp"
#include "scene/resources.hpp"
#include "scene/terrain_lighting_cache.hpp"
#include "scene/visibility.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::int32_t one = 65536;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Terrain grid(std::uint32_t size = 15) {
    Terrain result;
    for (std::uint32_t row = 0; row < size; ++row) {
        for (std::uint32_t column = 0; column < size; ++column) {
            result.sector_centers.push_back({(std::int32_t(column) - std::int32_t(size / 2)) * 64 * one, (std::int32_t(size / 2) - std::int32_t(row)) * 64 * one});
        }
    }
    return result;
}

void source_frame_journal() {
    SceneFrameJournal journal{Visibility(grid())};
    std::array<SceneJournalRoot, 17> roots;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        roots[actor].role = SceneJournalRootRole::car_parent;
        roots[actor].actor = actor;
    }
    roots[15].local_position = {128, 0, 0};
    roots[16].role = SceneJournalRootRole::follow_anchor;
    journal.initialize(0, roots);
    journal.migrate(3, 5, {64, 0, 0});
    journal.migrate(4, 16, {128, 0, 0});
    const std::array<std::uint32_t, 1> ancestors{16};
    journal.select(7, {identity, {0, 0, 0}, default_projection(3, 640, 480)}, ancestors);
    SceneJournalModels models;
    models.local_actor = 15;
    models.body_positions[15] = {128 * one, 0, 0};
    models.body_positions[5] = {64 * one, 0, 0};
    journal.models(9, models);
    journal.migrate(10, 5, {1000, 0, 0}); //Actual geometry migration after PVS/model selection.
    journal.publish(12);
    const auto previous = journal.context();
    expect(previous.actors[0].model_detail == 2 && previous.actors[5].physical.sector == no_visibility_sector && previous.actors[5].render_mask == 15
               && journal.car_masks()[5].full == 1 && std::ranges::find(previous.visibility.ordered_sectors, 114) != previous.visibility.ordered_sectors.end(),
        "Actual scene operations must derive View2 LOD from the local body, resolve ancestor identity and publish post-selection migration without importing outputs");
    models.body_positions[15] = {64 * one, 0, 0};
    models.body_modes[5] = 1;
    journal.select(15, {identity, {0, 0, 0}, default_projection(3, 640, 480)}, ancestors);
    journal.models(17, models);
    journal.publish(19);
    expect(journal.context().actors[5].model_detail == 0 && previous.actors[5].model_detail == 2 && journal.car_masks()[5].full == 0 && journal.car_masks()[5].car == 1
               && journal.car_masks()[5].high_medium == 1,
        "A second actual refresh needs no physical-update inference; canonical force1 changes LOD immediately while source full/body effect gates remain distinct");
    const auto rejected = [](auto action) {
        try {
            action();
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    expect(rejected([&] {
        journal.publish(20);
    }) && rejected([&] {
        journal.external_mask(21, 5, 0);
    }) && rejected([&] {
        journal.migrate(19, 5, {0, 0, 0});
    }),
        "Journal replay must reject missing operation phases, observed car-mask imports and reordered invocations");
    journal.select(22, {identity, {0, 0, 0}, default_projection(3, 640, 480)}, ancestors);
    models.view = 0;
    expect(rejected([&] {
        journal.models(23, models);
    }),
        "Authored camera profiles must remain outside the ordinary controller journal");
    expect(fleet_scene_observer(2, false, {5, 6, 7}, {1, 2, 3}) == std::array<float, 3>{1, 2, 3}
               && fleet_scene_observer(2, true, {5, 6, 7}, {1, 2, 3}) == std::array<float, 3>{5, 6, 7}
               && fleet_scene_observer(3, false, {5, 6, 7}, {1, 2, 3}) == std::array<float, 3>{1, 2, 3},
        "Both chase cameras must use the source zero-factor body observer");
}

void source_view_observers() {
    const std::array<std::int32_t, 3> body{10 * one, 2 * one, 30 * one};
    const std::array<float, 3> camera{11, 20, 29};
    expect(source_fleet_scene_observer(1, camera, body, {0, 0, one}) == std::array<std::int32_t, 3>{10 * one, 2 * one, 34 * one}
               && source_fleet_scene_observer(4, camera, body, {0, 0, one}) == std::array<std::int32_t, 3>{30 * one, 2 * one, 10 * one}
               && source_fleet_scene_observer(5, camera, body, {0, 0, one}) == std::array<std::int32_t, 3>{20 * one, 2 * one, 20 * one},
        "Cockpit/bonnet/rear observers must use the original separate forward and interpolation factors");
    expect(source_fleet_scene_observer(1, {}, {16777217, 0, 0}, {one, 0, 0})[0] == 17039359,
        "Cockpit LOD must preserve the body-parent float publication before both fixed multiplications");
    const std::array<std::int32_t, 3> moving_body{-10098926, 383958, -5220496};
    const std::array<float, 3> previous_parent{float(-10025561) / one, float(384020) / one, float(-5233294) / one};
    expect(source_fleet_scene_observer(1, {}, moving_body, {-64552, -140, 11336}, previous_parent) == std::array<std::int32_t, 3>{-10210404, 383958, -5200748},
        "Cockpit observer must consume the retained car-parent world position before the post-model migration");
    FleetScene fleet;
    FleetSceneView view{{500, 0, 0}, 3, true, true, std::array<std::int32_t, 3>{500 * one, 0, 0}};
    const std::array rivals{CarScenePose{1, {520, 0, 0}, 0, true, 1, std::array<std::int32_t, 3>{520 * one - 1, 0, 0}}};
    fleet.select_models(view, rivals, true);
    expect(fleet.rival(1).model_detail == 0, "Source LOD must preserve the fixed actor/observer displacement at a threshold lost by float world positions");
}

void fleet_scene_context() {
    const Visibility visibility(grid());
    VisibilityFrame selected;
    selected.selected.resize(225);
    selected.selected[112] = 1;
    selected.ordered_sectors = {112};
    FleetScene fleet;
    FleetSceneView view{{64, 0, 0}, 3, false};
    const CarScenePose player{0, {0, 0, 0}, 0};
    std::array rivals{CarScenePose{5, {64, 0, 0}, 0}};
    fleet.update(visibility.data(), selected, view, player, rivals);
    const auto previous = fleet.context(15);
    const auto& actor = previous.actors[5];
    expect(previous.actors[15].physical.sector == 112 && actor.physical.sector == 113 && !actor.sector_visible && actor.render_mask == 0 && actor.model_detail == 0
               && actor.contact_effect_mask == 1 && actor.wheel_effect_mask == 1 && actor.engine_effect_mask == 1 && previous.actors[15].contact_effect_mask == 0
               && previous.actors[15].wheel_effect_mask == 1,
        "One atomic fleet snapshot must bind the player identity and keep physical/high-detail/effect eligibility independent of PVS and cockpit hiding");
    rivals[0].position[0] = 200;
    fleet.update(visibility.data(), selected, view, player, rivals);
    expect(fleet.rival(5).model_detail == 2 && fleet.rival(5).contact_effect_mask == 0 && fleet.rival(5).wheel_effect_mask == 1 && previous.actors[5].model_detail == 0
               && previous.actors[5].physical.sector == 113,
        "A later refresh may change LOD/contact eligibility without rewriting the previous simulation/effect snapshot");
    rivals[0].position[0] = 64;
    fleet.reset_registration();
    for (unsigned hold = 0; hold < 10; ++hold) {
        fleet.update(visibility.data(), selected, view, player, rivals);
        expect(fleet.rival(5).model_detail == 2, "Physical registration reset and snapshot reads must preserve the ten-dispatch LOD hold");
        (void)fleet.context(15);
    }
    fleet.update(visibility.data(), selected, view, player, rivals);
    expect(
        fleet.rival(5).model_detail == 0 && fleet.rival(5).contact_effect_mask == 1, "The first eligible refresh after ten dispatches must restore the high-detail contact mask");
    rivals[0].position[0] = 1000;
    fleet.update(visibility.data(), selected, view, player, rivals);
    rivals[0].position[0] = 64;
    fleet.update(visibility.data(), selected, view, player, rivals);
    expect(fleet.rival(5).physical.sector == no_visibility_sector, "A root that left the registered grid must not silently register again on reentry");
    fleet.reset_registration();
    fleet.update(visibility.data(), selected, view, player, rivals);
    expect(fleet.rival(5).physical.sector == 113, "An explicit scene/grid registration reset may initialize the returning root");

    FleetScene budget;
    std::array<CarScenePose, 6> nearby;
    for (std::size_t index = 0; index < nearby.size(); ++index) {
        nearby[index] = {std::uint32_t(index), {65.f + float(index), 0, 0}, 0};
    }
    budget.update(visibility.data(), selected, view, player, nearby);
    expect(budget.rival(3).model_detail == 0 && budget.rival(4).model_detail == 1 && budget.rival(5).model_detail == 1,
        "The shared owner must retain the existing four-near-car budget and stable actor-distance ordering");
    nearby[0].manual_mask = 0;
    budget.update(visibility.data(), selected, view, player, nearby);
    expect(budget.rival(0).contact_effect_mask == 0 && budget.rival(0).wheel_effect_mask == 0 && budget.rival(0).physical.sector == 113,
        "Manual child hiding must disable effects without removing the independent physical parent registration");
    selected.selected[113] = 1;
    budget.update(visibility.data(), selected, view, player, {});
    expect(budget.rival(0).sector_visible && budget.rival(0).render_mask == 0 && budget.rival(0).physical.sector == 113,
        "An absent actor must retain physical registration while its snapshot uses the current selection and clears model masks");
    for (auto& pose : nearby) {
        pose.position = {65, 0, 0};
    }
    budget.select_models(view, nearby, true);
    expect(budget.rival(0).model_detail == 1 && budget.rival(1).model_detail == 1 && budget.rival(2).model_detail == 0,
        "Canonical forced model budgets must downgrade the first actor on equal farthest distances");
}

TerrainChunk chunk(std::uint32_t node, std::array<std::int32_t, 2> origin, VisibilityBounds bounds) {
    TerrainChunk result;
    result.node_index = node;
    result.origin_xz = origin;
    result.mesh.vertices = {{{bounds.minimum_x, 50, bounds.minimum_z}, {}, 0, 0}, {{bounds.maximum_x, -50, bounds.maximum_z}, {}, 0, 0}};
    return result;
}

VisibilityCamera camera(std::uint32_t detail = 3, std::uint32_t width = 640, std::uint32_t height = 480) {
    return visibility_camera(identity, default_projection(detail, width, height));
}

bool draws_root(const VisibilityFrame& frame, std::uint32_t root) {
    return std::ranges::any_of(frame.draws, [&](const auto& entry) {
        return entry.dynamic && entry.id == root;
    });
}

void horizontal_clip() {
    auto view = camera();
    view.projection.x = 1;
    expect(visibility_bounds_in_view({-1, 10, 1, 12}, view), "A corner inside both original horizontal clip walls admits its sector");
    expect(!visibility_bounds_in_view({20, 10, 22, 12}, view), "A sector entirely beyond one wall is rejected");
    expect(!visibility_bounds_in_view({-1, -12, 1, -10}, view), "A narrow sector entirely behind the eye fails the original test");
    expect(visibility_bounds_in_view({-100, 10, 100, 12}, view), "Crossing both clip walls admits a sector even when no corner lies inside");
    expect(visibility_bounds_in_view({-100, -12, 100, -10}, view),
        "47cf40 also admits a wide behind-eye rectangle crossing both walls; a conventional frustum replacement would change this");
    expect(!visibility_bounds_in_view({10, 10, 10, 10}, view), "Exact wall equality is excluded by the original strict tests");
    expect(visibility_bounds_in_view({0, .01f, .001f, .02f}, view) && visibility_bounds_in_view({0, 10000, 1, 10001}, view),
        "Sector selection adds neither a near plane nor a far plane to the original horizontal-only test");
    // X/Z~.89 lies beyond4:3's wall (~.78), but inside both16:10
    // (~.94) and16:9 (~1.04). X/Z=1 would still be outside16:10.
    const VisibilityBounds side{80, 90, 81, 91};
    expect(!visibility_bounds_in_view(side, camera(3, 640, 480)) && visibility_bounds_in_view(side, camera(3, 1280, 720)) && visibility_bounds_in_view(side, camera(3, 1280, 800)),
        "Widescreen selection must use each actual projection rather than preserve a hidden4:3 culling wall");
    auto transform = identity;
    transform[0] = 2;
    transform[5] = 3;
    transform[10] = 4;
    transform[12] = -20;
    transform[13] = -60;
    transform[14] = -120;
    expect(visibility_camera(transform, view.projection).world_position == std::array<float, 3>{10, 20, 30},
        "Camera extraction must invert the full affine basis, including non-unit lookup bases");
}

void spiral_and_levels() {
    auto ground = grid(9);
    Visibility visibility(ground);
    auto view = camera();
    view.projection.x = 1.25f;
    const auto first = visibility.select(view);
    constexpr std::array<std::uint16_t, 6> prefix{40, 31, 32, 49, 30, 21};
    expect(first.ordered_sectors.size() > prefix.size() && std::equal(prefix.begin(), prefix.end(), first.ordered_sectors.begin()),
        "47d3d0 must visit the original up/right/down/left growing spiral and retain admitted sectors in that order");
    expect(!first.sector_visible(no_visibility_sector) && first.terrain_sectors.empty(), "Invalid sectors are bounded and empty ground sectors do not become mesh draws");

    ground = grid();
    for (std::uint32_t row = 0; row < 15; ++row) {
        for (std::uint32_t column = 0; column < 15; ++column) {
            const auto index = row * 15 + column;
            const auto center = ground.sector_centers[index];
            ground.chunks.push_back(chunk(index + 1, center, {float(center[0]) / one - 32, float(center[1]) / one - 32, float(center[0]) / one + 32, float(center[1]) / one + 32}));
        }
    }
    Visibility levels(ground);
    const std::array<float, 3> at_two{0, 0, 128}, at_five{0, 0, 320};
    const auto low = levels.register_root(at_two, 0x04000000), medium = levels.register_root(at_two, 0x02000000);
    const auto regular = levels.register_root(at_five, 0), far = levels.register_root(at_five, 0x01000000);
    const auto two_sector = levels.root_sector(low), three_sector = levels.data().sector({0, 0, 192 * one});
    const auto five_sector = levels.root_sector(far), six_sector = levels.data().sector({0, 0, 384 * one});
    const auto high = levels.select(camera());
    expect(high.detail[two_sector] == 1 && high.detail[three_sector] == 2 && high.detail[five_sector] == 3 && high.detail[six_sector] == 4,
        "Compiled detail thresholds2/3 and projection-updated5/6 sectors use strict squared-distance comparisons");
    expect(!draws_root(high, low) && draws_root(high, medium) && !draws_root(high, regular) && draws_root(high, far) && high.root_mask(low) == 15 && high.root_mask(regular) == 15,
        "Root limits1/2/3/4 affect model submission while the pre-LOD camera masks still admit original actor effects");
    expect(std::ranges::find(high.terrain_sectors, five_sector) == high.terrain_sectors.end(), "Terrain stops at detail3 even when a far root remains drawable");
    const auto low_detail = levels.select(camera(0));
    expect(low_detail.detail[three_sector] == 3, "150-unit projection must set the original final thresholds3/4 without changing2/3");
    levels.select(camera());
    auto reverse = camera();
    reverse.world_to_view[0] = -1;
    reverse.world_to_view[10] = -1;
    const std::array force{five_sector};
    const auto forced = levels.select(reverse, force);
    expect(forced.selected[five_sector] && forced.ordered_sectors.back() == five_sector && forced.detail[five_sector] == 3,
        "47dd90 appends the camera ancestry sector without replacing its retained detail byte");
    auto remote = camera();
    remote.world_position = {10000, 0, 10000};
    remote.world_to_view[12] = remote.world_to_view[14] = -10000;
    const auto empty = levels.select(remote);
    expect(empty.ordered_sectors.empty() && empty.sector_visible(two_sector) && empty.root_mask(low) == 15 && empty.draws.empty(),
        "An empty47db00 selection admits effect/actor masks while the sector draw loop itself remains empty");
    const auto all = levels.select(remote, {}, true);
    expect(all.ordered_sectors.size() == 225 && all.terrain_sectors.size() == 225
               && std::ranges::all_of(all.detail,
                   [](auto level) {
                       return level == 0;
                   }),
        "47e340 override submits all sectors in file order at level0");
}

void mega_distances() {
    auto ground = grid(25);
    for (std::size_t index = 0; index < ground.sector_centers.size(); ++index) {
        const auto center = ground.sector_centers[index];
        ground.chunks.push_back(
            chunk(std::uint32_t(index + 1), center, {float(center[0]) / one - 32, float(center[1]) / one - 32, float(center[0]) / one + 32, float(center[1]) / one + 32}));
    }
    Visibility visibility(ground);
    const auto short_boundary = visibility.register_root({0, 0, 256}, 0x04000000), medium_boundary = visibility.register_root({0, 0, 384}, 0x02000000);
    const auto short_earlier = visibility.register_root({0, 0, 192}, 0x04000000), medium_earlier = visibility.register_root({0, 0, 320}, 0x02000000);
    const auto regular_boundary = visibility.register_root({0, 0, 576}, 0), far_boundary = visibility.register_root({0, 0, 640}, 0x01000000);
    const auto terrain_sector = visibility.data().sector({0, 0, 448 * one});
    const auto high = visibility.select(camera(3));
    expect(!draws_root(high, short_earlier) && !draws_root(high, medium_earlier) && std::ranges::find(high.terrain_sectors, terrain_sector) == high.terrain_sectors.end(),
        "Very High must retain the original small-object and terrain distances");
    const auto mega = visibility.select(camera(4));
    expect(draws_root(mega, short_earlier) && draws_root(mega, medium_earlier) && std::ranges::find(mega.terrain_sectors, terrain_sector) != mega.terrain_sectors.end(),
        "Mega must submit distant short/medium props and terrain before they enter the original draw ranges");
    expect(mega.detail[visibility.root_sector(short_boundary)] == 1 && mega.detail[visibility.root_sector(medium_boundary)] == 2
               && mega.detail[visibility.root_sector(regular_boundary)] == 3 && mega.detail[visibility.root_sector(far_boundary)] == 4,
        "Mega must double short thresholds to4/6 sectors and derive terrain/long thresholds9/10 from its500-unit projection");
    expect(!draws_root(mega, short_boundary) && !draws_root(mega, medium_boundary) && !draws_root(mega, regular_boundary) && !draws_root(mega, far_boundary),
        "All four Mega root limits must retain strict equality rejection");
    for (const float position : {-.25f, .25f}) {
        auto view = camera(4);
        view.world_position[2] = position;
        view.world_to_view[14] = -position;
        const auto& frame = visibility.select(view);
        expect(draws_root(frame, short_boundary) == (position > 0) && draws_root(frame, medium_boundary) == (position > 0) && draws_root(frame, regular_boundary) == (position > 0)
                   && draws_root(frame, far_boundary) == (position > 0),
            "Mega props must appear only after crossing each extended distance boundary toward their sector");
    }
}

void registration_and_motion() {
    auto ground = grid(9);
    ground.chunks.push_back(chunk(41, {0, 0}, {-2, -2, 2, 2}));
    Visibility visibility(ground);
    Terrain baked;
    baked.chunks = {chunk(0, {0, 0}, {-100, -10, 5, 5}), chunk(1, {0, 0}, {-10, -5, 200, 10}), chunk(2, {64 * one, 0}, {0, -500, 500, 500})};
    auto ids = visibility.register_static(baked);
    Terrain overwritten;
    overwritten.chunks = {chunk(0, {0, 0}, {-300, -300, 300, 300}), chunk(0, {0, 0}, {-3, -3, 3, 3})};
    overwritten.chunks.back().variant_index = 1;
    const auto last = visibility.register_static(overwritten);
    expect(last[0] == no_visibility_entry && last[1] == 3, "494a40 allocates one static entry per node and retains its last valid variant");
    expect(visibility.data().sectors[40].static_entries == std::vector<std::uint32_t>{3, ids[1], ids[0]},
        "Each static registration is prepended across resource families, preserving the reverse original allocation order");
    const auto bounds = visibility.data().sectors[40].bounds, empty_bounds = visibility.data().sectors[41].bounds;
    expect(bounds.minimum_x == -100 && bounds.maximum_x == 200 && bounds.minimum_z == -10 && bounds.maximum_z == 10 && empty_bounds.minimum_x == 32 && empty_bounds.maximum_x == 96
               && empty_bounds.minimum_z == -32 && empty_bounds.maximum_z == 32,
        "47de00 expands existing ground bounds by registered static geometry but never expands a sector with no ground mesh");
    const auto a = visibility.register_root({0, 0, 0}, 0), b = visibility.register_root({0, 0, 64}, 0);
    visibility.update_root(a, {0, 0, 64});
    auto frame = visibility.select(camera(), {}, true);
    std::vector<std::uint32_t> roots;
    for (const auto& draw : frame.draws) {
        if (draw.dynamic && draw.sector == 31) {
            roots.push_back(draw.id);
        }
    }
    expect(roots == std::vector<std::uint32_t>{b, a}, "Moving a root to another sector removes its old link and appends after existing roots");
    visibility.update_root(a, {0, 0, 0}, 0);
    expect(visibility.root_sector(a) == 31 && visibility.select(camera(), {}, true).root_mask(a) == 0,
        "Hidden roots preserve their prior source registration and clear their camera mask");
    visibility.update_root(a, {0, 0, 0}, 2);
    frame = visibility.select(camera(), {}, true);
    expect(frame.root_mask(a) == 2 && !draws_root(frame, a), "A nonzero manual mask still needs the current view bit before a root can draw");
    visibility.update_root(a, {10000, 0, 0});
    visibility.update_root(a, {0, 0, 0});
    expect(visibility.root_sector(a) == no_visibility_sector && !draws_root(visibility.select(camera(), {}, true), a),
        "After a root leaves the grid,47f500 no longer calls migration for itsffff registration");
    const auto round_boundary = visibility.register_root({32.f - 1.f / (4 * one), 0, 0}, 0);
    expect(visibility.root_sector(round_boundary) == 40, "Initial47de00 registration truncates the local float translation");
    visibility.update_root(round_boundary, {32.f - 1.f / (4 * one), 0, 0});
    expect(visibility.root_sector(round_boundary) == 41, "47e3c0 migration rounds the later world translation, including a sub-raw-unit sector crossing");

    Visibility initialized(ground);
    const auto first = initialized.register_root({10000, 0, 0}, 0), second = initialized.register_root({0, 0, 0}, 0);
    const std::array<std::array<float, 3>, 2> positions{{{0, 0, 64}, {0, 0, 64}}};
    initialized.initialize_roots(positions);
    const auto& initial_frame = initialized.select(camera(), {}, true);
    std::vector<std::uint32_t> initial_order;
    for (const auto& draw : initial_frame.draws) {
        if (draw.dynamic) {
            initial_order.push_back(draw.id);
        }
    }
    expect(initialized.root_sector(first) == 31 && initialized.root_sector(second) == 31 && initial_order == std::vector<std::uint32_t>{first, second},
        "47de00 initial body/follow positions must build lists in root allocation order, independent of earlier model-space origins");

    Visibility no_grid(Terrain{});
    const auto no_grid_static = no_grid.register_static(baked);
    const auto no_grid_root = no_grid.register_root({10000, 0, 0}, 0x04000000);
    const auto& global = no_grid.select(camera());
    expect(global.draws.size() == 4 && global.draws[0].id == no_grid_static[0] && global.draws[1].id == no_grid_static[1] && global.draws[2].id == no_grid_static[2]
               && global.draws[3].dynamic && global.draws[3].id == no_grid_root && global.sector_visible(7) && !global.sector_visible(no_visibility_sector),
        "489069's explicit no-grid branch retains forward static/actor allocation order without spatial or distance filtering");
}

void independent_copies() {
    auto ground = grid(9);
    ground.chunks.push_back(chunk(41, {0, 0}, {-2, -2, 2, 2}));
    Visibility simulation(ground);
    Terrain baked;
    baked.chunks = {chunk(0, {0, 0}, {-10, -10, 10, 10})};
    simulation.register_static(baked);
    const auto first = simulation.register_root({0, 0, 0}, 0), second = simulation.register_root({0, 0, 64}, 0x04000000);
    const auto expected = simulation.select(camera(), {}, true);
    auto presentation = simulation;
    expect(&presentation.data() == &simulation.data(), "Presentation copies must share the registered sector source rather than copy each static-entry vector");
    presentation.update_root(first, {0, 0, 64});
    presentation.update_root(second, {0, 0, 64}, 0);
    presentation.select(camera(0));
    expect(simulation.root_sector(first) == 40 && simulation.root_sector(second) == 31 && simulation.frame().root_mask(second) == 15
               && simulation.frame().ordered_sectors == expected.ordered_sectors && simulation.frame().detail == expected.detail,
        "Presentation migration, manual masks and selection must leave the simulation registrations and retained detail untouched");
    expect(&presentation.data() == &simulation.data(), "Migration and selection must keep their shared source immutable");

    // Assignment is the per-frame path: it must discard prior presentation
    // migration/order and restore the simulation's independent mutable state.
    presentation = simulation;
    const auto restored = presentation.select(camera(), {}, true);
    expect(restored.root_masks == expected.root_masks && restored.detail == expected.detail && restored.selected == expected.selected
               && std::ranges::equal(restored.draws, expected.draws,
                   [](const auto& a, const auto& b) {
                       return a.id == b.id && a.sector == b.sector && a.dynamic == b.dynamic;
                   }),
        "Assigning the next presentation frame must restore exact source draw order, root masks and retained selection state");

    const auto next = presentation.register_static(baked);
    expect(&presentation.data() != &simulation.data() && presentation.data().sectors[40].static_entries == std::vector<std::uint32_t>{next[0], 0}
               && simulation.data().sectors[40].static_entries == std::vector<std::uint32_t>{0},
        "Static allocation after a copy must detach bounds and static-list ownership while preserving each copy's prepend order");
    const auto extra = presentation.register_root({0, 0, 128}, 0x02000000);
    const auto separate = simulation.register_root({0, 0, 128}, 0x04000000);
    expect(extra == separate && draws_root(presentation.select(camera()), extra) && !draws_root(simulation.select(camera()), separate),
        "Root allocation after a copy must retain independent source LOD limits even when the new root IDs are equal");
    simulation.update_root(first, {0, 0, 64});
    expect(presentation.root_sector(first) == 40, "Later simulation migration must not change presentation registration or linked-list order");

    Visibility no_grid(Terrain{});
    auto no_grid_copy = no_grid;
    no_grid_copy.register_static(baked);
    const auto anchor = no_grid_copy.register_root({0, 0, 0}, 0x04000000);
    const auto& global = no_grid_copy.select(camera());
    expect(no_grid.select(camera()).draws.empty() && global.draws.size() == 2 && !global.draws[0].dynamic && global.draws[1].dynamic && global.draws[1].id == anchor,
        "Copied scenes without a grid must detach their global static/root allocations while retaining source allocation draw order");
}

void lighting_cache() {
    TerrainChunk first, second;
    first.mesh.vertices.resize(2);
    second.mesh.vertices.resize(2);
    const std::array<std::optional<std::array<float, 3>>, 2> normals{{std::array<float, 3>{0, 1, 0}, std::nullopt}};
    const auto other_normals = normals;
    std::size_t preparations = 0;
    bool fail = false;
    TerrainLightingCache cache{[&](const TerrainChunk& source, const Lighting& light, std::vector<std::uint32_t>& colors, TerrainLightingNormals custom) {
        ++preparations;
        if (fail) {
            throw std::runtime_error("deliberate preparation failure");
        }
        colors.assign(source.mesh.vertices.size(), std::uint32_t(light.ambient[0]) + (custom.empty() ? 0 : 256));
    }};
    const Lighting light{{0, 0, 1}, 1, {10, 20, 30}};
    expect(cache.colors(first, light) == std::vector<std::uint32_t>{10, 10}, "The initial terrain cache miss must prepare its source chunk");
    cache.colors(first, Lighting{light});
    expect(preparations == 1, "Equal Lighting values at different addresses must reuse prepared colors");
    cache.colors(second, light);
    expect(preparations == 2, "Equal-shaped source chunks must retain separate stable cache identities");
    expect(cache.colors(first, light, normals) == std::vector<std::uint32_t>{266, 266}, "Mega custom normals must participate in terrain color preparation");
    cache.colors(first, light, normals);
    expect(preparations == 3, "An unchanged Mega normals span must reuse its prepared colors");
    cache.colors(first, light, other_normals);
    expect(preparations == 4, "Different immutable normal sources must not reuse another span's prepared colors");
    cache.colors(first, light, std::span(normals).first(1));
    expect(preparations == 5, "Normal span extent must participate in cache identity");

    auto changed = light;
    changed.ambient[0] = 50;
    expect(cache.colors(first, changed) == std::vector<std::uint32_t>{50, 50}, "Changed lighting must replace cached colors");
    cache.colors(second, changed);
    expect(preparations == 7, "A light change must invalidate every previously prepared chunk");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        changed.direction[axis] += 1;
        cache.colors(first, changed);
        changed.ambient[axis] += 1;
        cache.colors(first, changed);
    }
    changed.diffuse += 1;
    cache.colors(first, changed);
    expect(preparations == 14, "The cache key must compare every Lighting component");

    changed.diffuse += 1;
    fail = true;
    bool rejected = false;
    try {
        cache.colors(first, changed);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "A failed lighting preparation must propagate its error");
    fail = false;
    cache.colors(first, changed);
    expect(preparations == 16, "A failed lighting preparation must leave no apparently valid stale entry");
}

void canonical(const std::filesystem::path& root) {
    const AssetStore assets(root);
    for (std::uint32_t track = 0; track < 10; ++track) {
        const auto resources = load_scene_resources(assets, resolve_setup({.track_id = track, .weather_choice = 1}, 1));
        const BarrierData barriers(resources.route_points, resources.ground.sector_centers);
        Visibility visibility(resources.ground);
        for (const auto* baked : {&resources.trees_baked, &resources.fence_baked, &resources.objects_baked, &resources.cones.baked, &resources.posts.baked, &resources.signs.baked,
                 &resources.flags.baked}) {
            visibility.register_static(*baked);
        }
        expect(visibility.data().sectors.size() == barriers.sector_count(), "Every canonical track must share its original GROUND sector grid with barrier registration");
        for (std::size_t index = 0; index < visibility.data().sectors.size(); ++index) {
            const auto& sector = visibility.data().sectors[index];
            const auto query = barriers.sector({sector.center[0], 0, sector.center[1]});
            expect(query && *query == index && visibility.data().sector({sector.center[0], 0, sector.center[1]}) == index,
                "Every authored center, including Lavaland's raw Z offsets, retains the original47e370 index");
            expect(std::ranges::is_sorted(sector.static_entries, std::greater<>()), "All ten canonical static lists must preserve47de00 prepend order");
        }
        // Real source bounds, not a reconstructed cell rectangle, drive the
        // selection at each drawable aspect and all five detail settings.
        const auto& center = resources.ground.sector_centers[resources.ground.sector_centers.size() / 2];
        auto matrix = identity;
        matrix[12] = -float(center[0]) / one;
        matrix[14] = -float(center[1]) / one;
        constexpr std::array flags{0x04000000u, 0x02000000u, 0u, 0x01000000u};
        std::vector<std::uint16_t> root_sectors;
        for (const auto& authored : resources.ground.sector_centers) {
            for (const auto flag : flags) {
                const auto id = visibility.register_root({float(authored[0]) / one, 0, float(authored[1]) / one}, flag);
                root_sectors.push_back(visibility.root_sector(id));
            }
        }
        for (const auto aspect : std::array<std::array<std::uint32_t, 2>, 3>{{{640, 480}, {1280, 720}, {1280, 800}}}) {
            for (std::uint32_t detail = 0; detail < 5; ++detail) {
                const auto& frame = visibility.select(visibility_camera(matrix, default_projection(detail, aspect[0], aspect[1])));
                expect(!frame.ordered_sectors.empty(), "Canonical track geometry must admit original sectors at every supported detail and aspect");
                for (const auto index : frame.terrain_sectors) {
                    expect(frame.detail[index] < 3 && visibility.data().sectors[index].ground_chunk < resources.ground.chunks.size(),
                        "Canonical terrain submissions must retain the original detail gate and exact GROUND mesh identity");
                }
                std::vector<bool> submitted(root_sectors.size());
                for (const auto& draw : frame.draws) {
                    if (draw.dynamic) {
                        submitted[draw.id] = true;
                    }
                }
                for (std::size_t id = 0; id < root_sectors.size(); ++id) {
                    const auto sector = root_sectors[id];
                    expect(
                        frame.root_mask(std::uint32_t(id)) == (frame.selected[sector] ? 15 : 0) && submitted[id] == (frame.selected[sector] && frame.detail[sector] < id % 4 + 1),
                        "All four root limits on all ten canonical grids must retain actual PVS masks and strict per-sector detail gates");
                }
            }
        }
        const auto& all = visibility.select(visibility_camera(matrix, default_projection(3, 640, 480)), {}, true);
        expect(std::ranges::count_if(all.draws,
                   [](const auto& draw) {
                       return draw.dynamic;
                   }) == std::ptrdiff_t(root_sectors.size())
                   && std::ranges::all_of(all.root_masks,
                       [](auto mask) {
                           return mask == 15;
                       }),
            "All four original root flags must integrate with every canonical track's ordered sector lists");
        std::cout << track_content(track).archive_prefix << " sectors=" << visibility.data().sectors.size() << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            source_frame_journal();
            source_view_observers();
            fleet_scene_context();
            horizontal_clip();
            spiral_and_levels();
            mega_distances();
            registration_and_motion();
            independent_copies();
            lighting_cache();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-visibility-tests [canonical-root]");
        }
        std::cout << "PASS original visibility spiral, clip walls, detail, registration and migration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
