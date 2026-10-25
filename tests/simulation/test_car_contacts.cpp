#include "simulation/car_contacts.hpp"
#include "simulation/fixed.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;

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

void restore(Motion& motion, Vector position, Vector velocity, Vector angular = {}, std::uint32_t car_id = 0, Vector delayed_force = {}, Vector delayed_moment = {}) {
    scene::BodyPose body;
    body.position = position;
    body.basis = {Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};
    scene::refresh_body_contacts(body, car_id);
    // Synthetic415090 record seeds an actual Motion body. The production
    // contact pass then owns all collision discovery, shifts and pending forces.
    SourceSnapshot snapshot;
    const auto vector = [&](std::size_t offset, Vector values) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            word(snapshot.body, offset + axis * 4, values[axis]);
        }
    };
    vector(0xbc, position);
    vector(0x268, position);
    vector(0xc8, velocity);
    vector(0xe4, angular);
    vector(0x1e4, {0, one, 0});
    vector(0x1f0, delayed_force);
    vector(0x1fc, delayed_moment);
    for (std::size_t i = 0; i < 3; ++i) {
        vector(0x150 + i * 12, body.basis[i]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        vector(0xf0 + i * 12, body.contacts[i]);
        snapshot.body[0x517 + i] = 1;
    }
    word(snapshot.dynamics, 0x27c, 1);
    motion.restore_source_snapshot(snapshot);
}

std::unique_ptr<Motion> car(
    const HpcSurface& surface, DamageContext& damage, SurfaceRoughness& roughness, Vector position, Vector velocity, Vector angular = {}, std::uint32_t car_id = 0) {
    auto result = std::make_unique<Motion>(surface, scene::BodyPose{}, car_id, true, 1, false, damage, roughness);
    restore(*result, position, velocity, angular, car_id);
    return result;
}

ContactCandidateGroups group(std::size_t count) {
    ContactCandidateGroups groups;
    groups.count = 1;
    groups.groups[0].sector = 0;
    groups.groups[0].count = std::uint8_t(count);
    for (std::size_t i = 0; i < count; ++i) {
        groups.groups[0].actors[i] = std::uint8_t(i);
    }
    return groups;
}

std::vector<CarContact> one_pass(std::span<const CarContactActor> actors) {
    CarContacts contacts;
    return contacts.resolve(actors, group(actors.size()));
}

void no_hits() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {4 * one, 0, 0});
    auto second = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {-4 * one, 0, 0});
    const std::array<CarContactActor, 2> actors{{{first.get(), 0, true, true, false, true}, {second.get(), 0, true, true, true, true}}};
    CarContacts resolver;
    const auto first_position = first->state().body.position, second_position = second->state().body.position;
    const auto cycle = damage.cycle();
    expect(resolver.resolve(actors, group(2), {.scrape_handle = {}, .enabled = false}).empty() && first->state().body.position == first_position
               && second->state().body.position == second_position && first->source_pending_acceleration() == Vector{} && second->source_pending_acceleration() == Vector{}
               && first->pending_car_moment() == Vector{} && second->pending_car_moment() == Vector{} && damage.cycle() == cycle && first->damage().levels == DamageState{}.levels
               && second->damage().levels == DamageState{}.levels && resolver.effects().audio.empty() && resolver.effects().fragments.empty() && resolver.effects().sparks.empty(),
        "CMNOHITS must skip actual overlapping-car separation, forces, damage and effects together");
    // Each struck side has five parts:419ad0 advances once on entry and
    // five times for selection. Both six-step visits wrap the shared cycle.
    expect(resolver.resolve(actors, group(2)).size() == 1 && first->source_pending_acceleration() != Vector{} && second->source_pending_acceleration() != Vector{}
               && first->damage().levels != DamageState{}.levels && second->damage().levels != DamageState{}.levels
               && std::ranges::any_of(resolver.effects().audio,
                   [](const auto& event) {
                       return event.action == CarContactAudioAction::impact;
                   }),
        "Turning car collisions on must restore the ordinary physical and audiovisual contact pass");
    const auto force = first->source_pending_acceleration(), moment = first->pending_car_moment();
    expect(resolver.resolve(actors, group(2), {.scrape_handle = {}, .enabled = false}).empty() && resolver.effects().audio.empty() && resolver.effects().fragments.empty()
               && resolver.effects().sparks.empty() && first->source_pending_acceleration() == force && first->pending_car_moment() == moment,
        "Skipped car dispatch must clear stale published effects while preserving forces accumulated by other passes");
}

