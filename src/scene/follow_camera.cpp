#include "scene/follow_camera.hpp"

#include "core/contracts.hpp"
#include "scene/matrix.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::scene {
namespace {
const std::array<std::int16_t, 513>& angles() {
    static const auto result = [] {
        std::array<std::int16_t, 513> table{};
        for (std::size_t index = 0; index < 512; ++index) {
            // 00478b20 generates512 entries; 0045e920 indexes the fixed ratio>>7.
            const auto radians = std::int32_t(std::atan(double(float(index)) / 511.0) * 65536.0);
            table[index] = std::int16_t((std::int64_t(radians) * 651) >> 16);
        }
        // Equal nonzero components index512, overlapping the low word of
        // original cos[0] at0053fc40 (65536). Preserve this source boundary.
        table[512] = 0;
        return table;
    }();
    return result;
}

} // namespace

std::int16_t follow_direction_angle(const std::array<std::int32_t, 3>& forward) {
    const auto x = forward[0], z = forward[2];
    const auto magnitude = [](std::int32_t value) {
        return std::uint64_t(value < 0 ? -std::int64_t(value) : value);
    };
    const auto ax = magnitude(x), az = magnitude(z);
    std::int32_t angle;
    if (az == 0) {
        angle = 0;
    } else if (ax == 0) {
        angle = 1024;
    } else {
        const auto index = ((std::min(ax, az) << 16) / std::max(ax, az)) >> 7;
        angle = angles().at(std::size_t(index));
        if (ax < az) {
            angle = 1024 - angle;
        }
    }
    if (x < 0) {
        return std::int16_t(z < 0 ? angle - 2048 : 2048 - angle);
    }
    return std::int16_t(z < 0 ? -angle : angle);
}

std::int16_t qualifying_camera_heading(std::uint32_t track_id) {
    constexpr std::array<std::int16_t, 10> degrees{105, 350, -150, 0, 0, 0, 180, -45, 0, 0};
    // 45eb60 converts signed degrees with integer truncation then adds a
    // quarter turn. Keep the stored heading, including values outside0..4095.
    return std::int16_t(std::int32_t(degrees.at(track_id)) * 65536 / 5760 + 1024);
}

FollowCamera::FollowCamera(std::int16_t initial_heading, bool micro, std::uint32_t car_id, bool follow, std::uint32_t detail, bool copter, bool actor_camera_available)
    : heading_(initial_heading), micro_(micro), car_id_(car_id), detail_(detail), view_(micro ? 3 : 2), follow_(follow), copter_(copter),
      actor_camera_available_(actor_camera_available) {
    if (car_id > 9 || detail >= graphics_detail_count) {
        throw std::runtime_error("Race camera requires original car0..9 and detail0..4");
    }
    constexpr std::array<bool, 10> positive_side{false, true, true, false, true, false, false, true, false, false};
    head_base_offset_ = head_offset_ = {0, 0x6666, positive_side[car_id] ? 0x6666 : -0x6666};
}

void FollowCamera::set_split_screen(std::optional<bool> vertical) {
    split_vertical_ = vertical;
}

void FollowCamera::initialize_heading(std::int16_t heading) {
    heading_ = heading;
}

void FollowCamera::update(const BodyPose& body, bool state_four) {
    // 4608b0 skips 47fa20's yaw update for CMMICRO in View3.
    if (micro_ && view_ == 3) {
        return;
    }
    auto difference = (std::int32_t(follow_direction_angle(body.basis[0])) + heading_) & 4095;
    if (difference > 2047) {
        difference -= 4096;
    }
    const auto step = state_four ? (difference > 284 ? 22 : difference < -284 ? -22 : difference / 16) : difference / 8;
    heading_ = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(heading_) - std::uint16_t(step)));
}

FollowCameraFrame FollowCamera::frame(const BodyPose& body) const {
    if (view_ == 0) {
        throw std::runtime_error("Camera0 requires its loaded HOR actor or authored trackside consumer");
    }
    FollowCameraFrame result;
    result.world_to_view = view_ == 1 ? cockpit_world_to_view(body, head_anchor_, head_camera_, head_offset_, &result.camera_to_world)
                                      : race_world_to_view(body, heading_, view_, car_id_, micro_, detail_, split_vertical_, &result.camera_to_world);
    std::copy_n(result.camera_to_world.begin() + 12, 3, result.world_position.begin());
    return result;
}

