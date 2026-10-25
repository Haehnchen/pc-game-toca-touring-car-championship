#include "assets/store.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "scene_models/props.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>

namespace {
using namespace toca::test::scene_models;

void banner_reference(const std::filesystem::path& root, const std::filesystem::path& mega_root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    const auto mega = load_scene_resources(store, resolve_setup({.track_id = 1, .weather_choice = 1, .detail = 4}), mega_root);
    constexpr unsigned width = 1280, height = 720;
    GpuFixture gpu("TOCA Silverstone banners", width, height);
    auto& renderer = gpu.renderer;
    // Authored opposing placements near Luffield, including the tight ORDSURV gap.
    for (const auto pair : {std::array{343u, 351u}, {345u, 357u}, {349u, 353u}, {355u, 369u}, {359u, 361u}}) {
        SceneResources full;
        full.setup = mega.setup;
        full.ground.sector_centers = mega.ground.sector_centers;
        full.objects_baked.textures = mega.objects_baked.textures;
        for (const auto node : pair) {
            const auto chunk = std::ranges::find_if(mega.objects_baked.chunks, [&](const auto& candidate) {
                return candidate.node_index == node && candidate.variant_index == 0;
            });
            expect(chunk != mega.objects_baked.chunks.end(), "Canonical opposing banner placement is missing");
            full.objects_baked.chunks.push_back(*chunk);
        }
        auto fronts = full;
        for (auto& chunk : fronts.objects_baked.chunks) {
            for (std::size_t face = chunk.mesh.faces.size(); face-- > 0;) {
                const auto slot = chunk.mesh.faces[face].texture;
                if (slot >= 0 && fronts.objects_baked.textures.at(std::size_t(slot)).name == "MegaSurface") {
                    chunk.mesh.faces.erase(chunk.mesh.faces.begin() + std::ptrdiff_t(face));
                    chunk.face_flags.erase(chunk.face_flags.begin() + std::ptrdiff_t(face));
                }
            }
        }
        for (unsigned side = 0; side < 2; ++side) {
            const auto& mesh = fronts.objects_baked.chunks[side].mesh;
            glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(-minimum);
            for (const auto& face : mesh.faces) {
                for (const auto index : face.indices) {
                    const auto point = glm::make_vec3(mesh.vertices[index].position.data());
                    minimum = glm::min(minimum, point);
                    maximum = glm::max(maximum, point);
                }
            }
            const auto center = (minimum + maximum) * .5f;
            const auto& face = mesh.faces.front();
            const auto a = glm::make_vec3(mesh.vertices[face.indices[0]].position.data());
            const auto b = glm::make_vec3(mesh.vertices[face.indices[1]].position.data());
            const auto c = glm::make_vec3(mesh.vertices[face.indices[2]].position.data());
            const auto normal = glm::normalize(glm::cross(b - a, c - a));
            for (const float distance : {50.f, 80.f}) {
                std::array<SceneCamera, 1> cameras{};
                std::array<std::int32_t, 3> target{};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    target[axis] = std::int32_t(center[axis] * 65536);
                    cameras[0].transform[12 + axis] = std::int32_t((center[axis] + normal[axis] * distance) * 65536);
                }
                const FinishCamera look(cameras);
                CameraInputs camera{look.world_to_view(target), default_projection(4, width, height)};
                camera.hide_player = true;
                const auto prefix = "silverstone-banner-" + std::to_string(pair[side]) + "-" + std::to_string(int(distance));
                const auto render = [&](const SceneResources& resources, const char* label) {
                    SceneView view(renderer, resources);
                    expect(renderer.draw(view.frame(camera, scene_lighting(resources.setup), {unit}, {}, {}), false), "Banner target unavailable");
                    renderer.save_screenshot(prefix + label + ".png");
                    return renderer.readback();
                };
                const auto expected = render(fronts, "-fronts");
                const auto actual = render(full, "-solid");
                std::size_t interior = 0, obscured = 0;
                for (unsigned y = 2; y + 2 < height; ++y) {
                    for (unsigned x = 2; x + 2 < width; ++x) {
                        bool inside = true;
                        for (int dy = -2; dy <= 2; ++dy) {
                            for (int dx = -2; dx <= 2; ++dx) {
                                inside &= expected[std::size_t(int(y) + dy) * width + int(x) + dx] != toca::gpu::background;
                            }
                        }
                        if (inside) {
                            ++interior;
                            obscured += expected[y * width + x] != actual[y * width + x];
                        }
                    }
                }
                expect(interior > 100, "Banner regression must cover visible artwork interiors");
                if (obscured) {
                    std::cerr << prefix << ": " << obscured << '/' << interior << " sponsor pixels obscured\n";
                }
                expect(obscured == 0, "Generated opposing backing must not cover source sponsor artwork");
            }
        }
    }
    std::cout << "PASS: five opposing Silverstone banner pairs retain artwork from both sides at50/80m\n";
}

void scenery_reference(const std::filesystem::path& root, const std::filesystem::path& mega_root, std::span<const std::string_view> names) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    GpuFixture gpu("TOCA Donington scenery batch", 1280, 720);
    auto& renderer = gpu.renderer;
    for (const auto track : {0u, 5u}) {
        const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 3}));
        const auto mega = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 4}), mega_root);
        for (const auto name : names) {
            if (name == "cones") {
                mega_prop_pixels(renderer, original, mega, &SceneResources::cones, name, 1280, 720, true);
            } else if (name == "posts") {
                mega_prop_pixels(renderer, original, mega, &SceneResources::posts, name, 1280, 720, true);
            } else if (name == "signs") {
                mega_prop_pixels(renderer, original, mega, &SceneResources::signs, name, 1280, 720, true);
            } else {
                mega_static_pixels(renderer, original, mega, false, name, 1280, 720);
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (!(argc >= 4)) {
            throw std::runtime_error("Usage: toca-scene-model-prop-tests --banner-reference|--scenery-reference CANONICAL_ROOT MEGA_ROOT [NAME...]");
        }
        if (std::string_view(argv[1]) == "--banner-reference" && argc == 4) {
            banner_reference(argv[2], argv[3]);
        } else if (std::string_view(argv[1]) == "--scenery-reference" && argc >= 5) {
            std::vector<std::string_view> names(argv + 4, argv + argc);
            scenery_reference(argv[2], argv[3], names);
        } else {
            throw std::runtime_error("Expected --banner-reference or --scenery-reference");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
