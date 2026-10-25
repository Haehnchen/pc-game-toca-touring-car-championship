#include "assets/store.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/shadow_support.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "simulation/fixed.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <iostream>

namespace {
using namespace toca::test::scene_models;

void shadow_alpha4_pixels(toca::gpu::Renderer& renderer) {
    using namespace toca::scene;
    // Runtime-only64x64 BMPs exercise SceneView's actual shadow upload. The
    // indexed palette deliberately disagrees with its indices:484bc2 reads
    // SHADAM's bytes, then packs their high nibbles into A4R4G4B4.
    const auto bitmap = [](bool indexed) {
        const auto offset = indexed ? 1078u : 54u;
        const auto pitch = indexed ? 64u : 192u;
        std::vector<std::uint8_t> bytes(offset + pitch * 64);
        const auto put = [&](std::size_t at, std::uint32_t value) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                bytes[at + byte] = std::uint8_t(value >> (byte * 8));
            }
        };
        bytes[0] = 'B';
        bytes[1] = 'M';
        put(2, std::uint32_t(bytes.size()));
        put(10, offset);
        put(14, 40);
        put(18, 64);
        put(22, 64);
        bytes[26] = 1;
        bytes[28] = indexed ? 8 : 24;
        put(34, pitch * 64);
        if (indexed) {
            put(46, 256);
            for (unsigned index = 0; index < 256; ++index) {
                bytes[54 + index * 4] = 255;
                bytes[54 + index * 4 + 2] = std::uint8_t(255 - index);
            }
            for (unsigned y = 0; y < 64; ++y) {
                for (unsigned x = 0; x < 64; ++x) {
                    // All256 indices, plus a dedicated15/16 boundary for
                    // bilinear interpolation after the source conversion.
                    auto alpha = std::uint8_t(y == 63 ? (x < 32 ? 15 : 16) : (y / 4) * 16 + x / 4);
                    if (x == 0 || x == 63) {
                        alpha = x == 0 ? 32 : 208;
                    }
                    if (y == 0) {
                        alpha = 64;
                    } else if (y == 63 && x != 31 && x != 32) {
                        alpha = 160;
                    }
                    bytes[offset + (63 - y) * pitch + x] = alpha;
                }
            }
        }
        return bytes;
    };
    SceneResources probe;
    probe.setup.selection.detail = 3;
    probe.car_shadow_color_bmp = bitmap(false);
    probe.car_shadow_alpha_bmp = bitmap(true);
    SceneView view(renderer, probe);
    const std::array<float, 16> overhead{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 20, 1};
    const CameraInputs camera{overhead, default_projection(3, 640, 480)};
    const auto source = view.frame(camera, {}, {unit, 0, {}, CarShadowFootprint{}}, {}, {});
    expect(source.draws.size() == 1 && source.draws.front().state.texture, "Synthetic shadow alpha fixture lost the production upload");
    expect(source.draws.front().state.clamp && source.draws.front().state.linear, "Original shadow draws must retain bilinear CLAMP addressing");
    toca::gpu::Frame target;
    target.quad({}, 0, 0, 640, 480);
    const auto sample = [&](float x, float y, float u, float v, bool linear) {
        const auto first = target.vertices.size();
        auto state = source.draws.front().state;
        state.linear = linear;
        target.quad(state, x, y, 12, 12);
        for (auto index = first; index < target.vertices.size(); ++index) {
            target.vertices[index].u = u;
            target.vertices[index].v = v;
        }
    };
    for (unsigned index = 0; index < 256; ++index) {
        sample(float(32 + index % 16 * 16), float(32 + index / 16 * 16), (float(index % 16 * 4) + 1.5f) / 64, (float(index / 16 * 4) + 1.5f) / 64, false);
    }
    sample(320, 32, .5f, 63.5f / 64, true);

    struct Edge {
        float u, v;
        unsigned white;
    };

    // Deliberately asymmetric source borders isolate the original extension's
    // U0/V.3,.7 and the core's U1/V endpoints from opposite-edge wrapping.
    constexpr std::array<Edge, 11> edges{
        {{0, .3f, 221}, {0, .7f, 221}, {1, .3f, 34}, {1, .7f, 34}, {.5f, 0, 187}, {.25f, 1, 85}, {.75f, 1, 85}, {0, 0, 187}, {1, 0, 187}, {0, 1, 85}, {1, 1, 85}}};
    for (std::size_t index = 0; index < edges.size(); ++index) {
        sample(320, float(64 + index * 24), edges[index].u, edges[index].v, true);
    }
    expect(renderer.draw(target, false), "Synthetic shadow alpha target was unavailable");
    const auto pixels = renderer.readback();
    // Independent RGBA readback goldens over white for all16 A4 values;
    // every low-nibble value must select the same entry in its source band.
    constexpr std::array<unsigned, 16> white_after_shadow{255, 238, 221, 204, 187, 170, 153, 136, 119, 102, 85, 68, 51, 34, 17, 0};
    for (unsigned index = 0; index < 256; ++index) {
        const auto pixel = pixels[(38 + index / 16 * 16) * 640 + 38 + index % 16 * 16];
        for (const unsigned shift : {0u, 8u, 16u}) {
            expect(((pixel >> shift) & 255) == white_after_shadow[index / 16], "Shadow upload must retain original A4 alpha at every nibble boundary");
        }
    }
    const auto midpoint = pixels[38 * 640 + 326];
    for (const unsigned shift : {0u, 8u, 16u}) {
        const auto channel = (midpoint >> shift) & 255;
        // Converted texels0/17 interpolate to8.5, giving246.5 over white.
        // Either UNORM tie rounding is valid; filtering raw15/16 gives239.5,
        // while quantizing that filtered value would give255 instead.
        expect(channel == 246 || channel == 247, "Shadow A4 conversion must happen before bilinear filtering");
    }
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto pixel = pixels[(70 + index * 24) * 640 + 326];
        for (const unsigned shift : {0u, 8u, 16u}) {
            expect(((pixel >> shift) & 255) == edges[index].white, "Original shadow UV borders must sample their own clamped alpha edge");
        }
    }
}

