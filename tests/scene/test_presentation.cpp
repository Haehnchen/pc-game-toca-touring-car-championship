#include "scene/matrix.hpp"
#include "scene/presentation.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace toca::scene;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

bool close(float first, float second, float tolerance = 0.00002f) {
    return std::abs(first - second) < tolerance;
}

CameraInputs camera() {
    return {identity, default_projection(3, 1280, 720)};
}

void intermediate_frames() {
    // Reconstruct constant motion from40ms simulation samples at both common
    // presentation rates. Successive images must move between physics ticks.
    for (const auto hz : {60, 144}) {
        float last = 0;
        for (int frame = 1; frame <= hz; ++frame) {
            const auto milliseconds = double(frame) * 1000 / hz;
            const auto tick = std::floor(milliseconds / 40);
            const auto alpha = float((milliseconds - tick * 40) / 40);
            CarPose previous{identity}, current{identity};
            previous.object_to_world[12] = float(tick * 4);
            current.object_to_world[12] = float((tick + 1) * 4);
            auto camera_previous = camera(), camera_current = camera();
            camera_previous.world_to_view[12] = -previous.object_to_world[12];
            camera_current.world_to_view[12] = -current.object_to_world[12];
            const auto pose = interpolate_car_pose(previous, current, alpha);
            const auto view = interpolate_camera(camera_previous, camera_current, alpha);
            require(close(pose.object_to_world[12], float(milliseconds / 10)), "Presentation must advance linearly between40ms ticks at60/144Hz");
            require(close(pose.object_to_world[12] - last, 100.f / float(hz)), "Presentation must not repeat a physical pose between ticks");
            require(close(view.world_to_view[12], -pose.object_to_world[12]), "Camera and car must use the same interpolation fraction");
            last = pose.object_to_world[12];
        }
    }
}

std::array<float, 16> yaw(float degrees) {
    auto result = identity;
    const auto radians = degrees * std::numbers::pi_v<float> / 180;
    // Quantize like a basis originating from the authoritative Q16 body.
    const auto cosine = std::round(std::cos(radians) * 65536) / 65536;
    const auto sine = std::round(std::sin(radians) * 65536) / 65536;
    result[0] = result[10] = cosine;
    result[2] = -sine;
    result[8] = sine;
    return result;
}

void rotation_and_angles() {
    CarPose previous{yaw(179)}, current{yaw(-179)};
    previous.steering_angle = 4090;
    current.steering_angle = 6;
    previous.wheel_angles = {4090, 6, 32760, -32760};
    current.wheel_angles = {6, 4090, -32760, 32760};
    previous.visual_angles = {-4, 4090};
    current.visual_angles = {4, 6};
    const auto half = interpolate_car_pose(previous, current, .5f);
    require(half.object_to_world[0] < -.9999f && half.object_to_world[10] < -.9999f, "Quaternion interpolation must cross180 degrees along the shortest path");
    require((std::uint16_t(half.steering_angle) & 4095) == 0 && (std::uint16_t(half.wheel_angles[0]) & 4095) == 0 && (std::uint16_t(half.wheel_angles[1]) & 4095) == 0,
        "Steering and wheels must wrap at the source4096-unit turn");
    require(half.wheel_angles[2] == -32768 && half.wheel_angles[3] == -32768, "Interpolation must preserve signed16-bit angle storage wrapping");
    require(half.visual_angles.local_x == 0 && (std::uint16_t(half.visual_angles.absolute_z) & 4095) == 0, "Visual tilt angles must interpolate across their wrap");
    for (const auto alpha : {.1f, .25f, .5f, .75f, .9f}) {
        const auto matrix = interpolate_car_pose(previous, current, alpha).object_to_world;
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                float dot = 0;
                for (std::size_t row = 0; row < 3; ++row) {
                    dot += matrix[a * 4 + row] * matrix[b * 4 + row];
                }
                require(close(dot, a == b ? 1.f : 0.f), "Interpolated Q16-derived basis must remain orthonormal");
            }
        }
        require(matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 && matrix[15] == 1, "Interpolated transform must remain affine");
    }
}

