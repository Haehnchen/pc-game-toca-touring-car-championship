#pragma once

#include "scene/body.hpp"
#include "scene/contact_effects.hpp"

namespace toca::scene {
struct GroundScrapeInput {
    BodyPose body;
    std::array<std::int32_t, 3> velocity{};
    std::array<bool, 8> active{};
    std::array<std::uint8_t, 8> surface_ids{};
    std::uint32_t car_id = 0, actor_state = 0;
    // dynamics+2c0 is the bit prepared before integration, not a new
    // post-ground orientation classification. Contacts/body are post-ground.
    bool tyre_contact = true, rain = false, snow = false, visible = true;
    std::uint32_t queued_sparks = 0;
};

struct GroundScrapeRequests {
    std::array<ScrapeSparkRequest, 8> requests{};
    std::size_t count = 0;
};

// 432210's dry body-contact branch, preserving contact order and the shared
// ten-request queue limit. It does not emit wheel dust or alter damage state.
GroundScrapeRequests ground_scrape(const GroundScrapeInput& input);
} // namespace toca::scene
