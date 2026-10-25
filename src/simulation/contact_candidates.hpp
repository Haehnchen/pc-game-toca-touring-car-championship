#pragma once

#include "scene/visibility.hpp"

namespace toca::simulation {
struct ContactCandidateActor {
    scene::RootSectorRegistration registration;
    bool active = true; //42bc70 excludes body mode4, not stationary wrecks.
};

struct ContactCandidateGroup {
    std::uint16_t sector = scene::no_visibility_sector;
    std::array<std::uint8_t, 16> actors{};
    std::uint8_t count = 0;
};

struct ContactCandidateGroups {
    std::array<ContactCandidateGroup, 64> groups{};
    std::uint8_t count = 0;
    std::span<const ContactCandidateGroup> ordered() const;
};

//42bc70: actor-slot order, own sector then up to3 neighbors until firstffff;
//sector groups retain first encounter order. Do NOT globally deduplicate pairs:
//428820 suppresses only successful contacts, so misses can be tested again in
//a later group after another pair displaces a body. Rawffff own sectors remain
//group keys. All storage is bounded to the original16 actors/64 groups.
ContactCandidateGroups contact_candidate_groups(std::span<const ContactCandidateActor> actors);
} // namespace toca::simulation
