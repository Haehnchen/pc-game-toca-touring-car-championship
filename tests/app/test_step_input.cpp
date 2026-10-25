#include "app/step_input.hpp"
#include "simulation/time.hpp"

#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

struct FlushedBuffer : std::stringbuf {
    unsigned flushes = 0;

    int sync() override {
        ++flushes;
        return std::stringbuf::sync();
    }
};
} // namespace

int main() {
    try {
        using namespace toca;
        std::istringstream commands("step 0\nstep 5\nstep 783\nstep 5 10\nstep 0 783\nlive\n");
        const auto neutral = read_step_input(commands);
        const auto gas_left = read_step_input(commands);
        const auto all_driving = read_step_input(commands);
        const auto local_pair = read_step_input(commands);
        const auto second_only = read_step_input(commands);
        const auto live = read_step_input(commands);
        expect(!neutral.live && neutral.held == 0 && !gas_left.live && gas_left.held == 5 && !all_driving.live && all_driving.held == step_input_actions && live.live,
            "The bridge must read exactly one driving-mask or live command per complete line");
        expect(neutral.held_player2 == 0 && local_pair.held == 5 && local_pair.held_player2 == 10 && second_only.held == 0 && second_only.held_player2 == step_input_actions,
            "One diagnostic tick must retain two independent driving masks, defaulting an absent second mask to neutral");
        rejects(
            [&] {
                (void)read_step_input(commands);
            },
            "EOF must terminate the bridge visibly rather than invent another simulation step");
        for (const auto text :
            {"", "step 0", "live", "\n", "step\n", "step -1\n", "step +1\n", "step 16\n", "step 1024\n", "step 4294967296\n", "step 5 junk\n", "live junk\n", "unknown\n"}) {
            rejects(
                [&] {
                    std::istringstream invalid(text);
                    (void)read_step_input(invalid);
                },
                "Malformed/truncated commands and non-driving action bits must not advance the application");
        }
        rejects(
            [&] {
                std::istringstream oversized("step " + std::string(64, '0') + "\n");
                (void)read_step_input(oversized);
            },
            "An oversized unfinished command must not grow the input bridge without bound");
        for (const auto text : {"step 5 \n", "step 5  4\n", "step 5 -1\n", "step 5 16\n", "step 5 4294967296\n", "step 5 4 junk\n"}) {
            rejects(
                [&] {
                    std::istringstream invalid(text);
                    (void)read_step_input(invalid);
                },
                "The second player's mask must obey the same bounded driving-only protocol");
        }
        StepInputClock clock;
        const std::uint64_t origin = 1000, wall_resume = 8'000'000'000;
        expect(!clock.stepping() && clock.now(origin) == origin && clock.event_time(origin + 7) == origin + 7,
            "Without stepping, the host and SDL event timestamps must retain the real clock epoch");
        simulation::LiveSchedule schedule;
        schedule.reset(origin);
        const auto first_deadline = schedule.next_deadline_ns();
        clock.advance_to(first_deadline);
        expect(clock.stepping() && clock.now(wall_resume) == first_deadline && clock.event_time(wall_resume + 9) == first_deadline,
            "Host/controller waiting time must freeze while the next simulation deadline is selected");
        const auto due = schedule.take_due(clock.now(wall_resume));
        expect(due.size() == 1 && due[0] == origin + simulation::LiveSchedule::interval_ns && schedule.take_due(clock.now(wall_resume)).empty(),
            "One accepted step must make exactly one normal40ms deadline due and must not replay it");
        rejects(
            [&] {
                clock.advance_to(first_deadline);
            },
            "A repeated diagnostic deadline must not duplicate a simulation step");
        rejects(
            [&] {
                clock.advance_to(first_deadline - 1);
            },
            "A diagnostic command must not move simulation time backwards");
        clock.resume(wall_resume);
        expect(!clock.stepping() && clock.now(wall_resume + 123) == first_deadline + 123 && clock.event_time(wall_resume + 17) == first_deadline + 17
                   && clock.event_time(wall_resume - 1) == first_deadline,
            "Returning to live time must preserve the logical epoch for both audio/application time and newly received SDL events");
        rejects(
            [&] {
                (void)clock.now(wall_resume - 1);
            },
            "The resumed host clock must reject a backwards wall timestamp");
        StepInputClock overflow;
        (void)overflow.now(std::numeric_limits<std::uint64_t>::max() - 1);
        overflow.advance_to(std::numeric_limits<std::uint64_t>::max());
        overflow.resume(wall_resume);
        rejects(
            [&] {
                (void)overflow.now(wall_resume + 1);
            },
            "The resumed application clock must not wrap its logical timestamp");
        rejects(
            [&] {
                (void)overflow.event_time(wall_resume + 1);
            },
            "SDL event timestamp translation must not wrap its logical timestamp");
        FlushedBuffer buffer;
        std::ostream ready(&buffer);
        write_step_ready(ready, 42);
        expect(buffer.str() == "TOCA_STEP 42\n" && buffer.flushes > 0, "READY must publish the exact completed step and flush before waiting for the host");
        std::ostringstream broken;
        broken.setstate(std::ios::badbit);
        rejects(
            [&] {
                write_step_ready(broken, 43);
            },
            "A broken readiness channel must terminate visibly instead of blocking its host");
        std::cout << "PASS: strict step-input framing/EOF, one40ms deadline, coherent resumed clock and flushed acknowledgement\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