void shadow_record_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& resources) {
    using namespace toca::scene;
    // Independent authored records isolate498430 from437770's producer.
    SceneResources probe;
    probe.setup.selection.detail = 3;
    probe.car_shadow_color_bmp = resources.car_shadow_color_bmp;
    probe.car_shadow_alpha_bmp = resources.car_shadow_alpha_bmp;
    SceneView view(renderer, probe);
    // A top-down diagnostic camera keeps all record points at viewZ20.
    const std::array<float, 16> overhead{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 20, 1};
    const CameraInputs camera{overhead, default_projection(3, 640, 480)};
    const std::array<ShadowVector, 11> coordinates{
        {{2, 0, 3}, {2, 0, -3}, {-2, 0, -3}, {-2, 0, 3}, {1, 0, -4}, {-1, 0, -5}, {-3, 0, -5}, {-5, 0, -3}, {-6, 0, -1}, {-6, 0, 1}, {-4, 0, 3}}};
    // 98130's exact original triangle lists, including the two core triangles.
    constexpr std::array<std::array<std::uint8_t, 3>, 8> one_side{{{0, 1, 3}, {1, 2, 3}, {5, 10, 4}, {6, 10, 5}, {7, 10, 6}, {8, 10, 7}, {9, 10, 8}, {4, 10, 9}}};
    constexpr std::array<std::array<std::uint8_t, 3>, 12> two_sides{
        {{0, 1, 3}, {1, 2, 3}, {5, 14, 4}, {6, 14, 5}, {7, 14, 6}, {8, 14, 7}, {9, 14, 8}, {10, 14, 9}, {11, 14, 10}, {12, 14, 11}, {13, 14, 12}, {4, 14, 13}}};
    constexpr std::array<std::array<std::uint8_t, 3>, 2> rectangle{{{1, 2, 0}, {0, 2, 3}}};
    std::array<std::vector<std::uint32_t>, 3> images;
    for (std::uint32_t type = 0; type < 3; ++type) {
        CarShadowFootprint shadow;
        shadow.type = CarShadowType(type);
        for (std::size_t point = 0; point < shadow.points.size(); ++point) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                shadow.points[point][axis] = coordinates[point][axis] * 65536;
            }
        }
        if (type == 1) {
            shadow.points[4] = {0, 0, -5 * 65536};
            shadow.points[5] = {-3 * 65536, 0, -5 * 65536};
            shadow.points[6] = {-5 * 65536, 0, -65536};
        }
        // Original prelit-vertex identities and UVs from498430.
        const std::array<std::uint8_t, 15> record_points =
            type == 1 ? std::array<std::uint8_t, 15>{0, 1, 2, 3, 0, 1, 4, 5, 6, 3, 11} : std::array<std::uint8_t, 15>{0, 1, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 3, 11};
        const auto triangles = type == 0   ? std::span<const std::array<std::uint8_t, 3>>(rectangle)
                               : type == 1 ? std::span<const std::array<std::uint8_t, 3>>(one_side)
                                           : std::span<const std::array<std::uint8_t, 3>>(two_sides);
        const auto frame = view.frame(camera, {}, {unit, 0, {}, shadow}, {}, {});
        expect(frame.draws.size() == 1 && frame.dynamic_world_vertices.size() == triangles.size() * 3 && frame.vertices.empty() && frame.indices.empty(),
            "Original shadow type lost merged prelit core/extension triangles or retained CPU projection");
        const auto& draw = frame.draws.front();
        expect(draw.dynamic_world && !draw.mesh && draw.first == 0 && draw.count == triangles.size() * 6 && draw.state.texture && draw.state.blend && draw.state.linear
                   && !draw.state.depth && !draw.state.key,
            "Extended shadow must preserve original alpha texture, blending and disabled depth test/write state");
        constexpr std::array<std::array<float, 2>, 4> core_uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (std::size_t triangle = 0; triangle < triangles.size(); ++triangle) {
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto original_vertex = triangles[triangle][corner];
                const auto point = record_points[original_vertex];
                // Midpoint of record points0 and5, independently fixed for this fixture.
                const auto position = point < 11 ? shadow.points[point] : type == 1 ? ShadowVector{-32768, 0, -65536} : ShadowVector{32768, 0, -65536};
                const auto& vertex = frame.dynamic_world_vertices[triangle * 3 + corner];
                const std::array<float, 3> expected_position{float(position[0]) / 65536, float(position[1]) / 65536, float(position[2]) / 65536};
                const auto uv = original_vertex < 4 ? core_uv[original_vertex] : point == 11 ? std::array{.5f, .5f} : std::array{0.f, original_vertex % 2 == 0 ? .7f : .3f};
                expect(vertex.position == expected_position && vertex.u == uv[0] && vertex.v == uv[1] && vertex.color == (original_vertex < 4 ? 0xc8000000u : 0xaa000000u),
                    "Shadow draw differs from original record ordering, midpoint, UV seam or core/extension alpha");
            }
        }
        toca::gpu::Frame target;
        target.quad({}, 0, 0, 640, 480, 0xffc0c0c0);
        target.dynamic_world_vertices = frame.dynamic_world_vertices;
        target.world_views = frame.world_views;
        target.draws.insert(target.draws.end(), frame.draws.begin(), frame.draws.end());
        expect(renderer.draw(target, false), "Original extended-shadow fixture target was unavailable");
        images[type] = renderer.readback();
        renderer.save_screenshot("shadow-record-type" + std::to_string(type) + ".png");
        expect(renderer.draw(toca::test::materialize_world(target), false), "CPU extended-shadow reference target was unavailable");
        expect(images[type] == renderer.readback(), "Original shadow pixels must exactly equal CPU clipping/projection for every source type");
        expect(std::ranges::count_if(images[type],
                   [](auto pixel) {
                       return (pixel & 255) < 160;
                   })
                   > 1000,
            "Original shadow type did not produce textured alpha coverage in GPU readback");
    }
    for (std::size_t type = 1; type < images.size(); ++type) {
        std::size_t extended = 0;
        for (std::size_t pixel = 0; pixel < images[type].size(); ++pixel) {
            extended += (images[type][pixel] & 255) + 8 < (images[0][pixel] & 255);
        }
        expect(extended > 1000, "Directional shadow extensions must cover actual pixels beyond the original grounded core");
    }
}

