#include "assets/store.hpp"
#include "formats/glb.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/follow_camera.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "scene_models/props.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace toca::test::scene_models;

void mega_distance_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& resources, std::string_view track) {
    using namespace toca::scene;
    const toca::HpcSurface surface(resources.collision_hpc);
    toca::SourceRandom random(7);
    FlagAnimation flags(resources.flags, random);
    HorizonActors actors(resources.moving_horizon);
    SceneView view(renderer, resources);
    auto middle_route = resources.route_points;
    const auto route_count =
        std::uint32_t(middle_route[0]) | (std::uint32_t(middle_route[1]) << 8) | (std::uint32_t(middle_route[2]) << 16) | (std::uint32_t(middle_route[3]) << 24);
    expect(route_count > 36, "Mega distance fixture needs a complete canonical route");
    // The recovered placement consumer chooses count-18. A shorter logical
    // POINTS prefix selects a real mid-lap record through that same consumer.
    put32(middle_route, 0, route_count / 2 + 18);
    const std::array starts{single_qualifying_start(resources.route_points), single_qualifying_start(middle_route)};
    constexpr std::array names{"qualifying", "mid-lap"};
    std::size_t added_vertices = 0, changed_pixels = 0, maximum_vertices = 0;
    for (std::size_t viewpoint = 0; viewpoint < starts.size(); ++viewpoint) {
        const auto body = initial_body_pose(surface, starts[viewpoint], 0);
        FollowCamera follow(viewpoint == 0 ? qualifying_camera_heading(resources.setup.selection.track_id) : -follow_direction_angle(body.basis[0]), false, 0, false, 4);
        follow.update(body, viewpoint == 0);
        std::array<std::size_t, 2> vertices{}, triangles{};
        std::array<std::vector<std::uint32_t>, 2> images;
        for (std::size_t distance = 0; distance < images.size(); ++distance) {
            const CameraInputs camera{follow.world_to_view(body), default_projection(distance == 0 ? 3 : 4, 640, 480)};
            const auto frame = view.frame(camera, scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            vertices[distance] = projected.vertices.size();
            triangles[distance] = projected.indices.size() / 3;
            maximum_vertices = std::max(maximum_vertices, vertices[distance]);
            for (const auto& draw : projected.draws) {
                expect(draw.vertex_offset >= 0 && std::size_t(draw.first) + draw.count <= projected.indices.size(), "Mega distance draw has invalid GPU bounds");
                for (std::size_t index = draw.first; index < std::size_t(draw.first) + draw.count; ++index) {
                    expect(std::size_t(draw.vertex_offset) + projected.indices[index] < projected.vertices.size(), "Mega distance draw references a missing vertex");
                }
            }
            expect(renderer.draw(frame, false), "Mega distance GPU target was unavailable");
            images[distance] = renderer.readback();
            renderer.save_screenshot("mega-distance-" + std::string(track) + "-" + names[viewpoint] + "-" + (distance == 0 ? "250" : "500") + ".png");
        }
        expect(vertices[1] >= vertices[0] && triangles[1] >= triangles[0], "Longer Mega range must retain the nearer geometry with identical assets and camera");
        std::size_t differences = 0;
        for (std::size_t pixel = 0; pixel < images[0].size(); ++pixel) {
            differences += images[0][pixel] != images[1][pixel];
        }
        added_vertices += vertices[1] - vertices[0];
        changed_pixels += differences;
        std::cout << "PASS: Mega distance " << track << ' ' << names[viewpoint] << " identical assets/camera: far250 " << vertices[0] << " vertices/" << triangles[0]
                  << " triangles, far500 " << vertices[1] << " vertices/" << triangles[1] << " triangles, " << differences << " changed GPU pixels\n";
    }
    expect(added_vertices > 0 && changed_pixels > 100, "Extended Mega range must expose additional canonical scene geometry and real GPU pixels");
    std::cout << "PASS: Mega distance " << track << " maximum frame " << maximum_vertices << " vertices\n";
}

void mega_horizon_motion(const toca::scene::HorizonActorResources& original, const toca::scene::HorizonActorResources& mega) {
    using namespace toca::scene;
    expect(original.moving_roots == mega.moving_roots && original.rotorcraft_root == mega.rotorcraft_root && original.rotorcraft_child == mega.rotorcraft_child,
        "Mega flight models must retain original actor and helicopter registrations");
    HorizonActors canonical(original), detailed(mega);
    for (unsigned tick = 0; tick <= 192; ++tick) {
        const auto& before = canonical.frame();
        const auto& after = detailed.frame();
        expect(before.moving.transforms == after.moving.transforms && before.moving.visible == after.moving.visible && before.rotorcraft.transforms == after.rotorcraft.transforms
                   && before.rotorcraft.visible == after.rotorcraft.visible && before.updates == after.updates && before.moving_count == after.moving_count
                   && before.rotorcraft_state == after.rotorcraft_state && before.rotorcraft_speed == after.rotorcraft_speed
                   && before.rotorcraft_position == after.rotorcraft_position && before.camera_zero.has_value() == after.camera_zero.has_value(),
            "Mega flight geometry must preserve every original route transform, visibility phase and controller update");
        if (before.camera_zero) {
            expect(before.camera_zero->world_to_view == after.camera_zero->world_to_view && before.camera_zero->position_q16 == after.camera_zero->position_q16
                       && before.camera_zero->anchor_position_q16 == after.camera_zero->anchor_position_q16 && before.camera_zero->rotorcraft == after.camera_zero->rotorcraft,
                "Mega models must preserve the original moving camera and listener pose");
        }
        canonical.update();
        detailed.update();
    }
}

void mega_horizon_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& original, const toca::scene::SceneResources& mega, bool helicopter, std::size_t actor,
    std::string_view name) {
    using namespace toca::scene;
    const auto& source = helicopter ? original.moving_horizon.rotorcraft : original.moving_horizon.moving;
    const auto& generated = helicopter ? mega.moving_horizon.rotorcraft : mega.moving_horizon.moving;
    const auto root = helicopter ? original.moving_horizon.rotorcraft_root : original.moving_horizon.moving_roots.at(actor);
    HorizonActors actors(original.moving_horizon);
    for (unsigned tick = 0; tick < 150; ++tick) {
        actors.update();
    }
    auto pose = helicopter ? actors.frame().rotorcraft : actors.frame().moving;
    const auto belongs = [&](std::size_t node) {
        auto current = std::int32_t(node);
        while (current >= 0 && std::uint32_t(current) != root) {
            current = generated.nodes.at(std::size_t(current)).parent;
        }
        return current >= 0;
    };
    for (std::size_t node = 0; node < pose.visible.size(); ++node) {
        pose.visible[node] = std::uint8_t(pose.visible[node] && belongs(node));
    }
    const auto matrices = horizon_actor_world_transforms(generated, pose);
    std::array<float, 3> minimum{32768, 32768, 32768}, maximum{-32768, -32768, -32768};
    for (std::size_t node = 0; node < generated.nodes.size(); ++node) {
        const auto& binding = generated.nodes[node];
        if (!pose.visible[node] || binding.variants.empty() || binding.variants[0] == 65535) {
            continue;
        }
        const auto& matrix = matrices[node];
        for (const auto& vertex : generated.meshes.at(binding.variants[0]).vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto world = vertex.position[0] * matrix[axis] + vertex.position[1] * matrix[4 + axis] + vertex.position[2] * matrix[8 + axis] + matrix[12 + axis];
                minimum[axis] = std::min(minimum[axis], world);
                maximum[axis] = std::max(maximum[axis], world);
            }
        }
    }
    const auto extent = std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]});
    expect(extent > 0 && extent < 200, "Mega flight closeup requires bounded actual actor geometry");
    std::size_t material_changes = 0, geometry_changes = 0, generated_triangles = 0, original_triangles = 0;
    const bool fixed_wing = name == "airmark" || name == "psp-jet" || name == "plane1" || name == "plane2";
    for (unsigned side = 0; side < (fixed_wing ? 4u : 2u); ++side) {
        // A low wing can look connected from above while leaving daylight
        // between its root and the rounded belly. Inspect both undersides too.
        const bool underside = side >= 2;
        const auto& matrix = matrices.at(root);
        std::array<float, 3> direction{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            direction[axis] = matrix[axis] * (side % 2 ? -1.f : 1.f) + matrix[4 + axis] * .35f + matrix[8 + axis] * (underside ? .7f : -.3f);
        }
        const auto length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        std::array<std::int32_t, 3> target{};
        std::array<SceneCamera, 1> cameras{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto center = (minimum[axis] + maximum[axis]) / 2;
            target[axis] = std::int32_t(center * 65536.f);
            cameras[0].transform[12 + axis] = std::int32_t((center + direction[axis] / length * extent * (underside ? 1.1f : 2.f)) * 65536.f);
        }
        const FinishCamera look(cameras);
        const CameraInputs camera{look.world_to_view(target), default_projection(4, 640, 480)};
        const auto render = [&](bool geometry, bool materials, std::string_view label) {
            SceneResources isolated{};
            isolated.setup = mega.setup;
            isolated.moving_horizon.texture_palette = original.moving_horizon.texture_palette;
            auto& model = helicopter ? isolated.moving_horizon.rotorcraft : isolated.moving_horizon.moving;
            model = geometry ? generated : source;
            model.textures = materials ? generated.textures : source.textures;
            HorizonFrame frame_pose{};
            (helicopter ? frame_pose.rotorcraft : frame_pose.moving) = pose;
            SceneView view(renderer, isolated);
            auto frame = view.frame(camera, scene_lighting(isolated.setup), {unit}, {}, frame_pose);
            const auto projected = toca::test::materialize_world(frame);
            const auto triangles = projected.indices.size() / 3;
            if (geometry && triangles == 0) {
                throw std::runtime_error("Mega flight closeup has no projected geometry: " + std::string(name) + " side" + std::to_string(side));
            }
            // A canonical one-sided jet billboard can disappear from its back.
            // Draw that actual empty result on the normal GPU clear color.
            if (frame.draws.empty()) {
                frame.quad({.depth_write = false}, 0, 0, 640, 480, toca::gpu::background);
            }
            expect(renderer.draw(frame, false), "Mega flight closeup target was unavailable");
            renderer.save_screenshot("scene-horizon-" + std::string(name) + "-side" + std::to_string(side) + "-" + std::string(label) + ".png");
            return std::pair{renderer.readback(), triangles};
        };
        const auto canonical = render(false, false, "very-high");
        const auto textured = render(false, true, "ai-original-geometry");
        const auto detailed = render(true, true, "mega");
        original_triangles += textured.second;
        generated_triangles += detailed.second;
        std::size_t visible = 0;
        for (std::size_t pixel = 0; pixel < detailed.first.size(); ++pixel) {
            material_changes += canonical.first[pixel] != textured.first[pixel];
            geometry_changes += textured.first[pixel] != detailed.first[pixel];
            visible += detailed.first[pixel] != toca::gpu::background;
        }
        if (visible < 40) {
            throw std::runtime_error("Mega flight closeup must show a real solid actor from both sides: " + std::string(name));
        }
        if (name == "psp-jet") {
            // The original PSP image is an alpha-keyed side billboard. Projecting
            // that entire image onto a rounded body previously cut the fuselage
            // into detached pieces, despite passing the overall pixel checks.
            // Trace the interior of the authored hull, away from its nose/tail
            // caps, and require a continuous rendered body from either side.
            auto body = generated.nodes.size();
            for (std::size_t node = 0; node < generated.nodes.size(); ++node) {
                const auto& variants = generated.nodes[node].variants;
                if (pose.visible[node] && !variants.empty() && variants[0] != 65535 && generated.meshes.at(variants[0]).name == "PSP1_1") {
                    body = node;
                    break;
                }
            }
            expect(body < generated.nodes.size(), "PSP closeup must contain its visible fuselage mesh");
            const auto& body_matrix = matrices.at(body);
            constexpr std::array<std::array<float, 2>, 4> axis{{{-1.8f, -10.98f}, {-.5f, -10.78f}, {2.f, -10.38f}, {4.5f, -10.f}}};
            std::size_t covered = 0, gap = 0, longest_gap = 0;
            for (unsigned sample = 0; sample <= 100; ++sample) {
                const auto y = -1.5f + 5.f * float(sample) / 100.f;
                std::size_t segment = 0;
                while (segment + 2 < axis.size() && y > axis[segment + 1][0]) {
                    ++segment;
                }
                const auto fraction = (y - axis[segment][0]) / (axis[segment + 1][0] - axis[segment][0]);
                const std::array<float, 3> local{-.038f, y, axis[segment][1] + fraction * (axis[segment + 1][1] - axis[segment][1])};
                std::array<float, 3> world{}, view{};
                for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
                    world[coordinate] =
                        local[0] * body_matrix[coordinate] + local[1] * body_matrix[4 + coordinate] + local[2] * body_matrix[8 + coordinate] + body_matrix[12 + coordinate];
                }
                for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
                    view[coordinate] = world[0] * camera.world_to_view[coordinate] + world[1] * camera.world_to_view[4 + coordinate]
                                       + world[2] * camera.world_to_view[8 + coordinate] + camera.world_to_view[12 + coordinate];
                }
                expect(view[2] > .25f, "PSP fuselage trace must remain in front of the closeup camera");
                const auto x = int(std::floor((1 + view[0] * camera.projection.x / view[2]) * 320.f));
                const auto screen_y = int(std::floor((1 - view[1] * camera.projection.y / view[2]) * 240.f));
                expect(x > 0 && x < 639 && screen_y > 0 && screen_y < 479, "PSP fuselage trace must remain inside the GPU closeup");
                unsigned solid = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        solid += detailed.first.at(std::size_t(screen_y + dy) * 640 + std::size_t(x + dx)) != toca::gpu::background;
                    }
                }
                const bool occupied = solid >= 5;
                covered += occupied;
                gap = occupied ? 0 : gap + 1;
                longest_gap = std::max(longest_gap, gap);
            }
            if (covered < 97 || longest_gap > 3) {
                throw std::runtime_error("PSP fuselage must remain opaque and connected: side" + std::to_string(side) + ", " + std::to_string(covered)
                                         + "/101 covered axis samples, longest gap " + std::to_string(longest_gap));
            }
            std::cout << "PASS: PSP fuselage side" << side << " connected GPU coverage " << covered << "/101, longest gap " << longest_gap << '\n';
        }
    }
    if (generated_triangles <= original_triangles + 8 || material_changes < 40 || geometry_changes < 80) {
        throw std::runtime_error("Flight closeup must show new AI materials and independent three-dimensional geometry: " + std::string(name));
    }
    std::cout << "PASS: Mega " << name << " both-side GPU views, " << generated_triangles << " visible triangles, " << material_changes << " material pixels, " << geometry_changes
              << " geometry pixels\n";
}

