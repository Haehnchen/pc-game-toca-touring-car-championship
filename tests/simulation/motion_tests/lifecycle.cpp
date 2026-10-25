#include "fixtures.hpp"

namespace toca::test::motion {
using namespace toca::simulation;

void qualifying_restart() {
    const toca::HpcSurface floor(driving_plane(0));
    DamageContext damage(floor);
    SurfaceRoughness roughness;
    const auto grid = toca::scene::initial_body_pose(floor, {{-24 * unit, -24 * unit}, 0, 0}, 3);
    const auto approach = toca::scene::initial_body_pose(floor, {{-20 * unit, -20 * unit}, 256, 0}, 3);
    Motion motion(floor, grid, 3, true, 3, false, damage, roughness, nullptr, true);
    motion.set_actor_identity(5, true);
    motion.set_damage_feedback_enabled(true);
    motion.set_model_detail(1);
    motion.set_damage_registration(7);
    ticks(motion, 20, {.throttle = true});
    motion.add_car_contact({unit, 0, 0}, {});
    motion.apply_car_damage({unit, 0, unit}, true);
    expect(motion.state().velocity_q16 != fixed::Vector{} && motion.damage().levels != DamageState{}.levels, "Qualifying restart fixture must have warm motion and visual damage");
    Bytes points(4 + 3 * 64);
    put32(points, 0, 3);
    std::array<std::uint8_t, 40> row{};
    std::array<std::vector<std::int16_t>, 3> distributions;
    toca::SourceRandom random(1);
    const auto driver = [&] {
        return AiDriver(points, row, distributions, 0, 0, random);
    };
    motion.prepare_ai(driver());
    motion.handoff_to_ai(false);
    expect(motion.ai_controlled(), "Qualifying restart fixture must enter with an existing AI owner");
    motion.reset_for_qualifying(approach);
    Motion fresh(floor, approach, 3, true, 3, true, damage, roughness, nullptr, true);
    expect(motion.state().body.position == approach.position && motion.state().velocity_q16 == fresh.state().velocity_q16
               && motion.state().wheel_speed_q16 == fresh.state().wheel_speed_q16 && motion.controls().gear == fresh.controls().gear && !motion.start_held()
               && motion.damage().levels == DamageState{}.levels && !motion.ai_controlled(),
        "Warm qualifying reset must reconstruct the rolling constructor state and repair the actor without retaining its old AI");
    expect(motion.state().wheel_speed_q16 == std::array<std::int32_t, 4>{0x33330, 0x33330, 0x33330, 0x33330} && motion.damage_feedback_enabled(),
        "Qualifying reset must retain source initial wheel speed and feedback assignment");
    damage.reset_parts();
    damage.set_visibility([](auto sector) {
        return sector == 7;
    });
    const std::array<std::uint8_t, 15> scratch{};
    damage.restore_source_state(4, scratch);
    motion.add_car_contact({unit, 0, 0}, {});
    motion.apply_car_damage({0, 0, unit}, true);
    expect(damage.parts().status().count == 1 && damage.parts().parts()[0].actor_id == 5,
        "The next detached-part consumer must use the reset actor's retained identity and damage registration");
    motion.prepare_ai(driver());
    motion.handoff_to_ai(false);
    expect(motion.ai_controlled() && !motion.start_held(), "The new qualifying approach must accept ordinary AI ownership without a grid hold");
    motion.restore_qualifying_control();
    expect(!motion.ai_controlled() && !motion.start_held(), "The source start-line handback must restore player controls after the warm qualifying reset");
}

} // namespace toca::test::motion
