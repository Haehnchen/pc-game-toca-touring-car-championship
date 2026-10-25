#include "assets/store.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct WoodlandCase {
    std::string_view id, material, mesh_name;
    std::uint32_t track;
    std::size_t node, mesh, triangles, camera, end_interior_vertex, end_vertex;
    float depth_limit;
    std::int16_t material_slot;
    std::size_t front_face;
    std::array<std::array<std::int32_t, 3>, 2> source_end_q16;
};

constexpr WoodlandCase croft{"croft", "SAP1", "TREES_24", 6, 127, 23, 5, 21, 12, 8, 1.2f, 1, 1, {{{859133, 438910, -1376259}, {-123497, 449356, -2357665}}}};
constexpr WoodlandCase croft_start{"croft-start-24", "SAP1", "TREES_25", 6, 128, 24, 3, 21, 3, 2, 1.2f, 1, 0, {{{-1947601, 425999, -1373737}, {-1676979, 427815, -2355515}}}};
constexpr WoodlandCase snet_llrts2{"snet-llrts2", "LLRTS2", "TREES_20", 8, 60, 19, 3, 11, 7, 5, 1.6f, 1, 1, {{{933688, 1240923, 99217}, {-1201216, 1211840, 1626487}}}};
constexpr WoodlandCase snet_llrtsx{"snet-llrtsx", "LLRTSX", "TREES_1", 8, 6, 0, 1, 9, 0, 1, 1.6f, 2, 0, {{{-1409168, 942329, -2592844}, {2228208, 926527, -1418420}}}};
constexpr unsigned width = 640, height = 480;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::array<float, 29>> triangle_attributes(const ModelMesh& mesh) {
    // GLB compaction changes index/storage order, not exact triangle attributes.
    std::vector<std::array<float, 29>> result;
    for (const auto& face : mesh.faces) {
        std::array<float, 29> triangle{};
        unsigned cursor = 0;
        for (const auto index : face.indices) {
            const auto& vertex = mesh.vertices[index];
            for (const auto value : vertex.position) {
                triangle[cursor++] = value;
            }
            for (const auto value : vertex.normal) {
                triangle[cursor++] = value;
            }
            triangle[cursor++] = vertex.u;
            triangle[cursor++] = vertex.v;
        }
        triangle[cursor++] = float(face.texture);
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

bool same_mesh(const ModelMesh& a, const ModelMesh& b) {
    return a.name == b.name && a.faces.size() == b.faces.size() && triangle_attributes(a) == triangle_attributes(b);
}

float verify_geometry(const Model& a, const Model& b, const WoodlandCase& pilot) {
    expect(a.meshes.at(pilot.mesh).name == pilot.mesh_name && a.meshes[pilot.mesh].faces.size() == pilot.triangles, "Woodland pilot requires its exact original mesh binding");
    expect(a.nodes.at(pilot.node).variants == std::vector<std::uint16_t>{std::uint16_t(pilot.mesh)}, "Pilot must retain its exact original node binding");
    expect(a.textures.size() == b.textures.size() && a.textures.at(pilot.material_slot).name == pilot.material, "Pilot must reuse its original foliage slot without new materials");
    expect(b.textures.at(pilot.material_slot).encoding == ImageEncoding::webp, "Pilot must load accepted woodland WebP through actual Mega bundle");
    for (const auto& face : b.meshes[pilot.mesh].faces) {
        expect(face.texture == pilot.material_slot, "Every pilot surface must reuse its accepted source-specific foliage artwork");
    }
    const auto& source = a.meshes[pilot.mesh];
    const auto& rebuilt = b.meshes[pilot.mesh];
    const std::array endpoint_indices{pilot.end_interior_vertex, pilot.end_vertex};
    for (unsigned point = 0; point < endpoint_indices.size(); ++point) {
        const auto& vertex = source.vertices.at(endpoint_indices[point]);
        for (unsigned axis = 0; axis < 3; ++axis) {
            expect(std::int32_t(std::lround(vertex.position[axis] * 65536)) == pilot.source_end_q16[point][axis],
                "Pilot end camera must use its exact canonical local source coordinates");
        }
    }
    expect(rebuilt.faces.size() > source.faces.size(), "Woodland must install real depth, beyond AI artwork on the original band");
    float depth = 0;
    for (const auto& vertex : rebuilt.vertices) {
        float nearest = std::numeric_limits<float>::max();
        for (const auto& face : source.faces) {
            const auto& p = source.vertices[face.indices[0]];
            float distance = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                distance += (vertex.position[axis] - p.position[axis]) * p.normal[axis];
            }
            nearest = std::min(nearest, std::abs(distance));
        }
        depth = std::max(depth, nearest);
        expect(vertex.u >= 0 && vertex.u <= 1 && vertex.v >= 0 && vertex.v <= 1, "Runtime foliage UVs must remain within the original full atlas");
    }
    expect(depth > .1f && depth < pilot.depth_limit, "Pilot must gain bounded foliage depth without moving its landscape footprint");
    return depth;
}

std::size_t source_chunk(const SceneResources& scene, std::size_t node) {
    const auto p = std::ranges::find(scene.trees_baked.chunks, node, &TerrainChunk::node_index);
    expect(p != scene.trees_baked.chunks.end(), "Woodland needs exact original baked node");
    return p - scene.trees_baked.chunks.begin();
}

void verify_croft_split(const Model& source, const Model& mega) {
    // Four shared seam vertices are enough for the simple two-skin body. This
    // checks source ownership/closure, without prescribing foliage profiling.
    const std::array<std::array<float, 5>, 4> seam{{{{3.03125f, 10.1231689453f, -128.7384490967f, 1, 0}}, {{5.8702625786f, 8.3116989136f, -136.5129076119f, .5f, .5f}},
        {{6.3836131538f, 8.3116989136f, -135.9254392509f, .5f, .5f}}, {{9.2226257324f, 6.5002288818f, -143.6999359131f, 0, 1}}}};
    for (const auto peer : {&croft, &croft_start}) {
        const auto& mesh = mega.meshes[peer->mesh];
        const auto& matrix = source.nodes[peer->node].transform;
        for (const auto& expected : seam) {
            bool found = false;
            for (const auto& v : mesh.vertices) {
                std::array<float, 3> world{};
                for (unsigned row = 0; row < 3; ++row) {
                    world[row] = matrix[12 + row];
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        world[row] += matrix[axis * 4 + row] * v.position[axis];
                    }
                }
                float d = 0;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    d += (world[axis] - expected[axis]) * (world[axis] - expected[axis]);
                }
                found = found || (d < .0002f * .0002f && std::abs(v.u - expected[3]) < .00001f && std::abs(v.v - expected[4]) < .00001f);
            }
            expect(found, "Both Croft roots must retain identical AB diagonal skin positions and source UVs");
        }
    }
    std::cout << "CROFT_SHARED_AB vertices=4 body_not_pinched=1\n";
}

