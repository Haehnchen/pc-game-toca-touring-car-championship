#include "physical_fleet.hpp"

#include "scene/engine_effects.hpp"

#include <algorithm>
#include <bit>
#include <ostream>

namespace toca::test::fleet {
using namespace toca::simulation;

void Fleet::capture_physical(std::array<SourceSnapshot, 16>& view) const {
    for (std::size_t actor = 0; actor < 16; ++actor) {
        actors_[actor]->publish_source_physical_state(view[actor]);
    }
}

void Fleet::write(std::ostream& output, std::uint32_t id, std::string_view phase, const TimeStep* time, const ClockTransactions* lifecycle) {
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto values = columns(*actors_[actor]);
        values.push_back({"body_mode", std::int32_t(modes_[actor])});
        values.push_back({"roughness_cursor", roughness_.current()});
        values.push_back({"damage_cycle", damage_.cycle()});
        values.push_back({"car_contact_count", std::int32_t(partner_counts_[actor])});
        for (std::size_t i = 0; i < 15; ++i) {
            values.push_back({"car_partner_" + std::to_string(i), i < partner_counts_[actor] ? partners_[actor][i] : -1});
        }
        if (time) {
            values.push_back({"delta_q16", time->delta_q16});
            values.push_back({"reciprocal_q16", time->reciprocal_q16});
            values.push_back({"extra_update", time->extra_step});
        }
        if (lifecycle) {
            const std::array<std::string, 9> names{
                "timer0_origin", "timer1_origin", "timer2_origin", "timer3_origin", "timer4_origin", "start_phase", "session_state", "release_epoch", "caller_clock"};
            const auto generated = lifecycle->lifecycle_values();
            for (std::size_t field = 0; field < names.size(); ++field) {
                values.push_back({names[field], generated[field]});
            }
        }
        if (!wrote_header_) {
            output << "id,actor,phase";
            for (const auto& value : values) {
                output << ',' << value.name;
            }
            output << '\n';
            wrote_header_ = true;
        }
        output << id << ',' << actor << ',' << phase;
        for (const auto& value : values) {
            output << ',' << value.value;
        }
        output << '\n';
    }
}

void Fleet::write_detached(std::ostream& output, std::uint32_t id, std::string_view phase) const {
    for (std::size_t slot = 0; slot < damage_.parts().parts().size(); ++slot) {
        const auto& part = damage_.parts().parts()[slot];
        output << id << ',' << phase << ',' << slot << ',' << damage_.parts().status().count << ',' << part.active << ',' << damage_.parts().free(slot) << ','
               << unsigned(part.type) << ',' << unsigned(part.source_slot);
        for (const auto& basis : {part.basis, part.rotation}) {
            for (const auto& row : basis) {
                for (const auto value : row) {
                    output << ',' << value;
                }
            }
        }
        for (const auto& vector : {part.position, part.velocity}) {
            for (const auto value : vector) {
                output << ',' << value;
            }
        }
        output << ',' << part.initial_y << ',' << part.remaining_ticks << ',' << part.sector << ',' << part.cached_triangle << ',' << part.resting << '\n';
    }
}

Fleet::Fleet(const HpcSurface& surface, const scene::BarrierData& barriers, const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
    const Terrain& terrain, std::uint32_t local, std::uint32_t weather)
    : surface_(surface), barriers_(barriers), damage_(surface, &barriers, false, process_.damage), props_(cones, posts, signs, terrain.sector_centers, surface), local_(local),
      weather_(weather) {
    damage_.set_visibility([this](auto sector) {
        return visibility_all_ || (sector < sector_visible_.size() && sector_visible_[sector]);
    });
}

