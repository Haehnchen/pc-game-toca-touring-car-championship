#include "simulation/rivals.hpp"

#include "scene/engine_effects.hpp"
#include "scene/surface_effects.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/fixed.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace toca::simulation {
namespace {
using namespace fixed;

void validate(const RaceGrid& grid) {
    std::array<bool, 16> present{};
    for (const auto actor : grid.rank_to_actor) {
        if (actor >= present.size() || present[actor]) {
            throw std::runtime_error("Single Race grid requires each original actor exactly once");
        }
        present[actor] = true;
    }
}

const scene::QualifyingData& validate_startup(const ColdGridInitResult& startup, const SourceRandom& random) {
    if (random.state() != startup.rng_final) {
        throw std::runtime_error("Native startup must continue the generated final RNG without another classification");
    }
    if (startup.release_counter != 0
        || std::ranges::any_of(startup.pair_touching,
            [](auto touching) {
                return touching != 0;
            })
        || std::ranges::any_of(startup.actors, [](const auto& actor) {
               return actor.mode != BodyMode::distant;
           })) {
        throw std::runtime_error("Native startup requires the ordinary pre-selection release, contact reset and body modes");
    }
    return startup.data;
}

void validate_qualifying_startup(const QualifyingStartInitResult& startup, const SourceRandom& random) {
    const auto& grid = startup.repeated;
    if (grid.data.local_actor >= 16 || random.state() != grid.rng_final || startup.first.rng_final != grid.rng_final || startup.first.points != grid.points
        || grid.release_counter != 0 || std::ranges::any_of(grid.pair_touching, [](auto touching) {
               return touching != 0;
           })) {
        throw std::runtime_error("Qualifying startup requires its generated two-call RNG, points and contact reset");
    }
    validate(grid.grid);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto expected = actor % 4 == grid.data.local_actor % 4 ? BodyMode::full : BodyMode::removed;
        if (grid.actors[actor].mode != expected) {
            throw std::runtime_error("Qualifying startup requires the four active443f60 actor slots");
        }
    }
}

} // namespace

std::uint32_t RaceGrid::rank(std::uint32_t actor) const {
    const auto found = std::find(rank_to_actor.begin(), rank_to_actor.end(), actor);
    if (found == rank_to_actor.end()) {
        throw std::runtime_error("Single Race actor is missing from grid");
    }
    return std::uint32_t(found - rank_to_actor.begin());
}

RaceGrid make_race_grid(const scene::QualifyingData& data, SourceRandom& random, const scene::QualifyingResults* qualifying, scene::QualifyingResults* classification,
    const std::array<std::uint8_t, 16>* driver_bindings) {
    RaceGrid grid;
    const auto generated = qualifying ? scene::QualifyingResults{} : scene::classify_qualifying(data, 3'600'000, random, driver_bindings);
    const auto& results = qualifying ? *qualifying : generated;
    if (results.local_actor != data.local_actor) {
        throw std::runtime_error("Single Race classification uses another local actor");
    }
    if (classification) {
        *classification = results;
    }
    for (std::size_t rank = 0; rank < grid.rank_to_actor.size(); ++rank) {
        grid.rank_to_actor[rank] = results.rows[rank].actor_id;
    }
    validate(grid);
    if (!qualifying) {
        std::swap(grid.rank_to_actor[grid.rank(data.local_actor)], grid.rank_to_actor.back());
    }
    return grid;
}

AiDriver make_race_grid_driver(
    std::span<const std::uint8_t> height_initialized_route, const scene::QualifyingData& data, std::uint32_t actor, SourceRandom& random, bool impossible) {
    const auto& driver_data = data.drivers.at(actor);
    AiDriver driver(height_initialized_route, driver_data.raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random, impossible);
    driver.initialize_race_grid();
    return driver;
}

Rivals::Rivals(const HpcSurface& surface, const std::array<scene::GridSlot, 32>& slots, std::span<const std::uint8_t> route, const scene::QualifyingData& data,
    std::uint32_t weather, SourceRandom& random, const RaceGrid& grid, DamageContext& damage, SurfaceRoughness& roughness, const scene::BarrierData* barriers, bool low_gravity,
    bool impossible)
    : data_(data), grid_(grid), order_(grid.rank_to_actor) {
    validate(grid_);
    integration_order_.reserve(16);
    fragments_.reserve(10);
    sparks_.reserve(10);
    if (data_.local_actor >= actors_.size()) {
        throw std::runtime_error("Single Race local actor exceeds original grid");
    }
    const auto initialized_route = initialize_ai_route_heights(route, surface);
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (actor == data_.local_actor) {
            continue;
        }
        const auto car = data_.drivers[actor].car_id;
        const auto body = cold_grid_body(surface, slots, grid_, actor, car);
        actors_[actor] = std::make_unique<Motion>(surface, body, car, false, weather, false, damage, roughness, barriers, low_gravity);
        //401370 initializes the same race caches after either qualifying or
        //NoQual classification, before the first route observation.
        actors_[actor]->prepare_ai(make_race_grid_driver(initialized_route, data_, actor, random, impossible));
        actors_[actor]->handoff_to_ai(false);
        actors_[actor]->set_start_hold(true);
        actors_[actor]->set_rival(true);
        actors_[actor]->set_actor_identity(actor, false);
        modes_[actor] = BodyMode::distant;
    }
}

