#include "assets/store.hpp"
#include "scene/body.hpp"
#include "scene/follow_camera.hpp"
#include "scene/skid_marks.hpp"
#include "scene/view.hpp"
#include "scene/visibility.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::int32_t one = 65536;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr Lighting flat_light{{0, 0, -1}, 0, {255, 255, 255}};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ModelMesh quad(float z, std::array<std::uint8_t, 4> color, float size = 2) {
    ModelMesh result;
    result.vertices = {{{-size, size, z}, {0, 0, -1}, 0, 0}, {{size, size, z}, {0, 0, -1}, 1, 0}, {{size, -size, z}, {0, 0, -1}, 1, 1}, {{-size, -size, z}, {0, 0, -1}, 0, 1}};
    result.faces = {{{0, 1, 2}, -1, color}, {{0, 2, 3}, -1, color}};
    return result;
}

std::vector<std::uint8_t> bmp(std::array<std::uint8_t, 3> color) {
    std::vector<std::uint8_t> result(58);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            result[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    result[0] = 'B';
    result[1] = 'M';
    put(2, 58);
    put(10, 54);
    put(14, 40);
    put(18, 1);
    put(22, 1);
    put(34, 4);
    result[26] = 1;
    result[28] = 24;
    result[54] = color[2];
    result[55] = color[1];
    result[56] = color[0];
    return result;
}

SceneResources synthetic(std::uint32_t detail = 3) {
    SceneResources result{};
    result.setup = resolve_setup({.track_id = 0, .weather_choice = 1, .detail = detail}, 1);
    for (std::int32_t row = 0; row < 15; ++row) {
        for (std::int32_t column = 0; column < 15; ++column) {
            result.ground.sector_centers.push_back({(column - 7) * 64 * one, (7 - row) * 64 * one});
        }
    }
    // The empty player mesh still has its real scene-root registration.
    for (auto& model : result.car.models) {
        model.nodes.push_back({-1, identity, 0, {}});
    }
    return result;
}

TerrainChunk baked_quad(std::uint32_t node, std::int32_t origin_z, std::int16_t texture) {
    TerrainChunk result;
    result.node_index = node;
    result.origin_xz = {0, origin_z};
    result.mesh = quad(8, {255, 255, 255, 255});
    for (auto& face : result.mesh.faces) {
        face.texture = texture;
        result.face_flags.push_back(0);
    }
    return result;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const gpu::Frame& source, const std::string& name = {}) {
    // The same clear-only background works for intentionally empty selections.
    auto frame = source;
    if (frame.draws.empty()) {
        frame.quad({}, 0, 0, 4096, 4096, gpu::background);
    }
    expect(renderer.draw(frame, false), "Visibility fixture requires its private GPU drawable");
    if (!name.empty()) {
        renderer.save_screenshot(name);
    }
    return renderer.readback();
}

void source_order(gpu::Renderer& renderer) {
    auto resources = synthetic();
    resources.ground.chunks.push_back(baked_quad(113, 0, -1));
    resources.trees_baked.textures = {{"red", bmp({255, 0, 0})}, {"blue", bmp({0, 0, 255})}};
    resources.trees_baked.chunks = {baked_quad(0, 0, 0), baked_quad(1, 0, 1)};
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    view.update_visibility(camera, {identity}, {});
    const auto frame = view.frame(camera, flat_light, {identity}, {}, {});
    const auto projected = test::materialize_world(frame);
    expect(projected.draws.size() == 6 && projected.draws[2].state.texture == projected.draws[3].state.texture
               && projected.draws[4].state.texture == projected.draws[5].state.texture && projected.draws[2].state.texture != projected.draws[4].state.texture,
        "Terrain must precede both original static registrations and each source quad's triangles stay adjacent");
    const auto pixels = render(renderer, frame, "visibility-static-prepend.png");
    expect(pixels[240 * 640 + 320] == 0xffff0000, "47de00 prepending must draw the first red registration after the later blue registration at equal depth");

    resources.objects.nodes.push_back({-1, identity, 0, {0}});
    resources.objects.meshes.push_back(quad(8, {0, 255, 0, 255}));
    SceneView dynamic(renderer, resources);
    const auto actors = dynamic.frame(camera, flat_light, {identity}, {}, {});
    expect(render(renderer, actors, "visibility-dynamic-after-static.png")[240 * 640 + 320] == 0xff00ff00,
        "489069 must draw the sector's dynamic root after its static list, preserving equal-depth submission order");
}

void frame_reuse(gpu::Renderer& renderer) {
    auto resources = synthetic();
    resources.ground.chunks.push_back(baked_quad(113, 0, -1));
    resources.objects.nodes.push_back({-1, identity, 0, {0}});
    resources.objects.meshes.push_back(quad(8, {0, 255, 0, 255}, 1));
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    const auto retained = view.frame(camera, flat_light, {identity}, {}, {});
    const auto original_pixels = render(renderer, retained);
    gpu::Frame reused;
    view.frame_into(reused, camera, flat_light, {identity}, {}, {});
    expect(!reused.model_instances.empty() && !reused.world_views.empty()
               && std::ranges::any_of(reused.draws,
                   [](const auto& draw) {
                       return bool(draw.mesh);
                   }),
        "Frame reuse fixture must include resident model and terrain geometry");
    const std::array<const void*, 5> buffers{reused.vertices.data(), reused.indices.data(), reused.draws.data(), reused.world_views.data(), reused.model_instances.data()};
    const std::array capacities{reused.vertices.capacity(), reused.indices.capacity(), reused.draws.capacity(), reused.world_views.capacity(), reused.model_instances.capacity()};
    view.frame_into(reused, camera, flat_light, {identity}, {}, {});
    expect(
        buffers == std::array<const void*, 5>{reused.vertices.data(), reused.indices.data(), reused.draws.data(), reused.world_views.data(), reused.model_instances.data()}
            && capacities
                   == std::array{reused.vertices.capacity(), reused.indices.capacity(), reused.draws.capacity(), reused.world_views.capacity(), reused.model_instances.capacity()},
        "Repeated frame_into calls must retain TL, index, draw, resident-view and model-instance buffers");
    expect(render(renderer, reused) == original_pixels, "Reused output must render the same static/dynamic scene as the independently returned frame");
    auto moved = camera;
    moved.world_to_view[12] = -.5f;
    auto dimmed = flat_light;
    dimmed.ambient = {20, 20, 20};
    view.frame_into(reused, moved, dimmed, {identity}, {}, {});
    expect(render(renderer, reused) != original_pixels, "Camera and lighting updates must reach the reused frame's actual GPU scene");
    expect(render(renderer, retained) == original_pixels, "A previously returned frame must retain its dynamic vertices and resident colors across later camera/light updates");
}

void shared_vertex_materials(gpu::Renderer& renderer) {
    auto resources = synthetic();
    auto mesh = quad(8, {255, 0, 0, 255});
    mesh.faces[1].color = {0, 255, 0, 255};
    mesh.faces.push_back(mesh.faces.front());
    // Only referenced source vertices participate in drawing or validation.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    mesh.vertices.push_back({{nan, nan, nan}, {nan, nan, nan}, nan, nan});
    resources.objects.nodes.push_back({-1, identity, 0, {0}});
    resources.objects.meshes.push_back(std::move(mesh));
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    const auto frame = view.frame(camera, flat_light, {identity}, {}, {});
    const auto projected = test::materialize_world(frame);
    expect(projected.draws.size() == 3 && projected.vertices.size() == 9, "Shared model vertices must retain all three source face submissions");
    constexpr std::array colors{0xffff0000u, 0xff00ff00u, 0xffff0000u};
    for (std::size_t face = 0; face < colors.size(); ++face) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            expect(projected.vertices[face * 3 + corner].diffuse == colors[face], "Shared model vertices must use each face's material, including a return to an earlier color");
        }
    }
    const auto pixels = render(renderer, frame);
    expect(pixels[220 * 640 + 340] == 0xffff0000 && pixels[260 * 640 + 300] == 0xff00ff00,
        "Distinct face materials sharing model vertices must reach their original red and green GPU pixels");
}

