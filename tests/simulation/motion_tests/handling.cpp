#include "fixtures.hpp"

#include <algorithm>

namespace toca::test::motion {
using namespace toca::simulation;

void automatic_reverse() {
    const toca::HpcSurface floor(driving_plane(0));
    DamageContext damage(floor);
    SurfaceRoughness roughness;
    const auto body = toca::scene::initial_body_pose(floor, {{-60 * unit, 50 * unit}, 0, 0}, 0);
    Motion motion(floor, body, 0, false, 1, false, damage, roughness);
    ticks(motion, 60, {.throttle = true});
    expect(motion.state().signed_speed_q16 > 0xccc, "Automatic reverse regression must begin with actual forward motion");
    for (unsigned tick = 0; tick < 250 && motion.controls().gear != 7; ++tick) {
        ticks(motion, 1, {.brake = true});
    }
    expect(motion.controls().gear == 7 && motion.controls().reverse_pedal, "Holding automatic brake after forward motion must select reverse through the real wheel/body feedback");
    const auto reverse_position = motion.state().body.position;
    ticks(motion, 40, {.brake = true});
    expect(motion.state().signed_speed_q16 < -0xccc && fixed::dot(fixed::sub(motion.state().body.position, reverse_position), body.basis[0]) < -unit,
        "Held automatic brake must propel the actual body backward after the reverse commit");
    ticks(motion, 60, {.throttle = true});
    expect(motion.controls().gear >= 1 && motion.controls().gear <= 6 && !motion.controls().reverse_pedal && motion.state().signed_speed_q16 > 0xccc,
        "Releasing automatic reverse brake and holding gas must restore forward gear and actual forward motion");
}

void handling() {
    using namespace toca::scene;
    const HorizontalPlacement start{{-60 * unit, 50 * unit}, 0, 0};
    std::array<std::array<std::int32_t, 5>, 4> speeds{}, peak_slips{};
    constexpr std::array<unsigned, 5> weather{1, 2, 3, 4, 5};
    for (unsigned material = 0; material < 4; ++material) {
        const toca::HpcSurface floor(driving_plane(std::uint16_t(material)));
        DamageContext damage(floor);
        for (std::size_t profile = 0; profile < weather.size(); ++profile) {
            SurfaceRoughness roughness;
            Motion motion(floor, initial_body_pose(floor, start, 0), 0, false, weather[profile], false, damage, roughness);
            ticks(motion, 100, {.throttle = true});
            speeds[material][profile] = motion.state().speed_q16;
            for (unsigned tick = 0; tick < 60; ++tick) {
                ticks(motion, 1, {.throttle = true, .right = true});
                for (const auto slip : motion.state().lateral_slip_q16) {
                    peak_slips[material][profile] = std::max(peak_slips[material][profile], fixed::abs(slip));
                }
            }
            expect(peak_slips[material][profile] > 0x400 && motion.state().angular_velocity_q16[1] > 0,
                "Sustained steering must exhaust real wheel grip and produce lateral slip/yaw on road, curb, grass and gravel in every weather profile");
            ticks(motion, 20, {.throttle = true, .left = true});
            expect(motion.state().angular_velocity_q16[1] <= 0, "Countersteering must reverse the real yaw response after a slide");
            const auto before_braking = motion.state().speed_q16;
            ticks(motion, 40, {.brake = true});
            expect(motion.state().speed_q16 < before_braking / 2 && fixed::abs(motion.state().angular_velocity_q16[1]) < 0x40
                       && fixed::abs(fixed::dot(motion.state().velocity_q16, motion.state().body.basis[2])) < 0x200,
                "Releasing steering and braking must recover lateral stability through tire forces without a synthetic yaw reset");
        }
        expect(speeds[material][0] == speeds[material][1] && speeds[material][2] == speeds[material][3] && speeds[material][0] > speeds[material][4]
                   && speeds[material][4] > speeds[material][2],
            "42e440 must select dry profile0 for sunny/cloudy, profile2 for rain/snow and profile1 for storm on every surface");
        expect(peak_slips[material][0] == peak_slips[material][1] && peak_slips[material][2] == peak_slips[material][3],
            "Weather values sharing the original friction branch must preserve the same actual peak slip response");
    }
    expect(peak_slips[0][0] < peak_slips[0][2] && peak_slips[0][0] < peak_slips[0][4],
        "The same asphalt steering sequence must lose more lateral grip in rain/snow and storm than in the dry");
    expect(speeds[0][0] > speeds[2][0] && speeds[2][0] > speeds[3][0], "Grass and gravel must alter actual motion through their distinct source friction/drag consumers");

    for (const auto conditions : weather) {
        const toca::HpcSurface floor(driving_plane(2, true));
        DamageContext damage(floor);
        SurfaceRoughness roughness;
        Motion motion(floor, initial_body_pose(floor, {{-48 * unit, 30 * unit}, 0, 0}, 0), 0, false, conditions, false, damage, roughness);
        bool mixed_axles = false;
        std::int32_t peak_slip = 0;
        for (unsigned tick = 0; tick < 180; ++tick) {
            ticks(motion, 1, {.throttle = true, .right = tick > 75});
            const auto& state = motion.state();
            mixed_axles |= state.surface_ids[0] == 2 && state.surface_ids[2] == 0;
            for (const auto slip : state.lateral_slip_q16) {
                peak_slip = std::max(peak_slip, fixed::abs(slip));
            }
        }
        expect(mixed_axles && motion.state().surface_ids == std::array<std::uint8_t, 4>{2, 2, 2, 2} && peak_slip > 0x4ccc,
            "Driving from asphalt onto grass must retain separate front/rear contacts during crossing and cause real lateral grip loss in every weather profile");
    }

    // The recovered rear-drive bonus car can spin under power on snowy grass.
    // Require velocity opposite its forward axis while still sliding sideways;
    // a circular turn or automatic reverse would not establish a spin.
    const toca::HpcSurface grass(driving_plane(2));
    DamageContext grass_damage(grass);
    SurfaceRoughness grass_roughness;
    Motion rear_drive(grass, initial_body_pose(grass, {{-110 * unit, 60 * unit}, 0, 0}, 9), 9, false, 4, false, grass_damage, grass_roughness);
    ticks(rear_drive, 150, {.throttle = true});
    bool spun = false;
    for (unsigned tick = 0; tick < 150; ++tick) {
        ticks(rear_drive, 1, {.throttle = true, .right = true});
        const auto& state = rear_drive.state();
        spun |= state.speed_q16 > 0x2000 && state.signed_speed_q16 < 0 && fixed::abs(fixed::dot(state.velocity_q16, state.body.basis[2])) > 0x2000
                && fixed::abs(state.lateral_slip_q16[2]) > 0x4ccc && rear_drive.controls().gear != 7;
    }
    expect(spun, "Rear-wheel traction loss must let the source car rotate across its actual travel direction without selecting reverse");
    ticks(rear_drive, 40, {});
    expect(fixed::abs(rear_drive.state().angular_velocity_q16[1]) < 0x40 && fixed::abs(fixed::dot(rear_drive.state().velocity_q16, rear_drive.state().body.basis[2])) < 0x100
               && rear_drive.state().lateral_slip_q16 == std::array<std::int32_t, 4>{},
        "Lifting throttle and releasing steering must allow grip recovery after the powered spin");
}

} // namespace toca::test::motion
