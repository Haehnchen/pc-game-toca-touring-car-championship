#pragma once

#include "scene/sun.hpp"
#include "scene/view.hpp"

namespace toca::scene {
class SunView {
    std::array<gpu::Texture, 7> textures_;
    std::array<bool, 7> linear_{};

public:
    SunView(gpu::Renderer& renderer, const SunResources& resources);
    void append(gpu::Frame& frame, const CameraInputs& camera, std::uint32_t track_id) const;
};
} // namespace toca::scene
