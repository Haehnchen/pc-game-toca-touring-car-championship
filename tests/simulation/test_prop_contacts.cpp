#include "assets/store.hpp"
#include "scene/resources.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/prop_contacts.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
using namespace toca::simulation;
using namespace toca::simulation::fixed;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

PropResources prop(Vector point, bool child = false) {
    PropResources result;
    ModelNode node{-1, {}, 0, {}};
    node.transform[0] = node.transform[5] = node.transform[10] = node.transform[15] = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        node.transform[12 + i] = float(point[i]) / one;
    }
    result.model.nodes.push_back(node);
    result.actors.push_back({0});
    if (child) {
        node.parent = 0;
        node.transform[12] = node.transform[13] = node.transform[14] = 0;
        result.model.nodes.push_back(node);
    }
    return result;
}

PropVehicle vehicle(Vector point, Vector velocity) {
    PropVehicle result;
    result.body.position = point;
    result.body.basis = {Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};
    result.velocity_q16 = velocity;
    return result;
}

Vector point(const HorizonModelFrame& frame) {
    const auto& matrix = frame.transforms.at(0);
    return {matrix[12], matrix[13], matrix[14]};
}

constexpr std::array<std::array<std::int32_t, 2>, 4> sectors{{{0, 0}, {64 * one, 0}, {0, -64 * one}, {64 * one, -64 * one}}};
const PropSurfaceQuery flat = [](auto, auto, auto) -> std::optional<HpcPropSample> {
    return HpcPropSample{0, {0, one, 0}, 0, 0};
};

std::vector<std::uint8_t> driving_floor() {
    // Same bounded128-unit square used by the actual wheel/contact fixtures.
    std::vector<std::uint8_t> bytes(106);
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        bytes.at(offset) = std::uint8_t(value);
        bytes.at(offset + 1) = std::uint8_t(value >> 8);
    };
    const auto put32 = [&](std::size_t offset, std::int32_t value) {
        put16(offset, std::uint16_t(value));
        put16(offset + 2, std::uint16_t(std::uint32_t(value) >> 16));
    };
    put32(0, -128 * one);
    put32(4, -128 * one);
    put16(8, 1);
    put16(20, 2);
    put16(24, 4);
    const std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * one, -64 * one}, {-64 * one, 64 * one}, {64 * one, -64 * one}, {64 * one, 64 * one}}};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        put32(28 + i * 12, vertices[i][0]);
        put32(32 + i * 12, 2 * one);
        put32(36 + i * 12, vertices[i][1]);
    }
    put16(76, 2);
    const std::array<std::array<std::uint16_t, 3>, 2> faces{{{0, 1, 2}, {2, 1, 3}}};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put16(80 + i * 8 + vertex * 2, faces[i][vertex]);
        }
    }
    put16(96, 2);
    put16(100, 1);
    put16(102, 1);
    put16(104, 1);
    return bytes;
}

void motion_feedback() {
    const HpcSurface floor(driving_floor());
    const auto body = initial_body_pose(floor, {{0, 0}, 0, 0}, 0);
    const auto post = prop(add(body.position, Vector{one, -0x8666, 0}));
    const PropResources empty;
    PropContacts contacts(empty, post, empty, sectors, floor);
    SurfaceRoughness reference_roughness, coupled_roughness;
    DamageContext reference_damage(floor), coupled_damage(floor);
    Motion reference(floor, body, 0, true, 1, true, reference_damage, reference_roughness);
    Motion coupled(floor, body, 0, true, 1, true, coupled_damage, coupled_roughness);
    const auto step = [&] {
        reference.update({}, {}, true);
        coupled.update({}, {}, true);
        contacts.advance();
        const auto response = contacts.contact({coupled.state().body, coupled.state().velocity_q16, 0, true});
        coupled.apply_prop_response(response);
        expect(coupled.state().prop_impact_q16 == response.impact_strength_q16, "A vehicle's actual prop response must reach its own current Motion feedback field");
        reference.finish_ground();
        coupled.finish_ground();
        return response;
    };
    const auto first = step();
    expect(coupled.state().boundary == MotionBoundary::none && contacts.frame().contacts == 1 && coupled.state().velocity_q16 == reference.state().velocity_q16
               && coupled.state().body.position == reference.state().body.position && first.impact_strength_q16 > 0 && coupled.state().prop_impact_q16 == first.impact_strength_q16,
        "Post contact must publish its current haptic event through ground correction without changing the current car tick's delayed force");
    step();
    expect(coupled.state().boundary == MotionBoundary::none && coupled.state().velocity_q16[0] < reference.state().velocity_q16[0] && coupled.state().prop_impact_q16 == 0,
        "Original post drag must enter the next integration while the one-tick haptic event is cleared instead of replayed");
    const auto pose = coupled.state().body.position;
    const auto updates = contacts.frame().updates;
    for (auto render = 0; render < 240; ++render) {
        (void)contacts.frame();
    }
    expect(contacts.frame().updates == updates && coupled.state().body.position == pose, "Presentation/pause reads must not advance prop or vehicle clocks");
    coupled.reset_for_race(body);
    expect(contacts.frame().updates == updates, "Resetting one Motion must not reset the scene-owned shared prop world");
    contacts.reset();
    expect(contacts.frame().updates == 0 && contacts.frame().post_states[0] == 0 && coupled.state().body.position == body.position,
        "Real qualifying-to-grid scene reset must reset prop contacts exactly once with the car");
}

