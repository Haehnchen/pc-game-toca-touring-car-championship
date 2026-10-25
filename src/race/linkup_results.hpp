#pragma once

#include "race/race_progress.hpp"

#include <array>
#include <stdexcept>

namespace toca::race {
//45ed30 state13 scans classification order but draws only identities for which
//426e80 is active. AI(-1/ff) and departed humans are skipped, even if finished.
//The first displayed human supplies the gap baseline; no AI time is invented.
inline RaceResults linkup_results(const RaceResults& classification, const std::array<bool, 8>& active_humans) {
    if (classification.count == 0 || classification.count > active_humans.size() || classification.local_actor >= active_humans.size()
        || !active_humans[classification.local_actor]) {
        throw std::invalid_argument("Linkup results require1..8 actors and an active local human");
    }
    std::array<bool, 8> seen{};
    RaceResults result{};
    result.local_actor = classification.local_actor;
    result.count = 0;
    for (std::uint32_t rank = 0; rank < classification.count; ++rank) {
        const auto& row = classification.rows[rank];
        if (row.actor_id >= seen.size() || seen[row.actor_id]) {
            throw std::invalid_argument("Linkup result actor IDs must be unique and bounded by eight");
        }
        seen[row.actor_id] = true;
        if (active_humans[row.actor_id]) {
            if (row.actor_id == result.local_actor) {
                result.local_rank = result.count;
            }
            result.rows[result.count++] = row;
        }
    }
    for (std::size_t actor = 0; actor < active_humans.size(); ++actor) {
        if (active_humans[actor] && !seen[actor]) {
            throw std::invalid_argument("Active Linkup human is missing from classification");
        }
    }
    return result;
}
} // namespace toca::race
