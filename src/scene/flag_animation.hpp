#pragma once

#include "core/random.hpp"
#include "scene/props.hpp"

namespace toca::scene {
// 45b880/45b8c0: CPU-owned flag mesh selection, advanced only by source ticks.
class FlagAnimation {
    struct Actor {
        std::uint32_t node;
        std::uint8_t frame, countdown = 3;
    };

    std::vector<Actor> actors_;
    std::vector<std::uint8_t> variants_;
    std::uint64_t updates_ = 0;

public:
    FlagAnimation(const PropResources& flags, SourceRandom& random);
    void update();
    std::span<const std::uint8_t> variants() const;
    std::uint32_t actor_count() const;
    std::uint8_t first_frame() const;
    std::uint64_t updates() const;
};
} // namespace toca::scene