std::vector<InitialWorldValue> Fleet::initial_world_values(const ColdGridInitResult* startup) const {
    std::vector<InitialWorldValue> values;
    const auto add = [&](std::string field, std::uint32_t index, std::int64_t value) {
        values.push_back({std::move(field), index, value});
    };
    const auto vector = [&](const std::string& prefix, std::uint32_t index, const auto& v) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            add(prefix + "_" + std::to_string(axis), index, v[axis]);
        }
    };
    const auto basis = [&](const std::string& prefix, std::uint32_t index, const auto& v) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            vector(prefix + "_" + std::to_string(axis), index, v[axis]);
        }
    };
    const auto& touching = contacts_.source_touching();
    for (std::uint32_t pair = 0; pair < touching.size(); ++pair) {
        add("touching", pair, touching[pair]);
    }
    const auto persistent = damage_.persistent_state();
    add("roughness", 0, roughness_.current());
    add("damage_cycle", 0, persistent.cycle);
    add("detached_count", 0, damage_.parts().status().count);
    for (std::uint32_t index = 0; index < persistent.selection.size(); ++index) {
        const auto& selected = persistent.selection[index];
        add("damage_selection", index * 3, selected.node_id);
        add("damage_selection", index * 3 + 1, selected.rule);
        add("damage_selection", index * 3 + 2, selected.slot);
    }
    for (std::uint32_t slot = 0; slot < damage_.parts().parts().size(); ++slot) {
        add("detached_active", slot, damage_.parts().parts()[slot].active);
        add("detached_free", slot, damage_.parts().free(slot));
    }
    for (std::uint32_t actor = 0; actor < actors_.size(); ++actor) {
        const auto damage = actors_[actor] ? actors_[actor]->state().damage : DamageState{};
        for (std::size_t level = 0; level < damage.levels.size(); ++level) {
            add("damage_level_" + std::to_string(level), actor, damage.levels[level]);
        }
        for (std::size_t feature = 0; feature < damage.broken_features.size(); ++feature) {
            add("broken_feature_" + std::to_string(feature), actor, damage.broken_features[feature]);
        }
        add("damage_feedback_side", actor, damage.feedback_side);
        add("damage_feedback_strength", actor, damage.feedback_strength);
        add("barrier_active", actor, actors_[actor] && actors_[actor]->state().barrier_recovery.active);
        add("contact_count", actor, partner_counts_[actor]);
        SourceSnapshot context;
        std::uint32_t identity_actor, identity_car;
        std::int32_t owner;
        ControllerHandoffState controller;
        if (actors_[actor]) {
            actors_[actor]->publish_source_cold_context(context);
            const auto identity = actors_[actor]->engine_effect_vehicle();
            identity_actor = identity.actor;
            identity_car = identity.car_type;
            owner = identity.local_player ? 0 : -1;
            controller = actors_[actor]->controller_handoff_state();
        } else {
            if (!startup) {
                throw std::logic_error("Initial cold actor projection requires the generated grid");
            }
            context = startup->actors[actor].snapshot;
            identity_actor = context.body[0x515];
            identity_car = context.body[0x516];
            owner = startup->ownership[actor];
            controller.human = source_u32(context.gameplay, 8);
            for (std::size_t part = 0; part < 4; ++part) {
                controller.modes[part] = source_i32(context.dynamics, part == 3 ? 0x27c : 0x310 + part * 4);
            }
        }
        for (const auto& field : source_cold_actor_fields) {
            const auto raw = field.block == 0 ? std::span<const std::uint8_t>(context.body) : std::span<const std::uint8_t>(context.dynamics);
            const auto width = std::string_view(field.name) == "visual_angles" ? 2u : std::string_view(field.name) == "rollover_ticks" ? 1u : 4u;
            for (std::size_t part = 0; part < field.size / width; ++part) {
                const auto offset = field.offset + part * width;
                const auto value = width == 4   ? std::int64_t(source_i32(raw, offset))
                                   : width == 2 ? std::int64_t(std::bit_cast<std::int16_t>(source_u16(raw, offset)))
                                                : raw[offset];
                add(std::string("cold_") + field.name + "_" + std::to_string(part), actor, value);
            }
        }
        add("cold_limiter_active", actor, context.limiter_active);
        add("cold_limiter_load", actor, context.limiter_load_q16);
        add("cold_identity_actor", actor, identity_actor);
        add("cold_identity_car", actor, identity_car);
        add("cold_identity_owner", actor, owner);
        add("cold_controller_human", actor, controller.human);
        for (std::size_t part = 0; part < controller.modes.size(); ++part) {
            add("cold_controller_mode_" + std::to_string(part), actor, controller.modes[part]);
        }
    }
    const auto props = props_.snapshot();
    add("cones_count", 0, props.cones.size());
    for (std::uint32_t index = 0; index < props.cones.size(); ++index) {
        const auto& cone = props.cones[index];
        basis("cone_axes", index, cone.axes);
        vector("cone_position", index, cone.position);
        vector("cone_velocity", index, cone.velocity);
        vector("cone_angular", index, cone.angular);
        add("cone_triangle", index, cone.triangle.value_or(0));
        add("cone_grounded", index, cone.grounded);
        add("cone_settled", index, cone.settled);
        add("cone_state", index, cone.state);
        add("cone_translating", index, cone.translating);
        add("cone_rotating", index, cone.rotating);
    }
    add("posts_count", 0, props.posts.size());
    for (std::uint32_t index = 0; index < props.posts.size(); ++index) {
        const auto& post = props.posts[index];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            for (std::size_t component = 0; component < 3; ++component) {
                add("post_saved_" + std::to_string(axis) + "_" + std::to_string(component), index, post.saved[axis * 4 + component]);
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            add("post_saved_position_" + std::to_string(axis), index, post.saved[12 + axis]);
        }
        basis("post_axes", index, post.axes);
        vector("post_position", index, post.position);
        vector("post_normal", index, post.normal);
        for (std::size_t end = 0; end < 2; ++end) {
            vector("post_endpoint_" + std::to_string(end), index, post.endpoints[end]);
        }
        add("post_degrees", index, post.degrees);
        add("post_wait", index, post.wait);
        add("post_state", index, post.state);
        add("post_touched", index, post.touched);
        add("post_broken", index, post.broken);
    }
    add("signs_count", 0, props.signs.size());
    for (std::uint32_t index = 0; index < props.signs.size(); ++index) {
        const auto& sign = props.signs[index];
        for (std::size_t component = 0; component < sign.transform.size(); ++component) {
            add("sign_transform_" + std::to_string(component), index, sign.transform[component]);
        }
        for (std::size_t end = 0; end < 2; ++end) {
            vector("sign_endpoint_" + std::to_string(end), index, sign.endpoints[end]);
        }
        vector("sign_normal", index, sign.normal);
        add("sign_state", index, sign.state);
        add("sign_visible", index, sign.visible);
    }
    return values;
}

