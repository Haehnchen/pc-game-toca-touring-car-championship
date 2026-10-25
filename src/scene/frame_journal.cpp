#include "scene/frame_journal.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace toca::scene {
SceneFrameJournal::SceneFrameJournal(Visibility visibility) : visibility_(std::move(visibility)) {
    car_roots_.fill(no_visibility_entry);
}

void SceneFrameJournal::advance(std::uint64_t order) {
    if (last_order_ && order <= *last_order_) {
        throw std::runtime_error("Scene journal operation order is not strictly increasing");
    }
    last_order_ = order;
    published_ = false;
}

void SceneFrameJournal::initialize(std::uint64_t order, std::span<const SceneJournalRoot> roots) {
    if (initialized_) {
        throw std::runtime_error("Scene journal cannot replace an initialized root world");
    }
    std::array<bool, 16> actors{};
    std::size_t follows = 0, cockpits = 0;
    for (const auto& root : roots) {
        if (root.role == SceneJournalRootRole::car_parent) {
            if (root.actor >= actors.size() || actors[root.actor]) {
                throw std::runtime_error("Scene journal car roles must be unique and bounded");
            }
            actors[root.actor] = true;
        } else if (root.role == SceneJournalRootRole::follow_anchor) {
            ++follows;
        } else if (root.role == SceneJournalRootRole::cockpit_anchor) {
            ++cockpits;
        } else if (root.role != SceneJournalRootRole::external && root.role != SceneJournalRootRole::authored_camera) {
            throw std::runtime_error("Scene journal root has an unsupported role");
        }
    }
    for (const auto actor : actors) {
        if (!actor) {
            throw std::runtime_error("Scene journal requires all sixteen initial physical car roots");
        }
    }
    if (follows != 1 || cockpits > 1) {
        throw std::runtime_error("Scene journal requires one initialized follow-anchor role");
    }
    advance(order);
    roots_.assign(roots.begin(), roots.end());
    root_positions_.reserve(roots_.size());
    for (std::size_t index = 0; index < roots_.size(); ++index) {
        const auto& root = roots_[index];
        root_positions_.push_back(root.local_position);
        const auto id = visibility_.register_root(root.local_position, root.flags, root.role == SceneJournalRootRole::external ? root.external_manual_mask : 15);
        if (id != index) {
            throw std::runtime_error("Scene journal requires a fresh canonical visibility owner");
        }
        if (root.role == SceneJournalRootRole::car_parent) {
            car_roots_[root.actor] = id;
        }
        if (root.role == SceneJournalRootRole::follow_anchor) {
            follow_root_ = id;
        }
        if (root.role == SceneJournalRootRole::cockpit_anchor) {
            cockpit_root_ = id;
        }
    }
    initialized_ = true;
}

void SceneFrameJournal::migrate(std::uint64_t order, std::uint32_t root, const std::array<float, 3>& world_position) {
    if (!initialized_ || root >= roots_.size()) {
        throw std::runtime_error("Scene journal migration has no registered root");
    }
    advance(order);
    visibility_.migrate_root(root, world_position);
    root_positions_[root] = world_position;
}

void SceneFrameJournal::reset_parent_masks(std::uint64_t order, std::optional<std::uint32_t> qualifying_local) {
    if (!initialized_ || selected_ || models_ || (qualifying_local && *qualifying_local >= 16)) {
        throw std::runtime_error("Scene parent reset requires an initialized inactive source scene");
    }
    advance(order);
    for (std::uint32_t actor = 0; actor < car_roots_.size(); ++actor) {
        const bool active = !qualifying_local || actor % 4 == *qualifying_local % 4;
        visibility_.set_root_mask(car_roots_[actor], active ? 15 : 14);
    }
}

void SceneFrameJournal::external_mask(std::uint64_t order, std::uint32_t root, std::uint8_t mask) {
    if (!initialized_ || root >= roots_.size() || roots_[root].role != SceneJournalRootRole::external) {
        throw std::runtime_error("Observed car/follow masks cannot enter the scene journal");
    }
    advance(order);
    visibility_.set_root_mask(root, mask);
}

void SceneFrameJournal::select(std::uint64_t order, const VisibilityCamera& camera, std::span<const std::uint32_t> ancestor_roots, bool all_sectors) {
    if (!initialized_ || camera.view_mask != 1) {
        throw std::runtime_error("Scene journal selection requires initialized single-view roots");
    }
    if (ancestor_roots.size() != 1 || ancestor_roots[0] >= roots_.size() || roots_[ancestor_roots[0]].role == SceneJournalRootRole::external) {
        throw std::runtime_error("Source view selection requires one registered camera ancestor");
    }
    selected_root_ = ancestor_roots[0];
    selected_camera_ = camera;
    std::vector<std::uint16_t> ancestors;
    for (const auto root : ancestor_roots) {
        if (root >= roots_.size()) {
            throw std::runtime_error("Scene journal ancestor has no registered root identity");
        }
        ancestors.push_back(visibility_.root_sector(root));
    }
    advance(order);
    visibility_.select(camera, ancestors, all_sectors);
    selected_ = true;
    models_.reset();
}