void generated_shadow_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& resources) {
    using namespace toca::scene;
    const toca::HpcSurface surface(resources.collision_hpc);
    const auto initial = initial_body_pose(surface, single_qualifying_start(resources.route_points), 0);
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = initial.position[0] + 6 * 65536;
    cameras[0].transform[13] = initial.position[1] + 4 * 65536;
    cameras[0].transform[14] = initial.position[2] - 6 * 65536;
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view(initial.position), default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    HorizonActors actors(resources.moving_horizon);
    constexpr std::array<const char*, 4> names{"rain", "sunny", "airborne", "rolled"};
    constexpr std::array<CarShadowType, 4> types{CarShadowType::rectangle, CarShadowType::two_sides, CarShadowType::two_sides, CarShadowType::one_side};
    std::array<CarShadowFootprint, 4> footprints;
    for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
        auto body = initial;
        auto setup = resources.setup;
        setup.weather = scenario == 0 ? 3 : 1;
        if (scenario >= 2) {
            body.position[1] += 2 * 65536;
        }
        if (scenario == 3) {
            // Roll ninety degrees around the retained body forward axis.
            body.basis[0] = initial.basis[1];
            body.basis[1] = toca::simulation::fixed::scale(initial.basis[0], -65536);
        }
        refresh_body_contacts(body, 0);
        for (std::size_t point = 0; point < body.contacts.size(); ++point) {
            const auto sample = surface.sample(body.contacts[point][0], body.contacts[point][2]);
            expect(bool(sample), "Generated shadow probe left canonical HPC coverage");
            body.surfaces[point] = *sample;
        }
        const auto input = car_shadow_input(body, setup);
        footprints[scenario] = car_shadow_footprint(input);
        expect(footprints[scenario].type == types[scenario], "Actual grounded, airborne or rolled actor did not select its original shadow drawing type");
        const auto light = scene_lighting(setup);
        const std::vector<std::uint8_t> flags(resources.flags.model.nodes.size());
        CarPose car{body.object_to_world};
        expect(renderer.draw(view.frame(camera, light, car, flags, actors.frame()), false), "Canonical shadow baseline target was unavailable");
        const auto before = renderer.readback();
        car.shadow = footprints[scenario];
        const auto frame = view.frame(camera, light, car, flags, actors.frame());
        expect(renderer.draw(frame, false), "Canonical generated shadow target was unavailable");
        const auto after = renderer.readback();
        renderer.save_screenshot(std::string("shadow-generated-") + names[scenario] + ".png");
        expect(renderer.draw(toca::test::materialize_world(frame), false), "CPU generated-shadow reference target was unavailable");
        expect(after == renderer.readback(), "Production grounded, directional, airborne and rolled shadow pixels must exactly equal CPU clipping/projection");
        std::size_t darkened = 0;
        for (std::size_t pixel = 0; pixel < after.size(); ++pixel) {
            darkened += before[pixel] != after[pixel];
            for (const unsigned channel : {0u, 8u, 16u}) {
                expect(((after[pixel] >> channel) & 255) <= ((before[pixel] >> channel) & 255),
                    "Original generated alpha-black shadow must not brighten terrain or replace car geometry");
            }
        }
        expect(darkened > 40, "Generated grounded, directional or airborne shadow did not darken actual GPU terrain pixels");
        std::size_t shadow_triangles = 0;
        for (const auto& draw : frame.draws) {
            if (draw.dynamic_world && draw.state.blend && !draw.state.depth && !draw.state.key) {
                expect(draw.count % 6 == 0, "Generated shadow draw lost its source triangle boundaries");
                shadow_triangles += draw.count / 6;
            }
        }
        expect(shadow_triangles == (scenario == 0 ? 2u : scenario == 3 ? 8u : 12u), "Generated sunlight/airborne silhouette extensions did not reach the shared scene frame");
    }
    expect(footprints[0].points != footprints[1].points && footprints[1].points != footprints[3].points,
        "Weather and body orientation must alter source shadow geometry independently of the presentation camera");
}

void shadow_reference(const std::filesystem::path& root) {
    using namespace toca::scene;
    GpuFixture gpu("TOCA original extended shadows", 640, 480);
    auto& renderer = gpu.renderer;
    const toca::AssetStore store(root);
    const auto resources = load_scene_resources(store, resolve_setup({}));
    shadow_alpha4_pixels(renderer);
    shadow_record_pixels(renderer, resources);
    generated_shadow_pixels(renderer, resources);
    std::cout << "PASS: original type0/1/2 shadow topology, record positions, UV seams, alpha and GPU extension coverage\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (!(argc == 2)) {
            throw std::runtime_error("Usage: toca-scene-model-shadow-tests CANONICAL_ROOT");
        }
        shadow_reference(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
