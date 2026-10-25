#include "app/wheel_feedback.hpp"
#include "simulation/motion.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca;
using Kind = WheelFeedbackCommand::Kind;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void impact_checks() {
    simulation::MotionState motion;
    motion.speed_q16 = 65536;
    motion.rumble[0] = true;
    const std::array<std::array<int, 3>, 4> directions{{{1, -100, 0}, {2, 100, 0}, {5, 0, 100}, {6, 0, -100}}};
    for (const auto& direction : directions) {
        WheelFeedback feedback;
        motion.wheel_feedback_side = std::uint8_t(direction[0]);
        motion.wheel_feedback_strength = 8123;
        const auto commands = feedback.update(motion);
        expect(
            commands
                == std::vector<WheelFeedbackCommand>{{.kind = Kind::constant, .direction_x = direction[1], .direction_y = direction[2], .magnitude = 8123, .duration_us = 50000}},
            "Source sides1/2/5/6 must preserve Cartesian direction, signed source magnitude and50ms duration without a simultaneous road update");
        motion.wheel_feedback_side = 0;
        for (int tick = 0; tick < 14; ++tick) {
            expect(feedback.update(motion).empty(), "The source fifteen-call cooldown includes the impact call and fourteen following calls");
        }
        const auto road = feedback.update(motion);
        expect(road.size() == 1 && road.front().kind == Kind::sine, "Road sine must resume immediately after the fifteen-call gate");
    }
    WheelFeedback feedback;
    motion.wheel_feedback_side = 1;
    motion.wheel_feedback_strength = -123;
    expect(feedback.update(motion).front().magnitude == -123, "Constant magnitude must preserve the source signed-short read without gamepad normalization");
    motion.wheel_feedback_side = 6;
    expect(feedback.update(motion).front().direction_y == -100, "The cooldown must not suppress a new impact");
    motion.wheel_feedback_side = 0;
    for (int tick = 0; tick < 14; ++tick) {
        expect(feedback.update(motion).empty(), "A new impact must restart the complete sine cooldown");
    }
    expect(feedback.update(motion).front().kind == Kind::sine, "The restarted impact cooldown must expire");
    for (const auto side : {0, 3, 4, 7}) {
        (void)feedback.reset();
        motion.wheel_feedback_side = std::uint8_t(side);
        expect(feedback.update(motion).front().kind == Kind::sine, "Other source side codes must neither create a constant effect nor start cooldown");
    }
}

void road_checks() {
    simulation::MotionState motion;
    WheelFeedback feedback;
    expect(feedback.update(motion).empty(), "An untouched source consumer must not invent an effect or stop");
    motion.rumble[2] = true;
    motion.speed_q16 = 0x3333;
    expect(feedback.update(motion).empty(), "Curb sine must honor the strict source speed>0x3333 boundary");
    motion.speed_q16 = 0x3334;
    auto commands = feedback.update(motion);
    expect(commands
               == std::vector<WheelFeedbackCommand>{{.kind = Kind::sine,
                   .magnitude = 0x3334 / 8,
                   .duration_us = std::numeric_limits<std::uint32_t>::max(),
                   .period_us = 160000 - 0x3334}},
        "Any discrete source wheel flag must produce source speed/8 and160000-speed, independent of a later contact flag");
    motion.speed_q16 = 65543;
    commands = feedback.update(motion);
    expect(commands.front().magnitude == 8192 && commands.front().period_us == 94457, "Every active source call must update sine parameters with integer truncation");
    motion.surface_ids[0] = 2;
    motion.wheel_contact[0] = true;
    expect(feedback.update(motion).size() == 1 && feedback.update(motion).front().magnitude == 8192, "Discrete curb feedback must take precedence over contacting loose surfaces");
    motion.rumble.fill(false);
    commands = feedback.update(motion);
    expect(commands.size() == 2 && commands[0].kind == Kind::stop_sine && commands[1].kind == Kind::sine && commands[1].magnitude == 2048 && commands[1].period_us == 100000,
        "Curb-to-loose transition must stop the old effect before replacing it with speed/32 and100ms period");
    motion.surface_ids[0] = 3;
    expect(feedback.update(motion).front().magnitude == 2048, "Source surface3 must share surface2's sine parameters");
    motion.wheel_contact[0] = false;
    expect(feedback.update(motion) == std::vector<WheelFeedbackCommand>{{.kind = Kind::stop_sine}}, "An unsupported loose-surface wheel must stop its retained sine effect");
    expect(feedback.update(motion).empty(), "A stopped source effect must not emit repeated stop commands");
    motion.wheel_contact[0] = true;
    motion.speed_q16 = 0x3333;
    expect(feedback.update(motion).empty(), "Loose sine must use the same strict source speed boundary");
    motion.speed_q16 = 0x3334;
    expect(feedback.update(motion).front().magnitude == 0x3334 / 32, "Loose feedback must begin immediately above the speed boundary");
    motion.speed_q16 = -65536;
    expect(feedback.update(motion).front().kind == Kind::stop_sine, "Negative raw speed must stop the source sine without applying an absolute-value adaptation");
    motion.rumble[0] = true;
    motion.speed_q16 = 160001;
    expect(feedback.update(motion).front().period_us == -1, "The command must retain a nonpositive original period for backend range conversion");
}

void lifecycle_checks() {
    simulation::MotionState motion;
    WheelFeedback feedback;
    motion.rumble[0] = true;
    motion.speed_q16 = 65536;
    (void)feedback.update(motion);
    motion.wheel_feedback_side = 1;
    (void)feedback.update(motion);
    motion.wheel_feedback_side = 0;
    expect(feedback.update(motion).empty(), "The original cooldown must leave a preceding sine running without restarting or stopping it");
    expect(feedback.reset() == WheelFeedbackCommand{.kind = Kind::stop_all}, "Pause/exit reset must explicitly stop constant and sine effects together");
    expect(feedback.update(motion).front().kind == Kind::sine, "Reset must clear the cooldown for a fresh local driving session");
    motion.boundary = simulation::MotionBoundary::missing_surface;
    expect(feedback.update(motion) == std::vector<WheelFeedbackCommand>{{.kind = Kind::stop_all}}, "An unsupported native motion boundary must clear every wheel effect");
    motion.boundary = simulation::MotionBoundary::none;
    motion.rumble.fill(false);
    expect(feedback.update(motion).empty(), "An unsupported-boundary reset must discard retained road flags");
}
} // namespace

int main() {
    try {
        impact_checks();
        road_checks();
        lifecycle_checks();
        std::cout << "PASS: original wheel directions, source magnitude/periods, sine cooldown and lifecycle stop\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