void pile_up() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto a = car(surface, damage, roughness, {0, 5 * one, 0}, {one, 0, 0});
    auto b = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {one, 0, 0});
    const auto first_pair = one_pass(std::array<CarContactActor, 2>{{{a.get(), 0}, {b.get(), 0}}});
    expect(first_pair.size() == 1, "Two overlapping moving cars must produce an actual contact");
    const auto pair_gap = sub(b->state().body.position, a->state().body.position);
    expect(pair_gap[0] > 4 * one, "The collision must separate the original overlapping boxes");

    a = car(surface, damage, roughness, {0, 5 * one, 0}, {one, 0, 0});
    b = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {one, 0, 0});
    auto c = car(surface, damage, roughness, {8 * one, 5 * one, 0}, {one, 0, 0});
    const auto a_before = a->state().body;
    const std::array<CarContactActor, 3> fleet{{{a.get(), 0}, {b.get(), 0}, {c.get(), 0}}};
    const auto contacts = one_pass(fleet);
    expect(contacts.size() == 2 && contacts[0].first == 0 && contacts[0].second == 1 && contacts[1].first == 1 && contacts[1].second == 2,
        "A three-car pile-up must discover the two adjacent pairs in stable actor order");
    expect(sub(b->state().body.position, a->state().body.position) == pair_gap,
        "Moving the middle car away from the third must also move its already-contacted first neighbor, preserving their resolved gap");
    const auto shift = sub(a->state().body.position, a_before.position);
    expect(shift[0] < 0 && a->state().previous_position_q16 == a_before.position, "Pile-up correction must preserve the previous integration position");
    for (std::size_t i = 0; i < 8; ++i) {
        expect(a->state().body.contacts[i] == add(a_before.contacts[i], shift), "Propagated corrections must translate every retained corner by the same amount");
    }
    expect(a->state().body.object_to_world[12] == float(a->state().body.position[0]) / one, "Rendering must receive the propagated collision position");

    a->shift_car_contact({-20 * one, 0, 0});
    c->shift_car_contact({-one, 0, 0});
    const auto separated = a->state().body.position;
    const auto next = one_pass(fleet);
    expect(next.size() == 1 && next.front().first == 1 && next.front().second == 2 && a->state().body.position == separated,
        "A previous update's neighbor list must not drag a now-disconnected car into a new contact");
}

void secondary_moment() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    for (const bool opposed : {false, true}) {
        auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {2 * one, 0, 0}, {0, 0, opposed ? one : 0});
        auto second = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {});
        if (opposed) {
            first->add_car_contact({}, {0, 0, -1});
        }
        const auto contacts = one_pass(std::array<CarContactActor, 2>{{{first.get(), 0}, {second.get(), 0}}});
        expect(contacts.size() == 1 && contacts.front().strength_q16 > 0, "The torque fixture requires an actual strong moving impact");
        if (opposed) {
            expect(first->pending_car_moment() == Vector{0, 0, -1}, "An accumulated moment opposing angular velocity must reject the first actor's new torque");
            expect(second->pending_car_moment() == Vector{}, "The secondary actor must use ordinary arm torque when the first actor's retained moment is below0x5999");
        } else {
            expect(first->pending_car_moment()[2] == 2 * one && second->pending_car_moment()[2] == -2 * one,
                "A strong eligible impact must apply the source center-of-mass moment and clamp both actors to2 per component");
        }
    }
}

