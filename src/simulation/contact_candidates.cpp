#include "simulation/contact_candidates.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::simulation {
std::span<const ContactCandidateGroup> ContactCandidateGroups::ordered() const {
    return {groups.data(), count};
}

ContactCandidateGroups contact_candidate_groups(std::span<const ContactCandidateActor> actors) {
    if (actors.size() > 16) {
        throw std::runtime_error("Source contact sector groups require up to16 actor slots");
    }
    ContactCandidateGroups result;
    const auto append = [&](std::uint16_t sector, std::uint8_t actor) {
        auto groups = std::span(result.groups).first(result.count);
        const auto found = std::ranges::find(groups, sector, &ContactCandidateGroup::sector);
        auto* group = found == groups.end() ? &result.groups.at(result.count++) : &*found;
        group->sector = sector;
        if (group->count >= group->actors.size()) {
            throw std::runtime_error("Source contact sector actor list exceeds16 entries");
        }
        group->actors[group->count++] = actor;
    };
    for (std::uint8_t actor = 0; actor < actors.size(); ++actor) {
        const auto& input = actors[actor];
        if (!input.active) {
            continue;
        }
        append(input.registration.sector, actor);
        for (const auto sector : input.registration.neighbors) {
            if (sector == scene::no_visibility_sector) {
                break;
            }
            append(sector, actor);
        }
    }
    return result;
}
} // namespace toca::simulation
