#include "assets/store.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

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

constexpr unsigned width = 640, height = 480;
using Point = std::array<float, 3>;

Point add(Point a, Point b) {
    for (unsigned i = 0; i < 3; ++i) {
        a[i] += b[i];
    }
    return a;
}

Point sub(Point a, Point b) {
    for (unsigned i = 0; i < 3; ++i) {
        a[i] -= b[i];
    }
    return a;
}

Point scale(Point a, float b) {
    for (auto& v : a) {
        v *= b;
    }
    return a;
}

float length(Point a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

Point unit_vector(Point a) {
    return scale(a, 1 / length(a));
}

bool close(Point a, Point b, float epsilon = .0002f) {
    return length(sub(a, b)) < epsilon;
}

std::size_t chunk(const SceneResources& scene, std::size_t node) {
    const auto it = std::ranges::find(scene.trees_baked.chunks, node, &TerrainChunk::node_index);
    expect(it != scene.trees_baked.chunks.end(), "Oulton pilot needs exact baked node binding");
    return it - scene.trees_baked.chunks.begin();
}

ModelMesh faces_with_slot(ModelMesh mesh, std::int16_t slot) {
    std::erase_if(mesh.faces, [&](const auto& f) {
        return f.texture != slot;
    });
    return mesh;
}

void keep_slot(TerrainChunk& chunk, std::int16_t slot) {
    expect(chunk.face_flags.size() == chunk.mesh.faces.size(), "Terrain comparison requires one original flag per face");
    std::vector<ModelFace> faces;
    std::vector<std::uint8_t> flags;
    for (std::size_t i = 0; i < chunk.mesh.faces.size(); ++i) {
        if (chunk.mesh.faces[i].texture == slot) {
            faces.push_back(chunk.mesh.faces[i]);
            flags.push_back(chunk.face_flags[i]);
        }
    }
    chunk.mesh.faces = std::move(faces);
    chunk.face_flags = std::move(flags);
}

struct Panel {
    unsigned part;
    std::size_t left, right, top_left, top_right, face;
};

struct OultonCase {
    std::string_view parameter, prefix, end_label;
    std::array<std::size_t, 2> nodes, meshes, vertices, triangles;
    std::vector<Panel> panels;
    std::vector<Point> bottoms, tops;
    std::vector<std::size_t> neighbors;
    Point route_eye;
    std::size_t camera, joint;
    bool neighbor16;
};

OultonCase joined_case() {
    OultonCase c;
    c.parameter = "join-100-101";
    c.prefix = "woodland-oulton";
    c.end_label = "end-C";
    c.nodes = {260, 261};
    c.meshes = {100, 101};
    c.vertices = {13, 4};
    c.triangles = {5, 2};
    c.panels = {{0, 3, 0, 2, 1, 0}, {1, 0, 1, 3, 2, 0}};
    c.bottoms = {{521.540039f, 18.954773f, -210.774490f}, {546.540039f, 17.139267f, -225.211914f}, {571.540039f, 19.673569f, -210.024521f}};
    c.tops = {{521.540039f, 31.552750f, -210.774490f}, {546.540039f, 29.737228f, -225.212021f}, {571.540039f, 32.271561f, -210.024475f}};
    c.neighbors = {236, 237, 259};
    c.route_eye = {544.2296f, 20.1373f, -238.3205f};
    c.camera = 31;
    c.joint = 1;
    c.neighbor16 = false;
    return c;
}

OultonCase wedge_case() {
    OultonCase c;
    c.parameter = "wedge-14-15";
    c.prefix = "woodland-oulton-wedge";
    c.end_label = "end-D";
    c.nodes = {63, 64};
    c.meshes = {14, 15};
    c.vertices = {10, 4};
    c.triangles = {4, 2};
    c.panels = {{0, 0, 1, 3, 2, 0}, {0, 4, 5, 8, 6, 2}, {1, 0, 1, 3, 2, 0}};
    c.bottoms = {{171.540283f, 19.635422f, 297.413147f}, {196.540283f, 19.846146f, 290.538116f}, {221.540283f, 19.815796f, 292.069458f}, {244.759033f, 20.792755f, 306.069427f}};
    c.tops = {{171.540283f, 32.233414f, 297.413116f}, {196.540283f, 32.444138f, 290.538086f}, {221.540283f, 32.413773f, 292.069519f}, {244.759033f, 33.390732f, 306.069519f}};
    c.neighbors = {39, 40, 65};
    c.route_eye = {219.5157f, 22.7909f, 265.9532f};
    c.camera = 23;
    c.joint = 2;
    c.neighbor16 = true;
    return c;
}

constexpr std::array<std::size_t, 4> pilot_meshes{14, 15, 100, 101};

bool pilot_mesh(std::size_t mesh) {
    return std::ranges::find(pilot_meshes, mesh) != pilot_meshes.end();
}

Point world_position(const ModelVertex& vertex, const ModelNode& node) {
    Point p{};
    for (unsigned row = 0; row < 3; ++row) {
        p[row] = node.transform[12 + row];
        for (unsigned axis = 0; axis < 3; ++axis) {
            p[row] += node.transform[axis * 4 + row] * vertex.position[axis];
        }
    }
    return p;
}

Point panel_point(const Model& model, const OultonCase& c, const Panel& p, std::size_t vertex) {
    return world_position(model.meshes[c.meshes[p.part]].vertices.at(vertex), model.nodes[c.nodes[p.part]]);
}

Point normal(const Model& model, const OultonCase& c, const Panel& p) {
    const auto& mesh = model.meshes[c.meshes[p.part]];
    return mesh.vertices[mesh.faces.at(p.face).indices[0]].normal;
}

void source_contract(const Model& source, const OultonCase& c) {
    for (unsigned part = 0; part < 2; ++part) {
        const auto& mesh = source.meshes.at(c.meshes[part]);
        expect(mesh.name == "TREES_" + std::to_string(c.meshes[part] + 1) && mesh.vertices.size() == c.vertices[part] && mesh.faces.size() == c.triangles[part],
            "Oulton source type/count differs");
        expect(source.nodes.at(c.nodes[part]).variants == std::vector<std::uint16_t>{std::uint16_t(c.meshes[part])}, "Oulton requires exact original root binding");
    }
    for (unsigned i = 0; i < c.panels.size(); ++i) {
        const auto& p = c.panels[i];
        expect(close(panel_point(source, c, p, p.left), c.bottoms[i]) && close(panel_point(source, c, p, p.right), c.bottoms[i + 1])
                   && close(panel_point(source, c, p, p.top_left), c.tops[i]) && close(panel_point(source, c, p, p.top_right), c.tops[i + 1]),
            "Source-specific panel points disagree with original transforms");
        const auto& mesh = source.meshes[c.meshes[p.part]];
        for (const auto index : {p.left, p.right, p.top_left, p.top_right}) {
            const auto& v = mesh.vertices.at(index);
            expect(v.u == 0 || v.u == 1, "Canonical panel corner must retain full U");
            expect(v.v == 0 || v.v == 1, "Canonical panel corner must retain original V+1 orientation");
        }
        expect(mesh.faces.at(p.face).texture == 0 && mesh.faces.at(p.face + 1).texture == 0, "Selected panel must use only canonical LLRTS1 slot0");
    }
}

void verify_joints(const Model& source, const Model& rebuilt, const OultonCase& c) {
    for (unsigned joint = 1; joint < c.panels.size(); ++joint) {
        const auto& before = c.panels[joint - 1];
        const auto& after = c.panels[joint];
        const auto B = panel_point(source, c, before, before.right);
        const auto direction = unit_vector(add(normal(source, c, before), normal(source, c, after)));
        expect(close(B, panel_point(source, c, after, after.left)) && close(panel_point(source, c, before, before.top_right), panel_point(source, c, after, after.top_left)),
            "Canonical panels must share a full joint");
        const Point plane_normal{-direction[2], 0, direction[0]};
        const auto on_joint = [&](Point p) {
            const auto d = sub(p, B);
            return std::abs(d[0] * plane_normal[0] + d[2] * plane_normal[2]) < .0003f;
        };
        std::array<std::vector<Point>, 2> edges;
        const std::array parts{before.part, after.part};
        for (unsigned side = 0; side < 2; ++side) {
            const auto part = parts[side];
            const auto& mesh = rebuilt.meshes[c.meshes[part]];
            const auto& node = rebuilt.nodes[c.nodes[part]];
            for (const auto& face : mesh.faces) {
                if (face.texture == 0) {
                    std::array<Point, 3> points{};
                    bool cap = true;
                    for (unsigned corner = 0; corner < 3; ++corner) {
                        points[corner] = world_position(mesh.vertices[face.indices[corner]], node);
                        const bool joint = on_joint(points[corner]);
                        cap = cap && joint;
                        if (joint && !std::ranges::any_of(edges[side], [&](Point p) {
                                return close(p, points[corner]);
                            })) {
                            edges[side].push_back(points[corner]);
                        }
                    }
                    if (cap) {
                        const auto a = sub(points[1], points[0]), b = sub(points[2], points[0]);
                        expect(length({a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}) < .00001f,
                            "Shared Oulton joint must not contain an internal closing cap");
                    }
                }
            }
        }
        expect(edges[0].size() == 4 && edges[0].size() == edges[1].size(), "Both Oulton panel sides must expose matching full miter section");
        for (const auto p : edges[0]) {
            expect(std::ranges::any_of(edges[1],
                       [&](Point q) {
                           return close(p, q);
                       }),
                "Oulton panels must meet without geometric joint gap");
        }
        const auto up = sub(panel_point(source, c, before, before.top_right), B);
        constexpr std::array fractions{0.f, .5f, 1.f}, offsets{0.f, .7f, 0.f};
        for (unsigned row = 0; row < fractions.size(); ++row) {
            for (const float sign : {-1.f, 1.f}) {
                const auto point = add(add(B, scale(up, fractions[row])), scale(direction, sign * offsets[row]));
                for (const auto& edge : edges) {
                    expect(std::ranges::any_of(edge,
                               [&](Point q) {
                                   return close(point, q);
                               }),
                        "Oulton miter must retain exact shared source-height section");
                }
            }
        }
        std::cout << "JOINT " << joint << " shared_positions=" << edges[0].size() << " internal_caps=0\n";
    }
}

void verify(const SceneResources& original, const SceneResources& mega, const OultonCase& c) {
    const auto& a = original.trees;
    const auto& b = mega.trees;
    expect(a.meshes.size() == b.meshes.size() && a.nodes.size() == b.nodes.size() && a.textures.size() == b.textures.size(), "Oulton must preserve every source slot");
    source_contract(a, c);
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i];
        const auto& y = b.nodes[i];
        expect(x.parent == y.parent && x.transform == y.transform && x.id_flags == y.id_flags && x.variants == y.variants, "Oulton node/transform/sector/variant changed");
    }
    for (unsigned part = 0; part < 2; ++part) {
        const auto& old = a.meshes[c.meshes[part]];
        const auto& rebuilt = b.meshes[c.meshes[part]];
        expect(rebuilt.faces.size() > old.faces.size(), "Oulton must add real depth to the source panel geometry");
        float depth = 0;
        std::array<float, 3> lo{32768, 32768, 32768}, hi{-32768, -32768, -32768};
        for (const auto& f : old.faces) {
            if (f.texture == 0) {
                for (const auto i : f.indices) {
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        lo[axis] = std::min(lo[axis], old.vertices[i].position[axis]);
                        hi[axis] = std::max(hi[axis], old.vertices[i].position[axis]);
                    }
                }
            }
        }
        for (const auto& f : rebuilt.faces) {
            expect(f.texture == 0 || (c.meshes[part] == 100 && f.texture == 1), "Oulton adds no material region");
            if (f.texture != 0) {
                continue;
            }
            expect(f.color == old.faces[0].color, "Oulton foliage color changed");
            for (const auto i : f.indices) {
                const auto& v = rebuilt.vertices.at(i);
                float nearest = 32768;
                for (const auto& face : old.faces) {
                    if (face.texture == 0) {
                        const auto& p = old.vertices[face.indices[0]];
                        float distance = 0;
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            distance += (v.position[axis] - p.position[axis]) * p.normal[axis];
                        }
                        nearest = std::min(nearest, std::abs(distance));
                    }
                }
                depth = std::max(depth, nearest);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const float margin = axis == 1 ? .002f : 1.8f;
                    expect(v.position[axis] >= lo[axis] - margin && v.position[axis] <= hi[axis] + margin, "Oulton body leaves bounded source height/footprint");
                }
                expect(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1, "Oulton runtime UV outside accepted atlas");
            }
        }
        expect(depth > .1f && depth < 1.8f, "Oulton body must have bounded real depth");
        std::cout << "PILOT mesh=" << c.meshes[part] << " depth=" << depth << " triangles=" << rebuilt.faces.size() << '\n';
    }
    verify_joints(a, b, c);
    expect(same_mesh(faces_with_slot(a.meshes[100], 1), faces_with_slot(b.meshes[100], 1)), "Unrelated LLRTS2 faces in Mesh100 must remain source-exact");
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        if (!pilot_mesh(i)) {
            expect(same_mesh(a.meshes[i], b.meshes[i]), "Other Oulton meshes, including neighbors, must retain exact source attributes");
        }
    }
    expect(original.trees_baked.chunks.size() == mega.trees_baked.chunks.size(), "Oulton baked source count changed");
    for (std::size_t i = 0; i < original.trees_baked.chunks.size(); ++i) {
        const auto& x = original.trees_baked.chunks[i];
        const auto& y = mega.trees_baked.chunks[i];
        expect(x.node_index == y.node_index && x.variant_index == y.variant_index && x.origin_xz == y.origin_xz, "Oulton baked registration changed");
        const auto mesh = a.nodes.at(x.node_index).variants.at(x.variant_index);
        if (!pilot_mesh(mesh)) {
            expect(same_mesh(x.mesh, y.mesh), "Other Oulton baked chunks must remain source-exact");
        }
        if (mesh == 100) {
            expect(same_mesh(faces_with_slot(x.mesh, 1), faces_with_slot(y.mesh, 1)), "Oulton baked LLRTS2 region changed");
        }
    }
    expect(a.textures.at(0).name == "LLRTS1" && a.textures.at(1).name == "LLRTS2" && b.textures[0].name == "LLRTS1" && b.textures[1].name == "LLRTS2",
        "Oulton foliage slot names changed");
    expect(b.textures[0].encoding == ImageEncoding::webp && b.textures[1].encoding == ImageEncoding::webp, "Oulton must load authored foliage artwork");
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "Oulton ground/collision/routes/sectors changed");
}

