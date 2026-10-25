#pragma once

#include <array>
#include <cstdint>

namespace toca::scene::qualifying_tables {
// Ability and random-variation fractions retain the recovered signed Q16 values.
inline constexpr std::array<std::int32_t, 11> ability_lap_fraction_q16{{
    0,
    65536,
    46530,
    36044,
    24903,
    17694,
    9830,
    4915,
    3276,
    0,
    0,
}};

inline constexpr std::array<std::int32_t, 11> lap_variation_fraction_q16{{
    0,
    6356,
    4915,
    4259,
    4128,
    3473,
    2686,
    1900,
    1114,
    983,
    458,
}};

} // namespace toca::scene::qualifying_tables
