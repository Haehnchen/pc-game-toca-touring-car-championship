#pragma once

#include "scene/pit_route.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace toca::simulation {
struct PitLaneTransition {
    bool crossed_entry = false;
    bool entered = false;
    bool exited = false;
};

// 4019f0/402910/403dd0: the local actor's original ordered PITS gates.
// The immutable route must outlive this consumer. Controls, POINTS checkpoint
// state and audio ownership remain with the host's existing actor/session.
class PitLane {
    const scene::PitRoute* route_;
    std::uint32_t track_;
    std::int32_t entry_distance_squared_ = 0;
    std::uint8_t zone_ = 0, point_ = 0;
    bool active_ = false, collision_enabled_ = true;
    std::uint8_t zone(std::uint32_t point, const std::array<std::int32_t, 3>& position) const;

public:
    PitLane(const scene::PitRoute& route, std::uint32_t track);
    //402910 selector2: normalized side test for an initialized PITS line.
    static std::uint8_t source_zone(const scene::PitRoutePoint& line, const std::array<std::int32_t, 3>& position_q16);
    PitLaneTransition update(const std::array<std::int32_t, 3>& position_q16, bool human_owned);
    void reset();
    bool active() const;
    std::uint32_t point() const;
    bool collision_enabled() const;
    bool exiting_hint() const;
    std::optional<std::array<std::int32_t, 3>> target() const;
};
} // namespace toca::simulation
