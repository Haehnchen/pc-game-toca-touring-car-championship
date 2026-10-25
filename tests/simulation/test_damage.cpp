#include "simulation/damage.hpp"
#include "simulation/damage_parts.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace toca;
using namespace toca::simulation;
using namespace toca::simulation::fixed;
constexpr Basis identity{{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

DamageInput hit(Vector position, std::int32_t impulse = one) {
    DamageInput input;
    input.contact.local_contact_q16 = position;
    input.contact.impulse_q16 = {impulse, 0, 0};
    input.inverse_half_dimensions_q16 = {one, one, one};
    return input;
}

void levels() {
    DamageState state;
    auto input = hit({one, 0, one});
    const auto strong = apply_damage(state, input);
    expect(strong.severity == 3 && strong.strength_q16 == one, "Original saturated impact strength");
    expect(state.levels[13] == 2 && state.broken_features[0], "Front-side glass flag and variant");
    expect(state.levels[2] == 2 && state.levels[3] == 2 && state.levels[7] == 2, "Original side panel rules");
    expect(strong.event_kind == 4, "Source reverse selection order leaves side-glass event4");
    const auto saved = state.levels;
    apply_damage(state, hit({one, 0, one}, 0x8000));
    expect(state.levels == saved, "Lower severity never repairs existing variants");
    reset_damage(state);
    expect(std::all_of(state.levels.begin(), state.levels.end(),
               [](auto value) {
                   return value == 0;
               }),
        "Race reset restores original parts");
    input.car_id = 8;
    apply_damage(state, input);
    expect(state.levels == DamageState{}.levels && state.cycle == 1, "Bonus car immunity retains source cycle advancement");
    input.car_id = 0;
    input.contact.impulse_q16 = {0x2000, 0, 0};
    const auto light = apply_damage(state, input);
    expect(light.severity == 0 && light.event_kind == 1 && state.levels == DamageState{}.levels, "Below visual threshold still emits ordinary contact sound");

    reset_damage(state);
    input = hit({0, 0, one}, 0x8000);
    apply_damage(state, input);
    expect(state.levels[0] == 1 && state.levels[6] == 1 && state.levels[9] == 0, "Minor front hit suppresses rule4 part");
    input = hit({0, 0, one}, 0xc000);
    std::vector<std::uint8_t> detached;
    input.detach = [&](auto slot) {
        detached.push_back(slot);
    };
    state.cycle = 5; // entry0, reversed slots6→1,9→2 (detaches),0→3.
    const auto medium = apply_damage(state, input);
    expect(medium.severity == 2 && medium.event_kind == 2 && state.levels[9] == 3 && detached == std::vector<std::uint8_t>{9},
        "Original severity2 rule4 promotes to detached variant at cycle2");
    reset_damage(state);
    input = hit({0, 0, one});
    state.cycle = 4; // entry5, slots6→0,9→1,0→2.
    detached.clear();
    input.detach = [&](auto slot) {
        detached.push_back(slot);
    };
    apply_damage(state, input);
    expect(detached == std::vector<std::uint8_t>{0} && state.levels[0] == 3, "Source cycle selects front detachable part");
    reset_damage(state);
    state.cycle = 4;
    input.debris_blocked_by_recent_part = true;
    detached.clear();
    apply_damage(state, input);
    expect(detached.empty() && state.levels[0] == 2, "Flying or recent debris blocks detachment");
}

void persistent_scratch() {
    //One empty bounded HPC root; scratch ownership needs no sampled surface.
    std::array<std::uint8_t, 38> hpc{};
    hpc[8] = hpc[34] = hpc[36] = 1;
    const HpcSurface surface(hpc);
    DamageContext context(surface);
    DamageState actor;
    context.apply(actor, hit({one, 0, one}));
    const auto side = context.persistent_state();
    expect(side.selection[3].node_id == 24 && side.selection[4].node_id == 5, "Side damage must populate both shared scratch tail records");
    const auto same_selection = [](const auto& left, const auto& right) {
        return std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
            return a.node_id == b.node_id && a.rule == b.rule && a.slot == b.slot;
        });
    };
    for (const auto z : {one, -one}) {
        context.apply(actor, hit({0, 0, z}));
        const auto scratch = context.persistent_state();
        expect(same_selection(std::span(scratch.selection).last(2), std::span(side.selection).last(2)),
            "Front and rear damage must overwrite only their three source scratch records");
    }
    context.apply(actor, hit({0, -one, 0}));
    const auto persistent = context.persistent_state();
    expect(persistent.cycle == 4 && persistent.selection[0].node_id == 6 && persistent.selection[0].rule == 5 && persistent.selection[0].slot == 1,
        "Roof damage must change only the first scratch rule while preserving the nonzero shared cycle");
    DamagePartsVehicle vehicle;
    vehicle.body.basis = identity;
    context.parts().detach(0, vehicle);
    expect(context.parts().status().count == 1, "Persistence fixture must begin with a live detached slot");
    context.reset_parts();
    reset_damage(actor);
    const auto after_reset = context.persistent_state();
    expect(context.parts().status().count == 0 && context.parts().free(0) && actor.levels == DamageState{}.levels && after_reset.cycle == persistent.cycle
               && same_selection(after_reset.selection, persistent.selection),
        "Warm detached reset and actor repair must preserve the process scratch independently");
    DamageContext reloaded(surface, nullptr, false, after_reset);
    const auto restored = reloaded.persistent_state();
    expect(restored.cycle == persistent.cycle && same_selection(restored.selection, persistent.selection) && reloaded.parts().status().count == 0 && reloaded.parts().free(0)
               && !reloaded.parts().parts()[0].active,
        "A new scene must import only damage scratch while constructing its own free detached pool");
    DamageState next_actor;
    context.apply(actor, hit({0, -one, 0}));
    reloaded.apply(next_actor, hit({0, -one, 0}));
    expect(actor.levels == next_actor.levels && actor.cycle == next_actor.cycle && same_selection(actor.selection, next_actor.selection),
        "The next partial damage consumer must produce the same result after a resource reload");
}

