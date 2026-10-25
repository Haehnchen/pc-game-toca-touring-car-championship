#include "assets/store.hpp"
#include "core/contracts.hpp"
#include "scene/mega_ground_edges.hpp"
#include "scene/mega_ground_uv.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace toca;
using namespace toca::scene;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Focus {
    std::array<float, 3> center;
    float span;
};

Focus focus(const Terrain& terrain, std::string_view material, std::optional<std::pair<std::size_t, std::size_t>> selected_face = {}) {
    double best = 0;
    Focus selected{};
    for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
        const auto& chunk = terrain.chunks[chunk_index];
        for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
            const auto& face = chunk.mesh.faces[face_index];
            if (selected_face && *selected_face != std::pair{chunk_index, face_index}) {
                continue;
            }
            if (face.texture < 0 || terrain.textures.at(std::size_t(face.texture)).name != material) {
                continue;
            }
            const auto& a = chunk.mesh.vertices.at(face.indices[0]).position;
            const auto& b = chunk.mesh.vertices.at(face.indices[1]).position;
            const auto& c = chunk.mesh.vertices.at(face.indices[2]).position;
            const auto area = std::abs(double(b[0] - a[0]) * (c[2] - a[2]) - double(b[2] - a[2]) * (c[0] - a[0])) / 2;
            if (area <= best) {
                continue;
            }
            best = area;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                selected.center[axis] = (a[axis] + b[axis] + c[axis]) / 3;
            }
            const auto length = [](const auto& first, const auto& second) {
                return std::hypot(first[0] - second[0], first[2] - second[2]);
            };
            selected.span = std::max({length(a, b), length(b, c), length(c, a)});
        }
    }
    expect(best > 0, "Refinement camera requires a real ground triangle");
    return selected;
}

struct Capture {
    gpu::Frame frame;
    gpu::Frame projected;
    std::vector<Uint32> pixels;
    gpu::FrameTiming timing;
};

Capture capture(gpu::Renderer& renderer, const TerrainView& terrain, const CameraInputs& camera, const Lighting& lighting, const char* filename) {
    auto frame = terrain.frame(camera, lighting);
    frame.multisample = true;
    expect(renderer.draw_offscreen(frame, {1280, 720}), "Refinement target unavailable");
    renderer.wait_idle();
    const auto timing = renderer.last_frame_timing();
    auto pixels = renderer.readback();
    renderer.save_screenshot(filename);
    auto projected = test::materialize_world(frame);
    return {std::move(frame), std::move(projected), std::move(pixels), timing};
}

struct GroundLayers {
    std::size_t opaque = 0, gravel = 0, gravel_faces = 0;
};

// Resident chunks submit source geometry before camera clipping. Audit their
// semantic layers instead of imposing a limit on camera-dependent draw totals:
// each source face keeps one opaque triangle and batches its cached gravel.
GroundLayers expected_layers(const Terrain& terrain, std::uint32_t track) {
    GroundLayers expected;
    const auto edges = prepare_mega_ground_edges(terrain, track);
    const auto direct = prepare_mega_ground_direct_edges(terrain, track);
    const auto profile = mega_ground_transition_profile(track);
    const auto direct_profile = mega_ground_direct_profile(track);
    const auto grass_name = profile ? profile->grass : direct_profile ? direct_profile->grass : std::string_view{};
    const auto gravel_name = profile ? profile->gravel : direct_profile ? direct_profile->gravel : std::string_view{};
    const auto authored = [&](std::string_view name) {
        return std::ranges::any_of(terrain.textures, [&](const auto& texture) {
            return texture.name == name && texture.encoding == ImageEncoding::webp;
        });
    };
    const auto grass_mapping = mega_ground_uv_mapping(authored(grass_name), track, grass_name);
    const auto gravel_mapping = mega_ground_uv_mapping(authored(gravel_name), track, gravel_name);
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        const auto& mesh = terrain.chunks[chunk].mesh;
        for (std::size_t face_index = 0; face_index < mesh.faces.size(); ++face_index) {
            const auto& face = mesh.faces[face_index];
            ++expected.opaque;
            if (profile && grass_mapping && gravel_mapping && face.texture >= 0 && is_mega_ground_transition(true, track, terrain.textures.at(std::size_t(face.texture)))) {
                const std::array source{mesh.vertices.at(face.indices[0]), mesh.vertices.at(face.indices[1]), mesh.vertices.at(face.indices[2])};
                const std::array positions{source[0].position, source[1].position, source[2].position};
                if (grass_mapping->supports_triangle(positions)) {
                    const bool refined = !edges.faces.empty() && edges.faces[chunk][face_index];
                    const auto caps = edges.caps.empty() ? std::span<const MegaGroundTransitionCap>{} : std::span<const MegaGroundTransitionCap>(edges.caps[chunk][face_index]);
                    if (const auto transition = prepare_mega_ground_transition(track, source, refined ? profile->feather_half_width : .05f, caps)) {
                        expected.gravel += transition->gravel.size();
                        expected.gravel_faces += !transition->gravel.empty();
                        continue;
                    }
                }
            }
            if (gravel_mapping && !direct.gravel.empty()) {
                const auto& band = direct.gravel[chunk][face_index];
                expected.gravel += band.size();
                expected.gravel_faces += !band.empty();
            }
            if (!edges.bridge_indices.empty()) {
                for (const auto bridge : edges.bridge_indices[chunk][face_index]) {
                    expected.opaque += edges.bridges[bridge].triangles.size();
                }
            }
        }
    }
    return expected;
}

