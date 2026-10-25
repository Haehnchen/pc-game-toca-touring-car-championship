#include "frontend/menu_transition.hpp"

#include "frontend/builtin_data.hpp"

#include <stdexcept>

namespace toca::frontend {
MenuTransitionResources load_menu_transition_resources() {
    return builtin::menu_transition();
}

MenuPoint menu_transition_point(const MenuQuad& quad, float u, float v) {
    const auto& points = quad.corners;
    const MenuPoint left{points[0].x + (points[1].x - points[0].x) * v, points[0].y + (points[1].y - points[0].y) * v};
    const MenuPoint right{points[3].x + (points[2].x - points[3].x) * v, points[3].y + (points[2].y - points[3].y) * v};
    // FUN_00471ca3 takes destination y only from the left edge. The right
    // pair's y coordinates belong to the separate static photo clip polygon.
    return {left.x + (right.x - left.x) * u, left.y};
}

MenuTransitionPhase menu_transition_phase(std::uint64_t elapsed_ns, int direction, bool startup) {
    if ((direction != 1 && direction != -1) || (startup && direction != 1)) {
        throw std::invalid_argument("Frontend roll direction must be +1 or -1; startup rolls forward");
    }
    constexpr std::uint64_t second = 1'000'000'000;
    const auto duration_ticks = startup ? 9u : 14u;
    if (elapsed_ns >= (duration_ticks * second + 29) / 30) {
        return {};
    }
    const auto ticks = std::uint32_t(elapsed_ns * 30 / second);
    const auto frame = std::uint32_t((int(startup ? 5 : 0) + direction * int(ticks) + 14) % 14);
    return {frame, std::uint32_t((int(frame) + direction + 14) % 14), float(elapsed_ns * 30 % second) / float(second), true};
}
} // namespace toca::frontend