void Fleet::copy_limiter(std::array<bool, 16>& active, std::array<std::int32_t, 16>& load) const {
    for (std::size_t actor = 0; actor < 16; ++actor) {
        active[actor] = actors_[actor]->controls().limiter_active;
        load[actor] = actors_[actor]->limiter_load_q16();
    }
}

void Fleet::publish_physical(std::array<SourceSnapshot, 16>& destination, bool ground, bool dispatch) const {
    const auto& view = ground ? ground_view_ : integration_view_;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        copy_physical_fields(destination[actor], view[actor]);
        if (dispatch) {
            source_store(destination[actor].body, 0x4f0, std::uint32_t((ground ? ground_modes_ : integration_modes_)[actor]));
        }
    }
}

void Fleet::publish_dispatch(std::array<SourceSnapshot, 16>& destination) const {
    for (std::size_t actor = 0; actor < 16; ++actor) {
        source_store(destination[actor].body, 0x4f0, std::uint32_t(modes_[actor]));
    }
}

void Fleet::publish_initial_body(std::array<SourceSnapshot, 16>& destination) const {
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        actors_[actor]->publish_source_physical_state(destination[actor]);
        destination[actor].body[0x515] = std::uint8_t(actor);
        destination[actor].body[0x516] = std::uint8_t(car_ids_[actor]);
        source_store(destination[actor].body, 0x4e8, std::uint32_t(actors_[actor]->state().published_brake_q16));
        source_store(destination[actor].body, 0x4f0, std::uint32_t(modes_[actor]));
    }
}