void detached_cached_plane() {
    //A flat triangular island inside a larger bounded HPC root.416320's
    //initial cache0 remains meaningful after the part moves beyond its face.
    std::array<std::uint8_t, 84> bytes{};
    const auto put16 = [&](std::size_t at, std::uint16_t value) {
        bytes[at] = std::uint8_t(value);
        bytes[at + 1] = std::uint8_t(value >> 8);
    };
    const auto put32 = [&](std::size_t at, std::int32_t value) {
        put16(at, std::uint16_t(value));
        put16(at + 2, std::uint16_t(std::uint32_t(value) >> 16));
    };
    put16(8, 1);
    put16(20, 1);
    put16(24, 3);
    put32(32, 2 * one);
    put32(44, 2 * one);
    put32(48, 64 * one);
    put32(52, 64 * one);
    put32(56, 2 * one);
    put16(64, 1);
    put16(70, 1);
    put16(72, 2);
    put16(76, 1);
    put16(80, 1);
    put16(82, 1);
    const HpcSurface surface(bytes);
    DamageContext context(surface);
    DamagePartsVehicle vehicle;
    vehicle.body.basis = identity;
    vehicle.body.position = {80 * one, one, 80 * one};
    expect(!surface.prop_sample(vehicle.body.position[0], vehicle.body.position[2], 0), "Cached-plane fixture must have no containing face at the detached position");
    context.parts().detach(3, vehicle); //Source type2 settles its Y axis.
    context.advance();
    const auto& part = context.parts().parts()[0];
    expect(part.active && part.resting && part.cached_triangle == 0 && part.remaining_ticks == 700 && part.position[1] == 2 * one + 0x1999,
        "Detached landing must retain42e9f0's cached plane after a failed containing-face search rather than falling through the HPC gap");
}

DetachedParts pool(DamageSurfaceQuery surface, bool low = false) {
    return DetachedParts(
        std::move(surface),
        [](const Vector&) {
            return std::optional<std::uint32_t>(0);
        },
        [](auto sector) {
            return sector == 0;
        },
        low);
}