void verify_simple_llrtsx(const ModelMesh& source, const ModelMesh& mesh) {
    expect(mesh.faces.size() <= 64, "LLRTSX must use a simple closed body rather than dense foliage sampling");
    using Point = std::array<float, 3>;
    std::map<std::array<Point, 2>, std::pair<unsigned, int>> edges;
    double volume = 0;
    for (const auto& face : mesh.faces) {
        std::array<Point, 3> points{};
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto& vertex = mesh.vertices.at(face.indices[corner]);
            points[corner] = vertex.position;
            expect(1.f - vertex.v <= vertex.u + .00001f, "Simple LLRTSX UVs must retain its source triangular domain t=-V<=U");
        }
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto& a = points[corner];
            const auto& b = points[(corner + 1) % 3];
            expect(a != b, "Simple LLRTSX cannot contain a zero-length edge");
            const bool forward = a < b;
            auto& edge = edges[forward ? std::array{a, b} : std::array{b, a}];
            ++edge.first;
            edge.second += forward ? 1 : -1;
        }
        const auto& a = points[0];
        const auto& b = points[1];
        const auto& c = points[2];
        volume += (double(a[0]) * (double(b[1]) * c[2] - double(b[2]) * c[1]) + double(a[1]) * (double(b[2]) * c[0] - double(b[0]) * c[2])
                      + double(a[2]) * (double(b[0]) * c[1] - double(b[1]) * c[0]))
                  / 6.;
    }
    for (const auto& [positions, count] : edges) {
        expect(count.first == 2 && count.second == 0, "Simple LLRTSX must have two oppositely directed faces per position edge");
    }
    expect(std::abs(volume) > .001, "Simple LLRTSX must enclose actual volume");
    for (const auto& endpoint : source.vertices) {
        expect(std::ranges::any_of(mesh.vertices,
                   [&](const auto& vertex) {
                       return vertex.position == endpoint.position;
                   }),
            "Simple LLRTSX must retain the original ground tip, right foot and upper endpoint");
    }
    std::cout << "SIMPLE_LLRTSX triangles=" << mesh.faces.size() << " manifold_edges=" << edges.size() << " volume=" << std::abs(volume) << '\n';
}

