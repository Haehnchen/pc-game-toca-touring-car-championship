#include "scene/presentation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace toca::scene {
namespace {
float fraction(float alpha) {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Presentation interpolation requires a finite fraction");
    }
    return std::clamp(alpha, 0.f, 1.f);
}

std::array<float, 16> rigid_transform(const std::array<float, 16>& previous, const std::array<float, 16>& current, float alpha) {
    if (alpha == 0) {
        return previous;
    }
    if (alpha == 1) {
        return current;
    }
    // The simulation's Q16-derived bases may have small rounding errors. A
    // normalized quaternion keeps the interpolated basis rigid, unlike lerp.
    const auto first = glm::normalize(glm::quat_cast(glm::mat3(glm::make_mat4(previous.data()))));
    const auto second = glm::normalize(glm::quat_cast(glm::mat3(glm::make_mat4(current.data()))));
    const auto rotation = glm::mat4_cast(glm::normalize(glm::slerp(first, second, alpha)));
    std::array<float, 16> result;
    std::copy_n(glm::value_ptr(rotation), result.size(), result.begin());
    for (std::size_t axis = 12; axis < 15; ++axis) {
        result[axis] = std::lerp(previous[axis], current[axis], alpha);
    }
    return result;
}

std::int16_t angle(std::int16_t previous, std::int16_t current, float alpha) {
    if (alpha == 0) {
        return previous;
    }
    if (alpha == 1) {
        return current;
    }
    // Angles occupy signed16-bit fields, but sine consumers wrap at4096.
    const auto difference = (std::uint32_t(std::int32_t(current) - previous) + 2048u) & 4095u;
    const auto delta = std::int32_t(difference) - 2048;
    const auto value = std::int32_t(previous) + std::int32_t(std::lround(float(delta) * alpha));
    return std::bit_cast<std::int16_t>(std::uint16_t(value));
}

std::int32_t position(std::int32_t previous, std::int32_t current, float alpha) {
    return std::int32_t(std::llround(std::lerp(double(previous), double(current), double(alpha))));
}
} // namespace

bool camera_continuous(const CameraInputs& previous, const CameraInputs& current) {
    const auto& a = previous.projection;
    const auto& b = current.projection;
    if (previous.view != current.view || previous.cockpit_variant != current.cockpit_variant || previous.large_hands != current.large_hands
        || previous.hide_player != current.hide_player || previous.registered_camera_position.has_value() != current.registered_camera_position.has_value() || a.width == 0
        || a.height == 0 || b.width == 0 || b.height == 0) {
        return false;
    }
    // Preserve the current drawable and its aspect correction. A change in
    // projection mode or camera flags starts a new presentation history.
    const auto horizontal_a = a.x * float(a.width) / float(a.height);
    const auto horizontal_b = b.x * float(b.width) / float(b.height);
    return std::abs(horizontal_a - horizontal_b) < 0.00001f && a.y == b.y && a.near_plane == b.near_plane && a.far_plane == b.far_plane && a.depth_scale == b.depth_scale
           && a.depth_offset == b.depth_offset;
}

CameraInputs interpolate_camera(const CameraInputs& previous, const CameraInputs& current, float alpha) {
    alpha = fraction(alpha);
    auto result = current;
    if (!camera_continuous(previous, current)) {
        return result;
    }
    if (alpha == 0) {
        result.world_to_view = previous.world_to_view;
    } else if (alpha != 1) {
        std::array<float, 16> previous_world, current_world;
        const auto first = glm::inverse(glm::make_mat4(previous.world_to_view.data()));
        const auto second = glm::inverse(glm::make_mat4(current.world_to_view.data()));
        std::copy_n(glm::value_ptr(first), previous_world.size(), previous_world.begin());
        std::copy_n(glm::value_ptr(second), current_world.size(), current_world.begin());
        // View translation is -R*cameraPosition. Interpolating it directly
        // couples a distant world anchor to the turning camera's rotation.
        const auto world = rigid_transform(previous_world, current_world, alpha);
        const auto view = glm::inverse(glm::make_mat4(world.data()));
        std::copy_n(glm::value_ptr(view), result.world_to_view.size(), result.world_to_view.begin());
    }
    if (alpha == 0) {
        result.published_world_position = previous.published_world_position;
    } else if (alpha != 1) {
        //431670 follows the original camera-node translation. Keep the same
        //render fraction without losing its bits through a second inverse.
        if (previous.published_world_position && current.published_world_position) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                (*result.published_world_position)[axis] = std::lerp((*previous.published_world_position)[axis], (*current.published_world_position)[axis], alpha);
            }
        } else {
            result.published_world_position.reset();
        }
    }
    if (result.registered_camera_position) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            (*result.registered_camera_position)[axis] = position((*previous.registered_camera_position)[axis], (*current.registered_camera_position)[axis], alpha);
        }
    }
    return result;
}

