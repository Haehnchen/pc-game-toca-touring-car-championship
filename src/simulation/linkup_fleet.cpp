#include "simulation/linkup_fleet.hpp"

#include "simulation/fixed.hpp"
#include "simulation/prop_contacts.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace toca::simulation {
namespace {
using namespace fixed;

void validate_roster(const LinkupRoster& roster) {
    if (roster.drivers.empty() || roster.drivers.size() > 8 || roster.humans == 0 || roster.humans > roster.drivers.size() || roster.local_actor >= roster.humans
        || roster.grid.size() != roster.drivers.size()) {
        throw std::runtime_error("Linkup fleet requires1..8 actors and an active local human");
    }
    std::array<bool, 8> seen{};
    for (const auto actor : roster.grid) {
        if (actor >= roster.drivers.size() || seen[actor]) {
            throw std::runtime_error("Linkup grid must be a shared actor permutation");
        }
        seen[actor] = true;
    }
    for (const auto& driver : roster.drivers) {
        if (driver.car > 9 || driver.sensitivity < 1 || driver.sensitivity > 3) {
            throw std::runtime_error("Linkup driver selection exceeds original ranges");
        }
    }
}
} // namespace

std::vector<std::uint8_t> make_linkup_grid(std::uint32_t humans, std::uint32_t actors, SourceRandom& random) {
    if (humans == 0 || humans > actors || actors > 8) {
        throw std::runtime_error("Linkup grid requires1..8 humans and bounded optional AI");
    }
    std::vector<std::uint8_t> grid(actors);
    for (std::uint32_t actor = 0; actor < actors; ++actor) {
        grid[actor] = std::uint8_t(actor);
    }
    std::array<bool, 8> used{};
    (void)random.next(); //463c52's unused initial rand()%humanCount.
    for (std::uint32_t actor = 0; actor < humans; ++actor) {
        std::uint32_t rank;
        do {
            rank = random.next() % humans;
        } while (used[rank]);
        used[rank] = true;
        grid[rank] = std::uint8_t(actor);
    }
    return grid;
}

LinkupFleet::LinkupFleet(const HpcSurface& surface, const std::array<scene::GridSlot, 32>& grid, std::span<const std::uint8_t> route, const scene::QualifyingData& data,
    std::uint32_t weather, SourceRandom& random, LinkupRoster roster, DamageContext& damage, SurfaceRoughness& roughness, const scene::BarrierData* barriers, bool car_boost)
    : roster_(std::move(roster)), car_boost_(car_boost) {
    validate_roster(roster_);
    race_leader_ = roster_.grid.front();
    tire_base_ = data.tire_offset_q16;
    modes_.fill(BodyMode::removed);
    for (auto& actor : scene_context_.actors) actor.contact_effect_mask = 1;
    for (std::uint32_t rank = 0; rank < count(); ++rank) {
        order_[rank] = roster_.grid[rank];
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        active_humans_[actor] = actor < roster_.humans;
        modes_[actor] = active_humans_[actor] ? BodyMode::full : BodyMode::distant;
        if (actor == local_actor()) {
            continue;
        }
        const auto& driver = roster_.drivers[actor];
        const auto physical_car = physics_car_id(actor);
        const auto rank = std::find(roster_.grid.begin(), roster_.grid.end(), actor) - roster_.grid.begin();
        const auto body = scene::initial_body_pose(surface, scene::race_grid_start(grid, std::uint32_t(rank)), physical_car);
        auto motion = std::make_unique<Motion>(surface, body, physical_car, driver.manual, weather, false, damage, roughness, barriers);
        motion->set_steering_profile(driver.sensitivity);
        motion->set_actor_identity(actor, active_humans_[actor]);
        motion->set_network_participant();
        motion->prepare_ai(AiDriver(route, data.drivers[actor].raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random));
        if (!active_humans_[actor]) {
            motion->handoff_to_ai(false);
            motion->set_rival(true);
        } else {
            inputs_[actor].source_flags = 1U << 20;
            motion->apply_network_controls(inputs_[actor]);
        }
        motion->set_start_hold(true);
        actors_[actor] = std::move(motion);
    }
}

