#include "app/gamepad_feedback.hpp"
#include "simulation/motion.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void curb_checks() {
    using namespace toca;
    GamepadFeedback feedback;
    simulation::MotionState motion;
    // Motion clamps speed*0x9851e to one already near speed_integer11.
    // At ordinary driving speed a single curb wheel therefore has full source
    // strength; the other three wheels can remain on plain road.
    motion.speed_integer = 50;
    motion.wheel_contact.fill(true);
    motion.rumble_strength_q16.fill(65536);
    motion.surface_ids[0] = 1;
    expect(feedback.update(motion) == GamepadRumble{}, "Surface class alone must not invent a discrete curb contact");
    motion.rumble[0] = true;
    const auto one = feedback.update(motion);
    expect(one.low >= 38000 && one.low <= 39000 && one.high >= 58000 && one.high <= 59000,
        "One real contacting curb wheel must produce substantial low/high motor output, about59/89 percent");
    motion.rumble[1] = true;
    motion.surface_ids[1] = 1;
    const auto two = feedback.update(motion);
    expect(two.low >= 43000 && two.low <= 45000 && two.high >= 62000 && two.high <= 63000 && two.low > one.low && two.high > one.high,
        "Two curb wheels must remain stronger than one without prematurely reaching maximum");
    motion.rumble[1] = false;
    motion.surface_ids[1] = 0;
    motion.speed_integer = 5;
    motion.rumble_strength_q16[0] = 32768;
    const auto slow = feedback.update(motion);
    expect(slow.low >= 22000 && slow.low <= 23000 && slow.high >= 38000 && slow.high <= 39000 && slow.low < one.low && slow.high < one.high,
        "A weaker single-wheel source contact must remain clearly graded below normal-speed curb feedback");
    motion.rumble_strength_q16[0] = 8192;
    const auto creeping = feedback.update(motion);
    expect(creeping.low > 0 && creeping.low < slow.low && creeping.high > 0 && creeping.high < slow.high,
        "Very weak curb inputs must remain proportional rather than receiving a minimum-amplitude pulse");
    motion.rumble_strength_q16[0] = 65536;
    motion.speed_integer = 50;
    motion.rumble[0] = false;
    motion.surface_ids[0] = 2;
    const auto grass = feedback.update(motion);
    motion.surface_ids[0] = 3;
    const auto gravel = feedback.update(motion);
    expect(grass == gravel && grass.low > 0 && grass.high > 0 && grass.low < one.low / 2 && grass.high < one.high / 2,
        "One grass/gravel wheel must retain weaker road texture instead of receiving the curb boost");
    motion.surface_ids[0] = 1;
    motion.rumble[0] = true;
    motion.wheel_contact[0] = false;
    expect(feedback.update(motion) == GamepadRumble{}, "Losing the only curb wheel's contact must stop its feedback immediately");
    motion.wheel_contact[0] = true;
    motion.speed_integer = 0;
    expect(feedback.update(motion) == GamepadRumble{}, "A stopped car must not retain strong curb vibration");
    motion.speed_integer = 50;
    motion.rumble_strength_q16.fill(0);
    expect(feedback.update(motion) == GamepadRumble{}, "Zero source strength must remain silent even with a curb flag");
}

void checks() {
    using namespace toca;
    GamepadFeedback feedback;
    simulation::MotionState motion;
    expect(feedback.update(motion) == GamepadRumble{}, "A quiet stationary car must not vibrate");
    motion.speed_integer = 50;
    motion.rumble.fill(true);
    motion.rumble_strength_q16.fill(65536);
    expect(feedback.update(motion) == GamepadRumble{}, "Airborne wheels cannot send road vibration");
    motion.wheel_contact.fill(true);
    const auto rough = feedback.update(motion);
    expect(rough.low > 0 && rough.high > rough.low, "Actual contacting rough wheels must drive both motors, emphasizing road texture");
    motion.rumble.fill(false);
    motion.surface_ids.fill(2);
    const auto loose = feedback.update(motion);
    expect(loose.high > 0 && loose.high < rough.high, "Loose surfaces must produce weaker texture feedback than a rumble strip");
    motion.speed_integer = 0;
    expect(feedback.update(motion) == GamepadRumble{}, "Stopped wheels must not leave stale road vibration");
    motion.impacts.push_back({.strength_q16 = 65536, .local_player = false});
    expect(feedback.update(motion) == GamepadRumble{}, "A remote collision must not shake the local controller");
    motion.impacts.front().local_player = true;
    const auto impact = feedback.update(motion);
    expect(impact.low == 65535 && impact.high < impact.low, "A local impact must produce a bounded low-motor pulse even while stationary");
    motion.impacts.clear();
    auto previous = impact.low;
    for (int tick = 0; tick < 4; ++tick) {
        const auto pulse = feedback.update(motion);
        expect(pulse.low < previous, "Impact strength must decay on simulation deadlines");
        previous = pulse.low;
    }
    expect(previous == 0, "An isolated impact must stop within160ms");
    const auto moderate_contact = feedback.update(motion, 32768);
    expect(moderate_contact.low >= 45000 && moderate_contact.low <= 55000,
        "A moderate local rival-contact impulse must share the impact path with substantial output below saturation");
    feedback.reset();
    expect(feedback.update(motion) == GamepadRumble{}, "Pause/exit reset must discard pending feedback");
    motion.prop_impact_q16 = 32768;
    expect(feedback.update(motion) == moderate_contact, "A real local cone/post/sign contact must drive both motors through the same bounded impact envelope as another car");
    motion.prop_impact_q16 = 0;
    expect(feedback.update(motion).low < moderate_contact.low, "A prop impact must decay after the actual contact tick instead of retriggering from moving debris");
    feedback.reset();
    for (const auto kind : {1u, 3u, 5u}) {
        motion.impacts.push_back({.kind = kind, .strength_q16 = 32768, .local_player = true});
        expect(feedback.update(motion) == moderate_contact, "Local wall impacts and hard landings must retain the same feedback path");
        motion.impacts.clear();
        feedback.reset();
    }
    motion.barrier_effects.scrape_strength_q16 = 65536;
    expect(feedback.update(motion).high > 0, "Actual barrier scraping must supply sustained feedback");
    motion.boundary = simulation::MotionBoundary::missing_surface;
    expect(feedback.update(motion, 65536) == GamepadRumble{}, "A stopped unsupported motion boundary must clear every motor");
    motion.boundary = simulation::MotionBoundary::none;
    motion.speed_integer = 100;
    motion.rumble_strength_q16.fill(std::numeric_limits<std::int32_t>::max());
    motion.barrier_effects.scrape_strength_q16 = std::numeric_limits<std::int32_t>::max();
    expect(feedback.update(motion, std::numeric_limits<std::int32_t>::max()).low == 65535, "Excessive source strengths must saturate without integer overflow");
}
} // namespace

int main() {
    try {
        curb_checks();
        checks();
        std::cout << "PASS: local road/contact gamepad feedback, saturation and bounded reset/decay\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
