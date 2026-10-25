#include "scene/engine_effects_render.hpp"

#include "image/decode.hpp"

#include <stdexcept>

namespace toca::scene {
EngineEffectsView::EngineEffectsView(gpu::Renderer& renderer, const EngineEffectResources& resources) {
    const auto surface = decode_bmp(resources.bang_bmp);
    texture_ = renderer.upload(surface.get(), true);
    palette_ = SDL_ISPIXELFORMAT_INDEXED(surface->format);
}

std::size_t EngineEffectsView::append(gpu::Frame& frame, const CameraInputs& camera, const EngineEffectFrame& effects, std::size_t queued, std::uint8_t view_mask) const {
    if (effects.visibility.size() != effects.sprites.size() || effects.flags != 6) {
        throw std::runtime_error("Engine effects require original BANG2 sprites, view masks and hardware Flags6");
    }
    std::vector<SurfaceEffectSprite> visible;
    visible.reserve(effects.sprites.size());
    for (std::size_t sprite = 0; sprite < effects.sprites.size(); ++sprite) {
        if (effects.visibility[sprite] & view_mask) {
            visible.push_back(effects.sprites[sprite]);
        }
    }
    return append_surface_effect_sprites(frame, camera, texture_.get(), visible, effects.flags, queued, palette_);
}
} // namespace toca::scene
