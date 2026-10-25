#include "scene/finish_camera.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::scene {
namespace {
simulation::fixed::Vector camera_normalized(simulation::fixed::Vector value) {
    // 492040's large-vector branch divides toward zero before fixed length.
    if (std::any_of(value.begin(), value.end(), [](auto component) {
            return component > 0x640000 || component < -0x640000;
        })) {
        for (auto& component : value) {
            component /= 512;
        }
    }
    return simulation::fixed::normalized(value);
}
} // namespace

FinishCamera::FinishCamera(std::span<const SceneCamera> cameras) : cameras_(cameras.begin(), cameras.end()) {
    if (cameras_.empty() || cameras_.size() > 65535) {
        throw std::runtime_error("Finish camera requires the original nonempty camera actor list");
    }
}

void FinishCamera::reset_selection_cache() {
    remaining_ = 0;
}

void FinishCamera::reset_session() {
    selected_ = 0;
    reset_selection_cache();
}

void FinishCamera::update(const std::array<std::int32_t, 3>& target, bool race_results) {
    //Case13 calls267c0 on the currently selected actor before selecting again.
    //449f50 never restores this scratch: its selected actor0 may differ from
    //the actor which last received the close translation before a warm reset.
    if (race_results && close_origin_) {
        auto& transform = cameras_[selected_].transform;
        std::copy(close_origin_->begin(), close_origin_->end(), transform.begin() + 12);
        close_origin_.reset();
    }
    //45ed30 case13 adds a60-call hold around426b10's own30-call cache.
    if (race_results && results_hold_ < 60) {
        ++results_hold_;
    } else if (remaining_ != 0) {
        --remaining_;
    } else {
        auto best = std::numeric_limits<std::uint64_t>::max();
        for (std::uint32_t index = 0; index < cameras_.size(); ++index) {
            const auto& transform = cameras_[index].transform;
            //426b10 chooses by whole-unit horizontal distance, retaining first ties.
            const auto dx = std::int64_t(transform[12] >> 16) - (target[0] >> 16);
            const auto dz = std::int64_t(transform[14] >> 16) - (target[2] >> 16);
            const auto distance = std::uint64_t(dx * dx + dz * dz);
            if (distance < best) {
                best = distance;
                selected_ = index;
            }
        }
        remaining_ = 30;
    }
    if (race_results) {
        if (previous_results_ != selected_) {
            results_hold_ = 0;
        }
        previous_results_ = selected_;
    }
}

std::uint32_t FinishCamera::selected() const {
    return selected_;
}

std::array<std::int32_t, 3> FinishCamera::position() const {
    return actor_position(selected_);
}

std::array<std::int32_t, 3> FinishCamera::actor_position(std::uint32_t actor) const {
    const auto& transform = cameras_.at(actor).transform;
    return {transform[12], transform[13], transform[14]};
}

std::array<float, 16> FinishCamera::publish(const std::array<std::int32_t, 3>& target, const HpcSurface* surface, bool close, std::array<float, 16>* world) {
    const auto origin = position();
    const auto next = position(target, surface, close);
    const auto result = world_to_view(target, surface, close, world);
    if (close) {
        close_origin_ = origin;
    }
    std::copy(next.begin(), next.end(), cameras_[selected_].transform.begin() + 12);
    return result;
}

std::array<std::int32_t, 3> FinishCamera::position(const std::array<std::int32_t, 3>& target, const HpcSurface* surface, bool close) const {
    using namespace simulation::fixed;
    if (!close) {
        return position();
    }
    if (!surface) {
        throw std::runtime_error("Race-result close camera requires the original HPC surface");
    }
    auto origin = add(target, scale(camera_normalized(sub(position(), target)), 0xf0000));
    const auto ground = surface->prop_sample(origin[0], origin[2], 0); //26830 initializes its cached triangle to0.
    if (!ground) {
        throw std::runtime_error("Race-result close camera has no original HPC ground coverage");
    }
    origin[1] = std::max(origin[1], add(ground->height, 0x30000));
    return origin;
}

std::array<float, 16> FinishCamera::world_to_view(const std::array<std::int32_t, 3>& target, const HpcSurface* surface, bool close, std::array<float, 16>* published_world) const {
    using namespace simulation::fixed;
    const auto authored_origin = position();
    const auto origin = position(target, surface, close);
    // 26830: forward=(target-origin), right=(forward.z,0,-forward.x), up=cross.
    const auto forward = camera_normalized(sub(target, authored_origin));
    const auto right = camera_normalized(Vector{forward[2], 0, neg(forward[0])});
    const auto up = camera_normalized(cross(forward, right));
    if (forward == Vector{} || right == Vector{} || up == Vector{}) {
        throw std::runtime_error("Finish camera target cannot define the original look-at basis");
    }
    const Basis basis{right, up, forward};
    std::array<float, 16> world{}, result{};
    world[15] = 1;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            world[row * 4 + column] = float(basis[row][column]) / one;
        }
        world[12 + row] = float(origin[row]) / one;
    }
    if (published_world) {
        *published_world = world;
    }
    // 47c520 affine inverse, preserving lookup-normalization scale errors.
    result[0] = world[10] * world[5] - world[9] * world[6];
    result[1] = world[9] * world[2] - world[10] * world[1];
    result[2] = world[6] * world[1] - world[5] * world[2];
    result[4] = world[8] * world[6] - world[4] * world[10];
    result[5] = world[0] * world[10] - world[8] * world[2];
    result[6] = world[4] * world[2] - world[0] * world[6];
    result[8] = world[4] * world[9] - world[8] * world[5];
    result[9] = world[8] * world[1] - world[0] * world[9];
    result[10] = world[0] * world[5] - world[4] * world[1];
    const auto determinant = world[0] * result[0] + world[4] * result[1] + world[8] * result[2];
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error("Finish camera has no affine inverse");
    }
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result[row * 4 + column] /= determinant;
        }
    }
    for (std::size_t column = 0; column < 3; ++column) {
        result[12 + column] = -world[12] * result[column] - world[13] * result[4 + column] - world[14] * result[8 + column];
    }
    result[15] = 1;
    return result;
}
} // namespace toca::scene
