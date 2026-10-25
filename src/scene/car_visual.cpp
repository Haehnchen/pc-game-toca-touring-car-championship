#include "scene/car_visual.hpp"

#include "scene/matrix.hpp"
#include "simulation/fixed.hpp"

namespace toca::scene {
namespace {
void advance(std::int16_t& angle, std::int32_t torque, std::int32_t threshold, std::int16_t step) {
    if (torque > threshold) {
        if (angle > -20) {
            angle = std::int16_t(angle - step);
        }
    } else if (torque < -threshold) {
        if (angle < 20) {
            angle = std::int16_t(angle + step);
        }
    } else if (angle > -20 && angle < 0) {
        angle = std::int16_t(angle + step);
    } else if (angle > 0 && angle < 20) {
        angle = std::int16_t(angle - step);
    }
}

float sine(std::int32_t angle) {
    return float(simulation::fixed::sine(angle)) / 65536.f;
}
} // namespace

void advance_car_visual_angles(CarVisualAngles& angles, std::uint32_t contacts, std::int32_t torque_x, std::int32_t torque_z) {
    if (contacts == 0) {
        angles = {};
        return;
    }
    // Retain the strict source limits, including neutral torque holding an
    // angle already at +/-20; this is not a smoothed physical suspension.
    advance(angles.local_x, torque_x, 0x640000, 4);
    advance(angles.absolute_z, torque_z, 0x320000, 2);
}

std::array<float, 16> car_body_visual_transform(const std::array<float, 16>& authored, const CarVisualAngles& angles) {
    auto local = authored;
    const auto zs = sine(angles.absolute_z), zc = sine(std::int32_t(angles.absolute_z) + 0x400);
    local[0] = local[5] = zc;
    local[1] = zs;
    local[4] = -zs;
    local[2] = local[6] = local[8] = local[9] = 0;
    local[10] = 1;
    if (angles.local_x == 0) {
        return local;
    }
    // 47fe20 rotates Y/Z rows around the current, slightly nonunit X row.
    return matrix::source_axis_rotation(local, 0, sine(-std::int32_t(angles.local_x)), sine(0x400 - angles.local_x));
}
} // namespace toca::scene
