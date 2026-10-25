#include "assets/store.hpp"
#include "core/contracts.hpp"
#include "scene/body.hpp"
#include "scene/follow_camera.hpp"
#include "scene/mega_ground_transitions.hpp"
#include "scene/view.hpp"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <span>

namespace {
using namespace toca;
using namespace toca::gpu;
using namespace toca::scene;
constexpr std::array modes{Anisotropy::off, Anisotropy::x2, Anisotropy::x4};
constexpr std::array names{"off", "2x", "4x"};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Paint> Surface image(unsigned width, unsigned height, Paint paint) {
    Surface result(SDL_CreateSurface(int(width), int(height), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    check(bool(result), "Create directional filtering fixture");
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto rgba = paint(x, y);
            std::memcpy(static_cast<Uint8*>(result->pixels) + y * result->pitch + x * 4, rgba.data(), 4);
        }
    }
    return result;
}

std::vector<Uint32> render(Renderer& renderer, const Frame& frame, platform::Extent extent, bool ui = false) {
    expect(renderer.draw_offscreen(frame, extent, ui), "Ground filtering target unavailable");
    return renderer.readback();
}

double contrast(std::span<const Uint32> pixels, unsigned width) {
    double total = 0;
    for (unsigned y = 4; y < 12; ++y) {
        for (unsigned x = 8; x < 120; ++x) {
            total += std::abs(int(pixels[y * width + x] & 255) - 128);
        }
    }
    return total / (8 * 112);
}

void directional_detail(Renderer& renderer) {
    // Sixteenfold V minification and only twofold U minification. Isotropic
    // LOD4 averages out the horizontal detail even though it is resolvable in U.
    const auto surface = image(256, 256, [](unsigned x, unsigned) {
        const auto value = Uint8((x / 8) % 2 ? 255 : 0);
        return std::array<Uint8, 4>{value, value, value, 255};
    });
    const auto texture = renderer.upload(surface.get(), false, true);
    std::array<double, 3> detail{};
    std::ofstream summary("ground-af-synthetic.csv");
    expect(bool(summary), "Cannot create directional detail table");
    summary << "anisotropy,mean_absolute_contrast,anisotropic_draw_calls\n";
    for (unsigned mode = 0; mode < modes.size(); ++mode) {
        Frame frame;
        frame.quad({.texture = texture.get(), .clamp = true, .mipmapped = true, .anisotropy = modes[mode]}, 0, 0, 128, 16);
        const auto pixels = render(renderer, frame, {128, 16});
        detail[mode] = contrast(pixels, 128);
        expect(renderer.last_frame_timing().anisotropic_draw_calls == (mode ? 1u : 0u), "Directional fixture did not use the requested AF sampler");
        summary << names[mode] << ',' << detail[mode] << ',' << renderer.last_frame_timing().anisotropic_draw_calls << '\n';
        renderer.save_screenshot("ground-af-synthetic-" + std::string(names[mode]) + ".png");
    }
    expect(detail[0] < 3 && detail[1] > detail[0] + 10, "2x AF must recover real minor-axis detail lost by isotropic mip selection");
    expect(detail[2] >= detail[1] - 4, "4x AF must retain the directional detail recovered at 2x");
    Frame faded;
    faded.quad({.texture = texture.get(), .blend = true, .clamp = true, .mipmapped = true, .anisotropy = Anisotropy::x2}, 0, 0, 128, 16);
    expect(contrast(render(renderer, faded, {128, 16}), 128) > detail[0] + 10 && renderer.last_frame_timing().anisotropic_draw_calls == 1,
        "Vertex-faded opaque ground must retain the neighboring material's directional detail");
    expect(bool(summary), "Cannot write directional detail table");
}

void borders_and_isolation(Renderer& renderer) {
    const auto surface = image(64, 64, [](unsigned x, unsigned y) {
        return std::array<Uint8, 4>{Uint8(y < 32 ? 224 : 32), Uint8((x / 4) % 2 ? 200 : 40), 80, 255};
    });
    const auto texture = renderer.upload(surface.get(), false, true);
    for (const auto mode : {Anisotropy::x2, Anisotropy::x4}) {
        Frame frame;
        for (unsigned edge = 0; edge < 2; ++edge) {
            const auto first = frame.vertices.size();
            frame.quad({.texture = texture.get(), .clamp = true, .mipmapped = true, .anisotropy = mode}, float(edge * 16), 0, 8, 8);
            for (auto& vertex : std::span(frame.vertices).subspan(first)) {
                vertex.v = edge ? 1.25f : -.25f;
            }
        }
        const auto pixels = render(renderer, frame, {32, 16});
        for (unsigned edge = 0; edge < 2; ++edge) {
            for (unsigned y = 2; y < 6; ++y) {
                for (unsigned x = 2; x < 6; ++x) {
                    const auto red = int((pixels[y * 32 + edge * 16 + x] >> 16) & 255);
                    expect(std::abs(red - (edge ? 32 : 224)) <= 1, "AF must preserve clamp borders without sampling the opposite terrain edge");
                }
            }
        }
    }

    Frame ordinary;
    ordinary.quad({.texture = texture.get(), .linear = true}, 0, 0, 128, 16);
    const auto baseline = render(renderer, ordinary, {128, 16});
    ordinary.draws.front().state.anisotropy = Anisotropy::x4;
    expect(render(renderer, ordinary, {128, 16}) == baseline && renderer.last_frame_timing().anisotropic_draw_calls == 0,
        "A nonmipmapped original draw must retain its existing bilinear sampler");

    const auto translucent = image(16, 16, [](unsigned, unsigned) {
        return std::array<Uint8, 4>{64, 128, 192, 127};
    });
    Uint32 levels = 0;
    const auto alpha = renderer.upload(translucent.get(), false, true, &levels);
    expect(levels == 1, "An alpha image must remain outside the opaque mip/AF path");
    Frame blended;
    blended.quad({}, 0, 0, 16, 16, 0xff000000);
    blended.quad({.texture = alpha.get(), .blend = true, .linear = true, .mipmapped = levels > 1, .anisotropy = Anisotropy::x4}, 0, 0, 16, 16);
    const auto pixels = render(renderer, blended, {16, 16});
    const auto pixel = pixels[8 * 16 + 8];
    expect(renderer.last_frame_timing().anisotropic_draw_calls == 0 && std::abs(int((pixel >> 16) & 255) - 32) <= 1 && std::abs(int((pixel >> 8) & 255) - 64) <= 1
               && std::abs(int(pixel & 255) - 96) <= 1,
        "Excluded alpha must retain its actual blend output");

    const auto keyed = image(16, 16, [](unsigned x, unsigned) {
        return x < 8 ? std::array<Uint8, 4>{0, 0, 0, 255} : std::array<Uint8, 4>{0, 200, 0, 255};
    });
    const auto key = renderer.upload(keyed.get(), true, true, &levels);
    expect(levels == 1, "RGB-black keyed images must stay outside mipmapped AF");
    Frame holes;
    holes.quad({}, 0, 0, 16, 16, 0xffff8000);
    holes.quad({.texture = key.get(), .key = true, .mipmapped = levels > 1, .anisotropy = Anisotropy::x2}, 0, 0, 16, 16);
    const auto keyed_pixels = render(renderer, holes, {16, 16});
    expect(renderer.last_frame_timing().anisotropic_draw_calls == 0 && keyed_pixels[8 * 16 + 4] == 0xffff8000 && keyed_pixels[8 * 16 + 12] == 0xff00c800,
        "Nonmipmapped keyed draws must preserve original transparent holes and opaque texels");

    Frame hud;
    hud.quad({.texture = texture.get(), .clamp = true, .mipmapped = true}, 0, 0, 128, 16);
    const auto hud_before = render(renderer, hud, {640, 480}, true);
    hud.draws.front().state.anisotropy = Anisotropy::x4;
    expect(render(renderer, hud, {640, 480}, true) == hud_before && renderer.last_frame_timing().anisotropic_draw_calls == 0,
        "UI rendering must retain its existing sampler even if an AF state is supplied");
    hud.draws.front().overlay = true;
    const auto overlay_before = render(renderer, hud, {128, 16});
    hud.draws.front().state.anisotropy = Anisotropy::off;
    expect(render(renderer, hud, {128, 16}) == overlay_before && renderer.last_frame_timing().anisotropic_draw_calls == 0,
        "World HUD overlays must retain trilinear sampling and never enter AF");
}

void coplanar_ground(Renderer& renderer) {
    const std::array<ModelVertex, 3> source{
        {{{-149.37123f, 12.1654f, 382.3245f}, {0, 1, 0}, .02f, .02f}, {{-158.7f, 12.7f, 400.1f}, {0, 1, 0}, .02f, .99f}, {{-138.435f, 13.875f, 408.9f}, {0, 1, 0}, .99f, .99f}}};
    glm::vec3 center{};
    for (const auto& vertex : source) {
        center += glm::make_vec3(vertex.position.data()) / 3.f;
    }
    CameraInputs camera{{}, default_projection(graphics_mega_detail, 1280, 720)};
    const auto matrix = glm::lookAtLH(center + glm::vec3(0, 8, -13), center, glm::vec3(0, 1, 0));
    std::copy_n(glm::value_ptr(matrix), 16, camera.world_to_view.begin());
    const auto transition = prepare_mega_ground_transition(4, source, .12f);
    expect(transition.has_value(), "Coplanar fixture requires a valid mixed terrain triangle");
    const auto frame = [&](bool test_depth, bool bias, bool occluder) {
        Frame result;
        result.multisample = true;
        std::array<LitVertex, 3> base;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            base[corner] = {source[corner].position, 0xff306030, 0, 0};
        }
        append_projected_triangle(result, {.depth = true}, base, camera);
        if (occluder) {
            result.quad({.depth = true}, 624, 344, 32, 32, 0xff2040c0);
        }
        for (const auto& triangle : transition->gravel) {
            std::array<LitVertex, 3> layer;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                layer[corner] = {triangle[corner].position, (Uint32(triangle[corner].opacity) << 24) | 0x00dbb040, 0, 0};
            }
            append_projected_triangle(result, {.depth = test_depth, .blend = true, .depth_write = false, .depth_bias = bias}, layer, camera);
        }
        return result;
    };
    const auto oracle = render(renderer, frame(false, false, false), {1280, 720});
    const auto biased = render(renderer, frame(true, true, false), {1280, 720});
    expect(biased == oracle, "Coplanar material depth bias must eliminate missing blend samples without changing color coverage");
    const auto occluded = render(renderer, frame(true, true, true), {1280, 720});
    expect(occluded[360 * 1280 + 640] == 0xff2040c0, "Biased ground must still respect nearer geometry in the depth buffer");
}

