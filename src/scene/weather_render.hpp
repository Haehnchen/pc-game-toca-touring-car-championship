#pragma once

#include "scene/view.hpp"
#include "scene/weather.hpp"

namespace toca::scene {
// 497070/497180 Flags2, before projection. Source rectangle words are
// vertical/horizontal, and the two camera-basis products shift separately.
std::array<LitVertex, 4> weather_sprite_vertices(const WeatherSprite& sprite, const WeatherCamera& camera);

class WeatherView {
    std::array<gpu::Texture, 3> textures_;
    std::array<bool, 3> palette_{};

public:
    WeatherView(gpu::Renderer& renderer, const WeatherResources& resources);
    // 48bd.. flushes497180,499740,47cb80, in that order. The host places
    // sign fragments between these sprite and line passes.
    void append_sprites(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const;
    void append_lines(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const;
    // 4632c0 ->49d0c0/49d290 ->48741e/49d3d0, after the3D view.
    void append_lightning(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const;
};
} // namespace toca::scene