void delayed_moment_integration() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto live = car(surface, damage, roughness, {0, 5 * one, 0}, {2 * one, 0, 0});
    auto other = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {});
    expect(one_pass(std::array<CarContactActor, 2>{{{live.get(), 0}, {other.get(), 0}}}).size() == 1 && live->pending_car_moment() != Vector{},
        "Delayed torque regression requires a real discovered vehicle contact with a nonzero pending moment");
    auto imported = car(surface, damage, roughness, live->state().body.position, live->state().velocity_q16);
    restore(*imported, live->state().body.position, live->state().velocity_q16, live->state().angular_velocity_q16, 0, live->source_pending_acceleration(),
        live->source_pending_moment());
    live->update_source_input({}, {}, true);
    imported->update_source_input({}, {}, true);
    expect(live->state().angular_velocity_q16 == imported->state().angular_velocity_q16 && live->state().body.basis == imported->state().body.basis
               && live->state().velocity_q16 == imported->state().velocity_q16 && live->state().body.position == imported->state().body.position,
        "A real prior-tick car moment must execute the same40caf0 mass scaling as the identical raw1fc source snapshot, rather than bypassing it");
}

void glancing_audio() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {0, 0, one / 4});
    auto second = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {});
    const auto contacts = one_pass(std::array<CarContactActor, 2>{{{first.get(), 0}, {second.get(), 0}}});
    expect(contacts.size() == 1 && contacts.front().strength_q16 > 0, "Glancing contact sound strength must retain tangential relative speed even with zero normal impulse");
    expect(first->pending_car_moment() == Vector{} && second->pending_car_moment() == Vector{}, "A glancing contact must not invent a normal collision torque for its sound");
}

void removed_body() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 0, 0}, {one, 0, 0});
    auto second = car(surface, damage, roughness, {one, 0, 0}, {});
    const auto before = first->state().body.position;
    const auto contacts = one_pass(std::array<CarContactActor, 2>{{{first.get(), 0}, {second.get(), 0, false}}});
    expect(contacts.empty() && first->state().body.position == before && first->pending_car_moment() == Vector{},
        "42bc70 must exclude mode4 before pair correction, delayed forces and collision audio");
}

void contact_effects() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {2 * one, 0, 0});
    auto second = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {});
    first->set_actor_identity(0, false);
    second->set_actor_identity(1, true);
    first->set_damage_feedback_enabled(true);
    second->set_damage_feedback_enabled(false);
    const std::array<CarContactActor, 2> actors{{{first.get(), 0, true, true, false, true}, {second.get(), 0, true, true, true, true}}};
    CarContacts resolver;
    expect(resolver.resolve(actors, group(2)).size() == 1, "Effects fixture must discover a real overlapping strong impact");
    expect(
        resolver.source_touching()[1] && resolver.source_touching()[16], "428b50 must publish both directed touching words so either actor's403dd0 traffic row sees the contact");
    const auto& effects = resolver.effects();
    expect(effects.audio.size() == 3 && effects.audio[0].action == CarContactAudioAction::reset_pair && effects.audio[1].event_kind == 4 && effects.audio[2].event_kind == 1
               && effects.audio[1].local_player && effects.audio[2].local_player,
        "428b50 must run first-car damage before second-car damage and normalize two glass breaks into fracture4 followed by contact1");
    expect(std::ranges::any_of(first->damage().levels,
               [](auto level) {
                   return level != 0;
               })
               && std::ranges::any_of(second->damage().levels,
                   [](auto level) {
                       return level != 0;
                   })
               && first->damage().feedback_strength != 0 && second->damage().feedback_strength == 0,
        "Both full bodies receive mesh damage while only an assigned sound slot enables419ad0 feedback, independently of human identity");
    expect(effects.fragments.size() == 1 && effects.fragments[0].velocity == Vector{one / 2, 0, 0} && effects.fragments[0].intensity_q16 == one,
        "A visible initial impact must queue one shared fragment burst using quarter-sum velocity and the final first damage strength");
    const auto cycle = damage.cycle();
    restore(*first, {0, 5 * one, 0}, {2 * one, 0, 0});
    restore(*second, {4 * one, 5 * one, 0}, {});
    expect(resolver.resolve(actors, group(2)).size() == 1 && resolver.effects().audio.empty() && resolver.effects().fragments.empty() && damage.cycle() == cycle,
        "A continued longitudinal contact must retain pair history and skip damage, scrape sound and new impact fragments");
    (void)resolver.resolve(actors, {});
    expect(resolver.source_touching()[1] && resolver.source_touching()[16], "A pair omitted from this update's registration groups must retain both actors' touching words");
    restore(*first, {-20 * one, 5 * one, 0}, {one, 0, 0});
    const CarContactOptions allocated{0, 0, [](auto, auto) {
                                          return true;
                                      }};
    expect(resolver.resolve(actors, group(2), allocated).empty() && resolver.effects().audio.size() == 1 && resolver.effects().audio[0].action == CarContactAudioAction::scrape_stop
               && resolver.effects().audio[0].event_kind == 12,
        "An attempted failed overlap must clear pair history and stop its allocated scrape slot");
    expect(!resolver.source_touching()[1] && !resolver.source_touching()[16],
        "428aa0 must clear both directed traffic words after an attempted miss, independently of the scrape handle");
    restore(*first, {0, 5 * one, 0}, {2 * one, 0, 0});
    (void)resolver.resolve(actors, group(2));
    expect(resolver.effects().audio.size() >= 2 && resolver.effects().audio[0].action == CarContactAudioAction::reset_pair
               && resolver.effects().audio[1].action == CarContactAudioAction::impact,
        "Physical recontact must become a fresh damage call and reset only the pair's scrape mapping");
}

