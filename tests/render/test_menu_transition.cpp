#include "assets/store.hpp"
#include "frontend/controller.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/view.hpp"
#include "render/gpu_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct TextAnchor {
    SDL_GPUTexture* texture;
    float u, v, x, y;
};

std::array<TextAnchor, 2> text_anchors(const toca::gpu::Frame& stable, toca::frontend::MenuRegion region, SDL_GPUTexture* metal) {
    const auto& window = toca::frontend::menu_transition_regions[std::size_t(region)];
    for (const auto& draw : stable.draws) {
        if (!draw.state.blend || !draw.state.linear || draw.state.texture == metal || draw.count != 6) {
            continue;
        }
        const auto vertex = [&](std::size_t corner) -> const toca::gpu::Vertex& {
            return stable.vertices.at(std::size_t(draw.vertex_offset) + stable.indices.at(draw.first + corner));
        };
        const auto& top_left = vertex(0);
        const auto& bottom_right = vertex(2);
        const auto left = std::max(top_left.x + .5f, window.x), right = std::min(bottom_right.x + .5f, window.x + window.width);
        const auto top = std::ceil(std::max(top_left.y + .5f, window.y)), bottom = std::floor(std::min(bottom_right.y + .5f, window.y + window.height));
        if (left >= right || top >= bottom) {
            continue;
        }
        const auto anchor = [&](float x, float y) {
            const auto u = std::lerp(top_left.u, bottom_right.u, (x - top_left.x - .5f) / (bottom_right.x - top_left.x));
            const auto v = std::lerp(top_left.v, bottom_right.v, (y - top_left.y - .5f) / (bottom_right.y - top_left.y));
            return TextAnchor{draw.state.texture, u, v, x, y};
        };
        return {anchor(left, top), anchor(right, bottom)};
    }
    throw std::runtime_error("Stable Mega title and help must supply actual glyph anchors");
}

std::optional<toca::frontend::MenuPoint> metal_point(const toca::gpu::Frame& animated, SDL_GPUTexture* metal, float u, float v) {
    for (const auto& draw : animated.draws) {
        if (draw.state.texture != metal) {
            continue;
        }
        for (std::size_t index = draw.first; index + 2 < std::size_t(draw.first) + draw.count; index += 3) {
            const auto vertex = [&](std::size_t corner) -> const toca::gpu::Vertex& {
                return animated.vertices.at(std::size_t(draw.vertex_offset) + animated.indices.at(index + corner));
            };
            const auto& a = vertex(0);
            const auto& b = vertex(1);
            const auto& c = vertex(2);
            const auto denominator = (b.v - c.v) * (a.u - c.u) + (c.u - b.u) * (a.v - c.v);
            if (std::abs(denominator) < 1e-12f) {
                continue;
            }
            const auto wa = ((b.v - c.v) * (u - c.u) + (c.u - b.u) * (v - c.v)) / denominator;
            const auto wb = ((c.v - a.v) * (u - c.u) + (a.u - c.u) * (v - c.v)) / denominator;
            const auto wc = 1 - wa - wb;
            if (wa >= -1e-5f && wb >= -1e-5f && wc >= -1e-5f) {
                return toca::frontend::MenuPoint{wa * a.x + wb * b.x + wc * c.x, wa * a.y + wb * b.y + wc * c.y};
            }
        }
    }
    return std::nullopt;
}

void anchored_text(const toca::gpu::Frame& animated, const toca::gpu::Frame& stable, SDL_GPUTexture* metal, bool edge) {
    using namespace toca::frontend;
    for (const auto region : {MenuRegion::title, MenuRegion::help}) {
        for (const auto& anchor : text_anchors(stable, region, metal)) {
            if (edge) {
                expect(std::none_of(animated.draws.begin(), animated.draws.end(),
                           [&](const auto& draw) {
                               return draw.state.texture == anchor.texture;
                           }),
                    "Edge-on Mega frame must omit its title and help glyphs");
                continue;
            }
            // Measure the position on the actual transformed metal mesh. This
            // checks relative attachment without repeating the roll tables.
            const auto expected = metal_point(animated, metal, anchor.x / 640, anchor.y / 480);
            expect(expected.has_value(), "Animated metal must retain the title and help attachment points");
            bool attached = false;
            for (const auto& draw : animated.draws) {
                if (draw.state.texture != anchor.texture) {
                    continue;
                }
                for (std::size_t index = draw.first; index < std::size_t(draw.first) + draw.count; ++index) {
                    const auto& vertex = animated.vertices.at(std::size_t(draw.vertex_offset) + animated.indices.at(index));
                    attached |= std::abs(vertex.u - anchor.u) < 1e-5f && std::abs(vertex.v - anchor.v) < 1e-5f && std::abs(vertex.x - expected->x) < .05f
                                && std::abs(vertex.y - expected->y) < .05f;
                }
            }
            expect(attached, region == MenuRegion::title ? "Mega headline must remain attached to the turning metal frame in every phase"
                                                         : "Mega help must remain attached to the turning metal frame in every phase");
        }
    }
}

