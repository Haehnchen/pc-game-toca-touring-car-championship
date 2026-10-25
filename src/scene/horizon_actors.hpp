#pragma once

#include "formats/model.hpp"
#include "scene/setup.hpp"

#include <memory>
#include <optional>
#include <span>

namespace toca::scene {
using HorizonTransform = std::array<std::int32_t, 16>;

// Presentation-only Q16 affine pose, preserving original scale/handedness.
HorizonTransform interpolate_horizon_transform(const HorizonTransform& previous, const HorizonTransform& current, float alpha);

struct HorizonActorResources {
    SceneSetup setup;
    Model moving, rotorcraft;
    // Complete original BGR0 palette supplied by scene loading for HOR BMPs
    // whose 255-entry palette omits an index still used by their pixels.
    std::vector<std::uint8_t> texture_palette;
    std::vector<std::uint32_t> moving_roots;
    std::uint32_t rotorcraft_root = UINT32_MAX, rotorcraft_child = UINT32_MAX;
};

// 43cf60/43e2a0 consume HOR members0/1 only on their track/weather branches.
// Rotorcraft includes the source's extra identity parent from496f70(0).
HorizonActorResources decode_horizon_actors(std::span<const std::uint8_t> moving, std::span<const std::uint8_t> rotorcraft, const SceneSetup& setup);

struct HorizonModelFrame {
    std::vector<HorizonTransform> transforms;
    std::vector<std::uint8_t> visible;
};

// 43cf60/43e2a0 create camera0 on the first moving actor or rotorcraft
// parent. Its world pose is independent of the local player's car.
struct HorizonCamera {
    std::array<float, 16> world_to_view{};
    std::array<std::int32_t, 3> position_q16{};
    // 47da47/47dd90 walks camera parents and adds their registered sectors.
    // Use the actor root here for CameraInputs.registered_camera_position;
    // position_q16 is the offset camera's actual audio/weather listener.
    std::array<std::int32_t, 3> anchor_position_q16{};
    bool rotorcraft = false;
    std::array<float, 3> published_world_position{};
};

struct HorizonFrame {
    HorizonModelFrame moving, rotorcraft;
    std::optional<HorizonCamera> camera_zero;
    std::uint64_t updates = 0;
    std::uint32_t moving_count = 0;
    std::uint8_t rotorcraft_state = 0;
    std::int32_t rotorcraft_speed = 0;
    std::array<std::int32_t, 3> rotorcraft_position{};
};

// Explicit 446a80 ticks; construction/reset/update consume no source RNG.
// Reset belongs to scene loading, not the qualifying-to-grid handoff.
class HorizonActors {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit HorizonActors(const HorizonActorResources& resources);
    ~HorizonActors();
    HorizonActors(HorizonActors&&) noexcept;
    HorizonActors& operator=(HorizonActors&&) noexcept;
    void reset();
    void update();
    const HorizonFrame& frame() const;
    // Render-only poses between the last two ticks, including attached camera0.
    // Visibility and diagnostic state stay current; route restarts cut to start.
    // Alpha1 returns the authoritative frame for pauses and presentation cuts.
    HorizonFrame presentation_frame(float alpha) const;
};
} // namespace toca::scene