void LinkupFleet::require_actor(std::uint32_t actor) const {
    if (actor >= count()) {
        throw std::runtime_error("Linkup actor is outside the active shared roster");
    }
}

std::uint32_t LinkupFleet::count() const {
    return std::uint32_t(roster_.drivers.size());
}

std::uint32_t LinkupFleet::local_actor() const {
    return roster_.local_actor;
}

std::uint32_t LinkupFleet::car_id(std::uint32_t actor) const {
    require_actor(actor);
    return actor < roster_.humans ? roster_.drivers[actor].car : actor;
}

std::uint32_t LinkupFleet::physics_car_id(std::uint32_t actor) const {
    require_actor(actor);
    //44db72 reads the first inactive roster entry for every AI. Its fresh BSS
    //value is0; do not reproduce stale selections from a prior larger lobby.
    return actor < roster_.humans ? roster_.drivers[actor].car : 0;
}

bool LinkupFleet::human(std::uint32_t actor) const {
    require_actor(actor);
    return active_humans_[actor];
}

bool LinkupFleet::owns(std::uint32_t actor) const {
    require_actor(actor);
    return active_humans_[actor] ? actor == local_actor() : roster_.hosting;
}

Motion& LinkupFleet::motion(std::uint32_t actor, Motion& local) {
    require_actor(actor);
    return actor == local_actor() ? local : *actors_[actor];
}

const Motion& LinkupFleet::motion(std::uint32_t actor, const Motion& local) const {
    require_actor(actor);
    return actor == local_actor() ? local : *actors_[actor];
}

BodyMode LinkupFleet::body_mode(std::uint32_t actor) const {
    require_actor(actor);
    return modes_[actor];
}

void LinkupFleet::apply_input(std::uint32_t actor, const RemoteControls& input, Motion& local) {
    require_actor(actor);
    if (owns(actor) || !human(actor)) {
        throw std::runtime_error("Remote input cannot replace this process's owned actor or an AI");
    }
    validate_remote_controls(input);
    inputs_[actor] = input;
    motion(actor, local).apply_network_controls(input);
}

void LinkupFleet::apply_state(std::uint32_t actor, const RemoteMotionState& state, Motion& local) {
    require_actor(actor);
    if (owns(actor)) {
        throw std::runtime_error("Remote correction cannot replace this process's authoritative actor");
    }
    motion(actor, local).apply_network_state(state);
    inputs_[actor] = state.controls;
}

void LinkupFleet::apply_gear(std::uint32_t actor, std::uint8_t gear, bool reverse, Motion& local) {
    require_actor(actor);
    if (owns(actor)) {
        throw std::runtime_error("Remote gear cannot replace this process's authoritative actor");
    }
    motion(actor, local).apply_network_gear(gear, reverse);
}

void LinkupFleet::apply_damage(std::uint32_t actor, const std::array<std::uint8_t, 17>& variants, Motion& local) {
    require_actor(actor);
    if (owns(actor)) {
        throw std::runtime_error("Remote visual damage cannot replace this process's authoritative actor");
    }
    motion(actor, local).apply_network_damage(variants);
}

void LinkupFleet::set_active_human(std::uint32_t actor, bool active, Motion& local) {
    require_actor(actor);
    if (active || actor == local_actor()) {
        throw std::runtime_error("Linkup takeover only removes a nonlocal active human; reconnect is unavailable");
    }
    if (!active_humans_[actor]) {
        return;
    }
    active_humans_[actor] = false;
    //402be0 changes control ownership without clearing retained finish state.
    motion(actor, local).handoff_to_ai(finished_[actor]);
    motion(actor, local).set_rival(modes_[actor] == BodyMode::distant);
}