Rivals::Rivals(const HpcSurface& surface, const std::array<scene::GridSlot, 32>& slots, std::span<const std::uint8_t> route, const ColdGridInitResult& startup,
    std::uint32_t weather, SourceRandom& random, DamageContext& damage, SurfaceRoughness& roughness, const scene::BarrierData* barriers, bool low_gravity)
    : Rivals(surface, slots, route, validate_startup(startup, random), weather, random, startup.grid, damage, roughness, barriers, low_gravity) {
    release_counter_ = startup.release_counter;
    for (std::size_t actor = 0; actor < modes_.size(); ++actor) {
        modes_[actor] = startup.actors[actor].mode;
    }
    //The generated4287f0 result is the cold reset, not audio handle storage.
    contact_pass_.reset();
}

Rivals::Rivals(const HpcSurface& surface, const std::array<scene::GridSlot, 32>&, std::span<const std::uint8_t> route, const QualifyingStartInitResult& startup,
    std::uint32_t weather, SourceRandom& random, DamageContext& damage, SurfaceRoughness& roughness, const scene::BarrierData* barriers, bool low_gravity)
    : data_(startup.repeated.data), grid_(startup.repeated.grid), order_(grid_.rank_to_actor), held_(false), qualifying_(true) {
    validate_qualifying_startup(startup, random);
    integration_order_.reserve(16);
    fragments_.reserve(10);
    sparks_.reserve(10);
    const auto initialized_route = initialize_ai_route_heights(route, surface);
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        modes_[actor] = startup.repeated.actors[actor].mode;
        if (actor == data_.local_actor) {
            continue;
        }
        const auto car = data_.drivers[actor].car_id;
        const auto preview = scene::initial_body_pose(surface, scene::qualifying_start(initialized_route, data_.local_actor, actor), car);
        auto motion = std::make_unique<Motion>(surface, preview, car, false, weather, true, damage, roughness, barriers, low_gravity);
        motion->prepare_ai(make_race_grid_driver(initialized_route, data_, actor, random));
        motion->handoff_to_ai(false);
        motion->set_rival(false);
        motion->set_actor_identity(actor, false);
        motion->restore_generated_grid_controls(startup.repeated.actors[actor].snapshot);
        actors_[actor] = std::move(motion);
    }
    contact_pass_.reset();
}

void Rivals::prepare_first_race_step(std::span<const scene::BodyPose, 16> bodies, Motion& local) {
    if (qualifying_) {
        throw std::runtime_error("Qualifying fleet requires its443f60 physical handoff");
    }
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        motion.prepare_first_race_step(bodies[actor]);
    }
}

void Rivals::prepare_first_qualifying_step(std::span<const scene::BodyPose, 16> bodies, Motion& local) {
    if (!qualifying_) {
        throw std::runtime_error("Qualifying physical handoff requires the443f60 fleet");
    }
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        motion.prepare_first_qualifying_step(bodies[actor]);
    }
}

void Rivals::restore_generated_grid_controls(const ColdGridInitResult& grid) {
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        if (actors_[actor]) {
            actors_[actor]->restore_generated_grid_controls(grid.actors[actor].snapshot);
        }
    }
}