scene::BodyPose Fleet::local_pose() const {
    return actors_[local_]->state().body;
}

fixed::Vector Fleet::local_velocity() const {
    return actors_[local_]->state().velocity_q16;
}

void Fleet::scene_boundary(ScenePhysicalBoundary& boundary) const {
    boundary.local_body = actors_[local_]->state().body;
    boundary.velocity = actors_[local_]->state().velocity_q16;
    boundary.angular_velocity_y = actors_[local_]->state().angular_velocity_q16[1];
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        boundary.positions[actor] = actors_[actor]->state().body.position;
        boundary.modes[actor] = std::uint8_t(modes_[actor]);
    }
}

void Fleet::publish_initial_control_calibration(std::array<SourceSnapshot, 16>& destination) const {
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        actors_[actor]->publish_source_control_calibration(destination[actor]);
    }
}

BodySelectionResult Fleet::select_dispatch(BodySelectionContext context, std::array<SourceSnapshot, 16>& drivers, std::array<BodySelectionActor, 16>* selected_inputs) {
    //421aa0 runs414e20/414ef0 before403dd0 and the descending control pass.
    //Read the previous retained gameplay here, before any driver writer.
    context.alternate_direction = source_i32(drivers[local_].gameplay, 0x10) != 0;
    std::array<BodySelectionActor, 16> selection;
    for (std::size_t actor = 0; actor < 16; ++actor) {
        const auto& motion = *actors_[actor];
        const auto& game = drivers[actor].gameplay;
        auto& input = selection[actor];
        input.previous = modes_[actor];
        input.rollover = motion.state().rollover;
        input.support_count = motion.state().contact_count;
        input.first_tire_q16 = motion.controls().tire_offsets_q16[0];
        input.normal_up_q16 = motion.retained_ground_up_q16();
        input.human_control = source_u32(game, 8) != 0;
        input.player_identity = std::bit_cast<std::int8_t>(game[0x80]);
        input.ahead = game[0x96];
        input.behind = game[0x97];
        input.forward_gap_q16 = source_i32(game, 0xf0);
    }
    const auto result = select_fleet_body_modes(selection, context);
    modes_ = result.modes;
    publish_dispatch(drivers);
    if (selected_inputs) {
        *selected_inputs = selection;
    }
    return result;
}

void Fleet::copy_pair_history(std::array<std::uint8_t, 0x800>& destination) const {
    destination.fill(0);
    const auto& touching = contacts_.source_touching();
    for (std::size_t pair = 0; pair < touching.size(); ++pair) {
        source_store(destination, pair * 8 + 4, touching[pair]);
    }
}

void Fleet::local_boundary(const SourceLocalInputBoundary& boundary) {
    actors_[local_]->prepare_source_local_input(boundary);
}

void Fleet::local_handoff_to_ai(std::int32_t tire_base) {
    actors_[local_]->source_handoff_to_ai(tire_base);
}

void Fleet::publish_warm_context(std::array<SourceSnapshot, 16>& actors) const {
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        source_store(actors[actor].dynamics, 0x12c, std::uint32_t(actors_[actor]->controls().engine_drive_q16));
    }
}

void Fleet::warm_grid(const ColdGridInitResult& grid) {
    reset_grid_world(contacts_, damage_, props_);
    partner_counts_.fill(0);
    for (std::size_t actor = 0; actor < actors_.size(); ++actor) {
        const auto& generated = grid.actors[actor];
        auto reset = generated.snapshot;
        //408690 clears active without discarding this Motion's retained
        //recovery vectors/secondary latch; inactive import leaves vectors.
        reset.has_barrier_recovery = true;
        source_store(reset.barrier_recovery, 0x2c, actors_[actor]->state().barrier_recovery.has_secondary);
        actors_[actor]->restore_source_snapshot(reset);
        actors_[actor]->set_actor_identity(actor, generated.human);
        modes_[actor] = integration_modes_[actor] = ground_modes_[actor] = generated.mode;
        reverse_[actor] = false;
    }
}