struct Metrics {
    double luminance = 0, spatial_delta = 0, temporal_delta = 0;
};

double luminance(Uint32 pixel) {
    return (.2126 * ((pixel >> 16) & 255)) + (.7152 * ((pixel >> 8) & 255)) + (.0722 * (pixel & 255));
}

Metrics metrics(std::span<const Uint32> current, std::span<const Uint32> previous, platform::Extent extent) {
    Metrics result;
    std::size_t count = 0, spatial_count = 0, temporal_count = 0;
    // Read a fixed lower-screen region and exclude untouched background.
    // Temporal delta includes actual camera motion; it is not an isolated
    // shimmer score or a claim that sharper/more contrast always looks better.
    for (unsigned y = extent.height / 3; y < extent.height * 9 / 10; ++y) {
        for (unsigned x = extent.width / 8; x < extent.width * 7 / 8; ++x) {
            const auto index = std::size_t(y) * extent.width + x;
            if (current[index] == background) {
                continue;
            }
            const auto value = luminance(current[index]);
            result.luminance += value;
            ++count;
            if (current[index - 1] != background) {
                result.spatial_delta += std::abs(value - luminance(current[index - 1]));
                ++spatial_count;
            }
            if (!previous.empty() && previous[index] != background) {
                result.temporal_delta += std::abs(value - luminance(previous[index]));
                ++temporal_count;
            }
        }
    }
    expect(count > 100, "Ground comparison region contains no rendered terrain");
    result.luminance /= double(count);
    result.spatial_delta /= double(std::max<std::size_t>(1, spatial_count));
    result.temporal_delta /= double(std::max<std::size_t>(1, temporal_count));
    return result;
}

