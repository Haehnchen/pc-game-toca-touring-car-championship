#pragma once

#include "simulation/fixed.hpp"

#include <span>
#include <utility>

namespace toca::simulation {
struct SourceRaceOrderActor {
    bool active = true, finished = false, start_pending = false;
    std::int16_t lap = 1;
    std::uint32_t rank = 1, spatial_rank = 0, ahead = 0, behind = 0;
    std::int32_t distance_q16 = 0, distance_delta_q16 = 0;
};

//402e90/402e10 mutate the neighboring spatial ring once per ascending actor.
//Return the old ahead identity:403dd0 retains its pointer for this actor's gap.
inline std::uint32_t advance_source_race_order(std::span<SourceRaceOrderActor> actors, std::uint32_t actor, std::int32_t rank_threshold_q16) {
    auto& self = actors[actor];
    const auto other_id = self.ahead;
    if (other_id >= actors.size() || self.behind >= actors.size()) {
        throw std::runtime_error("Source race ring exceeds active actor allocation");
    }
    auto& other = actors[other_id];
    if (!self.active || self.spatial_rank == 0) {
        return other_id;
    }
    const bool ordinary = !self.finished && !other.finished;
    const bool same_lap = self.lap == other.lap, same_start = self.start_pending == other.start_pending;
    const bool safe = self.distance_delta_q16 < rank_threshold_q16;
    if (other.distance_q16 < self.distance_q16) {
        if (other.ahead != actor) {
            if (other.ahead >= actors.size()) {
                throw std::runtime_error("Source race ring predecessor exceeds actor allocation");
            }
            actors[other.ahead].behind = actor;
            actors[self.behind].ahead = other_id;
            self.ahead = other.ahead;
            other.ahead = actor;
            other.behind = self.behind;
            self.behind = other_id;
        }
        --self.spatial_rank;
        ++other.spatial_rank;
        if (ordinary && other.rank < self.rank && same_lap && same_start && safe) {
            std::swap(self.rank, other.rank);
        }
    } else if (self.rank < other.rank) {
        if (ordinary && same_lap && same_start && safe) {
            std::swap(self.rank, other.rank);
        }
    } else if (other.rank < self.rank && ordinary && (other.lap < self.lap || (same_lap && !self.start_pending && other.start_pending))) {
        std::swap(self.rank, other.rank);
    }
    return other_id;
}

inline std::int32_t source_route_distance(std::int32_t cumulative_q16, std::int32_t projection_q16, std::int32_t length_q16) {
    auto distance = fixed::sub(cumulative_q16, projection_q16);
    if (distance < 0) {
        distance = fixed::add(distance, length_q16);
    }
    return distance > length_q16 ? length_q16 : distance;
}

inline std::int32_t source_forward_gap(
    std::int32_t self_q16, std::int32_t ahead_q16, std::int32_t length_q16, const fixed::Vector& self, const fixed::Vector& ahead, bool ahead_player, bool same_actor = false) {
    auto gap = fixed::sub(ahead_q16, self_q16);
    if (gap < 0) {
        gap = fixed::add(gap, length_q16);
    }
    if (gap < 100 * fixed::one && ahead_player) {
        auto delta = fixed::sub(self, ahead);
        delta[1] = 0;
        gap = fixed::length(delta);
    }
    return same_actor ? length_q16 : gap;
}
} // namespace toca::simulation
