#include "simulation/car_contacts.hpp"
#include "simulation/fixed.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/race_world.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;
constexpr Basis identity{{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
constexpr std::array<std::array<std::int32_t, 2>, 4> sectors{{{0, 0}, {64 * one, 0}, {0, -64 * one}, {64 * one, -64 * one}}};

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void word(auto& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
        bytes.at(offset + byte) = std::uint8_t(std::uint32_t(value) >> (byte * 8));
    }
}

HpcSurface floor() {
    std::vector<std::uint8_t> bytes(106);
    const auto short_word = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = std::uint8_t(value);
        bytes[offset + 1] = std::uint8_t(value >> 8);
    };
    word(bytes, 0, -128 * one);
    word(bytes, 4, -128 * one);
    short_word(8, 1);
    short_word(20, 2);
    short_word(24, 4);
    const std::array<Vector, 4> vertices{{{-64 * one, 0, -64 * one}, {-64 * one, 0, 64 * one}, {64 * one, 0, -64 * one}, {64 * one, 0, 64 * one}}};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            word(bytes, 28 + i * 12 + axis * 4, vertices[i][axis]);
        }
    }
    short_word(76, 2);
    for (std::size_t i = 0; i < 6; ++i) {
        short_word(80 + (i / 3) * 8 + (i % 3) * 2, std::array<std::uint16_t, 6>{0, 1, 2, 2, 1, 3}[i]);
    }
    short_word(96, 2);
    short_word(100, 1);
    short_word(102, 1);
    short_word(104, 1);
    return HpcSurface(bytes);
}

scene::PropResources prop(Vector point) {
    scene::PropResources result;
    ModelNode node{-1, {}, 0, {}};
    node.transform[0] = node.transform[5] = node.transform[10] = node.transform[15] = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        node.transform[12 + axis] = float(point[axis]) / one;
    }
    result.model.nodes.push_back(node);
    result.actors.push_back({0});
    return result;
}

PropVehicle vehicle(Vector point, Vector velocity) {
    PropVehicle result;
    result.body.position = point;
    result.body.basis = identity;
    result.velocity_q16 = velocity;
    return result;
}

void moving_car(Motion& motion, Vector point, Vector velocity) {
    scene::BodyPose body;
    body.position = point;
    body.basis = identity;
    scene::refresh_body_contacts(body, 0);
    // Synthetic actor kinematics only: shared contact/damage/prop state always
    // starts through the production world owner rather than an imported world.
    SourceSnapshot snapshot;
    const auto vector = [&](std::size_t offset, const Vector& value) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            word(snapshot.body, offset + axis * 4, value[axis]);
        }
    };
    vector(0xbc, point);
    vector(0x268, point);
    vector(0xc8, velocity);
    vector(0x1e4, {0, one, 0});
    for (std::size_t i = 0; i < 3; ++i) {
        vector(0x150 + i * 12, identity[i]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        vector(0xf0 + i * 12, body.contacts[i]);
        snapshot.body[0x517 + i] = 1;
    }
    word(snapshot.dynamics, 0x27c, 1);
    motion.restore_source_snapshot(snapshot);
}

bool same_scratch(const DamagePersistentState& a, const DamagePersistentState& b) {
    return a.cycle == b.cycle && std::ranges::equal(a.selection, b.selection, [](const auto& x, const auto& y) {
        return x.node_id == y.node_id && x.rule == y.rule && x.slot == y.slot;
    });
}

