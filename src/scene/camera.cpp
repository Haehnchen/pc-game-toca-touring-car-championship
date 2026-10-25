#include "scene/camera.hpp"

#include "core/contracts.hpp"

#include <array>
#include <stdexcept>

namespace toca::scene {
Projection selected_view_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height, std::uint16_t view) {
    if (view > 5 || detail >= graphics_detail_count || width == 0 || height == 0) {
        throw std::runtime_error("Scene projection needs detail 0..4 and a nonempty drawable");
    }
    constexpr std::array<std::int32_t, graphics_detail_count> far_planes{150, 150, 200, 250, 500};
    constexpr std::int64_t one = 0x10000;
    const std::int64_t factor = (view == 2 || view == 3) ? 0xa3d7 : 0xb333;
    constexpr std::int64_t near_fixed = 0x4000;
    const std::int64_t far_fixed = far_planes[detail] * one;
    const auto depth_scale = far_fixed * one / (far_fixed - near_fixed);
    // Original fixed multiply precedes fixed divide, which truncates toward zero.
    const auto depth_offset = (-far_fixed * near_fixed / one) * one / (far_fixed - near_fixed);
    const float x = static_cast<float>(factor * 0x20000 / one) / one;
    const float y = static_cast<float>(factor * 0x28000 / one) / one;
    const float aspect_scale = (4.0f * static_cast<float>(height)) / (3.0f * static_cast<float>(width));
    return {x * aspect_scale, y, 0.25f, static_cast<float>(far_planes[detail]), static_cast<float>(depth_scale) / one, static_cast<float>(depth_offset) / one, width, height};
}

Projection split_view_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height, bool vertical) {
    auto result = selected_view_projection(detail, width, height, 2);
    constexpr std::int64_t one = 65536;
    const std::int64_t factor = vertical ? 0x1383a : 0x10000;
    // The source scales the split axis in48f2ea while drawing in full-screen
    // coordinates. Each native pane has its own projection/viewport instead;
    // preserve vertical world coverage and correct X for the actual pane aspect.
    result.x = float(factor * 2) / float(one) * (4.f * float(height) / (3.f * float(width)));
    result.y = float(factor * 0x28000 / one) / float(one);
    return result;
}

Projection default_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height) {
    return selected_view_projection(detail, width, height, 2);
}
} // namespace toca::scene
