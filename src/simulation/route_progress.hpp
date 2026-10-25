#pragma once

#include "simulation/fixed.hpp"

#include <span>
#include <vector>

namespace toca::simulation {
struct RouteCursor {
    std::uint32_t next = 0, previous = 0;
};

struct RouteRecovery {
    RouteCursor cursor;
    bool recovered = false;
};

//4019f0's initialized nearest map and4046a3's separate human recovery step.
class NearestRouteMap {
    std::vector<fixed::Vector> targets_;
    std::vector<std::uint8_t> candidates_;

    static bool nearby(std::int32_t origin, std::int32_t candidate, std::int32_t count) {
        constexpr std::int32_t radius = 23;
        if (count <= origin + radius && (origin <= candidate || candidate < origin + radius - count)) {
            return true;
        }
        if (origin - radius < 0 && (candidate < origin || origin - radius + count <= candidate)) {
            return true;
        }
        return candidate < origin ? origin - candidate < radius : candidate - origin < radius;
    }

    static std::int32_t distance_squared(const fixed::Vector& first, const fixed::Vector& second) {
        const auto dx = fixed::sub(first[0], second[0]) >> 16, dz = fixed::sub(first[2], second[2]) >> 16;
        return fixed::bits(std::uint32_t(dx) * std::uint32_t(dx) + std::uint32_t(dz) * std::uint32_t(dz));
    }

public:
    NearestRouteMap() = default;

    explicit NearestRouteMap(std::span<const fixed::Vector> targets) : targets_(targets.begin(), targets.end()), candidates_(targets.size()) {
        if (targets.empty() || targets.size() > 256) {
            throw std::runtime_error("Nearest-route map requires1..256 POINTS targets");
        }
        const auto count = std::int32_t(targets.size());
        for (std::int32_t origin = 0; origin < count; ++origin) {
            std::int32_t best = 0x17d7840;
            for (std::int32_t candidate = 0; candidate < count; ++candidate) {
                if (!nearby(origin, candidate, count)) {
                    const auto distance = distance_squared(targets[candidate], targets[origin]);
                    if (distance < best) {
                        candidates_[origin] = std::uint8_t(candidate);
                        best = distance;
                    }
                }
            }
        }
    }

    std::span<const std::uint8_t> candidates() const {
        return candidates_;
    }

    RouteRecovery recover(RouteCursor cursor, const fixed::Vector& position) const {
        if (cursor.next >= targets_.size() || cursor.previous >= targets_.size()) {
            throw std::runtime_error("Nearest-route cursor exceeds initialized POINTS");
        }
        const auto candidate = std::uint32_t(candidates_[cursor.next]);
        if (distance_squared(position, targets_[candidate]) < distance_squared(position, targets_[cursor.next])) {
            return {{candidate, (candidate + std::uint32_t(targets_.size()) - 1) % std::uint32_t(targets_.size())}, true};
        }
        return {cursor, false};
    }
};

enum class RouteCrossing {
    none,
    forward,
    backward
};

struct RouteCursorStep {
    RouteCursor cursor;
    RouteCrossing crossing = RouteCrossing::none;
};

//402910 mode1 uses separately shifted products; equality is the passed side.
inline bool route_boundary_passed(const fixed::Vector& position, const fixed::Vector& first, const fixed::Vector& second) {
    using namespace fixed;
    return add(mul(sub(first[2], second[2]), sub(position[0], first[0])), mul(sub(second[0], first[0]), sub(position[2], first[2]))) >= 0;
}

//403dd0 advances one boundary at most, giving the forward test priority.
inline RouteCursorStep advance_route_cursor(RouteCursor cursor, std::uint32_t count, bool passed_next, bool passed_previous) {
    if (count == 0 || cursor.next >= count || cursor.previous >= count) {
        throw std::runtime_error("Route cursor exceeds initialized POINTS");
    }
    if (passed_next) {
        return {{(cursor.next + 1) % count, cursor.next}, RouteCrossing::forward};
    }
    if (!passed_previous) {
        return {{cursor.previous, (cursor.previous + count - 1) % count}, RouteCrossing::backward};
    }
    return {cursor, RouteCrossing::none};
}
} // namespace toca::simulation