void shared_fleet() {
    const PropResources empty;
    auto posts = prop({0, 0, 0});
    auto second = prop({20 * one, 0, 0}).model.nodes.front();
    posts.model.nodes.push_back(second);
    posts.actors.push_back({1});
    PropContacts contacts(empty, posts, empty, sectors, flat);
    std::array<PropVehicle, 16> actors;
    for (auto& actor : actors) {
        actor = vehicle({50 * one, 0, -50 * one}, {});
        actor.local_player = false;
    }
    actors[0] = vehicle({-one, 0x8666, 0}, {0x8000, 0, 0});
    actors[0].local_player = false;
    actors[15] = vehicle({19 * one, 0x8666, 0}, {0x8000, 0, 0});
    contacts.advance();
    const auto responses = contacts.contact(actors);
    expect(responses.size() == 16 && responses[0].force_q16 == Vector{-3277, 0, 0} && responses[15].force_q16 == Vector{-3277, 0, 0} && responses[0].impact_strength_q16 == 0
               && responses[15].impact_strength_q16 == one
               && std::ranges::all_of(std::span(responses).subspan(1, 14),
                   [](const auto& response) {
                       return response.contacts == 0 && response.impact_strength_q16 == 0;
                   }),
        "One shared dispatcher must return each actual actor's own post force without spreading it to other bodies");
    expect(contacts.frame().updates == 1 && contacts.frame().contacts == 2 && contacts.frame().sounds.size() == 2 && !contacts.frame().sounds[0].local_player
               && contacts.frame().sounds[1].local_player && contacts.frame().sounds[0].ordinal == 0 && contacts.frame().sounds[1].ordinal == 1,
        "Sixteen bodies must share one prop clock and ordered sound queue, retaining rival and human flags");

    // A later non-touching actor must not release a post still held by an
    // earlier actor.45b900 resets+134 and decides state1->6 once per fleet.
    for (auto tick = 0; tick < 8 && contacts.frame().post_states[0] != 1; ++tick) {
        contacts.advance();
    }
    expect(contacts.frame().post_states[0] == 1, "Weak post must reach its original held-bend state");
    (void)contacts.contact(actors);
    expect(contacts.frame().post_states[0] == 1 && contacts.frame().post_states[1] == 1,
        "A held post must remain bent when any fleet actor touches it, regardless of later non-touching actors");
    for (auto& actor : actors) {
        actor.body.position = {50 * one, 0, -50 * one};
    }
    (void)contacts.contact(actors);
    expect(contacts.frame().post_states[0] == 6 && contacts.frame().post_states[1] == 6,
        "Only a complete fleet pass without contact may release a held post into its ten-tick return wait");

    PropContacts signs(empty, empty, prop({0, 0, 0}, true), sectors, flat);
    actors[0] = vehicle({-one, one, 0}, {one, 0, 0});
    actors[0].local_player = false;
    actors[15] = actors[0];
    actors[15].local_player = true;
    signs.advance();
    const auto broken = signs.contact(actors);
    expect(broken[0].contacts == 1 && broken[15].contacts == 0 && broken[0].impact_strength_q16 == 0 && broken[15].impact_strength_q16 == 0 && signs.frame().breaks.size() == 1
               && signs.frame().signs.visible == std::vector<std::uint8_t>{0, 0},
        "A rival breaking a shared sign must not give the later local actor a haptic hit from its unrelated fragment/audio event");
    signs.reset();
    expect(signs.frame().updates == 0 && signs.frame().sign_states[0] == 0 && signs.frame().breaks.empty(),
        "A scene reset restores the one shared prop world and clears its event queue");

    PropContacts cones(prop({0, 0x75c2, 0}), empty, empty, sectors, flat);
    actors[0] = vehicle({-2 * one, 0x75c2, 0}, {0x8000, 0, 0});
    actors[0].local_player = false;
    actors[15] = vehicle({0x38000, 0x75c2, 0}, {-0x8000, 0, 0});
    cones.advance();
    const auto cone_hits = cones.contact(actors);
    expect(cone_hits[0].contacts == 1 && cone_hits[15].contacts == 1 && cone_hits[0].force_q16 == Vector{} && cone_hits[15].force_q16 == Vector{} && cones.frame().updates == 1
               && cone_hits[0].impact_strength_q16 == 0 && cone_hits[15].impact_strength_q16 > 0 && cones.frame().sounds.size() == 2 && !cones.frame().sounds[0].local_player
               && cones.frame().sounds[1].local_player,
        "Opposing rival and human bodies must push the same live cone in actor order without inventing a car impulse or advancing scenery twice");
}

