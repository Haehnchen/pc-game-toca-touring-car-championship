#include "scene/fleet_scene.hpp"

#include "core/contracts.hpp"
#include "core/source_fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
std::int32_t source_view_factor(std::uint16_t view) {
    constexpr std::array<std::int32_t, 6> factors{0, 0x20000, 0, 0, 0x140000, 0xa0000};
    if (view < 1 || view > 5) {
        throw std::runtime_error("Ordinary fleet observer requires camera view1..5");
    }
    return factors[view];
}

std::array<std::int32_t, 3> source_fleet_scene_observer(std::uint16_t view, const std::array<float, 3>& camera_position, const std::array<std::int32_t, 3>& body,
    const std::array<std::int32_t, 3>& forward, std::optional<std::array<float, 3>> local_parent_position) {
    const auto factor = source_view_factor(view);
    auto result = body;
    if (factor == 0) {
        return result;
    }
    const auto round_fixed = [](double value) {
        const auto rounded = std::nearbyint(value);
        if (!std::isfinite(rounded) || rounded < INT32_MIN || rounded > INT32_MAX) {
            throw std::runtime_error("Fleet observer exceeds original fixed coordinates");
        }
        return std::int32_t(rounded);
    };
    for (const auto axis : {0u, 2u}) {
        //View1 starts at the separately published body-parent world position,
        //adds forward*factor, then applies the same factor to that displacement.
        const auto parent = local_parent_position ? double((*local_parent_position)[axis]) * 65536.0 : double(float(body[axis]));
        const auto base = view == 1 ? source_fixed::add(round_fixed(parent), source_fixed::mul(forward[axis], factor)) : round_fixed(double(camera_position[axis]) * 65536.0);
        result[axis] = source_fixed::add(body[axis], source_fixed::mul(source_fixed::sub(base, body[axis]), factor));
    }
    return result;
}

std::array<float, 3> fleet_scene_observer(std::uint16_t view, bool authored_camera, const std::array<float, 3>& camera_position, const std::array<float, 3>& local_body_position,
    const std::array<std::int32_t, 3>& local_forward, std::optional<std::array<float, 3>> local_parent_position) {
    if (authored_camera || view < 1 || view > 5) {
        return camera_position;
    }
    std::array<std::int32_t, 3> body;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto value = std::nearbyint(double(local_body_position[axis]) * 65536.0);
        if (!std::isfinite(value) || value < INT32_MIN || value > INT32_MAX) {
            throw std::runtime_error("Fleet body observer exceeds original fixed coordinates");
        }
        body[axis] = std::int32_t(value);
    }
    const auto fixed = source_fleet_scene_observer(view, camera_position, body, local_forward, local_parent_position);
    return {float(fixed[0]) / 65536.f, float(fixed[1]) / 65536.f, float(fixed[2]) / 65536.f};
}

void FleetScene::reset_registration() {
    player_initialized_ = false;
    rival_initialized_.fill(false);
}