void mega_reference(const std::filesystem::path& root, const std::filesystem::path& mega_root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    GpuFixture gpu("TOCA Mega generic props", 640, 480);
    auto& renderer = gpu.renderer;
    const auto vertices = [](const toca::Model& model) {
        std::size_t count = 0;
        for (const auto& mesh : model.meshes) {
            count += mesh.vertices.size();
        }
        return count;
    };
    for (std::uint32_t track = 0; track < tracks.size(); ++track) {
        const auto canonical = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 3}));
        const auto mega = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 4}), mega_root);
        const auto verify = [&](const toca::Model& model, std::string_view family) {
            if (model.nodes.empty()) {
                return;
            }
            const auto file = mega_root / "scene" / ("track-" + std::to_string(track)) / (std::string(family) + ".glb");
            const auto packed = toca::load_glb(file);
            expect(model.meshes.size() == packed.meshes.size() && model.nodes.size() == packed.nodes.size(), "Mega family must install the generated GLB resource");
            for (std::size_t index = 0; index < model.meshes.size(); ++index) {
                const auto& actual = model.meshes[index];
                const auto& expected = packed.meshes[index];
                expect(actual.name == expected.name && actual.vertices.size() == expected.vertices.size() && actual.faces.size() == expected.faces.size()
                           && std::memcmp(actual.vertices.data(), expected.vertices.data(), actual.vertices.size() * sizeof(toca::ModelVertex)) == 0,
                    "Runtime Mega family must use the generated geometry rather than falling back");
            }
            expect(std::ranges::all_of(model.textures,
                       [](const auto& texture) {
                           return texture.encoding == toca::ImageEncoding::webp;
                       }),
                "Every generated generic prop/scenery material must use embedded or shared WebP");
        };
        verify(mega.cones.model, "cones");
        verify(mega.posts.model, "posts");
        verify(mega.signs.model, "signs");
        verify(mega.trees, "trees");
        verify(mega.fence, "fence");
        verify(mega.objects, "objects");
        verify(mega.moving_horizon.moving, "moving");
        verify(mega.moving_horizon.rotorcraft, "rotorcraft");
        expect((mega.moving_horizon.moving.nodes.empty() || vertices(mega.moving_horizon.moving) > vertices(canonical.moving_horizon.moving))
                   && (mega.moving_horizon.rotorcraft.nodes.empty() || vertices(mega.moving_horizon.rotorcraft) > vertices(canonical.moving_horizon.rotorcraft)),
            "Mega aircraft, helicopters and balloons must install genuinely rebuilt higher-detail geometry");
        mega_horizon_motion(canonical.moving_horizon, mega.moving_horizon);
        expect(mega.ground.sector_centers == canonical.ground.sector_centers && mega.collision_hpc == canonical.collision_hpc && mega.route_points == canonical.route_points
                   && mega.cones.actors.size() == canonical.cones.actors.size() && mega.signs.actors.size() == canonical.signs.actors.size(),
            "Mega presentation must preserve original collision, sectors, routes and prop registrations");
        expect(mega.objects_baked.chunks.size() == canonical.objects_baked.chunks.size() && mega.fence_baked.chunks.size() == canonical.fence_baked.chunks.size(),
            "Generated static scenery must retain the original sector chunk registrations");
        if (track == 0) {
            mega_prop_pixels(renderer, canonical, mega, &SceneResources::cones, "cones");
            mega_prop_pixels(renderer, canonical, mega, &SceneResources::posts, "posts");
            mega_prop_pixels(renderer, canonical, mega, &SceneResources::signs, "signs");
            mega_static_pixels(renderer, canonical, mega, false);
            mega_static_pixels(renderer, canonical, mega, true);
            mega_horizon_pixels(renderer, canonical, mega, false, 0, "airmark");
            mega_horizon_pixels(renderer, canonical, mega, false, 1, "psp-jet");
        }
        if (track == 1) {
            mega_horizon_pixels(renderer, canonical, mega, true, 0, "helicopter");
        }
        if (track == 2) {
            mega_horizon_pixels(renderer, canonical, mega, false, 0, "plane1");
            mega_horizon_pixels(renderer, canonical, mega, false, 2, "plane2");
        }
        if (track == 6) {
            mega_horizon_pixels(renderer, canonical, mega, false, 0, "balloon");
            mega_horizon_pixels(renderer, canonical, mega, false, 1, "payday-balloon");
            mega_horizon_pixels(renderer, canonical, mega, false, 2, "audi-airship");
        }
        if (track == 9) {
            expect(mega.cones.model.meshes.size() == 1 && mega.cones.model.meshes[0].name == "GARCON_1" && mega.cones.model.textures.size() == 2
                       && mega.cones.model.textures[0].name == "GARG01" && mega.cones.model.textures[1].name == "GARG02",
                "Mega Lavaland must retain the gargoyle identity and both source material slots");
            for (std::size_t node = 0; node < canonical.cones.model.nodes.size(); ++node) {
                expect(mega.cones.model.nodes.at(node).transform == canonical.cones.model.nodes[node].transform
                           && mega.cones.model.nodes[node].variants == canonical.cones.model.nodes[node].variants,
                    "Mega gargoyle relief must preserve every physical actor pivot and mesh binding");
            }
            const auto visual = prepare_cone_model(mega.cones.model, track);
            float bottom = std::numeric_limits<float>::max();
            for (const auto& vertex : visual.meshes[0].vertices) {
                bottom = std::min(bottom, vertex.position[1]);
            }
            const auto clearance = float(0x75c2) / 65536.f + bottom;
            expect(clearance >= 0 && clearance < .006f, "New Mega gargoyle geometry must preserve the corrected upright ground clearance");
            mega_prop_pixels(renderer, canonical, mega, &SceneResources::cones, "gargoyle", 960, 540, true);
        }
        if (track < 2) {
            mega_distance_pixels(renderer, mega, tracks[track].name);
        }
        std::cout << "PASS: Mega " << tracks[track].name << " generated generic props/scenery, WebP materials and original physics: " << vertices(mega.cones.model)
                  << " cone vertices, " << vertices(mega.posts.model) << " post vertices, " << vertices(mega.signs.model) << " sign vertices\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (!(argc == 3)) {
            throw std::runtime_error("Usage: toca-scene-model-mega-tests CANONICAL_ROOT MEGA_ROOT");
        }
        mega_reference(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
