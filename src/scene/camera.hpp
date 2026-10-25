#pragma once

#include "scene/world_lines.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace toca::scene {
// D3D row-vector projection: clip=(x*X,y*Y,z*depth_scale+depth_offset,z).
// World-to-view and the follow-node simulation are separate consumers.
struct Projection {
    float x, y;
    float near_plane, far_plane;
    float depth_scale, depth_offset;
    std::uint32_t width, height;
};

struct CameraInputs {
    std::array<float, 16> world_to_view;
    Projection projection;
    // Authored finish cameras are registered scene roots; follow views instead
    // inherit the separate anchor/body sector (4510bf/47dd90).
    std::optional<std::array<std::int32_t, 3>> registered_camera_position{};
    std::uint16_t view = 2;
    bool cockpit_variant = false, large_hands = false;
    bool hide_player = false;
    bool canonical_single_player = false;
    // 431670 reads the published camera node before49a660 inverts its basis.
    std::optional<std::array<float, 3>> published_world_position{};
    // 489069's already queued443030 presentation pool; absent uses runtime generation.
    std::optional<std::vector<QueuedWorldLine>> queued_grid_lines{};
};

std::array<float, 16> horizon_world_transform(const CameraInputs& camera, std::uint32_t track_id);

// Normal single-player views 2/3 from 004508ce/0048f5f9, with detail limits
// from 00452376. Wider drawables preserve vertical coverage and the original
// 4:3 projection; no camera position or follow-node transform is invented.
// Mega extends the original maximum250-unit far plane to500 units.
Projection default_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height);
// 4508ce/423198 selects0xa3d7 for views2/3,0xb333 otherwise.
Projection selected_view_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height, std::uint16_t view);
// 4508ce/423198: split mode uses a fixed factor per orientation for every view.
Projection split_view_projection(std::uint32_t detail, std::uint32_t width, std::uint32_t height, bool vertical);
} // namespace toca::scene
