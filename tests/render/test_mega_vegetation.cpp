#include "assets/store.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>

namespace {
struct TreeCase {
    std::string_view name, leaf_material, screenshot_prefix;
    std::array<std::size_t, 9> placements;
    float visible_height_fraction = 1.f;
};

constexpr TreeCase broad_tree{"TRIS04_1", "MegaTreeLeaf", "vegetation", {28, 4, 2, 40, 114, 28, 13, 1, 5}};
constexpr TreeCase slender_tree{"TRIS07_1", "MegaTreeLf7", "vegetation-tris07", {5, 0, 0, 6, 0, 5, 0, 0, 0}, .779f};
constexpr TreeCase tree_group{"TRGE17_1", "MegaGroveLf", "vegetation-trge17", {10, 0, 29, 5, 0, 10, 35, 0, 11}};
constexpr TreeCase large_broad_tree{"TRIS04B_1", "MegaTreeLeaf", "vegetation-tris04b", {13, 0, 0, 25, 0, 13, 0, 0, 0}};
constexpr TreeCase narrow_tree{"TRIS02B_1", "MegaTreeLf2", "vegetation-tris02b", {15, 0, 0, 0, 0, 15, 0, 0, 0}};
constexpr TreeCase stepped_tree{"TRIE01_1", "MegaTreeLf1", "vegetation-trie01", {0, 0, 0, 0, 0, 0, 16, 0, 0}, .9239691f};
constexpr TreeCase dense_group{"TRGS18_1", "MegaGrovLf4", "vegetation-trgs18", {0, 0, 0, 7, 4, 0, 0, 0, 0}};
constexpr TreeCase short_stem_group{"TRGS22_1", "MegaGrovLm4", "vegetation-trgs22", {0, 0, 0, 29, 0, 0, 0, 0, 0}, .9884021f};
constexpr TreeCase separated_group{"TRGS20_1", "MegaGrovLm2", "vegetation-trgs20", {0, 0, 0, 24, 0, 0, 0, 0, 0}};
constexpr std::array tree_cases{broad_tree, slender_tree, tree_group, large_broad_tree, narrow_tree, stepped_tree, dense_group, short_stem_group, separated_group};
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t verify(const toca::scene::SceneResources& original, const toca::scene::SceneResources& mega, const TreeCase& tree, std::size_t expected) {
    const auto& before = original.objects;
    const auto& after = mega.objects;
    expect(before.meshes.size() == after.meshes.size() && before.nodes.size() == after.nodes.size(), "Vegetation must preserve mesh and node slots");
    for (std::size_t i = 0; i < before.nodes.size(); ++i) {
        const auto& a = before.nodes[i];
        const auto& b = after.nodes[i];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants,
            "Vegetation must retain exact original transforms, parent, sector flags and variant registrations");
    }
    expect(after.textures.size() >= before.textures.size(), "Vegetation materials must be appended after the original material prefix");
    for (std::size_t i = 0; i < before.textures.size(); ++i) {
        expect(before.textures[i].name == after.textures[i].name, "Vegetation must preserve original material slot names");
    }
    std::size_t rebuilt = 0, rebuilt_faces = 0;
    for (std::size_t i = 0; i < before.meshes.size(); ++i) {
        if (before.meshes[i].name != tree.name) {
            continue;
        }
        expect(!before.meshes[i].faces.empty() && !after.meshes[i].faces.empty() && after.meshes[i].faces.size() != before.meshes[i].faces.size(),
            "Confirmed vegetation must install rebuilt geometry through the normal Mega resource loader");
        rebuilt_faces = after.meshes[i].faces.size();
        std::size_t bark = 0, leaves = 0, crown_surface = 0;
        for (const auto& face : after.meshes[i].faces) {
            expect(face.texture >= 0, "New vegetation must bind its authored materials");
            const auto& texture = after.textures.at(std::size_t(face.texture));
            expect(texture.encoding == toca::ImageEncoding::webp, "Tree materials must load their generated WebP pixels");
            const bool crown = (tree.name == tree_group.name && texture.name == "LHRTWS3") || (tree.name == narrow_tree.name && texture.name == "LHRTS2")
                               || (tree.name == dense_group.name && texture.name == "LHRTWS4") || (tree.name == short_stem_group.name && texture.name == "LMRTS4")
                               || (tree.name == separated_group.name && texture.name == "LMRTS2");
            expect(crown || std::size_t(face.texture) >= before.textures.size(), "New tree surfaces must use their appended materials or the authored crown motif");
            bark += texture.name == "MegaTreeBark";
            leaves += texture.name == tree.leaf_material;
            crown_surface += crown;
            for (const auto index : face.indices) {
                const auto& vertex = after.meshes[i].vertices.at(index);
                expect(std::isfinite(vertex.u) && std::isfinite(vertex.v) && vertex.u >= 0 && vertex.u <= 1 && vertex.v >= 0 && vertex.v <= 1,
                    "Simple vegetation must retain complete finite foliage/bark UVs inside its accepted materials");
            }
        }
        expect(bark > 0 && leaves > 0 && bark + leaves + crown_surface == after.meshes[i].faces.size(), "Tree must load solid bark and palette-matched foliage");
        ++rebuilt;
    }
    expect(rebuilt == (expected ? 1u : 0u), "Each circuit must retain its exact canonical tree mesh slots");
    expect(original.objects_baked.chunks.size() == mega.objects_baked.chunks.size(), "Vegetation must retain every original static chunk");
    std::size_t instances = 0;
    for (std::size_t i = 0; i < original.objects_baked.chunks.size(); ++i) {
        const auto& a = original.objects_baked.chunks[i];
        const auto& b = mega.objects_baked.chunks[i];
        expect(a.node_index == b.node_index && a.variant_index == b.variant_index && a.origin_xz == b.origin_xz, "Vegetation must preserve baked node, variant and sector origins");
        if (a.mesh.name == tree.name) {
            expect(b.mesh.name == tree.name && b.mesh.faces.size() == rebuilt_faces, "Every placed tree must consume the rebuilt mesh");
            ++instances;
        }
    }
    // Snetterton's five trees use dynamic roots, unlike the 230 statically
    // baked placements on the other circuits. Preserve both consumers.
    for (std::size_t i = 0; i < before.nodes.size(); ++i) {
        auto root = i;
        while (before.nodes[root].parent >= 0) {
            root = std::size_t(before.nodes[root].parent);
        }
        if (before.nodes[root].id_flags & 0x20000000u) {
            continue;
        }
        for (const auto mesh : before.nodes[i].variants) {
            instances += mesh != 65535 && before.meshes[mesh].name == tree.name;
        }
    }
    expect(instances == expected, "Vegetation must reach exactly the original placements on this circuit");
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "Tree presentation must preserve original terrain contacts, route and sector data");
    return instances;
}