struct CameraCase {
    std::string name;
    CameraInputs inputs;
    bool dense;
};

CameraInputs look_at(Point eye, Point target) {
    std::array<SceneCamera, 1> origins{};
    std::array<std::int32_t, 3> q{};
    for (unsigned a = 0; a < 3; ++a) {
        origins[0].transform[12 + a] = std::int32_t(std::lround(eye[a] * 65536));
        q[a] = std::int32_t(std::lround(target[a] * 65536));
    }
    const FinishCamera look(origins);
    return {look.world_to_view(q), default_projection(4, width, height), look.position()};
}

std::vector<CameraCase> cameras(const SceneResources& source, const OultonCase& c) {
    const auto baked_point = [&](const Panel& p, std::size_t index) {
        return source.trees_baked.chunks[chunk(source, c.nodes[p.part])].mesh.vertices.at(index).position;
    };
    const auto& first = c.panels.front();
    const auto& last = c.panels.back();
    const auto A = baked_point(first, first.left), Z = baked_point(last, last.right), joint = baked_point(c.panels[c.joint - 1], c.panels[c.joint - 1].right);
    const auto& lm = source.trees_baked.chunks[chunk(source, c.nodes[first.part])].mesh;
    const auto& rm = source.trees_baked.chunks[chunk(source, c.nodes[last.part])].mesh;
    auto front = unit_vector(add(lm.vertices[lm.faces[first.face].indices[0]].normal, rm.vertices[rm.faces[last.face].indices[0]].normal));
    front[1] = 0;
    front = unit_vector(front);
    const float h = (baked_point(first, first.top_right)[1] - baked_point(first, first.right)[1] + baked_point(last, last.top_right)[1] - Z[1]) * .5f;
    const float span = length(sub(Z, A));
    const auto target = add(joint, {0, h * .48f, 0});
    std::vector<CameraCase> result;
    const auto append = [&](std::string name, Point eye, Point aim, bool dense) {
        result.push_back({name, look_at(eye, aim), dense});
        std::cout << "CAMERA " << name << " eye=" << eye[0] << ',' << eye[1] << ',' << eye[2] << " target=" << aim[0] << ',' << aim[1] << ',' << aim[2] << '\n';
    };
    for (unsigned v = 0; v < 4; ++v) {
        const float angle = v == 0 ? 0.f : v == 1 ? std::numbers::pi_v<float> : v == 2 ? .65f : std::numbers::pi_v<float> * .5f;
        const Point direction{front[0] * std::cos(angle) - front[2] * std::sin(angle), 0, front[0] * std::sin(angle) + front[2] * std::cos(angle)};
        append(std::array{"front", "back", "oblique", "side"}[v], add(add(target, scale(direction, span * .9f)), {0, h * .18f, 0}), target, false);
    }
    const auto previous = baked_point(last, last.left);
    auto tangent = unit_vector(Point{Z[0] - previous[0], 0, Z[2] - previous[2]});
    const auto end_target = add(Z, {0, h * .45f, 0});
    append(std::string(c.end_label), add(add(add(end_target, scale(tangent, h * 2.6f)), scale(front, h * .2f)), {0, h * .15f, 0}), end_target, false);
    append("route", c.route_eye, target, true);
    expect(source.cameras.size() > c.camera, "Oulton needs exact authored camera");
    Point eye{};
    for (unsigned a = 0; a < 3; ++a) {
        eye[a] = source.cameras[c.camera].transform[12 + a] / 65536.f;
    }
    append("camera" + std::to_string(c.camera), eye, target, true);
    return result;
}