void check_layers(const Capture& capture, const GroundLayers& expected) {
    GroundLayers actual;
    const gpu::Draw* previous = nullptr;
    for (const auto& draw : capture.frame.draws) {
        expect(draw.mesh && draw.first % 6 == 0 && draw.count > 0 && draw.count % 6 == 0 && draw.vertex_offset == 0 && draw.state.depth && !draw.overlay,
            "Ground layers must retain complete resident source triangles");
        if (previous && previous->mesh == draw.mesh) {
            expect(previous->first + previous->count == draw.first && previous->state != draw.state,
                "Consecutive equal-state ground triangles must share one resident batch without changing source order");
        }
        if (draw.state.blend) {
            expect(!draw.state.depth_write && draw.state.depth_bias && draw.state.linear, "Gravel layers must blend coplanarly after their opaque source faces");
            actual.gravel += draw.count / 6;
            ++actual.gravel_faces;
        } else {
            actual.opaque += draw.count / 6;
        }
        previous = &draw;
    }
    expect(actual.opaque == expected.opaque && actual.gravel == expected.gravel,
        "Resident ground geometry must contain exactly the source faces and their cached transition, boundary and gap triangles");
    expect(actual.gravel_faces == expected.gravel_faces, "Each source face's complete gravel layer must share one draw without reordering neighboring faces");
    expect(capture.timing.draw_calls == capture.frame.draws.size(), "Every ordered resident ground batch must reach the renderer");
}

