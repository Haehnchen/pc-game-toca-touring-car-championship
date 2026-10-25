#include "simulation/controls.hpp"
#include "simulation/fixed.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;
constexpr auto one = fixed::one;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void checks() {
    const DrivetrainInputs moving{.speed_q16 = 0x1000, .signed_speed_q16 = 0x1000};
    Controls analog(0, true);
    ControlsInput input;
    input.analog = AnalogControls{
        .steering_q16 = -one / 2, .throttle_q16 = one / 2, .brake_q16 = one / 4, .profile = 1, .steering_analog = true, .throttle_analog = true, .brake_analog = true};
    analog.begin_update(input, {}, moving);
    expect(analog.state().raw_right_q16 == -one / 2 && analog.state().raw_left_q16 == 0 && analog.state().steering_angle == -90,
        "422f60 human analog X must retain negative raw fields and4190f0's90-unit slew, with correct Left sign");
    expect(analog.state().throttle_q16 == fixed::mul(0x68f5, one / 2) && analog.state().brake_q16 == fixed::mul(0x9999, one / 4),
        "Independent analog gas/brake must scale original maxima immediately, without keyboard buildup");
    for (int tick = 0; tick < 8; ++tick) {
        analog.begin_update(input, {}, moving);
    }
    const auto left = analog.state().steering_angle;
    expect(left < -50 && analog.state().steering_q16 == 0, "Human analog steering must reach its source angle without changing the keyboard accumulator");
    input.analog->steering_q16 = one / 2;
    analog.begin_update(input, {}, moving);
    expect(analog.state().steering_angle == left + 90, "Opposite analog input must slew through the existing angle rather than jumping or reversing its meaning");

    Controls low(0, true), high(0, true), keyboard_low(0, true), keyboard_high(0, true);
    ControlsInput low_input{.right = true};
    low_input.analog = AnalogControls{.steering_q16 = one / 4, .profile = 1, .steering_analog = true};
    auto high_input = low_input;
    high_input.analog->profile = 3;
    for (int tick = 0; tick < 8; ++tick) {
        low.begin_update(low_input, {}, moving);
        high.begin_update(high_input, {}, moving);
    }
    expect(high.state().steering_angle > low.state().steering_angle && low.state().steering_angle > 0,
        "418b60 sensitivity must apply the original1/2 analog gains without a second native gain curve");
    low_input.analog->steering_analog = false;
    high_input.analog->steering_analog = false;
    keyboard_low.begin_update(low_input, {}, moving);
    keyboard_high.begin_update(high_input, {}, moving);
    expect(-keyboard_high.state().steering_q16 > -keyboard_low.state().steering_q16,
        "Configured sensitivity must also reach the existing keyboard profiles when analog flags are false");

    Controls reverse(0, false);
    ControlsInput pedal;
    pedal.analog = AnalogControls{.brake_q16 = one / 2, .throttle_analog = true, .brake_analog = true};
    reverse.begin_update(pedal, {}, {});
    reverse.begin_update(pedal, {}, {});
    expect(reverse.state().gear == 7 && reverse.state().reverse_pedal && reverse.state().throttle_q16 == fixed::mul(0x68f5, one / 2) && reverse.state().brake_q16 == 0,
        "418cd0 analog brake must become proportional reverse gas after the original automatic reversal");
    pedal.analog->brake_q16 = 0;
    pedal.analog->throttle_q16 = one / 2;
    reverse.begin_update(pedal, {}, {});
    reverse.begin_update(pedal, {}, {});
    expect(reverse.state().gear == 1 && !reverse.state().reverse_pedal, "Releasing reverse brake and pressing gas must restore the original forward selection");

    Controls mixed(0, true);
    ControlsInput digital_brake{.brake = true};
    digital_brake.analog = AnalogControls{.throttle_q16 = one / 2, .throttle_analog = true};
    mixed.begin_update(digital_brake, {}, moving);
    expect(mixed.state().throttle_q16 == fixed::mul(0x68f5, one / 2) && mixed.state().brake_q16 > 0 && mixed.state().brake_q16 < 0x9999 / 2,
        "Independent source flags must permit proportional gas with the original filtered digital brake");
    Controls mixed_reverse(0, false);
    ControlsInput brake_only{.brake = true};
    brake_only.analog = AnalogControls{.throttle_analog = true};
    mixed_reverse.begin_update(brake_only, {}, {});
    mixed_reverse.begin_update(brake_only, {}, {});
    expect(mixed_reverse.state().gear == 7 && mixed_reverse.state().throttle_q16 == 0 && mixed_reverse.state().brake_q16 == 0,
        "418cd0 direct gas bypasses419630, while digital4197a0 supplies no reverse gas: preserve the original mixed-pedal asymmetry");
    auto invalid = input;
    invalid.analog->steering_q16 = one + 1;
    bool rejected = false;
    try {
        mixed.begin_update(invalid, {}, moving);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Out-of-range analog axes must be rejected before they enter drivetrain arithmetic");
}
} // namespace

int main() {
    try {
        checks();
        std::cout << "PASS: original local analog steering, independent pedals, sensitivity and automatic reverse\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
