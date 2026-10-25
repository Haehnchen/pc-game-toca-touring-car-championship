#pragma once

#include <array>
#include <cstdint>

namespace toca::scene::presentation_tables {
struct PanelColor {
    std::uint32_t red, green, blue, alpha;

    constexpr std::uint32_t argb() const {
        return (alpha << 24) | (red << 16) | (green << 8) | blue;
    }
};

inline constexpr PanelColor result_panel_color{.red = 10, .green = 10, .blue = 10, .alpha = 144};

struct CarIconRect {
    int x, y, width, height;
};

// Atlas rectangles in native car-ID order, shared by qualifying and race results.
inline constexpr std::array<CarIconRect, 10> qualifying_car_icons{{
    {225, 230, 28, 23},
    {216, 129, 39, 15},
    {232, 70, 24, 24},
    {172, 131, 43, 11},
    {190, 235, 33, 14},
    {229, 1, 27, 19},
    {187, 72, 26, 25},
    {213, 72, 18, 25},
    {187, 211, 30, 23},
    {239, 144, 17, 42},
}};
} // namespace toca::scene::presentation_tables