void SceneFrameJournal::models(std::uint64_t order, const SceneJournalModels& input) {
    if (!selected_ || models_ || input.local_actor >= 16 || input.graphics_detail > 3 || input.view < 1 || input.view > 5
        || input.view_factor_q16 != source_view_factor(std::uint16_t(input.view))) {
        throw std::runtime_error("Scene journal requires one source view/factor model call after each selection");
    }
    const auto expected_root = input.view == 1 ? cockpit_root_ : input.view == 2 || input.view == 3 ? follow_root_ : car_roots_[input.local_actor];
    const bool authored_root = roots_[selected_root_].role == SceneJournalRootRole::authored_camera;
    if (input.authored_camera ? !authored_root : selected_root_ != expected_root || expected_root == no_visibility_entry) {
        throw std::runtime_error("Selected camera ancestry disagrees with the source view hierarchy");
    }
    for (const auto mode : input.body_modes) {
        if (mode > 4) {
            throw std::runtime_error("Scene journal has an unsupported physical body mode");
        }
    }
    std::array<CarScenePose, 15> rivals;
    std::size_t count = 0;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (actor == input.local_actor) {
            continue;
        }
        auto& pose = rivals[count++];
        pose.actor = actor;
        pose.source_position = input.body_positions[actor];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            pose.position[axis] = float(input.body_positions[actor][axis]) / 65536.f;
        }
        pose.present = input.body_modes[actor] != 4;
    }
    std::array<std::int32_t, 3> observer;
    if (input.authored_camera) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto value = std::nearbyint(double(selected_camera_.world_position[axis]) * 65536.0);
            if (!std::isfinite(value) || value < INT32_MIN || value > INT32_MAX) {
                throw std::runtime_error("Authored scene observer exceeds original fixed coordinates");
            }
            observer[axis] = std::int32_t(value);
        }
    } else {
        observer = source_fleet_scene_observer(std::uint16_t(input.view), selected_camera_.world_position, input.body_positions[input.local_actor], input.local_forward,
            root_positions_[car_roots_[input.local_actor]]);
    }
    const std::array<float, 3> body{float(observer[0]) / 65536.f, float(observer[1]) / 65536.f, float(observer[2]) / 65536.f};
    advance(order);
    // Canonical PE .data initializes adeb8 to1;4271d0 only ever sets it to1.
    // It is retained source owner context, never an observed LOD request input.
    const bool local_visible = input.authored_camera || input.view != 1;
    fleet_.select_models({body, input.graphics_detail, local_visible, true, observer}, rivals, true);
    models_ = input;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        visibility_.set_root_mask(car_roots_[actor], input.body_modes[actor] == 4 ? 14 : 15);
        const auto active = input.body_modes[actor] != 4 && (actor != input.local_actor || local_visible);
        masks_[actor] = {std::uint8_t(active), std::uint8_t(active && model_detail(actor) < 2), std::uint8_t(active && input.body_modes[actor] == 0)};
    }
}

void SceneFrameJournal::publish(std::uint64_t order) {
    if (!selected_ || !models_) {
        throw std::runtime_error("Scene journal geometry publication requires actual selection and model calls");
    }
    advance(order);
    visibility_.refresh_roots(1);
    context_.visibility = visibility_.frame();
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto& target = context_.actors[actor];
        target.physical = visibility_.root_registration(car_roots_[actor]);
        target.sector_visible = context_.visibility.sector_visible(target.physical.sector);
        target.model_detail = actor == models_->local_actor ? (models_->graphics_detail < 2 ? 1 : 0) : fleet_.rival(actor).model_detail;
        target.render_mask = context_.visibility.root_mask(car_roots_[actor]);
        const auto active = models_->body_modes[actor] != 4 && (actor != models_->local_actor || models_->authored_camera || models_->view != 1);
        target.contact_effect_mask = active && target.model_detail == 0 ? 1 : 0;
        target.wheel_effect_mask = target.engine_effect_mask = active ? 1 : 0;
        masks_[actor] = {std::uint8_t(active), std::uint8_t(active && target.model_detail < 2), std::uint8_t(active && models_->body_modes[actor] == 0)};
    }
    published_ = true;
    selected_ = false;
    models_.reset();
}

RootSectorRegistration SceneFrameJournal::root_registration(std::uint32_t root) const {
    return visibility_.root_registration(root);
}

std::uint8_t SceneFrameJournal::root_manual_mask(std::uint32_t root) const {
    return visibility_.root_manual_mask(root);
}

std::uint8_t SceneFrameJournal::model_detail(std::uint32_t actor) const {
    if (actor >= 16 || (!models_ && !published_)) {
        throw std::runtime_error("Scene journal has no model publication for this actor");
    }
    if (models_) {
        return actor == models_->local_actor ? (models_->graphics_detail < 2 ? 1 : 0) : fleet_.rival(actor).model_detail;
    }
    return context_.actors[actor].model_detail;
}

const VisibilityFrame& SceneFrameJournal::visibility() const {
    return visibility_.frame();
}

const FleetSceneContext& SceneFrameJournal::context() const {
    if (!published_) {
        throw std::runtime_error("Scene journal has no completed geometry publication");
    }
    return context_;
}

const std::array<SceneJournalCarMasks, 16>& SceneFrameJournal::car_masks() const {
    if (!published_ && !models_) {
        throw std::runtime_error("Scene journal has no completed car-effect publication");
    }
    return masks_;
}
} // namespace toca::scene
