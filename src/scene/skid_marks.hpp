#pragma once

#include "scene/surface_effects.hpp"

#include <functional>
#include <optional>

namespace toca::scene {
using SkidMarkVector = SurfaceEffectVector;
// 47e370 sector lookup at the first vertex;0xffff is outside the authored grid.
using SkidSectorQuery = std::function<std::uint16_t(const SkidMarkVector&)>;

struct SkidMarkQuad {
    std::array<SkidMarkVector, 4> vertices{};
    std::uint32_t color = 0;
    std::uint16_t sector = 0xffff;
    std::uint8_t visibility = 0;
};

struct SkidMarkFrame {
    // 441d10 physical ring slots0..count-1, then the current segment queue.
    // Triangles use source indices0,2,1 /0,3,2, with no bitmap texture.
    std::vector<SkidMarkQuad> quads;
    std::size_t persistent = 0, active = 0;
    std::uint64_t updates = 0, emitted = 0;
};

struct SkidMarkRecord {
    SkidMarkQuad quad;
    std::uint8_t delay = 0;
    bool fading = false;
};

struct SkidMarkPersistentState {
    std::array<SkidMarkRecord, 100> records;
    std::size_t cursor = 0;
};

// 44b690 visual counterpart of the existing44c540/424d50 tire audio.
// No random stream: geometry follows actual contact/slip and traveled distance.
class SkidMarks {
    struct Wheel {
        SkidMarkQuad quad;
        SkidMarkVector origin{}, current{}, direction{}, normal{}, side{};
        std::uint8_t surface = 0;
        bool slipping = false;
    };

    SkidSectorQuery sectors_;
    std::array<std::array<Wheel, 4>, 16> wheels_{};
    std::array<SkidMarkRecord, 100> stored_{};
    std::vector<SkidMarkQuad> active_;

    struct ActiveAnchor {
        std::uint8_t actor;
        SkidMarkVector normal;
    };

    std::vector<ActiveAnchor> active_anchors_;
    std::array<std::uint32_t, 4> colors_{};
    std::size_t count_ = 0, cursor_ = 0;
    bool rain_ = false;
    std::uint64_t updates_ = 0, emitted_ = 0;
    mutable SkidMarkFrame frame_;
    SkidMarkQuad descriptor(const Wheel&, std::uint8_t visibility) const;
    void store(const Wheel&, std::uint8_t visibility);
    void start(Wheel&, const SurfaceEffectVehicle&, const SurfaceEffectWheel&, const SkidMarkVector& direction);
    void wheel(Wheel&, const SurfaceEffectVehicle&, const SurfaceEffectWheel&);
    void build_frame(SkidMarkFrame& result) const;

public:
    SkidMarks(const SceneSetup& setup, SkidSectorQuery sectors, std::optional<SkidMarkPersistentState> previous = std::nullopt);
    // 420420→446b50's view callback, scheduled natively once per active25Hz
    // tick after car physics. Rendering never changes queues or alpha.
    // A zero visibility mask closes existing slip through44c430 and prevents
    // stale distant-body wheel heights from starting or extending marks.
    void update(std::span<const SurfaceEffectVehicle> vehicles);
    const SkidMarkFrame& frame() const;
    SkidMarkFrame presentation_frame(std::span<const EffectPose> poses) const;
    void presentation_frame_into(SkidMarkFrame& result, std::span<const EffectPose> poses) const;
    // 469a0→44c3f0: clears counts and wheel slip state; retains ring cursor.
    void reset();
    std::size_t cursor() const;
    SkidMarkPersistentState persistent_state() const;
};
} // namespace toca::scene