void sector_dispatch() {
    const PropResources empty;
    constexpr auto half = one / 2;
    constexpr std::array<Vector, 4> positions{
        {{31 * one + half, 0, -31 * one - half}, {32 * one + half, 0, -31 * one - half}, {31 * one + half, 0, -32 * one - half}, {32 * one + half, 0, -32 * one - half}}};
    PropResources posts;
    // Reverse registration so observable sound order must follow sectors,
    // rather than the controller list: primary, horizontal, vertical, diagonal.
    for (std::size_t i = positions.size(); i-- > 0;) {
        posts.actors.push_back({std::uint32_t(posts.model.nodes.size())});
        posts.model.nodes.push_back(prop(positions[i]).model.nodes.front());
    }
    PropContacts corner(empty, posts, empty, sectors, flat);
    const auto hit = corner.contact(vehicle({31 * one + 3 * one / 4, 0x8666, -31 * one - 3 * one / 4}, {0x8000, 0, 0}));
    expect(hit.contacts == 4 && corner.frame().sounds.size() == 4, "A vehicle at an inner sector corner must contact props in all four candidate sectors exactly once");
    for (std::size_t i = 0; i < positions.size(); ++i) {
        expect(corner.frame().sounds[i].position_q16 == add(positions[i], Vector{0, 0x8666, 0}) && corner.frame().sounds[i].ordinal == i,
            "Four-sector contact audio must retain primary, horizontal, vertical and diagonal chronology independently of registration order");
    }

    PropContacts crossing(prop({31 * one + 3 * one / 4, 0x75c2, 0}), empty, empty, sectors, flat);
    const std::array<PropVehicle, 2> cars{{vehicle({31 * one + half, 0x75c2, 0}, {0x8000, 0, 0}), vehicle({36 * one + half, 0x75c2, 0}, {-0x8000, 0, 0})}};
    for (unsigned pass = 0; pass < 2; ++pass) {
        const auto responses = crossing.contact(cars);
        expect(responses.size() == 2 && responses[0].contacts == 1 && responses[1].contacts == 0 && point(crossing.frame().cones)[0] > 32 * one,
            "A moved cone must retain its entry sector for every vehicle in one shared dispatcher, including the strict4.5-unit neighbor boundary");
        expect(crossing.contact(cars[1]).contacts == 1, "The following dispatcher must refresh the cone sector so a vehicle querying only its new sector can hit it");
        if (pass == 0) {
            crossing.advance();
            crossing.reset();
        }
    }
}

