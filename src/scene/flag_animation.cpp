#include "scene/flag_animation.hpp"

#include <stdexcept>

namespace toca::scene {
FlagAnimation::FlagAnimation(const PropResources& flags, SourceRandom& random) : variants_(flags.model.nodes.size()) {
    for (const auto& flag : flags.actors) {
        const auto node = flag.animation_node;
        if (node != UINT32_MAX && (node >= flags.model.nodes.size() || flags.model.nodes[node].variants.size() < 3)) {
            throw std::runtime_error("Flag animation requires the original three mesh variants");
        }
        // 45b880 always draws44cb80(3)-1, even when80ce0 finds no ID1 node.
        const auto frame = std::uint8_t(random.next() % 3);
        actors_.push_back({node, frame});
        if (node != UINT32_MAX) {
            variants_[node] = frame;
        }
    }
}

void FlagAnimation::update() {
    ++updates_;
    // 421aa0 updates actors last-to-first; 45b8c0 advances after3 countdown ticks.
    for (auto actor = actors_.rbegin(); actor != actors_.rend(); ++actor) {
        if (actor->countdown == 0) {
            actor->frame = std::uint8_t((actor->frame + 1) % 3);
            actor->countdown = 3;
            if (actor->node != UINT32_MAX) {
                variants_[actor->node] = actor->frame;
            }
        } else {
            --actor->countdown;
        }
    }
}

std::span<const std::uint8_t> FlagAnimation::variants() const {
    return variants_;
}

std::uint32_t FlagAnimation::actor_count() const {
    return std::uint32_t(actors_.size());
}

std::uint8_t FlagAnimation::first_frame() const {
    return actors_.empty() ? 0 : actors_.front().frame;
}

std::uint64_t FlagAnimation::updates() const {
    return updates_;
}
} // namespace toca::scene
