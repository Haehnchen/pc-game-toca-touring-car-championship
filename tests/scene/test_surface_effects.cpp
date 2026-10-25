#include "core/random.hpp"
#include "scene/surface_effects.hpp"
#include "simulation/fixed.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
namespace fixed = toca::simulation::fixed;
constexpr auto one = fixed::one;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

PropFragmentTables constant() {
    PropFragmentTables result;
    result.positive.fill(one / 2);
    return result;
}

SurfaceEffectVehicle vehicle(std::uint8_t surface = 0, std::int32_t longitudinal = 0) {
    SurfaceEffectVehicle result;
    result.velocity = {one, 0, 0};
    result.body_radius = 2 * one;
    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
        auto& target = result.wheels[wheel];
        target.position = {wheel < 2 ? one : -one, 0, wheel % 2 == 0 ? -one : one};
        target.forward = {one, 0, 0};
        target.side = {0, 0, one};
        target.surface = surface;
        target.longitudinal_slip = longitudinal;
    }
    return result;
}

SceneSetup setup(std::uint32_t weather) {
    return resolve_setup({.weather_choice = weather}, 1);
}

void smoke() {
    auto tables = constant();
    SurfaceEffects effects(tables, setup(1));
    std::array cars{vehicle(0, one)};
    effects.advance(cars);
    effects.emit(cars);
    const auto& initial = effects.frame();
    expect(
        initial.particles == 4 && initial.emitted == 4 && tables.cursor == 4 && initial.kind_mask == 1, "Dry road slip must emit four smoke particles and four shared-table steps");
    const auto first = initial.batches[4].sprites.front();
    expect(first.center == SurfaceEffectVector{-one, 0x1999, one} && first.minimum == SurfaceEffectVector{-0x1999, -0x1999, 0} && first.shade0 == 128 && first.shade1 == 255,
        "4333d0 smoke must retain newest-wheel-first order, initial radius, road height and source shades");
    effects.advance(cars);
    const auto& tick = effects.frame().batches[4].sprites.front();
    expect(tick.center[1] == 10485 && tick.maximum[0] == 10485 && tick.shade0 == 117, "431b20 smoke adds3932 to both radius and height and subtracts eleven alpha units");
    for (int index = 1; index < 10; ++index) {
        effects.advance(cars);
    }
    expect(effects.frame().particles == 4 && effects.frame().batches[4].sprites.front().shade0 == 18, "A smoke particle remains present through its tenth integration");
    effects.advance(cars);
    expect(effects.frame().particles == 0, "Smoke expires before the eleventh integration");

    effects.emit(cars);
    const auto cursor = tables.cursor;
    for (int draw = 0; draw < 144; ++draw) {
        (void)effects.frame();
    }
    expect(tables.cursor == cursor && effects.frame().updates == 12, "Frame access and differing draw counts cannot advance effects or shared RNG");
    const auto cumulative = effects.frame().emitted;
    effects.reset();
    expect(effects.frame().particles == 0 && effects.frame().plumes == 0 && tables.cursor == cursor && effects.frame().emitted == cumulative,
        "431ab0 grid reset clears owned effects while retaining the shared cursor and diagnostic totals");
}