void endpoints_and_discrete_values() {
    CarPose previous{yaw(-20)}, current{yaw(30)};
    previous.object_to_world[12] = -12;
    current.object_to_world[12] = 24;
    previous.wheel_angles[0] = 4000;
    current.wheel_angles[0] = 5;
    previous.shadow.emplace();
    current.shadow.emplace();
    previous.shadow->points[0] = {-65536, INT32_MIN, INT32_MAX};
    current.shadow->points[0] = {65536, INT32_MAX, INT32_MIN};
    current.shadow->type = CarShadowType::two_sides;
    current.damage_levels[3] = 4;
    current.second_driver = true;
    previous.light_ground.emplace();
    current.light_ground.emplace();
    previous.light_ground->wheel_position[0] = {-65536, INT32_MIN, INT32_MAX};
    current.light_ground->wheel_position[0] = {65536, INT32_MAX, INT32_MIN};
    previous.light_ground->ground_height[0] = -65536;
    current.light_ground->ground_height[0] = 65536;
    current.light_ground->surface[0] = 2;
    current.light_ground->distant_body = true;
    current.light_ground->body_height = 40000;
    const auto start = interpolate_car_pose(previous, current, 0);
    const auto end = interpolate_car_pose(previous, current, 1);
    require(start.object_to_world == previous.object_to_world && start.wheel_angles == previous.wheel_angles && start.shadow->points == previous.shadow->points,
        "Alpha zero must retain exact previous continuous data");
    require(end.object_to_world == current.object_to_world && end.wheel_angles == current.wheel_angles && end.shadow->points == current.shadow->points,
        "Alpha one must retain exact current continuous data");
    const auto half = interpolate_car_pose(previous, current, .5f);
    require(half.shadow->points[0] == ShadowVector{0, -1, -1}, "Shadow Q16 points must interpolate without signed overflow");
    require(start.light_ground->wheel_position == previous.light_ground->wheel_position && start.light_ground->ground_height == previous.light_ground->ground_height
                && end.light_ground->wheel_position == current.light_ground->wheel_position && end.light_ground->ground_height == current.light_ground->ground_height,
        "Wet lamp wheel/plane snapshots must retain exact continuous endpoints");
    require(half.light_ground->wheel_position[0] == ShadowVector{0, -1, -1} && half.light_ground->ground_height[0] == 0,
        "Wet lamp wheel positions and sampled plane heights must follow the rendered car without signed overflow");
    require(start.light_ground->surface == current.light_ground->surface && half.light_ground->distant_body && half.light_ground->body_height == 40000,
        "Wet lamp surface eligibility, distant mode and authored height must retain the current tick");
    require(start.shadow->type == current.shadow->type && half.shadow->type == current.shadow->type && start.damage_levels == current.damage_levels && start.second_driver,
        "Shadow type, damage and driver identity must use the current tick");
    require(interpolate_car_pose(previous, current, -1).object_to_world == previous.object_to_world
                && interpolate_car_pose(previous, current, 2).object_to_world == current.object_to_world,
        "Presentation fractions must clamp to their endpoints");
    current.shadow.reset();
    current.light_ground.reset();
    require(!interpolate_car_pose(previous, current, .5f).shadow, "Removed shadows must remain removed");
    require(!interpolate_car_pose(previous, current, .5f).light_ground, "Missing lamp ground snapshots must remain absent");
    require(previous.object_to_world[12] == -12 && current.object_to_world[12] == 24 && previous.shadow->points[0][1] == INT32_MIN,
        "Interpolation must leave authoritative inputs unchanged");
}