std::array<float, 16> FollowCamera::world_to_view(const BodyPose& body, std::array<float, 16>* published_world) const {
    const auto result = frame(body);
    if (published_world) {
        *published_world = result.camera_to_world;
    }
    return result.world_to_view;
}

std::array<std::int32_t, 3> FollowCamera::position(const BodyPose& body) const {
    const auto world = frame(body).world_position;
    std::array<std::int32_t, 3> result;
    for (std::size_t component = 0; component < 3; ++component) {
        const auto fixed = std::nearbyint(double(world[component]) * 65536.0);
        if (!std::isfinite(fixed) || fixed < std::numeric_limits<std::int32_t>::min() || fixed > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Follow listener is outside original fixed coordinates");
        }
        result[component] = std::int32_t(fixed);
    }
    return result;
}

std::int16_t FollowCamera::heading() const {
    return heading_;
}

std::uint16_t FollowCamera::selected_view() const {
    return view_;
}

bool FollowCamera::trackside() const {
    return trackside_;
}

void FollowCamera::cycle_view() {
    if (rear_) {
        return;
    }
    ++view_;
    trackside_ = false;
    if (car_id_ == 8 && (view_ == 1 || view_ == 5)) {
        view_ = 2;
    } else if (view_ > 4) {
        // 423410 gives CMCOPTER priority over CMFOLLOW. Rain/snow may omit
        // the source HOR actor; preserve usable ordinary views in that case.
        if (copter_ && actor_camera_available_) {
            view_ = 0;
        } else {
            view_ = follow_ ? 0 : 1;
            trackside_ = follow_;
        }
    }
}

void FollowCamera::set_rear_view(bool held) {
    if (held == rear_) {
        return;
    }
    if (held) {
        saved_view_ = view_;
        view_ = 5;
    } else {
        view_ = saved_view_;
    }
    rear_ = held;
}

void FollowCamera::toggle_trackside() {
    // The source assumes camera0 exists. Retain authored mode if weather or
    // track loading omitted that node, rather than dereferencing no actor.
    if (follow_ && ((rear_ ? saved_view_ : view_) != 0 || actor_camera_available_)) {
        trackside_ = !trackside_;
    }
}

std::uint16_t FollowCamera::projection_view() const {
    // Rear press/release does not call48f5f9, retaining prior projection.
    return rear_ ? saved_view_ : view_;
}

bool FollowCamera::rear_held() const {
    return rear_;
}

