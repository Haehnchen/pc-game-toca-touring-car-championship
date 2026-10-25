#include "scene/car_damage.hpp"

#include "simulation/damage_parts.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::scene {
namespace {
constexpr std::array<std::uint32_t, 17> nodes{5, 6, 8, 9, 12, 15, 16, 17, 21, 22, 23, 13, 20, 24, 25, 10, 11};
}

std::uint8_t car_damage_variant(std::uint32_t node_id, const CarDamageLevels& levels) {
    node_id &= 0xffffffu;
    const auto found = std::ranges::find(nodes, node_id);
    const auto variant = node_id == 0x2c5 ? levels[3] : found == nodes.end() ? 0u : std::uint32_t(levels[std::size_t(found - nodes.begin())]);
    if (variant > 3) {
        throw std::runtime_error("Car damage requires original mesh variants0..3");
    }
    return std::uint8_t(variant);
}

std::uint32_t car_damage_node(std::uint8_t slot) {
    return nodes.at(slot);
}

std::array<float, 16> car_damage_part_transform(const simulation::DetachedPart& part, bool reverse_face) {
    using namespace simulation::fixed;
    constexpr std::array<Vector, 5> pivots{{{0x19999, -0x3333, 0xb333}, {0x19999, -0x3333, -0xb333}, {0x19999, 0x2666, 0}, {0x23333, -0x4ccc, 0}, {-0x23333, -0x4ccc, 0}}};
    auto basis = part.basis;
    if (reverse_face) {
        basis[2] = scale(basis[2], -one);
    }
    const auto pivot_type = reverse_face && part.type < 2 ? 1 - part.type : part.type;
    const auto& pivot = pivots.at(pivot_type);
    auto origin = part.position;
    for (std::size_t row = 0; row < 3; ++row) {
        origin = sub(origin, scale(basis[row], pivot[row]));
    }
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result[row * 4 + axis] = float(basis[row][axis]) / 65536.f;
        }
        result[12 + row] = float(origin[row]) / 65536.f;
    }
    result[15] = 1;
    return result;
}
} // namespace toca::scene
