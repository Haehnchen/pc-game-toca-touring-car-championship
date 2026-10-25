#pragma once

#include <array>
#include <cstdint>

namespace toca::scene {
struct CarVisualAngles {
    // Dynamics+278/+27a, in the original4096-angle turn units. These tilt
    // node300 independently of the physical body basis and wheel pivots.
    std::int16_t local_x = 0, absolute_z = 0;
};

// 40caf0: contact torque alone drives the cosmetic body feedback. Pending
// prop/barrier/car moments remain separate physical integration inputs.
void advance_car_visual_angles(CarVisualAngles&, std::uint32_t contacts, std::int32_t contact_torque_x, std::int32_t contact_torque_z);

// 4608b0 ->4807f0 then4809e0/47fe20: replace node300's basis, retain its
// authored translation, then rotate about the resulting local X axis.
std::array<float, 16> car_body_visual_transform(const std::array<float, 16>& authored, const CarVisualAngles&);
} // namespace toca::scene