void pixels(
    toca::gpu::Renderer& renderer, const toca::scene::SceneResources& original, const toca::scene::SceneResources& mega, std::uint32_t track, const TreeCase& tree, bool cleanup) {
    using namespace toca::scene;
    const auto& before = original.objects_baked;
    const auto& after = mega.objects_baked;
    auto selected = std::size_t(std::ranges::find_if(before.chunks, [&](const auto& chunk) {
        return chunk.mesh.name == tree.name;
    }) - before.chunks.begin());
    if (tree.name == slender_tree.name && track == 3) {
        // Node 135 stands at the open edge of the Brands grove. Other slender
        // placements are enclosed by much wider, still-original tree types.
        selected = std::size_t(std::ranges::find_if(before.chunks, [&](const auto& chunk) {
            return chunk.mesh.name == tree.name && chunk.node_index == 135;
        }) - before.chunks.begin());
        expect(selected < before.chunks.size(), "Brands surroundings view needs its original edge placement");
    }
    const bool dynamic = selected == before.chunks.size();
    std::size_t selected_node = 0, selected_mesh = 0;
    if (dynamic) {
        selected_mesh = std::size_t(std::ranges::find(original.objects.meshes, tree.name, &toca::ModelMesh::name) - original.objects.meshes.begin());
        selected_node = std::size_t(std::ranges::find_if(original.objects.nodes, [&](const auto& node) {
            return std::ranges::find(node.variants, selected_mesh) != node.variants.end();
        }) - original.objects.nodes.begin());
        expect(selected_node < original.objects.nodes.size(), "Tree closeup needs a canonical dynamic placement");
    }
    const auto matrices = model_world_transforms(original.objects, unit);
    const auto& source = dynamic ? original.objects.meshes[selected_mesh] : before.chunks[selected].mesh;
    std::array<float, 3> minimum{32768, 32768, 32768}, maximum{-32768, -32768, -32768};
    for (const auto& vertex : source.vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto& matrix = matrices[selected_node];
            const auto p = dynamic ? vertex.position[0] * matrix[axis] + vertex.position[1] * matrix[4 + axis] + vertex.position[2] * matrix[8 + axis] + matrix[12 + axis]
                                   : vertex.position[axis];
            minimum[axis] = std::min(minimum[axis], p);
            maximum[axis] = std::max(maximum[axis], p);
        }
    }
    // LHRTS7 has a transparent upper quarter. Frame the visible tree rather
    // than that empty part of its original billboard; all modes share this box.
    maximum[1] = minimum[1] + (maximum[1] - minimum[1]) * tree.visible_height_fraction;
    const auto height = maximum[1] - minimum[1];
    expect(height > 1, "Tree must stand upright through its original baked root transform");
    const bool two_sided_group = tree.name == short_stem_group.name;
    const bool grove = tree.name == tree_group.name || tree.name == dense_group.name || two_sided_group || tree.name == separated_group.name;
    const auto frame_span = grove ? std::max(height, .75f * std::hypot(maximum[0] - minimum[0], maximum[2] - minimum[2])) : height;
    auto front_radians = grove ? std::atan2(source.vertices.front().normal[2], source.vertices.front().normal[0]) : 0.f;
    if (two_sided_group) {
        // TRGS22 has opposing face windings and inconsistent vertex normals.
        // Orient its cameras from actual geometry; both source sides stay visible.
        const auto& indices = source.faces.front().indices;
        const auto& a = source.vertices[indices[0]].position;
        const auto& b = source.vertices[indices[1]].position;
        const auto& c = source.vertices[indices[2]].position;
        const auto nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
        const auto nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        front_radians = std::atan2(nz, nx);
    }
    // Four oblique compass directions, a driving-distance view and a full
    // surroundings view. All three render modes use the same camera/light.
    bool dense_crown_preserved = true;
    bool central_gap_preserved = true;
    for (unsigned angle = 0; angle < (grove ? 8u : 6u); ++angle) {
        if (cleanup && angle != 0 && angle != 5 && !(angle == 6 && (tree.name == dense_group.name || two_sided_group || tree.name == separated_group.name))) {
            continue;
        }
        const bool dense = angle == 5;
        const auto distance = frame_span * (dense ? 2.8f : angle == 4 ? 4.f : 1.35f);
        // Snetterton's broad tree stands behind a grove from the old view.
        // Inspect the same original node from its exposed side, retaining all
        // surrounding geometry and the grove's correct new rear occlusion.
        const auto dense_radians = (two_sided_group || tree.name == separated_group.name) ? front_radians + .32f
                                   : tree.name == slender_tree.name && track == 3         ? 2.2f
                                   : tree.name == broad_tree.name && track == 8           ? 4.2f
                                                                                          : 1.1f;
        const auto offset = angle >= 6 ? (angle == 6 ? .01f : .99f) * std::numbers::pi_v<float> / 2.f : float(angle % 4) * std::numbers::pi_v<float> / 2.f + .32f;
        const auto radians = dense ? dense_radians : front_radians + offset;
        const std::array<float, 3> direction{std::cos(radians), dense ? .25f : .12f, std::sin(radians)};
        std::array<std::int32_t, 3> target{};
        std::array<SceneCamera, 1> cameras{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto center = (minimum[axis] + maximum[axis]) / 2;
            target[axis] = std::int32_t(center * 65536.f);
            cameras[0].transform[12 + axis] = std::int32_t((center + direction[axis] * distance) * 65536.f);
        }
        const FinishCamera look(cameras);
        CameraInputs camera{look.world_to_view(target), default_projection(4, 640, 480)};
        camera.registered_camera_position = target;
        const auto render = [&](unsigned mode) {
            const bool geometry = mode == 2, materials = mode != 0;
            SceneResources scene = dense ? (mode == 0 ? original : mega) : SceneResources{};
            scene.setup = mega.setup;
            scene.ground.sector_centers = original.ground.sector_centers;
            auto& baked = scene.objects_baked;
            if (dense) {
                // Keep all other Mega scenery fixed while isolating new tree
                // geometry from its AI texture on the original crossed planes.
                if (mode == 1) {
                    for (std::size_t i = 0; i < original.objects.meshes.size(); ++i) {
                        if (original.objects.meshes[i].name == tree.name) {
                            scene.objects.meshes[i] = original.objects.meshes[i];
                        }
                    }
                    for (std::size_t i = 0; i < before.chunks.size(); ++i) {
                        if (before.chunks[i].mesh.name == tree.name) {
                            baked.chunks[i] = before.chunks[i];
                        }
                    }
                }
            } else if (dynamic) {
                scene.objects = geometry ? mega.objects : original.objects;
                scene.objects.textures = materials ? mega.objects.textures : original.objects.textures;
                for (std::size_t i = 0; i < scene.objects.nodes.size(); ++i) {
                    if (i != selected_node) {
                        scene.objects.nodes[i].variants.clear();
                    }
                }
            } else {
                baked.chunks.push_back((geometry ? after : before).chunks[selected]);
                const auto& textures = (materials ? after : before).textures;
                std::vector<std::int16_t> remap(textures.size(), -1);
                for (auto& face : baked.chunks.front().mesh.faces) {
                    if (face.texture >= 0) {
                        auto& slot = remap.at(std::size_t(face.texture));
                        if (slot < 0) {
                            slot = std::int16_t(baked.textures.size());
                            baked.textures.push_back(textures.at(std::size_t(face.texture)));
                        }
                        face.texture = slot;
                    }
                }
            }
            SceneView view(renderer, scene);
            toca::SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            auto frame = view.frame(camera, scene_lighting(scene.setup), {unit}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            const auto triangles = projected.indices.size() / 3;
            // TRGE17/TRGS18/TRGS20 are single-sided source planes. Their original and
            // texture-only back views are correctly culled by the scene path;
            // the rebuilt group must supply real visible geometry there.
            expect((grove && !geometry) || projected.indices.size() > 3, "Vegetation must reach actual GPU triangles at near and driving distances");
            if (projected.indices.empty()) {
                frame.quad({}, 0, 0, 640, 480, toca::gpu::background);
            }
            expect(renderer.draw(frame, false), "Vegetation render target unavailable");
            constexpr std::array labels{"original", "ai-original-geometry", "mega"};
            renderer.save_screenshot(std::string(tree.screenshot_prefix) + "-track-" + std::to_string(track) + "-view-" + std::to_string(angle) + "-" + labels[mode] + ".png");
            return std::pair{renderer.readback(), triangles};
        };
        const auto canonical = render(0), textured = render(1), detailed = render(2);
        if (tree.name == separated_group.name && angle == 6) {
            // Locate the source's largest interior gap on its middle scanline.
            // Its central area must stay open through the rebuilt crown too.
            const auto background = canonical.first.front();
            unsigned top = 480, bottom = 0;
            for (unsigned y = 0; y < 480; ++y) {
                for (unsigned x = 0; x < 640; ++x) {
                    if (canonical.first[y * 640 + x] != background) {
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            }
            expect(top < bottom, "Separated grove source must have a visible crown");
            const auto row = (top + bottom) / 2;
            unsigned first = 640, last = 0;
            for (unsigned x = 0; x < 640; ++x) {
                if (canonical.first[row * 640 + x] != background) {
                    first = std::min(first, x);
                    last = x;
                }
            }
            unsigned gap_start = 0, gap_width = 0, run = 0;
            for (unsigned x = first; x <= last; ++x) {
                run = canonical.first[row * 640 + x] == background ? run + 1 : 0;
                if (run > gap_width) {
                    gap_start = x + 1 - run;
                    gap_width = run;
                }
            }
            expect(gap_width > 6, "Source grove must retain its distinct gap between the two crown islands");
            std::size_t source_gap = 0, retained_gap = 0;
            for (unsigned y = top + (bottom - top) / 4; y < bottom - (bottom - top) / 4; ++y) {
                for (unsigned x = gap_start + 2; x < gap_start + gap_width - 2; ++x) {
                    if (canonical.first[y * 640 + x] == background) {
                        ++source_gap;
                        retained_gap += detailed.first[y * 640 + x] == background;
                    }
                }
            }
            central_gap_preserved = source_gap > 100 && retained_gap * 100 >= source_gap * 90;
            std::cout << tree.name << " gap retains " << retained_gap << '/' << source_gap << " original transparent pixels\n";
        }
        if (((tree.name == narrow_tree.name || tree.name == stepped_tree.name) && angle < 4)
            || ((tree.name == dense_group.name || two_sided_group || tree.name == separated_group.name) && (angle < 4 || angle == 6) && canonical.second > 0)) {
            const auto background = canonical.first.front();
            std::size_t source_coverage = 0, retained_coverage = 0;
            for (std::size_t pixel = 0; pixel < canonical.first.size(); ++pixel) {
                if (canonical.first[pixel] != background) {
                    ++source_coverage;
                    retained_coverage += detailed.first[pixel] != background;
                }
            }
            dense_crown_preserved &= source_coverage > 1000 && retained_coverage * 100 >= source_coverage * 90;
            std::cout << tree.name << " crown retains " << retained_coverage << '/' << source_coverage << " original silhouette pixels\n";
        }
        if (tree.name == tree_group.name && (angle == 6 || (cleanup && angle == 0))) {
            const auto background = textured.first.front();
            std::size_t source_coverage = 0, retained_coverage = 0;
            for (std::size_t pixel = 0; pixel < textured.first.size(); ++pixel) {
                if (textured.first[pixel] != background) {
                    ++source_coverage;
                    retained_coverage += detailed.first[pixel] != background;
                }
            }
            expect(source_coverage > 1000 && retained_coverage * 100 >= source_coverage * 80,
                "Rebuilt grove must preserve the source group's connected crown mass without large invented openings");
        }
        if (two_sided_group && !dense) {
            expect(canonical.second > 0 && textured.second > 0, "TRGS22 original and texture-only geometry must retain both original face windings");
        } else if (grove && !dense) {
            const auto& normal = source.vertices.front().normal;
            const bool source_front = normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2] > 0;
            expect((canonical.second > 0) == source_front && (textured.second > 0) == source_front, "Original group comparisons must retain their source front/back culling");
        }
        std::size_t material_changes = 0, geometry_changes = 0;
        for (std::size_t pixel = 0; pixel < detailed.first.size(); ++pixel) {
            material_changes += canonical.first[pixel] != textured.first[pixel];
            geometry_changes += textured.first[pixel] != detailed.first[pixel];
        }
        expect((grove && textured.second == 0 ? material_changes == 0 : material_changes > 40) && geometry_changes > 40,
            "Vegetation must show authored materials wherever its source is visible and new geometry from every direction");
        expect(detailed.second > 0, "Simple vegetation must retain visible solid trunk and spatial foliage geometry");
        std::cout << "PASS: " << tree.name << " track " << track << " view " << angle << ": " << detailed.second << " visible triangles, " << material_changes
                  << " material pixels, " << geometry_changes << " geometry pixels\n";
    }
    expect(dense_crown_preserved, "Rebuilt crown must retain its original foliage mass without large invented openings");
    expect(central_gap_preserved, "Rebuilt grove must preserve the original open gap between its crown islands");
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc >= 3 && argc <= 5,
            "Usage: toca-mega-vegetation-render-tests CANONICAL_ROOT MEGA_ROOT [TRIS04_1|TRIS07_1|TRGE17_1|TRIS04B_1|TRIS02B_1|TRIE01_1|TRGS18_1|TRGS22_1|TRGS20_1] [cleanup]");
        const auto selected = argc == 3 ? tree_cases.begin() : std::ranges::find(tree_cases, std::string_view(argv[3]), &TreeCase::name);
        expect(selected != tree_cases.end(), "Unknown vegetation type");
        const auto& tree = *selected;
        const bool cleanup = argc == 5;
        expect(!cleanup || std::string_view(argv[4]) == "cleanup", "Unknown compact vegetation selector");
        toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated vegetation check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Mega vegetation", 640, 480, 0), SDL_DestroyWindow);
        toca::gpu::check(bool(window), "Create isolated vegetation window");
        toca::gpu::Renderer renderer(window.get());
        const toca::AssetStore store(argv[1]);
        std::size_t instances = 0;
        for (std::uint32_t track = 0; track < 9; ++track) {
            const auto original = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({.track_id = track, .weather_choice = 1, .detail = 3}));
            const auto mega = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({.track_id = track, .weather_choice = 1, .detail = 4}), argv[2]);
            instances += verify(original, mega, tree, tree.placements[track]);
            const auto representative_track = tree.name == stepped_tree.name                                            ? 6u
                                              : tree.name == short_stem_group.name || tree.name == separated_group.name ? 3u
                                              : tree.name == dense_group.name                                           ? 4u
                                                                                                                        : 0u;
            const bool image_track = cleanup ? track == representative_track || (tree.name == tree_group.name && track == 6)
                                             : track == 0 || track == 3 || track == 8 || (tree.name == tree_group.name && (track == 2 || track == 6))
                                                   || tree.name == stepped_tree.name || tree.name == dense_group.name;
            if (tree.placements[track] && image_track) {
                pixels(renderer, original, mega, track, tree, cleanup);
            }
        }
        expect(instances == std::accumulate(tree.placements.begin(), tree.placements.end(), std::size_t{}), "Vegetation must reach every original placement of the selected type");
        std::cout << "PASS: " << tree.name << ": " << instances << " original placements preserved across all nine circuits\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
