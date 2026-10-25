#pragma once

#include "render/gpu_renderer.hpp"

namespace toca::scene {
// P1 owns the top/left pane; P2 receives the odd final row/column.
inline std::array<gpu::Viewport, 2> split_viewports(std::uint32_t width, std::uint32_t height, bool vertical) {
    if (width < 2 || height < 2 || width > INT32_MAX || height > INT32_MAX) {
        throw std::runtime_error("Split screen requires at least2x2 drawable pixels");
    }
    if (vertical) {
        const auto first = width / 2;
        return {{{0, 0, first, height}, {first, 0, width - first, height}}};
    }
    const auto first = height / 2;
    return {{{0, 0, width, first}, {0, first, width, height - first}}};
}
} // namespace toca::scene
