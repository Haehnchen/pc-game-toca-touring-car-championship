#include "platform/display.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace toca::platform {
namespace {
void check(bool result, const char* operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}

void validate(const VideoSettings& settings) {
    if (settings.width == 0 || settings.height == 0 || settings.width > 16384 || settings.height > 16384 || settings.fps_limit > 1000) {
        throw std::runtime_error("Video settings require dimensions 1..16384 and an FPS limit 0..1000");
    }
}
} // namespace

UiTransform ui_transform(Extent drawable) {
    const auto scale = std::min(float(drawable.width) / 640.f, float(drawable.height) / 480.f);
    return {scale, (float(drawable.width) - 640.f * scale) * .5f, (float(drawable.height) - 480.f * scale) * .5f, 640.f * scale, 480.f * scale};
}

UiPoint logical_pointer(float pixel_x, float pixel_y, Extent drawable) {
    const auto transform = ui_transform(drawable);
    if (transform.scale <= 0) {
        return {};
    }
    const float x = (pixel_x - transform.x) / transform.scale;
    const float y = (pixel_y - transform.y) / transform.scale;
    return {x, y, x >= 0 && x < 640 && y >= 0 && y < 480};
}

Display::Display(const char* title, const VideoSettings& settings) : window_(nullptr, SDL_DestroyWindow) {
    validate(settings);
    window_.reset(SDL_CreateWindow(title, int(settings.width), int(settings.height), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    check(bool(window_), "Create application window");
    apply(settings);
}

SDL_Window* Display::window() const {
    return window_.get();
}

void Display::apply(const VideoSettings& settings) {
    validate(settings);
    if (!settings.fullscreen) {
        check(SDL_SetWindowFullscreen(window(), false), "Leave fullscreen");
        check(SDL_SetWindowSize(window(), int(settings.width), int(settings.height)), "Set window resolution");
    } else {
        SDL_DisplayMode mode{};
        check(SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window()), int(settings.width), int(settings.height), 0, false, &mode), "Find fullscreen mode");
        check(SDL_SetWindowFullscreenMode(window(), &mode), "Set fullscreen resolution");
        check(SDL_SetWindowFullscreen(window(), true), "Enter fullscreen");
    }
    check(SDL_SyncWindow(window()), "Synchronize window settings");
    settings_ = settings;
}

VideoSettings Display::settings() const {
    auto result = settings_;
    int width = 0, height = 0;
    check(SDL_GetWindowSize(window(), &width, &height), "Query effective window size");
    result.width = std::uint32_t(width);
    result.height = std::uint32_t(height);
    result.fullscreen = (SDL_GetWindowFlags(window()) & SDL_WINDOW_FULLSCREEN) != 0;
    return result;
}

std::vector<Extent> Display::fullscreen_modes() const {
    int count = 0;
    const auto modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window()), &count);
    check(modes != nullptr, "Query fullscreen modes");
    std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> owned(modes, SDL_free);
    std::vector<Extent> result;
    for (int i = 0; i < count; ++i) {
        const Extent extent{std::uint32_t(modes[i]->w), std::uint32_t(modes[i]->h)};
        if (std::ranges::none_of(result, [extent](Extent item) {
                return item.width == extent.width && item.height == extent.height;
            })) {
            result.push_back(extent);
        }
    }
    return result;
}

UiPoint Display::map_pointer(float window_x, float window_y, Extent drawable) const {
    int width = 0, height = 0;
    check(SDL_GetWindowSize(window(), &width, &height), "Query input window size");
    if (width <= 0 || height <= 0) {
        return {};
    }
    return logical_pointer(window_x * float(drawable.width) / float(width), window_y * float(drawable.height) / float(height), drawable);
}

void Display::pace(std::uint64_t frame_start_ns) const {
    const auto flags = SDL_GetWindowFlags(window());
    const bool background = (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
    const auto cap = background ? (settings_.fps_limit == 0 ? 10u : std::min(10u, settings_.fps_limit)) : settings_.fps_limit;
    if (cap == 0) {
        return;
    }
    const std::uint64_t deadline = frame_start_ns + 1'000'000'000ull / cap;
    const auto now = SDL_GetTicksNS();
    if (now < deadline) {
        SDL_DelayNS(deadline - now);
    }
}
} // namespace toca::platform