void hidden_children_and_snapshot(gpu::Renderer& renderer) {
    auto resources = synthetic();
    // A legal source node order may interleave a second root before the first
    // root's child. Membership follows parent links, not the latest root ID.
    resources.signs.model.nodes = {{-1, identity, 0, {}}, {-1, identity, 2, {}}, {0, identity, 1, {0}}};
    resources.signs.model.meshes.push_back(quad(8, {0, 255, 0, 255}));
    PropFrame props;
    props.signs.transforms.resize(3);
    for (auto& transform : props.signs.transforms) {
        transform[0] = transform[5] = transform[10] = transform[15] = one;
    }
    props.signs.visible = {1, 0, 1};
    SceneView view(renderer, resources);
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    view.update_visibility(camera, {identity}, {}, props);
    const auto before = view.visibility();
    const auto visible = render(renderer, view.frame(camera, flat_light, {identity}, {}, {}, props));
    expect(visible[240 * 640 + 320] == 0xff00ff00, "An authored child mesh must draw through its registered root");
    props.signs.visible[0] = 0;
    const auto hidden = view.frame(camera, flat_light, {identity}, {}, {}, props);
    expect(test::materialize_world(hidden).vertices.empty(), "47f440 hidden-root traversal must suppress an independently visible child mesh");
    expect(render(renderer, hidden) != visible, "Hiding the root must remove its actual child pixels");
    for (unsigned draws = 0; draws < 5; ++draws) {
        (void)view.frame(camera, flat_light, {identity}, {}, {}, props);
    }
    expect(view.visibility().ordered_sectors == before.ordered_sectors && view.visibility().detail == before.detail && view.visibility().root_masks == before.root_masks,
        "Drawing a different pose or repeated UI frames must not change the stored simulation-tick visibility snapshot");
    view.update_visibility(camera, {identity}, {}, props);
    expect(view.visibility().root_masks != before.root_masks, "Only the explicit tick update may publish the new hidden-root mask");
}

