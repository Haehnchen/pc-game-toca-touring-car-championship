#include "assets/store.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/hud.hpp"
#include "scene/qualifying_view.hpp"
#include "scene/view.hpp"

#include <algorithm>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Append> void dense_overlay(toca::gpu::Renderer& renderer, toca::gpu::Frame scene, Append append) {
    const auto original_scene_vertices = scene.vertices.size();
    auto normal = scene;
    append(normal);
    expect(renderer.draw(normal, false), "Private overlay target was unavailable");
    const auto pixels = renderer.readback();
    while (scene.vertices.size() <= 65536) {
        scene.quad({}, -8, -8, 1, 1);
    }
    const auto scene_vertices = scene.vertices.size();
    append(scene);
    expect(scene.vertices.size() - scene_vertices == normal.vertices.size() - original_scene_vertices, "Dense scene must retain every overlay vertex");
    expect(renderer.draw(scene, false) && renderer.readback() == pixels, "Overlays must retain identical GPU pixels after more than65536 scene vertices");
}

template <class Append> toca::gpu::Frame matching_overlay(const toca::scene::QualifyingView& original, const toca::scene::QualifyingView& mega, Append append) {
    toca::gpu::Frame canonical, replacement;
    append(original, canonical);
    append(mega, replacement);
    expect(canonical.vertices.size() == replacement.vertices.size() && canonical.indices == replacement.indices && canonical.draws.size() == replacement.draws.size(),
        "Mega captions and tables must retain all canonical glyphs and draw ordering");
    for (std::size_t index = 0; index < canonical.vertices.size(); ++index) {
        const auto& a = canonical.vertices[index];
        const auto& b = replacement.vertices[index];
        expect(a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.u == b.u && a.v == b.v && a.diffuse == b.diffuse && a.specular == b.specular,
            "Mega BIG640 must preserve exact localized layout, kerning, wrapping, colors and normalized source coordinates");
    }
    return replacement;
}

void smooth_caption(const toca::gpu::Frame& frame) {
    expect(!frame.draws.empty()
               && std::all_of(frame.draws.begin(), frame.draws.end(),
                   [](const auto& draw) {
                       return draw.state.blend && draw.state.linear && draw.state.clamp && !draw.state.key;
                   }),
        "Mega scene captions must use alpha artwork with filtered, clamped sampling");
}

