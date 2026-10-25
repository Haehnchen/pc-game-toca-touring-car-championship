#pragma once

#include <array>
#include <cstdint>

namespace toca::scene {
using ShadowVector = std::array<std::int32_t, 3>;

enum class CarShadowType : std::uint32_t {
    rectangle,
    one_side,
    two_sides
};

// Original498400 record: eleven Q16 positions, then the498430 drawing type.
struct CarShadowFootprint {
    std::array<ShadowVector, 11> points{};
    CarShadowType type = CarShadowType::rectangle;
};

// Original437770 inputs, recovered independently of the presentation camera.
struct CarShadowInput {
    ShadowVector support_normal{}, support_position{};
    std::array<ShadowVector, 8> contact_points{};
    ShadowVector first_axis{}, fallback_axis{}, center_sample{};
    ShadowVector current_ground_normal{}, body_up{}, horizontal_light{};
    std::int32_t distance_q16 = 0;
    std::uint32_t car_id = 0, actor_index = 0;
    bool special_state_18 = false, directional = false;
};

CarShadowFootprint car_shadow_footprint(const CarShadowInput& input);
} // namespace toca::scene