void terrain_lod_and_skids(gpu::Renderer& renderer) {
    auto resources = synthetic(0);
    // Geometry is near the eye, but its authored registration is three grid
    // cells away. This isolates sector LOD from ordinary far-plane clipping.
    resources.ground.chunks.push_back(baked_quad(68, 192 * one, -1));
    const CameraInputs camera{identity, default_projection(0, 640, 480)};
    SceneView terrain(renderer, resources);
    const auto terrain_frame = terrain.frame(camera, flat_light, {identity}, {}, {});
    // Low-detail sky remains visible independently of terrain sector selection.
    expect(std::ranges::none_of(terrain_frame.draws,
               [](const auto& draw) {
                   return draw.state.depth;
               }),
        "Terrain sector at detail3 must be absent even when its vertices lie inside the projection");
    resources.objects_baked.textures.push_back({"red", bmp({255, 0, 0})});
    resources.objects_baked.chunks.push_back(baked_quad(0, 192 * one, 0));
    SceneView static_object(renderer, resources);
    expect(render(renderer, static_object.frame(camera, flat_light, {identity}, {}, {}))[240 * 640 + 320] == 0xffff0000,
        "Static geometry in a selected detail3 sector is still submitted after terrain's independent detail gate");

    auto empty = synthetic();
    SceneView skid_view(renderer, empty);
    SkidMarkFrame marks;
    marks.quads.push_back({{{{-one, one, 4 * one}, {-one, -one, 4 * one}, {one, -one, 4 * one}, {one, one, 4 * one}}}, 0xffffffff, 112, 1});
    const auto draw = [&](const SkidMarkFrame& frame) {
        return skid_view.frame(camera, flat_light, {identity}, {}, {}, {}, {}, nullptr, {}, {}, nullptr, &frame);
    };
    const auto selected = draw(marks);
    expect(
        !test::materialize_world(selected).vertices.empty() && render(renderer, selected)[240 * 640 + 320] == 0xffffffff, "Original skid sector112 must reach visible GPU pixels");
    marks.quads[0].sector = 119;
    expect(test::materialize_world(draw(marks)).vertices.empty(), "The same skid vertices tagged with an unselected source sector must be filtered by47db00");
    marks.quads[0].sector = no_visibility_sector;
    expect(test::materialize_world(draw(marks)).vertices.empty(), "The invalid skid-sector sentinel must stay bounded in the integrated scene path");
}

