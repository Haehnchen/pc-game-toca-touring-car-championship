#pragma once

#include "formats/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace toca::scene {
struct CarLights {
    bool headlights = false, tail = false, brake = false;
    std::array<bool, 4> broken{};
};

struct CarLampTriangle {
    std::array<std::array<float, 3>, 3> positions{};
    std::uint32_t color = 0;
};

struct CarLampFrame {
    std::array<CarLampTriangle, 8> triangles{};
    std::size_t count = 0;
};

//4367f0 selects front/rear and damaged sides;4392d0/439500/439720
//emit the model-specific untextured polygons through499580.
CarLampFrame car_lamp_frame(std::uint32_t car, std::uint32_t detail, const CarLights& lights, bool front_view);

struct CarLampLensTriangle {
    std::array<ModelVertex, 3> vertices{};
    std::size_t node = 0;
    std::uint32_t node_id_flags = 0;
    std::int16_t texture = -1;
    std::uint8_t broken_index = 0;
    bool front = false;
};

// Mega emission follows the intact model's real glass surfaces and their atlas
// coordinates. Cache these local triangles with the model, then use the same
// animated node matrices and livery texture selection as its ordinary draw.
std::vector<CarLampLensTriangle> mega_car_lamp_lenses(const Model& model, std::uint32_t car);
} // namespace toca::scene