std::size_t verify(const SceneResources& original, const SceneResources& mega, const WoodlandCase& pilot) {
    const auto& a = original.trees;
    const auto& b = mega.trees;
    const bool croft_family = pilot.track == 6;
    const std::array family = croft_family ? std::array{&croft, &croft_start} : std::array{&snet_llrtsx, &snet_llrts2};
    expect(a.meshes.size() == b.meshes.size() && a.nodes.size() == b.nodes.size() && a.textures.size() == b.textures.size(), "Woodland source slots changed");
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i];
        const auto& y = b.nodes[i];
        expect(
            x.parent == y.parent && x.transform == y.transform && x.id_flags == y.id_flags && x.variants == y.variants, "Woodland source parent/transform/flags/variant changed");
    }
    const auto depth = verify_geometry(a, b, pilot);
    for (const auto* member : family) {
        if (member->mesh != pilot.mesh) {
            verify_geometry(a, b, *member);
        }
    }
    if (croft_family) {
        verify_croft_split(a, b);
    }
    if (!croft_family) {
        verify_simple_llrtsx(a.meshes[snet_llrtsx.mesh], b.meshes[snet_llrtsx.mesh]);
    }
    const auto in_family = [&](std::size_t mesh) {
        return std::ranges::any_of(family, [&](auto c) {
            return c->mesh == mesh;
        });
    };
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        if (!in_family(i)) {
            expect(same_mesh(a.meshes[i], b.meshes[i]), "Every other woodland mesh must retain source triangle attributes");
        }
    }
    expect(original.trees_baked.chunks.size() == mega.trees_baked.chunks.size(), "Baked woodland count changed");
    std::array<unsigned, 2> bindings{};
    for (std::size_t i = 0; i < original.trees_baked.chunks.size(); ++i) {
        const auto& x = original.trees_baked.chunks[i];
        const auto& y = mega.trees_baked.chunks[i];
        expect(x.node_index == y.node_index && x.variant_index == y.variant_index && x.origin_xz == y.origin_xz, "Baked woodland registration changed");
        const auto mesh = a.nodes.at(x.node_index).variants.at(x.variant_index);
        if (!in_family(mesh)) {
            expect(same_mesh(x.mesh, y.mesh), "Other baked woodland must remain source-exact");
        }
        for (unsigned part = 0; part < family.size(); ++part) {
            if (x.node_index == family[part]->node) {
                ++bindings[part];
                expect(y.mesh.faces.size() > x.mesh.faces.size(), "Identified woodland node must consume real body geometry");
            }
        }
    }
    expect(bindings[0] == 1 && bindings[1] == 1, "Exactly one source binding per family half is required");
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "Ground/collision/routes/sectors changed");
    std::cout << "PILOT " << pilot.id << " mesh=" << pilot.mesh << " node=" << pilot.node << " source_triangles=" << a.meshes[pilot.mesh].faces.size()
              << " mega_triangles=" << b.meshes[pilot.mesh].faces.size() << " added_depth=" << depth << '\n';
    return source_chunk(original, pilot.node);
}

SceneResources without_node(SceneResources scene, std::size_t node) {
    if (node < scene.trees.nodes.size()) {
        scene.trees.nodes[node].variants.clear();
    }
    std::erase_if(scene.trees_baked.chunks, [&](const auto& c) {
        return c.node_index == node;
    });
    return scene;
}

std::vector<std::uint32_t> capture(gpu::Renderer& renderer, const SceneResources& scene, const CameraInputs& camera, const std::string& path = {}) {
    SceneView view(renderer, scene);
    SourceRandom random(7);
    FlagAnimation flags(scene.flags, random);
    HorizonActors actors(scene.moving_horizon);
    auto frame = view.frame(camera, scene_lighting(scene.setup), {unit}, flags.variants(), actors.frame());
    const auto projected = toca::test::materialize_world(frame);
    if (projected.indices.empty()) {
        frame.quad({}, -8, -8, 1, 1);
    }
    expect(renderer.draw(frame, false), "Woodland diagnostic target unavailable");
    if (!path.empty()) {
        renderer.save_screenshot(path);
    }
    return renderer.readback();
}

