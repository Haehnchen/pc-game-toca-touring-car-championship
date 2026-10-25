#include "assets/store.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void family_selection(toca::gpu::Renderer& renderer) {
    using namespace toca;
    using namespace toca::scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    SceneResources resources;
    resources.setup = resolve_setup({.weather_choice = 1});
    resources.car.car_id = 7;
    frontend::CarModels recorded;
    recorded.car_id = 1;
    const auto family = [&](std::size_t lod, std::uint8_t red) {
        Model result;
        result.meshes.push_back(
            {"ghost-lod", {{{-.5f, -.5f, 0}, {0, 0, -1}, 0, 0}, {{0, .5f, 0}, {0, 0, -1}, .5f, 1}, {{.5f, -.5f, 0}, {0, 0, -1}, 1, 0}}, {{{0, 1, 2}, -1, {red, 30, 60, 255}}}});
        for (std::size_t node = 0; node <= lod; ++node) {
            result.nodes.push_back({std::int16_t(node == 0 ? -1 : 0), identity, node == 0 ? 501u : 20u + std::uint32_t(node), {0}});
        }
        return result;
    };
    for (std::size_t lod = 0; lod < 3; ++lod) {
        resources.car.models[lod] = family(lod, std::uint8_t(40 + lod));
        recorded.models[lod] = family(lod, std::uint8_t(100 + lod));
    }
    SceneView view(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
    view.set_ghost_model(renderer, recorded);
    CameraInputs camera{identity, default_projection(3, 640, 480)};
    camera.canonical_single_player = true;
    CarPose player{identity}, ghost{identity};
    player.object_to_world[12] = 3;
    player.object_to_world[14] = ghost.object_to_world[14] = 8;
    ghost.actor_id = 1;
    ghost.remap_livery = false;
    const auto frame = [&](bool same_car = false) {
        return view.frame(camera, {{0, 0, -1}, 0, {255, 255, 255}}, player, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, &ghost, same_car);
    };
    const auto verify = [&](const gpu::Frame& drawn, std::size_t lod, bool same_car) {
        std::size_t ghost_nodes = 0, player_nodes = 0;
        const auto wanted = 0xff001e3cu | (std::uint32_t((same_car ? 40 : 100) + lod) << 16);
        for (const auto& draw : drawn.draws) {
            if (!draw.mesh) {
                continue;
            }
            if (draw.state.blend) {
                ++ghost_nodes;
                expect(draw.mesh->vertices.front().color == wanted && (drawn.model_instances.at(draw.model_instance).ambient_flags[3] & 2u),
                    "The selected canonical Ghost family must retain its own geometry/material pass");
            } else {
                ++player_nodes;
                expect(draw.mesh->vertices.front().color == 0xff281e3cu, "Ghost LOD transitions must leave the player's HIGH family unchanged");
            }
        }
        expect(ghost_nodes == lod + 1 && player_nodes == 1, "Ghost LOD transitions must use the selected family's actual independent node count");
    };
    for (const auto lod : {0u, 1u, 2u, 0u}) {
        ghost.render_selection = CarRenderSelection{std::uint8_t(lod), 1, std::vector<std::uint8_t>(lod + 1, 1), std::vector<std::int16_t>(lod + 1, 0)};
        verify(frame(), lod, false);
        verify(frame(true), lod, true);
    }
    ghost.render_selection.reset();
    for (const auto& [distance, lod] : {std::pair{40.f, 1u}, std::pair{200.f, 2u}, std::pair{8.f, 0u}}) {
        ghost.object_to_world[14] = distance;
        view.update_visibility(camera, player, {}, {}, &ghost);
        verify(frame(), lod, false);
    }
}