void Rivals::update(const TimeStep& time, Motion& local, const std::array<std::uint32_t, 16>& next_points, const std::array<bool, 16>& finished, bool start_hold,
    const ControlsInput& local_input, PropContacts* props) {
    if (qualifying_ && start_hold) {
        throw std::runtime_error("Qualifying fleet has no NoQual launch hold");
    }
    contacts_.clear();
    integration_order_.clear();
    fragments_.clear();
    sparks_.clear();
    local.set_actor_identity(data_.local_actor, true);
    local.set_ai_release_counter(release_counter_);
    local.set_model_detail(scene_context_.actors[data_.local_actor].model_detail);
    local.set_damage_registration(scene_context_.actors[data_.local_actor].physical.sector);
    local.begin_tick();
    for (auto& actor : actors_) {
        if (actor) {
            actor->begin_tick();
        }
    }
    if (held_ && !start_hold) {
        for (auto& actor : actors_) {
            if (actor) {
                actor->release_ai_start();
            }
        }
    }
    held_ = start_hold;
    std::array<std::uint32_t, 16> order_storage;
    std::size_t order_count = 0;
    for (const auto actor : order_) {
        if (modes_[actor] != BodyMode::removed) {
            order_storage[order_count++] = actor;
        }
    }
    const auto order = std::span(order_storage).first(order_count);
    //404811 retains the last active ascending route actor's pose.404b07
    //reuses it for the short-player gap after ring ordering, not the follower.
    auto source_gap_position = local.state().body.position;
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (modes_[actor] != BodyMode::removed) {
            source_gap_position = (actor == data_.local_actor ? local : *actors_[actor]).state().body.position;
        }
    }
    std::array<AiTraffic, 16> fleet;
    //4036f0 follows game+99 through the allocated actor table even after
    //that blocker has left the active ring. Retirement retains its body.
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        auto& record = fleet[actor];
        record = motion.ai_traffic_state();
        record.actor_id = actor;
        record.player = actor == data_.local_actor;
        record.next_point = next_points[actor];
        record.route_distance_q16 = route_distances_[actor];
    }
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const auto actor = order[rank];
        auto& record = fleet[actor];
        record.ahead_id = order[(rank + order.size() - 1) % order.size()];
        record.behind_id = order[(rank + 1) % order.size()];
        const auto& ahead = record.ahead_id == data_.local_actor ? local : *actors_[record.ahead_id];
        record.forward_gap_q16 = source_forward_gap(route_distances_[actor], route_distances_[record.ahead_id], route_length_q16_, source_gap_position, ahead.state().body.position,
            record.ahead_id == data_.local_actor, record.ahead_id == actor);
        if (route_length_q16_ == 0) {
            auto delta = sub(ahead.state().body.position, record.body.position);
            delta[1] = 0;
            record.forward_gap_q16 = length(delta);
        }
    }
    std::array<BodySelectionActor, 16> selection;
    for (std::uint32_t actor = 0; actor < selection.size(); ++actor) {
        const auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        const auto& state = motion.state();
        auto& input = selection[actor];
        input.previous = modes_[actor];
        input.rollover = state.rollover;
        input.human_control = fleet[actor].human;
        input.player_identity = actor == data_.local_actor ? 0 : -1;
        input.support_count = state.contact_count;
        input.first_tire_q16 = motion.controls().tire_offsets_q16[0];
        input.normal_up_q16 = motion.retained_ground_up_q16();
        input.ahead = fleet[actor].ahead_id;
        input.behind = fleet[actor].behind_id;
        input.forward_gap_q16 = fleet[actor].forward_gap_q16;
    }
    BodySelectionContext selection_context;
    selection_context.local_actor = data_.local_actor;
    selection_context.tire_base_q16 = data_.tire_offset_q16;
    //Host race progress supplies the current ring; native ordinary races use
    //game+10==0. The shared selector also covers the captured alternate flag.
    modes_ = select_fleet_body_modes(selection, selection_context).modes;
    //403dd0's target pass includes the human and runs ascending before traffic.
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (modes_[actor] == BodyMode::removed) {
            continue;
        }
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        if (actor != data_.local_actor) {
            motion.set_rival(modes_[actor] == BodyMode::distant);
            if (finished[actor] != finished_[actor]) {
                motion.handoff_to_ai(finished[actor]);
                finished_[actor] = finished[actor];
            }
        }
        motion.set_ai_route_point(next_points[actor]);
        motion.observe_ai_route();
        fleet[actor].route_angle = motion.ai_traffic_state().route_angle;
    }
    const auto traffic_actor = [&](std::uint32_t actor) {
        return actors_[actor] || (qualifying_ && actor == data_.local_actor && local.ai_controlled());
    };
    const auto refresh_decision = [&](std::uint32_t actor) {
        const auto decision = (actor == data_.local_actor ? local : *actors_[actor]).ai_traffic_state();
        auto& record = fleet[actor];
        record.blocked = decision.blocked;
        record.passing = decision.passing;
        record.blocker_id = decision.blocker_id;
        record.mode = decision.mode;
    };
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (traffic_actor(actor) && modes_[actor] != BodyMode::removed) {
            (actor == data_.local_actor ? local : *actors_[actor]).decide_rival_traffic(fleet[actor], fleet, route_length_q16_);
            refresh_decision(actor);
        }
    }
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (traffic_actor(actor) && modes_[actor] != BodyMode::removed) {
            (actor == data_.local_actor ? local : *actors_[actor]).decide_rival_traffic_brake(fleet[actor], fleet);
            refresh_decision(actor);
        }
    }
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        if (modes_[actor] == BodyMode::removed) {
            continue;
        }
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        motion.set_start_hold(start_hold);
        motion.set_ai_start_wait(start_hold, false);
        motion.recover_ai_controls();
    }
    //421aa0 maps every AI actor descending before415090 dispatches any body.
    for (std::uint32_t actor = 16; actor-- > 0;) {
        if (modes_[actor] != BodyMode::removed) {
            (actor == data_.local_actor ? local : *actors_[actor]).prepare_ai_controls();
        }
    }
    std::array<std::uint32_t, 16> full_storage;
    std::size_t full_count = 0;
    bool dispatch = true;
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        auto& motion = actor == data_.local_actor ? local : *actors_[actor];
        if (modes_[actor] == BodyMode::distant) {
            motion.update_distant(time, true);
            integration_order_.push_back(actor);
        } else if (modes_[actor] == BodyMode::full) {
            if (!motion.prepare_body_update(actor == data_.local_actor)) {
                dispatch = false;
                break;
            }
            full_storage[full_count++] = actor;
        }
    }
    if (dispatch) {
        const auto full_bodies = std::span(full_storage).first(full_count);
        for (auto it = full_bodies.rbegin(); it != full_bodies.rend(); ++it) {
            auto& motion = *it == data_.local_actor ? local : *actors_[*it];
            motion.update(*it == data_.local_actor ? local_input : ControlsInput{}, time, true);
            integration_order_.push_back(*it);
        }
    }
    // Prop movement is once per scene;45b900 retains even removed actors.
    if (props) {
        props->advance();
    }
    //409c30 queues wall effects in the415090 integration order, before428b50.
    for (const auto actor : integration_order_) {
        const auto& body = actor == data_.local_actor ? local : *actors_[actor];
        const auto& effect = body.state().barrier_effects;
        const auto palette = std::uint8_t(actor != data_.local_actor && car_id(actor) == 2 ? 11 : car_id(actor));
        if (effect.fragment_strength_q16 > 0 && fragments_.size() < 10) {
            fragments_.push_back({effect.position_q16, {}, effect.fragment_strength_q16, {palette, palette}});
        }
        if (effect.spark_strength_q16 > 0 && sparks_.size() < 10) {
            sparks_.push_back({effect.position_q16, effect.spark_direction_q16, effect.tangent_velocity_q16, effect.spark_strength_q16});
        }
    }
    contact_options_.queued_fragments = std::uint32_t(fragments_.size());
    contact_options_.queued_sparks = std::uint32_t(sparks_.size());
    std::array<CarContactActor, 16> contact_actors;
    std::array<ContactCandidateActor, 16> candidates;
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        auto* body = actor == data_.local_actor ? &local : actors_[actor].get();
        body->set_model_detail(scene_context_.actors[actor].model_detail);
        contact_actors[actor] = {body, car_id(actor), modes_[actor] != BodyMode::removed, modes_[actor] == BodyMode::full, actor == data_.local_actor,
            modes_[actor] != BodyMode::removed && (scene_context_.actors[actor].contact_effect_mask != 0)};
        candidates[actor] = {scene_context_.actors[actor].physical, modes_[actor] != BodyMode::removed};
    }
    contacts_ = contact_pass_.resolve(contact_actors, contact_candidate_groups(candidates), contact_options_);
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
        std::array<PropVehicle, 16> vehicles;
        for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
            const auto& state = contact_actors[actor].motion->state();
            vehicles[actor] = {state.body, state.velocity_q16, car_id(actor), actor == data_.local_actor};
        }
        const auto responses = props->contact(vehicles);
        for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
            contact_actors[actor].motion->apply_prop_response(responses[actor]);
        }
    }
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        auto& motion = *contact_actors[actor].motion;
        const auto mode = modes_[actor];
        motion.finish_ground(mode);
        if (mode == BodyMode::crashing && motion.state().boundary == MotionBoundary::none) {
            modes_[actor] = BodyMode::crashed;
        } else if (mode == BodyMode::crashed && !scene_context_.actors[actor].sector_visible) {
            modes_[actor] = BodyMode::removed;
        }
    }
}

