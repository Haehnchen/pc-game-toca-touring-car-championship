#pragma once

#include "scene/hud.hpp"

namespace toca::scene::hud_sprites {
// Rectangles are pixel coordinates in the original 640-wide HUD atlases.
inline constexpr HudRect speed_background{15, 190, 150, 20};
inline constexpr HudRect rpm_background{123, 144, 116, 41};
inline constexpr HudRect lap_background{0, 212, 186, 28};
inline constexpr HudRect timing_labels{178, 186, 72, 25};
inline constexpr HudRect position_background{0, 241, 187, 15};
inline constexpr HudRect split_label{1, 132, 75, 11};

inline constexpr std::array<HudRect, 12> rpm_segments{{
    {5, 179, 20, 11},
    {28, 175, 20, 15},
    {53, 171, 19, 18},
    {75, 169, 20, 19},
    {96, 164, 19, 17},
    {20, 150, 16, 16},
    {39, 147, 16, 16},
    {55, 147, 14, 17},
    {69, 146, 12, 17},
    {83, 145, 10, 16},
    {93, 145, 9, 16},
    {102, 145, 17, 16},
}};

struct Position {
    int x, y;
};

inline constexpr std::array<Position, 12> rpm_positions{{
    {453, 457},
    {457, 449},
    {465, 443},
    {472, 438},
    {482, 435},
    {492, 433},
    {501, 431},
    {511, 429},
    {522, 428},
    {532, 427},
    {542, 427},
    {551, 427},
}};

inline constexpr std::array<HudRect, 12> large_glyphs{{
    {60, 70, 62, 35},
    {0, 0, 36, 35},
    {36, 0, 63, 35},
    {99, 0, 62, 35},
    {161, 0, 60, 35},
    {0, 35, 61, 35},
    {61, 35, 61, 35},
    {123, 35, 57, 35},
    {180, 35, 62, 35},
    {0, 70, 60, 35},
    {122, 70, 65, 35},
    {187, 70, 64, 35},
}};

inline constexpr std::array<HudRect, 14> small_glyphs{{
    {0, 106, 19, 12},
    {21, 106, 19, 12},
    {40, 106, 19, 12},
    {59, 106, 19, 12},
    {78, 106, 19, 12},
    {97, 106, 19, 12},
    {116, 106, 19, 12},
    {135, 106, 19, 12},
    {154, 106, 19, 12},
    {173, 106, 19, 12},
    {192, 106, 7, 12},
    {200, 106, 19, 12},
    {219, 106, 11, 12},
    {235, 106, 7, 12},
}};

inline constexpr int unknown_time_y = 118;
inline constexpr int gear_atlas_row_offset = 106;
} // namespace toca::scene::hud_sprites
