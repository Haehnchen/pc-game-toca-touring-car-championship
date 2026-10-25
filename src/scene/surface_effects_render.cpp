#include "scene/surface_effects_render.hpp"

#include "image/decode.hpp"
#include "simulation/fixed.hpp"

#include <stdexcept>

namespace toca::scene {
std::array<LitVertex, 4> surface_effect_sprite_vertices(const SurfaceEffectSprite& sprite, std::uint8_t flags, const WeatherCamera& camera) {
    if (flags != 2 && flags != 6) {
        throw std::runtime_error("Surface sprite requires its original497070 Flags2/6");
    }
    WeatherSprite billboard;
    billboard.center = sprite.center;
    billboard.minimum = sprite.minimum;
    billboard.maximum = sprite.maximum;
    // 497180's ordinary branch packs source+30 as alpha and+31 as RGB.
    // Flags bit4 instead uses+30 for RGB, alpha255 and ONE/INVSRCCOLOR.
    billboard.alpha = flags & 4 ? 255 : sprite.shade0;
    billboard.brightness = flags & 4 ? sprite.shade0 : sprite.shade1;
    return weather_sprite_vertices(billboard, camera);
}

std::size_t append_surface_effect_sprites(
    gpu::Frame& frame, const CameraInputs& camera, SDL_GPUTexture* texture, std::span<const SurfaceEffectSprite> sprites, std::uint8_t flags, std::size_t queued, bool palette) {
    if (queued >= 128) {
        throw std::runtime_error("Surface effects require the source bounded sprite queue");
    }
    const auto count = sprites.size();
    if (count == 0 || count >= 128 - queued) {
        return queued;
    }
    if (flags != 2 && flags != 6) {
        throw std::runtime_error("Surface sprite batch requires original Flags2/6");
    }
    const auto projection = checked_world_projection(camera);
    const auto source_camera = weather_camera(camera.world_to_view);
    const gpu::State state{.texture = texture, .depth = true, .blend = true, .key = true, .depth_write = false, .screen = (flags & 4) != 0, .clamp = true, .palette = palette};
    for (const auto& sprite : sprites) {
        using namespace simulation::fixed;
        auto separation = sub(sprite.center, source_camera.position);
        separation[1] = 0;
        const auto diagonal = sub(sprite.maximum, sprite.minimum);
        if (dot(separation, separation) < 0x40000 && dot(diagonal, diagonal) > 0xa0000) {
            continue; // 497180 rejection still consumes497070 queue space.
        }
        const auto vertices = surface_effect_sprite_vertices(sprite, flags, source_camera);
        for (const auto& indices : {std::array{0u, 3u, 2u}, std::array{0u, 1u, 3u}}) {
            std::array<gpu::WorldVertex, 3> triangle;
            for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                const auto& vertex = vertices[indices[corner]];
                triangle[corner] = {vertex.position, vertex.color, vertex.u, vertex.v};
            }
            frame.world_triangle(state, triangle, projection);
        }
    }
    return queued + count;
}

SurfaceEffectsView::SurfaceEffectsView(gpu::Renderer& renderer, const SurfaceEffectResources& resources) {
    for (std::size_t texture = 0; texture < textures_.size(); ++texture) {
        const auto surface = decode_bmp(resources.textures[texture]);
        textures_[texture] = renderer.upload(surface.get(), true);
        palette_[texture] = SDL_ISPIXELFORMAT_INDEXED(surface->format);
    }
}

std::size_t SurfaceEffectsView::append_batch(gpu::Frame& frame, const CameraInputs& camera, const SurfaceEffectBatch& batch, std::size_t queued) const {
    if (queued >= 128) {
        throw std::runtime_error("Surface effects require the source bounded sprite queue");
    }
    if (batch.sprites.empty() || batch.sprites.size() >= 128 - queued) {
        return queued;
    }
    const auto texture = std::size_t(batch.texture);
    if (texture >= textures_.size()) {
        throw std::runtime_error("Surface batch requires an original texture");
    }
    return append_surface_effect_sprites(frame, camera, textures_[texture].get(), batch.sprites, batch.flags, queued, palette_[texture]);
}

std::size_t SurfaceEffectsView::append(gpu::Frame& frame, const CameraInputs& camera, const SurfaceEffectFrame& effects, std::size_t queued) const {
    for (const auto& batch : effects.batches) {
        queued = append_batch(frame, camera, batch, queued);
    }
    return queued;
}
} // namespace toca::scene