void Rivals::set_release_counter(std::uint32_t counter) {
    release_counter_ = counter;
    for (auto& actor : actors_) {
        if (actor) {
            actor->set_ai_release_counter(counter);
        }
    }
}

void Rivals::set_order(const std::array<std::uint32_t, 16>& rank_to_actor) {
    validate(RaceGrid{rank_to_actor});
    order_ = rank_to_actor;
}

void Rivals::set_route_distances(const std::array<std::int32_t, 16>& distances_q16, std::int32_t route_length_q16) {
    if (route_length_q16 <= 0) {
        throw std::runtime_error("Rival route distances must lie inside the source total distance");
    }
    for (std::size_t actor = 0; actor < distances_q16.size(); ++actor) {
        //401370 initializes distance=-1.443f60's removed actors never enter
        //403dd0's distance writer, so their retained records keep that value.
        if (modes_[actor] != BodyMode::removed && (distances_q16[actor] < 0 || distances_q16[actor] > route_length_q16)) {
            throw std::runtime_error("Rival route distances must lie inside the source total distance");
        }
    }
    route_distances_ = distances_q16;
    route_length_q16_ = route_length_q16;
}

std::uint8_t Rivals::traffic_mode(std::uint32_t actor) const {
    (void)this->actor(actor);
    return actors_[actor]->traffic_mode();
}

