#pragma once

#include "scene/qualifying.hpp"
#include "scene/fleet_scene.hpp"
#include "simulation/car_contacts.hpp"
#include "simulation/motion.hpp"

namespace toca::simulation {
class PropContacts;

struct LinkupDriver {
    std::uint32_t car = 0, sensitivity = 2;
    bool manual = false;
};

struct LinkupRoster {
    std::vector<LinkupDriver> drivers;
    std::vector<std::uint8_t> grid;
    std::uint32_t humans = 0, local_actor = 0;
    bool hosting = false;
};

//463c52 randomizes human ranks with rejection sampling, including its first
//unused CRT draw. AI ranks retain their initialized trailing actor order.
std::vector<std::uint8_t> make_linkup_grid(std::uint32_t humans, std::uint32_t actors, SourceRandom&);

struct LinkupActorEvent {
    enum class Kind {
        gear,
        damage
    } kind = Kind::gear;
    std::uint8_t actor = 0, gear = 0;
    bool reverse = false;
    std::array<std::uint8_t, 17> variants{};
};

// Shared actor IDs use the host's roster order, independently of this process's
// local input slot. Local Motion is borrowed; all other actors are owned here.
// Every peer simulates human physics; only host AI or an actor's human owner
// publishes corrections and discrete events. No transport or SDL is used here.
class LinkupFleet {
    LinkupRoster roster_;
    std::array<std::unique_ptr<Motion>, 8> actors_;
    std::array<bool, 8> active_humans_{};
    std::array<BodyMode, 16> modes_{};
    std::array<RemoteControls, 8> inputs_{};
    std::array<std::array<std::uint8_t, 17>, 8> damage_levels_{};
    std::array<bool, 8> finished_{};
    std::int32_t tire_base_ = 0;
    scene::FleetSceneContext scene_context_;
    std::array<std::uint32_t, 16> order_{};
    std::array<std::int32_t, 16> route_distances_{};
    std::int32_t route_length_ = 0;
    CarContacts contact_pass_;
    CarContactOptions contact_options_;
    std::vector<CarContact> contacts_;
    std::vector<LinkupActorEvent> events_;
    std::vector<std::uint32_t> integration_order_;
    std::vector<scene::ImpactFragmentRequest> fragments_;
    std::vector<scene::ScrapeSparkRequest> sparks_;
    bool car_boost_ = true;
    std::uint32_t race_leader_ = 0;
    std::array<CarBoostState, 8> boost_states_{};

    void require_actor(std::uint32_t actor) const;

public:
    LinkupFleet(const HpcSurface&, const std::array<scene::GridSlot, 32>&, std::span<const std::uint8_t> route, const scene::QualifyingData&, std::uint32_t weather, SourceRandom&,
        LinkupRoster, DamageContext&, SurfaceRoughness&, const scene::BarrierData* barriers = nullptr, bool car_boost = true);
    std::uint32_t count() const;
    std::uint32_t local_actor() const;
    std::uint32_t car_id(std::uint32_t actor) const;
    std::uint32_t physics_car_id(std::uint32_t actor) const;
    bool human(std::uint32_t actor) const;
    bool owns(std::uint32_t actor) const;
    Motion& motion(std::uint32_t actor, Motion& local);
    const Motion& motion(std::uint32_t actor, const Motion& local) const;
    BodyMode body_mode(std::uint32_t actor) const;
    void apply_input(std::uint32_t actor, const RemoteControls&, Motion& local);
    void apply_state(std::uint32_t actor, const RemoteMotionState&, Motion& local);
    void apply_gear(std::uint32_t actor, std::uint8_t gear, bool reverse, Motion& local);
    void apply_damage(std::uint32_t actor, const std::array<std::uint8_t, 17>& variants, Motion& local);
    void set_active_human(std::uint32_t actor, bool active, Motion& local);
    void update(const TimeStep&, Motion& local, const ControlsInput&, const std::array<std::uint32_t, 16>& next_points, const std::array<bool, 16>& finished, bool start_hold,
        PropContacts* props = nullptr);
    void set_order(const std::array<std::uint32_t, 16>&);
    //428440 uses race rank1, not the separate402e10 spatial traffic ring.
    void set_race_leader(std::uint32_t actor);
    void set_route_distances(const std::array<std::int32_t, 16>&, std::int32_t route_length);
    void set_scene_context(const scene::FleetSceneContext&);
    void set_scrape_handle_query(std::function<bool(std::uint32_t, std::uint32_t)>);
    const CarContactEffects& contact_effects() const;
    std::span<const CarContact> contacts() const;
    std::span<const LinkupActorEvent> events() const;
    std::span<const std::uint32_t> integration_order() const;
    std::span<const scene::ImpactFragmentRequest> fragments() const;
    std::span<const scene::ScrapeSparkRequest> sparks() const;
    std::array<std::array<std::int32_t, 3>, 16> positions(const scene::BodyPose& local) const;
};
} // namespace toca::simulation