void LinkupFleet::update(const TimeStep& time, Motion& local, const ControlsInput& input, const std::array<std::uint32_t, 16>& next, const std::array<bool, 16>& finished,
    bool held, PropContacts* props) {
    contacts_.clear();
    events_.clear();
    integration_order_.clear();
    fragments_.clear();
    sparks_.clear();
    local.set_network_participant();
    local.set_steering_profile(roster_.drivers[local_actor()].sensitivity);
    std::array<AiTraffic, 16> traffic{};
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        auto& body = motion(actor, local);
        body.set_actor_identity(actor, actor < roster_.humans);
        body.set_model_detail(scene_context_.actors[actor].model_detail);
        body.set_damage_registration(scene_context_.actors[actor].physical.sector);
        body.begin_tick();
        if (finished[actor] && !finished_[actor]) {
            body.handoff_to_ai(true);
            finished_[actor] = true;
        }
        traffic[actor] = body.ai_traffic_state();
        traffic[actor].actor_id = actor;
        traffic[actor].player = actor < roster_.humans;
        traffic[actor].next_point = next[actor];
        traffic[actor].route_distance_q16 = route_distances_[actor];
    }
    for (std::uint32_t rank = 0; rank < count(); ++rank) {
        const auto actor = order_[rank], ahead = order_[(rank + count() - 1) % count()], behind = order_[(rank + 1) % count()];
        auto& record = traffic[actor];
        record.ahead_id = ahead;
        record.behind_id = behind;
        record.forward_gap_q16 = sub(route_distances_[ahead], route_distances_[actor]);
        if (record.forward_gap_q16 < 0 && route_length_ > 0) {
            record.forward_gap_q16 = add(record.forward_gap_q16, route_length_);
        }
        if (route_length_ == 0 || (record.forward_gap_q16 < 100 * one && active_humans_[ahead])) {
            auto delta = sub(traffic[ahead].body.position, record.body.position);
            delta[1] = 0;
            record.forward_gap_q16 = length(delta);
        }
    }
    std::array<BodySelectionActor, 16> selection{};
    for (auto& entry : selection) {
        entry.previous = BodyMode::removed;
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        const auto& body = motion(actor, local);
        const auto& state = body.state();
        selection[actor] = {modes_[actor], state.rollover, traffic[actor].human, actor < roster_.humans ? 0 : -1, state.contact_count, body.controls().tire_offsets_q16[0],
            body.retained_ground_up_q16(), traffic[actor].ahead_id, traffic[actor].behind_id, traffic[actor].forward_gap_q16};
    }
    modes_ = select_fleet_body_modes(selection, {.local_actor = local_actor(), .tire_base_q16 = tire_base_}).modes;
    std::array<AiTraffic, 8> traffic_storage;
    const auto active_traffic = std::span(traffic_storage).first(count());
    for (std::uint32_t rank = 0; rank < count(); ++rank) {
        active_traffic[rank] = traffic[order_[rank]];
    }
    const auto update_traffic = [&](std::uint32_t actor) {
        const auto entry = std::find_if(active_traffic.begin(), active_traffic.end(), [actor](const auto& value) {
            return value.actor_id == actor;
        });
        *entry = traffic[actor];
    };
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        if (!motion(actor, local).ai_controlled() || modes_[actor] == BodyMode::removed) {
            continue;
        }
        auto& body = motion(actor, local);
        body.set_rival(modes_[actor] == BodyMode::distant);
        body.set_ai_route_point(next[actor]);
        body.observe_rival_ai();
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        if (motion(actor, local).ai_controlled() && modes_[actor] != BodyMode::removed) {
            auto& body = motion(actor, local);
            body.decide_rival_traffic(traffic[actor], active_traffic, route_length_);
            const auto decision = body.ai_traffic_state();
            auto& record = traffic[actor];
            record.blocked = decision.blocked;
            record.passing = decision.passing;
            record.blocker_id = decision.blocker_id;
            record.mode = decision.mode;
            update_traffic(actor);
        }
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        if (motion(actor, local).ai_controlled() && modes_[actor] != BodyMode::removed) {
            auto& body = motion(actor, local);
            body.decide_rival_traffic_brake(traffic[actor], active_traffic);
            const auto decision = body.ai_traffic_state();
            auto& record = traffic[actor];
            record.blocked = decision.blocked;
            record.passing = decision.passing;
            record.blocker_id = decision.blocker_id;
            record.mode = decision.mode;
            update_traffic(actor);
        }
    }
    std::array<std::uint32_t, 8> full;
    std::size_t full_count = 0;
    bool dispatch = true;
    if (car_boost_ && !held) {
        //415090 prepass visits all original human roster slots, including
        //inactive humans handed to AI, but never the trailing pure AI actors.
        const auto leader_position = motion(race_leader_, local).state().body.position;
        for (std::uint32_t actor = 0; actor < roster_.humans; ++actor) {
            auto& body = motion(actor, local);
            boost_states_[actor].maximum_throttle_q16 = body.controls().maximum_throttle_q16;
            const auto parameters = car_boost_step(boost_states_[actor], body.controls().base_throttle_q16, body.state().body.position, leader_position, actor == race_leader_);
            boost_states_[actor] = parameters.state;
            body.apply_car_boost(parameters);
        }
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        auto& body = motion(actor, local);
        body.set_start_hold(held);
        if (body.ai_controlled()) {
            //401260's only caller passes0 and clears every reaction byte.
            body.set_ai_start_wait(held, false);
        }
        if (modes_[actor] == BodyMode::distant) {
            body.update_distant(time, true);
            integration_order_.push_back(actor);
        } else if (modes_[actor] == BodyMode::full) {
            if (!body.prepare_body_update(actor < roster_.humans)) {
                dispatch = false;
                break;
            }
            full[full_count++] = actor;
        }
    }
    if (dispatch) {
        const auto full_bodies = std::span(full).first(full_count);
        for (auto it = full_bodies.rbegin(); it != full_bodies.rend(); ++it) {
            motion(*it, local).update(*it == local_actor() ? input : ControlsInput{}, time, true);
            integration_order_.push_back(*it);
        }
    }
    if (props) {
        props->advance();
    }
    for (const auto actor : integration_order_) {
        const auto& effect = motion(actor, local).state().barrier_effects;
        const auto palette = std::uint8_t(physics_car_id(actor));
        if (effect.fragment_strength_q16 > 0 && fragments_.size() < 10) {
            fragments_.push_back({effect.position_q16, {}, effect.fragment_strength_q16, {palette, palette}});
        }
        if (effect.spark_strength_q16 > 0 && sparks_.size() < 10) {
            sparks_.push_back({effect.position_q16, effect.spark_direction_q16, effect.tangent_velocity_q16, effect.spark_strength_q16});
        }
    }
    contact_options_.queued_fragments = std::uint32_t(fragments_.size());
    contact_options_.queued_sparks = std::uint32_t(sparks_.size());
    std::array<CarContactActor, 8> actors;
    std::array<ContactCandidateActor, 8> candidates;
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        actors[actor] = {
            &motion(actor, local), physics_car_id(actor), modes_[actor] != BodyMode::removed, modes_[actor] == BodyMode::full, actor == local_actor(), (scene_context_.actors[actor].contact_effect_mask != 0)};
        candidates[actor] = {scene_context_.actors[actor].physical, modes_[actor] != BodyMode::removed};
    }
    contacts_ = contact_pass_.resolve(std::span(actors).first(count()), contact_candidate_groups(std::span(candidates).first(count())), contact_options_);
    const auto& effects = contact_pass_.effects();
    fragments_.insert(fragments_.end(), effects.fragments.begin(), effects.fragments.end());
    sparks_.insert(sparks_.end(), effects.sparks.begin(), effects.sparks.end());
    for (const auto& contact : contacts_) {
        for (const auto actor : {contact.first, contact.second}) {
            if (modes_[actor] == BodyMode::crashed) {
                modes_[actor] = BodyMode::crashing;
            }
        }
    }
    if (props) {
        std::array<PropVehicle, 8> vehicles;
        for (std::uint32_t actor = 0; actor < count(); ++actor) {
            const auto& state = motion(actor, local).state();
            vehicles[actor] = {state.body, state.velocity_q16, physics_car_id(actor), actor == local_actor()};
        }
        const auto responses = props->contact(std::span(vehicles).first(count()));
        for (std::uint32_t actor = 0; actor < count(); ++actor) {
            motion(actor, local).apply_prop_response(responses[actor]);
        }
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        auto& body = motion(actor, local);
        const auto mode = modes_[actor];
        body.finish_ground(mode);
        if (mode == BodyMode::crashing && body.state().boundary == MotionBoundary::none) {
            modes_[actor] = BodyMode::crashed;
        } else if (mode == BodyMode::crashed && !scene_context_.actors[actor].sector_visible) {
            modes_[actor] = BodyMode::removed;
        }
        if (!owns(actor) || actor != local_actor()) {
            continue;
        }
        if (body.controls().gear_committed) {
            events_.push_back({LinkupActorEvent::Kind::gear, std::uint8_t(actor), std::uint8_t(body.controls().gear), body.controls().reverse_pedal, {}});
        }
        if (body.damage().levels != damage_levels_[actor]) {
            damage_levels_[actor] = body.damage().levels;
            events_.push_back({LinkupActorEvent::Kind::damage, std::uint8_t(actor), 0, false, damage_levels_[actor]});
        }
    }
}

