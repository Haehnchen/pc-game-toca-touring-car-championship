#include "scene/model_textures.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {
using namespace toca::test::scene_models;

void resident_models() {
    using namespace toca::scene;
    GpuFixture gpu("TOCA resident scene models", 640, 480);
    auto& renderer = gpu.renderer;
    SceneResources resources{};
    {
        toca::ModelTexture first{"WSCRN", Bytes(58)};
        auto& bytes = first.bmp;
        bytes[0] = 'B';
        bytes[1] = 'M';
        put32(bytes, 2, 58);
        put32(bytes, 10, 54);
        put32(bytes, 14, 40);
        put32(bytes, 18, 1);
        put32(bytes, 22, 1);
        put16(bytes, 26, 1);
        put16(bytes, 28, 24);
        bytes[54] = 90;
        bytes[55] = 74;
        bytes[56] = 41;
        auto later = first;
        later.bmp[54] = 87;
        later.bmp[55] = 76;
        ModelTextures uploads(renderer);
        const auto ordinary = uploads.get_model(first).texture;
        expect(uploads.get_model(later).texture == ordinary, "496250 must retain the first named ordinary texture when later car palettes differ");
        const auto ghost = uploads.get_model(later, {}, {}, true).texture;
        expect(ghost != ordinary && uploads.get_model(first, {}, {}, true).texture == ghost, "496250 must keep separate first-name bindings for ordinary and ghost materials");
        later.name = "SIDEG";
        expect(uploads.get_model(later).texture == ghost, "Different source names may share an exact immutable upload without changing their independent binding");
    }
    resources.setup = resolve_setup({.weather_choice = 1, .detail = 3});
    toca::ModelMesh triangle{"repeated-root", {{{-.75f, -.5f, 0}, {0, 0, -1}, 0, 0}, {{0, .5f, 0}, {0, .6f, -.8f}, .5f, 1}, {{.75f, -.5f, 0}, {.6f, 0, -.8f}, 1, 0}},
        {{{0, 1, 2}, -1, {180, 130, 75, 255}}}};
    resources.objects.meshes.push_back(triangle);
    for (float x : {-3.f, 0.f, 3.f}) {
        auto matrix = unit;
        matrix[12] = x;
        matrix[14] = 8;
        resources.objects.nodes.push_back({-1, matrix, 0, {0}});
    }
    // The player's independently prepared model has identical source corners.
    // Its pose changes separately from the three authored scenery roots.
    resources.car.models[0].meshes.push_back(triangle);
    resources.car.models[0].nodes.push_back({-1, unit, 0, {0}});
    SceneView view(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
    SceneView shared_view(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false, view.model_textures(), view.model_meshes());
    CameraInputs camera{unit, default_projection(3, 640, 480)};
    Lighting light{{0, 0, -1}, .5f, {20, 30, 40}};
    CarPose pose{unit};
    pose.object_to_world[13] = 2.5f;
    pose.object_to_world[14] = 8;
    const auto first = view.frame(camera, light, pose, {}, {});
    expect(first.color_load == toca::gpu::ColorLoad::preserve, "Detailed non-Lavaland SceneView must retain the source's rotating color targets");
    expect(first.vertices.empty() && first.indices.empty() && first.draws.size() == 4 && first.model_instances.size() == 4,
        "Production SceneView must keep three repeated scenery roots and the player in resident model draws");
    const auto mesh = first.draws[0].mesh;
    expect(bool(mesh) && mesh->vertices.size() == 3 && mesh->attributes.size() == 3, "Production model roots require immutable source corners and attributes");
    const auto corners = mesh->vertices;
    const auto attributes = mesh->attributes;
    auto* const buffer = mesh->buffer;
    const auto verify = [&](const toca::gpu::Frame& frame) {
        expect(frame.vertices.empty() && frame.indices.empty() && frame.draws.size() == 4 && frame.model_instances.size() == 4,
            "Camera/light/pose changes must retain resident SceneView geometry");
        for (const auto& draw : frame.draws) {
            expect(draw.mesh == mesh && draw.mesh->buffer == buffer && draw.model_instance < frame.model_instances.size() && draw.instance_count == 1,
                "Repeated roots, player poses and shared SceneViews must reuse the same immutable uploaded model");
        }
        expect(std::memcmp(mesh->vertices.data(), corners.data(), corners.size() * sizeof(corners[0])) == 0
                   && std::memcmp(mesh->attributes.data(), attributes.data(), attributes.size() * sizeof(attributes[0])) == 0,
            "Presentation changes must preserve every source position, material, normal, UV and ghost attribute byte");
        // Build expected hierarchy/lighting from the authored fixture, without
        // reading resident vertices or instance matrices from the frame.
        toca::gpu::Frame expected;
        const auto& projection = camera.projection;
        const toca::gpu::WorldProjection source_view{camera.world_to_view, {projection.x, projection.y, projection.near_plane, projection.far_plane},
            {projection.depth_scale, projection.depth_offset, float(projection.width), float(projection.height)}};
        const auto append = [&](const std::array<float, 16>& matrix) {
            std::array<LitVertex, 3> vertices;
            for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
                const auto& vertex = triangle.vertices[corner];
                vertices[corner].position = vertex.position;
                vertices[corner].color = model_vertex_color(vertex.normal, matrix, triangle.faces[0].color, light);
                vertices[corner].u = vertex.u;
                vertices[corner].v = vertex.v;
            }
            // 48741e stores WV/WVP before 47e730 projects original local corners;
            // the separate rounded world-position intermediate loses source bits.
            toca::gpu::Draw draw{};
            draw.state.depth = true;
            toca::test::materialize_source_triangle(expected, draw, vertices, source_view, toca::gpu::source_clip_matrix(source_view, matrix), false);
        };
        for (const auto& node : resources.objects.nodes) {
            append(node.transform);
        }
        append(pose.object_to_world);
        const auto projected = toca::test::materialize_world(frame);
        expect(!expected.vertices.empty() && projected.vertices.size() == expected.vertices.size()
                   && std::memcmp(projected.vertices.data(), expected.vertices.data(), expected.vertices.size() * sizeof(toca::gpu::Vertex)) == 0,
            "Resident production roots must retain exact authored hierarchy, normals, source lighting, UVs and projection");
        // These independent geometry comparisons deliberately have no scene
        // background or chronological source presentation history.
        auto isolated = frame;
        isolated.color_load = toca::gpu::ColorLoad::clear;
        expect(renderer.draw(isolated, false), "Resident SceneView render target was unavailable");
        const auto& counters = renderer.last_frame_timing();
        expect(counters.model_draw_calls == 1 && counters.model_instances == 4 && counters.instanced_draw_calls == 1 && counters.world_draw_calls == 1,
            "Consecutive repeated production roots must become one actual GPU draw with four ordered model instances");
        const auto pixels = renderer.readback();
        expect(std::ranges::count_if(pixels,
                   [](auto color) {
                       return color != toca::gpu::background;
                   })
                   > 100,
            "Resident repeated roots and the player must contribute real GPU pixels");
        return pixels;
    };
    const auto baseline = verify(first);
    auto hidden_player = pose;
    hidden_player.render_selection = CarRenderSelection{0, 0};
    const auto hidden_frame = view.frame(camera, light, hidden_player, {}, {});
    expect(hidden_frame.model_instances.size() == 3, "A frozen hidden player selection must preserve scenery while suppressing its model");
    hidden_player.render_selection->root_mask = 1;
    expect(verify(view.frame(camera, light, hidden_player, {}, {})) == baseline,
        "Restoring a published player selection must reuse immutable models and the exact existing GPU result");
    camera.world_to_view[12] = -.5f;
    const auto camera_pixels = verify(view.frame(camera, light, pose, {}, {}));
    expect(camera_pixels != baseline, "Camera movement must change production GPU pixels while retaining model buffers");
    light = {{0, 0, -1}, .25f, {25, 50, 75}};
    const auto light_pixels = verify(view.frame(camera, light, pose, {}, {}));
    expect(light_pixels != camera_pixels, "Lighting must change production GPU pixels through instance data");
    pose.object_to_world[0] = .8f;
    pose.object_to_world[2] = .6f;
    pose.object_to_world[8] = -.6f;
    pose.object_to_world[10] = .8f;
    pose.object_to_world[12] = 1;
    const auto pose_pixels = verify(view.frame(camera, light, pose, {}, {}));
    expect(pose_pixels != light_pixels, "An independent player pose must change production GPU pixels through instance data");
    expect(verify(shared_view.frame(camera, light, pose, {}, {})) == pose_pixels, "A second SceneView sharing model resources must retain exact production GPU pixels");
    // Reuse prepared slots with the same CarPose and body matrix while only
    // steering, hidden descendants and the active rival prefix change.
    auto animated_resources = resources;
    animated_resources.car.models[0].nodes[0].id_flags = 1;
    auto descendant = unit;
    descendant[13] = 1;
    animated_resources.car.models[0].nodes.push_back({0, descendant, 20, {0}});
    const std::array bindings{RivalModel{1, &animated_resources.car}};
    SceneView animated_view(renderer, animated_resources, nullptr, nullptr, nullptr, bindings, nullptr, nullptr, false, view.model_textures(), view.model_meshes());
    CameraInputs observer{unit, default_projection(3, 640, 480)};
    CarPose animated{unit};
    animated.object_to_world[12] = 1.5f;
    animated.object_to_world[13] = 2;
    animated.object_to_world[14] = 8;
    std::array rivals{RivalPose{1, {unit}}};
    rivals[0].car.object_to_world[12] = -2;
    rivals[0].car.object_to_world[14] = 8;
    const auto prepare = [&](std::span<const RivalPose> visible) {
        return animated_view.frame(observer, light, animated, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, visible);
    };
    observer.view = 4;
    expect(prepare(rivals).model_instances.size() == 5, "Bonnet preparation must hide the front-wheel root and its descendant while retaining scenery/rival instances");
    observer.view = 2;
    expect(prepare(rivals).model_instances.size() == 7, "Normal view must prepare visible wheel descendants before the steering-only change");
    const auto body_before = animated.object_to_world;
    animated.steering_angle = 1024;
    const auto steering = prepare(rivals);
    const auto root_instance = std::ranges::find_if(steering.model_instances, [&](const auto& instance) {
        return instance.object_to_world[12] == 1.5f && instance.object_to_world[13] == 2 && instance.object_to_world[14] == 8;
    });
    expect(steering.model_instances.size() == 7 && animated.object_to_world == body_before && root_instance != steering.model_instances.end()
               && root_instance->object_to_world[0] == 0 && root_instance->object_to_world[2] == -1,
        "Visible-again slots must recompute steering at the same CarPose address and unchanged body matrix, including formerly hidden descendants");
    expect(prepare({}).model_instances.size() == 5, "Removing the rival must shrink the active preparation prefix without replaying a previous slot");
    observer.view = 4;
    expect(prepare({}).model_instances.size() == 3, "A shorter preparation prefix must not preserve previously visible wheel descendants");
    observer.view = 2;
    expect(prepare(rivals).model_instances.size() == 7, "Regrowing the preparation prefix must restore local descendants and the rival exactly once");
    expect(renderer.draw(prepare(rivals), false), "Restored preparation slots must still reach actual GPU rendering");
    auto selected_resources = animated_resources;
    selected_resources.car.models[1] = selected_resources.car.models[0];
    selected_resources.car.models[2] = selected_resources.car.models[0];
    selected_resources.car.models[2].meshes[0].faces[0].color = {10, 240, 30, 255};
    const std::array selected_bindings{RivalModel{1, &selected_resources.car}};
    SceneView selected_view(renderer, selected_resources, nullptr, nullptr, nullptr, selected_bindings, nullptr, nullptr, false);
    auto selected_rivals = rivals;
    selected_rivals[0].car.render_selection = CarRenderSelection{2, 1};
    const auto selected_frame = selected_view.frame(observer, light, animated, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, selected_rivals);
    expect(std::ranges::any_of(selected_frame.draws,
               [](const auto& draw) {
                   return draw.mesh && draw.mesh->vertices.front().color == 0xff0af01eu;
               }),
        "A published LOW rival selection must reach the actual LOW material instead of recomputing its LOD from a new view");
    expect(renderer.draw(selected_frame, false), "Published rival selection must reach the actual GPU consumer");
    selected_rivals[0].car.render_selection->root_mask = 0;
    const auto selected_hidden = selected_view.frame(observer, light, animated, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, selected_rivals);
    expect(selected_hidden.model_instances.size() + 2 == selected_frame.model_instances.size(), "A hidden published rival must remove its whole selected family");
    {
        SceneResources hierarchy_resources{};
        hierarchy_resources.setup = resolve_setup({.weather_choice = 1, .detail = 3});
        auto wheel = triangle, body_child = triangle;
        wheel.faces[0].color = {10, 240, 30, 255};
        body_child.faces[0].color = {240, 20, 40, 255};
        auto& model = hierarchy_resources.car.models[0];
        model.meshes = {wheel, body_child};
        auto wheel_local = unit, child_local = unit;
        wheel_local[12] = 1;
        child_local[12] = -1;
        // The source allocates this wheel before the body's drawable child,
        // but 48d367 visits the complete body subtree before the wheel sibling.
        model.nodes = {{-1, unit, 501, {}}, {0, unit, 300, {}}, {0, wheel_local, 3, {0}}, {1, child_local, 20, {1}}};
        SceneView hierarchy_view(renderer, hierarchy_resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
        CarPose hierarchy_pose{unit};
        hierarchy_pose.object_to_world[14] = 8;
        const auto hierarchy_frame = hierarchy_view.frame(observer, {{0, 0, -1}, 0, {255, 255, 255}}, hierarchy_pose, {}, {});
        expect(hierarchy_frame.draws.size() == 2 && hierarchy_frame.draws[0].mesh->vertices.front().color == 0xfff01428u
                   && hierarchy_frame.draws[1].mesh->vertices.front().color == 0xff0af01eu,
            "Production car submission must draw the body's child before its earlier-allocated wheel sibling");
        expect(hierarchy_frame.model_instances[hierarchy_frame.draws[0].model_instance].object_to_world[12] == -1
                   && hierarchy_frame.model_instances[hierarchy_frame.draws[1].model_instance].object_to_world[12] == 1,
            "Hierarchy draw ordering must retain each original node's independent pose");
        expect(renderer.draw(hierarchy_frame, false), "Ordered production hierarchy must reach actual GPU rendering");
        const auto hierarchy_pixels = renderer.readback();
        expect(std::ranges::find(hierarchy_pixels, 0xfff01428u) != hierarchy_pixels.end() && std::ranges::find(hierarchy_pixels, 0xff0af01eu) != hierarchy_pixels.end(),
            "Both independently colored hierarchy nodes must contribute actual GPU pixels");
    }
    {
        auto queued_camera = observer;
        queued_camera.queued_grid_lines = std::vector<QueuedWorldLine>{{{-65536, 65536, 4 * 65536}, {65536, 65536, 4 * 65536}, 0xff12f03fu}};
        const auto queued = selected_view.frame(queued_camera, light, animated, {}, {});
        expect(!queued.draws.empty() && queued.draws.front().state.lines && queued.draws.front().state.depth && queued.draws.front().state.depth_always
                   && !queued.draws.front().state.blend && queued.draws.front().count == 2 && queued.dynamic_world_lines.size() == 2,
            "The frozen443030 pool must reach the actual SceneView before car models as opaque depth-ALWAYS world lines");
        expect(queued.dynamic_world_lines[0].position == std::array<float, 3>{-1, 1, 4} && queued.dynamic_world_lines[1].position == std::array<float, 3>{1, 1, 4}
                   && queued.dynamic_world_lines[0].color == 0xff12f03fu && queued.dynamic_world_lines[1].color == 0xff12f03fu,
            "Queued presentation lines must preserve exact Q16 endpoint order and both opaque colors");
        expect(renderer.draw(queued, false), "Frozen grid lines must reach the actual GPU consumer");
        const auto queued_pixels = renderer.readback();
        expect(std::ranges::find(queued_pixels, 0xff12f03fu) != queued_pixels.end(), "Frozen grid line must contribute its distinct actual GPU pixel color");
        for (const auto track : {4u, 7u}) {
            auto track_resources = selected_resources;
            track_resources.setup = resolve_setup({.track_id = track, .weather_choice = 1});
            SceneView track_view(renderer, track_resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
            const auto track_frame = track_view.frame(queued_camera, light, animated, {}, {});
            expect(track_frame.draws.front().state.depth && !track_frame.draws.front().state.depth_always && !track_frame.draws.front().state.depth_bias,
                "443030 tracks4/7 must keep the measured wrapper's LEQUAL/writeALL and zero rasterizer bias");
        }
        queued_camera.queued_grid_lines->clear();
        expect(selected_view.frame(queued_camera, light, animated, {}, {}).dynamic_world_lines.empty(), "An explicitly empty frozen pool must suppress runtime regeneration");
    }
    std::cout << "PASS: immutable production model reuse across camera/light/pose/shared views and consecutive root GPU instancing\n";
}
} // namespace

int main(int argc, char**) {
    try {
        if (!(argc == 1)) {
            throw std::runtime_error("Usage: toca-scene-model-residency-tests");
        }
        resident_models();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