void camera_changes() {
    auto previous = camera(), current = camera();
    previous.world_to_view[12] = 10;
    current.world_to_view[12] = 20;
    previous.registered_camera_position = ShadowVector{0, -65536, 0};
    current.registered_camera_position = ShadowVector{65536, 65536, 0};
    current.projection = default_projection(3, 1920, 1200);
    require(camera_continuous(previous, current), "A drawable resize must preserve continuous camera history");
    const auto half = interpolate_camera(previous, current, .5f);
    require(half.world_to_view[12] == 15 && half.projection.width == 1920 && half.projection.height == 1200 && half.projection.x == current.projection.x,
        "Interpolated cameras must retain the current drawable projection");
    require(*half.registered_camera_position == ShadowVector{32768, 0, 0}, "Registered camera Q16 coordinates must follow presentation motion");
    require(interpolate_camera(previous, current, 0).world_to_view == previous.world_to_view && interpolate_camera(previous, current, 1).world_to_view == current.world_to_view,
        "Camera endpoints must be exact");
    for (int change = 0; change < 9; ++change) {
        auto cut = current;
        switch (change) {
            case 0:
                ++cut.view;
                break;
            case 1:
                cut.cockpit_variant = true;
                break;
            case 2:
                cut.large_hands = true;
                break;
            case 3:
                cut.hide_player = true;
                break;
            case 4:
                cut.projection.y += .1f;
                break;
            case 5:
                cut.projection.x += .1f;
                break;
            case 6:
                cut.projection.far_plane += 50;
                break;
            case 7:
                cut.projection.depth_offset += .1f;
                break;
            case 8:
                cut.registered_camera_position.reset();
                break;
        }
        require(!camera_continuous(previous, cut) && interpolate_camera(previous, cut, .5f).world_to_view == cut.world_to_view,
            "Camera mode cuts must present the current pose immediately");
    }
}

void published_camera_positions() {
    auto previous = camera(), current = camera();
    auto first_world = yaw(-37), second_world = yaw(23);
    const std::array first{-110.85408020019531f, 9.905120849609375f, -138.31088256835938f};
    const std::array second{-102.85408782958984f, 13.905120849609375f, -154.31088256835938f};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        first_world[12 + axis] = first[axis];
        second_world[12 + axis] = second[axis];
    }
    previous.world_to_view = matrix::inverse(first_world, "Test camera basis");
    current.world_to_view = matrix::inverse(second_world, "Test camera basis");
    const auto original_previous = previous, original_current = current;
    previous.published_world_position = first;
    current.published_world_position = second;
    for (const auto alpha : {0.f, .25f, .5f, 1.f}) {
        const auto rendered = interpolate_camera(previous, current, alpha);
        require(rendered.world_to_view == interpolate_camera(original_previous, original_current, alpha).world_to_view,
            "Published camera positions must not change existing interpolated view matrices");
        require(rendered.published_world_position.has_value(), "Interpolated source camera must retain its published world position");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            require((*rendered.published_world_position)[axis] == std::lerp(first[axis], second[axis], alpha),
                "Sky camera position must retain source endpoints and their exact presentation fraction");
        }
    }
    previous.published_world_position.reset();
    require(!interpolate_camera(previous, current, 0).published_world_position && !interpolate_camera(previous, current, .5f).published_world_position,
        "A camera history without published position must retain the affine diagnostic fallback");
    current.view = 4;
    require(interpolate_camera(previous, current, .5f).published_world_position == current.published_world_position,
        "Camera cuts must publish the actual current world position immediately");
}

void camera_world_anchor() {
    const std::array<float, 3> first_position{1000, 2000, 3000}, second_position{1004, 2002, 3006};
    const auto world_view = [](float degrees, const auto& position) {
        const auto world = yaw(degrees);
        auto view = identity;
        // Independent rigid inverse: transpose the basis, then -R*position.
        for (std::size_t row = 0; row < 3; ++row) {
            view[12 + row] = 0;
            for (std::size_t column = 0; column < 3; ++column) {
                view[column * 4 + row] = world[row * 4 + column];
                view[12 + row] -= view[column * 4 + row] * position[column];
            }
        }
        return view;
    };
    auto previous = camera(), current = camera();
    previous.world_to_view = world_view(-45, first_position);
    current.world_to_view = world_view(45, second_position);
    const auto half = interpolate_camera(previous, current, .5f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float position = 0;
        for (std::size_t row = 0; row < 3; ++row) {
            position -= half.world_to_view[axis * 4 + row] * half.world_to_view[12 + row];
        }
        // Input bases retain their Q16 rounding, so inversion adds a small
        // sub-unit error at these deliberately distant world coordinates.
        require(close(position, (first_position[axis] + second_position[axis]) * .5f, .1f),
            "Turning cameras must interpolate their world position without pulling a distant anchor toward the origin");
    }
}

