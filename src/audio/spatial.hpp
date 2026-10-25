#pragma once

#include <array>
#include <cstdint>

namespace toca::audio {
using SpatialVector = std::array<std::int32_t, 3>;

struct SpatialListener {
    SpatialVector position_q16{};
    // 426190/430af0's4d59c8, from4313d0's camera+bc/c0/c4 axis.
    std::int16_t inverse_heading = 0;
    bool stereo = true;
};

struct SourceSpatial {
    std::int32_t distance_q16 = 0, fade_q16 = 0;
    std::uint16_t angle = 0;
    std::uint32_t pan = 64;
    bool in_range = false;
};

// 423fb0/424070/424c70: quantized horizontal distance/fade, but unquantized
// direction, original camera-relative angle and424ac0's asymmetric pan.
SourceSpatial source_spatial(const SpatialVector& source_q16, const SpatialListener& listener, bool half_distance = false);
std::uint32_t source_pan(std::uint16_t angle);
// 426190: normalize the horizontal camera+bc/c0/c4 axis and invert its heading.
std::int16_t source_listener_heading(const SpatialVector& camera_axis_q16);
} // namespace toca::audio