void cold_actor_context() {
    const auto surface = floor();
    auto process = fresh_race_world_process();
    DamageContext damage(surface);
    const auto body = scene::initial_body_pose(surface, {{0, 0}, 0, 0}, 0);
    Motion factory(surface, body, 0, false, 1, false, damage, process.roughness);
    SourceSnapshot authored;
    factory.publish_warm_grid_context(authored);
    factory.publish_source_control_calibration(authored);
    word(authored.dynamics, 0x27c, 2); // Generated automatic keyboard handoff.
    authored.dynamics[0x330] = 1;
    word(authored.body, 0x510, 1); // Explicit contact-enable configuration.
    authored.body[0x53b] = 0xff; // No external force-feedback device slot.
    // These slots are written before their first relevant physical read. The
    // new contract must not turn them into speculative cold-zero reset claims.
    word(authored.body, 0x268, 1234);
    word(authored.dynamics, 0x128, 4321);
    word(authored.dynamics, 0x12c, 123);
    word(authored.dynamics, 0x240, 5678);
    word(authored.dynamics, 0x1b4, 8765);
    authored.dynamics[0x334] = 0; // Native constructor1 is not a source writer.
    auto observed = authored;
    for (const auto& field : source_cold_actor_fields) {
        auto bytes = field.block == 0 ? std::span<std::uint8_t>(observed.body) : std::span<std::uint8_t>(observed.dynamics);
        std::fill_n(bytes.begin() + field.offset, field.size, std::uint8_t(1));
    }
    observed.limiter_active = true;
    observed.limiter_load_q16 = one;
    auto merged = observed;
    factory.publish_source_cold_context(merged);
    expect(source_i32(merged.body, 0x268) == 1234 && source_i32(merged.dynamics, 0x128) == 4321 && source_i32(merged.dynamics, 0x12c) == 123
               && source_i32(merged.dynamics, 0x240) == 5678 && source_i32(merged.dynamics, 0x1b4) == 8765 && merged.dynamics[0x334] == 0 && source_u32(merged.body, 0x510) == 1
               && merged.body[0x53b] == 0xff,
        "Cold publication must preserve before-first-write storage and external contact/device context");

    Controls controls(0), warm_controls(0);
    controls.restore_cold_source_snapshot(observed);
    warm_controls.restore_source_snapshot(observed);
    expect(controls.state().raw_throttle_q16 == 0 && controls.state().raw_left_q16 == 0 && controls.state().throttle_q16 == 0 && !controls.state().limiter_active
               && controls.limiter_load_q16() == 0 && controls.state().pending_gear == 0 && controls.state().engine_drive_q16 == 123 && warm_controls.state().raw_throttle_q16 != 0
               && warm_controls.state().limiter_active && warm_controls.limiter_load_q16() == one,
        "Cold Controls must retain typed fresh axes/throttle/limiter while observed restore retains its independent warm context");

    for (const bool distant : {false, true}) {
        Motion cold(surface, body, 0, false, 1, false, damage, process.roughness);
        Motion expected(surface, body, 0, false, 1, false, damage, process.roughness);
        Motion warm(surface, body, 0, false, 1, false, damage, process.roughness);
        cold.restore_cold_source_snapshot(observed);
        expected.restore_source_snapshot(authored);
        warm.restore_source_snapshot(observed);
        const auto handoff = cold.controller_handoff_state();
        expect(cold.state().velocity_q16 == Vector{} && cold.state().angular_velocity_q16 == Vector{} && cold.state().speed_q16 == 0 && !cold.state().rollover.player_righting
                   && !cold.state().tyre_gate && !cold.controls().limiter_active && warm.state().velocity_q16 != Vector{} && warm.state().rollover.player_righting
                   && warm.state().tyre_gate && warm.controls().limiter_active && handoff.human == 1 && handoff.modes == std::array<std::int32_t, 4>{0, 0, 0, 2},
            "Cold physical restore must retain actual constructor state independently of a diagnostic warm restore");
        const SourceInput input{.throttle_q16 = one, .left_q16 = 0x1000};
        if (distant) {
            cold.update_source_distant(input, {});
            expected.update_source_distant(input, {});
        } else {
            cold.update_source_input(input, {});
            expected.update_source_input(input, {});
        }
        SourceSnapshot actual_step, expected_step;
        cold.publish_warm_grid_context(actual_step);
        expected.publish_warm_grid_context(expected_step);
        expect(cold.state().boundary == MotionBoundary::none && actual_step.body == expected_step.body && actual_step.dynamics == expected_step.dynamics,
            "The first actual full/distant consumer must match clean production context despite poisoned observed cold fields");
    }
}

