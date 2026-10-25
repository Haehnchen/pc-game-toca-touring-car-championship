#pragma once

#include "scene/body.hpp"

#include <optional>

namespace toca::scene {
// 0045e920's signed angular result (body forward X/Z), in original 12-bit turns.
std::int16_t follow_direction_angle(const std::array<std::int32_t, 3>& forward);
// 45eb60's per-track initial State4 anchor, before its first follow update.
std::int16_t qualifying_camera_heading(std::uint32_t track_id);

struct FollowCameraFrame {
    std::array<float, 16> camera_to_world{}, world_to_view{};
    std::array<float, 3> world_position{};
};

class FollowCamera {
    std::optional<bool> split_vertical_;
    std::int16_t heading_;
    bool micro_;
    std::uint32_t car_id_, detail_;
    std::uint16_t view_, saved_view_ = 2;
    bool rear_ = false, trackside_ = false, follow_ = false, copter_ = false, actor_camera_available_ = false;
    //42f280 leaves the cold anchor zero-filled;42f950 seeds identity on exit.
    std::array<std::array<std::int32_t, 3>, 3> head_anchor_{};
    std::array<std::array<std::int32_t, 3>, 3> head_camera_{{{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}}};
    std::array<std::int32_t, 3> head_offset_{}, head_base_offset_{}, head_velocity_{};
    std::int16_t head_roll_ = 0;
    bool head_inactive_ = true;

public:
    explicit FollowCamera(std::int16_t initial_heading, bool micro = false, std::uint32_t car_id = 0, bool follow = false, std::uint32_t detail = 3, bool copter = false,
        bool actor_camera_available = false);
    void set_split_screen(std::optional<bool> vertical);
    // Called once per active source tick. State4 has its own slew threshold.
    void update(const BodyPose& body, bool state_four = true);
    //460170/45ea50 replaces the grid heading without reconstructing the
    //retained view, rear latch, cockpit spring or head transforms.
    void initialize_heading(std::int16_t heading);
    //4608b0/47c520 ordinary views1..5 publication. Read the latest body translation
    //without advancing45ea90's separately retained, pre-physics heading.
    FollowCameraFrame frame(const BodyPose& body) const;
    std::array<float, 16> world_to_view(const BodyPose& body, std::array<float, 16>* published_world = nullptr) const;
    std::array<std::int32_t, 3> position(const BodyPose& body) const;
    std::int16_t heading() const;
    std::uint16_t selected_view() const;
    std::uint16_t projection_view() const;
    bool rear_held() const;
    // 421980 calls42fac0 after physics, using old/new body velocity and yaw rate.
    void update_cockpit(
        const BodyPose& body, const std::array<std::int32_t, 3>& previous_velocity, const std::array<std::int32_t, 3>& current_velocity, std::int32_t angular_velocity_y);
    bool trackside() const;
    // 423198: C press advances1→2→3→4→1; tank8 skips cockpit1.
    void cycle_view();
    // Original held Z enters5 and restores the exact prior view on release.
    void set_rear_view(bool held);
    // Original F5 toggles authored trackside cameras only with CMFOLLOW.
    // Returning from stored0 needs its actual loaded HOR camera node.
    void toggle_trackside();
};
} // namespace toca::scene