CarPose interpolate_car_pose(const CarPose& previous, const CarPose& current, float alpha) {
    alpha = fraction(alpha);
    auto result = current;
    result.object_to_world = rigid_transform(previous.object_to_world, current.object_to_world, alpha);
    result.steering_angle = angle(previous.steering_angle, current.steering_angle, alpha);
    for (std::size_t wheel = 0; wheel < result.wheel_angles.size(); ++wheel) {
        result.wheel_angles[wheel] = angle(previous.wheel_angles[wheel], current.wheel_angles[wheel], alpha);
    }
    result.visual_angles.local_x = angle(previous.visual_angles.local_x, current.visual_angles.local_x, alpha);
    result.visual_angles.absolute_z = angle(previous.visual_angles.absolute_z, current.visual_angles.absolute_z, alpha);
    if (previous.shadow && result.shadow) {
        for (std::size_t point = 0; point < result.shadow->points.size(); ++point) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                result.shadow->points[point][axis] = position(previous.shadow->points[point][axis], current.shadow->points[point][axis], alpha);
            }
        }
    }
    if (previous.light_ground && result.light_ground) {
        for (std::size_t wheel = 0; wheel < result.light_ground->wheel_position.size(); ++wheel) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                result.light_ground->wheel_position[wheel][axis] =
                    position(previous.light_ground->wheel_position[wheel][axis], current.light_ground->wheel_position[wheel][axis], alpha);
            }
            result.light_ground->ground_height[wheel] = position(previous.light_ground->ground_height[wheel], current.light_ground->ground_height[wheel], alpha);
        }
    }
    return result;
}

ChampionshipFinaleFrame interpolate_championship_finale(const ChampionshipFinaleFrame& previous, const ChampionshipFinaleFrame& current, float alpha) {
    alpha = fraction(alpha);
    auto result = current;
    if (previous.camera_cut != current.camera_cut || previous.camera_phase != current.camera_phase || alpha == 1) {
        return result;
    }
    const auto interpolate = [alpha](const ChampionshipFinaleCarPose& first, const ChampionshipFinaleCarPose& second) {
        const auto as_car = [](const ChampionshipFinaleCarPose& source) {
            CarPose result;
            result.object_to_world = source.object_to_world;
            result.steering_angle = source.steering_angle;
            result.wheel_angles = source.wheel_angles;
            result.visual_angles = source.visual_angles;
            return result;
        };
        const auto pose = interpolate_car_pose(as_car(first), as_car(second), alpha);
        return ChampionshipFinaleCarPose{pose.object_to_world, pose.steering_angle, pose.wheel_angles, pose.visual_angles};
    };
    result.pace = interpolate(previous.pace, current.pace);
    result.transporter = interpolate(previous.transporter, current.transporter);
    auto first = CameraInputs{previous.camera_world_to_view, default_projection(3, 640, 480)};
    auto second = CameraInputs{current.camera_world_to_view, first.projection};
    first.registered_camera_position = previous.camera_position_q16;
    second.registered_camera_position = current.camera_position_q16;
    const auto camera = interpolate_camera(first, second, alpha);
    result.camera_world_to_view = camera.world_to_view;
    result.camera_position_q16 = *camera.registered_camera_position;
    return result;
}
} // namespace toca::scene