void synthetic() {
    const PropResources empty;
    const auto cones = prop({0, 0x75c2, 0});
    PropContacts loose(cones, empty, empty, sectors, flat);
    loose.advance();
    loose.advance();
    expect(loose.frame().cone_states == std::vector<std::uint8_t>{2} && point(loose.frame().cones) == Vector{0, 0x75c2, 0},
        "A cone must settle upright with the source0x75c2 support height and sleeping state2");
    const auto pose = point(loose.frame().cones);
    loose.advance();
    expect(point(loose.frame().cones) == pose, "Sleeping cone state2 must retain its pose without gravity integration");
    const auto response = loose.contact(vehicle({-2 * one, 0x75c2, 0}, {0x8000, 0, 0}));
    expect(response.contacts == 1 && response.force_q16 == Vector{} && response.torque_q16 == Vector{} && response.impact_strength_q16 == one,
        "Cone contact must publish bounded haptics while moving only the prop, without inventing a vehicle impulse");
    expect(loose.frame().cone_states[0] == 1 && loose.frame().sounds.size() == 1 && loose.frame().sounds[0].event_kind == 6 && loose.frame().sounds[0].strength_q16 > 0,
        "Cone contact must emit original event6 and enter force integration state1");
    const auto hit_pose = point(loose.frame().cones);
    const auto before_flight = loose.frame().cones.transforms;
    loose.advance();
    expect(point(loose.frame().cones)[0] > hit_pose[0] && loose.frame().sounds.empty() && loose.frame().cones.transforms[0][5] != one,
        "Hit cone must integrate translation, source torque/tumble and clear prior tick events");
    const auto after_flight = loose.frame();
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            const auto rendered = loose.presentation_frame(alpha);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto expected = double(hit_pose[axis]) + double(point(after_flight.cones)[axis] - hit_pose[axis]) * alpha;
                expect(std::abs(double(point(rendered.cones)[axis]) - expected) <= 1, "Tumbling cone translation must expose60/144Hz intermediate poses");
            }
            expect(rendered.cone_states == after_flight.cone_states && rendered.updates == after_flight.updates && rendered.sounds.size() == after_flight.sounds.size(),
                "Prop presentation must retain current controller states, events and update count");
        }
    }
    expect(loose.presentation_frame(0).cones.transforms == before_flight && loose.presentation_frame(1).cones.transforms == after_flight.cones.transforms
               && loose.frame().cones.transforms == after_flight.cones.transforms,
        "Prop interpolation endpoints and repeated reads must preserve the exact source basis and current simulation geometry");
    loose.reset();
    expect(point(loose.frame().cones) == Vector{0, 0x75c2, 0} && loose.frame().cone_states[0] == 4 && loose.frame().updates == 0,
        "Grid reset must restore cone position, upright basis and source initialization state4");
    expect(loose.presentation_frame(0).cones.transforms == loose.frame().cones.transforms, "Grid reset must discard the cone's preceding flight pose");

    // Vehicle sector0 reaches across the authored64-unit boundary into sector1.
    const auto across = prop({33 * one, 0x75c2, 0});
    PropContacts boundary(across, empty, empty, sectors, flat);
    const auto boundary_hit = boundary.contact(vehicle({31 * one, 0x75c2, 0}, {0x8000, 0, 0}));
    expect(boundary_hit.contacts == 1, "Source4.5-unit sector margin must include an adjacent cone without a whole-track search");
    const auto crossing = prop({31 * one, 0x75c2, 0});
    PropContacts pushed(crossing, empty, empty, sectors, flat);
    const auto crossing_hit = pushed.contact(vehicle({30 * one, 0x75c2, 0}, {0x8000, 0, 0}));
    expect(crossing_hit.contacts == 1 && point(pushed.frame().cones)[0] > 32 * one && pushed.frame().sounds.size() == 1,
        "A cone pushed into a neighbor sector must keep its stored source candidate sector until the next dispatcher call");
    PropContacts distant(across, empty, empty, sectors, flat);
    expect(distant.contact(vehicle({0, 0x75c2, 0}, {one, 0, 0})).contacts == 0, "An unrelated terrain sector must not produce a prop contact");
    const auto near_sign = prop({31 * one, 0, 0});
    PropContacts ordered(across, empty, near_sign, sectors, flat);
    const auto ordered_hit = ordered.contact(vehicle({31 * one, one, 0}, {one, 0, 0}));
    expect(ordered_hit.contacts == 2 && ordered.frame().breaks.size() == 1 && ordered.frame().sounds.size() == 1 && ordered.frame().breaks[0].ordinal == 0
               && ordered.frame().sounds[0].ordinal == 1,
        "A primary-sector sign break must retain its audio position before the following neighbor-sector cone sound");

    const auto posts = prop({0, 0, 0});
    PropContacts flexible(empty, posts, empty, sectors, flat);
    const auto bump = flexible.contact(vehicle({-one, 0x8666, 0}, {0x8000, 0, 0}));
    expect(bump.contacts == 1 && bump.force_q16 == Vector{-3277, 0, 0} && bump.torque_q16[2] != 0,
        "First post contact must retain source velocity*-0x1999 vehicle drag and local offset cross drag");
    expect(flexible.frame().post_states[0] == 4 && flexible.frame().sounds[0].event_kind == 0x12, "Weak post contact must initialize source25-degree bend and event18");
    PropContacts bending(empty, posts, empty, sectors, flat);
    bending.contact(vehicle({-one, 0x8666, 0}, {0x8000, 0, 0}));
    const auto upright = bending.frame().posts.transforms;
    bending.advance();
    expect(bending.frame().post_states[0] == 3 && bending.frame().posts.transforms == upright && bending.presentation_frame(.5f).posts.transforms == upright,
        "The first post update prepares its source rotation without moving the pending bend pose");
    bending.advance();
    const auto bent = bending.frame().posts.transforms;
    const auto half_bend = bending.presentation_frame(.5f).posts.transforms;
    expect(bent != upright && half_bend != upright && half_bend != bent, "A bending post must rotate through an intermediate rigid presentation pose");
    const auto again = flexible.contact(vehicle({-one, 0x8666, 0}, {0x8000, 0, 0}));
    expect(bump.impact_strength_q16 == one && again.contacts == 1 && again.force_q16 == Vector{} && again.impact_strength_q16 == 0,
        "Post pending bend state4 must suppress duplicate first-hit force and haptic events");
    bool returning = false, restored = false, played_return = false;
    for (auto i = 0; i < 100; ++i) {
        flexible.advance();
        played_return = played_return || std::ranges::any_of(flexible.frame().sounds, [](const auto& event) {
            return event.event_kind == 0x13;
        });
        flexible.contact(vehicle({20 * one, 0, 0}, {}));
        const auto state = flexible.frame().post_states[0];
        returning = returning || state == 6 || state == 2;
        if (returning && state == 0) {
            restored = true;
            break;
        }
    }
    expect(returning && restored && played_return && flexible.frame().posts.transforms[0][5] == one,
        "Released weak post must wait10 ticks, emit event19, rotate back and restore its authored matrix");
    PropContacts broken_post(empty, posts, empty, sectors, flat);
    broken_post.contact(vehicle({-one, 0x8666, 0}, {2 * one, 0, 0}));
    expect(broken_post.frame().sounds[0].event_kind == 0x14, "A post hit above source horizontal speed1 must use event20");
    for (auto i = 0; i < 10; ++i) {
        broken_post.advance();
        broken_post.contact(vehicle({20 * one, 0, 0}, {}));
    }
    expect(broken_post.frame().post_states[0] == 5, "Strongly hit post must remain down in source terminal state5");

    const auto signs = prop({0, 0, 0}, true);
    PropContacts light_sign(empty, empty, signs, sectors, flat);
    const auto tip = light_sign.contact(vehicle({-one, one, 0}, {0x4000, 0, 0}));
    // The source square-root lookup quantizes length even for this axial
    // vector. Compare the actual shared speed*2 result, not ideal Euclidean0.5.
    expect(light_sign.frame().sign_states[0] == 3 && light_sign.frame().sounds[0].event_kind == 0x12 && !light_sign.frame().sounds[0].local_player && tip.impact_strength_q16 > 0
               && tip.impact_strength_q16 <= one && tip.impact_strength_q16 == light_sign.frame().sounds[0].strength_q16,
        "A weak local sign hit must provide speed*2 haptics despite its original false audio local flag");
    const auto sign_pose = light_sign.frame().signs.transforms;
    light_sign.advance();
    expect(light_sign.frame().signs.transforms != sign_pose && light_sign.frame().breaks.empty(), "Light sign contact must advance the actual hinge matrix without fragments");
    auto paired_signs = signs;
    paired_signs.model.nodes.push_back(signs.model.nodes[0]);
    auto second_child = signs.model.nodes[1];
    second_child.parent = 2;
    paired_signs.model.nodes.push_back(second_child);
    paired_signs.actors.push_back({2});
    PropContacts paired(empty, empty, paired_signs, sectors, flat);
    paired.contact(vehicle({-one, one, 0}, {0x4000, 0, 0}));
    expect(paired.frame().sounds.size() == 2 && !paired.frame().sounds[0].local_player && paired.frame().sounds[1].local_player,
        "Source sign local_8 must survive candidate iteration after the first small contact assigns it");
    PropContacts shattered(empty, empty, signs, sectors, flat);
    const auto smash = shattered.contact(vehicle({-one, one, 0}, {one, 0, 0}));
    expect(smash.contacts == 1 && smash.impact_strength_q16 == one && shattered.frame().breaks.size() == 1 && shattered.frame().sounds.empty()
               && shattered.frame().signs.visible == std::vector<std::uint8_t>{0, 0},
        "Fast local sign hit must supply full haptics before optional fragment allocation, without inventing an extra source audio event");
    const auto consumed = shattered.contact(vehicle({-one, one, 0}, {one, 0, 0}));
    expect(shattered.frame().breaks.size() == 1 && consumed.impact_strength_q16 == 0, "A broken sign must not trigger fragment allocation or a haptic event twice");
    shattered.reset();
    expect(shattered.frame().signs.visible == std::vector<std::uint8_t>{1, 1} && shattered.frame().sign_states[0] == 0,
        "Grid restart must restore hidden signs and their collision eligibility");
    motion_feedback();
    shared_fleet();
    sector_dispatch();
    std::cout << "PASS: original prop candidates, cone tumble, vehicle/post impulses and return, sign tipping/break events\n";
}

