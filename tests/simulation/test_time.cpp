#include "simulation/time.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint64_t> deadlines(std::uint64_t present_hz, bool stall) {
    toca::simulation::LiveSchedule schedule;
    schedule.reset(0);
    std::vector<std::uint64_t> result;
    const auto collect = [&](std::uint64_t now) {
        while (true) {
            const auto batch = schedule.take_due(now);
            result.insert(result.end(), batch.begin(), batch.end());
            if (batch.empty()) {
                break;
            }
        }
    };
    for (std::uint64_t frame = 0; frame <= present_hz * 2; ++frame) {
        const auto now = frame * 1'000'000'000 / present_hz;
        if (!stall || now < 500'000'000 || now > 1'100'000'000) {
            collect(now);
        }
    }
    collect(2'000'000'000);
    return result;
}
} // namespace

int main() {
    try {
        using namespace toca::simulation;
        const auto native_boost = boosted_step({}, true);
        expect(native_boost.delta_q16 == 0x20000 && native_boost.reciprocal_q16 == 0x8000 && native_boost.update_count() == 1,
            "Native fixed deadlines must use the original boost arithmetic without extra updates");
        const auto capped_boost = boosted_step({0x20000, 0x8000, true}, true);
        expect(capped_boost.delta_q16 == 0x30000 && capped_boost.reciprocal_q16 == 0x4000 && capped_boost.extra_step,
            "Boost must retain the original delta cap and halve its input reciprocal independently");
        // Synthetic counters: frequency/25=36000, exact 25/30/60 Hz intervals.
        for (const auto [ticks, delta] : std::array<std::array<std::int32_t, 2>, 3>{{{36000, 65536}, {30000, 54613}, {15000, 27306}}}) {
            SourceClock clock(900000);
            const auto first = clock.sample(100);
            const auto step = clock.sample(std::uint32_t(100 + ticks));
            expect(first.delta_q16 == 65536 && first.reciprocal_q16 == 65536 && first.update_count() == 1, "Original first delta must be one, not supplied elapsed time");
            expect(step.delta_q16 == delta && step.reciprocal_q16 == INT64_C(0x100000000) / delta && !step.extra_step, "Synthetic normal source delta vectors changed");
        }
        SourceClock clock(900000);
        (void)clock.sample(0);
        auto step = clock.sample(54000);
        expect(step.delta_q16 == 98304 && !step.extra_step, "Exactly 1.5 source units must remain a single update");
        step = clock.sample(108001);
        expect(step.delta_q16 == 49152 && step.extra_step && step.reciprocal_q16 == 87381, "Above 1.5 units must halve delta and request two updates");
        step = clock.sample(216001);
        expect(step.delta_q16 == 98304 && step.extra_step, "Exactly three source units must split into two 1.5-unit steps");
        step = clock.sample(400000);
        expect(step.delta_q16 == 98304 && step.extra_step, "Long intervals must cap each extra step at 1.5 units");
        clock.reset();
        step = clock.sample(500000, true);
        expect(step.delta_q16 == 131072 && step.reciprocal_q16 == 32768 && step.extra_step, "Source reset preserves extra flag and first-sample boost halves reciprocal");
        step = clock.sample(536000, true, true);
        expect(step.delta_q16 == 65536 && step.reciprocal_q16 == 65536 && !step.extra_step, "Special-mode gate must prevent boost");

        SourceClock wrap(1000);
        (void)wrap.sample(0xfffffff0);
        step = wrap.sample(0x18);
        expect(step.delta_q16 == 63897 && step.reciprocal_q16 == 67217, "Counter wrap must retain the source's subtraction of one");
        step = wrap.sample(0x18);
        expect(step.delta_q16 == -1638 && step.reciprocal_q16 == -2622080, "Equal low counter samples retain the source's signed minus-one interval");

        auto stored = SourceClock::from_stored_divisor(40, true);
        step = stored.sample(0xfffffff0);
        expect(step.delta_q16 == 65536 && step.reciprocal_q16 == 65536 && step.extra_step,
            "An observed divisor must preserve the explicit previous extra flag through its first sample");
        step = stored.sample(0x18);
        expect(step.delta_q16 == 63897 && step.reciprocal_q16 == 67217 && !step.extra_step,
            "Stored-divisor samples must share the original wrap arithmetic without reconstructing a frequency");
        auto overlay = SourceClock::from_stored_divisor(40);
        (void)overlay.sample(1000);
        step = overlay.sample(1200);
        expect(step.delta_q16 == 98304 && step.extra_step, "A menu-only source sample still updates the retained extra latch");
        step = overlay.sample(1240);
        expect(step.delta_q16 == 65536 && !step.extra_step, "A second menu-only frame must use the immediately preceding QPC sample");
        step = overlay.sample(1440);
        expect(step.extra_step, "The last source sample before warm grid may retain an extra update");
        overlay.reset(80); //Warm460170→412a80 observes a fresh QPF divisor.
        step = overlay.sample(50000);
        expect(step.delta_q16 == 65536 && step.reciprocal_q16 == 65536 && step.extra_step, "Warm first sample must ignore the dialog gap while retaining the earlier extra latch");
        step = overlay.sample(50080);
        expect(step.delta_q16 == 65536 && step.reciprocal_q16 == 65536 && !step.extra_step,
            "Following warm samples must use the newly observed divisor and first sample as their previous counter");
        for (const auto divisor : {0, -1}) {
            bool rejected = false;
            try {
                (void)SourceClock::from_stored_divisor(divisor);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            expect(rejected, "A recorded nonpositive clock divisor must fail before sampling");
            rejected = false;
            try {
                overlay.reset(divisor);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            expect(rejected, "A warm QPF reset cannot install a nonpositive stored divisor");
        }

        const auto reference = deadlines(30, false);
        expect(reference.size() == 50 && reference.front() == 40'000'000 && reference.back() == 2'000'000'000, "Native schedule must publish all 25 Hz deadlines");
        for (const auto cap : {1u, 25u, 60u, 120u}) {
            expect(deadlines(cap, true) == reference, "Presentation gaps/caps must not change simulation deadlines or discard debt");
        }
        LiveSchedule schedule;
        schedule.reset(100);
        expect(schedule.next_deadline_ns() == 40'000'100 && schedule.take_due(100).empty(), "Reset must not invent an immediate tick");
        expect(schedule.take_due(1'000'000'100).size() == 8 && schedule.next_deadline_ns() == 360'000'100, "Batch limit must preserve remaining catch-up debt");
        schedule.reset(2'000'000'000);
        expect(schedule.next_deadline_ns() == 2'040'000'000 && schedule.take_due(2'000'000'000).empty(), "Focus resume must start a fresh deadline without paused catch-up");
        std::cout << "PASS: source counter deltas/extra/boost and independent native deadlines with retained catch-up\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