std::size_t pixel_difference(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    expect(a.size() == b.size(), "Woodland readback size differs");
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        n += a[i] != b[i];
    }
    return n;
}

SceneResources comparison(const SceneResources& original, const SceneResources& mega, unsigned mode, bool dense, std::size_t selected, const WoodlandCase& pilot) {
    SceneResources scene = dense ? mega : SceneResources{};
    scene.setup = mega.setup;
    scene.ground.sector_centers = original.ground.sector_centers;
    auto& baked = scene.trees_baked;

    if (pilot.track == 6) {
        const std::array peers{&croft, &croft_start};
        if (dense) {
            for (const auto peer : peers) {
                const auto index = source_chunk(original, peer->node);
                if (mode < 2) {
                    scene.trees.meshes[peer->mesh] = original.trees.meshes[peer->mesh];
                    baked.chunks[index] = original.trees_baked.chunks[index];
                }
                if (mode == 0) {
                    const auto local_slot = std::int16_t(scene.trees.textures.size());
                    scene.trees.textures.push_back(original.trees.textures[peer->material_slot]);
                    for (auto& face : scene.trees.meshes[peer->mesh].faces) {
                        face.texture = local_slot;
                    }
                    const auto slot = std::int16_t(baked.textures.size());
                    baked.textures.push_back(original.trees_baked.textures[peer->material_slot]);
                    for (auto& face : baked.chunks[index].mesh.faces) {
                        face.texture = slot;
                    }
                }
            }
        } else {
            baked.textures = {(mode == 0 ? original : mega).trees_baked.textures[pilot.material_slot]};
            for (const auto peer : peers) {
                auto selected = (mode == 2 ? mega : original).trees_baked.chunks[source_chunk(original, peer->node)];
                for (auto& face : selected.mesh.faces) {
                    face.texture = 0;
                }
                baked.chunks.push_back(std::move(selected));
            }
        }
        return scene;
    }
    if (dense) {
        // Sky, ground and every neighboring object stay identical between all
        // three modes. Only the pilot's source geometry/material are changed.
        if (mode < 2) {
            scene.trees.meshes[pilot.mesh] = original.trees.meshes[pilot.mesh];
            baked.chunks[selected] = original.trees_baked.chunks[selected];
        }
        if (mode == 0) {
            // Woodland textures are shared by unchanged distant strips. Append
            // the comparison-only source texture and rebind just this pilot, rather
            // than changing their materials along with this pilot.
            const auto model_slot = std::int16_t(scene.trees.textures.size());
            scene.trees.textures.push_back(original.trees.textures[pilot.material_slot]);
            for (auto& face : scene.trees.meshes[pilot.mesh].faces) {
                face.texture = model_slot;
            }
            const auto baked_slot = std::int16_t(baked.textures.size());
            baked.textures.push_back(original.trees_baked.textures[pilot.material_slot]);
            for (auto& face : baked.chunks[selected].mesh.faces) {
                face.texture = baked_slot;
            }
        }
    } else {
        baked.chunks.push_back((mode == 2 ? mega : original).trees_baked.chunks[selected]);
        const auto& textures = (mode == 0 ? original : mega).trees_baked.textures;
        std::vector<std::int16_t> remap(textures.size(), -1);
        for (auto& face : baked.chunks.front().mesh.faces) {
            auto& slot = remap.at(std::size_t(face.texture));
            if (slot < 0) {
                slot = std::int16_t(baked.textures.size());
                baked.textures.push_back(textures.at(std::size_t(face.texture)));
            }
            face.texture = slot;
        }
    }
    return scene;
}