void low_gravity() {
    const PropResources empty;
    const auto airborne = prop({0, 10 * one, 0});
    PropContacts falling(airborne, empty, empty, sectors, flat), floating(airborne, empty, empty, sectors, flat, true);
    for (std::int32_t tick = 1; tick <= 3; ++tick) {
        falling.advance();
        floating.advance();
        expect(point(falling.frame().cones)[1] == 10 * one - 0x1999 * tick * (tick + 1) / 2 && point(floating.frame().cones)[1] == 10 * one - 0x28f * tick * (tick + 1) / 2,
            "458400/458ac0 CMLOGRAV must reduce airborne cone gravity from0x1999 to0x28f before each translation");
    }
    const auto cones = prop({0, 0x75c2, 0});
    PropContacts normal(cones, empty, empty, sectors, flat), light(cones, empty, empty, sectors, flat, true);
    normal.advance();
    light.advance();
    const auto car = vehicle({-2 * one, 0x75c2, 0}, {0x1000, 0, 0});
    const auto normal_hit = normal.contact(car), light_hit = light.contact(car);
    const auto before = point(normal.frame().cones);
    expect(before == point(light.frame().cones) && normal_hit.contacts == 1 && light_hit.contacts == 1 && normal_hit.impact_strength_q16 == light_hit.impact_strength_q16
               && normal_hit.force_q16 == light_hit.force_q16 && normal.frame().sounds[0].strength_q16 == light.frame().sounds[0].strength_q16,
        "CMLOGRAV cone response must preserve car impulses, contact resolution, haptics and sound strength");
    normal.advance();
    light.advance();
    const auto direction = normalized(Vector{2 * one, 0, 0});
    const auto approach = dot(car.velocity_q16, direction);
    const auto travel = [&](std::int32_t multiplier) {
        return mul(mul(mul(mul(mul(approach, multiplier), 0xa0000), direction[0]), 0x1999), 0x3333);
    };
    expect(point(normal.frame().cones)[0] - before[0] == travel(0x40000) && point(light.frame().cones)[0] - before[0] == travel(0x60000),
        "458400/45c6c0 CMLOGRAV must raise cone contact impulse multiplier from0x40000 to0x60000");
    expect(normal.frame().cones.transforms[0][5] == one && light.frame().cones.transforms[0][5] != one,
        "A0x1000 approach must cross only CMLOGRAV's0x51e tumbling threshold, retaining the ordinary0x3333 threshold");
}