void authored_camera_ancestor(gpu::Renderer& renderer) {
    auto resources = synthetic();
    resources.objects_baked.textures.push_back({"red", bmp({255, 0, 0})});
    resources.objects_baked.chunks.push_back(baked_quad(0, 0, 0));
    SceneView view(renderer, resources);
    CameraInputs camera{identity, default_projection(3, 640, 480)};
    CarPose car{identity};
    car.object_to_world[14] = -320;
    // The authored camera at the origin looks away from the body in sector187.
    // View2 instead forces that body/follow-anchor sector after the spiral.
    constexpr std::uint16_t body_sector = 187;
    SkidMarkFrame marks;
    marks.quads.push_back({{{{-one, one, 4 * one}, {-one, -one, 4 * one}, {one, -one, 4 * one}, {one, one, 4 * one}}}, 0xffffffff, body_sector, 1});
    const auto draw = [&] {
        return view.frame(camera, flat_light, car, {}, {}, {}, {}, nullptr, {}, {}, nullptr, &marks);
    };
    view.update_visibility(camera, car, {});
    expect(view.visibility().selected[body_sector] && view.visibility().ordered_sectors.back() == body_sector,
        "A follow camera must append the body/follow-anchor sector even when its bounds lie behind the camera");
    expect(render(renderer, draw())[240 * 640 + 320] == 0xffffffff, "The forced follow-anchor sector must admit its saved skid through the integrated GPU path");

    camera.view = 1;
    camera.canonical_single_player = true;
    view.update_visibility(camera, car, {});
    const auto cockpit = view.scene_context(15).actors[15];
    expect(cockpit.physical.sector == body_sector && (view.visibility().root_masks.front() & 1) && !cockpit.contact_effect_mask && !cockpit.wheel_effect_mask
               && !cockpit.engine_effect_mask,
        "Cockpit model hiding must retain the physical parent and its forced head sector while suppressing manual model effect gates");
    expect(render(renderer, draw())[240 * 640 + 320] == 0xffffffff, "The registered cockpit anchor must preserve its body's selected effects sector without geometry");
    camera.view = 2;

    camera.registered_camera_position = std::array<std::int32_t, 3>{0, 0, 0};
    view.update_visibility(camera, car, {});
    expect(view.visibility().selected[112] && !view.visibility().selected[body_sector] && view.visibility().root_masks.front() == 0,
        "4510bf/47dd90 authored cameras must force their own registered sector instead of the unrelated body sector");
    expect(view.player_visibility() == 1, "The supported single-player effect mask must remain1 even when the body's sector/root geometry is invisible");
    expect(render(renderer, draw(), "visibility-authored-camera-ancestor.png")[240 * 640 + 320] == 0xffff0000,
        "Authored-camera selection must remove the body's forced skid while retaining actual visible static geometry");
}

void horizon_special_projection(gpu::Renderer& renderer) {
    auto resources = synthetic();
    resources.moving_horizon.moving.nodes.push_back({-1, identity, 0x04000000u, {0}});
    resources.moving_horizon.moving.meshes.push_back(quad(500, {255, 0, 255, 255}, 100));
    HorizonFrame actors;
    actors.moving.transforms.resize(1);
    actors.moving.transforms[0][0] = actors.moving.transforms[0][5] = actors.moving.transforms[0][10] = actors.moving.transforms[0][15] = one;
    actors.moving.visible = {1};
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    const auto frame = view.frame(camera, flat_light, {identity}, {}, actors);
    const auto projected = test::materialize_world(frame);
    expect(projected.draws.size() == 2 && projected.draws[0].state.depth && projected.draws[0].state.depth_write,
        "496f70 HOR root must bypass GROUND/PVS/LOD under its separate normal-depth draw branch");
    const auto expected_z = 1.0001220703125f - .250030517578125f / 500;
    expect(std::abs(projected.vertices.front().z - expected_z) < .000001f && render(renderer, frame, "visibility-hor-far2000.png")[240 * 640 + 320] == 0xffff00ff,
        "HOR beyond the ordinary250-unit far plane must use the original2000-unit projection and reach the GPU");
}