void dust_and_debris() {
    auto tables = constant();
    SurfaceEffects effects(tables, setup(2));
    std::array cars{vehicle(1)};
    cars[0].wheels[0].surface = 3;
    cars[0].wheels[0].longitudinal_slip = -one;
    effects.emit(cars);
    const auto& frame = effects.frame();
    expect(frame.batches[2].sprites.size() == 1 && frame.batches[5].sprites.size() == 1 && tables.cursor == 3,
        "A driven gravel wheel emits one ballistic stone and one dust puff, consuming two then one table steps");
    expect(frame.batches[2].sprites[0].center == SurfaceEffectVector{one, 0, -one} && frame.batches[2].sprites[0].shade0 == 255,
        "432d10 debris starts at ground height with fixed radius and full alpha");
    expect(
        frame.batches[5].sprites[0].center == SurfaceEffectVector{-one, 0x147a, -91750} && frame.batches[5].sprites[0].shade0 == 200 && frame.batches[5].sprites[0].shade1 == 240,
        "4333d0 FWD forward dust moves to opposite axle, offsets body side and uses the original5242 radius");
    effects.advance(cars);
    expect(effects.frame().batches[2].sprites[0].center == SurfaceEffectVector{47189, 5504, -one},
        "432d10's lookup normalization, launch coefficient and positive-table lift must match the source fixed values");

    auto floating_tables = constant();
    SurfaceEffects floating(floating_tables, setup(2), true);
    floating.emit(cars);
    floating.advance(cars);
    floating.advance(cars);
    effects.advance(cars);
    expect(floating.frame().batches[2].sprites[0].center[1] - effects.frame().batches[2].sprites[0].center[1] == 0x83,
        "Low gravity removes exactly431820's131-unit ballistic acceleration without changing initial launch lift");

    effects.reset();
    cars[0].wheels[0].longitudinal_slip = one;
    cars[0].wheels[0].lateral_slip = 0;
    const auto cursor = tables.cursor;
    effects.emit(cars);
    expect(effects.frame().batches[2].sprites.empty() && tables.cursor == (cursor + 1) % 37,
        "Matching gear/slip sign blocks thrown gravel before RNG, while independent dust still consumes its one step");
    effects.reset();
    cars[0].wheels[0].longitudinal_slip = -one;
    cars[0].wheels[0].lateral_slip = -one;
    const auto quadrant_cursor = tables.cursor;
    effects.emit(cars);
    expect(effects.frame().batches[2].sprites.empty() && effects.frame().batches[5].sprites.size() == 1 && tables.cursor == (quadrant_cursor + 3) % 37,
        "A wheel's inward launch quadrant rejects debris after its two random steps, while the independent dust spawn still proceeds");
    effects.reset();
    cars[0].front_drive = 0;
    effects.emit(cars);
    expect(effects.frame().particles == 0, "Rear drive suppresses both gravel effects from a front wheel");

    for (const auto initial_cursor : {0u, 1u}) {
        auto wet_tables = constant();
        wet_tables.cursor = initial_cursor;
        SurfaceEffects wet(wet_tables, setup(3));
        auto grass_car = vehicle(1);
        grass_car.wheels[2].surface = 2;
        grass_car.wheels[2].longitudinal_slip = -one;
        const std::array grass_cars{grass_car};
        wet.emit(grass_cars);
        expect(wet.frame().batches[initial_cursor == 0 ? 1 : 3].sprites.size() == 1 && wet_tables.cursor == initial_cursor + 4,
            "Wet grass chooses mud on an even pre-spawn cursor, grass on odd, then consumes two debris and two base-plume steps");
    }

    auto dry_tables = constant();
    SurfaceEffects dry(dry_tables, setup(1));
    const std::array grass_cars{vehicle(2)};
    dry.emit(grass_cars);
    expect(dry_tables.cursor == 2 && dry.frame().plumes == 0 && dry.frame().particles == 0,
        "434030 dry grass plume must consume its two table steps even though446f20 only draws this plume pool when wet");
}

