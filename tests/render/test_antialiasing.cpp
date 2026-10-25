#include "render/gpu_renderer.hpp"

#include <SDL3/SDL_main.h>

#include <iostream>

namespace {
using namespace toca::gpu;
using toca::platform::Extent;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void triangle(Frame& frame, const std::array<std::array<float, 2>, 3>& points, Uint32 color) {
    const auto offset = frame.batch_vertex_offset(3, 3);
    const auto base = Uint16(frame.vertices.size() - std::size_t(offset));
    frame.draws.push_back({{}, Uint32(frame.indices.size()), 3, offset});
    for (const auto& point : points) {
        frame.vertices.push_back({point[0], point[1], .5f, 1, color, 0xff000000, 0, 0});
    }
    frame.indices.insert(frame.indices.end(), {base, Uint16(base + 1), Uint16(base + 2)});
}

Frame diagonal(Extent extent) {
    Frame frame;
    const auto width = float(extent.width), height = float(extent.height);
    frame.quad({}, 0, 0, width, height, 0xff000000);
    triangle(frame, {{{width * .07f, height * .06f}, {width * .81f, height * .23f}, {width * .10f, height * .65f}}}, 0xffffffff);
    const auto overlay = frame.draws.size();
    // A diagonal overlay tests its actual edge pixels, not only its interior.
    // Its entire bounding box sits over a uniform part of the black world.
    triangle(frame, {{{width * .61f, height * .74f}, {width * .94f, height * .81f}, {width * .72f, height * .95f}}}, 0xffff00ff);
    frame.mark_overlay(overlay);
    return frame;
}

std::vector<Uint32> render(Renderer& renderer, const Frame& frame, Extent extent, bool ui = false) {
    require(renderer.draw_offscreen(frame, extent, ui), "Offscreen frame must render");
    return renderer.readback();
}

Uint32 diagonal_and_overlay(Renderer& renderer) {
    Uint32 mega_samples = 0;
    for (const auto extent : {Extent{256, 160}, Extent{301, 181}}) {
        const auto normal = diagonal(extent);
        const auto original = render(renderer, normal, extent);
        require(renderer.scene_sample_count() == 1, "Normal scene profiles must retain single-sample rendering");
        auto mega = normal;
        mega.multisample = true;
        const auto filtered = render(renderer, mega, extent);
        require(renderer.scene_sample_count() == 1 || renderer.scene_sample_count() == 2, "Mega must use exactly2 samples or the supported single-sample fallback");
        require(mega_samples == 0 || mega_samples == renderer.scene_sample_count(), "Resizing must retain the supported Mega sample count");
        mega_samples = renderer.scene_sample_count();
        std::size_t partial = 0, changed = 0;
        for (Uint32 y = 0; y < extent.height; ++y) {
            for (Uint32 x = 0; x < extent.width; ++x) {
                const auto index = std::size_t(y) * extent.width + x;
                if (y < extent.height * .69f) {
                    const auto value = filtered[index];
                    const auto red = (value >> 16) & 255, green = (value >> 8) & 255, blue = value & 255;
                    partial += red > 0 && red < 255 && red == green && green == blue;
                    changed += value != original[index];
                    require(original[index] == 0xff000000 || original[index] == 0xffffffff, "Unfiltered fixture must have hard black/white geometric edges");
                }
                if (y > extent.height * .70f) {
                    require(filtered[index] == original[index], "Antialiasing must leave explicit overlay edges and their uniform background unchanged");
                }
            }
        }
        if (renderer.scene_sample_count() == 2) {
            require(partial >= 8 && changed >= 8, "Mega2x MSAA must produce real intermediate coverage on the diagonal primitive");
        } else {
            require(filtered == original, "Unsupported2x hardware must still render the correct single-sample scene");
        }
        require(render(renderer, mega, extent) == filtered, "Repeated Mega rendering must retain exact pixels after resizing");
        require(render(renderer, normal, extent) == original && renderer.scene_sample_count() == 1,
            "Returning from Mega to a normal profile must restore its exact single-sample pixels");
    }
    return mega_samples;
}

Frame pane(Uint32 width, Uint32 height, Uint32 color) {
    Frame result;
    result.quad({}, 0, 0, float(width), float(height), color);
    triangle(result, {{{float(width) * .25f, float(height) * .2f}, {float(width) * .7f, float(height) * .3f}, {float(width) * .4f, float(height) * .65f}}}, 0xffffffff);
    const auto overlay = result.draws.size();
    triangle(result, {{{float(width) * .2f, float(height) * .78f}, {float(width) * .5f, float(height) * .8f}, {float(width) * .3f, float(height) * .9f}}}, 0xff00ff00);
    result.mark_overlay(overlay);
    return result;
}

void split_boundaries(Renderer& renderer, Uint32 mega_samples) {
    constexpr Extent extent{127, 97};
    for (const auto vertical : {false, true}) {
        const auto first_width = vertical ? extent.width / 2 : extent.width;
        const auto first_height = vertical ? extent.height : extent.height / 2;
        const Viewport first{0, 0, first_width, first_height};
        const Viewport second{vertical ? first_width : 0, vertical ? 0 : first_height, extent.width - (vertical ? first_width : 0), extent.height - (vertical ? 0 : first_height)};
        Frame combined;
        combined.append_view(pane(first.width, first.height, 0xffff0000), first);
        combined.append_view(pane(second.width, second.height, 0xff0000ff), second);
        auto first_mega = pane(first.width, first.height, 0xffff0000);
        auto second_mega = pane(second.width, second.height, 0xff0000ff);
        first_mega.multisample = second_mega.multisample = true;
        Frame mega;
        mega.append_view(first_mega, first);
        mega.append_view(second_mega, second);
        Frame overlay;
        overlay.quad({}, 4, 4, 4, 4, 0xffffff00);
        overlay.mark_overlay(0);
        mega.append_view(overlay, {0, 0, extent.width, extent.height});
        // Appending an ordinary fullscreen HUD must retain the panes' policy.
        combined.append_view(overlay, {0, 0, extent.width, extent.height});
        const auto with_overlay = render(renderer, combined, extent);
        require(renderer.scene_sample_count() == 1, "Normal split views must remain single-sample");
        const auto filtered = render(renderer, mega, extent);
        require(renderer.scene_sample_count() == mega_samples, "Mega pane sampling must survive composition with an ordinary fullscreen HUD");
        // Both sides of an odd-sized split must retain their exact solid pane
        // color. Geometry in one view must not affect samples in the other.
        const auto length = vertical ? extent.height : extent.width;
        for (Uint32 along = 0; along < length; ++along) {
            for (int distance = -2; distance < 2; ++distance) {
                const auto cross = Uint32(int(vertical ? first_width : first_height) + distance);
                const auto index = vertical ? std::size_t(along) * extent.width + cross : std::size_t(cross) * extent.width + along;
                require(filtered[index] == (distance < 0 ? 0xffff0000u : 0xff0000ffu), "Split pane boundaries must not bleed colors across views");
            }
        }
        // append_view must also preserve overlay classification in each pane.
        for (const auto viewport : {first, second}) {
            for (Uint32 y = Uint32(float(viewport.height) * .73f) + 1; y < viewport.height; ++y) {
                for (Uint32 x = 0; x < viewport.width; ++x) {
                    const auto index = std::size_t(viewport.y + y) * extent.width + viewport.x + x;
                    require(filtered[index] == with_overlay[index], "Composed split overlays must retain exact unfiltered pixels");
                }
            }
        }
        require(filtered[5 * extent.width + 5] == 0xffffff00, "Fullscreen HUD appended after Mega panes must retain its own pixels");
        if (renderer.scene_sample_count() == 2) {
            require(filtered != with_overlay, "Mega sampling must propagate from both source views through later HUD composition");
        }
        require(render(renderer, combined, extent) == with_overlay && renderer.scene_sample_count() == 1, "Normal split rendering must remain unchanged after Mega composition");
    }
}

void ui_unchanged(Renderer& renderer) {
    const Extent extent{640, 480};
    auto frame = diagonal(extent);
    const auto original = render(renderer, frame, extent);
    frame.multisample = true;
    require(render(renderer, frame, extent, true) == original, "UI must retain the single-sample artwork and diagonal edge pixels");
    require(renderer.scene_sample_count() == 1, "Mega scene metadata must not enable multisampling in UI");
    // Switch between world sampling and UI at the same extent, then resize.
    // UI draws ignore scene overlay classification and retain source ordering.
    render(renderer, frame, extent);
    frame.mark_overlay(0);
    require(render(renderer, frame, extent, true) == original, "Returning from a scene must preserve exact UI pixels and draw order");
    const auto smaller = render(renderer, frame, {320, 240}, true);
    auto small_scene = diagonal({320, 240});
    small_scene.multisample = true;
    render(renderer, small_scene, {320, 240});
    require(render(renderer, frame, {320, 240}, true) == smaller, "UI resizing must remain stable across scene draws");
}
} // namespace

int main() {
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated antialiasing video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA antialiasing checks", 320, 240, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        check(bool(window), "Create isolated antialiasing window");
        Renderer renderer(window.get());
        const auto mega_samples = diagonal_and_overlay(renderer);
        split_boundaries(renderer, mega_samples);
        ui_unchanged(renderer);
        std::cout << "PASS: normal1x and Mega" << mega_samples << "x scene policy, geometric coverage, overlays, resize, split seams and unchanged UI\n";
        renderer.wait_idle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
