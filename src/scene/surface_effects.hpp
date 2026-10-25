#pragma once

#include "scene/prop_fragments.hpp"
#include "scene/surface_effect_resources.hpp"

#include <span>

namespace toca::scene {
using SurfaceEffectVector = std::array<std::int32_t, 3>;
using SurfaceEffectBasis = std::array<SurfaceEffectVector, 3>;

// Maps an actor's current world geometry onto its displayed pose. Detached
// particles keep their own history; only attached/newly emitted geometry uses it.
struct EffectPose {
    std::uint8_t actor = 0;
    std::array<double, 16> source_to_render{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool unchanged = true;
};

EffectPose effect_pose(std::uint8_t actor, const std::array<float, 16>& source, const std::array<float, 16>& rendered);
SurfaceEffectVector effect_position(const SurfaceEffectVector& position, std::uint8_t actor, std::span<const EffectPose> poses);

struct SurfaceEffectWheel {
    SurfaceEffectVector position{}, forward{}, side{};
    std::int32_t ground_height = 0, longitudinal_slip = 0, lateral_slip = 0;
    std::uint8_t surface = 0;
    // Saved vehicle+517 wheel flag; later body correction may move the point.
    bool airborne = false;
};

struct SurfaceEffectVehicle {
    SurfaceEffectVector position{}, velocity{}, support_normal{0, 65536, 0};
    SurfaceEffectBasis basis{{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    std::array<SurfaceEffectWheel, 4> wheels;
    // 40a770 vehicle+38: lookup length of its three half-dimensions.
    std::int32_t body_radius = 0, front_drive = 65536, gear_ratio = 65536;
    std::uint8_t actor = 0, visibility = 1;
    bool airborne = false, barrier_recovery = false;
    // Saved controls+2c0 orientation gate from40b360, before body integration.
    // This is independent of the final pose and of the ground-contact count.
    bool tyre_gate = true;
    // 4347d0: AI car or source follow-camera mode1 shows the second plume.
    bool secondary_plume = false;
};

struct SurfaceEffectSprite {
    SurfaceEffectVector center{}, minimum{}, maximum{};
    // Original descriptor bytes +30/+31. Flags2: alpha/grey;
    // flags6: screen-blended grey/unused.
    std::uint8_t shade0 = 255, shade1 = 255;
};

struct SurfaceEffectBatch {
    SurfaceEffectTexture texture = SurfaceEffectTexture::smoke;
    std::uint8_t flags = 2;
    std::vector<SurfaceEffectSprite> sprites;
};

struct SurfaceEffectFrame {
    // Original446f20 submission order: spray, mud, gravel, grass, smoke, dust.
    std::array<SurfaceEffectBatch, 6> batches;
    std::uint64_t updates = 0, emitted = 0;
    std::size_t particles = 0, plumes = 0;
    std::uint32_t kind_mask = 0;
};

// 431820/431b20's shared80-particle pool and432210's per-car emitters.
// SourceRandom setup belongs to make_prop_fragment_tables; runtime uses only
// that same cyclic table, shared with fragments and weather.
class SurfaceEffects {
    struct Particle {
        SurfaceEffectVector position{}, velocity{};
        SurfaceEffectVector previous_position{};
        std::int32_t radius = 0, growth = 0, initial_y = 0;
        std::int32_t previous_radius = 0;
        std::uint8_t alpha = 255, decay = 0, remaining = 0, actor = 0, wheel = 0, visibility = 0;
        std::uint8_t previous_alpha = 255;
        bool has_previous = false;
    };

    struct Emitter {
        SurfaceEffectVehicle vehicle;
        SurfaceEffectVector center{}, second{}, offset{}, trail_step{};
        std::array<bool, 4> wheels{};
        std::int32_t radius = 0, second_radius = 0, opacity = 0, second_opacity = 0;
        std::int32_t trail_opacity = 0, trail_decay = 0, trail_growth = 0;
        bool active = false, trail = false;
    };

    PropFragmentTables& tables_;
    std::array<Particle, 80> particles_{};
    std::array<Emitter, 16> emitters_{};
    std::array<std::vector<std::uint8_t>, 5> groups_;
    std::vector<std::uint8_t> free_;
    bool wet_;
    std::int32_t gravity_;
    std::uint8_t shade_;
    std::uint64_t updates_ = 0, emitted_ = 0;
    mutable SurfaceEffectFrame frame_;
    void next();
    SurfaceEffectVector slip(const SurfaceEffectWheel& wheel) const;
    Particle& allocate(std::size_t group);
    void debris(const SurfaceEffectVehicle&, std::size_t wheel, std::size_t group);
    void cloud(const SurfaceEffectVehicle&, std::size_t wheel, bool dust);
    void road_spray(Emitter&, std::size_t wheel, const SurfaceEffectVector& projected, std::int32_t speed, bool& suppressed);
    void plume(Emitter&, const SurfaceEffectVector& direction, std::int32_t speed);
    void build_frame(SurfaceEffectFrame& result, float alpha, std::span<const EffectPose> poses, std::uint8_t view_mask) const;

public:
    SurfaceEffects(PropFragmentTables& tables, const SceneSetup& setup, bool low_gravity = false);
    // 446a80: before fragments.update() and Weather::advance(). Vehicles are
    // the current car states used to constrain existing smoke to their wheels.
    void advance(std::span<const SurfaceEffectVehicle> vehicles);
    // 432210: after the wind update, in source car order. Invisible cars clear
    // their vehicle-bound plumes; previously emitted particles retain masks.
    void emit(std::span<const SurfaceEffectVehicle> vehicles);
    // 432090/4347d0: pure descriptors for the selected view bit (local=1).
    const SurfaceEffectFrame& frame(std::uint8_t view_mask = 1) const;
    SurfaceEffectFrame presentation_frame(float alpha, std::span<const EffectPose> poses = {}, std::uint8_t view_mask = 1) const;
    void presentation_frame_into(SurfaceEffectFrame& result, float alpha, std::span<const EffectPose> poses = {}, std::uint8_t view_mask = 1) const;
    // 431ab0 grid reset: clears pools/emitters, preserves shared table cursor.
    void reset();
};
} // namespace toca::scene