void continued_scrape() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
    auto second = car(surface, damage, roughness, {0, 5 * one, one}, {});
    std::array<CarContactActor, 2> actors{{{first.get(), 2, true, true, false, true}, {second.get(), 0, true, true, true, true}}};
    CarContacts resolver;
    (void)resolver.resolve(actors, group(2));
    expect(resolver.effects().fragments.size() == 1 && resolver.effects().fragments[0].car_ids == std::array<std::uint8_t, 2>{11, 0},
        "A nonhuman Type2 source body must use palette11 while the human retains its actual car palette");
    const auto cycle = damage.cycle();
    for (const bool handle : {false, true}) {
        restore(*first, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
        restore(*second, {0, 5 * one, one}, {});
        const CarContactOptions options{0, 0, [handle](auto, auto) {
                                            return handle;
                                        }};
        (void)resolver.resolve(actors, group(2), options);
        const auto& effects = resolver.effects();
        expect(effects.audio.size() == 1 && effects.audio[0].event_kind == (handle ? 11u : 10u) && effects.audio[0].local_player && effects.sparks.size() == 1
                   && effects.sparks[0].velocity == Vector{one / 2, 0, one / 8} && effects.sparks[0].intensity_q16 == 65522 && effects.fragments.empty() && damage.cycle() == cycle,
            "Continued side contact must use allocated-handle10/11 semantics and source tangential sparks without new damage");
    }
    restore(*first, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
    restore(*second, {0, 5 * one, one}, {});
    (void)resolver.resolve(actors, group(2), {0, 10, {}});
    expect(resolver.effects().sparks.empty(), "Earlier wall requests must occupy the original ten-slot shared spark queue");
    // Omitted groups retain touching history, just as registration and
    //stationary-pair skips do; only an attempted miss can clear it.
    (void)resolver.resolve(actors, {});
    restore(*first, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
    restore(*second, {0, 5 * one, one}, {});
    (void)resolver.resolve(actors, group(2));
    expect(resolver.effects().fragments.empty() && damage.cycle() == cycle, "A pair omitted by sector registration must preserve continued-contact state");
    //427170's cockpit child mask must suppress only the visual request.
    actors[1].visible = false;
    restore(*first, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
    restore(*second, {0, 5 * one, one}, {});
    expect(resolver.resolve(actors, group(2)).size() == 1 && resolver.effects().sparks.empty() && resolver.effects().audio.size() == 1,
        "A hidden high-detail child must retain physical scrape and audio while suppressing sparks");
    resolver.reset();
    restore(*first, {0, 5 * one, 0}, {one, 0, one / 4}, {}, 2);
    restore(*second, {0, 5 * one, one}, {});
    expect(resolver.resolve(actors, group(2)).size() == 1 && resolver.effects().fragments.empty() && resolver.effects().audio.size() > 1,
        "A hidden high-detail child must retain fresh impact damage/audio while suppressing fragments");
}

void full_body_damage_gate() {
    const auto surface = floor();
    for (const bool second_full : {false, true}) {
        SurfaceRoughness roughness;
        DamageContext damage(surface);
        auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {2 * one, 0, 0});
        auto second = car(surface, damage, roughness, {4 * one, 5 * one, 0}, {});
        const std::array<CarContactActor, 2> actors{{{first.get(), 0, true, false, false, true}, {second.get(), 0, true, second_full, true, true}}};
        CarContacts resolver;
        expect(resolver.resolve(actors, group(2)).size() == 1 && first->damage().levels == DamageState{}.levels,
            "A distant or crash body still resolves physical contact but must skip419ad0 and visual mesh changes");
        expect(second_full ? second->damage().levels != DamageState{}.levels : second->damage().levels == DamageState{}.levels,
            "The second actor's full-body eligibility must independently control its original damage call");
        if (!second_full) {
            expect(resolver.effects().fragments.empty() && resolver.effects().audio.size() == 1 && resolver.effects().audio[0].action == CarContactAudioAction::reset_pair,
                "A first collision with no full bodies must only initialize pair history, without invented damage sounds or impact fragments");
        }
    }
}

void candidate_chronology() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {one, 0, 0});
    auto middle = car(surface, damage, roughness, {5 * one, 5 * one, 0}, {one, 0, 0});
    auto last = car(surface, damage, roughness, {9 * one, 5 * one, 0}, {one, 0, 0});
    const std::array<CarContactActor, 3> actors{{{first.get(), 0}, {middle.get(), 0}, {last.get(), 0}}};
    auto groups = group(2);
    groups.count = 4;
    groups.groups[1] = {1, {1, 2}, 2};
    groups.groups[2] = groups.groups[0];
    groups.groups[3] = groups.groups[1];
    CarContacts resolver;
    const auto contacts = resolver.resolve(actors, groups);
    expect(contacts.size() == 2 && contacts[0].first == 1 && contacts[0].second == 2 && contacts[1].first == 0 && contacts[1].second == 1,
        "A later sector group must retry an earlier miss after another pair moves its body, while suppressing duplicate successful contacts");
}