SceneResources comparison(const SceneResources& original, const SceneResources& mega, unsigned mode, bool dense, const OultonCase& c) {
    SceneResources scene = dense ? mega : SceneResources{};
    scene.setup = mega.setup;
    scene.ground.sector_centers = original.ground.sector_centers;
    auto& baked = scene.trees_baked;
    if (dense) {
        for (unsigned part = 0; part < 2; ++part) {
            const auto index = chunk(original, c.nodes[part]);
            if (mode < 2) {
                scene.trees.meshes[c.meshes[part]] = original.trees.meshes[c.meshes[part]];
                baked.chunks[index] = original.trees_baked.chunks[index];
            }
            if (mode == 0) {
                const auto local_slot = std::int16_t(scene.trees.textures.size());
                scene.trees.textures.push_back(original.trees.textures[0]);
                for (auto& f : scene.trees.meshes[c.meshes[part]].faces) {
                    if (f.texture == 0) {
                        f.texture = local_slot;
                    }
                }
                const auto slot = std::int16_t(baked.textures.size());
                baked.textures.push_back(original.trees_baked.textures[0]);
                for (auto& f : baked.chunks[index].mesh.faces) {
                    if (f.texture == 0) {
                        f.texture = slot;
                    }
                }
            }
        }
    } else {
        baked.textures = {(mode == 0 ? original : mega).trees_baked.textures[0]};
        for (const auto node : c.nodes) {
            auto selected = (mode == 2 ? mega : original).trees_baked.chunks[chunk(original, node)];
            keep_slot(selected, 0);
            baked.chunks.push_back(std::move(selected));
        }
    }
    return scene;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const SceneResources& scene, const CameraInputs& camera, const std::string& path = {}) {
    SceneView view(renderer, scene);
    SourceRandom random(7);
    FlagAnimation flags(scene.flags, random);
    HorizonActors actors(scene.moving_horizon);
    auto frame = view.frame(camera, scene_lighting(scene.setup), {unit}, flags.variants(), actors.frame());
    if (frame.indices.empty()) {
        frame.quad({}, -8, -8, 1, 1);
    }
    expect(renderer.draw(frame, false), "Oulton GPU target unavailable");
    if (!path.empty()) {
        renderer.save_screenshot(path);
    }
    return renderer.readback();
}