void edge_surfaces(gpu::Renderer& renderer, const AssetStore& store, const std::filesystem::path& mega_root, std::uint32_t track, bool direct = false) {
    const auto resources = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = graphics_mega_detail}), mega_root);
    TerrainView terrain(renderer, resources.ground, true, track);
    const auto edges = prepare_mega_ground_edges(resources.ground, track);
    std::size_t patch_faces = 0;
    std::size_t normal_copies = 0;
    std::set<std::array<float, 3>> normal_positions;
    for (std::size_t chunk = 0; chunk < edges.faces.size(); ++chunk) {
        patch_faces += std::ranges::count(edges.faces[chunk], true);
        for (std::size_t vertex = 0; vertex < edges.normals[chunk].size(); ++vertex) {
            if (edges.normals[chunk][vertex]) {
                ++normal_copies;
                normal_positions.insert(resources.ground.chunks[chunk].mesh.vertices[vertex].position);
            }
        }
    }

    const auto direct_edges = direct || track == 3 ? prepare_mega_ground_direct_edges(resources.ground, track) : MegaGroundDirectEdges{};
    std::size_t band_faces = 0, band_triangles = 0, direct_normal_copies = 0;
    std::set<std::array<float, 3>> direct_normal_positions;
    for (std::size_t chunk = 0; chunk < direct_edges.gravel.size(); ++chunk) {
        for (const auto& face : direct_edges.gravel[chunk]) {
            band_faces += !face.empty();
            band_triangles += face.size();
        }
        for (std::size_t vertex = 0; vertex < direct_edges.normals[chunk].size(); ++vertex) {
            if (direct_edges.normals[chunk][vertex]) {
                ++direct_normal_copies;
                direct_normal_positions.insert(resources.ground.chunks[chunk].mesh.vertices[vertex].position);
            }
        }
    }
    if (direct) {
        expect(direct_edges.edges.size() == (track == 1 ? 9 : 19), "Direct edges must match the independently audited canonical boundary count");
        expect(direct_normal_positions.size() == 14 && direct_normal_copies == (track == 1 ? 48 : 90),
            "Direct edge lighting must match independently audited safe source-normal copies");
        expect(band_faces >= direct_edges.edges.size(), "Direct bands must cover their source boundaries");
    }

    struct Audit {
        std::uint32_t track;
        std::size_t faces, positions, copies;
    };

    constexpr std::array audits{
        Audit{0, 196, 177, 1015}, Audit{1, 162, 93, 533}, Audit{3, 70, 77, 431}, Audit{4, 46, 39, 238}, Audit{5, 196, 49, 281}, Audit{7, 102, 102, 545}, Audit{8, 124, 72, 421}};
    const auto expected = std::ranges::find(audits, track, &Audit::track);
    if (!direct) {
        expect(expected != audits.end(), "Edge capture requires an audited track");
        std::cout << "AUDIT: track" << track << " mixed=" << patch_faces << " normal_positions=" << normal_positions.size() << " normal_copies=" << normal_copies << '\n';
        expect(patch_faces == expected->faces && normal_positions.size() == expected->positions && normal_copies == expected->copies,
            "Ground edges must match the independently audited canonical faces and safe normal positions");
    }
    std::size_t gap_triangles = 0;
    for (const auto& bridge : edges.bridges) {
        gap_triangles += bridge.triangles.size();
    }
    if (track == 3) {
        expect(edges.bridges.size() == 2 && gap_triangles > 0, "The audited Brands source gap must receive two render-only bridges");
    } else {
        expect(edges.bridges.empty(), "Ground bridge refinement must remain confined to the accepted neutral cap profile");
    }
    std::size_t cap_faces = 0, mixed_overlay_triangles = 0;
    std::set<std::pair<std::array<std::array<float, 3>, 2>, bool>> cap_edges;
    if (track == 3) {
        expect(direct_edges.edges.size() == 2 && direct_normal_positions.size() == 2 && direct_normal_copies == 8,
            "Brands direct grass/sand edges and exact normal copies must match the independent canonical audit");
    }
    if (!direct) {
        const auto profile = mega_ground_transition_profile(track);
        expect(profile.has_value(), "Mixed capture requires an accepted material profile");
        for (std::size_t chunk_index = 0; chunk_index < resources.ground.chunks.size(); ++chunk_index) {
            const auto& mesh = resources.ground.chunks[chunk_index].mesh;
            for (std::size_t face_index = 0; face_index < mesh.faces.size(); ++face_index) {
                if (!edges.faces[chunk_index][face_index]) {
                    continue;
                }
                const auto caps = edges.caps.empty() ? std::span<const MegaGroundTransitionCap>{} : std::span<const MegaGroundTransitionCap>(edges.caps[chunk_index][face_index]);
                cap_faces += !caps.empty();
                for (const auto& cap : caps) {
                    cap_edges.emplace(cap.edge.endpoints, cap.gravel);
                }
                const auto& face = mesh.faces[face_index];
                const std::array source{mesh.vertices.at(face.indices[0]), mesh.vertices.at(face.indices[1]), mesh.vertices.at(face.indices[2])};
                const auto transition = prepare_mega_ground_transition(track, source, profile->feather_half_width, caps);
                expect(transition.has_value(), "Every eligible mixed face must cache its source material transition");
                mixed_overlay_triangles += transition->gravel.size();
            }
        }
        expect(track == 3 ? cap_edges.size() == 11 && cap_faces >= 11 : cap_edges.empty(), "Only the eleven source-conflicting Brands caps require bounded material collars");
    }
    // GP's second large gravel area lies outside the accepted 18-face pilot.
    auto target =
        direct ? focus(resources.ground, track == 1 ? "GRASS2" : "NUGRASS", track == 1 ? std::pair<std::size_t, std::size_t>{103, 12} : std::pair<std::size_t, std::size_t>{59, 0})
               : focus(resources.ground,
                     track == 1   ? "GG"
                     : track == 3 ? "NGG"
                     : track == 7 ? "g&g"
                     : track == 8 ? "G&G2"
                                  : "G&G",
                     track == 0 ? std::optional(std::pair<std::size_t, std::size_t>{107, 37}) : std::nullopt);
    glm::vec3 tangent{}, inward{};
    if (direct) {
        const auto& mesh = resources.ground.chunks.at(track == 1 ? 103 : 59).mesh;
        const auto& face = mesh.faces.at(track == 1 ? 12 : 0);
        const auto a = glm::make_vec3(mesh.vertices.at(face.indices[track == 1 ? 0 : 1]).position.data());
        const auto b = glm::make_vec3(mesh.vertices.at(face.indices[track == 1 ? 1 : 2]).position.data());
        const auto c = glm::make_vec3(mesh.vertices.at(face.indices[track == 1 ? 2 : 0]).position.data());
        const auto midpoint = (a + b) * .5f;
        std::copy_n(glm::value_ptr(midpoint), 3, target.center.begin());
        target.span = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
        tangent = (b - a) / target.span;
        inward = glm::normalize(glm::vec3(-tangent.z, 0, tangent.x));
        if (glm::dot(inward, c - midpoint) < 0) {
            inward = -inward;
        }
    }
    const auto center = glm::make_vec3(target.center.data());
    const auto lighting = scene_lighting(resources.setup);

    struct View {
        const char* name;
        glm::vec3 offset;
        glm::vec3 target_offset{};
    };

    const auto distance = std::max(8.f, target.span * .85f);
    std::vector views{View{"overview", {0, distance, -distance}}, View{"close", {0, distance * .3f, -distance * .3f}}, View{"low", {0, distance * .12f, -distance * .65f}}};
    if (direct) {
        const auto close = inward * 2.5f + tangent * .7f + glm::vec3(0, 2.2f, 0);
        views = {{"overview", inward * distance + glm::vec3(0, distance, 0)}, {"close", close}, {"low", inward * 9.f + glm::vec3(0, 1.8f, 0)},
            {"end-a", close, -tangent * target.span * .5f}, {"end-b", close, tangent * target.span * .5f}};
        if (track == 1) {
            const auto& mesh = resources.ground.chunks.at(138).mesh;
            const auto& face = mesh.faces.at(11);
            const auto a = glm::make_vec3(mesh.vertices.at(face.indices[0]).position.data());
            const auto b = glm::make_vec3(mesh.vertices.at(face.indices[1]).position.data());
            views.push_back({"narrow", {2.5f, 2.2f, .7f}, (a + b) * .5f - center});
        }
        if (track == 2) {
            struct BoundaryFocus {
                const char* name;
                std::size_t chunk, face, a, b;
            };

            constexpr std::array additional{
                BoundaryFocus{"chain-2", 84, 5, 2, 0}, BoundaryFocus{"chain-3", 82, 2, 0, 2}, BoundaryFocus{"chain-4", 107, 6, 0, 1}, BoundaryFocus{"narrow", 106, 0, 1, 0}};
            for (const auto& selected : additional) {
                const auto& mesh = resources.ground.chunks.at(selected.chunk).mesh;
                const auto& face = mesh.faces.at(selected.face);
                const auto a = glm::make_vec3(mesh.vertices.at(face.indices[selected.a]).position.data());
                const auto b = glm::make_vec3(mesh.vertices.at(face.indices[selected.b]).position.data());
                const auto c = glm::make_vec3(mesh.vertices.at(face.indices[3 - selected.a - selected.b]).position.data());
                const auto midpoint = (a + b) * .5f;
                const auto along = glm::normalize(b - a);
                auto inside = glm::normalize(glm::vec3(-along.z, 0, along.x));
                if (glm::dot(inside, c - midpoint) < 0) {
                    inside = -inside;
                }
                views.push_back({selected.name, inside * 2.5f + along * .7f + glm::vec3(0, 2.2f, 0), midpoint - center});
            }
        }
    }
    if (track == 3) {
        struct BoundaryFocus {
            const char* name;
            std::size_t chunk, face, a, b;
        };

        constexpr std::array boundaries{BoundaryFocus{"cap-66-61", 66, 61, 2, 0}, BoundaryFocus{"cap-73-73", 73, 73, 2, 0}, BoundaryFocus{"cap-73-74", 73, 74, 2, 0},
            BoundaryFocus{"cap-73-75", 73, 75, 2, 0}, BoundaryFocus{"cap-109-56", 109, 56, 2, 0}, BoundaryFocus{"cap-112-21", 112, 21, 2, 0},
            BoundaryFocus{"cap-117-104", 117, 104, 2, 0}, BoundaryFocus{"cap-118-129", 118, 129, 2, 0}, BoundaryFocus{"cap-126-85", 126, 85, 2, 0},
            BoundaryFocus{"cap-141-17", 141, 17, 2, 0}, BoundaryFocus{"cap-112-20", 112, 20, 2, 0}, BoundaryFocus{"paint-42-52", 42, 52, 2, 0},
            BoundaryFocus{"direct-2", 69, 2, 0, 1}, BoundaryFocus{"direct-4", 69, 4, 2, 0}};
        for (const auto& selected : boundaries) {
            const auto& mesh = resources.ground.chunks.at(selected.chunk).mesh;
            const auto& face = mesh.faces.at(selected.face);
            const auto a = glm::make_vec3(mesh.vertices.at(face.indices[selected.a]).position.data());
            const auto b = glm::make_vec3(mesh.vertices.at(face.indices[selected.b]).position.data());
            const auto c = glm::make_vec3(mesh.vertices.at(face.indices[3 - selected.a - selected.b]).position.data());
            const auto midpoint = (a + b) * .5f;
            const auto along = glm::normalize(b - a);
            auto inside = glm::normalize(glm::vec3(-along.z, 0, along.x));
            if (glm::dot(inside, c - midpoint) < 0) {
                inside = -inside;
            }
            views.push_back({selected.name, inside * 2.5f + along * .7f + glm::vec3(0, 2.2f, 0), midpoint - center});
        }
    }
    if (track == 7 || track == 8) {
        struct BoundaryFocus {
            const char* name;
            std::size_t chunk, face, a, b;
        };

        constexpr std::array knockhill{BoundaryFocus{"paint-84-83", 84, 83, 0, 1}, BoundaryFocus{"slope-103-90", 103, 90, 0, 1}};
        constexpr std::array snetterton{BoundaryFocus{"kerb-70-54", 70, 54, 0, 1}, BoundaryFocus{"paint-249-64", 249, 64, 0, 1}};
        const auto& boundaries = track == 7 ? knockhill : snetterton;
        for (const auto& selected : boundaries) {
            const auto& mesh = resources.ground.chunks.at(selected.chunk).mesh;
            const auto& face = mesh.faces.at(selected.face);
            const auto a = glm::make_vec3(mesh.vertices.at(face.indices[selected.a]).position.data());
            const auto b = glm::make_vec3(mesh.vertices.at(face.indices[selected.b]).position.data());
            const auto c = glm::make_vec3(mesh.vertices.at(face.indices[3 - selected.a - selected.b]).position.data());
            const auto midpoint = (a + b) * .5f;
            const auto along = glm::normalize(b - a);
            auto inside = glm::normalize(glm::vec3(-along.z, 0, along.x));
            if (glm::dot(inside, c - midpoint) < 0) {
                inside = -inside;
            }
            views.push_back({selected.name, inside * 2.5f + along * .7f + glm::vec3(0, 2.2f, 0), midpoint - center});
        }
    }
    const auto prefix = std::string(direct ? "ground-direct-track" : "ground-edge-track") + std::to_string(track);
    std::ofstream metrics(prefix + "-metrics.json");
    metrics << "{\n  \"track_id\": " << track << ", \"patch_faces\": " << patch_faces << ", \"normal_positions\": " << normal_positions.size()
            << ", \"normal_copies\": " << normal_copies << ", \"direct_edges\": " << direct_edges.edges.size() << ", \"band_faces\": " << band_faces
            << ", \"band_triangles\": " << band_triangles << ", \"direct_normal_positions\": " << direct_normal_positions.size()
            << ", \"direct_normal_copies\": " << direct_normal_copies << ", \"cap_edges\": " << cap_edges.size() << ", \"cap_faces\": " << cap_faces
            << ", \"mixed_overlay_triangles\": " << mixed_overlay_triangles << ", \"gap_bridges\": " << edges.bridges.size() << ", \"gap_triangles\": " << gap_triangles
            << ",\n  \"views\": [\n";
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        CameraInputs camera{{}, default_projection(graphics_mega_detail, 1280, 720)};
        const auto camera_matrix = glm::lookAtLH(center + view.target_offset + view.offset, center + view.target_offset, glm::vec3(0, 1, 0));
        std::copy_n(glm::value_ptr(camera_matrix), 16, camera.world_to_view.begin());
        auto result = capture(renderer, terrain, camera, lighting, (prefix + "-" + view.name + ".png").c_str());
        expect(result.timing.mipmapped_draw_calls > 0 && result.timing.anisotropic_draw_calls == result.timing.mipmapped_draw_calls,
            "Both mixed and plain opaque ground must retain the same AF sampling");
        metrics << "    {\"name\": \"" << view.name << "\", \"vertices\": " << result.projected.vertices.size() << ", \"triangles\": " << result.projected.indices.size() / 3
                << ", \"gpu_draws\": " << result.timing.draw_calls << ", \"samples\": " << result.timing.samples
                << ", \"anisotropic_draws\": " << result.timing.anisotropic_draw_calls << "}" << (index + 1 == views.size() ? "\n" : ",\n");
    }
    metrics << "  ]\n}\n";
    expect(bool(metrics), "Edge metrics must be written");
    std::cout << "PASS: track" << track << " refined_faces=" << patch_faces << " shared_normal_positions=" << normal_positions.size() << " copies=" << normal_copies << '\n';
}