void pixels(gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, std::size_t selected, const WoodlandCase& pilot) {
    const auto& mesh = original.trees_baked.chunks[selected].mesh;
    std::array<float, 3> minimum{32768, 32768, 32768}, maximum{-32768, -32768, -32768}, center{};
    for (const auto& v : mesh.vertices) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], v.position[axis]);
            maximum[axis] = std::max(maximum[axis], v.position[axis]);
        }
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        center[axis] = (minimum[axis] + maximum[axis]) * .5f;
    }
    const auto span = std::hypot(maximum[0] - minimum[0], maximum[2] - minimum[2]);
    const auto h = maximum[1] - minimum[1];
    const auto& normal = mesh.vertices[mesh.faces.at(pilot.front_face).indices[0]].normal;
    const auto front = std::atan2(normal[2], normal[0]);
    constexpr std::array labels{"original", "ai-original-geometry", "mega"};
    const std::array<std::string, 8> names{"front", "back", "oblique", "end",
        pilot.track == 6             ? "road-eye"
        : pilot.id == snet_llrtsx.id ? "distance-isolated"
                                     : "driving",
        "dense-camera" + std::to_string(pilot.camera), "dense-left", "dense-right"};
    const auto prefix = "woodland-" + std::string(pilot.id);
    std::ofstream report(prefix + "-gpu.csv");
    report << "view,source_triangles,ai_triangles,mega_triangles,material_pixels,geometry_pixels,ai_coverage,retained_coverage\n";
    std::optional<std::ofstream> per_node;
    if (pilot.track == 6) {
        per_node.emplace(prefix + "-coverage.csv");
        *per_node << "view,mode,track,node,mesh,visible_pixels,status\n";
    }

    for (unsigned view = 0; view < names.size(); ++view) {
        const bool road_eye = pilot.track == 6 && view == 4;
        const bool dense = view >= 5 || road_eye;
        auto target = center;
        std::array<SceneCamera, 1> cameras{};
        if (road_eye) {
            // Route205 plus1.6m: actual road-height context, not an isolated distant strip.
            cameras[0].transform[12] = std::int32_t(39.4837 * 65536);
            cameras[0].transform[13] = std::int32_t(7.9897 * 65536);
            cameras[0].transform[14] = std::int32_t(-162.8783 * 65536);
            const auto& road_target = original.trees_baked.chunks[source_chunk(original, croft_start.node)].mesh;
            std::array<float, 3> low{32768, 32768, 32768}, high{-32768, -32768, -32768};
            for (const auto& vertex : road_target.vertices) {
                for (unsigned axis = 0; axis < 3; ++axis) {
                    low[axis] = std::min(low[axis], vertex.position[axis]);
                    high[axis] = std::max(high[axis], vertex.position[axis]);
                }
            }
            for (unsigned axis = 0; axis < 3; ++axis) {
                target[axis] = (low[axis] + high[axis]) * .5f;
            }
        } else if (dense) {
            expect(original.cameras.size() > pilot.camera, "Pilot dense view requires its original authored camera");
            cameras[0] = original.cameras[pilot.camera];
            const auto lateral = view == 6 ? -h : view == 7 ? h : 0.f;
            cameras[0].transform[12] += std::int32_t(std::sin(front) * lateral * 65536);
            cameras[0].transform[14] -= std::int32_t(std::cos(front) * lateral * 65536);
        } else {
            const auto radians = front + (view == 1 ? std::numbers::pi_v<float> : view == 2 ? .65f : view == 3 ? 1.48f : 0.f);
            const auto distance = view == 3 ? h * 3.f : view == 4 ? span * 2.1f : span * .9f;
            if (view == 3) {
                // Source-specific endpoints: Croft's full final panel ends at
                // vertex8; LLRTS2's full quad begins at vertex5 while its OTHER
                // end tapers upward to vertex0. LLRTSX uses vertex0→1 as its
                // base edge, ending at the full right cross-section. No schema guessing.
                expect(mesh.vertices.size() > std::max(pilot.end_interior_vertex, pilot.end_vertex), "End camera requires canonical source vertex slots");
                const auto& interior = mesh.vertices[pilot.end_interior_vertex];
                const auto& endpoint = mesh.vertices[pilot.end_vertex];
                const auto left = interior.position, right = endpoint.position;
                float min_u = 1, max_u = 0, max_v = 0;
                for (const auto& vertex : mesh.vertices) {
                    min_u = std::min(min_u, vertex.u);
                    max_u = std::max(max_u, vertex.u);
                    max_v = std::max(max_v, vertex.v);
                }
                // parse_terrain clamps baked UVs to .02..99. Validate actual
                // extrema rather than assuming exact0..1 on the baked mesh.
                expect(interior.v == max_v && endpoint.v == max_v && interior.u != endpoint.u && (interior.u == min_u || interior.u == max_u)
                           && (endpoint.u == min_u || endpoint.u == max_u) && std::hypot(right[0] - left[0], right[2] - left[2]) > 1,
                    "End view requires two distinct source-specific baked panel base points at UV extrema");
                target = right;
                target[1] += h * .45f;
                const auto tangent = std::atan2(right[2] - left[2], right[0] - left[0]);
                cameras[0].transform[12] = std::int32_t((target[0] + std::cos(tangent) * distance) * 65536);
                cameras[0].transform[13] = std::int32_t((target[1] + h * .15f) * 65536);
                cameras[0].transform[14] = std::int32_t((target[2] + std::sin(tangent) * distance) * 65536);
                std::cout << "END_CAMERA baked_uv=" << min_u << ',' << max_u << ',' << max_v << " target=" << target[0] << ',' << target[1] << ',' << target[2] << '\n';
            } else {
                cameras[0].transform[12] = std::int32_t((target[0] + std::cos(radians) * distance) * 65536);
                cameras[0].transform[13] = std::int32_t((target[1] + h * .15f) * 65536);
                cameras[0].transform[14] = std::int32_t((target[2] + std::sin(radians) * distance) * 65536);
            }
        }
        std::array<std::int32_t, 3> target_q16{};
        for (unsigned axis = 0; axis < 3; ++axis) {
            target_q16[axis] = std::int32_t(target[axis] * 65536);
        }
        const FinishCamera look(cameras);
        const CameraInputs camera{look.world_to_view(target_q16), default_projection(4, width, height), dense ? look.position() : target_q16};

        struct Result {
            std::vector<std::uint32_t> pixels;
            std::size_t triangles;
        };

        std::array<Result, 3> results;
        for (unsigned mode = 0; mode < results.size(); ++mode) {
            auto scene = comparison(original, mega, mode, dense, selected, pilot);
            SceneView rendered(renderer, scene);
            SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            auto frame = rendered.frame(camera, scene_lighting(mega.setup), {unit}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            const auto triangles = projected.indices.size() / 3;
            if (projected.indices.empty()) {
                frame.quad({}, 0, 0, width, height, gpu::background);
            }
            expect(renderer.draw(frame, false), "Woodland GPU target unavailable");
            renderer.save_screenshot(prefix + "-" + names[view] + "-" + labels[mode] + ".png");
            results[mode] = {renderer.readback(), triangles};
            if (per_node) {
                for (const auto peer : {&croft, &croft_start}) {
                    const auto count = pixel_difference(results[mode].pixels, capture(renderer, without_node(scene, peer->node), camera));
                    *per_node << names[view] << ',' << labels[mode] << ",6," << peer->node << ',' << peer->mesh << ',' << count << ',' << (count ? "visible" : "not-visible")
                              << '\n';
                }
                per_node->flush();
            }
        }
        std::size_t material = 0, geometry = 0, coverage = 0, retained = 0;
        const auto background = results[1].pixels.front();
        std::vector<std::uint32_t> without_pilot;
        if (pilot.track == 6) {
            auto ai = comparison(original, mega, 1, dense, selected, pilot);
            auto full = comparison(original, mega, 2, dense, selected, pilot);
            for (const auto peer : {&croft, &croft_start}) {
                ai = without_node(std::move(ai), peer->node);
                full = without_node(std::move(full), peer->node);
            }
            without_pilot = capture(renderer, ai, camera);
            expect(without_pilot == capture(renderer, full, camera), "Croft comparison must preserve identical surroundings after removing both peers");
        } else if (dense) {
            // Scene triangle counts include every neighbor. Render the same
            // surroundings with only this pilot removed to measure its actual
            // visible pixels, including source backface culling/occlusion.
            auto scene = mega;
            scene.trees.meshes[pilot.mesh].faces.clear();
            scene.trees_baked.chunks[selected].mesh.faces.clear();
            scene.trees_baked.chunks[selected].face_flags.clear();
            SceneView rendered(renderer, scene);
            SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            const auto frame = rendered.frame(camera, scene_lighting(mega.setup), {unit}, flags.variants(), actors.frame());
            expect(renderer.draw(frame, false), "Woodland surroundings coverage target unavailable");
            without_pilot = renderer.readback();
        }
        for (std::size_t i = 0; i < results[2].pixels.size(); ++i) {
            material += results[0].pixels[i] != results[1].pixels[i];
            geometry += results[1].pixels[i] != results[2].pixels[i];
            const auto surroundings = (dense || pilot.track == 6) ? without_pilot[i] : background;
            if (results[1].pixels[i] != surroundings) {
                ++coverage;
                retained += results[2].pixels[i] != surroundings;
            }
        }
        expect(results[2].triangles > 0 && geometry > 20, "Actual Mega foliage depth must remain visible in every pilot view");
        // CPU clipping may submit a source triangle that projects to a line
        // in the exact end-on view. Require source artwork differences only
        // when that source actually covers rasterized pixels.
        if (coverage > 100) {
            expect(material > 20, "Source-visible views must show the accepted AI foliage artwork");
        }
        if (view == 0 || (view == 4 && (!road_eye || coverage > 100))) {
            expect(coverage > 100 && retained * 100 >= coverage * 85, "Front/driving views must preserve the source-facing foliage silhouette");
        }
        report << names[view] << ',' << results[0].triangles << ',' << results[1].triangles << ',' << results[2].triangles << ',' << material << ',' << geometry << ',' << coverage
               << ',' << retained << '\n';
        std::cout << "PASS woodland " << names[view] << " material_pixels=" << material << " geometry_pixels=" << geometry << " retained=" << retained << '/' << coverage << '\n';
    }
}