void world_lifetime() {
    const auto surface = floor();
    auto process = fresh_race_world_process();
    DamageContext damage(surface, nullptr, false, process.damage);
    CarContacts contacts;
    std::vector<std::optional<std::uint16_t>> queries;
    const auto ground = [&](auto, auto, auto cached) -> std::optional<HpcPropSample> {
        queries.push_back(cached);
        return HpcPropSample{0, {0, one, 0}, 7, 0};
    };
    PropContacts props(prop({0, 0x75c2, 0}), prop({20 * one, 0, 0}), prop({40 * one, 0, 0}), sectors, ground);
    const auto initial = props.snapshot();
    expect(initial.cones[0].axes == identity && initial.cones[0].position == Vector{0, 0x75c2, 0} && initial.cones[0].triangle == 0
               && initial.posts[0].endpoints[0] == Vector{20 * one, 0x8666, 0} && initial.posts[0].endpoints[1] == initial.posts[0].endpoints[0]
               && initial.signs[0].endpoints[0] == Vector{41 * one, one, 0} && initial.signs[0].endpoints[1] == Vector{39 * one, one, 0} && initial.signs[0].visible
               && initial.signs[0].transform[12] == 40 * one,
        "Cold prop controllers must derive registered geometry and cache0 from authored resources and ground queries");
    queries.clear();
    props.advance();
    expect(queries == std::vector<std::optional<std::uint16_t>>{0} && props.snapshot().cones[0].triangle == 7,
        "The first actual cone update must consume cold cache0 and retain the returned triangle");
    props.contact(vehicle({-2 * one, 0x75c2, 0}, {0x8000, 0, 0}));
    props.contact(vehicle({19 * one, 0x8666, 0}, {0x8000, 0, 0}));
    props.contact(vehicle({39 * one, one, 0}, {0x4000, 0, 0}));
    const auto struck = props.snapshot();
    expect(struck.cones[0].state == 1 && struck.posts[0].state == 4 && struck.signs[0].state == 3 && !props.frame().sounds.empty(),
        "The package fixture must first produce live cone, post and sign state and current effects");

    Motion first(surface, scene::BodyPose{}, 0, true, 1, false, damage, process.roughness);
    Motion second(surface, scene::BodyPose{}, 0, true, 1, false, damage, process.roughness);
    moving_car(first, {0, 5 * one, 0}, {4 * one, 0, 0});
    moving_car(second, {4 * one, 5 * one, 0}, {-4 * one, 0, 0});
    ContactCandidateGroups groups;
    groups.count = 1;
    groups.groups[0].sector = 0;
    groups.groups[0].count = 2;
    groups.groups[0].actors[1] = 1;
    const std::array<CarContactActor, 2> actors{{{&first, 0}, {&second, 0}}};
    expect(contacts.resolve(actors, groups).size() == 1
               && std::ranges::any_of(contacts.source_touching(),
                   [](bool value) {
                       return value;
                   })
               && !contacts.effects().audio.empty() && first.damage().levels != DamageState{}.levels,
        "Fresh shared owners must produce actual pair touching, effects and actor damage before reset");
    DamageState actor;
    DamageInput hit;
    hit.contact.local_contact_q16 = {one, 0, one};
    hit.contact.impulse_q16 = {one, 0, 0};
    hit.inverse_half_dimensions_q16 = {one, one, one};
    damage.apply(actor, hit);
    DamagePartsVehicle detached;
    detached.body.basis = identity;
    damage.parts().detach(0, detached);
    expect(damage.parts().status().count == 1 && damage.parts().parts()[0].active && !damage.parts().free(0),
        "The same cold detached pool must allocate its first free record through the production writer");
    const auto payload = damage.parts().parts()[0];
    process.roughness.next();
    process.roughness.next();
    const auto scratch = damage.persistent_state();
    expect(scratch.selection[3].node_id == 24 && scratch.selection[4].node_id == 5 && process.roughness.current() == 2,
        "Shared process scratch and roughness must be positively consumed before the grid transition");

    reset_grid_world(contacts, damage, props);
    const auto reset = props.snapshot();
    expect(std::ranges::none_of(contacts.source_touching(),
               [](bool value) {
                   return value;
               })
               && contacts.effects().audio.empty() && contacts.effects().fragments.empty() && contacts.effects().sparks.empty() && damage.parts().status().count == 0
               && std::ranges::none_of(damage.parts().parts(),
                   [](const auto& part) {
                       return part.active;
                   }),
        "The shared grid writer must clear pair/effect output and retire the entire detached pool together");
    for (std::size_t slot = 0; slot < 10; ++slot) {
        expect(damage.parts().free(slot), "Every detached slot must be free after the grid writer");
    }
    expect(damage.parts().parts()[0].position == payload.position && damage.parts().parts()[0].remaining_ticks == payload.remaining_ticks
               && same_scratch(damage.persistent_state(), scratch) && process.roughness.current() == 2,
        "Warm grid reset must retain dormant detached payload and the independently owned process scratch/cursor");
    expect(reset.cones[0].state == 4 && reset.cones[0].position == initial.cones[0].position && reset.cones[0].triangle == 7 && reset.posts[0].state == 0 && !reset.posts[0].touched
               && !reset.posts[0].broken && reset.posts[0].axes == initial.posts[0].axes && reset.signs[0].state == 0 && reset.signs[0].visible
               && reset.signs[0].transform == initial.signs[0].transform && props.frame().sounds.empty(),
        "The same grid writer must restore registered authored prop poses while retaining the source cone cache");
    expect(first.damage().levels != DamageState{}.levels, "Shared grid reset must leave actor repair to its separate factory owner");

    process.damage = damage.persistent_state();
    DamageContext reloaded(surface, nullptr, false, process.damage);
    DamageState next_actor;
    reset_damage(actor);
    hit.contact.local_contact_q16 = {0, -one, 0};
    damage.apply(actor, hit);
    reloaded.apply(next_actor, hit);
    expect(actor.levels == next_actor.levels && same_scratch(damage.persistent_state(), reloaded.persistent_state()) && reloaded.parts().status().count == 0
               && process.roughness.current() == 2,
        "A resource reload must retain partial-selection consumer behavior while creating a separate free detached pool");
    auto fresh = fresh_race_world_process();
    DamageContext independent(surface, nullptr, false, fresh.damage);
    expect(fresh.roughness.current() == 0 && same_scratch(independent.persistent_state(), DamagePersistentState{}),
        "A genuinely fresh process must be distinct from warm scene/grid state");
}
} // namespace

int main() {
    try {
        cold_actor_context();
        world_lifetime();
        std::cout << "PASS: cold actor/world production and retained warm grid/process lifetime\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
