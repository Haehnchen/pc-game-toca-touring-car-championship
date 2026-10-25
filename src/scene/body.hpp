#pragma once

#include "formats/hpc.hpp"
#include "scene/placement.hpp"

#include <optional>

namespace toca::scene {
struct BodyPose {
    std::array<std::int32_t, 3> position{};
    std::array<std::array<std::int32_t, 3>, 3> basis{};
    std::array<std::array<std::int32_t, 3>, 8> contacts{};
    std::array<HpcSample, 8> surfaces{};
    // 415240 support normal; retained while no contact touches the ground.
    std::array<std::int32_t, 3> ground_normal{0, 65536, 0};
    // Row-vector object-to-world, translation [12..14].
    std::array<float, 16> object_to_world{};
};

struct InitialBodySupport {
    std::array<HpcSample, 8> surfaces{};
    std::array<std::int32_t, 3> normal{};
};

//40a770's first SGRID edge at Y0 establishes support before460170 placement.
InitialBodySupport initial_body_support(const HpcSurface&, const HorizontalPlacement&, std::uint32_t car_id);

// 412040: initial orientation, eight HPC contacts, tilt and body-height shift.
// Original cars 0..9; no simulation step or invented suspension settling.
//412040's ground helpers use the process current-body globals. When another
//actor is current, only the explicit pose/basis writes affect this body; its
//old support remains and its yaw contacts are not rebuilt after the tilt.
BodyPose initial_body_pose(
    const HpcSurface& surface, const HorizontalPlacement& start, std::uint32_t car_id, const InitialBodySupport* retained_support = nullptr, bool current_body = true);
// Rebuild original eight contact offsets and publish the changed body transform.
void refresh_body_contacts(BodyPose& body, std::uint32_t car_id);
std::array<std::int32_t, 3> car_half_dimensions(std::uint32_t car_id);

// Camera2 from 4508ce, positioned/yawed by 45ea50/4608b0. The original
// follow anchor has its own heading; it does not inherit body roll or pitch.
// Optional output retains the composed camera node before affine inversion.
std::array<float, 16> initial_follow_world_to_view(
    const BodyPose& body, std::int16_t anchor_heading, bool micro = false, std::optional<bool> split_vertical = std::nullopt, std::array<float, 16>* published_world = nullptr);
// 4508ce single-player camera nodes2..5.
std::array<float, 16> race_world_to_view(const BodyPose& body, std::int16_t anchor_heading, std::uint16_t view, std::uint32_t car_id, bool micro, std::uint32_t detail = 3,
    std::optional<bool> split_vertical = std::nullopt, std::array<float, 16>* published_world = nullptr);
// 42f280/42fac0: separately sprung cockpit camera and anchor nodes.
std::array<float, 16> cockpit_world_to_view(const BodyPose& body, const std::array<std::array<std::int32_t, 3>, 3>& anchor_basis,
    const std::array<std::array<std::int32_t, 3>, 3>& camera_basis, const std::array<std::int32_t, 3>& offset, std::array<float, 16>* published_world = nullptr);
} // namespace toca::scene