struct BoundaryView {
    const char* name;
    glm::vec3 eye, target;
};

BoundaryView boundary_view(const Terrain& terrain, const char* name, std::size_t chunk, std::size_t face, std::size_t first, std::size_t second, float inward_distance,
    float height, float along_distance = .7f) {
    const auto& mesh = terrain.chunks.at(chunk).mesh;
    const auto& triangle = mesh.faces.at(face);
    const auto a = glm::make_vec3(mesh.vertices.at(triangle.indices[first]).position.data());
    const auto b = glm::make_vec3(mesh.vertices.at(triangle.indices[second]).position.data());
    const auto c = glm::make_vec3(mesh.vertices.at(triangle.indices[3 - first - second]).position.data());
    const auto target = (a + b) * .5f;
    const auto along = glm::normalize(b - a);
    auto inside = glm::normalize(glm::vec3(-along.z, 0, along.x));
    if (glm::dot(inside, c - target) < 0) {
        inside = -inside;
    }
    return {name, target + inside * inward_distance + along * along_distance + glm::vec3(0, height, 0), target};
}

void inspect_boundaries(gpu::Renderer& renderer, const AssetStore& store, const std::filesystem::path& mega_root, std::uint32_t track) {
    expect(track == 6 || track == 9, "Boundary inspection requires a source-audited special track");
    const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 3}));
    const auto mega = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = graphics_mega_detail}), mega_root);
    // Croft compares source artwork at the same diagnostic filtering. Lavaland
    // compares existing Mega with the new source-gap cache disabled/enabled.
    TerrainView source(renderer, track == 9 ? mega.ground : original.ground, true), accepted(renderer, mega.ground, true, track);
    const auto edges = prepare_mega_ground_edges(mega.ground, track);
    std::size_t gap_triangles = 0;
    for (const auto& bridge : edges.bridges) {
        gap_triangles += bridge.triangles.size();
    }
    expect(edges.faces.empty() && prepare_mega_ground_direct_edges(mega.ground, track).edges.empty(),
        "A special track without neutral grass/gravel strips must retain its authored material boundaries");
    if (track == 9) {
        expect(edges.bridges.size() == 22 && gap_triangles > 0 && edges.normals.empty() && edges.caps.empty(),
            "Lavaland must close only the22 audited coarse source gaps, without normal smoothing or material feathering");
    }
    const auto lighting = scene_lighting(mega.setup);
    std::vector<BoundaryView> views;
    if (track == 6) {
        views = {boundary_view(mega.ground, "overview", 112, 2, 0, 1, 16.f, 16.f), boundary_view(mega.ground, "close", 112, 2, 0, 1, 2.5f, 2.2f),
            boundary_view(mega.ground, "low", 112, 2, 0, 1, 9.f, 1.8f), boundary_view(mega.ground, "paint", 112, 5, 2, 0, 2.5f, 2.2f),
            boundary_view(mega.ground, "pit-end", 112, 2, 0, 1, 2.5f, 2.2f, 8.0373549f)};
    }
    if (track == 9) {
        views = {boundary_view(mega.ground, "overview", 52, 105, 2, 0, 12.f, 9.f), boundary_view(mega.ground, "road-close", 48, 125, 2, 0, 2.5f, 2.2f),
            boundary_view(mega.ground, "road-height", 48, 125, 2, 0, 5.f, 1.2f), boundary_view(mega.ground, "lava-wall", 43, 16, 1, 2, 4.f, 2.f),
            boundary_view(mega.ground, "gap-39-50", 39, 50, 2, 0, 1.5f, 1.5f), boundary_view(mega.ground, "gap-39-50-low", 39, 50, 2, 0, 2.5f, 1.2f),
            boundary_view(mega.ground, "gap-52-97", 52, 97, 2, 0, 1.5f, 1.5f), boundary_view(mega.ground, "lava-gap", 86, 9, 2, 0, 2.5f, 1.5f)};
        views[3].eye = views[3].target + glm::vec3(-.9817286f, 0, -.1840286f) * 4.f + glm::vec3(0, 2.f, 0);
    }
    const auto prefix = std::string(track == 9 ? "ground-gap-track" : "ground-inspect-track") + std::to_string(track);
    if (track == 9) {
        std::filesystem::create_directory("before");
        std::filesystem::create_directory("after");
    }
    std::array<std::ofstream, 2> phase_metrics;
    if (track == 9) {
        for (unsigned phase = 0; phase < 2; ++phase) {
            phase_metrics[phase].open(std::string(phase == 0 ? "before/" : "after/") + prefix + "-metrics.json");
            phase_metrics[phase] << "{\n  \"track_id\": " << track << ", \"gap_bridges\": " << (phase == 0 ? 0 : edges.bridges.size())
                                 << ", \"gap_triangles\": " << (phase == 0 ? 0 : gap_triangles) << ",\n  \"views\": [\n";
        }
    }
    std::ofstream metrics(prefix + "-metrics.json");
    metrics << "{\n  \"track_id\": " << track << ", \"comparison\": \""
            << (track == 9 ? "existing Mega without/with source-gap repair" : "source artwork versus existing Mega; no additional boundary geometry") << "\",\n  \"views\": [\n";
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        CameraInputs camera{{}, default_projection(graphics_mega_detail, 1280, 720)};
        const auto matrix = glm::lookAtLH(view.eye, view.target, glm::vec3(0, 1, 0));
        std::copy_n(glm::value_ptr(matrix), 16, camera.world_to_view.begin());
        const auto before_name = track == 9 ? "before/" + prefix + "-" + view.name + ".png" : prefix + "-source-" + view.name + ".png";
        const auto after_name = track == 9 ? "after/" + prefix + "-" + view.name + ".png" : prefix + "-mega-" + view.name + ".png";
        auto before = capture(renderer, source, camera, lighting, before_name.c_str());
        auto after = capture(renderer, accepted, camera, lighting, after_name.c_str());
        expect(before.timing.samples == after.timing.samples && (after.timing.samples == 1 || after.timing.samples == 2),
            "Boundary comparisons must use the same supported Mega sample count");
        if (track == 6) {
            expect(before.projected.vertices.size() == after.projected.vertices.size() && before.projected.indices == after.projected.indices,
                "Artwork and neutral UV refinement must preserve special-track geometry and draw order");
            for (std::size_t vertex = 0; vertex < before.projected.vertices.size(); ++vertex) {
                const auto& a = before.projected.vertices[vertex];
                const auto& b = after.projected.vertices[vertex];
                expect(a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.diffuse == b.diffuse && a.specular == b.specular,
                    "Special-track artwork must preserve projected source positions and lighting");
            }
        } else {
            const auto triangles = [](const gpu::Frame& frame) {
                std::vector<std::array<gpu::Vertex, 3>> result;
                for (const auto& draw : frame.draws) {
                    for (Uint32 index = draw.first; index < draw.first + draw.count; index += 3) {
                        std::array<gpu::Vertex, 3> triangle;
                        for (unsigned corner = 0; corner < 3; ++corner) {
                            triangle[corner] = frame.vertices.at(std::size_t(draw.vertex_offset) + frame.indices.at(index + corner));
                        }
                        result.push_back(triangle);
                    }
                }
                return result;
            };
            const auto original_triangles = triangles(before.projected), repaired_triangles = triangles(after.projected);
            std::size_t next = 0;
            for (const auto& triangle : repaired_triangles) {
                if (next == original_triangles.size()) {
                    break;
                }
                bool equal = true;
                for (unsigned corner = 0; corner < 3; ++corner) {
                    const auto& a = triangle[corner];
                    const auto& b = original_triangles[next][corner];
                    equal &= a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.diffuse == b.diffuse && a.specular == b.specular && a.u == b.u && a.v == b.v;
                }
                next += equal;
            }
            expect(next == original_triangles.size(), "Gap repair must preserve every original projected triangle, lighting value and authored UV in draw order");
            expect(repaired_triangles.size() >= original_triangles.size() && repaired_triangles.size() - original_triangles.size() <= gap_triangles * 2,
                "Only the bounded source-gap cache may add projected geometry");
        }
        metrics << "    {\"name\": \"" << view.name << "\", \"source_geometry_preserved\": true, \"phases\": {";
        for (unsigned phase = 0; phase < 2; ++phase) {
            const auto& result = phase == 0 ? before : after;
            expect(result.timing.mipmapped_draw_calls > 0 && result.timing.anisotropic_draw_calls == result.timing.mipmapped_draw_calls,
                "Both inspection phases must retain opaque mipmapped2x AF");
            expect(std::ranges::none_of(result.frame.draws,
                       [](const auto& draw) {
                           return draw.state.blend || draw.state.depth_bias;
                       }),
                "Special-track inspection must not introduce material overlay layers");
            metrics << (track == 9 ? (phase == 0 ? "\"before\"" : ", \"after\"") : (phase == 0 ? "\"source\"" : ", \"mega\""))
                    << ": {\"triangles\": " << result.projected.indices.size() / 3 << ", \"gpu_draws\": " << result.timing.draw_calls << ", \"samples\": " << result.timing.samples
                    << "}";
            if (track == 9) {
                phase_metrics[phase] << "    {\"name\": \"" << view.name << "\", \"triangles\": " << result.projected.indices.size() / 3
                                     << ", \"gpu_draws\": " << result.timing.draw_calls << ", \"samples\": " << result.timing.samples << "}"
                                     << (index + 1 == views.size() ? "\n" : ",\n");
            }
        }
        metrics << "}}" << (index + 1 == views.size() ? "\n" : ",\n");
    }
    metrics << "  ]\n}\n";
    if (track == 9) {
        for (auto& phase : phase_metrics) {
            phase << "  ]\n}\n";
            expect(bool(phase), "Gap comparison metrics must be written");
        }
    }
    expect(bool(metrics), "Boundary inspection metrics must be written");
    std::cout << "PASS: special track" << track << " source geometry, lighting and material boundaries preserved\n";
}

