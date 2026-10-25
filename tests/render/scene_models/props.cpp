#include "props.hpp"

#include "../world_geometry.hpp"
#include "assets/store.hpp"
#include "fixtures.hpp"
#include "formats/glb.hpp"
#include "formats/static_model.hpp"
#include "frontend/resources.hpp"
#include "gpu_fixture.hpp"
#include "image/decode.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/follow_camera.hpp"
#include "scene/loading.hpp"
#include "scene/shadow_support.hpp"
#include "scene/view.hpp"
#include "simulation/fixed.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>

namespace toca::test::scene_models {
toca::scene::PropResources first_prop(const toca::scene::PropResources& source) {
    expect(!source.actors.empty(), "Mega closeup requires an authored prop actor");
    const auto root = source.actors.front().root_node;
    auto result = source;
    result.baked = {};
    result.actors = {{0}};
    result.model.nodes.clear();
    for (std::size_t index = root; index < source.model.nodes.size(); ++index) {
        auto node = source.model.nodes[index];
        if (index != root && node.parent < 0) {
            break;
        }
        if (index == root) {
            node.transform[12] = node.transform[13] = node.transform[14] = 0;
        } else {
            node.parent = std::int16_t(node.parent - root);
        }
        result.model.nodes.push_back(std::move(node));
    }
    return result;
}

void mega_prop_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& original, const toca::scene::SceneResources& mega,
    toca::scene::PropResources toca::scene::SceneResources::* family, std::string_view name, int width, int height, bool batch) {
    using namespace toca::scene;
    SceneResources detailed{}, canonical{};
    detailed.setup = mega.setup;
    canonical.setup = original.setup;
    detailed.*family = first_prop(mega.*family);
    canonical.*family = first_prop(original.*family);
    const auto& model = (detailed.*family).model;
    const auto matrices = model_world_transforms(model, unit);
    std::array<float, 3> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{-minimum[0], -minimum[1], -minimum[2]}, facing{0, 0, -1};
    float largest = 0;
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const auto& node = model.nodes[index];
        if (node.variants.empty() || node.variants[0] == 65535) {
            continue;
        }
        const auto& mesh = model.meshes.at(node.variants[0]);
        const auto& matrix = matrices[index];
        for (const auto& vertex : mesh.vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto p = vertex.position[0] * matrix[axis] + vertex.position[1] * matrix[4 + axis] + vertex.position[2] * matrix[8 + axis] + matrix[12 + axis];
                minimum[axis] = std::min(minimum[axis], p);
                maximum[axis] = std::max(maximum[axis], p);
            }
        }
        for (const auto& face : mesh.faces) {
            if (face.texture < 0) {
                continue;
            }
            std::array<std::array<float, 3>, 3> points{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& p = mesh.vertices[face.indices[corner]].position;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    points[corner][axis] = p[0] * matrix[axis] + p[1] * matrix[4 + axis] + p[2] * matrix[8 + axis] + matrix[12 + axis];
                }
            }
            std::array<float, 3> normal{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto j = (axis + 1) % 3, k = (axis + 2) % 3;
                normal[axis] = (points[1][j] - points[0][j]) * (points[2][k] - points[0][k]) - (points[1][k] - points[0][k]) * (points[2][j] - points[0][j]);
            }
            const auto area = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
            if (normal[1] * normal[1] < area * .5f && ((name == "signs" && largest == 0) || (name != "signs" && area > largest))) {
                largest = area;
                facing = normal;
            }
        }
    }
    // View signs from their authored first/front surface. Cones/posts use an oblique
    // view so the body, base and sides all contribute pixels.
    if (name != "signs") {
        facing = {1, .6f, -1};
    }
    const auto length = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
    const auto extent = std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2], .5f});
    std::array<std::int32_t, 3> target{};
    std::array<SceneCamera, 1> cameras{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto center = (minimum[axis] + maximum[axis]) / 2;
        target[axis] = std::int32_t(center * 65536.f);
        cameras[0].transform[12 + axis] = std::int32_t((center + facing[axis] / length * extent * 2) * 65536.f);
    }
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view(target), default_projection(4, width, height)};
    const auto prefix = "scene-prop-" + (batch ? "track" + std::to_string(original.setup.selection.track_id) + "-" : "") + std::string(name);
    const auto render = [&](const SceneResources& resources, std::string_view quality) {
        SceneView view(renderer, resources);
        const auto frame = view.frame(camera, scene_lighting(resources.setup), {unit}, {}, {});
        expect(!toca::test::materialize_world(frame).vertices.empty()
                   && std::ranges::any_of(frame.draws,
                       [](const auto& draw) {
                           return draw.state.texture != nullptr;
                       }),
            "Generic prop must reach actual textured GPU geometry in its closeup");
        expect(renderer.draw(frame, false), "Mega prop closeup target was unavailable");
        renderer.save_screenshot(prefix + "-" + std::string(quality) + ".png");
        return renderer.readback();
    };
    const auto before = render(canonical, "very-high");
    if (batch) {
        auto artwork = canonical;
        (artwork.*family).model.textures = model.textures;
        (void)render(artwork, "ai-materials-original-geometry");
    }
    const auto after = render(detailed, "mega");
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < before.size(); ++pixel) {
        changed += before[pixel] != after[pixel];
    }
    expect(changed > 100, "Regenerated generic prop geometry/materials must change real closeup GPU pixels");
}

