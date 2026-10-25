#pragma once

#include "formats/terrain.hpp"

namespace toca::scene {
enum class PropKind {
    cones,
    posts,
    signs,
    flags
};

struct PropActor {
    std::uint32_t root_node = 0;
    // 45b880/480ce0 select the first ID1 actor, including following siblings.
    // Missing targets still consume the original flag initialization RNG draw.
    std::uint32_t animation_node = UINT32_MAX;
};

struct PropResources {
    Model model;
    Terrain baked;
    // Original bounded controller registration, distinct from loaded geometry.
    std::vector<PropActor> actors;
};

// 494a40 actor creation, followed by44d4b3/59e/668/72f registration.
// Owns all data; unresolved forward parents retain and overwrite the previous
// actor as the source loader does, without repairing the original game bytes.
PropResources decode_props(std::span<const std::uint8_t> bytes, PropKind kind);

// Render-only GARCON origin correction for Lavaland's shared cone controller.
// Keep decoded actor transforms and physical support/contact state unchanged.
Model prepare_cone_model(Model model, std::uint32_t track_id);
} // namespace toca::scene