SceneResources without(SceneResources scene, std::size_t node) {
    if (node == 260) {
        // This node also carries three unrelated LLRTS2 faces. Keep those in
        // the surroundings so coverage proves the rebuilt LLRTS1 body only.
        if (scene.trees.meshes.size() > 100) {
            std::erase_if(scene.trees.meshes[100].faces, [](const auto& face) {
                return face.texture != 1;
            });
        }
        for (auto& c : scene.trees_baked.chunks) {
            if (c.node_index == node) {
                keep_slot(c, 1);
            }
        }
        return scene;
    }
    if (node < scene.trees.nodes.size()) {
        scene.trees.nodes[node].variants.clear();
    }
    std::erase_if(scene.trees_baked.chunks, [&](const auto& c) {
        return c.node_index == node;
    });
    return scene;
}

std::size_t difference(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    expect(a.size() == width * height && a.size() == b.size(), "Oulton GPU readback dimensions differ");
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        n += a[i] != b[i];
    }
    return n;
}

void neighbor16(gpu::Renderer& renderer, const SceneResources& source, const SceneResources& mega, const OultonCase& c, std::ofstream& csv, std::ofstream& coverage) {
    expect(source.trees.nodes.at(65).variants == std::vector<std::uint16_t>{16} && same_mesh(source.trees.meshes[16], mega.trees.meshes[16]),
        "Neighbor16 must remain its exact original binding/geometry");
    expect(source.cameras.size() > 24, "Neighbor16 visibility probe needs original camera24");
    const auto index = chunk(source, 65);
    const auto& mesh = source.trees_baked.chunks[index].mesh;
    Point lo{32768, 32768, 32768}, hi{-32768, -32768, -32768}, eye{}, target{};
    for (const auto& v : mesh.vertices) {
        for (unsigned a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], v.position[a]);
            hi[a] = std::max(hi[a], v.position[a]);
        }
    }
    for (unsigned a = 0; a < 3; ++a) {
        target[a] = (lo[a] + hi[a]) * .5f;
        eye[a] = source.cameras[24].transform[12 + a] / 65536.f;
    }
    for (const auto& face : mesh.faces) {
        const auto& p = mesh.vertices[face.indices[0]];
        float distance = 0;
        for (unsigned a = 0; a < 3; ++a) {
            distance += (eye[a] - p.position[a]) * p.normal[a];
        }
        expect(distance > 0, "Neighbor16 authored camera must be on actual front-facing side");
    }
    const auto camera = look_at(eye, target);
    constexpr std::array labels{"original", "ai-original-geometry", "mega"};
    std::array<std::vector<std::uint32_t>, 3> images;
    std::array<std::size_t, 3> counts{};
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto scene = mega;
        if (mode == 0) {
            scene.trees.meshes[16] = source.trees.meshes[16];
            scene.trees_baked.chunks[index] = source.trees_baked.chunks[index];
            const auto local_slot = std::int16_t(scene.trees.textures.size());
            scene.trees.textures.push_back(source.trees.textures[1]);
            for (auto& f : scene.trees.meshes[16].faces) {
                f.texture = local_slot;
            }
            const auto slot = std::int16_t(scene.trees_baked.textures.size());
            scene.trees_baked.textures.push_back(source.trees_baked.textures[1]);
            for (auto& f : scene.trees_baked.chunks[index].mesh.faces) {
                f.texture = slot;
            }
        }
        images[mode] = render(renderer, scene, camera, std::string(c.prefix) + "-neighbor16-camera24-" + labels[mode] + ".png");
        counts[mode] = difference(images[mode], render(renderer, without(scene, 65), camera));
        coverage << "neighbor16-camera24," << labels[mode] << ",65,16," << counts[mode] << ',' << (counts[mode] ? "visible" : "not-visible") << '\n';
    }
    const auto material = difference(images[0], images[1]);
    expect(images[1] == images[2], "Unchanged neighbor16 AI and Mega geometry must produce identical scene pixels");
    if (counts[0] > 100 && counts[1] > 100) {
        expect(material > 20, "Visible neighbor16 must exercise actual accepted foliage artwork");
    }
    csv << "neighbor16-camera24," << material << ",0," << counts[1] << ',' << counts[1] << ',' << counts[2] << '\n';
    csv.flush();
    coverage.flush();
    std::cout << "NEIGHBOR16 camera24 source=" << counts[0] << " ai=" << counts[1] << " mega=" << counts[2] << " detail_visible=" << (counts[2] > 100) << " geometry_unchanged=1\n";
}

