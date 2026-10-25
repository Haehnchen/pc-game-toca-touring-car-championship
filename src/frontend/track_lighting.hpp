#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace toca::frontend {
using PreviewVector = std::array<float, 3>;

inline const std::array<PreviewVector, 2>& track_preview_light_positions() {
    static const auto positions = [] {
        // 46c860 / 47fe20: local Y, Z, X, with these stored 4096-turn angles.
        constexpr float tau = 6.2831853071795864769f;
        const auto sx = std::sin(tau * 577.f / 4096.f), cx = std::cos(tau * 577.f / 4096.f);
        const auto sy = std::sin(tau * -255.f / 4096.f), cy = std::cos(tau * -255.f / 4096.f);
        const auto sz = std::sin(tau * 135.f / 4096.f), cz = std::cos(tau * 135.f / 4096.f);
        std::array<PreviewVector, 2> result{{{0, 32, 10}, {0, -1.5f, 0}}};
        for (auto& point : result) {
            const PreviewVector rx{point[0], cx * point[1] - sx * point[2], sx * point[1] + cx * point[2]};
            const PreviewVector rz{cz * rx[0] - sz * rx[1], sz * rx[0] + cz * rx[1], rx[2]};
            const PreviewVector ry{cy * rz[0] + sy * rz[2], rz[1], -sy * rz[0] + cy * rz[2]};
            // 480ab0 supplies the frame's world translation as a Q16 point
            // and its world matrix to 47b9e0: the intended result is R*P+P.
            // Native repair: initialize all input coordinates, convert Q16
            // consistently and keep outputs separate. The original helper
            // reads uninitialized/in-place locals; this is not bit equality.
            for (std::size_t axis = 0; axis < 3; ++axis) {
                point[axis] += ry[axis];
            }
        }
        return result;
    }();
    return positions;
}

inline float track_preview_illumination(const PreviewVector& position, const PreviewVector& normal, bool available) {
    // Native presentation repair: keep 46c860's initial ambient RGB 128 so the
    // authored miniature remains readable. Original 46c9eb/46d1dd later writes
    // ambient RGB 0; retaining 128 is deliberate, not an original-fidelity claim.
    // Locked tracks keep the original 0x3333/0x20000 light-intensity ratio.
    const auto intensity_ratio = available ? 1.f : float(0x3333) / float(0x20000);
    float illumination = (128.f / 255.f) * intensity_ratio;
    for (const auto& light : track_preview_light_positions()) {
        PreviewVector direction{};
        float squared_distance = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            direction[axis] = light[axis] - position[axis];
            squared_distance += direction[axis] * direction[axis];
        }
        // 49da90: point light, range 5000, constant attenuation 1.
        if (squared_distance <= 0 || squared_distance > 5000.f * 5000.f) {
            continue;
        }
        const auto inverse_distance = 1.f / std::sqrt(squared_distance);
        float cosine = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            cosine += normal[axis] * (direction[axis] * inverse_distance);
        }
        illumination += std::max(0.f, cosine) * (available ? 2.f : float(0x3333) / 65536.f);
    }
    return std::clamp(illumination, 0.f, 1.f);
}
} // namespace toca::frontend
