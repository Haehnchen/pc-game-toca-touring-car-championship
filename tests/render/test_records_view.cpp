#include "assets/store.hpp"
#include "frontend/font.hpp"
#include "frontend/records.hpp"
#include "frontend/view.hpp"
#include "image/decode.hpp"
#include "render/gpu_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t differences(const std::vector<Uint32>& before, const std::vector<Uint32>& after, int x, int y, int width, int height) {
    std::size_t changed = 0;
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            changed += before.at(std::size_t(row * 640 + column)) != after.at(std::size_t(row * 640 + column));
        }
    }
    return changed;
}

void run(const char* root) {
    using namespace toca;
    using namespace toca::frontend;
    const AssetStore store(root);
    const auto defaults = load_default_lap_records();
    const auto championship_defaults = load_default_championship_records();
    const auto font = decode_font(store.open_archive("data/frontend.bfp").member(24).bytes());
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA original record pages", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private record view window");
    gpu::Renderer renderer(window.get());
    View view(store, renderer);
    view.load_frontend();
    for (std::uint32_t language = 0; language < 3; ++language) {
        Controller controller;
        controller.set_name_defaults(view.name_defaults());
        controller.set_lap_records(defaults);
        controller.set_championship_records(championship_defaults);
        const auto press = [&](Action action) {
            return controller.update({.pressed = action_mask(action), .text_input = {}}, {0, 1});
        };
        controller.update({}, {0, 5'000'000'000});
        controller.frontend_ready();
        for (std::uint32_t index = 0; index < language; ++index) {
            press(Action::down);
        }
        press(Action::confirm);
        press(Action::down);
        press(Action::down);
        press(Action::confirm);
        const auto render = [&](int width, int height, const std::string& capture = {}) {
            const auto elapsed = controller.animation_time_ns() - controller.menu_entered_ns();
            if (elapsed < 466'666'667) {
                controller.update({}, {0, 466'666'667 - elapsed});
            }
            gpu::check(SDL_SetWindowSize(window.get(), width, height), "Resize private record target");
            gpu::check(SDL_SyncWindow(window.get()), "Synchronize private record target");
            SDL_PumpEvents();
            expect(renderer.draw(view.frame(controller)), "Private record render target unavailable");
            const auto pixels = renderer.readback();
            expect(pixels.size() == std::size_t(width * height), "Record readback must use the actual drawable size");
            if (!capture.empty()) {
                renderer.save_screenshot(capture);
            }
            return pixels;
        };
        const auto mega_fonts = [&](const std::string& capture, std::size_t backdrop_quads) {
            auto settings = controller.settings_values();
            settings.graphics.detail = 3;
            controller.set_settings(settings, {{640, 480}});
            const auto original = render(640, 480);
            expect(view.frame(controller).draws.size() == backdrop_quads, "Ordinary profiles must retain their original indexed font path");
            settings.graphics.detail = graphics_mega_detail;
            controller.set_settings(settings, {{640, 480}, {1920, 1080}});
            const auto authored = view.frame(controller);
            const auto mega_backdrop_quads = backdrop_quads + (controller.screen() == Screen::championship_honors ? 0 : 1);
            expect(authored.draws.size() > mega_backdrop_quads, "Mega must draw authored font textures above the original canvas");
            std::size_t glyph_draws = 0;
            for (std::size_t draw = mega_backdrop_quads; draw < authored.draws.size(); ++draw) {
                const auto& state = authored.draws[draw].state;
                expect(state.blend && state.clamp && !state.key && !authored.draws[draw].viewport,
                    "Mega fonts and ordered canvas sprites must retain alpha and the centered UI viewport at every resolution");
                glyph_draws += state.linear;
            }
            expect(glyph_draws > 0, "Authored fonts must use linear filtering while original canvas sprites retain nearest sampling");
            const auto replacement = render(640, 480);
            expect(differences(original, replacement, 63, 78, 536, 340) > 100, "Mega records must display the authored fonts over the generated menu background and frame");
            render(1920, 1080, capture);
            settings.graphics.detail = 3;
            controller.set_settings(settings, {{640, 480}});
            expect(render(640, 480) == original, "Returning from Mega must restore every original font pixel and invalidate the canvas cache");
        };
        mega_fonts("mega-records-lang" + std::to_string(language) + ".png", 1);
        if (language == 0) {
            // Eleven wide letters are valid stored data and fit the original
            // driver column, extending behind the later manufacturer badge.
            auto overlapping = defaults;
            overlapping[0].driver = std::string(11, 'W');
            overlapping[0].car_id = 3;
            controller.set_lap_records(overlapping);
            const auto original = render(640, 480);
            auto settings = controller.settings_values();
            settings.graphics.detail = graphics_mega_detail;
            controller.set_settings(settings, {{640, 480}});
            const auto authored = view.frame(controller);
            expect(std::ranges::any_of(authored.draws.begin() + 2, authored.draws.end(),
                       [](const auto& draw) {
                           return draw.state.blend && draw.state.clamp && !draw.state.linear && !draw.state.key;
                       }),
                "Later record badges must replay after font quads with nearest sampling");
            const auto replacement = render(640, 480, "mega-records-overlapping-badge.png");
            const auto badge_atlas = toca::decode_bmp(store.open_archive("data/frontend.bfp").member(22).bytes());
            const auto badge = load_record_resources().badges.at(overlapping[0].car_id);
            const int badge_x = 551 + (48 - badge.width) / 2;
            std::size_t opaque_core = 0;
            for (int y = 1; y < badge.height - 1; ++y) {
                for (int x = 1; x < badge.width - 1; ++x) {
                    bool opaque = true;
                    for (int dy = -1; dy <= 1; ++dy) {
                        const auto* row = static_cast<const std::uint8_t*>(badge_atlas->pixels) + (badge.y + y + dy) * badge_atlas->pitch;
                        for (int dx = -1; dx <= 1; ++dx) {
                            opaque = opaque && row[badge.x + x + dx] != 0;
                        }
                    }
                    if (opaque) {
                        const auto pixel = std::size_t((124 + y) * 640 + badge_x + x);
                        expect(original[pixel] == replacement[pixel], "Original opaque badge cores must cover overlapping Mega driver text in the same painter order");
                        ++opaque_core;
                    }
                }
            }
            expect(opaque_core > 100 && differences(original, replacement, 333, 124, 218, font.line_height) > 20,
                "The overlap fixture must retain a substantial badge core while actually rendering the new driver font");
            settings.graphics.detail = 3;
            controller.set_settings(settings, {{640, 480}});
            expect(render(640, 480) == original, "Leaving Mega must remove ordered sprite overlays and restore the original overlapping record row");
            controller.set_lap_records(defaults);
        }
        std::array<std::vector<Uint32>, 2> pages;
        for (std::size_t page = 0; page < 2; ++page) {
            expect(controller.screen() == Screen::time_trial_records && controller.selection() == page, "Main High Scores must expose both source record pages");
            for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
                const auto capture =
                    "records-lang" + std::to_string(language) + "-page" + std::to_string(page) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png";
                const auto pixels = render(size[0], size[1], capture);
                if (size[0] == 640) {
                    pages[page] = pixels;
                }
                expect(std::ranges::count_if(pixels,
                           [&](auto pixel) {
                               return pixel != pixels.front();
                           })
                           > 10000,
                    "Localized record pages must contain the actual frontend artwork");
            }
            if (page == 0) {
                press(Action::confirm);
            }
        }
        expect(pages[0] != pages[1], "The two record pages must show different track rows and badges");
        press(Action::confirm);
        expect(controller.screen() == Screen::championship_records, "Main High Scores must append the original Championship table after both Time Trial pages");
        std::vector<Uint32> championship;
        for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
            auto pixels = render(size[0], size[1], "championship-records-lang" + std::to_string(language) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
            if (size[0] == 640) {
                championship = std::move(pixels);
            }
        }
        expect(differences(pages[1], championship, 63, 78, 536, 340) > 1000, "Championship must render its own nine-row Player/Points/Round table and four earned cups per driver");
        if (language == 0) {
            auto changed = championship_defaults;
            changed[0] = {"ALICE", 333, 22, {0, 1, 2, 3}};
            changed[8] = {"ZOE", 5, 1, {3, 2, 1, 0}};
            controller.set_championship_records(changed);
            const auto changed_page = render(640, 480, "championship-records-updated.png");
            const auto last_y = 122 + 8 * (17 + font.line_height);
            expect(differences(championship, changed_page, 63, 122, 220, font.line_height) > 20 && differences(championship, changed_page, 283, 122, 148, font.line_height) > 20
                       && differences(championship, changed_page, 431, 122, 80, font.line_height) > 20
                       && differences(championship, changed_page, 63, last_y, 220, font.line_height) > 20,
                "Changed stored names, points and literal rounds must invalidate the table cache at the first and ninth rows");
            auto gray = changed;
            gray[0].awards.fill(0);
            controller.set_championship_records(gray);
            const auto gray_page = render(640, 480);
            for (std::uint32_t block = 1; block < 4; ++block) {
                expect(differences(gray_page, changed_page, 513 + int(block) * 17, 122, 16, 16) > 4,
                    "Each original bronze, silver and gold cup must render at its own independent quarter-season position");
            }
            expect(differences(gray_page, changed_page, 513, 122, 16, 16) == 0, "Tier0 must retain the actual gray cup rather than inventing an empty icon");
            controller.set_championship_records(changed);
            controller.update({}, {0, 1'133'333'334});
            const auto flashed = render(640, 480, "championship-records-highlight.png");
            expect(differences(changed_page, flashed, 63, 122, 440, font.line_height) > 20
                       && differences(changed_page, flashed, 63, 122 + 17 + font.line_height, 536, font.line_height) == 0,
                "The source flash callback must recolor only the first record row");
            controller.set_championship_records(changed, "damaged private fixture");
            const auto failed = render(640, 480, "championship-records-file-error.png");
            expect(differences(flashed, failed, 41, 440, 558, 20) > 20 && differences(flashed, failed, 63, 122, 536, 300) == 0,
                "A damaged table must show the localized file-error notice while keeping all loaded/default rows visible");
            controller.set_championship_records(changed);
            const auto repaired = render(640, 480);
            expect(differences(flashed, repaired, 41, 440, 558, 20) == 0, "Reloading a repaired table must remove the old file-error caption");
            controller.set_championship_records(championship_defaults);
        }
        press(Action::back);
        expect(controller.screen() == Screen::time_trial_records && controller.selection() == 1, "Championship Escape must restore Time Trial page two");
        auto state = controller.cheats_state();
        state.overrides.bonus_track = true;
        controller.set_cheats_state(state);
        const auto lava = render(640, 480, "records-lang" + std::to_string(language) + "-lavaland.png");
        const auto lava_y = 124 + 4 * (21 + font.line_height * 2);
        expect(differences(pages[1], lava, 63, lava_y, 536, 45) > 100, "Unlocking Lavaland must expose its original final record row");
        press(Action::back);
        if (language == 0) {
            auto improved = defaults;
            improved[0] = {83519, "DANIEL", 1, "ABC123"};
            controller.set_lap_records(improved, 0);
            const auto result = render(640, 480, "records-improved.png");
            expect(differences(pages[0], result, 188, 124, 145, 18) > 20 && differences(pages[0], result, 188, 142, 145, 16) > 20
                       && differences(pages[0], result, 551, 124, 48, 43) > 20,
                "An improved record must render its real time, verification code and saved car badge");
            controller.set_lap_records(defaults);
            press(Action::back); // Main High Scores.
            press(Action::up);
            press(Action::up);
            for (int step = 0; step < 6; ++step) {
                press(Action::confirm);
            }
            expect(controller.screen() == Screen::track_selection, "Record fixture must use the ordinary selection route");
            const auto track_before = render(640, 480);
            controller.set_lap_records(improved);
            const auto track_after = render(640, 480, "track-saved-record.png");
            expect(differences(track_before, track_after, 350, 345, 249, 55) > 50, "Track selection must display the same saved lap, driver and car as High Scores");
        }
        // Honors combines BIG640 headings and LIL640 rows, with the original
        // white face/black edge override applied to both fonts.
        controller.show_championship_honors({.block = 0, .tier = 1, .podiums = {1, 2, 3}, .rewards = {}});
        mega_fonts("mega-honors-lang" + std::to_string(language) + ".png", 2);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected canonical data root");
        }
        toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private record view");
        run(argv[1]);
        SDL_Quit();
        std::cout << "Original localized Time Trial and Championship record pages, badges, codes, cups, storage notices and three aspects passed\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