void maximum_neighbors() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    constexpr Vector velocity{one, 0, 0};
    std::array<Vector, 16> positions{}, resolved_gaps{};
    positions[0] = {0, 5 * one, 0};
    auto reference = car(surface, damage, roughness, positions[0], velocity);
    // Derive each next contact from a two-body pass. Its center correction is
    // independent of earlier neighbors, whose later translations are checked below.
    for (std::size_t neighbor = 1; neighbor < positions.size(); ++neighbor) {
        positions[neighbor] = add(reference->state().body.position, Vector{4 * one, 0, 0});
        auto other = car(surface, damage, roughness, positions[neighbor], velocity);
        const auto pair = one_pass(std::array<CarContactActor, 2>{{{reference.get(), 0, true, false}, {other.get(), 0, true, false}}});
        expect(pair.size() == 1, "Each maximum-neighbor reference pair must discover a real moving contact");
        resolved_gaps[neighbor] = sub(other->state().body.position, reference->state().body.position);
    }
    const auto expected_center = reference->state().body.position;
    std::array<std::unique_ptr<Motion>, 16> bodies;
    std::array<CarContactActor, 16> actors;
    ContactCandidateGroups groups;
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        bodies[actor] = car(surface, damage, roughness, positions[actor], velocity);
        actors[actor] = {bodies[actor].get(), 0, true, false};
        if (actor != 0) {
            groups.groups[groups.count++] = {std::uint16_t(actor), {0, std::uint8_t(actor)}, 2};
        }
    }
    // All fifteen neighbors are now retained. Repeat both directed forms,
    // including the forward lookup of actor15 in the final occupied slot.
    for (const bool reverse : {false, true}) {
        for (std::uint8_t neighbor = 1; neighbor < actors.size(); ++neighbor) {
            const std::array<std::uint8_t, 16> pair = reverse ? std::array<std::uint8_t, 16>{neighbor, 0} : std::array<std::uint8_t, 16>{0, neighbor};
            groups.groups[groups.count++] = {std::uint16_t(neighbor + (reverse ? 32 : 16)), pair, 2};
        }
    }
    CarContacts resolver;
    const auto contacts = resolver.resolve(actors, groups);
    expect(contacts.size() == 15 && bodies[0]->state().body.position == expected_center,
        "One actor must retain all fifteen successful neighbors without repeating forward or reverse contacts");
    for (std::size_t neighbor = 1; neighbor < bodies.size(); ++neighbor) {
        expect(contacts[neighbor - 1].first == 0 && contacts[neighbor - 1].second == neighbor, "Maximum-neighbor contacts must preserve candidate discovery order");
        const auto expected = add(expected_center, resolved_gaps[neighbor]);
        expect(bodies[neighbor]->state().body.position == expected && bodies[neighbor]->state().previous_position_q16 == positions[neighbor],
            "Each prior neighbor must receive every later center shift once while preserving integration history");
        auto expected_body = bodies[neighbor]->state().body;
        expected_body.position = positions[neighbor];
        scene::refresh_body_contacts(expected_body, 0);
        const auto displacement = sub(expected, positions[neighbor]);
        for (std::size_t corner = 0; corner < expected_body.contacts.size(); ++corner) {
            expect(bodies[neighbor]->state().body.contacts[corner] == add(expected_body.contacts[corner], displacement),
                "Maximum-neighbor propagation must translate every retained corner exactly");
        }
        expect(resolver.source_touching()[neighbor] && resolver.source_touching()[neighbor * 16], "All fifteen successful pairs must retain both directed touching records");
    }
}

