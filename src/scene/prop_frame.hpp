#pragma once

#include "scene/horizon_actors.hpp"

namespace toca::scene {
struct PropSoundEvent {
    std::array<std::int32_t, 3> position_q16{};
    std::uint32_t event_kind = 0;
    std::int32_t strength_q16 = 0;
    bool local_player = true;
    std::uint32_t ordinal = 0;
};

struct PropBreakEvent {
    std::uint32_t root_node = 0;
    HorizonTransform root_transform{};
    std::array<std::int32_t, 3> vehicle_velocity_q16{};
    std::uint32_t ordinal = 0;
};

struct PropFrame {
    HorizonModelFrame cones, posts, signs;
    std::vector<std::uint8_t> cone_states, post_states, sign_states;
    std::vector<PropSoundEvent> sounds;
    std::vector<PropBreakEvent> breaks;
    std::uint64_t updates = 0, contacts = 0;
    bool missing_surface = false;
};
} // namespace toca::scene