void spray() {
    auto tables = constant();
    SurfaceEffects effects(tables, setup(3));
    std::array cars{vehicle(0, 4 * one)};
    effects.advance(cars);
    effects.emit(cars);
    const auto& frame = effects.frame();
    expect(frame.particles == 0 && frame.plumes == 13 && frame.kind_mask == (1u << std::uint8_t(SurfaceEffectTexture::spray)) && tables.cursor == 2,
        "Wet road emits one base puff and three trail points per wheel, without smoke or per-wheel RNG");
    const auto& batch = frame.batches[0];
    expect(batch.flags == 6 && batch.sprites[0].shade0 == 35 && batch.sprites[0].shade1 == 180 && batch.sprites[0].maximum == SurfaceEffectVector{53091, 74327, 0},
        "434430's speed lookup, random opacity/radius and4347d0's1.4 horizontal rectangle must remain exact");
    expect(batch.sprites[1].center == SurfaceEffectVector{one, 0x1999, -one} && batch.sprites[1].shade0 == 99 && batch.sprites[2].shade0 == 79 && batch.sprites[3].shade0 == 59
               && batch.sprites[2].center == SurfaceEffectVector{45876, 13106, -one},
        "433b90 road spray must preserve lookup-angle opacity, twenty-step fade and capped rearward trail increment");
    cars[0].secondary_plume = true;
    effects.emit(cars);
    expect(effects.frame().plumes == 14 && effects.frame().batches[0].sprites[1].shade0 == 31,
        "4347d0 secondary plume uses the selected first opacity times0.9, only for the source camera/player condition");
    cars[0].gear_ratio = -one;
    effects.emit(cars);
    expect(effects.frame().plumes == 2, "Wrong-way gear/velocity suppresses wheel spray but retains the independent base plume");
    cars[0].velocity = {};
    effects.emit(cars);
    expect(effects.frame().plumes == 0, "Wet dispatch clears vehicle-bound plumes before testing stopped cars");
}

void light_particles() {
    // 431b20 applies DAT4dacf0 only to the first three groups. Smoke/dust
    // growth and the vehicle-bound spray have no CMLOGRAV branch.
    for (const auto [surface, weather, batch] : {std::array<unsigned, 3>{3, 2, 2}, {2, 2, 3}, {2, 3, 1}, {0, 1, 4}, {0, 3, 0}}) {
        auto normal_table = constant(), light_table = constant();
        SurfaceEffects normal(normal_table, setup(weather)), light(light_table, setup(weather), true);
        const std::array cars{vehicle(std::uint8_t(surface), surface == 0 ? one : -one)};
        normal.emit(cars);
        light.emit(cars);
        expect(!normal.frame().batches[batch].sprites.empty(), "CMLOGRAV coverage must exercise each actual surface particle kind");
        for (unsigned tick = 0; tick < 2; ++tick) {
            normal.advance(cars);
            light.advance(cars);
        }
        const auto normal_frame = normal.frame(), light_frame = light.frame();
        for (std::size_t group = 0; group < normal_frame.batches.size(); ++group) {
            const auto& a = normal_frame.batches[group].sprites;
            const auto& b = light_frame.batches[group].sprites;
            expect(a.size() == b.size(), "CMLOGRAV must preserve surface emission counts");
            for (std::size_t index = 0; index < a.size(); ++index) {
                expect(b[index].center[1] - a[index].center[1] == (group >= 1 && group <= 3 ? 0x83 : 0) && a[index].center[0] == b[index].center[0]
                           && a[index].center[2] == b[index].center[2] && a[index].maximum == b[index].maximum && a[index].shade0 == b[index].shade0
                           && a[index].shade1 == b[index].shade1,
                    "431820 low gravity removes exactly0x83 ballistic acceleration while retaining smoke, dust and spray parameters");
            }
        }
        expect(normal_table.cursor == light_table.cursor && normal_frame.emitted == light_frame.emitted && normal_frame.particles == light_frame.particles,
            "CMLOGRAV must retain the shared random stream and surface pools");
    }
}