void check(gpu::Renderer& renderer, const AssetStore& store, const std::filesystem::path& mega_root, std::uint32_t track, std::string_view material) {
    const auto resources = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = graphics_mega_detail}), mega_root);
    TerrainView before(renderer, resources.ground, true), after(renderer, resources.ground, true, track);
    const auto target = focus(resources.ground, material);
    const auto center = glm::make_vec3(target.center.data());
    const auto distance = std::max(8.f, target.span * .85f);
    const auto camera_matrix = glm::lookAtLH(center + glm::vec3(0, distance, -distance), center, glm::vec3(0, 1, 0));
    CameraInputs camera{{}, default_projection(graphics_mega_detail, 1280, 720)};
    std::copy_n(glm::value_ptr(camera_matrix), 16, camera.world_to_view.begin());
    const auto lighting = scene_lighting(resources.setup);
    const auto prefix = std::string("ground-refinement-track") + std::to_string(track);
    auto original = capture(renderer, before, camera, lighting, (prefix + "-before.png").c_str());
    auto refined = capture(renderer, after, camera, lighting, (prefix + "-after.png").c_str());
    GroundLayers source_layers;
    for (const auto& chunk : resources.ground.chunks) {
        source_layers.opaque += chunk.mesh.faces.size();
    }
    check_layers(original, source_layers);
    const auto refined_layers = expected_layers(resources.ground, track);
    check_layers(refined, refined_layers);
    expect(original.timing.samples == refined.timing.samples && (refined.timing.samples == 1 || refined.timing.samples == 2), "A/B must use the same Mega sample count");
    expect(refined.timing.mipmapped_draw_calls > 0 && refined.timing.anisotropic_draw_calls > 0, "Refined Mega ground must retain mipmapped AF rendering");
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < original.pixels.size(); ++pixel) {
        changed += original.pixels[pixel] != refined.pixels[pixel];
    }
    expect(changed > 1000, "Refinement must visibly affect the selected ground view");
    if (material != "G&G" && material != "GG" && material != "NGG" && material != "g&g" && material != "G&G2" && track != 2) {
        expect(original.projected.vertices.size() == refined.projected.vertices.size() && original.projected.indices == refined.projected.indices,
            "Neutral world UV mapping must preserve source topology and draw order");
    } else {
        expect(refined_layers.gravel > 0 && refined_layers.gravel_faces > 0, "Accepted ground transitions and direct boundaries must activate their source gravel caches");
    }
    std::cout << "PASS: track" << track << " " << material << " changed_pixels=" << changed << " vertices=" << original.projected.vertices.size() << "->"
              << refined.projected.vertices.size() << " gpu_draws=" << original.timing.draw_calls << "->" << refined.timing.draw_calls << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 3
                   || ((argc == 4 || argc == 5)
                       && (std::string_view(argv[3]) == "--edges" || std::string_view(argv[3]) == "--direct" || (argc == 5 && std::string_view(argv[3]) == "--boundaries"))),
            "Expected CANONICAL_ROOT MEGA_ROOT [--edges [TRACK] | --direct | --boundaries TRACK]");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private ground refinement video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Mega ground refinement", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private ground refinement window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[1]);
        if (argc >= 4) {
            if (std::string_view(argv[3]) == "--boundaries") {
                inspect_boundaries(renderer, store, argv[2], std::uint32_t(std::stoul(argv[4])));
            } else if (std::string_view(argv[3]) == "--direct") {
                for (const auto track : {1u, 2u}) {
                    edge_surfaces(renderer, store, argv[2], track, true);
                }
            } else if (argc == 5) {
                edge_surfaces(renderer, store, argv[2], std::uint32_t(std::stoul(argv[4])));
            } else {
                for (const auto track : {0u, 4u, 5u}) {
                    edge_surfaces(renderer, store, argv[2], track);
                }
            }
            return 0;
        }
        constexpr std::array samples{std::pair{0u, "G&G"}, std::pair{1u, "GG"}, std::pair{2u, "GRAVEL"}, std::pair{3u, "NGG"}, std::pair{4u, "G&G"}, std::pair{5u, "G&G"},
            std::pair{6u, "GRASS2"}, std::pair{7u, "g&g"}, std::pair{8u, "G&G2"}};
        for (const auto& [track, material] : samples) {
            check(renderer, store, argv[2], track, material);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
