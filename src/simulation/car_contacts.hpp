#pragma once

#include "scene/contact_effects.hpp"
#include "simulation/contact_candidates.hpp"
#include "simulation/motion.hpp"

namespace toca::simulation {
struct CarContact {
    std::uint32_t first = 0, second = 0;
    std::array<std::int32_t, 3> position{};
    std::int32_t strength_q16 = 0;
};

struct CarContactActor {
    Motion* motion;
    std::uint32_t car_id;
    bool active = true; //42bc70 excludes body mode4; stationary wrecks remain.
    bool full_body = true, local_player = false, visible = true;
};

enum class CarContactAudioAction {
    impact,
    scrape_start,
    scrape_update,
    scrape_stop,
    reset_pair
};

struct CarContactAudioEvent {
    CarContactAudioAction action = CarContactAudioAction::impact;
    std::uint32_t first = 0, second = 0;
    std::array<std::int32_t, 3> position{};
    std::uint32_t event_kind = 0;
    std::int32_t strength_q16 = 0;
    bool local_player = false;
};

struct CarContactEffects {
    std::vector<CarContactAudioEvent> audio;
    std::vector<scene::ImpactFragmentRequest> fragments;
    std::vector<scene::ScrapeSparkRequest> sparks;
};

struct CarContactOptions {
    std::uint32_t queued_fragments = 0, queued_sparks = 0;
    //428b50 tests whether the pair has an allocated handle, not whether its
    //sample is playing. The host owns allocation and pause/release behavior.
    std::function<bool(std::uint32_t, std::uint32_t)> scrape_handle;
    //421980 skips428820 for CMNOHITS; prop, barrier and ground passes continue.
    bool enabled = true;
};

//428820/428b50/42b570: source sector pair chronology, per-update successful
//neighbors, and persistent first-contact/continued-contact state.
class CarContacts {
    std::array<bool, 256> touching_{};
    CarContactEffects effects_;

public:
    std::vector<CarContact> resolve(std::span<const CarContactActor> actors, const ContactCandidateGroups& groups, const CarContactOptions& options = {});
    const CarContactEffects& effects() const;
    void reset();
    //Diagnostic import of256 source handle/touching pairs (8bytes each).
    void restore_source_touching(std::span<const std::uint8_t, 0x800> raw);
    //Retained source pair matrix for the following403dd0 traffic decision.
    //Audio handles remain externally owned; only touching words are physical.
    const std::array<bool, 256>& source_touching() const;
};
} // namespace toca::simulation
