#pragma once

#include <cstdint>

namespace toca {
enum class GameMode : std::uint32_t {
    single_race = 0,
    championship = 1,
    time_trial = 2,
    linkup = 3
};
} // namespace toca