void source_state_import() {
    PropContacts contacts(prop({0, 0, 0}), prop({20 * one, 0, 0}), prop({30 * one, 0, 0}, true), sectors, flat);
    std::vector<std::uint8_t> cone(0xcc), post(0x13c), sign(0x40);
    std::array<std::array<std::uint8_t, 68>, 1> models{};
    const auto put = [](auto& bytes, std::size_t offset, std::int32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] = std::uint8_t(std::uint32_t(value) >> (byte * 8));
        }
    };
    const auto vector = [&](auto& bytes, std::size_t offset, Vector value) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put(bytes, offset + axis * 4, value[axis]);
        }
    };
    // 458ac0 warm cone: pending hit force, horizontal drift and airborne
    // gravity must continue from the observed body rather than a reset cone.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        put(cone, 0x48 + axis * 16, one);
    }
    vector(cone, 0x6c, {5 * one, 10 * one, 0});
    vector(cone, 0x78, {0, one, 0});
    vector(cone, 0x84, {0x2000, 0, 0});
    vector(cone, 0x9c, {one, 0, 0});
    cone[0xc0] = 1;
    put(cone, 0xc4, 1);
    // 45a460 state6 keeps its captured remaining wait and both endpoints.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        put(post, 0xc8 + axis * 16, one);
        put(post, 0x48 + axis * 20, one);
    }
    vector(post, 0xec, {20 * one, 0, 0});
    vector(post, 0x104, {0, one, 0});
    vector(post, 0x114, {20 * one, 0x8666, 0});
    vector(post, 0x120, {20 * one, 0x8666, 0});
    put(post, 0x12c, 1);
    post[0x130] = 6;
    // 45b010 reads its live float model; the compact sign record alone does
    // not contain this translated model or distinguish hidden state1.
    for (std::size_t component = 0; component < 16; ++component) {
        const auto value = component % 5 == 0 ? 1.0f : component == 12 ? 30.0f : 0.0f;
        put(models[0], component * 4, std::bit_cast<std::int32_t>(value));
    }
    put(models[0], 64, 0xf);
    vector(sign, 8, {31 * one, one, 0});
    vector(sign, 0x14, {29 * one, one, 0});
    vector(sign, 0x20, {0, one, 0});
    put(sign, 0x2c, 10);
    vector(sign, 0x30, {30 * one, one, 0});
    put(sign, 0x3c, 3);
    // Runtime pointer words are intentionally unusable; import only copies
    // numeric source state into the registered native roots.
    put(cone, 0, -1);
    put(post, 0, -1);
    put(sign, 0, -1);
    contacts.restore_source_state(cone, post, sign, models);
    expect(contacts.presentation_frame(0).cones.transforms == contacts.frame().cones.transforms
               && contacts.presentation_frame(.5f).signs.transforms == contacts.frame().signs.transforms,
        "Imported prop state must initialize presentation at the imported geometry");
    expect(contacts.frame().updates == 0 && contacts.frame().cone_states[0] == 1 && contacts.frame().post_states[0] == 6 && contacts.frame().sign_states[0] == 3
               && point(contacts.frame().cones) == Vector{5 * one, 10 * one, 0} && point(contacts.frame().signs) == Vector{30 * one, 0, 0},
        "Source import must restore every registered controller without advancing its shared clock or dereferencing captured pointers");
    contacts.advance();
    // Positive increment initially rotates Z below the up normal;45b010's
    // dot>=0 stop has not fired. The source pivot alias still shifts X.
    expect(point(contacts.frame().cones) == Vector{5 * one + 0x2000 + mul(mul(one, 0x1999), 0x3333), 10 * one - 0x1999, 0} && contacts.frame().cone_states[0] == 0
               && contacts.frame().post_states[0] == 6 && contacts.frame().sign_states[0] == 3 && point(contacts.frame().signs) == Vector{30 * one - 0xccc, one, 0},
        "Imported warm cone impulse, post wait and live sign pivot must follow their next original controller branches");
    contacts.advance();
    expect(contacts.frame().post_states[0] == 2 && contacts.frame().sounds.size() == 1 && contacts.frame().sounds[0].event_kind == 0x13,
        "Imported post wait must expire on its actual next tick and emit the source return sound once");
    put(sign, 0x3c, 1);
    put(models[0], 64, 0);
    contacts.restore_source_state(cone, post, sign, models);
    expect(contacts.frame().signs.visible == std::vector<std::uint8_t>{0, 0} && contacts.frame().sounds.empty() && contacts.frame().breaks.empty(),
        "Imported broken sign root mask must hide its whole native tree without replaying old events");
    const auto before = contacts.frame().cones.transforms;
    auto invalid = models;
    put(invalid[0], 0, std::bit_cast<std::int32_t>(std::numeric_limits<float>::quiet_NaN()));
    bool rejected = false;
    try {
        contacts.restore_source_state(cone, post, sign, invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected && contacts.frame().cones.transforms == before && contacts.frame().signs.visible == std::vector<std::uint8_t>{0, 0},
        "Malformed live model import must fail before replacing any part of the shared world");
}