struct VisibilityCase {
    std::string_view id;
    std::size_t node, mesh;
    std::vector<std::size_t> cameras;
    std::int16_t slot;
};

void visibility_probe(gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const VisibilityCase& p, std::uint32_t track, std::ofstream& report) {
    expect(original.trees.nodes.at(p.node).variants == std::vector<std::uint16_t>{std::uint16_t(p.mesh)} && same_mesh(original.trees.meshes[p.mesh], mega.trees.meshes[p.mesh]),
        "Visibility-only probe must retain its exact source binding/geometry");
    const auto index = source_chunk(original, p.node);
    const auto& mesh = original.trees_baked.chunks[index].mesh;
    expect(same_mesh(mesh, mega.trees_baked.chunks[index].mesh), "Visibility-only baked source geometry changed");
    std::array<float, 3> lo{32768, 32768, 32768}, hi{-32768, -32768, -32768};
    for (const auto& v : mesh.vertices) {
        for (unsigned a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], v.position[a]);
            hi[a] = std::max(hi[a], v.position[a]);
        }
    }
    std::array<std::int32_t, 3> target{};
    for (unsigned a = 0; a < 3; ++a) {
        target[a] = std::int32_t(std::lround((lo[a] + hi[a]) * .5f * 65536));
    }
    std::optional<CameraInputs> chosen;
    std::size_t best = 0, selected_camera = 0;
    for (const auto camera : p.cameras) {
        expect(camera < original.cameras.size(), "Visibility probe requires actual authored camera");
        std::array<SceneCamera, 1> origin{original.cameras[camera]};
        const FinishCamera look(origin);
        const CameraInputs view{look.world_to_view(target), default_projection(4, width, height), look.position()};
        unsigned front = 0;
        const auto eye = look.position();
        for (const auto& f : mesh.faces) {
            const auto& v = mesh.vertices[f.indices[0]];
            float distance = 0;
            for (unsigned a = 0; a < 3; ++a) {
                distance += (eye[a] / 65536.f - v.position[a]) * v.normal[a];
            }
            front += distance > 0;
        }
        expect(front > 0, "Visibility probe camera must lie before actual source face winding");
        const auto full = capture(renderer, mega, view);
        const auto count = pixel_difference(full, capture(renderer, without_node(mega, p.node), view));
        std::cout << "VISIBILITY_CANDIDATE " << p.id << " camera=" << camera << " front_faces=" << front << " pixels=" << count << '\n';
        if (!chosen || count > best) {
            chosen = view;
            best = count;
            selected_camera = camera;
        }
        if (count > 100) {
            break;
        }
    }
    expect(bool(chosen), "Visibility probe has no canonical camera");
    constexpr std::array labels{"original", "ai-original-geometry", "mega"};
    std::array<std::vector<std::uint32_t>, 3> results;
    std::array<std::size_t, 3> counts{};
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto scene = mega;
        if (mode == 0) {
            scene.trees.meshes[p.mesh] = original.trees.meshes[p.mesh];
            scene.trees_baked.chunks[index] = original.trees_baked.chunks[index];
            const auto slot = std::int16_t(scene.trees.textures.size());
            scene.trees.textures.push_back(original.trees.textures.at(p.slot));
            for (auto& f : scene.trees.meshes[p.mesh].faces) {
                expect(f.texture == p.slot, "Visibility probe source uses another material");
                f.texture = slot;
            }
            const auto baked_slot = std::int16_t(scene.trees_baked.textures.size());
            scene.trees_baked.textures.push_back(original.trees_baked.textures.at(p.slot));
            for (auto& f : scene.trees_baked.chunks[index].mesh.faces) {
                f.texture = baked_slot;
            }
        }
        const auto name = "woodland-croft-start-24-visibility-" + std::string(p.id) + "-camera" + std::to_string(selected_camera) + "-" + labels[mode] + ".png";
        results[mode] = capture(renderer, scene, *chosen, name);
        counts[mode] = pixel_difference(results[mode], capture(renderer, without_node(scene, p.node), *chosen));
        report << p.id << ',' << track << ',' << p.node << ',' << p.mesh << ',' << selected_camera << ',' << labels[mode] << ',' << counts[mode] << ','
               << (counts[mode] > 100 ? "detail-visible" : "insufficient-coverage") << '\n';
    }
    expect(results[1] == results[2], "Unchanged visibility-only AI/Mega geometry must produce identical context images");
    if (counts[0] > 100 && counts[1] > 100) {
        expect(pixel_difference(results[0], results[1]) > 20, "Visible source must exercise actual accepted foliage artwork");
    }
    report.flush();
    std::cout << "VISIBILITY " << p.id << " camera=" << selected_camera << " source=" << counts[0] << " ai=" << counts[1] << " mega=" << counts[2] << " geometry_unchanged=1\n";
}

