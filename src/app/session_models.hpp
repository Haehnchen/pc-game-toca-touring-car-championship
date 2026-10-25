#pragma once

#include "frontend/resources.hpp"
#include "scene/cockpit.hpp"

#include <array>
#include <optional>
#include <utility>

namespace toca {
// One scene/detail selection. Primary resources outlive this cache; fixed slots
// keep references stable until both players and their views have been destroyed.
class SessionModels {
    const frontend::CarModels& primary_car_;
    const scene::CockpitResources& primary_cockpit_;
    const std::filesystem::path mega_models_;
    std::array<std::optional<frontend::CarModels>, 11> cars_;
    std::array<std::optional<scene::CockpitResources>, 11> cockpits_;

public:
    SessionModels(const frontend::CarModels& car, const scene::CockpitResources& cockpit, std::filesystem::path mega_models)
        : primary_car_(car), primary_cockpit_(cockpit), mega_models_(std::move(mega_models)) {}

    SessionModels(const SessionModels&) = delete;
    SessionModels& operator=(const SessionModels&) = delete;

    const frontend::CarModels& car(const AssetStore& assets, std::uint32_t id) {
        if (id == primary_car_.car_id) {
            return primary_car_;
        }
        auto& model = cars_.at(id);
        if (!model) {
            model.emplace(frontend::load_car_models(assets, id, mega_models_));
        }
        return *model;
    }

    const frontend::CarModels& car(std::uint32_t id) const {
        return id == primary_car_.car_id ? primary_car_ : cars_.at(id).value();
    }

    const scene::CockpitResources& cockpit(const AssetStore& assets, std::uint32_t id) {
        if (id == primary_car_.car_id) {
            return primary_cockpit_;
        }
        auto& model = cockpits_.at(id);
        if (!model) {
            model.emplace(scene::load_cockpit_resources(assets, id, mega_models_, primary_cockpit_.shared_roles));
        }
        return *model;
    }
};
} // namespace toca