void reference(const std::filesystem::path& root) {
    const AssetStore store(root);
    for (std::uint32_t track = 0; track < 10; ++track) {
        const auto resources = load_scene_resources(store, resolve_setup({track, 0, 1, 3}));
        const HpcSurface surface(resources.collision_hpc);
        PropContacts contacts(resources.cones, resources.posts, resources.signs, resources.ground.sector_centers, surface);
        expect(contacts.frame().cone_states.size() == resources.cones.actors.size() && contacts.frame().post_states.size() == resources.posts.actors.size()
                   && contacts.frame().sign_states.size() == resources.signs.actors.size(),
            "Canonical registered prop controllers must survive original source construction");
        for (auto i = 0; i < 4; ++i) {
            contacts.advance();
        }
        expect(contacts.frame().updates == 4, "Canonical props must advance independently of rendering");
        contacts.reset();
        expect(contacts.frame().updates == 0
                   && std::ranges::all_of(contacts.frame().sign_states,
                       [](auto state) {
                           return state == 0;
                       }),
            "Canonical new-grid reset must restore original sign eligibility");
        if (track != 0 && track != 3 && track != 6 && track != 9) {
            continue;
        }
        const PropResources empty;
        for (const auto kind : {PropKind::cones, PropKind::posts, PropKind::signs}) {
            auto selected = kind == PropKind::cones ? resources.cones : kind == PropKind::posts ? resources.posts : resources.signs;
            if (selected.actors.empty()) {
                continue;
            }
            selected.actors.resize(1);
            const auto& matrix = selected.model.nodes.at(selected.actors.front().root_node).transform;
            Vector center{};
            for (std::size_t i = 0; i < 3; ++i) {
                center[i] = std::int32_t(std::nearbyint(double(matrix[12 + i]) * one));
            }
            if (kind == PropKind::posts) {
                center[1] = add(center[1], 0x8666);
            } else if (kind == PropKind::signs) {
                center[1] = add(center[1], one);
            }
            PropContacts isolated(kind == PropKind::cones ? selected : empty, kind == PropKind::posts ? selected : empty, kind == PropKind::signs ? selected : empty,
                resources.ground.sector_centers, surface);
            const auto hit = isolated.contact(vehicle(sub(center, Vector{one, 0, 0}), {one, 0, 0}));
            if (hit.contacts != 1) {
                throw std::runtime_error("Actual canonical prop contact failed: track " + std::to_string(track) + " kind " + std::to_string(int(kind)) + " root "
                                         + std::to_string(selected.actors.front().root_node) + " at " + std::to_string(center[0]) + "," + std::to_string(center[1]) + ","
                                         + std::to_string(center[2]));
            }
            expect((kind == PropKind::signs ? isolated.frame().breaks.size() == 1 : !isolated.frame().sounds.empty()) && hit.impact_strength_q16 == one,
                "Canonical contacts must emit their originating sound/fragment event and an independent bounded local haptic response");
            if (track == 0 && kind == PropKind::posts) {
                isolated.reset();
                std::array<PropVehicle, 16> fleet;
                for (auto& actor : fleet) {
                    actor = vehicle(add(center, Vector{100 * one, 0, 0}), {});
                    actor.local_player = false;
                }
                fleet[0] = vehicle(sub(center, Vector{one, 0, 0}), {one, 0, 0});
                fleet[0].local_player = false;
                fleet[15] = fleet[0];
                fleet[15].local_player = true;
                isolated.advance();
                const auto responses = isolated.contact(fleet);
                expect(isolated.frame().updates == 1 && responses[0].contacts == 1 && responses[0].force_q16 != Vector{} && responses[15].contacts == 1
                           && responses[15].force_q16 == Vector{} && isolated.frame().sounds.size() == 1 && !isolated.frame().sounds[0].local_player,
                    "Canonical Donington post must share its first-hit state and original rival drag/sound across all sixteen registered bodies");
            }
        }
    }
    std::cout << "PASS: canonical prop controller construction, unit-normal ground queries, updates and grid reset\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            low_gravity();
            source_state_import();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Expected optional canonical data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
