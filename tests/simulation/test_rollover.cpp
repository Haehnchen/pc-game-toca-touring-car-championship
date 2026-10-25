#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/rollover.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = std::uint8_t(value);
    bytes.at(at + 1) = std::uint8_t(value >> 8);
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::int32_t value) {
    put16(bytes, at, std::uint16_t(value));
    put16(bytes, at + 2, std::uint16_t(std::uint32_t(value) >> 16));
}

std::vector<std::uint8_t> plane() {
    std::vector<std::uint8_t> bytes(106);
    put32(bytes, 0, -128 * one);
    put32(bytes, 4, -128 * one);
    put16(bytes, 8, 1);
    put16(bytes, 20, 2);
    put16(bytes, 24, 4);
    constexpr std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * one, -64 * one}, {-64 * one, 64 * one}, {64 * one, -64 * one}, {64 * one, 64 * one}}};
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        put32(bytes, 28 + vertex * 12, vertices[vertex][0]);
        put32(bytes, 32 + vertex * 12, 2 * one);
        put32(bytes, 36 + vertex * 12, vertices[vertex][1]);
    }
    put16(bytes, 76, 2);
    constexpr std::array<std::array<std::uint16_t, 3>, 2> faces{{{0, 1, 2}, {2, 1, 3}}};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put16(bytes, 80 + face * 8 + vertex * 2, faces[face][vertex]);
        }
    }
    put16(bytes, 96, 2);
    put16(bytes, 100, 1);
    put16(bytes, 102, 1);
    put16(bytes, 104, 1);
    return bytes;
}

void thresholds() {
    RolloverState upside, side, pitch;
    for (unsigned tick = 1; tick <= 61; ++tick) {
        upside.observe({0, -one, 0});
        side.observe({0, 0, one});
        pitch.observe({one, 0, 0});
        expect(upside.upside_down == (tick > 60), "4102e0 upside-down request must require61 consecutive stable support updates");
        expect(side.sideways == (tick > 10), "4102e0 side request must require11 consecutive stable support updates");
        expect(pitch.pitched == (tick > 25), "4102e0 pitch request must require26 consecutive stable support updates");
    }
    upside.observe({0, one, 0});
    expect(upside.upside_down && upside.ticks[0] == 61, "A latched source request is not cleared by a later upright support dot");
    RolloverState edge;
    for (unsigned tick = 0; tick < 60; ++tick) {
        edge.observe({0xe666, -0xf333, 0xf333});
    }
    expect(edge.ticks == std::array<std::uint8_t, 3>{} && !edge.upside_down && !edge.sideways && !edge.pitched,
        "Equality at each source orientation threshold must remain outside the trigger");
    edge.observe({0, -one, 0});
    edge.observe({0, one, 0});
    expect(edge.ticks[0] == 0, "An interrupted orientation interval must reset its unlatched source byte counter");

    expect(select_body_mode(BodyMode::full, upside, false, 4, -one) == BodyMode::crashing, "Eligible AI upside-down request must enter mode3");
    expect(select_body_mode(BodyMode::crashed, upside, false, 4, -one) == BodyMode::crashed, "A visible frozen wreck must remain mode2 with its requests latched");
    expect(select_body_mode(BodyMode::full, upside, true, 4, -one) == BodyMode::full, "Player/human/tyre-forced bodies must remain full despite a rollover request");
    expect(select_body_mode(BodyMode::removed, upside, true, 4, -one) == BodyMode::removed, "Mode4 must not reenter physics through forced-full selection");
    expect(select_body_mode(BodyMode::full, pitch, false, 4, one) == BodyMode::distant, "Pitch alone is not the mode3 removal selector");
    expect(select_body_mode(BodyMode::full, {}, false, 4, 0xf332) == BodyMode::full && select_body_mode(BodyMode::full, {}, false, 4, 0xf333) == BodyMode::distant,
        "414e20 must preserve the exact supported-body upright threshold");
}

