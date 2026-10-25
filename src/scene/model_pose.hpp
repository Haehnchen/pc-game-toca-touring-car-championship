#pragma once

#include "formats/model.hpp"
#include "scene/car_damage.hpp"
#include "scene/car_light_glow.hpp"
#include "scene/car_lights.hpp"
#include "scene/car_shadow.hpp"
#include "scene/car_visual.hpp"
#include "scene/cockpit.hpp"

#include <optional>

namespace toca::scene {
struct HorizonModelFrame;

// A published render choice can be retained with a frozen pose. Geometry,
// materials and node transforms still come from the shared model consumer.
struct CarRenderSelection {
    std::uint8_t model_detail = 0;
    std::uint8_t root_mask = 1;
    std::vector<std::uint8_t> node_masks{};
    std::vector<std::int16_t> node_variants{};
};

struct CarPose {
    std::array<float, 16> object_to_world;
    std::int16_t steering_angle = 0;
    std::array<std::int16_t, 4> wheel_angles{};
    std::optional<CarShadowFootprint> shadow{};
    CarDamageLevels damage_levels{};
    CarVisualAngles visual_angles{};
    bool second_driver = false;
    CarLights lights{};
    std::optional<CarLightGroundInput> light_ground{};
    std::optional<CarRenderSelection> render_selection{};
    std::uint32_t actor_id = UINT32_MAX;
    std::optional<CockpitSnapshot> cockpit{};
    bool remap_livery = true;
};

std::uint32_t local_car_model_index(std::uint32_t detail);
// 44df6e uses a separate Vectra livery for actor5, retaining physical car2.
std::uint32_t rival_model_id(std::uint32_t actor_id, std::uint32_t car_id);
// 44d7f9 prepares only the medium model at graphics detail0/1.
Model prepare_car_model(const Model& model, std::uint32_t model_id, std::uint32_t graphics_detail, std::uint32_t model_index);
// Recompute every node while retaining the output's allocated storage.
void model_world_transforms_into(std::vector<std::array<float, 16>>& output, const Model& model, const std::array<float, 16>& object_to_world);
void horizon_actor_world_transforms_into(std::vector<std::array<float, 16>>& output, const Model& model, const HorizonModelFrame& frame);
void car_world_transforms_into(std::vector<std::array<float, 16>>& output, const Model& model, const CarPose& pose, std::uint32_t detail);
std::vector<std::array<float, 16>> model_world_transforms(const Model& model, const std::array<float, 16>& object_to_world);
std::vector<std::array<float, 16>> horizon_actor_world_transforms(const Model& model, const HorizonModelFrame& frame);
std::vector<std::array<float, 16>> car_world_transforms(const Model& model, const CarPose& pose, std::uint32_t detail);
} // namespace toca::scene