void gates_and_capacity() {
    auto tables = constant();
    SurfaceEffects effects(tables, setup(1));
    std::array cars{vehicle(0, one)};
    for (int tick = 0; tick < 20; ++tick) {
        effects.emit(cars);
    }
    expect(effects.frame().particles == 80 && effects.frame().emitted == 80 && tables.cursor == 6, "All five kinds share the original eighty-slot pool");
    effects.emit(cars);
    expect(effects.frame().emitted == 80 && tables.cursor == 6, "Full-pool smoke rejection cannot consume table entries");
    expect(effects.frame(2).batches[4].sprites.empty() && effects.frame(2).particles == 80, "Stored emission visibility masks select views without killing particles");
    effects.reset();
    cars[0].basis = {{{0, one, 0}, {-one, 0, 0}, {0, 0, one}}};
    cars[0].tyre_gate = false;
    effects.emit(cars);
    expect(effects.frame().particles == 0, "Saved controls+2c0 gate rejects a car on its side despite ground-height wheels");
    cars[0].basis = vehicle().basis;
    effects.emit(cars);
    expect(effects.frame().particles == 0, "The saved pre-integration gate must not be recomputed from a later upright pose");
    cars[0] = vehicle(0, one);
    cars[0].airborne = true;
    for (auto& wheel : cars[0].wheels) {
        wheel.position[1] = one;
    }
    effects.emit(cars);
    expect(effects.frame().particles == 0, "All-eight-corner airborne gate suppresses elevated emitters");
    cars[0] = vehicle(0, one);
    cars[0].wheels[0].position[1] = 0x3333;
    effects.emit(cars);
    expect(effects.frame().particles == 3, "Tyre/ground separation equal to0x3333 is outside the strict per-wheel spawn interval");
    cars[0].visibility = 0;
    effects.emit(cars);
    expect(effects.frame().particles == 3, "Invisible cars cannot emit, but existing smoke retains its stored visibility");

    effects.reset();
    cars[0] = vehicle(0, one);
    effects.emit(cars);
    for (auto& wheel : cars[0].wheels) {
        wheel.position[0] += 10 * one;
    }
    effects.advance(cars);
    const auto& constrained = effects.frame().batches[4].sprites[0];
    const auto delta = fixed::sub(SurfaceEffectVector{-one, 10485, one}, cars[0].wheels[3].position);
    const auto normal = fixed::scale(cars[0].support_normal, fixed::dot(cars[0].support_normal, delta));
    const auto expected = fixed::add(fixed::add(cars[0].wheels[3].position, fixed::scale(fixed::normalized(fixed::sub(delta, normal)), 0xb333)), normal);
    expect(constrained.center == expected, "431b20 constrains smoke to an expanding cylinder around its current emitting wheel, retaining the support-normal component");
}

void cadence() {
    SourceRandom first_random(7), second_random(7);
    auto first_tables = make_prop_fragment_tables(first_random), second_tables = make_prop_fragment_tables(second_random);
    SurfaceEffects first(first_tables, setup(3)), second(second_tables, setup(3));
    std::array cars{vehicle(2, -one)};
    for (int tick = 0; tick < 80; ++tick) {
        first.advance(cars);
        second.advance(cars);
        first.emit(cars);
        second.emit(cars);
        for (int draw = 0; draw < tick % 7; ++draw) {
            (void)second.frame();
        }
    }
    expect(first.frame().updates == second.frame().updates && first.frame().emitted == second.frame().emitted && first.frame().particles == second.frame().particles
               && first_tables.cursor == second_tables.cursor && first_random.next() == second_random.next(),
        "Explicit25Hz stepping must preserve pool state and both RNG streams across render rates and zero-step pauses");
    for (std::size_t batch = 0; batch < 6; ++batch) {
        const auto& a = first.frame().batches[batch].sprites;
        const auto& b = second.frame().batches[batch].sprites;
        expect(a.size() == b.size(), "Different draw rates must preserve every batch count");
        for (std::size_t index = 0; index < a.size(); ++index) {
            expect(a[index].center == b[index].center && a[index].maximum == b[index].maximum && a[index].shade0 == b[index].shade0,
                "Different draw rates must preserve all particle positions, radii and opacity");
        }
    }
    for (std::uint32_t track = 0; track < 10; ++track) {
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            auto table = constant();
            SurfaceEffects state(table, resolve_setup({.track_id = track, .weather_choice = weather}, 1));
            state.advance(cars);
            state.emit(cars);
            state.reset();
            expect(state.frame().particles == 0 && state.frame().plumes == 0, "Every selectable map/weather must support construction, a tick and grid reset");
        }
    }
}