void wheel_housings(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source) {
    using namespace toca;
    using namespace toca::scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Lighting light{{0, 0, -1}, 0, {255, 255, 255}};
    const CameraInputs camera{identity, default_projection(3, 640, 480)};
    CarPose pose{identity}, absent{identity};
    pose.object_to_world[12] = 1;
    pose.object_to_world[14] = 8;
    absent.object_to_world[13] = -1000;
    for (const bool housing_first : {false, true}) {
        SceneResources resources{};
        resources.setup = source.setup;
        resources.setup.selection.detail = 3;
        ModelMesh mesh;
        mesh.vertices = {{{-1, 1, 0}, {0, 0, -1}, 0, 0}, {{1, 1, 0}, {0, 0, -1}, 1, 0}, {{0, -1, 2}, {0, 0, -1}, .5f, 1}};
        // The mouth stays fixed; only the inner edge contracts to20% depth.
        // Sharing every vertex with the exterior tests both cache visitation orders.
        mesh.ghost_positions = {{-1, 1, 0}, {1, 1, 0}, {0, -1, .4f}};
        mesh.faces = {{{0, 1, 2}, -1, {255, 255, 255, 255}, housing_first}, {{0, 1, 2}, -1, {255, 255, 255, 255}, !housing_first}};
        auto local = identity;
        local[12] = .5f;
        auto& model = resources.car.models[0];
        model.nodes.push_back({-1, local, 0, {0}});
        model.meshes.push_back(mesh);
        ModelMesh wheel;
        wheel.vertices = {{{-.5f, .5f, 0}, {0, 0, -1}, 0, 0}, {{.5f, .5f, 0}, {0, 0, -1}, 1, 0}, {{0, -.5f, 0}, {0, 0, -1}, .5f, 1}};
        wheel.faces = {{{0, 1, 2}, -1, {255, 255, 255, 255}}};
        model.meshes.push_back(wheel);
        auto wheel_transform = identity, child_transform = identity;
        wheel_transform[12] = -2;
        child_transform[12] = -.25f;
        child_transform[13] = -2;
        model.nodes.push_back({0, wheel_transform, 1, {1}});
        model.nodes.push_back({1, child_transform, 100, {1}});
        SceneView view(renderer, resources);
        view.set_ghost_model(renderer, resources.car);
        const auto draw = [&](SceneView& drawn, bool ghost, bool player_model = false) {
            return drawn.frame(camera, light, ghost ? absent : pose, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, ghost ? &pose : nullptr, player_model);
        };
        const auto selected_vertices = [&](const gpu::Frame& source_frame, bool ghost) {
            const auto frame = toca::test::materialize_world(source_frame);
            std::vector<gpu::Vertex> result;
            for (const auto& command : frame.draws) {
                if (!command.state.depth || command.state.blend != ghost) {
                    continue;
                }
                expect(command.state.depth_write, "Reduced Ghost housings must retain scene depth writes");
                for (std::size_t offset = command.first; offset < command.first + command.count; ++offset) {
                    result.push_back(frame.vertices.at(std::size_t(command.vertex_offset) + frame.indices.at(offset)));
                }
            }
            return result;
        };
        const auto expected = [&](bool ghost, bool mega = true) {
            gpu::Frame frame;
            const auto append = [&](const ModelMesh& mesh, std::array<float, 3> offset, bool wheel) {
                for (const auto& face : mesh.faces) {
                    std::array<LitVertex, 3> triangle;
                    for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                        const auto index = face.indices[corner];
                        auto point = ghost && mega && face.ghost_hidden ? mesh.ghost_positions[index] : mesh.vertices[index].position;
                        for (std::size_t axis = 0; axis < point.size(); ++axis) {
                            point[axis] += offset[axis];
                        }
                        const auto color = ghost && mega ? (face.ghost_hidden || wheel ? 0x609696ffu : 0x969696ffu) : 0xffffffffu;
                        triangle[corner] = {point, color, mesh.vertices[index].u, mesh.vertices[index].v};
                    }
                    append_projected_triangle(frame, {.depth = true, .blend = ghost}, triangle, camera);
                }
            };
            append(mesh, {1.5f, 0, 8}, false);
            append(wheel, {-.5f, 0, 8}, true);
            append(wheel, {-.75f, -2, 8}, true);
            return selected_vertices(frame, ghost);
        };
        const auto compare = [](const auto& actual, const auto& wanted) {
            expect(actual.size() == 12 && actual.size() == wanted.size(), "Housing, exterior, wheel and wheel-child triangles must remain visible");
            for (std::size_t index = 0; index < actual.size(); ++index) {
                const auto& a = actual[index];
                const auto& b = wanted[index];
                expect(std::abs(a.x - b.x) < .0001f && std::abs(a.y - b.y) < .0001f && std::abs(a.z - b.z) < .000001f && std::abs(a.rhw - b.rhw) < .000001f
                           && a.diffuse == b.diffuse && a.u == b.u && a.v == b.v,
                    "Authored Ghost contraction, pinned mouth, interior/wheel opacity or ordinary shared geometry changed");
            }
        };
        const auto ghost = draw(view, true);
        compare(selected_vertices(ghost, true), expected(true));
        compare(selected_vertices(draw(view, true, true), true), expected(true));
        compare(selected_vertices(draw(view, false), false), expected(false));
        auto original = resources;
        for (auto& mesh : original.car.models[0].meshes) {
            mesh.ghost_positions.clear();
            for (auto& face : mesh.faces) {
                face.ghost_hidden = false;
            }
        }
        SceneView original_view(renderer, original);
        original_view.set_ghost_model(renderer, original.car);
        compare(selected_vertices(draw(original_view, true), true), expected(true, false));
        compare(selected_vertices(draw(original_view, false), false), expected(false, false));
        expect(renderer.draw(ghost, false), "Authored Ghost housing fixture requires its private GPU drawable");
        const auto pixels = renderer.readback();
        expect(std::ranges::count_if(pixels,
                   [](auto pixel) {
                       return pixel != gpu::background;
                   })
                   > 100,
            "Restored inner housings and exterior must produce actual Ghost pixels");
    }
}

