#pragma once

#include <array>
#include <cstdint>

namespace toca::simulation {
struct DetachedPart;
}

namespace toca::scene {
using CarDamageLevels = std::array<std::uint8_t, 17>;

// 41a280 registers the same component slots in high and medium models.
// The low model receives only slot3 through its node709.
std::uint8_t car_damage_variant(std::uint32_t node_id, const CarDamageLevels& levels);
std::uint32_t car_damage_node(std::uint8_t slot);
// 416320/417830: source part centres rotate about their individual pivots.
std::array<float, 16> car_damage_part_transform(const simulation::DetachedPart& part, bool reverse_face);
} // namespace toca::scene