void presentation() {
    auto tables = constant();
    SurfaceEffects effects(tables, setup(1));
    std::array cars{vehicle(0, one)};
    cars[0].actor = 6;
    effects.emit(cars);
    const auto born = effects.frame().batches[4].sprites.front();
    const auto cursor = tables.cursor;
    SurfaceEffectFrame rendered;
    const std::array<float, 16> source{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            auto displayed = source;
            displayed[12] = alpha - 1;
            const std::array poses{effect_pose(6, source, displayed)};
            effects.presentation_frame_into(rendered, alpha, poses);
            const auto& sprite = rendered.batches[4].sprites.front();
            expect(std::abs(double(sprite.center[0]) - (double(born.center[0]) + double(alpha - 1) * one)) <= 1 && sprite.center[1] == born.center[1]
                       && sprite.center[2] == born.center[2] && sprite.maximum == born.maximum && sprite.shade0 == born.shade0,
                "New smoke must follow the displayed emitting car at60/144Hz while retaining its source birth size and opacity");
        }
    }
    effects.advance(cars);
    const auto integrated = effects.frame().batches[4].sprites.front();
    auto distant = source;
    distant[12] = 100;
    const std::array distant_pose{effect_pose(6, source, distant)};
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            effects.presentation_frame_into(rendered, alpha, distant_pose);
            const auto& sprite = rendered.batches[4].sprites.front();
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto expected = double(born.center[axis]) + double(integrated.center[axis] - born.center[axis]) * alpha;
                expect(std::abs(double(sprite.center[axis]) - expected) <= 1, "Persistent smoke must interpolate its own world history independently of its former emitting car");
            }
            expect(std::abs(double(sprite.maximum[0]) - (double(born.maximum[0]) + double(integrated.maximum[0] - born.maximum[0]) * alpha)) <= 1
                       && std::abs(double(sprite.shade0) - (double(born.shade0) + double(int(integrated.shade0) - born.shade0) * alpha)) <= 1,
                "Persistent smoke radius and opacity must change continuously between source ticks");
        }
    }
    expect(tables.cursor == cursor && effects.frame().batches[4].sprites.front().center == integrated.center
               && effects.frame().batches[4].sprites.front().maximum == integrated.maximum && effects.frame().particles == 4 && effects.frame().updates == 1,
        "Repeated surface presentation must preserve particle history, source count and shared random cursor");
    effects.emit(cars);
    const auto mixed = effects.presentation_frame(.5f, distant_pose);
    expect(mixed.batches[4].sprites.size() == 8 && mixed.batches[4].sprites[0].center[0] == born.center[0] + 100 * one
               && mixed.batches[4].sprites[4].center[0] == integrated.center[0],
        "New and surviving particles in the same pool must independently select car attachment and their own flight history");
    for (unsigned tick = 0; tick < 11; ++tick) {
        effects.advance(cars);
    }
    expect(effects.presentation_frame(.5f, distant_pose).particles == 0, "Retired surface records must disappear from presentation before pool reuse");
    for (auto& wheel : cars[0].wheels) {
        wheel.position[0] += 20 * one;
    }
    effects.emit(cars);
    const auto reused = effects.frame().batches[4].sprites.front();
    expect(effects.presentation_frame(0).batches[4].sprites.front().center == reused.center,
        "A reused smoke slot must initialize at the new emission rather than the old particle's position");
    effects.reset();
    expect(effects.presentation_frame(.5f, distant_pose).particles == 0, "Grid reset must discard all surface particle presentation history");

    auto wet_tables = constant();
    SurfaceEffects wet(wet_tables, setup(3));
    cars = {vehicle(0, 4 * one)};
    wet.emit(cars);
    const auto spray = wet.frame();
    const auto wet_cursor = wet_tables.cursor;
    const std::array<float, 16> rotated{0, 0, -2, 0, 0, 3, 0, 0, 4, 0, 0, 0, 10, 20, 30, 1};
    const std::array wet_pose{effect_pose(0, source, rotated)};
    const auto attached = wet.presentation_frame(.5f, wet_pose);
    expect(attached.plumes == spray.plumes && spray.plumes == 13 && attached.particles == spray.particles,
        "Spray attachment must preserve the entire base-plume and wheel-trail queue");
    for (std::size_t index = 0; index < spray.batches[0].sprites.size(); ++index) {
        const auto& initial = spray.batches[0].sprites[index];
        const auto& rendered = attached.batches[0].sprites[index];
        expect(rendered.center == SurfaceEffectVector{10 * one + 4 * initial.center[2], 20 * one + 3 * initial.center[1], 30 * one - 2 * initial.center[0]}
                   && rendered.maximum == initial.maximum && rendered.shade0 == initial.shade0 && rendered.shade1 == initial.shade1,
            "Every vehicle-bound spray sample must follow translated, rotated, scaled car poses without changing its discrete size or shade");
    }
    expect(wet_tables.cursor == wet_cursor && wet.frame().batches[0].sprites.front().center == spray.batches[0].sprites.front().center,
        "Spray presentation must leave original emitters and their random cursor untouched");

    // Warm smoke and spray capacities, then alternate hidden, empty and live outputs.
    const auto dry_cursor = tables.cursor;
    wet.presentation_frame_into(rendered, .5f, wet_pose);
    std::array<const SurfaceEffectSprite*, 6> storage;
    for (std::size_t batch = 0; batch < storage.size(); ++batch) {
        storage[batch] = rendered.batches[batch].sprites.data();
    }
    wet.presentation_frame_into(rendered, .5f, wet_pose, 0);
    expect(rendered.kind_mask == 0 && rendered.plumes == 0, "A hidden view must clear previously rendered spray batches");
    for (const auto& batch : rendered.batches) {
        expect(batch.sprites.empty(), "Hidden surface output must not retain a prior view's sprites");
    }
    effects.presentation_frame_into(rendered, .5f);
    expect(rendered.particles == 0 && rendered.plumes == 0 && rendered.kind_mask == 0, "A reset pool must clear reused surface counters and geometry");
    wet.presentation_frame_into(rendered, 1);
    for (std::size_t batch = 0; batch < storage.size(); ++batch) {
        const auto& actual = rendered.batches[batch];
        const auto& expected = spray.batches[batch];
        expect(actual.texture == expected.texture && actual.flags == expected.flags && actual.sprites.size() == expected.sprites.size() && actual.sprites.data() == storage[batch],
            "Reused surface output must restore batch descriptors without reallocating warmed sprite storage");
        for (std::size_t index = 0; index < actual.sprites.size(); ++index) {
            const auto& a = actual.sprites[index];
            const auto& b = expected.sprites[index];
            expect(a.center == b.center && a.minimum == b.minimum && a.maximum == b.maximum && a.shade0 == b.shade0 && a.shade1 == b.shade1,
                "Exact-tick reused surface output must retain every source sprite field");
        }
    }
    expect(wet_tables.cursor == wet_cursor && tables.cursor == dry_cursor, "Reusing surface buffers must not advance source emitters or random tables");
}
} // namespace

int main() {
    try {
        smoke();
        dust_and_debris();
        spray();
        light_particles();
        gates_and_capacity();
        cadence();
        presentation();
        std::cout << "PASS surface effects source pools, tyre gates, plume geometry, RNG and cadence\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