void visible_outer_rails(toca::gpu::Renderer& renderer, const toca::gpu::Frame& animated, SDL_GPUTexture* metal) {
    auto rails = animated;
    std::erase_if(rails.draws, [&](const auto& draw) {
        return draw.state.texture != metal;
    });
    expect(rails.background_image.has_value() && !rails.draws.empty(), "Mega turn must provide its photograph and actual metal frame");
    auto background = rails;
    background.draws.clear();
    expect(renderer.draw(background), "Draw private photograph baseline for outer rails");
    const auto baseline = renderer.readback();
    expect(renderer.draw(rails), "Draw private turning metal over its photograph");
    const auto pixels = renderer.readback();
    const auto extent = renderer.drawable_extent();
    // The authored canvas stays centered at its original height scale. A
    // turning rail can extend beyond either side and must reach real pixels.
    // Isolate the metal so text or a changed background cannot satisfy this.
    const auto canvas_width = float(extent.height) * 640.f / 480.f;
    const auto left = (float(extent.width) - canvas_width) / 2;
    const auto right = left + canvas_width;
    std::size_t left_pixels = 0, right_pixels = 0;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto offset = std::size_t(y) * extent.width + x;
            if (pixels[offset] != baseline[offset]) {
                left_pixels += float(x) + .5f < left - 1;
                right_pixels += float(x) + .5f > right + 1;
            }
        }
    }
    expect(left_pixels > 100, "Turning Mega left rail must remain visible beyond the centered 4:3 canvas at 19:9");
    expect(right_pixels > 100, "Turning Mega right rail must remain visible beyond the centered 4:3 canvas at 19:9");
    expect(renderer.draw(background) && renderer.readback() == baseline, "Reused UI photograph storage must restore its exact pixels after the turning rails");
}

