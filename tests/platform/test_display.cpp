#include "platform/display.hpp"
#include "render/gpu_renderer.hpp"

#include <SDL3/SDL_main.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool result, const char* message) {
    if (!result) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

void mapping() {
    for (const auto extent : {toca::platform::Extent{640, 480}, {1280, 720}, {1280, 800}, {1554, 1110}}) {
        const auto transform = toca::platform::ui_transform(extent);
        const auto center = toca::platform::logical_pointer(float(extent.width) * .5f, float(extent.height) * .5f, extent);
        expect(center.inside && std::abs(center.x - 320) < .001f && std::abs(center.y - 240) < .001f, "Inverse UI center changed");
        const auto corner = toca::platform::logical_pointer(transform.x + transform.scale * 10, transform.y + transform.scale * 20, extent);
        expect(corner.inside && std::abs(corner.x - 10) < .001f && std::abs(corner.y - 20) < .001f, "Inverse UI scale changed");
        expect(!toca::platform::logical_pointer(transform.x - 1, transform.y, extent).inside, "Unused side area became an input hit region");
    }
    expect(!toca::platform::logical_pointer(0, 0, {}).inside, "Zero drawable extent mapped input");
}

void verify_frame(toca::platform::Display& display, toca::gpu::Renderer& renderer, const toca::gpu::Frame& frame) {
    SDL_PumpEvents();
    expect(renderer.draw(frame), "Visible window had no swapchain");
    const auto extent = renderer.drawable_extent();
    int pixel_width = 0, pixel_height = 0, window_width = 0, window_height = 0;
    expect(SDL_GetWindowSizeInPixels(display.window(), &pixel_width, &pixel_height), "Query drawable dimensions");
    expect(SDL_GetWindowSize(display.window(), &window_width, &window_height), "Query window dimensions");
    expect(extent.width == unsigned(pixel_width) && extent.height == unsigned(pixel_height), "Render targets differ from acquired drawable size");
    const auto pixels = renderer.readback();
    expect(pixels.size() == std::size_t(pixel_width * pixel_height), "Readback used a fixed resolution");
    auto pixel = [&](int x, int y) {
        return pixels.at(std::size_t(y) * extent.width + std::size_t(x));
    };
    expect(pixel(pixel_width / 2, pixel_height / 2) == 0xff00c000, "Uniform UI center moved");
    const auto transform = toca::platform::ui_transform(extent);
    expect(pixel(int(std::ceil(transform.x)) + 2, pixel_height / 2) == 0xffc00000, "Authored UI edge was cropped");
    if (transform.x >= 2) {
        expect(pixel(int(transform.x) - 2, pixel_height / 2) == toca::gpu::background, "Authored image stretched into side areas");
    }
    const auto pointer = display.map_pointer(float(window_width) * .5f, float(window_height) * .5f, extent);
    expect(pointer.inside && std::abs(pointer.x - 320) < .001f && std::abs(pointer.y - 240) < .001f, "Window-to-pixel input scaling differs from display");
}

Uint32 backdrop_pixel(int x, int y) {
    return 0xff000000 | (Uint32(32 + x / 4) << 16) | (Uint32(48 + y / 4) << 8) | Uint32(64 + (x / 16 + y / 16) % 128);
}

toca::gpu::Texture backdrop_texture(toca::gpu::Renderer& renderer) {
    std::vector<Uint8> rgba(640 * 480 * 4);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            const auto color = backdrop_pixel(x, y);
            const auto offset = std::size_t(y * 640 + x) * 4;
            rgba[offset] = Uint8(color >> 16);
            rgba[offset + 1] = Uint8(color >> 8);
            rgba[offset + 2] = Uint8(color);
            rgba[offset + 3] = 255;
        }
    }
    auto texture = renderer.texture(640, 480);
    renderer.update(texture.get(), rgba.data(), 640, 480);
    return texture;
}