void shared_damage_owner() {
    const auto surface = floor();
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    damage.set_visibility([](auto sector) {
        return sector == 7;
    });
    DamageState scratch;
    DamageInput light;
    for (auto i = 0; i < 4; ++i) {
        (void)damage.apply(scratch, light);
    }
    auto first = car(surface, damage, roughness, {0, 5 * one, 0}, {one, 0, 0});
    auto second = car(surface, damage, roughness, {10 * one, 5 * one, 0}, {});
    first->set_actor_identity(3, false);
    second->set_actor_identity(8, true);
    first->set_damage_registration(7);
    second->set_damage_registration(7);
    first->add_car_contact({one, 0, 0}, {});
    (void)first->apply_car_damage({0, 0, one}, false);
    expect(damage.parts().status().count == 1 && first->damage().levels[0] == 3,
        "The shared cycle must detach using its owner's stored visible sector7, not its updated world-position sector0");
    const auto detached = std::ranges::find_if(damage.parts().parts(), [](const auto& part) {
        return part.active;
    });
    expect(detached != damage.parts().parts().end() && detached->actor_id == 3 && detached->car_id == 0,
        "Shared detached records must retain the actual source actor and car mesh identity");
    const auto before = detached->position;
    const auto velocity = detached->velocity;
    first->reset_for_race(first->state().body);
    expect(damage.parts().status().count == 1 && damage.cycle() == 2, "Resetting one car must not reset the shared damage cycle or remove another scene-owned record");
    damage.advance();
    expect(detached->position == add(before, velocity), "The host must advance the shared ten-part pool exactly once, independently of actor count");
    (void)damage.apply(scratch, light);
    (void)damage.apply(scratch, light);
    second->add_car_contact({one, 0, 0}, {});
    (void)second->apply_car_damage({0, 0, one}, true);
    expect(damage.parts().status().count == 1 && second->damage().levels[0] == 2,
        "One actor's flying part must block a later actor's source-cycle detachment through the shared pool gate");
    const auto selected = second->damage().selection[0];
    DamageState underside;
    auto bottom = light;
    bottom.contact.impulse_q16 = {one, 0, 0};
    bottom.contact.local_contact_q16 = {0, -one, 0};
    bottom.inverse_half_dimensions_q16 = {one, one, one};
    (void)damage.apply(underside, bottom);
    expect(underside.selection[0].node_id == selected.node_id && underside.selection[0].slot == selected.slot && underside.selection[0].rule == 5,
        "419ad0's underside branch must retain the preceding actor's global selection slot and node instead of using per-car scratch");
    const auto cycle = damage.cycle();
    damage.reset_parts();
    expect(damage.parts().status().count == 0 && damage.cycle() == cycle, "Source scene/grid debris clear preserves the global damage selection cycle");
}

} // namespace

int main() {
    try {
        no_hits();
        pile_up();
        secondary_moment();
        delayed_moment_integration();
        glancing_audio();
        removed_body();
        contact_effects();
        continued_scrape();
        full_body_damage_gate();
        candidate_chronology();
        maximum_neighbors();
        shared_damage_owner();
        std::cout << "PASS: physical pile-up propagation, retained corners and asymmetric strong-contact torque\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