void pixels(gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const OultonCase& c) {
    const auto views = cameras(original, c);
    constexpr std::array labels{"original", "ai-original-geometry", "mega"};
    std::ofstream csv(std::string(c.prefix) + "-gpu.csv"), coverage(std::string(c.prefix) + "-coverage.csv");
    csv << "view,material_pixels,geometry_pixels,ai_pair_coverage,retained_pair_coverage,mega_pair_coverage\n";
    coverage << "view,mode,node,mesh,visible_pixels,status\n";
    std::array<std::size_t, 2> best{};
    for (unsigned v = 0; v < views.size(); ++v) {
        const auto& view = views[v];
        std::array<std::vector<std::uint32_t>, 3> images, without_pair;
        std::array<std::size_t, 3> pair_coverage{};
        for (unsigned mode = 0; mode < 3; ++mode) {
            const auto scene = comparison(original, mega, mode, view.dense, c);
            images[mode] = render(renderer, scene, view.inputs, std::string(c.prefix) + "-" + view.name + "-" + labels[mode] + ".png");
            without_pair[mode] = render(renderer, without(without(scene, c.nodes[0]), c.nodes[1]), view.inputs);
            pair_coverage[mode] = difference(images[mode], without_pair[mode]);
            std::vector<std::size_t> candidates(c.nodes.begin(), c.nodes.end());
            if (view.dense) {
                candidates.insert(candidates.end(), c.neighbors.begin(), c.neighbors.end());
            }
            for (const auto node : candidates) {
                const auto n = difference(images[mode], render(renderer, without(scene, node), view.inputs));
                const auto mesh = original.trees.nodes.at(node).variants.at(0);
                coverage << view.name << ',' << labels[mode] << ',' << node << ',' << mesh << ',' << n << ',' << (n ? "visible" : "not-visible") << '\n';
                for (unsigned part = 0; part < 2; ++part) {
                    if (mode == 2 && node == c.nodes[part]) {
                        best[part] = std::max(best[part], n);
                    }
                }
            }
        }
        const auto material = difference(images[0], images[1]), geometry = difference(images[1], images[2]);
        std::size_t retained = 0;
        for (std::size_t i = 0; i < images[1].size(); ++i) {
            if (images[1][i] != without_pair[1][i]) {
                retained += images[2][i] != without_pair[2][i];
            }
        }
        csv << view.name << ',' << material << ',' << geometry << ',' << pair_coverage[1] << ',' << retained << ',' << pair_coverage[2] << '\n';
        csv.flush();
        coverage.flush();
        std::cout << "VIEW " << view.name << " material=" << material << " geometry=" << geometry << " retained=" << retained << '/' << pair_coverage[1]
                  << " mega=" << pair_coverage[2] << '\n';
        expect(geometry > 20 && pair_coverage[2] > 100, "Oulton views must show actual new geometry and foliage coverage");
        if (pair_coverage[0] > 100) {
            expect(material > 20, "Source-visible Oulton views must exercise accepted dense AI artwork");
        }
        if (v == 0 || v == 5) {
            expect(pair_coverage[1] > 100 && retained * 100 >= pair_coverage[1] * 85, "Oulton front/route must retain source-facing AI foliage silhouette");
        }
    }
    expect(best[0] > 100 && best[1] > 100, "Each Oulton pilot node must independently show actual native coverage");
    if (c.neighbor16) {
        neighbor16(renderer, original, mega, c, csv, coverage);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "wedge-14-15"), "Usage: toca-woodland-oulton-tests CANONICAL_ROOT MEGA_ROOT [wedge-14-15]");
        const auto selected = argc == 4 ? wedge_case() : joined_case();
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated Oulton woodland");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Oulton woodland", width, height, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create isolated Oulton woodland window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[1]);
        const auto original = load_scene_resources(store, resolve_setup({.track_id = 4, .weather_choice = 2, .detail = 3}, 2));
        const auto mega = load_scene_resources(store, resolve_setup({.track_id = 4, .weather_choice = 2, .detail = 4}, 2), argv[2]);
        verify(original, mega, selected);
        pixels(renderer, original, mega, selected);
        std::cout << "PASS: Oulton " << selected.parameter << " seven views/three modes, exact accepted pilots/other source geometry and measured per-node context\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