void Fleet::restore(const Record& record, bool fresh_world) {
    if (fresh_world) {
        const auto registered = props_.snapshot();
        if (record.props[0].size() != registered.cones.size() * 0xcc || record.props[1].size() != registered.posts.size() * 0x13c
            || record.props[2].size() != registered.signs.size() * 0x40 || record.sign_models.size() != registered.signs.size()) {
            throw std::runtime_error("Cold prop declarations differ from canonical resource registration");
        }
        reset_grid_world(contacts_, damage_, props_);
    } else {
        roughness_.import_cursor(record.roughness);
        damage_.restore_source_state(record.damage_cycle, record.damage_selection, record.detached_pool, record.detached_free, record.detached_count);
        contacts_.restore_source_touching(record.pair_history);
        props_.restore_source_state(record.props[0], record.props[1], record.props[2], record.sign_models);
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& captured = record.actors[actor];
        auto snapshot = captured.snapshot;
        if (fresh_world) {
            snapshot.has_damage = snapshot.has_barrier_recovery = false;
        }
        const auto car = snapshot.body[0x516];
        if (car >= 10 || snapshot.body[0x515] != actor) {
            throw std::runtime_error("Fleet actor/car identity disagrees with original source slot");
        }
        car_ids_[actor] = car;
        modes_[actor] = BodyMode(source_u32(snapshot.body, 0x4f0));
        actors_[actor] = std::make_unique<Motion>(surface_, scene::BodyPose{}, car, source_i32(snapshot.dynamics, 0x27c) == 1, weather_, false, damage_, roughness_, &barriers_);
        try {
            if (fresh_world) {
                actors_[actor]->restore_cold_source_snapshot(snapshot);
            } else {
                actors_[actor]->restore_source_snapshot(snapshot);
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Fleet initial actor " + std::to_string(actor) + " body mode " + std::to_string(source_u32(snapshot.body, 0x4f0)) + ": " + error.what());
        }
        actors_[actor]->set_actor_identity(actor, std::bit_cast<std::int8_t>(record.ownership[actor]) >= 0);
        reverse_[actor] = source_i32(snapshot.gameplay, 0x28) != 0;
        partner_counts_[actor] = fresh_world ? 0 : snapshot.body[0x529];
        if (partner_counts_[actor] > 15) {
            throw std::runtime_error("Captured original contact list exceeds15 partners");
        }
        for (std::size_t i = 0; i < partner_counts_[actor]; ++i) {
            partners_[actor][i] = snapshot.body[0x52a + i];
        }
    }
}

void Fleet::step(const Record& record, std::ostream& output, std::ostream& detached, std::ostream& launch, std::ostream& selection_output, bool imported, bool derive_selection,
    bool preselected_dispatch, const ClockTransactions* clock, const scene::FleetSceneContext* generated_scene) {
    auto selection = record.selection;
    if (derive_selection && !imported) {
        for (std::size_t actor = 0; actor < selection.size(); ++actor) {
            const auto& motion = *actors_[actor];
            auto& input = selection[actor];
            input.previous = modes_[actor];
            input.rollover = motion.state().rollover;
            input.support_count = motion.state().contact_count;
            input.first_tire_q16 = motion.controls().tire_offsets_q16[0];
            input.normal_up_q16 = motion.retained_ground_up_q16();
        }
    }
    //The joined dispatch mode has already selected before AI. Never rerun
    //the selector here using tires/gameplay produced by that AI pass.
    if (!preselected_dispatch) {
        const auto selected = select_fleet_body_modes(selection, record.selection_context);
        for (std::size_t actor = 0; actor < selected.modes.size(); ++actor) {
            selection_output << record.id << ',' << actor << ',' << unsigned(selected.modes[actor]) << '\n';
        }
        if (derive_selection) {
            modes_ = selected.modes;
        }
    } else {
        for (std::size_t actor = 0; actor < modes_.size(); ++actor) {
            selection_output << record.id << ',' << actor << ',' << unsigned(modes_[actor]) << '\n';
        }
    }
    std::array<SourceInput, 16> inputs;
    auto& local_motion = *actors_[local_];
    const bool consumed_hold = source_i32(record.actors[local_].snapshot.dynamics, 0x148) != 0;
    if (!imported) {
        //Only events enter this prephase. Captured numeric launch/RPM/tire
        //values are outputs; the native Controls state survives each tick.
        local_motion.prepare_source_local_input({record.pre_start_hold != 0, (record.launch_flags & 1) != 0, false, (record.launch_flags & 0x40) != 0});
        if (local_motion.controls().launch_tire_adjustment_q16 != record.pre_launch || local_motion.controls().engine_speed_q16 != record.pre_rpm) {
            throw UnsupportedSourceState("Fleet update " + std::to_string(record.id) + " has an unexplained local launch/RPM change before421aa0");
        }
        local_motion.prepare_source_local_input({consumed_hold, (record.launch_flags & 2) != 0, (record.launch_flags & 4) != 0, (record.launch_flags & 0x80) != 0});
    }
    const auto& local_controls = local_motion.controls();
    launch << record.id << ',' << local_ << ",consumed," << local_controls.launch_tire_adjustment_q16 << ',' << local_controls.engine_speed_q16 << ','
           << local_motion.start_held();
    for (const auto offset : local_controls.tire_offsets_q16) {
        launch << ',' << offset;
    }
    launch << '\n';
    visibility_all_ = generated_scene ? generated_scene->visibility.ordered_sectors.empty() : record.visibility_override != 0 || record.visibility_control == 0;
    for (std::size_t sector = 0; sector < sector_visible_.size(); ++sector) {
        sector_visible_[sector] =
            generated_scene ? generated_scene->visibility.sector_visible(std::uint16_t(sector)) : (record.visibility[sector / 8] & (1u << (sector % 8))) != 0;
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto& motion = *actors_[actor];
        const auto& captured = record.actors[actor];
        const auto& snapshot = captured.snapshot;
        if (snapshot.body[0x516] != car_ids_[actor]) {
            throw UnsupportedSourceState("Fleet car configuration changed after initial import");
        }
        const auto mode = source_u32(snapshot.body, 0x4f0);
        if (mode > 4) {
            throw std::runtime_error("Captured original body mode is outside0..4");
        }
        if (!derive_selection && !preselected_dispatch) {
            modes_[actor] = BodyMode(mode); //Observed dispatch baseline.
        }
        motion.begin_tick();
        motion.set_actor_identity(actor, std::bit_cast<std::int8_t>(record.ownership[actor]) >= 0);
        motion.set_model_detail(generated_scene ? generated_scene->actors[actor].model_detail : captured.detail);
        motion.set_damage_registration(generated_scene ? generated_scene->actors[actor].physical.sector : captured.registration.sector);
        //401260 clutch/gear state is independent of the448130 event.
        motion.set_source_start_hold(source_i32(snapshot.dynamics, 0x148) != 0);
        auto& input = inputs[actor];
        const auto& raw = snapshot.dynamics;
        input = {source_i32(raw, 0x140), source_i32(raw, 0x144), source_i32(raw, 0x138), source_i32(raw, 0x13c), std::bit_cast<std::int8_t>(raw[0x14c])};
        const bool reverse = source_i32(snapshot.gameplay, 0x28) != 0;
        if (source_i32(raw, 0x27c) == 0 && reverse != reverse_[actor]) {
            input.forced_gear = std::int8_t(raw[0x330]);
            input.reset_reverse_pedal = !reverse;
        }
        reverse_[actor] = reverse;
        if (source_i32(raw, 0x27c) == 0) {
            const auto offset = source_i32(raw, 0xdc);
            for (std::size_t index = 1; index < 8; ++index) {
                if (source_i32(raw, 0xdc + index * 4) != offset) {
                    throw UnsupportedSourceState("Observed AI tire output must be the uniform12890 assignment");
                }
            }
            const auto base = record.ai_tire_base;
            const auto previous = motion.controls().tire_offsets_q16[0];
            //402ac0 adds62h below the shared base, returning the base on
            //the following tick if that addition overshoots it.
            if (!valid_observed_ai_tire_offset(offset, previous, base)) {
                throw UnsupportedSourceState(
                    "Fleet update " + std::to_string(record.id) + " actor " + std::to_string(actor) + " has an unsupported AI tire output outside402b10/402ac0");
            }
            input.ai_tire_offset_q16 = offset;
        }
    }
    std::vector<std::uint32_t> full;
    bool dispatch = true;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (modes_[actor] == BodyMode::distant) {
            actors_[actor]->update_source_distant(inputs[actor], record.time, true);
        } else if (modes_[actor] == BodyMode::full) {
            if (!actors_[actor]->prepare_body_update(actor == local_)) {
                dispatch = false;
                break;
            }
            full.push_back(actor);
        }
    }
    if (dispatch) {
        for (auto it = full.rbegin(); it != full.rend(); ++it) {
            actors_[*it]->update_source_input(inputs[*it], record.time, true);
        }
    }
    props_.advance();
    integration_modes_ = modes_;
    capture_physical(integration_view_);
    write(output, record.id, "integration", clock ? &record.time : nullptr, clock);
    write_detached(detached, record.id, "integration");
    std::array<CarContactActor, 16> bodies;
    std::array<ContactCandidateActor, 16> candidates;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto& captured = record.actors[actor];
        bodies[actor] = {actors_[actor].get(), car_ids_[actor], modes_[actor] != BodyMode::removed, modes_[actor] == BodyMode::full,
            std::bit_cast<std::int8_t>(record.ownership[actor]) >= 0, generated_scene ? generated_scene->actors[actor].contact_effect_mask != 0 : captured.mask != 0};
        candidates[actor] = {generated_scene ? generated_scene->actors[actor].physical : captured.registration, modes_[actor] != BodyMode::removed};
    }
    //Shared pair history is imported only at the declared initialization.
    if (generated_scene || record.contacts_enabled) {
        partner_counts_.fill(0);
        const auto hits = contacts_.resolve(bodies, contact_candidate_groups(candidates));
        for (const auto& hit : hits) {
            for (const auto& [actor, other] : {std::pair{hit.first, hit.second}, std::pair{hit.second, hit.first}}) {
                partners_[actor].at(partner_counts_[actor]++) = std::int32_t(other);
                if (modes_[actor] == BodyMode::crashed) {
                    modes_[actor] = BodyMode::crashing;
                }
            }
        }
    }
    std::array<PropVehicle, 16> vehicles;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        vehicles[actor] = {actors_[actor]->state().body, actors_[actor]->state().velocity_q16, car_ids_[actor], bodies[actor].local_player};
    }
    const auto responses = props_.contact(vehicles);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        actors_[actor]->apply_prop_response(responses[actor]);
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        actors_[actor]->finish_ground(modes_[actor]);
        if (modes_[actor] == BodyMode::crashing && actors_[actor]->state().boundary == MotionBoundary::none) {
            modes_[actor] = BodyMode::crashed;
        } else if (modes_[actor] == BodyMode::crashed && !(generated_scene ? generated_scene->actors[actor].sector_visible : record.actors[actor].visible)) {
            modes_[actor] = BodyMode::removed;
        }
    }
    ground_modes_ = modes_;
    capture_physical(ground_view_);
    write(output, record.id, "ground", clock ? &record.time : nullptr, clock);
    write_detached(detached, record.id, "ground");
    damage_.advance(); //The retained native world advances once at415e10.
    write_detached(detached, record.id, "post");
}

} // namespace toca::test::fleet
