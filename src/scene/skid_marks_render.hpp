#pragma once

#include "scene/skid_marks.hpp"
#include "scene/view.hpp"

namespace toca::scene {
// 442500/441d10 follows opaque terrain and precedes the car-shadow pass.
// An empty sector selection preserves47db00's unselected-view fallback.
void append_skid_marks(gpu::Frame& frame, const CameraInputs& camera, const SkidMarkFrame& marks, std::span<const std::uint16_t> visible_sectors = {}, std::uint8_t view_mask = 1);
} // namespace toca::scene