void fleet_selection() {
    std::array<BodySelectionActor, 16> actors;
    for (std::uint32_t actor = 0; actor < actors.size(); ++actor) {
        auto& input = actors[actor];
        input.previous = BodyMode::full;
        input.support_count = 4;
        input.normal_up_q16 = one;
        input.ahead = (actor + 15) % 16;
        input.behind = (actor + 1) % 16;
        input.forward_gap_q16 = 100;
    }
    actors[0].player_identity = 0; //Permanent player remains full after AI handoff.
    actors[1].forward_gap_q16 = 50;
    BodySelectionContext context;
    auto selected = select_fleet_body_modes(actors, context);
    expect(selected.full_count == 4 && selected.modes[0] == BodyMode::full && selected.modes[1] == BodyMode::full && selected.modes[15] == BodyMode::full
               && selected.modes[14] == BodyMode::full && selected.modes[13] == BodyMode::distant,
        "414ef0 ordinary branch must promote the closer follower before walking the forward ring");
    actors[1].forward_gap_q16 = 100;
    selected = select_fleet_body_modes(actors, context);
    expect(selected.modes[1] == BodyMode::distant && selected.modes[13] == BodyMode::full, "Equal source forward gaps must skip the follower promotion");
    context.alternate_direction = true;
    actors[0].forward_gap_q16 = 50;
    selected = select_fleet_body_modes(actors, context);
    expect(selected.full_count == 4 && selected.modes[15] == BodyMode::full && selected.modes[1] == BodyMode::full && selected.modes[2] == BodyMode::full
               && selected.modes[3] == BodyMode::distant,
        "414ef0 alternate branch must promote the forward neighbor before walking the backward ring");

    context.alternate_direction = false;
    actors[0].forward_gap_q16 = 100;
    actors[1].forward_gap_q16 = 50;
    actors[8].normal_up_q16 = 0xf332;
    actors[9].support_count = 3;
    actors[10].rollover.sideways = true;
    actors[11].previous = BodyMode::crashed;
    actors[11].rollover.upside_down = true;
    actors[12].previous = BodyMode::removed;
    actors[12].human_control = true;
    selected = select_fleet_body_modes(actors, context);
    expect(selected.full_count == 4 && selected.modes[8] == BodyMode::full && selected.modes[9] == BodyMode::full && selected.modes[1] == BodyMode::full
               && selected.modes[15] == BodyMode::distant && selected.modes[10] == BodyMode::crashing && selected.modes[11] == BodyMode::crashed
               && selected.modes[12] == BodyMode::removed,
        "Retained tilt and unsupported full bodies consume the budget before promotions; crash and removed modes keep source priority");
    actors[2].human_control = true;
    actors[3].first_tire_q16 = -1;
    selected = select_fleet_body_modes(actors, context);
    expect(selected.full_count == 5 && selected.modes[2] == BodyMode::full && selected.modes[3] == BodyMode::full && selected.modes[1] == BodyMode::distant,
        "Human, permanent-player and exceptional-tire force-full bodies may exceed the budget without another promotion");
    context.local_players = 2;
    selected = select_fleet_body_modes(actors, context);
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        expect(selected.modes[actor] == actors[actor].previous, "414e20 must leave every body mode unchanged for multiple local players");
    }
}

void physical_controls() {
    scene::BodyPose body;
    body.basis = {{{one, 0, 0}, {0, -one, 0}, {0, 0, -one}}};
    Vector angular{};
    RolloverState state;
    state.upside_down = state.sideways = true;
    expect(prepare_rollover(state, true, body, angular) && state.player_righting && !state.upside_down && state.sideways,
        "415090 player upside-down request must enable righting, preserve the side request and abort this dispatcher");
    expect(!prepare_rollover(state, true, body, angular) && !state.sideways && state.player_righting,
        "415090 player side request must enable righting while continuing the dispatcher");
    advance_player_righting(state, -one, -one, angular);
    expect(angular[0] == -0xccc, "40caf0 gradual player righting must use the original negative side-dependent angular velocity");
    advance_player_righting(state, 0xb333, one, angular);
    expect(angular[0] == 0xccc && state.player_righting, "Player righting must remain enabled strictly belowb334");
    advance_player_righting(state, 0xb334, one, angular);
    expect(!state.player_righting && angular[0] == 0xccc, "Player righting release must preserve angular velocity at the source threshold");
    state.pitched = true;
    body.basis[2][0] = -one;
    expect(!prepare_rollover(state, false, body, angular) && angular[2] == 0x2666 && !state.pitched,
        "The nonplayer fullbody pitch request must use the source2666 impulse and clear its counter");
}