void verify_backdrop(toca::gpu::Renderer& renderer, const toca::gpu::Frame& plain, SDL_GPUTexture* texture) {
    auto extended = plain;
    extended.side_backdrop = texture;
    SDL_PumpEvents();
    // This must be the first draw after resizing: only the acquired swapchain
    // can supply the current side widths.
    expect(renderer.draw(extended), "Draw extended backdrop after resize");
    const auto extent = renderer.drawable_extent();
    const auto pixels = renderer.readback();
    expect(renderer.draw(plain), "Draw baseline without optional backdrop");
    const auto baseline = renderer.readback();
    const auto transform = toca::platform::ui_transform(extent);
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            const auto offset = std::size_t(y) * extent.width + x;
            const auto center = float(x) + .5f;
            if (center >= transform.x && center < transform.x + transform.width) {
                expect(pixels[offset] == baseline[offset], "Optional backdrop changed a centered UI pixel");
            } else {
                expect(baseline[offset] == toca::gpu::background, "Omitted backdrop changed unused side areas");
                expect(pixels[offset] != toca::gpu::background, "Extended backdrop left a flat side pixel");
            }
        }
    }
    if (transform.x < 4) {
        expect(pixels == baseline, "Backdrop changed a 4:3 frame without side areas");
        return;
    }
    for (const float fraction : {.125f, .5f, .875f}) {
        const auto left_x = int(transform.x * fraction);
        const auto right_x = int(extent.width) - 1 - left_x;
        const auto source_x = int(std::floor((transform.x - float(left_x) - .5f) / transform.scale));
        for (const int y : {int(extent.height / 4), int(extent.height * 3 / 4)}) {
            const auto source_y = int(std::floor((float(y) + .5f) / transform.scale));
            expect(pixels.at(std::size_t(y) * extent.width + unsigned(left_x)) == backdrop_pixel(source_x, source_y), "Left backdrop lost reflected authored scale");
            expect(pixels.at(std::size_t(y) * extent.width + unsigned(right_x)) == backdrop_pixel(639 - source_x, source_y), "Right backdrop lost reflected authored scale");
        }
    }
    expect(renderer.draw(extended, false), "Draw scene with optional backdrop field");
    const auto scene = renderer.readback();
    expect(renderer.draw(plain, false), "Draw scene baseline");
    expect(renderer.readback() == scene, "Menu backdrop changed full-drawable scene rendering");
}

void caps(toca::platform::Display& display) {
    auto settings = display.settings();
    for (const auto cap : {30u, 60u, 120u}) {
        settings.fps_limit = cap;
        display.apply(settings);
        const auto start = SDL_GetTicksNS();
        for (int frame = 0; frame < 4; ++frame) {
            display.pace(SDL_GetTicksNS());
        }
        const double elapsed = double(SDL_GetTicksNS() - start) / 1'000'000.0;
        const double expected = 4000.0 / cap;
        expect(elapsed >= expected - 3 && elapsed <= expected + 60, "Frame cap departed from target interval (3ms early/60ms scheduling allowance)");
        std::cout << "Cap " << cap << ": " << elapsed / 4 << " ms/frame\n";
    }
    const auto start = SDL_GetTicksNS();
    display.pace(start - std::min(start, std::uint64_t(25'000'000)));
    expect(SDL_GetTicksNS() - start < 15'000'000, "Pacer added a full interval after presentation already blocked");
    settings.fps_limit = 0;
    display.apply(settings);
}
} // namespace

int main() {
    try {
        mapping();
        expect(SDL_Init(SDL_INIT_VIDEO), "Initialize display test");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        toca::platform::Display display("TOCA display checks", {.fullscreen = false});
        toca::gpu::Renderer renderer(display.window());
        expect(renderer.set_vsync(true), "VSync mode unavailable");
        const bool fallback = renderer.set_vsync(false);
        std::cout << "VSync-off effective: " << (fallback ? "VSync (unsupported immediate)" : "immediate") << '\n';
        toca::gpu::Frame frame;
        frame.quad({}, 0, 0, 640, 480, 0xffc00000);
        frame.quad({}, 310, 230, 20, 20, 0xff00c000);
        const auto backdrop = backdrop_texture(renderer);
        for (const auto extent : {toca::platform::Extent{640, 480}, {1280, 720}, {1280, 800}}) {
            auto settings = display.settings();
            settings.width = extent.width;
            settings.height = extent.height;
            display.apply(settings);
            verify_backdrop(renderer, frame, backdrop.get());
            verify_frame(display, renderer, frame);
        }
        expect(SDL_SetWindowSize(display.window(), 777, 555) && SDL_SyncWindow(display.window()), "Live resize window");
        verify_backdrop(renderer, frame, backdrop.get());
        verify_frame(display, renderer, frame);
        expect(renderer.drawable_extent().width == 777 && renderer.drawable_extent().height == 555, "Live resize did not rebuild drawable resources");
        expect(display.settings().width == 777 && display.settings().height == 555, "Effective settings retained stale dimensions after a live resize");
        expect(SDL_HideWindow(display.window()) && SDL_SyncWindow(display.window()), "Hide window");
        SDL_PumpEvents();
        expect((SDL_GetWindowFlags(display.window()) & SDL_WINDOW_HIDDEN) != 0, "Window did not become hidden");
        (void)renderer.draw(frame);
        expect(SDL_ShowWindow(display.window()) && SDL_SyncWindow(display.window()), "Show window");
        verify_frame(display, renderer, frame);
        caps(display);
        expect(renderer.set_vsync(true), "Restore VSync mode");
        verify_frame(display, renderer, frame);
        renderer.save_screenshot("display-restored.png");
        std::cout << "PASS: three aspects, reflected menu backdrop, drawable UI/input, live resize, hide/show, VSync and presentation caps\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
