#include "simulation/car_boost.hpp"
#include "simulation/controls.hpp"
#include "simulation/source_snapshot.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace toca::simulation;

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void word(auto& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::uint8_t(std::uint32_t(value) >> (i * 8));
    }
}
} // namespace

int main() {
    try {
        constexpr std::int32_t unit = 65536, baseline = 0x68f5;
        const CarBoostState initial{baseline, 0};
        const auto near = car_boost_step(initial, baseline, {0, 0, 0}, {10 * unit, 0, 0}, false);
        expect(near.distance_factor_q16 == 2600 && near.target_throttle_q16 == 27128 && near.target_tire_q16 == 311 && near.state.maximum_throttle_q16 == 27128
                   && near.state.tire_offset_q16 == 311 && near.tire_offsets_q16[4] == 3587,
            "428440 near-distance source vector must preserve Q16 products and sub-step target snapping");
        const auto xyz = car_boost_step(initial, baseline, {0, 0, 0}, {-10 * unit, 20 * unit, -30 * unit}, false);
        expect(xyz.distance_factor_q16 == 36400 && xyz.target_throttle_q16 == 30508 && xyz.target_tire_q16 == 4367 && xyz.state.maximum_throttle_q16 == baseline + 0x28f
                   && xyz.state.tire_offset_q16 == 0x28f,
            "Catch-up must use all XYZ squares, signed source products and one655-unit approach per physical tick");
        const auto fifty = car_boost_step(initial, baseline, {}, {50 * unit, 0, 0}, false);
        const auto fifty_one = car_boost_step(initial, baseline, {}, {51 * unit, 0, 0}, false);
        const auto hundred = car_boost_step(initial, baseline, {}, {100 * unit, 0, 0}, false);
        const auto beyond = car_boost_step(initial, baseline, {}, {100 * unit + 1, 0, 0}, false);
        expect(fifty.distance_factor_q16 == 65000 && fifty_one.distance_factor_q16 == unit && hundred.distance_factor_q16 == unit && beyond.distance_factor_q16 == 65000
                   && beyond.target_throttle_q16 == 33368 && beyond.target_tire_q16 == 7799,
            "Source inclusive100-unit axis bound and2500Q16 far sentinel must retain their distinct65000 factor");
        expect(car_boost_step(initial, baseline, {}, {0, -100 * unit - 1, 0}, false).distance_factor_q16 == 65000
                   && car_boost_step(initial, baseline, {}, {0, 0, 100 * unit + 1}, false).distance_factor_q16 == 65000,
            "Every axis independently selects the far sentinel");
        auto state = initial;
        for (unsigned tick = 0; tick < 32; ++tick) {
            state = car_boost_step(state, baseline, {}, {51 * unit, 0, 0}, false).state;
        }
        expect(state.maximum_throttle_q16 == 33422 && state.tire_offset_q16 == 7864, "Full catch-up must converge exactly to the source maximum targets");
        const auto becoming_leader = car_boost_step(state, baseline, {}, {51 * unit, 0, 0}, true);
        expect(becoming_leader.distance_factor_q16 == 0 && becoming_leader.state.maximum_throttle_q16 == 32767 && becoming_leader.state.tire_offset_q16 == 7209,
            "New leader decays its retained boost toward baseline, rather than snapping or retaining a distance bonus");
        for (unsigned tick = 0; tick < 32; ++tick) {
            state = car_boost_step(state, baseline, {}, {}, true).state;
        }
        const auto settled = car_boost_step(state, baseline, {}, {}, true);
        expect(state.maximum_throttle_q16 == baseline && state.tire_offset_q16 == 0 && settled.tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
            "Leader convergence must restore all eight source tire fields including the front-longitudinal exception");
        expect(car_boost_step(initial, baseline, {}, {1, -1, 1}, false).distance_factor_q16 == 0,
            "Individual sub-Q16 squares are rounded before summing, not after a floating point distance calculation");
        expect(car_boost_step(initial, baseline, {}, {std::numeric_limits<std::int32_t>::max(), 0, 0}, false).distance_factor_q16 == 65000
                   && car_boost_step(initial, baseline, {}, {std::numeric_limits<std::int32_t>::min(), 0, 0}, false).distance_factor_q16 == 0,
            "Extreme typed positions retain source32-bit wrap arithmetic without undefined signed overflow");
        Controls launch_controls(0);
        SourceSnapshot launch;
        word(launch.dynamics, 0xc, 0x68f5);
        word(launch.dynamics, 0x10, 0x68f5);
        word(launch.dynamics, 0x27c, 2);
        launch.dynamics[0x330] = 1;
        launch.launch_tire_adjustment_q16 = 0x1000;
        launch_controls.restore_source_snapshot(launch);
        const auto launch_boost = car_boost_step({0x68f5, 0}, 0x68f5, {}, {10 * unit, 0, 0}, false);
        launch_controls.apply_car_boost(launch_boost);
        launch_controls.begin_update({.throttle = true}, {});
        expect(launch_controls.state().launch_tire_adjustment_q16 == 0x1000 - 0x73 && launch_controls.state().tire_offsets_q16 == launch_boost.tire_offsets_q16
                   && launch_controls.state().maximum_throttle_q16 == 27128,
            "Catch-up source prepass must win after native embedded launch decay while advancing the retained launch state");
        std::cout << "Source local/Linkup Car Boost distance, smoothing, leader transition and wrapped Q16 vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