void FleetScene::select_models(const FleetSceneView& view, std::span<const CarScenePose> rivals, bool force) {
    if (view.graphics_detail >= graphics_detail_count || rivals.size() > rivals_.size()) {
        throw std::runtime_error("Fleet scene requires a supported detail and at most sixteen actors");
    }
    const auto finite = [](const auto& position) {
        return std::ranges::all_of(position, [](float value) {
            return std::isfinite(value);
        });
    };
    if (!finite(view.observer)) {
        throw std::runtime_error("Fleet scene requires finite camera and actor positions");
    }
    std::array<bool, 16> present{};
    std::vector<std::pair<std::int64_t, std::uint32_t>> distances;
    for (const auto& rival : rivals) {
        if (rival.actor >= rivals_.size() || present[rival.actor] || !finite(rival.position)) {
            throw std::runtime_error("Fleet scene requires unique bounded rival identities");
        }
        present[rival.actor] = true;
        if (!rival.present) {
            continue;
        }
        std::int64_t squared = 0;
        if (view.source_observer && !rival.source_position) {
            throw std::runtime_error("Source fleet observer requires generated fixed actor positions");
        }
        for (const auto axis : {0u, 2u}) {
            const auto displacement = view.source_observer ? std::int64_t(source_fixed::sub((*rival.source_position)[axis], (*view.source_observer)[axis]))
                                                           : std::int64_t(std::nearbyint((rival.position[axis] - view.observer[axis]) * 65536.f));
            const auto delta = std::clamp<std::int64_t>(displacement, -0x500000LL, 0x500000LL);
            squared += (delta * delta) >> 16;
        }
        distances.emplace_back(squared, rival.actor);
    }
    const auto detail = view.graphics_detail;
    const bool mega = detail == graphics_mega_detail;
    const std::int64_t near_limit = mega ? 0x6400000 : detail < 2 ? 0x190000 : detail == 2 ? 0xc40000 : 0x1900000;
    const std::int64_t middle_limit = mega ? 0x19000000 : detail < 2 ? 0x1900000 : detail == 2 ? 0x3100000 : 0x6400000;
    const std::size_t near_count = mega ? rivals_.size() : detail < 2 ? 0 : detail == 2 ? 1 : 4;
    const std::size_t middle_count = mega ? rivals_.size() : 5;
    std::ranges::stable_sort(distances, [force](const auto& left, const auto& right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        //4271d0 downgrades the first/farthest actor on strict-distance ties.
        //The canonical force path therefore retains the later actor IDs.
        return force && left.second > right.second;
    });
    std::size_t close = 0, middle = 0;
    for (const auto& [distance, actor] : distances) {
        const std::uint8_t wanted = distance < near_limit && close < near_count ? 0 : distance < middle_limit && middle < middle_count ? 1 : 2;
        close += wanted == 0;
        middle += wanted == 1;
        if (force && rivals_[actor].model_detail != wanted) {
            rivals_[actor].model_detail = wanted;
            if (!lod_holds_[actor]) {
                lod_holds_[actor] = 11;
            }
        }
        if (lod_holds_[actor]) {
            --lod_holds_[actor];
        } else if (rivals_[actor].model_detail != wanted) {
            rivals_[actor].model_detail = wanted;
            lod_holds_[actor] = 10;
        }
    }
}

void FleetScene::update(const VisibilityData& data, const VisibilityFrame& visibility, const FleetSceneView& view, const CarScenePose& player, std::span<const CarScenePose> rivals,
    bool force_models) {
    select_models(view, rivals, force_models);
    const auto detail = view.graphics_detail;
    const auto refresh = [&](CarSceneContext& target, bool& initialized, const CarScenePose& pose, bool model_visible) {
        target.render_mask = pose.present ? pose.render_mask : 0;
        target.contact_effect_mask = pose.present && model_visible && target.model_detail == 0 ? pose.manual_mask : 0;
        // 4469a0 initializes every car's gate to1. In singleplayer4271d0
        // retains it;446e50 reads manual model+140, never PVS output+138.
        target.wheel_effect_mask = target.engine_effect_mask = pose.present && (!view.source_local_visibility || model_visible) ? pose.manual_mask : 0;
        if (pose.present) {
            if (!initialized) {
                target.physical = initial_root_registration(data, pose.position);
                initialized = true;
            }
            update_root_registration(data, target.physical, pose.position, 1);
        }
        target.sector_visible = visibility.sector_visible(target.physical.sector);
    };
    player_.model_detail = detail < 2 ? 1 : 0;
    refresh(player_, player_initialized_, player, view.player_model_visible);
    for (auto& rival : rivals_) {
        rival.render_mask = rival.contact_effect_mask = rival.wheel_effect_mask = rival.engine_effect_mask = 0;
        rival.sector_visible = visibility.sector_visible(rival.physical.sector);
    }
    for (const auto& pose : rivals) {
        refresh(rivals_[pose.actor], rival_initialized_[pose.actor], pose, true);
    }
    visibility_ = visibility;
}

const CarSceneContext& FleetScene::player() const {
    return player_;
}

const CarSceneContext& FleetScene::rival(std::uint32_t actor) const {
    return rivals_.at(actor);
}

FleetSceneContext FleetScene::context(std::uint32_t local_actor) const {
    if (local_actor >= rivals_.size()) {
        throw std::runtime_error("Fleet scene local identity exceeds sixteen actors");
    }
    FleetSceneContext result{rivals_, visibility_};
    result.actors[local_actor] = player_;
    return result;
}
} // namespace toca::scene