void source_imports() {
    DamageState state;
    state.cycle = 4;
    state.selection[0] = {24, 2, 13};
    state.feedback_side = 2;
    state.feedback_strength = 1234;
    std::array<std::uint8_t, 17> levels;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        levels[i] = std::uint8_t(i % 4);
    }
    std::array<std::uint8_t, 16> broken{};
    broken[0] = broken[8] = 1;
    restore_source_damage(state, levels, broken);
    expect(state.levels == levels && state.broken_features == std::array<bool, 4>{true, false, true, false} && state.cycle == 4 && state.selection[0].node_id == 24
               && state.feedback_side == 2 && state.feedback_strength == 1234,
        "Actor damage import retains seventeen variants/four features without replacing shared selection or feedback state");
    auto malformed_levels = levels;
    malformed_levels[16] = 4;
    bool rejected = false;
    try {
        restore_source_damage(state, malformed_levels, broken);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected && state.levels == levels, "Invalid source damage variants must be rejected atomically");

    std::array<std::uint8_t, 10 * 0x11c> raw{};
    std::array<std::uint8_t, 40> free{};
    const auto word = [](auto& data, std::size_t offset, std::int32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            data.at(offset + byte) = std::uint8_t(std::uint32_t(value) >> (byte * 8));
        }
    };
    for (std::size_t slot = 0; slot < 10; ++slot) {
        word(free, slot * 4, 1);
    }
    const auto allocated = [&](std::size_t slot, std::uint8_t type) {
        const auto at = slot * 0x11c;
        word(free, slot * 4, 0);
        word(raw, at, std::int32_t(0x4bf8a8 + at + 8));
        word(raw, at + 4, std::int32_t(0x4bf8a8 + at + 0x48));
        word(raw, at + 0x108, 1);
        raw[at + 0x10c] = type;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                word(raw, at + 0x88 + row * 16 + axis * 4, identity[row][axis]);
                word(raw, at + 0xc8 + row * 12 + axis * 4, identity[row][axis]);
            }
        }
    };
    allocated(0, 3); // Source slot2 uniquely maps to detached type3.
    word(raw, 0xec, 10 * one);
    word(raw, 0xf0, 10 * one);
    word(raw, 0xf4, 20 * one);
    word(raw, 0xf8, one);
    word(raw, 0xfc, -one / 2);
    word(raw, 0x100, 2 * one);
    word(raw, 0x104, 10 * one);
    raw[0x10e] = 7;
    auto query = [](auto, auto, auto) {
        return std::optional<HpcPropSample>({-100 * one, {0, one, 0}, 7, 0});
    };
    auto parts = pool(query);
    parts.restore_source_state(raw, free, 1);
    const auto first = parts.parts()[0];
    expect(parts.presentation_parts(0)[0].position == first.position && parts.presentation_parts(.5f)[0].basis == first.basis,
        "Detached source-state import must initialize each active slot's presentation history");
    expect(first.active && !first.resting && first.source_slot == 2 && first.type == 3 && first.car_id == DetachedPart::unknown_owner
               && first.actor_id == DetachedPart::unknown_owner && first.basis == identity && first.rotation == identity && first.cached_triangle == 7,
        "Warm source detached import reconstructs fixed matrices and unique type/slot while explicitly leaving render owner unknown");
    parts.advance();
    expect(parts.parts()[0].position == Vector{11 * one, 10 * one - one / 2, 22 * one}
               && parts.parts()[0].velocity == Vector{one - mul(one, 0xa3d), -one / 2 - 0x7ae, 2 * one - mul(2 * one, 0xa3d)}
               && parts.parts()[0].basis == Basis{{{65522, 0, 0}, {0, 65522, 0}, {0, 0, 65522}}},
        "An imported airborne part must execute actual417890 translation, gravity, drag and retained rotation on its next step");
    auto invalid = raw;
    word(invalid, 0, 0x4bf8a8 + 0x11c + 8);
    rejected = false;
    try {
        parts.restore_source_state(invalid, free, 1);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected && parts.parts()[0].position == Vector{11 * one, 10 * one - one / 2, 22 * one},
        "A captured matrix pointer into another pool slot must fail before replacing live detached state");

    // Source417890 visits index<count, without compacting or checking allocation.
    // Retiring invisible slot0 shortens traversal, leaving slot3 untouched.
    allocated(3, 2);
    word(raw, 0x118, 1);
    word(raw, 0x110, 321);
    word(raw, 0x114, 37); //Invisible landed sector retires this warm record.
    word(raw, 3 * 0x11c + 0xec, 42 * one);
    parts.restore_source_state(raw, free, 2);
    parts.advance();
    expect(parts.status().count == 1 && !parts.parts()[0].active && parts.free(0) && parts.parts()[3].active && parts.parts()[3].position[0] == 42 * one,
        "Imported source pool holes preserve417830 retirement and417890 changing-count traversal without compacting active slots");
    DamagePartsVehicle vehicle;
    vehicle.body.basis = identity;
    vehicle.car_id = 3;
    vehicle.actor_id = 11;
    parts.detach(1, vehicle);
    expect(parts.parts()[0].active && !parts.free(0) && parts.parts()[0].source_slot == 1 && parts.parts()[0].actor_id == 11 && parts.parts()[0].car_id == 3
               && parts.parts()[0].remaining_ticks == 321 && parts.parts()[0].sector == 37 && parts.parts()[3].active,
        "A new detachment reuses the first source free slot, retaining timer/sector while supplying its real actor/car identity");
    expect(parts.presentation_parts(0)[0].position == parts.parts()[0].position && parts.presentation_parts(0)[0].basis == parts.parts()[0].basis,
        "A reused detached slot must never interpolate from its previous owner's pose");
    parts.reset();
    expect(parts.status().count == 0 && parts.free(0) && parts.free(3) && !parts.parts()[0].active && !parts.parts()[3].active && parts.parts()[0].remaining_ticks == 321
               && parts.parts()[0].sector == 37,
        "Source415d50 releases every record without erasing its retained flight/landing fields");
    expect(std::ranges::none_of(parts.presentation_parts(.5f),
               [](const auto& part) {
                   return part.active;
               }),
        "Reset detached records must disappear from presentation while retained source fields remain intact");
}