void poses() {
    scene::BodyPose body;
    body.position = {7 * one, 10 * one, 9 * one};
    body.ground_normal = {0, one, 0};
    body.basis = {{{one, 0, 0}, {0, -one, 0}, {0, 0, -one}}};
    const auto half = scene::car_half_dimensions(0);
    RolloverState upside;
    upside.upside_down = true;
    settle_rollover_pose(body, 0, 2 * one, upside);
    expect(body.position == Vector{7 * one, 2 * one + half[1], 9 * one} && body.basis[1] == Vector{0, -one, 0},
        "445360 must rest the inverted roof at the original horizontal position, not right or relocate the body");
    for (const auto sign : {-1, 1}) {
        body.basis = {{{one, 0, 0}, {0, 0, -sign * one}, {0, sign * one, 0}}};
        RolloverState side;
        side.sideways = true;
        settle_rollover_pose(body, 0, 2 * one, side);
        auto ordinary = body;
        scene::refresh_body_contacts(ordinary, 0);
        expect(body.position[1] == 2 * one + half[2] && body.basis[2] == Vector{0, sign * one, 0},
            "445360 must preserve both side orientations and use the original half-width height");
        for (std::size_t corner = 0; corner < body.contacts.size(); ++corner) {
            const bool shifted = corner % 2 == (sign > 0 ? 0u : 1u);
            expect(body.contacts[corner][1] == ordinary.contacts[corner][1] - (shifted ? 0xcccc : 0), "445360 must lower only the original parity of side contact points bycccc");
        }
    }
}

void supported_motion() {
    const HpcSurface surface(plane());
    auto initial = scene::initial_body_pose(surface, {{0, 0}, 0, 0}, 0);
    // Drop an already inverted body through actual flight/landing so the
    // support history is physical, rather than injecting orientation flags.
    initial.position[1] += 5 * one;
    initial.basis[1] = scale(initial.basis[1], -one);
    initial.basis[2] = scale(initial.basis[2], -one);
    scene::refresh_body_contacts(initial, 0);
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    Motion motion(surface, initial, 0, false, 1, false, damage, roughness);
    unsigned supported = 0;
    for (unsigned tick = 0; tick < 160 && !motion.state().rollover.upside_down; ++tick) {
        expect(motion.prepare_body_update(false), "A nonplayer must not enter the player dispatcher abort branch");
        motion.update({}, {}, true);
        motion.finish_ground();
        damage.advance();
        expect(motion.state().boundary == MotionBoundary::none, "An actual roof contact interval must remain inside authored HPC coverage");
        supported += motion.state().contact_count > 0 ? 1u : 0u;
    }
    expect(motion.state().rollover.upside_down && supported >= 61 && motion.state().body.basis[1][1] < -0xf333,
        "Actual Motion roof contacts must naturally latch the source upside-down request without injecting flags or race outcomes");
    const auto before = motion.state();
    const auto engine = motion.controls().engine_speed_q16;
    motion.finish_ground(BodyMode::crashing);
    expect(motion.state().velocity_q16 == Vector{} && motion.state().angular_velocity_q16 == Vector{} && motion.state().wheel_speed_q16 == std::array<std::int32_t, 4>{},
        "445360 must clear actual translational/angular velocity and all four wheel speeds");
    expect(motion.state().body.position[0] == before.body.position[0] && motion.state().body.position[2] == before.body.position[2] && motion.state().body.basis[1][1] < 0
               && motion.controls().engine_speed_q16 == engine && motion.state().speed_q16 == before.speed_q16,
        "Wreck settling must preserve horizontal position, inverted orientation, RPM and the retained source speed record");
    const auto frozen = motion.state().body.position;
    motion.finish_ground(BodyMode::crashed);
    expect(motion.state().body.position == frozen, "Mode2 must remain physically frozen while visibility/lifecycle remains caller-owned");
}
} // namespace

int main() {
    try {
        thresholds();
        fleet_selection();
        physical_controls();
        poses();
        supported_motion();
        std::cout << "PASS: source rollover thresholds, physical righting, inverted/side settling and frozen wreck state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
