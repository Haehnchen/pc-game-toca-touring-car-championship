#include "simulation/pit_lane.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::simulation {
namespace {
using namespace fixed;

// 4017b0, unrotated endpoint plane. Separate shifted products are significant.
bool inside(const Vector& endpoint, const Vector& position, const Vector& other) {
    return add(mul(sub(endpoint[0], other[0]), sub(endpoint[0], position[0])), mul(sub(endpoint[2], other[2]), sub(endpoint[2], position[2]))) >= 0;
}

std::int32_t integer_distance_squared(const Vector& a, const Vector& b) {
    const auto x = sub(a[0], b[0]) >> 16, z = sub(a[2], b[2]) >> 16;
    return bits(std::uint32_t(x) * std::uint32_t(x) + std::uint32_t(z) * std::uint32_t(z));
}
} // namespace

PitLane::PitLane(const scene::PitRoute& route, std::uint32_t track) : route_(&route), track_(track) {
    // The source uses an unsigned byte cursor and exits at count-2. Four
    // records are needed for both exit stages; larger cursors cannot exit.
    if (route.points.size() < 4 || route.points.size() > 257 || track > 9) {
        throw std::runtime_error("Pit lane requires a source byte-addressable route and valid track");
    }
    const auto& first = route.points.front();
    const auto first_half = length(sub(first.right, first.best)) >> 16;
    const auto second_half = length(sub(first.left, first.best)) >> 16;
    const auto radius = std::max(first_half, second_half);
    entry_distance_squared_ = bits(std::uint32_t(radius) * std::uint32_t(radius) + 2500);
}

std::uint8_t PitLane::zone(std::uint32_t point, const std::array<std::int32_t, 3>& position) const {
    return source_zone(route_->points.at(point), position);
}

std::uint8_t PitLane::source_zone(const scene::PitRoutePoint& line, const std::array<std::int32_t, 3>& position) {
    auto delta = sub(position, line.left);
    delta[1] = 0;
    delta = normalized(delta); // 402910 selector2 normalizes before the cross.
    return std::uint8_t(1 + (add(mul(neg(sub(line.right[2], line.left[2])), delta[0]), mul(sub(line.right[0], line.left[0]), delta[2])) >= 0));
}

PitLaneTransition PitLane::update(const std::array<std::int32_t, 3>& position, bool human_owned) {
    PitLaneTransition result;
    if (!active_) {
        // 403dd0 excludes Lavaland even though KEEP contains a PITS member.
        if (track_ == 9) {
            return result;
        }
        const auto next_zone = zone(0, position);
        const auto& first = route_->points.front();
        if (next_zone == 2 && zone_ == 1 && inside(first.right, position, first.left) && inside(first.left, position, first.right)
            && integer_distance_squared(position, first.best) < entry_distance_squared_) {
            result.crossed_entry = true;
            if (human_owned) {
                active_ = true;
                result.entered = true;
            }
            // Even a non-human crossing resets source POINTS next/previous;
            // crossed_entry lets the owning session perform that operation.
            point_ = 1;
        }
        zone_ = next_zone;
        return result;
    }
    zone_ = zone(point_, position);
    if (zone_ == 2) {
        point_ = std::uint8_t(point_ + 1);
        if (point_ == route_->points.size() - 4) {
            collision_enabled_ = false;
        }
        if (point_ == route_->points.size() - 2) {
            active_ = false;
            collision_enabled_ = true;
            point_ = 0;
            result.exited = true;
        }
    }
    return result;
}

void PitLane::reset() {
    zone_ = point_ = 0;
    active_ = false;
    collision_enabled_ = true;
}

bool PitLane::active() const {
    return active_;
}

std::uint32_t PitLane::point() const {
    return point_;
}

bool PitLane::collision_enabled() const {
    return collision_enabled_;
}

bool PitLane::exiting_hint() const {
    // 41ed00 switches from the pit message to its exit hint strictly after.
    return active_ && std::int32_t(point_) > std::int32_t(route_->points.size()) - 8;
}

std::optional<std::array<std::int32_t, 3>> PitLane::target() const {
    if (!active_) {
        return std::nullopt;
    }
    return route_->points.at(std::size_t(point_) + 1).best;
}
} // namespace toca::simulation