void pieces() {
    std::int32_t ground_height = -100 * one;
    Vector ground_normal{0, one, 0};
    auto query = [&](auto, auto, auto) {
        return std::optional<HpcPropSample>({ground_height, ground_normal, 7, 0});
    };
    auto parts = pool(query);
    DamagePartsVehicle vehicle;
    vehicle.body.basis = identity;
    vehicle.body.position = {10 * one, 2 * one, 20 * one};
    vehicle.velocity = {one, -one, 2 * one};
    vehicle.car_id = 3;
    vehicle.actor_id = 11;
    parts.detach(0, vehicle);
    auto first = parts.parts()[0];
    expect(first.source_slot == 0 && first.type == 0 && first.car_id == 3 && first.actor_id == 11 && first.active, "Detached source actor/car identity");
    expect(first.position == add(vehicle.body.position, Vector{0x19999, -0x3333, 0xb333}), "Source pivot transformed from car origin");
    expect(first.velocity == Vector{0x8000, 0, 0x19999}, "Original launch plus half velocity and downward clamp");
    expect(parts.status().count == 1 && parts.status().blocked_by_recent_part, "Flying part blocks further detach");
    expect(parts.presentation_parts(0)[0].position == first.position && parts.presentation_parts(0)[0].basis == first.basis,
        "Detached part birth must cut directly to its original pivot pose");
    parts.advance();
    expect(parts.parts()[0].position == add(first.position, first.velocity), "Flight advances before gravity and drag");
    expect(parts.parts()[0].velocity[1] == -0x7ae, "Original ordinary gravity");
    expect(parts.parts()[0].velocity[0] == sub(first.velocity[0], mul(first.velocity[0], 0xa3d)), "Original XZ drag");
    const auto next = parts.parts()[0];
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            const auto rendered = parts.presentation_parts(alpha)[0];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                expect(std::abs(double(rendered.position[axis]) - (double(first.position[axis]) + double(first.velocity[axis]) * alpha)) <= 1,
                    "Detached rotation centers must move continuously at60/144Hz before rendering applies the original mesh pivot");
            }
            expect(rendered.remaining_ticks == next.remaining_ticks && rendered.active == next.active && rendered.resting == next.resting && rendered.velocity == next.velocity
                       && rendered.car_id == next.car_id && rendered.actor_id == next.actor_id,
                "Detached presentation must preserve current lifetime, flight state and source ownership");
        }
    }
    expect(parts.presentation_parts(0)[0].basis == first.basis && parts.presentation_parts(1)[0].basis == next.basis && parts.parts()[0].basis == next.basis
               && parts.parts()[0].position == next.position && parts.status().count == 1,
        "Detached presentation must preserve exact source endpoint bases, positions and pool count");
    const auto half_turn = parts.presentation_parts(.5f)[0].basis;
    expect(half_turn != first.basis && half_turn != next.basis && fixed::abs(dot(half_turn[0], half_turn[1])) < 32,
        "Flying detached parts must use an intermediate rigid rotation rather than shrinking a linearly blended basis");
    auto low = pool(query, true);
    low.detach(0, vehicle);
    low.advance();
    expect(low.parts()[0].velocity[1] == -0xc4, "Source low gravity cheat");

    // Contact while Z is exactly tangent retires types0/1, rather than freezing.
    parts.reset();
    parts.detach(0, vehicle);
    ground_height = 100 * one;
    parts.advance();
    expect(parts.status().count == 0 && !parts.parts()[0].active, "Degenerate original landing retires part");

    parts.reset();
    vehicle.velocity = {};
    parts.detach(3, vehicle); // source slot3→type2, aligns Y.
    parts.advance();
    const auto landed = parts.parts()[0];
    expect(landed.resting && landed.remaining_ticks == 700 && landed.position[1] == ground_height + 0x1999, "Type2 original ground clearance and rest duration");
    expect(landed.basis[1] == ground_normal && landed.cached_triangle == 7, "Landing keeps sampled support normal and cached face");
    for (int tick = 0; tick < 200; ++tick) {
        parts.advance();
    }
    expect(!parts.status().blocked_by_recent_part && parts.parts()[0].remaining_ticks == 500, "Source pool gate opens after200 resting ticks");
    for (int tick = 0; tick < 500; ++tick) {
        parts.advance();
    }
    expect(parts.parts()[0].active && parts.parts()[0].remaining_ticks == 0, "Rest timer zero survives until next update");
    parts.advance();
    expect(parts.status().count == 0 && !parts.parts()[0].active, "Rest expiration removes actual detached mesh");

    // Original front/back pieces settle on signed Z, using different lifts.
    for (const auto slot : {0u, 1u}) {
        for (const auto sign : {-1, 1}) {
            parts.reset();
            vehicle.body.basis = {{{one, 0, 0}, {0, 0, -sign * one}, {0, sign * one, 0}}};
            parts.detach(std::uint8_t(slot), vehicle);
            parts.advance();
            const auto& panel = parts.parts()[0];
            expect(panel.resting && panel.position[1] == ground_height + (sign < 0 ? 0x1999 : 0x3333), "Original front/back signed Z landing clearance");
            expect(panel.basis[2] == Vector{0, sign * one, 0}, "Front/back aligns signed Z to ground");
        }
    }
    vehicle.body.basis = identity;
    ground_normal = {0, 0xcccc, 0x9999};
    parts.reset();
    parts.detach(3, vehicle);
    parts.advance();
    expect(parts.parts()[0].basis[1] == ground_normal && parts.parts()[0].resting, "Sloped ground uses original sampled normal");
    expect(fixed::abs(dot(parts.parts()[0].basis[0], parts.parts()[0].basis[1])) < 8, "Landing projects remaining basis axis onto slope");

    // Outward X dominance chooses .15 clearance; opposite face chooses Y/.2.
    ground_normal = {one, 0, 0};
    parts.reset();
    parts.detach(4, vehicle); // type4 outward +X.
    parts.advance();
    expect(parts.parts()[0].resting && parts.parts()[0].position[1] == ground_height + 0x2666, "Type4 outward X landing");
    ground_normal = {-one, 0, 0};
    parts.reset();
    parts.detach(2, vehicle); // type3 outward -X.
    parts.advance();
    expect(parts.parts()[0].resting && parts.parts()[0].position[1] == ground_height + 0x2666, "Type3 outward X landing");
    ground_normal = {0, one, 0};
    parts.reset();
    parts.detach(2, vehicle);
    parts.advance();
    expect(parts.parts()[0].resting && parts.parts()[0].position[1] == ground_height + 0x3333, "Side panel Y landing");

    ground_height = -100 * one;
    parts.reset();
    vehicle.velocity = {0, -20 * one, 0};
    parts.detach(2, vehicle);
    for (unsigned tick = 0; tick < 300 && parts.status().count; ++tick) {
        parts.advance();
    }
    expect(parts.status().count == 0, "Missing support retires after original five-unit fall limit");
    parts.detach(5, vehicle);
    vehicle.model_detail = 2;
    parts.detach(0, vehicle);
    expect(parts.status().count == 0, "Original slot and model-detail spawn gates");
}