void visibility_probes(
    gpu::Renderer& renderer, const AssetStore& store, const std::filesystem::path& directory, const SceneResources& croft_source, const SceneResources& croft_mega) {
    std::ofstream report("woodland-croft-start-24-visibility.csv");
    report << "probe,track,node,mesh,camera,mode,visible_pixels,status\n";
    visibility_probe(renderer, croft_source, croft_mega, {"croft27", 141, 27, {20}, 1}, 6, report);
    const auto original = load_scene_resources(store, resolve_setup({.track_id = 4, .weather_choice = 2, .detail = 3}, 2));
    const auto mega = load_scene_resources(store, resolve_setup({.track_id = 4, .weather_choice = 2, .detail = 4}, 2), directory);
    visibility_probe(renderer, original, mega, {"oulton88", 236, 88, {30, 32}, 0}, 4, report);
    visibility_probe(renderer, original, mega, {"oulton37", 118, 37, {29}, 1}, 4, report);
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc >= 3 && argc <= 4, "Usage: toca-mega-woodland-render-tests CANONICAL_ROOT MEGA_ROOT [start-24|snet-llrts2|snet-llrtsx]");
        expect(argc == 3 || std::string_view(argv[3]) == "start-24" || std::string_view(argv[3]) == snet_llrts2.id || std::string_view(argv[3]) == snet_llrtsx.id,
            "Unknown woodland pilot selector");
        const auto& pilot = argc == 3 ? croft : std::string_view(argv[3]) == "start-24" ? croft_start : std::string_view(argv[3]) == snet_llrts2.id ? snet_llrts2 : snet_llrtsx;
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated woodland check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA woodland pilot", width, height, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create isolated woodland window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[1]);
        const auto original = load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 2, .detail = 3}, 2));
        const auto mega = load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 2, .detail = 4}, 2), argv[2]);
        const auto selected = verify(original, mega, pilot);
        pixels(renderer, original, mega, selected, pilot);
        if (pilot.id == croft_start.id) {
            visibility_probes(renderer, store, argv[2], original, mega);
        }
        std::cout << "PASS: " << pilot.id << " woodland geometry pilot; " << (pilot.track == 6 ? "two connected original bindings" : "one original binding") << ", eight views, "
                  << "three presentation modes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