void rival_detail_ranges(gpu::Renderer& renderer) {
    frontend::CarModels car{};
    // Only HIGH has the green attached part. Switching the complete model
    // removes that geometry even while the body remains plainly visible.
    car.models[0].nodes = {{-1, identity, 0, {}}, {0, identity, 300, {0}}, {1, identity, 50, {1}}};
    car.models[0].meshes = {quad(0, {255, 0, 0, 255}), quad(0, {0, 255, 0, 255}, .75f)};
    car.models[0].nodes[2].transform[12] = 3;
    for (std::size_t lod = 1; lod < 3; ++lod) {
        car.models[lod].nodes = {{-1, identity, 0, {0}}};
        car.models[lod].meshes = {quad(0, lod == 1 ? std::array<std::uint8_t, 4>{0, 0, 255, 255} : std::array<std::uint8_t, 4>{255, 255, 0, 255})};
    }
    const std::array binding{RivalModel{1, &car}};

    struct Sample {
        float distance;
        std::array<std::uint8_t, 5> lod;
    };

    constexpr std::array samples{Sample{6, {1, 1, 0, 0, 0}}, Sample{14, {1, 1, 1, 0, 0}}, Sample{20, {2, 2, 1, 1, 0}}, Sample{28, {2, 2, 2, 1, 0}}, Sample{30, {2, 2, 2, 1, 0}},
        Sample{40, {2, 2, 2, 2, 1}}, Sample{60, {2, 2, 2, 2, 1}}, Sample{80, {2, 2, 2, 2, 2}}, Sample{100, {2, 2, 2, 2, 2}}};
    {
        auto resources = synthetic();
        SceneView view(renderer, resources, nullptr, nullptr, nullptr, binding);
        CameraInputs camera{identity, default_projection(3, 640, 480)};
        camera.world_to_view[14] = 20; //Camera behind the body is not the View2 LOD observer.
        camera.canonical_single_player = true;
        std::array poses{RivalPose{1, {identity}}};
        poses[0].car.object_to_world[14] = 30;
        view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
        expect(view.scene_context(15).actors[1].model_detail == 1, "Normal source View2 must select LOD around the generated local body rather than the rendering camera");
        const auto medium = render(renderer, view.frame(camera, flat_light, {identity}, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses));
        expect(std::ranges::count(medium, 0xff0000ffu) > 0, "The source View2 model choice must reach actual GPU geometry");
        poses[0].car.object_to_world[14] = 6;
        view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
        expect(view.scene_context(15).actors[1].model_detail == 0, "Canonical retained adeb8=1 must permit a real View2 model change without the native Mega hold");
    }
    {
        auto resources = synthetic();
        SceneView view(renderer, resources, nullptr, nullptr, nullptr, binding);
        CameraInputs camera{identity, default_projection(3, 640, 480)};
        camera.registered_camera_position = std::array<std::int32_t, 3>{0, 0, 0};
        std::array poses{RivalPose{1, {identity}}};
        poses[0].car.object_to_world[14] = -320;
        view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
        const auto context = view.scene_context(15);
        expect(context.actors[1].physical.sector == 187 && !context.actors[1].sector_visible && context.actors[1].render_mask == 0 && context.actors[1].wheel_effect_mask == 1
                   && context.actors[1].engine_effect_mask == 1 && context.actors[1].model_detail == 2 && context.actors[1].contact_effect_mask == 0
                   && context.actors[15].physical.sector == 112,
            "SceneView must publish actual physical/LOD state with offscreen rival effects independent of geometry PVS and high-LOD contacts");
        render(renderer, view.frame(camera, flat_light, {identity}, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses));
        const auto after = view.scene_context(15);
        expect(after.actors[1].physical.sector == context.actors[1].physical.sector && after.actors[1].model_detail == context.actors[1].model_detail
                   && after.visibility.ordered_sectors == context.visibility.ordered_sectors,
            "GPU presentation must leave the simulation-owned fleet snapshot unchanged");
    }
    for (std::uint32_t detail = 0; detail < 5; ++detail) {
        auto resources = synthetic(detail);
        SceneView view(renderer, resources, nullptr, nullptr, nullptr, binding);
        CameraInputs camera{identity, default_projection(detail, 640, 480)};
        camera.hide_player = true;
        std::array poses{RivalPose{1, {identity}}};
        const auto settle = [&] {
            for (unsigned tick = 0; tick < 11; ++tick) {
                view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
            }
        };
        for (const auto& sample : samples) {
            poses[0].car.object_to_world[14] = sample.distance;
            settle();
            const auto lod = sample.lod[detail];
            expect(view.rival_model_detail(1) == lod, "Original vehicle cutoffs must remain strict while Mega extends HIGH/MEDIUM to40/80units");
            const auto frame = view.frame(camera, flat_light, {identity}, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses);
            const auto pixels = render(renderer, frame);
            constexpr std::array colors{0xffff0000u, 0xff0000ffu, 0xffffff00u};
            expect(std::ranges::count(pixels, colors[lod]) > 0, "Each selected vehicle model must reach actual GPU pixels");
            expect((std::ranges::count(pixels, 0xff00ff00u) > 0) == (lod == 0), "HIGH's attached part must remain visible throughout its extended Mega range");
        }
        if (detail == 4) {
            poses[0].car.object_to_world[14] = 60;
            settle();
            poses[0].car.object_to_world[14] = 30;
            view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
            poses[0].car.object_to_world[14] = 60;
            for (unsigned tick = 0; tick < 10; ++tick) {
                view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
                expect(view.rival_model_detail(1) == 0, "Mega must retain the ten-dispatch hold after changing vehicle detail");
            }
            view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
            expect(view.rival_model_detail(1) == 1, "Mega must change detail when the original ten-dispatch hold expires");
        }
    }

    std::vector<RivalModel> bindings;
    std::vector<RivalPose> poses;
    for (std::uint32_t actor = 1; actor < 16; ++actor) {
        bindings.push_back({actor, &car});
        poses.push_back({actor, {identity}});
        poses.back().car.object_to_world[14] = 10 + float(actor) / 2;
    }
    for (const auto detail : {3u, 4u}) {
        auto resources = synthetic(detail);
        SceneView view(renderer, resources, nullptr, nullptr, nullptr, bindings);
        const CameraInputs camera{identity, default_projection(detail, 640, 480)};
        for (const float offset : {0.f, 50.f}) {
            for (auto& pose : poses) {
                pose.car.object_to_world[14] = 10 + float(pose.actor_id) / 2 + offset;
            }
            for (unsigned tick = 0; tick < 11; ++tick) {
                view.update_visibility(camera, {identity}, {}, {}, nullptr, poses);
            }
            std::array<unsigned, 3> counts{};
            for (const auto& pose : poses) {
                ++counts[view.rival_model_detail(pose.actor_id)];
            }
            const std::array<unsigned, 3> expected =
                detail == 4 ? (offset == 0 ? std::array{15u, 0u, 0u} : std::array{0u, 15u, 0u}) : (offset == 0 ? std::array{4u, 5u, 6u} : std::array{0u, 0u, 15u});
            expect(counts == expected, "Mega must retain detailed models for the whole field; Very High must preserve its original4/5-car budgets");
        }
        for (auto& pose : poses) {
            pose.car.object_to_world[14] = 10 + float(pose.actor_id) / 2;
        }
    }
}