void recorded_detachment() {
    //Original Single Race trace CRC32a9135ddd, update104, actor14/car7,
    //poolslot0/type0.
    //The part stays airborne during this first415e10 update; a lower test
    //surface isolates416320/417890 arithmetic from terrain lookup coverage.
    auto parts = pool([](auto, auto, auto) {
        return std::optional<HpcPropSample>({-100 * one, {0, one, 0}, 2741, 0});
    });
    DamagePartsVehicle vehicle;
    vehicle.car_id = 7;
    vehicle.actor_id = 14;
    vehicle.body.position = {-14163753, 387188, -2657314};
    vehicle.body.basis = {{{15761, 2989, 63541}, {-633, 65470, -2926}, {-63598, 89, 15770}}};
    vehicle.velocity = {-12482, 627, -2652};
    parts.detach(0, vehicle);
    const auto& launched = parts.parts()[0];
    expect(parts.status().count == 1 && launched.active && !parts.free(0) && launched.actor_id == 14 && launched.car_id == 7 && launched.source_slot == 0 && launched.type == 0
               && launched.position == Vector{-14182927, 378939, -2544025} && launched.initial_y == 378939,
        "Recorded416320 pivot transform must retain47b150 nearest-even rounding before adding the vehicle position");
    expect(launched.velocity == Vector{-44400, 366, 8135} && launched.basis == vehicle.body.basis
               && launched.rotation == Basis{{{61602, 961, 22340}, {-1037, 65528, 40}, {-22338, -392, 61609}}},
        "Recorded launch velocity and47fc00 rotation must retain their individual fixed-product arithmetic");
    parts.advance();
    const auto& airborne = parts.parts()[0];
    expect(airborne.position == Vector{-14227327, 379305, -2535890} && airborne.velocity == Vector{-42624, -1600, 7810} && airborne.initial_y == 378939,
        "Recorded417890 first flight update must translate before applying gravity and horizontal drag");
    expect(airborne.basis == Basis{{{-6891, 2840, 65112}, {-634, 65456, -2926}, {-65170, -939, -6856}}} && airborne.cached_triangle == 2741 && !airborne.resting
               && airborne.remaining_ticks == 0,
        "Recorded417890 must round each full47b150 row transform before fixed normalization without inventing a landing or rest timer");
}
} // namespace

int main() {
    try {
        levels();
        persistent_scratch();
        detached_cached_plane();
        pieces();
        source_imports();
        recorded_detachment();
        std::cout << "damage source state and detached flight/ground/lifetime passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