void championship_finale() {
    for (const auto hz : {60, 144}) {
        float last = 0;
        for (int image = 1; image <= hz; ++image) {
            const auto milliseconds = double(image) * 1000 / hz;
            const auto tick = float(std::floor(milliseconds / 40));
            const auto alpha = float((milliseconds - double(tick) * 40) / 40);
            ChampionshipFinaleFrame previous, current;
            previous.pace.object_to_world = previous.transporter.object_to_world = previous.camera_world_to_view = identity;
            current.pace.object_to_world = current.transporter.object_to_world = current.camera_world_to_view = identity;
            previous.pace.object_to_world[12] = tick * 4;
            current.pace.object_to_world[12] = (tick + 1) * 4;
            previous.transporter.object_to_world[14] = tick * 2;
            current.transporter.object_to_world[14] = (tick + 1) * 2;
            previous.camera_world_to_view[12] = -tick * 2;
            current.camera_world_to_view[12] = -(tick + 1) * 2;
            previous.camera_position_q16[0] = std::int32_t(tick * 2 * 65536);
            current.camera_position_q16[0] = std::int32_t((tick + 1) * 2 * 65536);
            previous.camera_phase = current.camera_phase = 2;
            previous.camera_cut = current.camera_cut = UINT64_C(2) << 32 | 7;
            previous.pace.wheel_angles.fill(4090);
            current.pace.wheel_angles.fill(6);
            previous.credit_page = 1;
            current.credit_page = 2;
            current.credits = {{"CURRENT PAGE", true}};
            current.teams = {2, 1, 0};
            const auto rendered = interpolate_championship_finale(previous, current, alpha);
            require(close(rendered.pace.object_to_world[12], float(milliseconds / 10)) && close(rendered.transporter.object_to_world[14], float(milliseconds / 20))
                        && close(rendered.camera_world_to_view[12], -float(milliseconds / 20)),
                "Finale cars and camera must share linear60/144Hz intermediate motion between40ms source ticks");
            require(close(rendered.pace.object_to_world[12] - last, 100.f / float(hz)), "Finale presentation must move continuously across source tick boundaries");
            require(rendered.credit_page == current.credit_page && rendered.credits == current.credits && rendered.teams == current.teams,
                "Finale credits and championship identities must retain the current discrete values");
            require(std::abs(double(rendered.camera_position_q16[0]) / 65536 + rendered.camera_world_to_view[12]) < .00002,
                "Finale camera registration must follow the same rendered camera pose");
            const auto start = interpolate_championship_finale(previous, current, 0), end = interpolate_championship_finale(previous, current, 1);
            require(start.pace.object_to_world == previous.pace.object_to_world && start.pace.wheel_angles == previous.pace.wheel_angles
                        && end.pace.object_to_world == current.pace.object_to_world && end.pace.wheel_angles == current.pace.wheel_angles,
                "Finale presentation must preserve exact source vehicle endpoints and wrapped wheel angles");
            require(
                interpolate_championship_finale(previous, current, .5f).pace.wheel_angles[0] == 4096, "Finale wheels must interpolate through the original4096-unit angle wrap");
            for (const auto change : {0, 1}) {
                auto cut = current;
                if (change == 0) {
                    ++cut.camera_phase;
                } else {
                    ++cut.camera_cut;
                }
                for (const auto fraction : {0.f, .5f}) {
                    const auto snapped = interpolate_championship_finale(previous, cut, fraction);
                    require(snapped.camera_world_to_view == cut.camera_world_to_view && snapped.pace.object_to_world == cut.pace.object_to_world,
                        "Finale phase and authored-camera changes must cut directly to the current frame");
                }
            }
            last = rendered.pace.object_to_world[12];
        }
    }
}
} // namespace

int main() {
    try {
        intermediate_frames();
        rotation_and_angles();
        endpoints_and_discrete_values();
        camera_changes();
        published_camera_positions();
        camera_world_anchor();
        championship_finale();
        std::cout << "PASS: render-only60/144Hz motion, rigid rotation, source angle wrapping and camera cuts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