void canonical(SDL_Window* window, gpu::Renderer& renderer, const std::filesystem::path& root) {
    const AssetStore assets(root);
    for (const auto track : {0u, 2u, 7u}) {
        const auto resources = load_scene_resources(assets, resolve_setup({.track_id = track, .weather_choice = 1}, 1));
        const HpcSurface surface(resources.collision_hpc);
        const auto start = single_race_start(resources.grid);
        const auto body = initial_body_pose(surface, start, 0);
        const FollowCamera follow(start.heading);
        const HorizonActors actors(resources.moving_horizon);
        SceneView view(renderer, resources);
        const std::vector<std::uint8_t> flags(resources.flags.model.nodes.size());
        for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
            gpu::check(SDL_SetWindowSize(window, size[0], size[1]), "Resize private visibility target");
            gpu::check(SDL_SyncWindow(window), "Synchronize private visibility target");
            SDL_PumpEvents();
            const CameraInputs camera{follow.world_to_view(body), default_projection(3, size[0], size[1])};
            const CarPose car{body.object_to_world};
            view.update_visibility(camera, car, actors.frame());
            const auto snapshot = view.visibility();
            const auto frame = view.frame(camera, scene_lighting(resources.setup), car, flags, actors.frame());
            const auto pixels = render(renderer, frame, "visibility-track" + std::to_string(track) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
            expect(!snapshot.terrain_sectors.empty() && view.player_visibility()
                       && std::ranges::count_if(pixels,
                              [](auto pixel) {
                                  return pixel != gpu::background;
                              })
                              > 1000,
                "Authored Donington/Thruxton/Knockhill grid scenes must show actual selected terrain, local car and scenery at all three aspects");
            expect(view.visibility().ordered_sectors == snapshot.ordered_sectors && view.visibility().root_masks == snapshot.root_masks,
                "Canonical GPU preparation must preserve the25Hz visibility snapshot");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 1 || argc == 2, "Usage: scene-visibility-render-tests [canonical-root]");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private visibility fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA visibility", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private visibility GPU window");
        gpu::Renderer renderer(window.get());
        source_order(renderer);
        frame_reuse(renderer);
        shared_vertex_materials(renderer);
        hidden_children_and_snapshot(renderer);
        terrain_lod_and_skids(renderer);
        authored_camera_ancestor(renderer);
        horizon_special_projection(renderer);
        rival_detail_ranges(renderer);
        if (argc == 2) {
            canonical(window.get(), renderer, argv[1]);
        }
        std::cout << "PASS integrated source visibility/order, root descendants, sector LOD, skid filters and HOR projection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
