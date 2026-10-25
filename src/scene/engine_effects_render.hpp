#pragma once

#include "scene/engine_effects.hpp"
#include "scene/surface_effects_render.hpp"

namespace toca::scene {
class EngineEffectsView {
    gpu::Texture texture_;
    bool palette_ = false;

public:
    EngineEffectsView(gpu::Renderer& renderer, const EngineEffectResources& resources);
    // 446f20: BANG2 after spray/before wheel groups. Original visibility
    // filtering precedes whole-batch admission to the shared127-sprite queue.
    std::size_t append(gpu::Frame& frame, const CameraInputs& camera, const EngineEffectFrame& effects, std::size_t already_queued = 0, std::uint8_t view_mask = 1) const;
};
} // namespace toca::scene
