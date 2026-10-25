#pragma once

#include "formats/hpc.hpp"
#include "scene/body.hpp"
#include "scene/prop_frame.hpp"
#include "scene/props.hpp"
#include "simulation/fixed.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace toca::simulation {
struct PropVehicle {
    scene::BodyPose body;
    std::array<std::int32_t, 3> velocity_q16{};
    std::uint32_t car_id = 0;
    bool local_player = true;
};

struct PropResponse {
    // Source car+1f0 acceleration and car+1fc body-local moment. Motion adds
    // these at the next integration, before the source0.2/delta scaling.
    std::array<std::int32_t, 3> force_q16{}, torque_q16{};
    std::uint32_t contacts = 0;
    // Native haptic presentation of actual new local prop contacts,0..65536.
    // Independent of source audio flags/fragment capacity; never a body force.
    std::int32_t impact_strength_q16 = 0;
};

using PropSurfaceQuery = std::function<std::optional<HpcPropSample>(std::int32_t, std::int32_t, std::optional<std::uint16_t>)>;

// Typed controller state: resource geometry and mutable simulation values,
// without original pointers, padding or externally owned audio handles.
struct PropConeSnapshot {
    fixed::Basis axes{};
    fixed::Vector position{}, normal{}, velocity{}, angular{}, force{}, torque{};
    std::optional<std::uint16_t> triangle;
    std::uint8_t state = 0;
    bool grounded = false, settled = false, translating = false, rotating = false;
};

struct PropPostSnapshot {
    scene::HorizonTransform saved{};
    fixed::Basis axes{}, rotation{};
    fixed::Vector position{}, normal{}, turn_axis{};
    std::array<fixed::Vector, 2> endpoints{};
    std::int32_t degrees = 0, wait = 0;
    std::uint8_t state = 0;
    bool touched = false, broken = false;
};

struct PropSignSnapshot {
    scene::HorizonTransform transform{};
    std::array<fixed::Vector, 2> endpoints{};
    fixed::Vector normal{}, pivot{};
    std::int16_t increment = 0;
    std::uint8_t state = 0;
    bool visible = false;
};

struct PropContactSnapshot {
    std::vector<PropConeSnapshot> cones;
    std::vector<PropPostSnapshot> posts;
    std::vector<PropSignSnapshot> signs;
};

class PropContacts {
    struct State;
    std::unique_ptr<State> state_;

public:
    PropContacts(const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
        std::span<const std::array<std::int32_t, 2>> sector_centers, PropSurfaceQuery surface, bool low_gravity = false);
    PropContacts(const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
        std::span<const std::array<std::int32_t, 2>> sector_centers, const HpcSurface& surface, bool low_gravity = false);
    ~PropContacts();
    PropContacts(PropContacts&&) noexcept;
    PropContacts& operator=(PropContacts&&) noexcept;
    // 45c4c0: reset registered cones/posts/signs for a new grid; flags retain
    // their independent initialization/animation state.
    void reset();
    // 421aa0: called before flags and421980's contact dispatcher.
    void advance();
    // 421980 ->45b900: called after vehicle integration, before ground correction.
    PropResponse contact(const PropVehicle& vehicle);
    // One45b900 dispatcher: actor identity order, shared sector candidates and
    // one post-release decision after every registered body has been tested.
    // Full, distant and removed body modes do not filter this source pass.
    std::vector<PropResponse> contact(std::span<const PropVehicle> vehicles);
    // Diagnostic import at415090: original registered controller order and
    // exact0xcc/0x13c/0x40 records. Sign models contain64float transform bytes
    // followed by the root's four source bytes at+140. Pointer words are never
    // imported. Counts must match the canonical resources of this controller.
    void restore_source_state(
        std::span<const std::uint8_t> cones, std::span<const std::uint8_t> posts, std::span<const std::uint8_t> signs, std::span<const std::array<std::uint8_t, 68>> sign_models);
    const scene::PropFrame& frame() const;
    PropContactSnapshot snapshot() const;
    // Current discrete states/events, with render-only node poses between ticks.
    scene::PropFrame presentation_frame(float alpha) const;
};
} // namespace toca::simulation
