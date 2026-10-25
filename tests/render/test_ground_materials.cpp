#include "assets/store.hpp"
#include "core/contracts.hpp"
#include "core/crc32.hpp"
#include "image/decode.hpp"
#include "scene/body.hpp"
#include "scene/follow_camera.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <SDL3/SDL_main.h>

#include <cmath>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace toca;
using namespace toca::scene;
using Bytes = std::vector<std::uint8_t>;

struct Material {
    std::size_t slot;
    std::string name, source_crc32, image;
    int width, height;
};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<Material> materials_for(const std::filesystem::path& inventory, unsigned track) {
    std::ifstream stream(inventory);
    expect(bool(stream), "Ground inventory is absent");
    std::vector<Material> result;
    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        std::istringstream row(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(row, field, '\t')) {
            fields.push_back(field);
        }
        expect(fields.size() == 14, "Unexpected ground inventory columns");
        if (std::stoul(fields[0]) != track || fields[12].empty() || std::stoul(fields[11]) == 0) {
            continue;
        }
        int width = 0, height = 0;
        if (fields[9] != "beibehalten") {
            const auto separator = fields[9].find('x');
            expect(separator != std::string::npos, "Mapped ground texture requires a target size");
            width = std::stoi(fields[9].substr(0, separator));
            height = std::stoi(fields[9].substr(separator + 1));
        }
        result.push_back({std::stoul(fields[6]), fields[7], fields[10], fields[12], width, height});
    }
    expect(!result.empty(), "Selected track has no accepted ground artwork");
    return result;
}

Bytes read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    expect(bool(stream), "Ground material asset is absent");
    Bytes result{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    expect(!result.empty() && !stream.bad(), "Cannot read complete ground material asset");
    return result;
}

std::string checksum(std::span<const std::uint8_t> bytes) {
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << crc32(bytes);
    return text.str();
}

void same_terrain(const Terrain& a, const Terrain& b) {
    expect(a.sector_centers == b.sector_centers && a.chunks.size() == b.chunks.size() && a.textures.size() == b.textures.size(),
        "Ground material replacement changed original sectors, chunks or material slots");
    for (std::size_t index = 0; index < a.chunks.size(); ++index) {
        const auto& first = a.chunks[index];
        const auto& second = b.chunks[index];
        expect(first.node_index == second.node_index && first.variant_index == second.variant_index && first.origin_xz == second.origin_xz && first.face_flags == second.face_flags
                   && first.mesh.name == second.mesh.name && first.mesh.vertices.size() == second.mesh.vertices.size() && first.mesh.faces.size() == second.mesh.faces.size(),
            "Ground material replacement changed original chunk identity or lighting flags");
        for (std::size_t vertex = 0; vertex < first.mesh.vertices.size(); ++vertex) {
            const auto& x = first.mesh.vertices[vertex];
            const auto& y = second.mesh.vertices[vertex];
            expect(x.position == y.position && x.normal == y.normal && x.u == y.u && x.v == y.v, "Ground artwork changed original positions, normals or UVs");
        }
        for (std::size_t face = 0; face < first.mesh.faces.size(); ++face) {
            const auto& x = first.mesh.faces[face];
            const auto& y = second.mesh.faces[face];
            expect(x.indices == y.indices && x.texture == y.texture && x.color == y.color, "Ground artwork changed face topology or material assignment");
        }
    }
}

