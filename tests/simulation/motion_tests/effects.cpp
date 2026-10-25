#include "fixtures.hpp"
#include "scene/car_light_glow.hpp"
#include "scene/catalog.hpp"
#include "scene/engine_effects.hpp"
#include "scene/surface_effects.hpp"

#include <algorithm>

namespace toca::test::motion {
using namespace toca::simulation;

void cold_distant_effect_gate() {
    const toca::HpcSurface grass(driving_plane(2));
    DamageContext damage(grass);
    SurfaceRoughness roughness;
    const auto body = toca::scene::initial_body_pose(grass, {{-100 * unit, 0}, 0, 0}, 0);
    Motion motion(grass, body, 0, false, 3, false, damage, roughness);
    expect(!motion.surface_effect_vehicle().tyre_gate, "A pristine car must retain the source's cold-zero tyre gate before any full-body writer");
    for (unsigned tick = 0; tick < 240 && motion.state().speed_q16 < unit; ++tick) {
        motion.update_source_distant({.throttle_q16 = unit}, {});
        finite(motion.state());
    }
    expect(motion.state().speed_q16 >= unit && !motion.surface_effect_vehicle().tyre_gate,
        "Actual distant acceleration and ground placement must retain the cold gate despite positive speed and support");
    toca::scene::PropFragmentTables tables;
    tables.positive.fill(unit / 2);
    toca::scene::SurfaceEffects effects(tables, toca::scene::resolve_setup({.weather_choice = 3}));
    const std::array cold_vehicles{motion.surface_effect_vehicle()};
    effects.emit(cold_vehicles);
    expect(effects.frame().particles == 0 && effects.frame().plumes == 0 && tables.cursor == 0,
        "Moving cold distant cars must not emit grass/debris effects or consume their shared random table before40b360 writes the gate");

    motion.update_source_input({.throttle_q16 = unit}, {});
    finite(motion.state());
    expect(motion.surface_effect_vehicle().tyre_gate, "A real full-body promotion must compute the positive tyre orientation gate");
    const std::array promoted_vehicles{motion.surface_effect_vehicle()};
    effects.emit(promoted_vehicles);
    expect(effects.frame().plumes > 0 && tables.cursor > 0, "Full-body promotion must enable actual positive grass effects through the Motion-to-effects consumer");

    motion.update_source_distant({.throttle_q16 = unit}, {});
    finite(motion.state());
    expect(motion.surface_effect_vehicle().tyre_gate, "A later distant step must retain the full-body gate rather than clear it on every dispatch");
    effects.reset();
    const auto cursor = tables.cursor;
    const std::array retained_vehicles{motion.surface_effect_vehicle()};
    effects.emit(retained_vehicles);
    expect(effects.frame().plumes > 0 && tables.cursor != cursor, "Warm distant cars must continue positive effects from the retained full-body gate");
}

void brake_lights() {
    publish_source_frame_brake_pedals({}, {}); //An empty source actor count is a valid no-op.
    const toca::HpcSurface floor(driving_plane(0));
    DamageContext damage(floor);
    SurfaceRoughness roughness;
    const auto body = toca::scene::initial_body_pose(floor, {{-60 * unit, 50 * unit}, 0, 0}, 0);
    Motion motion(floor, body, 0, true, 1, false, damage, roughness);
    expect(!motion.brake_lights(), "Unpressed human brake pedal must leave the brake lamps off");
    ticks(motion, 1, {.brake = true});
    expect(!motion.brake_lights(), "A physical brake update must retain the lamp until its frame callback");
    motion.publish_frame_brake_pedal();
    expect(!motion.brake_lights() && motion.state().published_brake_q16 == unit, "4608b0 pedal publication must not itself update446b50 lamps");
    motion.publish_frame_brake_lights(1);
    expect(!motion.brake_lights(), "View1 must retain an unlit lamp even with a published brake pedal");
    motion.publish_frame_brake_lights();
    expect(motion.brake_lights() && motion.controls().brake_q16 < 0x1999, "446b50 lamps must consume the published raw pedal before filtered brake torque reaches its threshold");
    ticks(motion, 1, {});
    motion.publish_frame_brake_lights();
    expect(motion.brake_lights(), "A callback before4608b0 publication must consume the old pedal after raw brake release");
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights(1);
    expect(motion.brake_lights(), "View1 must retain the lit lamp after the released pedal is published");
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "View0 must extinguish the lamp after the human brake release is published");