void run(const char* root) {
    using namespace toca;
    using namespace toca::frontend;
    const AssetStore store(root);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA menu transition", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private menu transition window");
    gpu::Renderer renderer(window.get());
    for (const auto detail : {3u, graphics_mega_detail}) {
        View view(store, renderer);
        view.load_frontend();
        Controller controller;
        Settings settings;
        settings.graphics.detail = detail;
        controller.set_settings(settings, {{640, 480}});
        const auto advance = [&](std::uint64_t elapsed) {
            controller.update({}, {0, elapsed});
        };
        const auto press = [&](Action action) {
            controller.update({.pressed = action_mask(action), .text_input = {}}, {0, 1});
        };
        const auto frame = [&](const std::string& name = {}) {
            auto result = view.frame(controller);
            for (const auto& vertex : result.vertices) {
                expect(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) && std::isfinite(vertex.rhw), "Menu turn must retain finite GPU geometry");
            }
            expect(renderer.draw(result), "Private transition drawable unavailable");
            const auto pixels = renderer.readback();
            if (!name.empty()) {
                renderer.save_screenshot((detail == graphics_mega_detail ? "mega-" : "original-") + name + ".png");
            }
            return std::pair{std::move(result), pixels};
        };
        controller.update({}, {0, 5'000'000'000});
        controller.frontend_ready();
        frame();
        advance(300'000'000);
        frame();
        press(Action::confirm);
        frame();
        advance(466'666'667);
        frame();
        expect(controller.screen() == Screen::main_menu, "Language must lead into Main through its startup roll");
        for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}, std::array{1140, 540}}) {
            gpu::check(SDL_SetWindowSize(window.get(), size[0], size[1]), "Resize private transition target");
            gpu::check(SDL_SyncWindow(window.get()), "Synchronize transition target");
            SDL_PumpEvents();
            const auto suffix = std::to_string(size[0]) + "x" + std::to_string(size[1]);
            const auto main = frame("main-" + suffix);
            const auto& stable = main.second;
            press(Action::confirm);
            frame();
            advance(100'000'000);
            const auto forward = frame("forward-" + suffix);
            expect(forward.first.vertices.size() > 1000, "A page change must send the row-warped menu to the GPU");
            const auto changed = std::count_if(stable.begin(), stable.end(), [&, index = std::size_t(0)](auto pixel) mutable {
                return pixel != forward.second[index++];
            });
            expect(changed > size[0] * size[1] / 8, "The turn must visibly move the frame and menu contents");
            if (detail == graphics_mega_detail && size[0] == 1140) {
                visible_outer_rails(renderer, forward.first, main.first.draws.front().state.texture);
            }
            advance(100'000'000);
            const auto edge = frame("edge-" + suffix);
            expect(edge.first.draws.size() < forward.first.draws.size(), "Phase6 must omit every dynamic content region");
            advance(100'000'000);
            frame("incoming-" + suffix);
            advance(166'666'667);
            const auto settled = frame("race-" + suffix);
            expect(controller.screen() == Screen::race_menu && settled.first.vertices.size() < 2000, "The turn must settle on ordinary unwarped menu quads");
            press(Action::down);
            expect(frame().first.vertices.size() < 2000, "Selection navigation must not start a page turn");
            press(Action::up);
            frame();
            press(Action::back);
            frame();
            advance(100'000'000);
            const auto backward = frame("backward-" + suffix);
            expect(backward.second != forward.second && backward.first.vertices.size() > 1000, "Back must rotate through the opposite source phases");
            if (detail == graphics_mega_detail && size[0] == 1140) {
                visible_outer_rails(renderer, backward.first, main.first.draws.front().state.texture);
            }
            advance(366'666'667);
            frame();
            expect(controller.screen() == Screen::main_menu, "Back must restore Main after the turn");
        }
        if (detail == graphics_mega_detail) {
            const auto main = view.frame(controller);
            auto* const metal = main.draws.front().state.texture;
            press(Action::confirm);
            view.frame(controller);
            advance(466'666'667);
            const auto race = view.frame(controller);
            press(Action::back);
            view.frame(controller);
            advance(466'666'667);
            view.frame(controller);
            for (const int direction : {1, -1}) {
                press(direction == 1 ? Action::confirm : Action::back);
                std::uint64_t previous = 0;
                for (std::uint32_t step = 0; step < 14; ++step) {
                    const auto elapsed = (std::uint64_t(step) * 1'000'000'000 + 29) / 30;
                    advance(elapsed - previous);
                    previous = elapsed;
                    const auto phase = std::uint32_t((direction * int(step) + 14) % 14);
                    const bool outgoing = phase == 0 || (direction == 1 ? phase < 6 : phase >= 6);
                    const auto& snapshot = (direction == 1) == outgoing ? main : race;
                    const auto animated = direction == 1 && phase == 10 ? frame("headline-phase10").first : view.frame(controller);
                    anchored_text(animated, snapshot, metal, phase == 6);
                }
                advance(466'666'667 - previous);
                view.frame(controller);
            }
            expect(controller.screen() == Screen::main_menu, "All-phase attachment checks must preserve forward and backward menu navigation");
        }
        // Changing the target again while a roll is active must retain live
        // snapshot/font/icon resources and finish on the latest requested page.
        press(Action::confirm);
        frame();
        advance(100'000'000);
        frame();
        press(Action::back);
        frame();
        advance(100'000'000);
        frame();
        advance(366'666'667);
        frame();
        expect(controller.screen() == Screen::main_menu, "Fast confirm/back must retain the existing input behavior");
        press(Action::down);
        press(Action::down);
        press(Action::confirm);
        frame();
        advance(466'666'667);
        frame();
        press(Action::confirm);
        frame();
        advance(100'000'000);
        expect(controller.screen() == Screen::time_trial_records && controller.selection() == 1 && frame().first.vertices.size() > 1000,
            "Explicit record-page changes must turn even when both pages share a controller screen");

        // Exercise arbitrary miniature triangles as well as the menu quads.
        // One existing aspect suffices; the menu loop already covers all four.
        gpu::check(SDL_SetWindowSize(window.get(), 640, 480), "Resize private miniature transition target");
        gpu::check(SDL_SyncWindow(window.get()), "Synchronize miniature transition target");
        SDL_PumpEvents();
        const auto settle = [&] {
            frame();
            advance(466'666'667);
            frame();
        };
        press(Action::back);
        settle();
        press(Action::back);
        settle();
        expect(controller.screen() == Screen::main_menu && controller.selection() == 2, "Records Back must restore the main menu before entering the track route");
        press(Action::up);
        press(Action::up);
        for (const auto screen : {Screen::race_menu, Screen::single_options, Screen::difficulty, Screen::name_entry, Screen::car_selection}) {
            press(Action::confirm);
            expect(controller.screen() == screen, "Miniature regression must use the normal menu-to-garage route");
            settle();
        }
        const auto miniature = [](const gpu::Frame& animated) {
            std::size_t depth_vertices = 0;
            for (const auto& draw : animated.draws) {
                if (!draw.state.depth) {
                    continue;
                }
                for (std::size_t index = draw.first; index < std::size_t(draw.first) + draw.count; ++index) {
                    const auto& vertex = animated.vertices.at(std::size_t(draw.vertex_offset) + animated.indices.at(index));
                    expect(vertex.z > 0 && vertex.rhw > 0 && vertex.rhw < 1, "Clipped miniature triangles must preserve their actual projected depth and reciprocal W");
                    ++depth_vertices;
                }
            }
            expect(depth_vertices > 0, "Track turns must submit actual depth-tested miniature geometry");
        };
        press(Action::confirm);
        expect(controller.screen() == Screen::track_selection, "Garage confirmation must enter the track miniature");
        frame();
        advance(333'333'334); // Forward phase10 contains the incoming Track page.
        miniature(frame("miniature-incoming-640x480").first);
        advance(133'333'333);
        frame();
        press(Action::back);
        frame();
        advance(100'000'000); // Reverse phase11 still contains the outgoing Track page.
        miniature(frame("miniature-outgoing-640x480").first);
        advance(366'666'667);
        frame();
        expect(controller.screen() == Screen::car_selection, "Miniature Back must settle on the garage");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "Expected canonical data root");
        toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private menu transition view");
        run(argv[1]);
        SDL_Quit();
        std::cout << "Original/Mega menu turns, forward/back, edge-on content, rapid input, outer rails and four aspects passed\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