SceneResources verify(const AssetStore& store, const std::filesystem::path& mega_root, const std::filesystem::path& authored_root, std::uint32_t track,
    const std::vector<Material>& materials, std::ofstream& manifest) {
    const auto setup = resolve_setup({.track_id = track, .weather_choice = 1, .detail = graphics_mega_detail});
    const auto original = load_scene_resources(store, setup);
    auto replacement = load_scene_resources(store, setup, mega_root);
    const auto very_high = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 1, .detail = 3}), mega_root);
    same_terrain(original.ground, replacement.ground);
    same_terrain(original.ground, very_high.ground);
    expect(original.collision_hpc == replacement.collision_hpc && original.route_points == replacement.route_points && original.low_detail_points == replacement.low_detail_points
               && original.cameras.size() == replacement.cameras.size(),
        "Ground artwork changed collision, route or camera resources");
    for (std::size_t index = 0; index < original.cameras.size(); ++index) {
        expect(original.cameras[index].transform == replacement.cameras[index].transform, "Ground artwork changed an authored camera");
    }
    for (std::size_t index = 0; index < original.grid.size(); ++index) {
        const auto& a = original.grid[index];
        const auto& b = replacement.grid[index];
        expect(a.first_xz == b.first_xz && a.second_xz == b.second_xz && a.heading == b.heading && a.reserved == b.reserved, "Ground artwork changed original grid positions");
    }
    std::size_t changed = 0;
    for (std::size_t slot = 0; slot < original.ground.textures.size(); ++slot) {
        const auto& before = original.ground.textures[slot];
        const auto& after = replacement.ground.textures[slot];
        const auto& normal = very_high.ground.textures[slot];
        expect(before.encoding == ImageEncoding::bmp && normal.encoding == ImageEncoding::bmp && before.name == normal.name && before.bmp == normal.bmp,
            "Very High must retain every canonical ground BMP");
        expect(before.name == after.name, "Mega must retain the original ground material names and slots");
        const auto selected = std::ranges::find(materials, slot, &Material::slot);
        if (selected == materials.end()) {
            const bool referenced = std::ranges::any_of(original.ground.chunks, [&](const auto& chunk) {
                return std::ranges::any_of(chunk.mesh.faces, [&](const auto& face) {
                    return face.texture == std::int32_t(slot);
                });
            });
            // Unused slots can already have a global replacement (Knockhill TYRES).
            // They have no ground artwork task or material-detail camera.
            expect(!referenced || (before.encoding == after.encoding && before.bmp == after.bmp), "Only used ground materials recorded in the inventory may change");
            continue;
        }
        ++changed;
        expect(before.name == selected->name && checksum(before.bmp) == selected->source_crc32, "Pilot material must match its exact canonical source CRC32");
        const auto authored = read(authored_root / selected->image);
        const auto bundled = read(mega_root / "scene" / (std::string(selected->source_crc32) + ".webp"));
        expect(after.encoding == ImageEncoding::webp && after.bmp == authored && bundled == authored && after.bmp != before.bmp,
            "Mega ground must load the exact newly authored WebP through its source-CRC32 bundle path");
        const auto decoded = toca::decode_model_texture(after);
        gpu::Surface rgba(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        expect(bool(rgba) && (selected->width == 0 || (rgba->w == selected->width && rgba->h == selected->height)), "Ground material must retain its selected size");
        const auto source = toca::decode_model_texture(before);
        gpu::Surface source_rgba(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        expect(bool(source_rgba) && rgba->w * source_rgba->h == rgba->h * source_rgba->w, "Ground material aspect must match its original slot");
        bool source_keyed = false, authored_keyed = false;
        for (int y = 0; y < source_rgba->h; ++y) {
            const auto* pixels = static_cast<const Uint8*>(source_rgba->pixels) + y * source_rgba->pitch;
            for (int x = 0; x < source_rgba->w; ++x) {
                source_keyed |= pixels[x * 4] == 0 && pixels[x * 4 + 1] == 0 && pixels[x * 4 + 2] == 0;
            }
        }
        for (int y = 0; y < rgba->h; ++y) {
            const auto* pixels = static_cast<const Uint8*>(rgba->pixels) + y * rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
                authored_keyed |= pixels[x * 4 + 3] < 255;
            }
        }
        expect(source_keyed == authored_keyed, "Ground artwork must preserve opaque versus keyed material behavior");
        manifest << track << ',' << before.name << ',' << selected->source_crc32 << ',' << checksum(after.bmp) << ',' << after.bmp.size() << ',' << rgba->w << ',' << rgba->h
                 << '\n';
    }
    expect(changed == materials.size(), "Every mapped ground slot must receive its authored artwork");
    std::cout << "PASS: track " << track << ", " << changed << " exact artwork payloads, source CRCs, dimensions/alpha, original Very High and unchanged ground/physics\n";
    return replacement;
}

