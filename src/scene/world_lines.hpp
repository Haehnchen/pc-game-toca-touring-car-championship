#pragma once

#include <array>
#include <cstdint>

namespace toca::scene {
struct QueuedWorldLine {
    std::array<std::int32_t, 3> from{}, to{}; // Signed Q16 world coordinates.
    std::uint32_t color = 0xffffffff;
    bool operator==(const QueuedWorldLine&) const = default;
};
} // namespace toca::scene
