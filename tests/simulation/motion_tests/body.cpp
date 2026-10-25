#include "body.hpp"

#include "scene/car_light_glow.hpp"
#include "scene/catalog.hpp"
#include "scene/shadow_support.hpp"

namespace toca::test::motion {
using namespace toca::simulation;

void BodyFixture::coverage() {
    using namespace toca::scene;
    for (const auto x : {-60 * unit, 0, 60 * unit}) {
        for (const auto z : {-60 * unit, 0, 60 * unit}) {
            const auto sample = floor.sample(x, z);
            expect(sample && sample->height == 2 * unit, "Synthetic driving floor must cover both triangles and their diagonal without a false height boundary");
        }
    }
}

void BodyFixture::flight_landing() {
    using namespace toca::scene;
    auto airborne = body;
    airborne.position[1] += 2 * unit;
    refresh_body_contacts(airborne, 0);
    Motion falling(floor, airborne, 0, true, 1, false, damage, roughness);
    bool left_ground = false, landed = false;
    bool faded_reflections = false, landed_reflections = false;
    std::uint32_t landing_events = 0;
    for (unsigned tick = 0; tick < 100 && !landed; ++tick) {
        ticks(falling, 1, {});
        const auto& state = falling.state();
        left_ground |= state.contact_count == 0;
        landed = left_ground && state.contact_count != 0;
        if (landed) {
            expect(state.impacts.size() == 1, "First hard ground contact must publish one original landing event");
            const auto& impact = state.impacts.front();
            expect(impact.kind == 5 && impact.strength_q16 == 0x3333 && impact.local_player && impact.barrier_index == UINT32_MAX,
                "40f5c0 must override419ad0 with event5 and retain its original fixed strength without barrier scaling");
            expect(fixed::abs(impact.position_q16[1] - 2 * unit) < 64, "Landing audio must originate at the averaged touching body corners, on the actual floor");
            ++landing_events;
        } else {
            expect(state.impacts.empty(), "Airborne ticks must not synthesize landing audio");
        }
        expect(state.damage.levels == DamageState{}.levels, "The original sub-threshold landing impulse must not invent dented panels");
        if (state.contact_count == 0) {
            expect(state.body.ground_normal == body.ground_normal, "Airborne motion must retain its support normal until landing updates the ground plane");
        }
        const auto lamp_ground = falling.car_light_ground_input();
        auto lamp_camera = state.body.position;
        lamp_camera[0] += 10 * unit;
        const auto lamps = car_light_glow_frame(0, 3, {.headlights = true}, state.body.object_to_world, state.body.object_to_world, lamp_camera, &lamp_ground);
        expect(lamps.count == 2 && lamps.ground_count == 2, "Wet asphalt must retain both lamp reflections even while no wheel touches");
        faded_reflections |= state.contact_count == 0 && lamps.ground_sprites[0].shade0 == 0;
        landed_reflections |= landed && lamps.ground_sprites[0].shade0 > 100;
        const auto distant_ground = falling.car_light_ground_input(true);
        expect(!lamp_ground.distant_body && distant_ground.distant_body && lamp_ground.body_height == car_half_dimensions(0)[1],
            "The reflection adapter must distinguish distant body mode from airborne wheel distances and use the authored half-height");
        for (std::size_t wheel = 0; wheel < 4; ++wheel) {
            expect(lamp_ground.wheel_position[wheel] == state.body.contacts[wheel] && lamp_ground.ground_height[wheel] == 2 * unit && lamp_ground.surface[wheel] == 0,
                "Flight and landing must retain the physical wheel positions, sampled road plane and asphalt class");
        }
        const auto shadow = car_shadow_footprint(car_shadow_input(state.body, resolve_setup({0, 0, 1, 3})));
        for (const auto& point : shadow.points) {
            expect(fixed::abs(point[1] - 2 * unit) < 256, "The live shadow must stay on the sampled road throughout flight and landing");
        }
    }
    expect(left_ground && landed, "The airborne shadow fixture must exercise actual no-contact gravity and a subsequent landing");
    expect(faded_reflections && landed_reflections, "Actual flight must fade wet-ground lamps and restore their brightness on landing");
    expect(landing_events == 1 && falling.state().impact_count == 1 && falling.damage().cycle == 1,
        "A real landing must advance the original419ad0 cycle once and count its event once");
    const auto landing_velocity = falling.state().velocity_q16;
    ticks(falling, 1, {});
    expect(falling.state().velocity_q16[0] > landing_velocity[0], "The next40caf0 must consume the source landing acceleration left in actor+1f0");
    ticks(falling, 7, {});
    expect(falling.state().impacts.empty() && falling.state().impact_count == 1 && falling.damage().cycle == 1,
        "Continuous support after landing must not retrigger the first-contact effect");
    falling.reset_for_race(body);
    expect(falling.state().impact_count == 0 && falling.state().impacts.empty() && falling.damage().cycle == 0,
        "Grid reconstruction must clear landing events and damage selection state");
    auto low_drop = body;
    // 0.125 units leaves one genuine unsupported tick and lands below the
    // source0x3333 speed gate. A0.2-unit drop crosses that gate.
    low_drop.position[1] += 0x2000;
    refresh_body_contacts(low_drop, 0);
    Motion soft_landing(floor, low_drop, 0, true, 1, false, damage, roughness);
    bool soft_airborne = false, soft_landed = false;
    for (unsigned tick = 0; tick < 100 && !soft_landed; ++tick) {
        ticks(soft_landing, 1, {});
        soft_airborne |= soft_landing.state().contact_count == 0;
        soft_landed = soft_airborne && soft_landing.state().contact_count != 0;
        expect(soft_landing.state().impacts.empty(), "A gentle landing below the source normal-speed threshold must remain silent");
    }
    expect(soft_airborne && soft_landed && soft_landing.damage().cycle == 0, "A genuine gentle contact transition must leave419ad0 untouched");
}

void BodyFixture::drivetrain() {
    using namespace toca::scene;
    for (const auto car : {8u, 9u}) {
        const auto bonus_body = initial_body_pose(floor, start, car);
        Motion bonus(floor, bonus_body, car, false, 1, false, damage, roughness);
        ticks(bonus, 80, {.throttle = true});
        expect(bonus.state().body.position[0] > bonus_body.position[0] + unit && bonus.state().speed_q16 > 0xccc,
            "Both bonus cars must move from the grid through their actual source drivetrain/contact forces");
        ticks(bonus, 10, {.brake = true});
        bonus.reset_for_race(bonus_body);
        expect(
            bonus.state().body.position == bonus_body.position && bonus.state().speed_q16 == 0, "Bonus car qualifying-to-grid reset must retain its original dimensions and mass");
    }
    Motion parked(floor, body, 0, true, 1, false, damage, roughness);
    ticks(parked, 20, {});
    expect(parked.state().body.position == body.position && parked.state().wheel_speed_q16 == std::array<std::int32_t, 4>{},
        "A supported unpowered race start must not acquire invented wheel or horizontal motion");

    Motion held(floor, body, 0, false, 1, false, damage, roughness);
    held.set_start_hold(true);
    ticks(held, 12, {.throttle = true});
    expect(held.start_held() && held.controls().gear == 0 && held.controls().engine_speed_q16 > 0 && held.state().body.position == body.position
               && held.controls().wheel_drive_q16 == std::array<std::int32_t, 4>{},
        "The NoQual session hold must flow through both Motion drivetrain phases without invented brakes or pedal gating");
    held.set_start_hold(false);
    held.update({.throttle = true}, {});
    expect(!held.start_held() && held.controls().gear == 1 && held.controls().wheel_drive_q16[0] > 0,
        "NoQual release must reconnect real wheel torque on the next original Motion step");
    Motion rolling(floor, body, 0, true, 1, true, damage, roughness);
    expect(rolling.state().wheel_speed_q16[0] == 0x33330 && rolling.state().speed_q16 == 0 && rolling.state().velocity_q16[0] > 0xc000,
        "Qualifying must retain its source initial body and wheel speeds");
    std::array<SurfaceRoughness, 5> independent_roughness;
    Motion straight(floor, body, 0, true, 1, false, damage, independent_roughness[0]), repeat(floor, body, 0, true, 1, false, damage, independent_roughness[1]),
        braking(floor, body, 0, true, 1, false, damage, independent_roughness[2]), turning(floor, body, 0, true, 1, false, damage, independent_roughness[3]),
        left_turn(floor, body, 0, true, 1, false, damage, independent_roughness[4]);
    for (auto* motion : {&straight, &repeat, &braking, &turning, &left_turn}) {
        ticks(*motion, 80, {.throttle = true});
    }
    expect(straight.state().body.position[0] > body.position[0] + unit && straight.state().speed_q16 > 0xccc,
        "Actual wheel torque and contact force must move the body forward from the race grid");
    expect(straight.controls().engine_speed_q16 > 0 && straight.state().wheel_speed_q16[0] > 0, "Engine coupling must consume the newly integrated real wheel rotation");
    ticks(straight, 20, {});
    ticks(repeat, 20, {});
    ticks(braking, 20, {.brake = true});
    ticks(turning, 20, {.throttle = true, .right = true});
    ticks(left_turn, 20, {.throttle = true, .left = true});
    expect(straight.state().body.position == repeat.state().body.position && straight.state().body.basis == repeat.state().body.basis
               && straight.state().wheel_speed_q16 == repeat.state().wheel_speed_q16,
        "Identical source tick/input sequences must reproduce every integrated pose and wheel speed");
    expect(braking.state().speed_q16 < straight.state().speed_q16, "Original split brake torque must slow the moving chassis relative to coasting");
    // The known source basis faces +X, up is +Y, and View2 screen-right is -Z.
    // This checks actual force/integration direction, not merely a changed pose.
    expect(turning.state().body.basis[0][2] < straight.state().body.basis[0][2] && turning.state().body.position[2] < straight.state().body.position[2],
        "Right input must turn and displace the +X-facing chassis toward View2 right (-Z)");
    expect(left_turn.state().body.basis[0][2] > straight.state().body.basis[0][2] && left_turn.state().body.position[2] > straight.state().body.position[2],
        "Left input must turn and displace the +X-facing chassis toward View2 left (+Z)");
}

void BodyFixture::barriers_materials() {
    using namespace toca::scene;
    Bytes linked_points(96);
    put32(linked_points, 4, 1);
    put32(linked_points, 8, -21 * unit);
    put32(linked_points, 16, -48 * unit);
    put32(linked_points, 20, -21 * unit);
    put32(linked_points, 28, 48 * unit);
    linked_points[8 + 0x55] = 2;
    constexpr std::array<std::array<std::int32_t, 2>, 4> sectors{{{-32 * unit, 32 * unit}, {32 * unit, 32 * unit}, {-32 * unit, -32 * unit}, {32 * unit, -32 * unit}}};
    const toca::scene::BarrierData linked_barriers(linked_points, sectors);
    Motion linked(floor, body, 0, true, 1, false, damage, roughness, &linked_barriers);
    for (unsigned tick = 0; tick < 80 && linked.state().recovery_entry_count == 0; ++tick) {
        ticks(linked, 1, {.throttle = true});
    }
    expect(linked.state().recovery_entry_count == 1 && linked.state().barrier_recovery.active && linked.state().impact_count == 0,
        "Real wheel/HPC Motion must enter the authored kind2 recovery chain through its normal contact consumer without a fabricated crash event");
    const auto recovery_position = linked.state().body.position;
    ticks(linked, 4, {.throttle = true});
    expect(linked.state().recovery_entry_count == 1 && linked.state().impact_count == 0 && linked.state().body.position != recovery_position,
        "Persistent recovery must flow through normal force/integration/HPC updates without freezing the body or generating crash audio");
    const toca::HpcSurface wet_surface(plane(1));
    DamageContext wet_damage(wet_surface);
    SurfaceRoughness wet_roughness;
    Motion discrete(wet_surface, initial_body_pose(wet_surface, start, 0), 0, true, 3, false, wet_damage, wet_roughness);
    discrete.update({}, {});
    expect(discrete.state().rumble[0] && !discrete.state().previous_rumble[0], "First exact surface-one wheel event must preserve the source transition");
    discrete.update({}, {});
    expect(discrete.state().rumble[0] && discrete.state().previous_rumble[0], "Held exact wheel event must retain current and previous flags separately");
    const toca::HpcSurface marked(plane(0x181));
    DamageContext marked_damage(marked);
    SurfaceRoughness marked_roughness;
    Motion marked_material(marked, initial_body_pose(marked, start, 0), 0, true, 1, false, marked_damage, marked_roughness);
    marked_material.update({}, {});
    expect(marked_material.state().surface_ids == std::array<std::uint8_t, 4>{1, 1, 1, 1}, "42e6f0's marked-material branch must mask all upper bits before class lookup");
    auto outside = body;
    outside.position[0] = 200 * unit; // Beyond the HPC root rectangle, not merely outside its last cached triangle.
    refresh_body_contacts(outside, 0);
    Motion boundary(floor, outside, 0, true, 1, false, damage, roughness);
    boundary.update({.throttle = true}, {});
    expect(boundary.state().boundary == MotionBoundary::missing_surface && boundary.state().body.position == outside.position,
        "Coordinates outside the HPC root domain must preserve the last pose with an explicit boundary");
}

} // namespace toca::test::motion