void same_frame(const gpu::Frame& first, const gpu::Frame& second) {
    const auto a = test::materialize_world(first), b = test::materialize_world(second);
    expect(a.multisample && b.multisample && a.vertices.size() == b.vertices.size() && a.indices == b.indices && a.draws.size() == b.draws.size(),
        "Ground A/B must retain identical geometry, draw order and Mega sampling policy");
    expect(std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(gpu::Vertex)) == 0,
        "Ground A/B must retain exact projected positions, lighting, UVs and depth");
    for (std::size_t index = 0; index < a.draws.size(); ++index) {
        const auto& x = a.draws[index];
        const auto& y = b.draws[index];
        expect((!x.state.mipmapped || x.state.clamp) && (!y.state.mipmapped || y.state.clamp), "Mipmapped terrain draws must clamp texture borders in both ground variants");
        expect(x.first == y.first && x.count == y.count && x.vertex_offset == y.vertex_offset && x.viewport == y.viewport && x.overlay == y.overlay
                   && x.state.mipmapped == y.state.mipmapped && x.state.anisotropy == y.state.anisotropy && x.state.depth == y.state.depth && x.state.linear == y.state.linear
                   && x.state.clamp == y.state.clamp,
            "Ground A/B must retain draw ranges, viewports, depth, mipmaps and anisotropy");
    }
}

void static_pair(gpu::Renderer& renderer, const TerrainView& before, const TerrainView& after, const CameraInputs& camera, const Lighting& lighting, const char* name,
    bool centered_target = false) {
    const platform::Extent extent{1920, 1080};
    std::array frames{before.frame(camera, lighting), after.frame(camera, lighting)};
    for (auto& frame : frames) {
        frame.multisample = true;
    }
    same_frame(frames[0], frames[1]);
    std::array<std::vector<Uint32>, 2> pixels;
    std::array<Uint32, 2> samples{};
    for (std::size_t variant = 0; variant < frames.size(); ++variant) {
        expect(renderer.draw_offscreen(frames[variant], extent), "Static ground detail frame was unavailable");
        samples[variant] = renderer.last_frame_timing().samples;
        expect(renderer.last_frame_timing().mipmapped_draw_calls > 0, "Static ground details must retain Mega mipmapped drawing");
        pixels[variant] = renderer.readback();
        const auto visible = std::ranges::count_if(pixels[variant], [](auto color) {
            return color != gpu::background;
        });
        expect(visible > 1000, "Static grid/transition camera must show actual ground pixels");
        if (centered_target) {
            unsigned visible_center = 0;
            for (int y = int(extent.height / 2) - 4; y <= int(extent.height / 2) + 4; ++y) {
                for (int x = int(extent.width / 2) - 4; x <= int(extent.width / 2) + 4; ++x) {
                    visible_center += pixels[variant][std::size_t(y) * extent.width + x] != gpu::background;
                }
            }
            expect(visible_center > 8, "Transition target must reach the actual rendered image center");
        }
        gpu::Surface image(
            SDL_CreateSurfaceFrom(int(extent.width), int(extent.height), SDL_PIXELFORMAT_XRGB8888, pixels[variant].data(), int(extent.width * 4)), SDL_DestroySurface);
        gpu::check(bool(image), "Create static ground detail capture");
        const auto path = std::string("ground-materials-") + name + (variant ? "-ai.png" : "-original.png");
        gpu::check(SDL_SavePNG(image.get(), path.c_str()), "Save static ground detail capture");
    }
    expect(samples[0] == samples[1], "Static ground A/B must retain identical Mega sampling");
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < pixels[0].size(); ++pixel) {
        changed += pixels[0][pixel] != pixels[1][pixel];
    }
    expect(changed > 100, "Static grid/transition view must expose the authored ground materials");
}