void terrain_policy(const TerrainView& mega, const TerrainView& original, const CameraInputs& camera, const Lighting& lighting) {
    const auto frame = mega.frame(camera, lighting);
    std::size_t opaque = 0;
    for (const auto& draw : frame.draws) {
        expect(draw.state.anisotropy == (draw.state.mipmapped ? Anisotropy::x2 : Anisotropy::off), "Mega TerrainView must request 2x only for its opaque mipmapped slots");
        expect(!draw.state.mipmapped || draw.state.clamp, "AF terrain must preserve clamped borders");
        opaque += draw.state.mipmapped;
    }
    expect(opaque > 0, "Actual Donington terrain contains no opaque mipmapped material");
    for (const auto& draw : original.frame(camera, lighting).draws) {
        expect(!draw.state.mipmapped && draw.state.anisotropy == Anisotropy::off, "Original TerrainView must retain original non-AF sampling");
    }
}

void comparison(Renderer& renderer, const std::filesystem::path& canonical, const std::filesystem::path& mega_root) {
    const AssetStore store(canonical);
    const auto setup = resolve_setup({.track_id = 0, .weather_choice = 1, .detail = graphics_mega_detail});
    const auto resources = load_scene_resources(store, setup, mega_root);
    const auto lighting = scene_lighting(setup);
    TerrainView terrain(renderer, resources.ground, true), original(renderer, resources.ground, false);
    const HpcSurface surface(resources.collision_hpc);
    const auto body = initial_body_pose(surface, single_qualifying_start(resources.route_points), setup.selection.car_id);
    FollowCamera follow(qualifying_camera_heading(0), false, setup.selection.car_id, false, graphics_mega_detail);
    follow.update(body, true);
    const auto base = glm::inverse(glm::make_mat4(follow.world_to_view(body).data()));
    std::ofstream rows("ground-af-camera.csv"), notes("ground-af-comparison.txt");
    expect(bool(rows) && bool(notes), "Cannot create AF comparison metadata");
    rows << "width,height,view,step,anisotropy,vertices,samples,mipmapped_draw_calls,anisotropic_draw_calls,mean_luminance,spatial_delta,temporal_delta\n";
    notes << "Donington GP sunny Mega, identical ground images, geometry, lighting, camera and supported 2x MSAA. Only terrain sampler anisotropy varies.\n"
          << "Off means existing trilinear mip filtering. 2x/4x retain clamp borders and mip chains.\n"
          << "Twelve camera steps: lateral 0.12 m and forward 0.2 m per step; far view offsets the same camera 40 m backward.\n"
          << "Modes rotate draw order each step.\n"
          << "Image metrics use the lower-screen terrain region. Temporal delta includes camera motion and is not an isolated shimmer metric.\n"
          << "All 1080p near-view steps are PNG sequences; step 6 captures also cover far and 1440p. Compare images/motion before choosing a fixed AF level.\n";
    for (const auto extent : {platform::Extent{1920, 1080}, platform::Extent{2560, 1440}}) {
        for (const bool far : {false, true}) {
            std::array<std::vector<Uint32>, 3> previous;
            for (unsigned step = 0; step < 12; ++step) {
                auto world = base;
                for (int axis = 0; axis < 3; ++axis) {
                    world[3][axis] += world[0][axis] * float(step) * .12f + world[2][axis] * (float(step) * .2f - (far ? 40.f : 0.f));
                }
                const auto inverse = glm::inverse(world);
                CameraInputs camera{{}, default_projection(graphics_mega_detail, extent.width, extent.height)};
                std::copy_n(glm::value_ptr(inverse), 16, camera.world_to_view.begin());
                if (step == 0) {
                    terrain_policy(terrain, original, camera, lighting);
                }
                auto prepared = terrain.frame(camera, lighting);
                prepared.multisample = true;
                std::array frames{prepared, prepared, prepared};
                for (unsigned mode = 0; mode < modes.size(); ++mode) {
                    for (auto& draw : frames[mode].draws) {
                        draw.state.anisotropy = draw.state.mipmapped ? modes[mode] : Anisotropy::off;
                    }
                }
                std::array<std::vector<Uint32>, 3> pixels;
                std::array<Uint32, 3> samples{};
                for (unsigned order = 0; order < modes.size(); ++order) {
                    const auto mode = (step + order) % modes.size();
                    expect(renderer.draw_offscreen(frames[mode], extent), "AF camera frame unavailable");
                    const auto timing = renderer.last_frame_timing();
                    expect(timing.mipmapped_draw_calls > 0 && (mode ? timing.anisotropic_draw_calls > 0 : timing.anisotropic_draw_calls == 0),
                        "Actual terrain frame did not use its selected AF sampler");
                    samples[mode] = timing.samples;
                    pixels[mode] = renderer.readback();
                    const auto image_metrics = metrics(pixels[mode], previous[mode], extent);
                    rows << extent.width << ',' << extent.height << ',' << (far ? "far" : "near") << ',' << step << ',' << names[mode] << ',' << prepared.vertices.size() << ','
                         << timing.samples << ',' << timing.mipmapped_draw_calls << ',' << timing.anisotropic_draw_calls << ',' << image_metrics.luminance << ','
                         << image_metrics.spatial_delta << ',' << image_metrics.temporal_delta << '\n';
                    if (step == 6 || (extent.width == 1920 && !far)) {
                        renderer.save_screenshot("ground-af-" + std::to_string(extent.width) + "x" + std::to_string(extent.height) + (far ? "-far-" : "-near-") + names[mode]
                                                 + "-step-" + (step < 10 ? "0" : "") + std::to_string(step) + ".png");
                    }
                }
                expect(samples[0] == samples[1] && samples[0] == samples[2] && (samples[0] == 1 || samples[0] == 2), "AF comparison changed the supported Mega MSAA count");
                if (step == 6) {
                    for (unsigned mode = 1; mode < modes.size(); ++mode) {
                        std::size_t changed = 0;
                        for (std::size_t pixel = 0; pixel < pixels[0].size(); ++pixel) {
                            changed += pixels[0][pixel] != pixels[mode][pixel];
                        }
                        expect(changed > 100, "AF must visibly affect actual oblique Donington terrain");
                        notes << extent.width << 'x' << extent.height << ' ' << (far ? "far" : "near") << ' ' << names[mode] << " versus off: " << changed << " changed pixels.\n";
                    }
                }
                previous = std::move(pixels);
            }
        }
    }
    expect(bool(rows) && bool(notes), "Cannot write AF comparison results");
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 1 || argc == 3, "Expected no arguments or CANONICAL_ROOT MEGA_ROOT");
        check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated ground filtering video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA ground filtering checks", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        check(bool(window), "Create private ground filtering window");
        Renderer renderer(window.get());
        directional_detail(renderer);
        borders_and_isolation(renderer);
        coplanar_ground(renderer);
        if (argc == 3) {
            comparison(renderer, argv[1], argv[2]);
        }
        renderer.wait_idle();
        std::cout << "PASS: directional AF detail, clamped terrain borders and original/alpha/HUD sampler isolation"
                  << (argc == 3 ? "; Donington off/2x/4x 1080p/1440p camera comparison retained\n" : "\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