void occlusion(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source, const toca::frontend::CarModels& recorded) {
    using namespace toca;
    using namespace toca::scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Lighting light{{0, 0, -1}, 0, {255, 255, 255}};
    CarPose absent{identity}, ghost{identity};
    absent.object_to_world[13] = -1000;
    ghost.object_to_world[14] = 8;
    for (const bool dynamic : {false, true}) {
        for (const bool foreground : {false, true}) {
            for (const bool player_model : {false, true}) {
                for (const bool late : {false, true}) {
                    SceneResources resources{};
                    resources.setup = source.setup;
                    resources.car = source.car;
                    for (std::int32_t row = 0; row < 3; ++row) {
                        for (std::int32_t column = 0; column < 3; ++column) {
                            resources.ground.sector_centers.push_back({(column - 1) * 64 * 65536, (1 - row) * 64 * 65536});
                        }
                    }
                    // The same rail spans the view, while its owning sector
                    // changes submission order. This models long guardrails
                    // whose registration does not describe each face's depth.
                    const float registration = late ? 64.f : 0.f;
                    const float z = foreground ? 4.f : 12.f;
                    ModelMesh rail;
                    const float right = foreground ? 0.f : 8.f;
                    rail.vertices = {{{-8, 8, z}, {0, 0, -1}, 0, 0}, {{right, 8, z}, {0, 0, -1}, 1, 0}, {{right, -8, z}, {0, 0, -1}, 1, 1}, {{-8, -8, z}, {0, 0, -1}, 0, 1}};
                    rail.faces = {{{0, 1, 2}, -1, {255, 0, 0, 255}}, {{0, 2, 3}, -1, {255, 0, 0, 255}}};
                    if (dynamic) {
                        auto transform = identity;
                        transform[14] = registration;
                        for (auto& vertex : rail.vertices) {
                            vertex.position[2] -= registration;
                        }
                        resources.fence.nodes.push_back({-1, transform, 0, {0}});
                        resources.fence.meshes.push_back(rail);
                    } else {
                        resources.fence_baked.chunks.push_back({0, 0, {0, std::int32_t(registration * 65536)}, rail, {0, 0}});
                    }
                    SceneView view(renderer, resources);
                    view.set_ghost_model(renderer, recorded);
                    const CameraInputs camera{identity, default_projection(3, 640, 480)};
                    view.update_visibility(camera, absent, {}, {}, &ghost);
                    const auto frame = [&](const CarPose* pose) {
                        return view.frame(camera, light, absent, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, pose, player_model);
                    };
                    const auto render = [&](const gpu::Frame& value) {
                        auto independent = value;
                        independent.color_load = gpu::ColorLoad::clear;
                        expect(renderer.draw(independent, false), "Ghost occlusion fixture requires its private GPU drawable");
                        return renderer.readback();
                    };
                    const auto pixels = render(frame(&ghost));
                    const auto rail_only = render(frame(nullptr));
                    auto silhouette = frame(&ghost);
                    std::erase_if(silhouette.draws, [](const auto& draw) {
                        return !draw.state.blend;
                    });
                    const auto ghost_only = render(silhouette);
                    expect(view.visibility().ordered_sectors.size() >= 2, "Ghost occlusion fixture must select both owning sectors");
                    std::size_t overlap = 0, visible = 0;
                    for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
                        if (ghost_only[pixel] == gpu::background) {
                            continue;
                        }
                        if (rail_only[pixel] != gpu::background) {
                            ++overlap;
                            expect(foreground ? pixels[pixel] == rail_only[pixel] : pixels[pixel] != rail_only[pixel],
                                "Foreground rail must occlude the ghost; a background rail must not replace it");
                            if (!foreground && late) {
                                expect(pixels[pixel] == ghost_only[pixel], "Ghost depth must reject a later background rail at every covered pixel");
                            }
                        } else {
                            visible += pixels[pixel] != rail_only[pixel];
                        }
                    }
                    expect(overlap > 100 && (!foreground || visible > 100), "Rail probe must cover actual ghost pixels and retain its unoccluded half");
                }
            }
        }
    }
}