    //4608b0 publishes raw+144 for446b50; received human axes share it.
    motion.apply_network_controls({.brake_q16 = 0x1998});
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "Original human brake-lamp threshold excludes raw pedal1998");
    motion.apply_network_controls({.brake_q16 = 0x1999});
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "Received controls cannot bypass the retained physical pedal publication");
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights();
    expect(motion.brake_lights(), "Original human brake-lamp threshold includes raw pedal1999");
    motion.apply_network_gear(7, true);
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "Automatic brake-as-reverse gas must suppress the rear brake lamps");
    motion.apply_network_gear(7, false);
    motion.publish_frame_brake_lights();
    expect(motion.brake_lights(), "Manual reverse retains brake lamps when the brake pedal is used as a brake");
    motion.apply_network_controls({});
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "Received human brake release must extinguish the same published lamp state");

    expect(source_frame_brake_lights(7, 0, unit, 0, 0) == 7 && source_frame_brake_lights(7, 1, 0, 0, 1) == 7,
        "446b50 must preserve the complete retained word for AI and nonzero-view gates");
    expect(source_frame_brake_lights(1, 1, -1, 0, 0) == 0 && source_frame_brake_lights(1, 1, unit, -1, 0) == 0,
        "Published brake comparison is signed and every nonzero reverse-pedal value suppresses human lamps");

    Bytes points(4 + 3 * 64);
    put32(points, 0, 3);
    std::array<std::uint8_t, 40> row{};
    std::array<std::vector<std::int16_t>, 3> distributions;
    toca::SourceRandom random(1);
    AiDriver driver(points, row, distributions, 0, 0, random);
    AiMappedControlState mapped;
    mapped.reaction_ticks = 5;
    mapped.brake_ticks = 3;
    driver.restore_source_controls(mapped);
    motion.apply_network_controls({.brake_q16 = unit});
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights();
    motion.apply_network_controls({});
    motion.publish_frame_brake_pedal();
    motion.prepare_ai(std::move(driver));
    motion.handoff_to_ai(false);
    expect(motion.brake_lights(), "402be0 handoff must retain the human lamp rather than expose the prepared AI's old lamp output");
    motion.publish_frame_brake_pedal();
    motion.publish_frame_brake_lights();
    expect(motion.brake_lights(), "A human frame callback must not replace an existing AI brake-lamp latch with a released pedal");
    motion.restore_player_control();
    expect(motion.brake_lights(), "Controller handback must retain the previous AI lamp until the human callback writes it");
    motion.publish_frame_brake_lights(1);
    expect(motion.brake_lights(), "View1 after handback must preserve the same retained lamp");
    motion.publish_frame_brake_lights();
    expect(!motion.brake_lights(), "The next human view0 callback must replace the retained AI lamp using the published pedal");
    motion.reset_for_race(body);
    expect(!motion.brake_lights() && motion.state().published_brake_q16 == 0, "Race reconstruction must initialize retained pedal and lamps for the new body");
}

void engine_effect_adapter() {
    const toca::HpcSurface floor(driving_plane(0));
    DamageContext damage(floor);
    SurfaceRoughness roughness;
    const auto body = toca::scene::initial_body_pose(floor, {{-60 * unit, 50 * unit}, 0, 0}, 0);
    Motion motion(floor, body, 0, false, 1, false, damage, roughness);
    bool engine_changed = false;
    for (unsigned tick = 0; tick < 30; ++tick) {
        const auto previous = motion.controls().engine_speed_q16;
        ticks(motion, 1, {.throttle = tick < 20, .right = tick >= 15 && tick < 20});
        const auto effects = motion.engine_effect_vehicle();
        const auto& state = motion.state();
        expect(effects.previous_engine_speed_q16 == previous && state.previous_engine_speed_q16 == previous,
            "40b360 engine effects must retain the actual engine speed from before this motion update");
        expect(effects.engine_speed_q16 == motion.controls().engine_speed_q16 && effects.position == state.body.position && effects.basis == state.body.basis
                   && effects.car_type == 0 && effects.gear == motion.controls().gear && effects.local_player,
            "Engine effects must receive the final original pose/basis, current RPM/gear and permanent local-player identity");
        engine_changed |= effects.engine_speed_q16 != previous;
    }
    expect(engine_changed, "Engine adapter fixture must exercise actual RPM changes rather than only compare a stopped zero state");
    motion.set_actor_identity(11, false);
    expect(motion.engine_effect_vehicle().actor == 11 && !motion.engine_effect_vehicle().local_player,
        "Rival exhaust must retain its source actor identity and remote audio admission");
    Bytes points(4 + 3 * 64);
    put32(points, 0, 3);
    std::array<std::uint8_t, 40> row{};
    std::array<std::vector<std::int16_t>, 3> distributions;
    toca::SourceRandom random(1);
    motion.prepare_ai(AiDriver(points, row, distributions, 0, 0, random));
    motion.handoff_to_ai(false);
    expect(motion.engine_effect_vehicle().actor == 11 && !motion.engine_effect_vehicle().local_player, "AI control must not promote a rival exhaust event to the local audio bank");
    motion.set_actor_identity(5, true);
    expect(motion.ai_controlled() && motion.engine_effect_vehicle().actor == 5 && motion.engine_effect_vehicle().local_player,
        "A permanently human actor must retain local exhaust audio while its finish or qualifying controller is AI-owned");
    motion.reset_for_race(body);
    expect(motion.engine_effect_vehicle().actor == 5 && motion.engine_effect_vehicle().local_player, "Warm reconstruction must preserve the same actor-bound exhaust ownership");
}

} // namespace toca::test::motion