void reference(const char* root, const char* artwork_directory) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private qualifying view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA qualifying results", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private qualifying window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    const auto resources = scene::load_scene_resources(store, scene::resolve_setup({}));
    const HpcSurface surface(resources.collision_hpc);
    const auto body = scene::initial_body_pose(surface, scene::single_qualifying_start(resources.route_points), 0);
    const auto data = scene::load_qualifying_data(store, resources.setup, 1, "DANIEL");
    SourceRandom random(12345);
    scene::FlagAnimation flags(resources.flags, random);
    scene::HorizonActors actors(resources.moving_horizon);
    // Authored local lap time; the other fifteen rows use original generation.
    const auto results = scene::classify_qualifying(data, 123450, random);
    scene::FinishCamera camera(resources.cameras);
    camera.update(body.position);
    scene::SceneView scene_view(renderer, resources);
    scene::QualifyingView results_view(store, renderer, 2);
    for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
        gpu::check(SDL_SetWindowSize(window.get(), size[0], size[1]), "Resize qualifying target");
        gpu::check(SDL_SyncWindow(window.get()), "Synchronize qualifying target size");
        SDL_PumpEvents();
        const scene::CameraInputs inputs{camera.world_to_view(body.position), scene::default_projection(3, size[0], size[1])};
        auto frame = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
        const auto scene_vertices = frame.vertices.size();
        results_view.append(frame, results, size[0], size[1]);
        expect(frame.vertices.size() > scene_vertices + 600, "Original qualifying rows did not reach the shared scene frame");
        const auto visible_overlay = [&](const auto& target) {
            return std::all_of(target.vertices.begin() + std::ptrdiff_t(scene_vertices), target.vertices.end(), [&](const auto& vertex) {
                return vertex.x >= 0 && vertex.x <= size[0] && vertex.y >= 0 && vertex.y <= size[1];
            });
        };
        expect(visible_overlay(frame), "Localized qualifying table is clipped by the drawable");
        expect(renderer.draw(frame, false), "Private qualifying render target was unavailable");
        renderer.save_screenshot("qualifying-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        const auto pixels = renderer.readback();
        expect(pixels.size() == std::size_t(size[0] * size[1]), "Qualifying target dimensions changed");
        if (size[0] == 640) {
            const auto background = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
            dense_overlay(renderer, background, [&](auto& target) {
                results_view.append(target, results, size[0], size[1]);
            });
            dense_overlay(renderer, background, [&](auto& target) {
                results_view.append_rollout(target, GameMode::single_race, 20, 50, 900, size[0], size[1]);
            });
            scene::Hud hud(store, renderer, 2);
            hud.update(123, 6500, 4, 3);
            hud.set_session({.lap = 2, .lap_time_ms = 123450, .best_lap_ms = 124560, .position = 3});
            dense_overlay(renderer, background, [&](auto& target) {
                hud.append(target, size[0], size[1]);
            });
        }
        auto finish = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
        results_view.append_finish(finish, size[0], size[1]);
        expect(finish.vertices.size() > scene_vertices && visible_overlay(finish), "Localized qualifying finish label is missing or clipped");
        expect(renderer.draw(finish, false), "Private finish render target was unavailable");
        renderer.save_screenshot("finish-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        // A layout fixture with explicit totals; CPU RaceProgress tests supply
        // the actual-pose classification. Qualifying times do not enter this table.
        race::RaceResults race_results;
        for (std::uint32_t rank = 0; rank < 16; ++rank) {
            race_results.rows[rank] = {rank, rank / 2, 120000 + rank * 4321, 1, "DRIVER" + std::to_string(rank), true};
        }
        auto race_frame = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
        results_view.append_race(race_frame, race_results, size[0], size[1]);
        expect(race_frame.vertices.size() > scene_vertices + 600 && visible_overlay(race_frame), "Original16-row race table must fit every supported drawable aspect");
        expect(renderer.draw(race_frame, false), "Private race-result target was unavailable");
        renderer.save_screenshot("race-results-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        race_results.count = 2;
        for (std::size_t rank = 2; rank < race_results.rows.size(); ++rank) {
            race_results.rows[rank].car_id = 99; // Inactive rows must never reach rendering.
        }
        auto split_results = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
        results_view.append_race(split_results, race_results, size[0], size[1]);
        expect(split_results.vertices.size() > scene_vertices && split_results.vertices.size() < race_frame.vertices.size() && visible_overlay(split_results),
            "Two-player results must show only the active rows and fit the full-window overlay");
        expect(renderer.draw(split_results, false), "Private two-player result target was unavailable");
        renderer.save_screenshot("split-results-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        for (const auto position : {1u, 2u, 3u, 16u}) {
            auto race_finish = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
            results_view.append_race_finish(race_finish, position, size[0], size[1]);
            expect(race_finish.vertices.size() > scene_vertices && visible_overlay(race_finish), "Localized actual-rank race finish caption is missing or clipped");
        }
        constexpr std::array<std::string_view, 3> captions{"PREPARE TO QUALIFY", "QUALIFICATIONS", "AUF QUALIFIKTN VORBEREITEN"};
        const auto images = store.open_archive("data/image640.bfp");
        const auto caption_font = frontend::decode_font(images.member(23).bytes());
        for (std::uint32_t language = 0; language < captions.size(); ++language) {
            scene::QualifyingView caption_view(store, renderer, language);
            gpu::Frame caption;
            caption_view.append_rollout(caption, GameMode::single_race, 20, 50, 900, size[0], size[1]);
            const float scale = std::min(float(size[0]) / 640, float(size[1]) / 480);
            std::size_t glyph_index = 0;
            bool second_line = false;
            for (const auto character : captions[language]) {
                if (character == ' ') {
                    continue;
                }
                const auto* glyph = caption_font.find(std::uint8_t(character));
                expect(glyph && (glyph_index + 1) * 4 <= caption.vertices.size(), "Localized rollout text lost an original font glyph");
                const auto& vertex = caption.vertices[glyph_index * 4];
                expect(std::all_of(caption.vertices.begin() + std::ptrdiff_t(glyph_index * 4), caption.vertices.begin() + std::ptrdiff_t((glyph_index + 1) * 4),
                           [](const auto& corner) {
                               return corner.diffuse == 0xffc8c8c8;
                           }),
                    "Original4652ae caption glyphs require RGB200 modulation with opaque alpha");
                expect(vertex.u == float(glyph->x) / caption_font.atlas->w && vertex.v == float(glyph->y) / caption_font.atlas->h,
                    "Rollout caption differs from its original localized GSTRINGS text");
                second_line |= vertex.y > 130 * scale;
                ++glyph_index;
            }
            expect(caption.vertices.size() == glyph_index * 4 && second_line == (language == 2), "Original German preparation caption must wrap into two lines");
            expect(std::all_of(caption.vertices.begin(), caption.vertices.end(),
                       [&](const auto& vertex) {
                           return vertex.x >= 0 && vertex.x < size[0] && vertex.y >= 100 * scale && vertex.y < 180 * scale;
                       }),
                "Preparation caption lost its centered authored position or exceeds its drawable");
            auto rollout = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
            caption_view.append_rollout(rollout, GameMode::single_race, 20, 50, 900, size[0], size[1]);
            expect(renderer.draw(rollout, false), "Private rollout render target was unavailable");
            renderer.save_screenshot("rollout-lang" + std::to_string(language) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
            gpu::Frame hidden;
            caption_view.append_rollout(hidden, GameMode::single_race, 49, 50, 660, size[0], size[1]);
            expect(hidden.vertices.empty(), "Final rollout caption must disappear in the original off phase");
        }
        constexpr std::array<std::string_view, 5> trial_captions{"GET READY", "PREPAREZ-VOUS", "BEREITMACHEN", "PREPARATI", "PREPARATE"};
        for (std::uint32_t language = 0; language < trial_captions.size(); ++language) {
            scene::QualifyingView caption_view(store, renderer, language);
            gpu::Frame caption;
            caption_view.append_rollout(caption, GameMode::time_trial, 20, 50, 900, size[0], size[1]);
            std::size_t glyph_index = 0;
            for (const auto character : trial_captions[language]) {
                if (character == ' ') {
                    continue;
                }
                const auto* glyph = caption_font.find(std::uint8_t(character));
                expect(glyph && (glyph_index + 1) * 4 <= caption.vertices.size(), "Time Trial preparation text lost an original font glyph");
                const auto& vertex = caption.vertices[glyph_index * 4];
                expect(vertex.u == float(glyph->x) / caption_font.atlas->w && vertex.v == float(glyph->y) / caption_font.atlas->h,
                    "Time Trial preparation must select its original localized GSTRINGS51 text");
                ++glyph_index;
            }
            expect(caption.vertices.size() == glyph_index * 4, "Time Trial preparation caption has unexpected glyphs");
            gpu::Frame start_hidden, flash_visible, flash_hidden;
            caption_view.append_rollout(start_hidden, GameMode::time_trial, 4, 50, 0, size[0], size[1]);
            caption_view.append_rollout(flash_visible, GameMode::time_trial, 49, 50, 659, size[0], size[1]);
            caption_view.append_rollout(flash_hidden, GameMode::time_trial, 49, 50, 660, size[0], size[1]);
            expect(start_hidden.vertices.empty() && flash_visible.vertices.size() == caption.vertices.size() && flash_hidden.vertices.empty(),
                "Time Trial preparation must retain original approach target and flash boundaries");
        }
    }
    gpu::check(SDL_SetWindowSize(window.get(), 1920, 1080), "Resize Mega caption target");
    gpu::check(SDL_SyncWindow(window.get()), "Synchronize Mega caption target");
    SDL_PumpEvents();
    const scene::CameraInputs inputs{camera.world_to_view(body.position), scene::default_projection(3, 1920, 1080)};
    const auto background = scene_view.frame(inputs, scene::scene_lighting(resources.setup), {body.object_to_world}, flags.variants(), actors.frame());
    for (std::uint32_t language = 0; language < 5; ++language) {
        scene::QualifyingView original(store, renderer, language);
        scene::QualifyingView mega(store, renderer, language, true, artwork_directory);
        for (const auto mode : {GameMode::single_race, GameMode::time_trial}) {
            const auto caption = matching_overlay(original, mega, [&](const auto& view, auto& target) {
                view.append_rollout(target, mode, 20, 50, 900, 1920, 1080);
            });
            smooth_caption(caption);
            const auto hidden = matching_overlay(original, mega, [&](const auto& view, auto& target) {
                view.append_rollout(target, mode, 49, 50, 660, 1920, 1080);
            });
            expect(hidden.vertices.empty(), "Mega rollout must retain the strict original660ms flash-off boundary");
        }
        auto frame = background;
        mega.append_rollout(frame, GameMode::single_race, 20, 50, 900, 1920, 1080);
        expect(renderer.draw(frame, false), "Private Mega localized caption target unavailable");
        renderer.save_screenshot("rollout-mega-lang" + std::to_string(language) + "-1920x1080.png");
        smooth_caption(matching_overlay(original, mega, [&](const auto& view, auto& target) {
            view.append_finish(target, 1920, 1080);
        }));
        smooth_caption(matching_overlay(original, mega, [&](const auto& view, auto& target) {
            view.append_race_finish(target, 16, 1920, 1080);
        }));
        for (const auto exiting : {false, true}) {
            smooth_caption(matching_overlay(original, mega, [&](const auto& view, auto& target) {
                view.append_pit(target, exiting, 659, 1920, 1080);
            }));
            const auto flashed = matching_overlay(original, mega, [&](const auto& view, auto& target) {
                view.append_pit(target, exiting, 660, 1920, 1080);
            });
            expect(flashed.vertices.empty() != exiting, "Mega pit entry must flash while the original exit caption remains continuously visible");
        }
        const auto table = matching_overlay(original, mega, [&](const auto& view, auto& target) {
            view.append(target, results, 1920, 1080);
        });
        const auto smooth = std::count_if(table.draws.begin(), table.draws.end(), [](const auto& draw) {
            return draw.state.linear && draw.state.clamp && draw.state.blend && !draw.state.key;
        });
        expect(smooth > 0 && std::size_t(smooth) + 17 == table.draws.size(),
            "BIG640 headings and all LIL640 result rows must use smooth artwork while the panel and16 original car icons retain their rendering");
        if (language == 0) {
            auto trial = background;
            mega.append_rollout(trial, GameMode::time_trial, 20, 50, 900, 1920, 1080);
            expect(renderer.draw(trial, false), "Private Mega Time Trial target unavailable");
            renderer.save_screenshot("time-trial-rollout-mega-1920x1080.png");
            auto classified = background;
            mega.append(classified, results, 1920, 1080);
            expect(renderer.draw(classified, false), "Private Mega results target unavailable");
            renderer.save_screenshot("qualifying-mega-1920x1080.png");
        }
    }
    std::cout << "PASS: original qualifying rows, finish and localized rollout captions at 4:3/16:9/16:10; local rank=" << results.local_rank + 1 << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Usage: toca-qualifying-view-tests CANONICAL_ROOT ARTWORK_DIRECTORY");
        }
        reference(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