void run(const char* root) {
    using namespace toca;
    using namespace toca::scene;
    const AssetStore assets(root);
    const auto resources = load_scene_resources(assets, resolve_setup({0, 0, 1}));
    const HpcSurface surface(resources.collision_hpc);
    const auto body = initial_body_pose(surface, single_qualifying_start(resources.route_points), 0);
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA ghost check", 640, 480, 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create isolated ghost window");
    gpu::Renderer renderer(window.get());
    SceneView view(renderer, resources);
    const auto recorded = frontend::load_car_models(assets, 1);
    view.set_ghost_model(renderer, recorded);
    std::array<SceneCamera, 1> viewpoints{};
    viewpoints[0].transform[12] = body.position[0] + 4 * 65536;
    viewpoints[0].transform[13] = body.position[1] + 2 * 65536;
    viewpoints[0].transform[14] = body.position[2] - 4 * 65536;
    const FinishCamera look(viewpoints);
    const CameraInputs camera{look.world_to_view(body.position), default_projection(3, 640, 480)};
    const auto light = scene_lighting(resources.setup);
    const std::vector<std::uint8_t> flags(resources.flags.model.nodes.size());
    HorizonActors actors(resources.moving_horizon);
    CarPose absent{body.object_to_world}, ghost{body.object_to_world};
    absent.object_to_world[13] -= 1000;
    const auto draw = [&](const CarPose* pose, bool player_model) {
        return view.frame(camera, light, absent, flags, actors.frame(), {}, {}, nullptr, {}, {}, nullptr, nullptr, pose, player_model);
    };
    const auto render = [&](const gpu::Frame& frame, const char* capture) {
        auto independent = frame;
        independent.color_load = gpu::ColorLoad::clear;
        expect(renderer.draw(independent, false), "Ghost GPU target must be available");
        if (capture) {
            renderer.save_screenshot(capture);
        }
        return renderer.readback();
    };
    const auto baseline = render(draw(nullptr, false), nullptr);
    const auto frame = draw(&ghost, false);
    auto opaque = frame;
    const auto projected = toca::test::materialize_world(frame);
    std::size_t ghost_triangles = 0;
    for (const auto& command : projected.draws) {
        if (!command.state.blend || !command.state.depth) {
            continue;
        }
        ghost_triangles += command.count / 3;
        expect(command.state.depth_write, "Original ghost materials must retain scene depth writes");
        for (std::size_t offset = command.first; offset < command.first + command.count; ++offset) {
            const auto index = std::size_t(command.vertex_offset) + projected.indices[offset];
            expect((projected.vertices[index].diffuse >> 24) == (command.state.texture ? 150u : 255u),
                "Source software ghosts use alpha150 for textured materials and alpha255 for original untextured face RGB");
        }
    }
    // Preserve the GPU ghost material/lighting and source geometry. Disabling
    // blending gives the same solid RGB comparison as forcing vertex alpha255.
    for (auto& command : opaque.draws) {
        if (command.state.blend && command.state.depth) {
            command.state.blend = false;
        }
    }
    expect(ghost_triangles > 100, "Recorded Audi must contribute real CM3D triangles");
    const auto pixels = render(frame, "time-trial-audi-ghost.png");
    const auto solid = render(opaque, nullptr);
    std::size_t visible = 0, blended = 0;
    for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
        visible += pixels[pixel] != baseline[pixel];
        blended += pixels[pixel] != baseline[pixel] && pixels[pixel] != solid[pixel];
    }
    expect(visible > 1000 && blended > 500, "Ghost must visibly blend with actual track pixels");
    const auto player = render(draw(&ghost, true), "time-trial-new-honda-ghost.png");
    expect(player != pixels, "A new player best must replace the loaded Audi model with Honda");
    ghost.object_to_world[12] += 2;
    expect(render(draw(&ghost, false), nullptr) != pixels, "Playback body motion must change ghost pixels");
    expect(ghost_vertex_color({0, 0, -1}, {{0, 0, 1}, 1, {255, 255, 255}}) == 0x969696ff, "Source software ghost ambient must use the registered RGB150/150/255 face diffuse");
    wheel_housings(renderer, resources);
    occlusion(renderer, resources, recorded);
    family_selection(renderer);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected canonical data root");
        }
        toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated ghost renderer");
        run(argv[1]);
        SDL_Quit();
        std::cout << "Time Trial ghost material, saved car, movement and GPU blending passed\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
