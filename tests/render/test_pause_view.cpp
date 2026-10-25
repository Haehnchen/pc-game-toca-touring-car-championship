#include "assets/store.hpp"
#include "scene/pause_view.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void reference(const char* root, const char* artwork_directory) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private pause view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA pause view", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private pause view window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    // English bounds measured from the running unchanged original. Width and
    // height include49d0c0's truncation to four equal panel tiles per axis.
    constexpr std::array<std::array<int, 4>, 3> english_panels{{{116, 186, 404, 128}, {88, 186, 460, 128}, {122, 186, 392, 68}}};
    for (std::uint32_t language = 0; language < 3; ++language) {
        scene::PauseView view(store, renderer, language);
        scene::PauseView mega(store, renderer, language, true, artwork_directory);
        scene::PauseController controller;
        // Match the actual mutable original profile used for live capture.
        controller.open(true, {.music_volume = 40});
        for (int page = 0; page < 5; ++page) {
            const std::string_view notice = page == 4 ? "Ghost could not be saved. Check free space and try again." : "";
            gpu::Frame authored;
            view.append(authored, controller, 640, 480, notice);
            gpu::Frame replacement;
            mega.append(replacement, controller, 640, 480, notice);
            expect(replacement.vertices.size() == authored.vertices.size() && replacement.indices == authored.indices && replacement.draws.size() == authored.draws.size(),
                "Mega pause must preserve every original panel, heading, row and notice glyph");
            for (std::size_t index = 0; index < authored.vertices.size(); ++index) {
                const auto& a = authored.vertices[index];
                const auto& b = replacement.vertices[index];
                expect(a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.u == b.u && a.v == b.v && a.diffuse == b.diffuse && a.specular == b.specular,
                    "Mega pause font replacement must retain canonical layout, kerning, UVs and selected-row colors");
            }
            const auto smooth = std::count_if(replacement.draws.begin(), replacement.draws.end(), [](const auto& draw) {
                return draw.state.linear && draw.state.clamp && draw.state.blend && !draw.state.key;
            });
            const auto text_draws = std::count_if(replacement.draws.begin(), replacement.draws.end(), [](const auto& draw) {
                return draw.state.texture != nullptr;
            });
            expect(smooth > 0 && smooth == text_draws && std::size_t(smooth) < replacement.draws.size(),
                "Mega pause headings, rows and notices must all use smooth font artwork while preserving the translucent panels");
            expect(!authored.vertices.empty() && authored.vertices[0].diffuse == 0x900a0a0a, "Pause must use the original translucent panel color");
            if (page < 3) {
                expect(std::abs(authored.vertices[0].y - 185.5f) < .001f, "Original pause panel must begin at authored y186");
            }
            if (language == 0 && page < 3) {
                const auto& bounds = english_panels[page];
                expect(authored.vertices[0].x == float(bounds[0]) - .5f && authored.vertices[0].y == float(bounds[1]) - .5f
                           && authored.vertices[2].x - authored.vertices[0].x == bounds[2] && authored.vertices[2].y - authored.vertices[0].y == bounds[3],
                    "English pause panel must match measured original window edges");
            }
            for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
                gpu::check(SDL_SetWindowSize(window.get(), size[0], size[1]), "Resize pause view target");
                gpu::check(SDL_SyncWindow(window.get()), "Synchronize pause view target");
                SDL_PumpEvents();
                gpu::Frame frame;
                view.append(frame, controller, size[0], size[1], notice);
                expect(frame.vertices.size() == authored.vertices.size(), "Changing aspect must retain every original pause glyph");
                const float scale = std::min(float(size[0]) / 640, float(size[1]) / 480);
                const float left = (float(size[0]) - 640 * scale) / 2, top = (float(size[1]) - 480 * scale) / 2;
                for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
                    const auto& actual = frame.vertices[index];
                    const auto& original = authored.vertices[index];
                    expect(std::abs(actual.x + .5f - left - (original.x + .5f) * scale) < .001f && std::abs(actual.y + .5f - top - (original.y + .5f) * scale) < .001f,
                        "Widescreen pause must preserve the centered original proportions");
                    expect(actual.x >= 0 && actual.x < size[0] && actual.y >= 0 && actual.y < size[1], "Localized pause glyph exceeds its drawable");
                }
                expect(renderer.draw(frame, false), "Private pause target was unavailable");
                const auto pixels = renderer.readback();
                expect(!pixels.empty(), "Private pause rendering must produce readable pixels");
                if (language == 0 && (page == 0 || page == 4) && size[0] == 640) {
                    gpu::Frame dense;
                    while (dense.vertices.size() <= 65536) {
                        dense.quad({}, -8, -8, 1, 1);
                    }
                    view.append(dense, controller, size[0], size[1], notice);
                    expect(renderer.draw(dense, false) && renderer.readback() == pixels,
                        "Pause panels and save notices must retain identical GPU pixels after more than65536 scene vertices");
                }
                renderer.save_screenshot(
                    "pause-lang" + std::to_string(language) + "-page" + std::to_string(page) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
            }
            if (page == 0) {
                controller.update({.pressed = action_mask(Action::down), .text_input = {}});
                controller.update({.pressed = action_mask(Action::confirm), .text_input = {}});
            } else if (page == 1) {
                controller.open(true, {});
                controller.update({.pressed = action_mask(Action::up), .text_input = {}});
                controller.update({.pressed = action_mask(Action::confirm), .text_input = {}});
            } else if (page == 2) {
                controller.open(true, {}, GameMode::time_trial);
            } else if (page == 3) {
                controller.update({.pressed = action_mask(Action::up), .text_input = {}});
                controller.update({.pressed = action_mask(Action::confirm), .text_input = {}});
                expect(controller.page() == scene::PausePage::quit_time_trial && controller.selection() == 1, "Time Trial confirmation must render with NO selected");
            }
        }
        controller.close();
        gpu::Frame empty;
        view.append(empty, controller, 640, 480);
        expect(empty.vertices.empty(), "Closed pause must leave scene rendering untouched");
        gpu::Frame remote;
        view.append(remote, controller, 640, 480, {}, "PAUSED BY : REMOTE");
        expect(!remote.vertices.empty() && !controller.active(), "A remote-only shared pause must render its requester without opening the local menu");
        for (const auto& vertex : remote.vertices) {
            expect(vertex.x >= 0 && vertex.x < 640 && vertex.y >= 57 && vertex.y < 140, "Shared pause status must stay below the top HUD and above the source local pause menu");
        }
        controller.open(true, {}, GameMode::linkup);
        gpu::Frame local, combined;
        view.append(local, controller, 640, 480);
        view.append(combined, controller, 640, 480, {}, "PAUSED BY : LOCAL, REMOTE");
        expect(combined.vertices.size() > local.vertices.size(), "Shared status must remain visible while the local pause menu is usable");
        for (std::size_t index = 0; index < local.vertices.size(); ++index) {
            expect(std::memcmp(&combined.vertices[index], &local.vertices[index], sizeof(gpu::Vertex)) == 0,
                "Shared pause status must preserve every original local pause-menu vertex");
        }
    }
    gpu::check(SDL_SetWindowSize(window.get(), 1920, 1080), "Resize Mega pause target");
    gpu::check(SDL_SyncWindow(window.get()), "Synchronize Mega pause target");
    SDL_PumpEvents();
    scene::PauseView mega(store, renderer, 0, true, artwork_directory);
    scene::PauseController controller;
    controller.open(true, {});
    gpu::Frame frame;
    mega.append(frame, controller, 1920, 1080);
    expect(renderer.draw(frame, false), "Private Mega pause target unavailable");
    renderer.save_screenshot("pause-mega-1920x1080.png");
    std::cout << "PASS: localized original pause panels/fonts, submenu rendering and three drawable aspects\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Usage: toca-pause-view-tests CANONICAL_ROOT ARTWORK_DIRECTORY");
        }
        reference(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