void FollowCamera::update_cockpit(
    const BodyPose& body, const std::array<std::int32_t, 3>& previous_velocity, const std::array<std::int32_t, 3>& current_velocity, std::int32_t angular_velocity_y) {
    namespace fixed = simulation::fixed;
    using fixed::Vector;
    constexpr fixed::Basis initial_camera{{{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}}};
    if (view_ != 1) {
        //42f950 resets once on exit, preserving42f280's cold zero anchor.
        if (!head_inactive_) {
            head_anchor_ = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
            head_camera_ = initial_camera;
            head_velocity_ = {};
            head_roll_ = 0;
            head_inactive_ = true;
        }
        return;
    }
    head_inactive_ = false;
    auto up = body.basis[1];
    const auto up_difference = fixed::sub(up, head_anchor_[1]);
    auto up_distance = fixed::length(up_difference);
    std::int32_t blend = 0x1999;
    if (up_distance >= 0x4290) {
        up_distance -= 0x428f;
        blend = fixed::add(fixed::mul(std::min(fixed::mul(up_distance, 0x20000), 65536), 0xcccc), 0x1999);
    }
    if (up_distance > 0x41) {
        up = fixed::normalized(fixed::add(head_anchor_[1], fixed::scale(up_difference, blend)));
    }
    auto forward = body.basis[2];
    const auto forward_difference = fixed::sub(forward, head_anchor_[2]);
    if (fixed::length(forward_difference) > 0x41) {
        const auto projection = fixed::mul(fixed::dot(up, forward_difference), fixed::sub(65536, blend));
        forward = fixed::sub(forward, fixed::scale(up, projection));
    }
    forward = fixed::normalized(fixed::project(forward, up));
    head_anchor_ = {fixed::normalized(fixed::cross(up, forward)), up, forward};

    // 42fac0 retains its local translation until the first acceleration update.
    const auto acceleration = fixed::sub(previous_velocity, current_velocity);
    Vector force{fixed::mul(fixed::dot(body.basis[0], acceleration), 0x191eb), 0, fixed::mul(fixed::dot(body.basis[2], acceleration), 0x9999)};
    const auto strength = fixed::length(force);
    bool translate = false;
    if (strength < 0x290) {
        const auto speed = fixed::length(head_velocity_);
        if (speed > 0) {
            if (speed < 0x1ca) {
                head_velocity_ = {};
            } else {
                head_velocity_ = fixed::add(head_velocity_, fixed::scale(fixed::scale(head_velocity_, fixed::reciprocal(-speed)), 0x1ca));
            }
            translate = true;
        }
    } else {
        force = fixed::scale(fixed::scale(force, fixed::reciprocal(strength)), fixed::mul(strength - 0x28f, 0x1999));
        head_velocity_ = fixed::add(head_velocity_, force);
        const auto speed = fixed::length(head_velocity_);
        if (speed > 0x1999) {
            head_velocity_ = fixed::scale(fixed::normalized(head_velocity_), 0x1999);
        }
        translate = true;
    }
    // 42f4c0 uses local spring velocity and fixed reference distance -.5.
    head_camera_ = initial_camera;
    if (translate) {
        // b4/b8/bc are the immutable original local translation. Acceleration
        // displaces that base for this update; it never accumulates a new base.
        head_offset_ = fixed::add(head_base_offset_, head_velocity_);
        if (head_velocity_[0] != 0) {
            const auto direction = fixed::normalized(Vector{head_velocity_[0], 0x8000, 0});
            head_camera_[1] = direction;
            head_camera_[2] = {direction[1], fixed::neg(direction[0]), 0};
        }
        if (head_velocity_[2] != 0) {
            head_camera_[1] = fixed::normalized(fixed::project(Vector{0, 0x8000, fixed::neg(head_velocity_[2])}, head_camera_[2]));
            head_camera_[0] = fixed::normalized(fixed::cross(head_camera_[1], head_camera_[2]));
        }
    }
    const auto roll_target = fixed::abs(angular_velocity_y) < 0xccc ? 0 : fixed::mul(fixed::mul(angular_velocity_y, 0x50000), 0x28b);
    const auto difference = std::bit_cast<std::int16_t>(std::uint16_t(roll_target - head_roll_));
    head_roll_ = std::int16_t(head_roll_ + std::clamp(std::int32_t(difference), -3, 3));
    if (head_roll_ != 0) {
        const auto axis = body.basis[1];
        const auto sn = fixed::sine(-head_roll_), cs = fixed::sine(1024 - head_roll_);
        fixed::Basis rotation{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                const auto product = fixed::mul(axis[row], axis[column]);
                rotation[row][column] = row == column ? fixed::add(fixed::mul(fixed::sub(65536, product), cs), product) : fixed::mul(product, fixed::sub(65536, cs));
            }
        }
        rotation[0][1] = fixed::sub(rotation[0][1], fixed::mul(sn, axis[2]));
        rotation[1][0] = fixed::add(rotation[1][0], fixed::mul(sn, axis[2]));
        rotation[0][2] = fixed::add(rotation[0][2], fixed::mul(sn, axis[1]));
        rotation[2][0] = fixed::sub(rotation[2][0], fixed::mul(sn, axis[1]));
        rotation[1][2] = fixed::sub(rotation[1][2], fixed::mul(sn, axis[0]));
        rotation[2][1] = fixed::add(rotation[2][1], fixed::mul(sn, axis[0]));
        for (auto& vector : head_camera_) {
            vector = fixed::normalized(fixed::world(vector, rotation));
        }
    }
}
} // namespace toca::scene
