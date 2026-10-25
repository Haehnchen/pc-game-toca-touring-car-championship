#pragma once

#include "render/gpu_renderer.hpp"

namespace toca::test::scene_models {
class GpuFixture {
    struct Video {
        Video() {
            gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated scene-model video");
        }

        ~Video() {
            SDL_Quit();
        }
    } video_;

public:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window;
    gpu::Renderer renderer;

    GpuFixture(const char* title, int width = 640, int height = 480) : window(SDL_CreateWindow(title, width, height, 0), SDL_DestroyWindow), renderer(checked_window()) {}

private:
    SDL_Window* checked_window() {
        gpu::check(bool(window), "Create isolated scene-model window");
        return window.get();
    }
};
} // namespace toca::test::scene_models