const MotionState& Rivals::actor(std::uint32_t id) const {
    if (id >= actors_.size() || !actors_[id]) {
        throw std::runtime_error("Rival actor access requires a nonlocal actor");
    }
    return actors_[id]->state();
}

const VehicleState& Rivals::controls(std::uint32_t id) const {
    if (id >= actors_.size() || !actors_[id]) {
        throw std::runtime_error("Rival controls access requires a nonlocal actor");
    }
    return actors_[id]->controls();
}

scene::SurfaceEffectVehicle Rivals::surface_effect_vehicle(std::uint32_t id) const {
    (void)actor(id);
    return actors_[id]->surface_effect_vehicle();
}

scene::EngineEffectVehicle Rivals::engine_effect_vehicle(std::uint32_t id) const {
    (void)actor(id);
    return actors_[id]->engine_effect_vehicle();
}

std::uint32_t Rivals::car_id(std::uint32_t id) const {
    return data_.drivers.at(id).car_id;
}

std::uint32_t Rivals::local_actor() const {
    return data_.local_actor;
}

bool Rivals::qualifying() const {
    return qualifying_;
}

bool Rivals::distant(std::uint32_t id) const {
    return modes_.at(id) == BodyMode::distant;
}

BodyMode Rivals::body_mode(std::uint32_t actor) const {
    return modes_.at(actor);
}

const RaceGrid& Rivals::grid() const {
    return grid_;
}

std::array<std::array<std::int32_t, 3>, 16> Rivals::positions(const scene::BodyPose& local) const {
    std::array<Vector, 16> result;
    for (std::size_t id = 0; id < result.size(); ++id) {
        result[id] = actors_[id] ? actors_[id]->state().body.position : local.position;
    }
    return result;
}

std::span<const CarContact> Rivals::contacts() const {
    return contacts_;
}

void Rivals::set_scene_context(const scene::FleetSceneContext& context) {
    for (const auto& actor : context.actors) {
        if (actor.model_detail > 2) throw std::runtime_error("Rivals scene context has an invalid model detail");
    }
    auto next = context;
    scene_context_ = std::move(next);
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        if (actors_[actor]) {
            actors_[actor]->set_model_detail(scene_context_.actors[actor].model_detail);
            actors_[actor]->set_damage_registration(scene_context_.actors[actor].physical.sector);
        }
    }
}

void Rivals::set_scrape_handle_query(std::function<bool(std::uint32_t, std::uint32_t)> query) {
    contact_options_.scrape_handle = std::move(query);
}

void Rivals::set_car_contacts_enabled(bool enabled) {
    contact_options_.enabled = enabled;
}

const CarContactEffects& Rivals::contact_effects() const {
    return contact_pass_.effects();
}

std::span<const std::uint32_t> Rivals::integration_order() const {
    return integration_order_;
}

std::span<const scene::ImpactFragmentRequest> Rivals::fragments() const {
    return fragments_;
}

std::span<const scene::ScrapeSparkRequest> Rivals::sparks() const {
    return sparks_;
}

const Motion& Rivals::motion(std::uint32_t actor) const {
    return *actors_.at(actor);
}

void Rivals::publish_frame_brake_pedals(Motion& local) {
    for (auto actor = actors_.size(); actor-- > 0;) {
        (actor == data_.local_actor ? local : *actors_[actor]).publish_frame_brake_pedal();
    }
}
} // namespace toca::simulation
