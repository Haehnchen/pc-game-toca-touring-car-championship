#pragma once

#include "scene/qualifying.hpp"
#include "scene/fleet_scene.hpp"
#include "simulation/car_contacts.hpp"
#include "simulation/motion.hpp"

namespace toca::simulation {
class PropContacts;
struct ColdGridInitResult;
struct QualifyingStartInitResult;

struct RaceGrid {
    std::array<std::uint32_t, 16> rank_to_actor{};
    std::uint32_t rank(std::uint32_t actor) const;
};

// 460170: qualifying classification survives into the race. NoQual runs the
// original generated qualifying table with local60min, then swaps local last.
RaceGrid make_race_grid(const scene::QualifyingData&, SourceRandom&, const scene::QualifyingResults* qualifying = nullptr, scene::QualifyingResults* classification = nullptr,
    const std::array<std::uint8_t, 16>* driver_bindings = nullptr);

//401370 initializes every race driver's route/traffic/control caches after
//classification. The caller supplies canonical height-initialized POINTS;
//construction and this writer do not consume the shared RNG.
AiDriver make_race_grid_driver(std::span<const std::uint8_t> height_initialized_route, const scene::QualifyingData&, std::uint32_t actor, SourceRandom&, bool impossible = false);

// Actual nonplayer actors. The host keeps the local Motion and all peractor
// Session route/lap records. All borrowed collision resources and RNG outlive it.
class Rivals {
    std::array<std::unique_ptr<Motion>, 16> actors_;
    scene::QualifyingData data_;
    RaceGrid grid_;
    std::array<std::uint32_t, 16> order_;
    std::array<std::int32_t, 16> route_distances_{};
    std::int32_t route_length_q16_ = 0;
    std::uint32_t release_counter_ = 0;

    std::array<BodyMode, 16> modes_{};
    std::array<bool, 16> finished_{};
    bool held_ = true, qualifying_ = false;
    std::vector<CarContact> contacts_;
    CarContacts contact_pass_;
    scene::FleetSceneContext scene_context_;
    CarContactOptions contact_options_;
    std::vector<std::uint32_t> integration_order_;
    std::vector<scene::ImpactFragmentRequest> fragments_;
    std::vector<scene::ScrapeSparkRequest> sparks_;

public:
    Rivals(const HpcSurface&, const std::array<scene::GridSlot, 32>&, std::span<const std::uint8_t> route, const scene::QualifyingData&, std::uint32_t weather, SourceRandom&,
        const RaceGrid&, DamageContext&, SurfaceRoughness&, const scene::BarrierData* barriers = nullptr, bool low_gravity = false, bool impossible = false);
    //Use the generated ordinary two-call setup without another classification
    //or random seed. Actors initially use the grounded native preview;
    //prepare_first_race_step installs typed PRE poses at the actual deadline.
    //Raw snapshot complements are never imported by this constructor.
    Rivals(const HpcSurface&, const std::array<scene::GridSlot, 32>&, std::span<const std::uint8_t> route, const ColdGridInitResult&, std::uint32_t weather, SourceRandom&,
        DamageContext&, SurfaceRoughness&, const scene::BarrierData* barriers = nullptr, bool low_gravity = false);
    //443f60 retains only the local actor's four-way group. All sixteen records
    //remain allocated for retired traffic lookups; grounded POINTS previews
    //are replaced at the first deadline without classification or RNG draws.
    Rivals(const HpcSurface&, const std::array<scene::GridSlot, 32>&, std::span<const std::uint8_t> route, const QualifyingStartInitResult&, std::uint32_t weather, SourceRandom&,
        DamageContext&, SurfaceRoughness&, const scene::BarrierData* barriers = nullptr, bool low_gravity = false);
    //The host invokes this once at its first actual deadline, before any
    //session/selector/AI consumer. Rendering before that deadline stays grounded.
    void prepare_first_race_step(std::span<const scene::BodyPose, 16>, Motion& local);
    void prepare_first_qualifying_step(std::span<const scene::BodyPose, 16>, Motion& local);
    void restore_generated_grid_controls(const ColdGridInitResult&);
    // 415090 integrates distant actors ascending, then full bodies descending.
    // Shared car/prop contacts precede the ascending415240 ground pass.
    void update(const TimeStep&, Motion& local, const std::array<std::uint32_t, 16>& next_points, const std::array<bool, 16>& finished, bool start_hold,
        const ControlsInput& local_input = {}, PropContacts* props = nullptr);
    void set_release_counter(std::uint32_t counter);
    //4608b0 publishes all sixteen raw brake pedals descending, including
    //the Scene-owned local body and actors excluded from physical dispatch.
    void publish_frame_brake_pedals(Motion& local);
    void set_order(const std::array<std::uint32_t, 16>& rank_to_actor);
    void set_route_distances(const std::array<std::int32_t, 16>& distances_q16, std::int32_t route_length_q16);
    std::uint8_t traffic_mode(std::uint32_t actor) const;
    const MotionState& actor(std::uint32_t id) const;
    const VehicleState& controls(std::uint32_t id) const;
    scene::SurfaceEffectVehicle surface_effect_vehicle(std::uint32_t id) const;
    scene::EngineEffectVehicle engine_effect_vehicle(std::uint32_t id) const;
    std::uint32_t car_id(std::uint32_t actor) const;
    std::uint32_t local_actor() const;
    bool qualifying() const;
    bool distant(std::uint32_t actor) const;
    BodyMode body_mode(std::uint32_t actor) const;
    // Physical parent registration, sector visibility for wreck retirement,
    // model LOD, and the separate high-detail child mask used by428b50.
    void set_scene_context(const scene::FleetSceneContext&);
    void set_scrape_handle_query(std::function<bool(std::uint32_t, std::uint32_t)>);
    void set_car_contacts_enabled(bool enabled);
    const CarContactEffects& contact_effects() const;
    std::span<const std::uint32_t> integration_order() const;
    std::span<const scene::ImpactFragmentRequest> fragments() const;
    std::span<const scene::ScrapeSparkRequest> sparks() const;
    const Motion& motion(std::uint32_t actor) const;
    const RaceGrid& grid() const;
    std::array<std::array<std::int32_t, 3>, 16> positions(const scene::BodyPose& local) const;
    std::span<const CarContact> contacts() const;
};
} // namespace toca::simulation
