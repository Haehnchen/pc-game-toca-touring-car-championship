#pragma once

#include "core/contracts.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace toca::platform {
struct Extent {
    std::uint32_t width = 0, height = 0;
};

struct UiPoint {
    float x = 0, y = 0;
    bool inside = false;
};

struct UiTransform {
    float scale = 0, x = 0, y = 0, width = 0, height = 0;
};

UiTransform ui_transform(Extent drawable);
UiPoint logical_pointer(float pixel_x, float pixel_y, Extent drawable);

// Owns presentation resources only. Application time and game state remain outside it.
class Display {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_;
    VideoSettings settings_;

public:
    Display(const char* title, const VideoSettings& settings);
    SDL_Window* window() const;
    void apply(const VideoSettings& settings);
    VideoSettings settings() const;
    std::vector<Extent> fullscreen_modes() const;
    UiPoint map_pointer(float window_x, float window_y, Extent drawable) const;
    void pace(std::uint64_t frame_start_ns) const;
};
} // namespace toca::platform