void mega_static_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& original, const toca::scene::SceneResources& mega, bool tyres, std::string_view mesh_name,
    int width, int height) {
    using namespace toca::scene;
    const bool named = !mesh_name.empty();
    const auto& before = tyres ? original.fence_baked : original.objects_baked;
    const auto& after = tyres ? mega.fence_baked : mega.objects_baked;
    expect(before.chunks.size() == after.chunks.size(), "Static Mega probe requires preserved canonical sector chunks");
    const auto semantic = [&](const toca::ModelFace& face) {
        return face.texture >= 0 && (named || before.textures.at(std::size_t(face.texture)).name == (tyres ? "TYRES" : "CASTROL"));
    };
    std::size_t selected = before.chunks.size();
    for (std::size_t index = 0; index < before.chunks.size(); ++index) {
        const auto& source = before.chunks[index].mesh;
        if (named ? source.name == mesh_name
                  : after.chunks[index].mesh.faces.size() > source.faces.size() && (tyres || source.faces.size() <= 8) && std::ranges::any_of(source.faces, semantic)) {
            selected = index;
            break;
        }
    }
    expect(selected < before.chunks.size(),
        named ? "Requested scenery mesh must have an original baked placement" : "Canonical Donington must supply a genuinely detailed static tyre barrier and sponsor board");
    const auto& source = before.chunks[selected].mesh;
    const auto& detailed = after.chunks[selected].mesh;
    std::array<float, 3> minimum{32768, 32768, 32768}, maximum{-32768, -32768, -32768};
    for (const auto& vertex : source.vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }
    std::array<float, 3> center{}, facing{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        center[axis] = (minimum[axis] + maximum[axis]) / 2;
    }
    const auto front = std::ranges::find_if(source.faces, semantic);
    expect(front != source.faces.end(), "Static scenery probe requires a source textured face");
    facing = source.vertices.at(front->indices[0]).normal;
    if (tyres && !named) {
        // Original TYRES fronts have horizontal normals. A new shoulder/inner
        // wall normal identifies physical tire geometry on that same material.
        bool found = false;
        for (const auto& face : detailed.faces) {
            if (face.texture < 0 || after.textures.at(std::size_t(face.texture)).name != "TYRES") {
                continue;
            }
            for (const auto index : face.indices) {
                const auto& vertex = detailed.vertices.at(index);
                if (std::abs(vertex.normal[1]) > .4f) {
                    center = vertex.position;
                    center[1] += .45f;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        expect(found, "Static tyre GLB must contain new rounded shoulders and inner walls");
    }
    const auto normal_length = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
    expect(normal_length > 0, "Static scenery probe requires an authored front normal");
    for (auto& component : facing) {
        component /= normal_length;
    }
    // An oblique view reveals the solid backing and rounded tire silhouettes;
    // the second comparison holds all AI textures fixed to isolate geometry.
    if (named) {
        const std::array<float, 3> side{-facing[2], 0, facing[0]};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            facing[axis] += side[axis] * .3f;
        }
        facing[1] += .12f;
    } else {
        facing[0] += -facing[2] * .3f;
        facing[1] += tyres ? .35f : .12f;
    }
    const auto distance = named   ? std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]}) * 1.35f
                          : tyres ? 3.f
                                  : std::max(maximum[0] - minimum[0], maximum[2] - minimum[2]) * 1.35f;
    std::array<std::int32_t, 3> target{};
    std::array<SceneCamera, 1> cameras{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        target[axis] = std::int32_t(center[axis] * 65536.f);
        cameras[0].transform[12 + axis] = std::int32_t((center[axis] + facing[axis] * distance) * 65536.f);
    }
    const FinishCamera look(cameras);
    CameraInputs camera{look.world_to_view(target), default_projection(4, width, height)};
    camera.registered_camera_position = target;
    const auto prefix =
        named ? "scene-static-track" + std::to_string(original.setup.selection.track_id) + "-" + std::string(mesh_name) : "scene-static-" + std::string(tyres ? "tyres" : "board");
    const auto render = [&](bool geometry, bool materials, std::string_view label) {
        SceneResources isolated{};
        isolated.setup = mega.setup;
        isolated.ground.sector_centers = original.ground.sector_centers;
        auto& baked = tyres ? isolated.fence_baked : isolated.objects_baked;
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
        SceneView view(renderer, isolated);
        const auto frame = view.frame(camera, scene_lighting(isolated.setup), {unit}, {}, {});
        const auto projected = toca::test::materialize_world(frame);
        if (named) {
            expect(!projected.indices.empty(), "Selected scenery must reach the actual GPU through its original placement");
        } else {
            expect(projected.indices.size() > 3 && projected.vertices.size() < 65536, "New static scenery must reach bounded geometry through canonical sector registration");
        }
        expect(renderer.draw(frame, false), "Static Mega closeup target was unavailable");
        renderer.save_screenshot(prefix + "-" + std::string(label) + ".png");
        return std::pair{renderer.readback(), projected.indices.size() / 3};
    };
    const auto canonical = render(false, false, "very-high");
    const auto textured = render(false, true, "ai-materials-original-geometry");
    const auto generated = render(true, true, "mega");
    std::size_t material_changes = 0, geometry_changes = 0;
    for (std::size_t pixel = 0; pixel < generated.first.size(); ++pixel) {
        material_changes += (named ? textured.first[pixel] : generated.first[pixel]) != canonical.first[pixel];
        geometry_changes += generated.first[pixel] != textured.first[pixel];
    }
    if (named) {
        expect(material_changes > 0
                   && std::ranges::any_of(generated.first,
                       [](auto pixel) {
                           return pixel != toca::gpu::background;
                       }),
            "Selected scenery artwork must reach visible GPU pixels");
        std::cout << "PASS: " << prefix << ' ' << width << 'x' << height << ", original/artwork/Mega triangles " << canonical.second << '/' << textured.second << '/'
                  << generated.second << ", material_pixels=" << material_changes << " geometry_pixels=" << geometry_changes << '\n';
        for (std::size_t axis = 0; axis < 3; ++axis) {
            cameras[0].transform[12 + axis] = std::int32_t((center[axis] - facing[axis] * distance) * 65536.f);
        }
        const FinishCamera rear(cameras);
        camera.world_to_view = rear.world_to_view(target);
        (void)render(true, true, "mega-rear");
    } else {
        expect(material_changes > 100 && geometry_changes > 40, "Real Mega static geometry must change GPU pixels independently of its generated materials");
    }
}
} // namespace toca::test::scene_models