void static_details(gpu::Renderer& renderer, const TerrainView& before, const TerrainView& after, const SceneResources& resources, const Lighting& lighting,
    const std::vector<Material>& materials) {
    CameraInputs camera{{}, default_projection(graphics_mega_detail, 1920, 1080)};
    std::array<float, 16> authored_world;
    for (std::size_t index = 0; index < authored_world.size(); ++index) {
        authored_world[index] = float(resources.cameras.at(0).transform[index]) / 65536.f;
    }
    const auto grid_view = glm::inverse(glm::make_mat4(authored_world.data()));
    std::copy_n(glm::value_ptr(grid_view), 16, camera.world_to_view.begin());
    static_pair(renderer, before, after, camera, lighting, "grid");

    for (const auto& material : materials) {
        bool captured = false;
        for (const auto& chunk : resources.ground.chunks) {
            for (const auto& face : chunk.mesh.faces) {
                if (face.texture != std::int32_t(material.slot)) {
                    continue;
                }
                glm::vec3 center{};
                std::array<LitVertex, 3> triangle;
                for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                    const auto& vertex = chunk.mesh.vertices.at(face.indices[corner]);
                    triangle[corner] = {vertex.position};
                    center += glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]) / 3.f;
                }
                // The renderer projects positive viewZ with Y up before mapping to
                // screenY down. GLM's left-handed view has the matching convention.
                glm::vec3 offset(0, 8, -8);
                if (resources.setup.selection.track_id == 9 && material.slot >= 5) {
                    // Lavaland's reliefs sit on steep walls. Face them directly
                    // so the road above cannot hide the authored motif.
                    const auto a = glm::make_vec3(triangle[0].position.data());
                    const auto b = glm::make_vec3(triangle[1].position.data());
                    const auto c = glm::make_vec3(triangle[2].position.data());
                    auto normal = glm::cross(b - a, c - a);
                    if (glm::length(normal) > .001f) {
                        normal = glm::normalize(normal);
                        if (glm::dot(normal, offset) < 0) {
                            normal = -normal;
                        }
                        if (std::abs(normal.y) < .98f) {
                            offset = normal * 16.f;
                        }
                    }
                }
                const auto view = glm::lookAtLH(center + offset, center, glm::vec3(0, 1, 0));
                std::copy_n(glm::value_ptr(view), 16, camera.world_to_view.begin());
                gpu::Frame visible;
                if (append_projected_triangle(visible, {.depth = true}, triangle, camera) == 0) {
                    continue;
                }
                const auto name = "material-" + std::to_string(material.slot);
                static_pair(renderer, before, after, camera, lighting, name.c_str(), true);
                captured = true;
                break;
            }
            if (captured) {
                break;
            }
        }
        expect(captured, "No visible ground triangle for an accepted material detail camera");
    }
}

