#pragma once

#include "scene/surface_effect_resources.hpp"
#include "scene/surface_effects.hpp"
#include "scene/weather_render.hpp"

namespace toca::scene {
std::array<LitVertex, 4> surface_effect_sprite_vertices(const SurfaceEffectSprite& sprite, std::uint8_t flags, const WeatherCamera& camera);
// 497070/497180 shared by wheel particles and exhaust flames. Queue admission
// precedes close-camera rejection and rejects an overflowing batch in full.
std::size_t append_surface_effect_sprites(gpu::Frame& frame, const CameraInputs& camera, SDL_GPUTexture* texture, std::span<const SurfaceEffectSprite> sprites, std::uint8_t flags,
    std::size_t already_queued = 0, bool palette = false);

class SurfaceEffectsView {
    std::array<gpu::Texture, 6> textures_;
    std::array<bool, 6> palette_{};

public:
    SurfaceEffectsView(gpu::Renderer& renderer, const SurfaceEffectResources& resources);
    std::size_t append_batch(gpu::Frame& frame, const CameraInputs& camera, const SurfaceEffectBatch& batch, std::size_t already_queued = 0) const;
    // 446f20 submits complete batches to497070 in source pool order. Its
    // strict127-sprite budget includes previously submitted weather sprites,
    // even if those are later culled by497180. Return the new queue occupancy.
    std::size_t append(gpu::Frame& frame, const CameraInputs& camera, const SurfaceEffectFrame& effects, std::size_t already_queued = 0) const;
};
} // namespace toca::scene