void LinkupFleet::set_order(const std::array<std::uint32_t, 16>& order) {
    std::array<bool, 8> seen{};
    for (std::uint32_t rank = 0; rank < count(); ++rank) {
        const auto actor = order[rank];
        if (actor >= count() || seen[actor]) {
            throw std::runtime_error("Linkup spatial ring must contain each actor once");
        }
        seen[actor] = true;
    }
    order_ = order;
}

void LinkupFleet::set_route_distances(const std::array<std::int32_t, 16>& distances, std::int32_t length) {
    if (length <= 0) {
        throw std::runtime_error("Linkup route length must be positive");
    }
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        if (distances[actor] < 0 || distances[actor] > length) {
            throw std::runtime_error("Linkup route distance is outside source route");
        }
    }
    route_distances_ = distances;
    route_length_ = length;
}

void LinkupFleet::set_scene_context(const scene::FleetSceneContext& context) {
    for (const auto& actor : context.actors) {
        if (actor.model_detail > 2) throw std::runtime_error("Linkup scene context has an invalid model detail");
    }
    auto next = context;
    scene_context_ = std::move(next);
}

void LinkupFleet::set_scrape_handle_query(std::function<bool(std::uint32_t, std::uint32_t)> query) {
    contact_options_.scrape_handle = std::move(query);
}

const CarContactEffects& LinkupFleet::contact_effects() const {
    return contact_pass_.effects();
}

std::span<const CarContact> LinkupFleet::contacts() const {
    return contacts_;
}

std::span<const LinkupActorEvent> LinkupFleet::events() const {
    return events_;
}

std::span<const std::uint32_t> LinkupFleet::integration_order() const {
    return integration_order_;
}

std::span<const scene::ImpactFragmentRequest> LinkupFleet::fragments() const {
    return fragments_;
}

std::span<const scene::ScrapeSparkRequest> LinkupFleet::sparks() const {
    return sparks_;
}

std::array<std::array<std::int32_t, 3>, 16> LinkupFleet::positions(const scene::BodyPose& local) const {
    std::array<Vector, 16> positions{};
    for (std::uint32_t actor = 0; actor < count(); ++actor) {
        positions[actor] = actor == local_actor() ? local.position : actors_[actor]->state().body.position;
    }
    return positions;
}

void LinkupFleet::set_race_leader(std::uint32_t actor) {
    require_actor(actor);
    race_leader_ = actor;
}

} // namespace toca::simulation