void comparison(gpu::Renderer& renderer, const AssetStore& store, const SceneResources& authored, const std::vector<Material>& materials, unsigned track) {
    auto original = authored;
    const auto canonical = load_scene_resources(store, authored.setup);
    for (const auto& material : materials) {
        original.ground.textures[material.slot] = canonical.ground.textures[material.slot];
    }
    // Both sides remain the same sunny Mega scene. Only mapped image payloads vary.
    TerrainView before(renderer, original.ground, true), after(renderer, authored.ground, true);
    const auto lighting = scene_lighting(authored.setup);
    const HpcSurface surface(authored.collision_hpc);
    const auto body = initial_body_pose(surface, single_qualifying_start(authored.route_points), authored.setup.selection.car_id);
    FollowCamera follow(qualifying_camera_heading(track), false, authored.setup.selection.car_id, false, graphics_mega_detail);
    follow.update(body, true);
    const auto base = glm::inverse(glm::make_mat4(follow.world_to_view(body).data()));
    std::ofstream metadata("ground-materials-camera.csv");
    expect(bool(metadata), "Cannot create ground material comparison table");
    metadata << "width,height,view,step,variant,vertices,samples,mipmapped_draw_calls\n";
    for (const auto extent : {platform::Extent{1920, 1080}, platform::Extent{2560, 1440}}) {
        for (const bool far : {false, true}) {
            for (const unsigned step : {0u, 6u, 11u}) {
                auto world = base;
                for (int axis = 0; axis < 3; ++axis) {
                    world[3][axis] += world[0][axis] * float(step) * .12f + world[2][axis] * (float(step) * .2f - (far ? 40.f : 0.f));
                }
                const auto inverse = glm::inverse(world);
                CameraInputs camera{{}, default_projection(graphics_mega_detail, extent.width, extent.height)};
                std::copy_n(glm::value_ptr(inverse), 16, camera.world_to_view.begin());
                std::array frames{before.frame(camera, lighting), after.frame(camera, lighting)};
                for (auto& frame : frames) {
                    frame.multisample = true;
                }
                same_frame(frames[0], frames[1]);
                expect(!test::materialize_world(frames[0]).draws.empty(), "Driving camera must expose actual ground geometry");
                std::array<std::vector<Uint32>, 2> pixels;
                std::array<Uint32, 2> samples{};
                for (unsigned order = 0; order < 2; ++order) {
                    const auto variant = (step + order) % 2;
                    const auto projected_vertices = test::materialize_world(frames[variant]).vertices.size();
                    expect(renderer.draw_offscreen(frames[variant], extent), "Ground comparison frame was unavailable");
                    const auto& timing = renderer.last_frame_timing();
                    samples[variant] = timing.samples;
                    expect(timing.mipmapped_draw_calls > 0, "Both ground artwork variants must retain active mipmapped drawing");
                    metadata << extent.width << ',' << extent.height << ',' << (far ? "far" : "near") << ',' << step << ',' << (variant ? "ai" : "original") << ','
                             << projected_vertices << ',' << timing.samples << ',' << timing.mipmapped_draw_calls << '\n';
                    if (step == 6) {
                        pixels[variant] = renderer.readback();
                        renderer.save_screenshot("ground-materials-" + std::to_string(extent.width) + "x" + std::to_string(extent.height) + (far ? "-far-" : "-near-")
                                                 + (variant ? "ai.png" : "original.png"));
                    }
                }
                expect(samples[0] == samples[1] && (samples[0] == 1 || samples[0] == 2), "Ground A/B must use the same supported Mega sample count");
                if (step == 6) {
                    std::size_t changed = 0;
                    for (std::size_t pixel = 0; pixel < pixels[0].size(); ++pixel) {
                        changed += pixels[0][pixel] != pixels[1][pixel];
                    }
                    expect(changed > 100, "Authored ground materials must produce visible pixel changes in the real driving camera");
                    if (extent.width == 1920 && !far) {
                        // Reuse the exact AI frame to isolate the old sampler
                        // behavior without changing artwork, camera or geometry.
                        auto repeat = frames[1];
                        for (auto& draw : repeat.draws) {
                            if (draw.state.mipmapped) {
                                draw.state.clamp = false;
                            }
                        }
                        expect(renderer.draw_offscreen(repeat, extent), "Ground repeat-sampler comparison was unavailable");
                        renderer.save_screenshot("ground-sampler-1920x1080-near-repeat.png");
                    }
                }
            }
        }
    }
    static_details(renderer, before, after, authored, lighting, materials);
    expect(bool(metadata), "Cannot write ground material comparison table");
    std::cout << "PASS: sunny driving-camera near/far A/B, identical Mega sampling/mip policy and geometry, three representative steps at 1080p/1440p\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 6, "Expected CANONICAL_ROOT MEGA_ROOT AUTHORED_ROOT INVENTORY TRACK_ID");
        const auto track = unsigned(std::stoul(argv[5]));
        expect(track < 10, "Invalid ground comparison track");
        const auto materials = materials_for(argv[4], track);
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private ground artwork video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Donington ground material checks", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private ground artwork window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[1]);
        std::ofstream assets("ground-materials-assets.csv");
        expect(bool(assets), "Cannot create ground material provenance table");
        assets << "track,material,source_crc32,output_crc32,bytes,width,height\n";
        const auto resources = verify(store, argv[2], argv[3], track, materials, assets);
        if (track == 0) {
            const auto short_materials = materials_for(argv[4], 5);
            const auto short_track = verify(store, argv[2], argv[3], 5, short_materials, assets);
            for (const auto& material : materials) {
                expect(resources.ground.textures[material.slot].bmp == short_track.ground.textures[material.slot].bmp, "Donington GP and Short must share the exact image bytes");
            }
        }
        expect(bool(assets), "Cannot write ground material provenance table");
        comparison(renderer, store, resources, materials, track);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
