#include "app/wheel_feedback.hpp"
#include "app/wheel_haptics.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace toca;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Trace {
    std::vector<SDL_HapticEffect> created, updated;
    std::vector<int> run, stopped, destroyed, autocenter;
    bool fail_create = false, fail_update = false, fail_run = false, fail_autocenter = false;
};

class Sink final : public WheelHapticSink {
    std::shared_ptr<Trace> trace_;
    Uint32 features_;
    int axes_;

public:
    Sink(std::shared_ptr<Trace> trace, Uint32 features, int axes = 2) : trace_(std::move(trace)), features_(features), axes_(axes) {}

    Uint32 features() const override {
        return features_;
    }

    int axes() const override {
        return axes_;
    }

    bool set_autocenter(int percent) override {
        trace_->autocenter.push_back(percent);
        return !trace_->fail_autocenter;
    }

    int create(const SDL_HapticEffect& effect) override {
        if (trace_->fail_create) {
            return -1;
        }
        trace_->created.push_back(effect);
        return int(trace_->created.size()) - 1;
    }

    bool update(int, const SDL_HapticEffect& effect) override {
        trace_->updated.push_back(effect);
        return !trace_->fail_update;
    }

    bool run(int id) override {
        trace_->run.push_back(id);
        return !trace_->fail_run;
    }

    void stop(int id) override {
        trace_->stopped.push_back(id);
    }

    void destroy(int id) override {
        trace_->destroyed.push_back(id);
    }
};

WheelFeedbackCommand constant() {
    return {WheelFeedbackCommand::Kind::constant, -100, 0, 5000, 50000, 0};
}

WheelFeedbackCommand sine() {
    return {WheelFeedbackCommand::Kind::sine, 0, 0, 2500, std::numeric_limits<std::uint32_t>::max(), 100000};
}

void checks() {
    auto trace = std::make_shared<Trace>();
    {
        WheelHaptics haptics(std::make_unique<Sink>(trace, SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SINE));
        expect(haptics.supported() && haptics.apply(constant()), "Capable sink accepts directed constant force");
        const auto& impact = trace->created.at(0).constant;
        expect(impact.type == SDL_HAPTIC_CONSTANT && impact.direction.type == SDL_HAPTIC_CARTESIAN && impact.direction.dir[0] == -100 && impact.direction.dir[1] == 0
                   && impact.level == 16383 && impact.length == 50,
            "Original direction, DI magnitude and microsecond duration translate to SDL constant units");
        auto second = constant();
        second.direction_x = 0;
        second.direction_y = 100;
        second.magnitude = -20000;
        expect(haptics.apply(second) && trace->created.size() == 1 && trace->updated.back().constant.direction.dir[1] == 100 && trace->updated.back().constant.level == -32767,
            "Impacts update one owned effect, preserve sign/direction and clamp oversized magnitudes");
        expect(haptics.apply(sine()), "Sine runs independently of constant force");
        const auto& periodic = trace->created.at(1).periodic;
        expect(periodic.type == SDL_HAPTIC_SINE && periodic.direction.type == SDL_HAPTIC_STEERING_AXIS && periodic.period == 100 && periodic.magnitude == 8191
                   && periodic.length == SDL_HAPTIC_INFINITY,
            "Source zero-vector sine maps to the steering axis and preserves period, gain and source lifetime");
        auto fast = sine();
        fast.period_us = -10000;
        expect(haptics.apply(fast) && trace->updated.back().periodic.period == 1, "Negative source periods use minimum SDL millisecond period");
        const auto before = trace->stopped.size();
        expect(haptics.apply({WheelFeedbackCommand::Kind::stop_sine}) && trace->stopped.size() == before + 1 && trace->stopped.back() == 1,
            "Stopping sine leaves the separate constant effect untouched");
        trace->fail_update = true;
        const auto runs = trace->run.size();
        expect(!haptics.apply(constant()) && trace->run.size() == runs && trace->stopped.back() == 0, "Failed effect update stops its stale force rather than replaying it");
        trace->fail_update = false;
        trace->fail_run = true;
        expect(!haptics.apply(sine()) && trace->stopped.back() == 1, "Failed run stops its effect");
        expect(haptics.apply({WheelFeedbackCommand::Kind::stop_all}), "Lifecycle stop succeeds");
    }
    expect(trace->destroyed == std::vector<int>{0, 1} && trace->stopped[trace->stopped.size() - 2] == 0 && trace->stopped.back() == 1,
        "Destruction stops and releases both owned effects before sink closure");
    for (const auto features : {Uint32(0), Uint32(SDL_HAPTIC_CONSTANT), Uint32(SDL_HAPTIC_SINE)}) {
        auto limited = std::make_shared<Trace>();
        WheelHaptics haptics(std::make_unique<Sink>(limited, features));
        expect(haptics.supported() == (features != 0), "Unsupported sinks do not claim wheel force feedback");
        expect(haptics.apply(constant()) == ((features & SDL_HAPTIC_CONSTANT) != 0), "Constant capability is independent");
        expect(haptics.apply(sine()) == ((features & SDL_HAPTIC_SINE) != 0), "Sine capability is independent; no rumble fallback");
    }
    auto single = std::make_shared<Trace>();
    {
        WheelHaptics haptics(std::make_unique<Sink>(single, SDL_HAPTIC_CONSTANT, 1));
        expect(haptics.apply(constant()) && single->created.back().constant.direction.type == SDL_HAPTIC_STEERING_AXIS && single->created.back().constant.level == -16383,
            "One-axis wheels map signed source X force explicitly onto the steering axis");
        auto lateral = constant();
        lateral.direction_x = 0;
        lateral.direction_y = 100;
        expect(haptics.apply(lateral) && single->updated.back().constant.level == 0, "Source Y-only impacts have no fabricated X force on one-axis hardware");
    }
    auto centered = std::make_shared<Trace>();
    {
        WheelHaptics haptics(std::make_unique<Sink>(centered, SDL_HAPTIC_CONSTANT | SDL_HAPTIC_AUTOCENTER));
        expect(haptics.supported() && centered->autocenter.empty(), "Capability queries never enable autocenter");
        haptics.activate();
        haptics.activate();
        expect(centered->autocenter == std::vector<int>{100}, "Original device autocenter enables once per active driving interval");
        haptics.stop();
        expect(centered->autocenter == std::vector<int>{100, 0}, "Stopping feedback disables owned autocenter");
        haptics.activate();
    }
    expect(centered->autocenter == std::vector<int>{100, 0, 100, 0}, "Resume reenables autocenter and destruction disables it again");
    auto failed_center = std::make_shared<Trace>();
    failed_center->fail_autocenter = true;
    {
        WheelHaptics haptics(std::make_unique<Sink>(failed_center, SDL_HAPTIC_CONSTANT | SDL_HAPTIC_AUTOCENTER));
        haptics.activate();
        haptics.activate();
        expect(haptics.apply(constant()), "Optional autocenter failure does not reject usable force effects");
        haptics.stop();
    }
    expect(failed_center->autocenter == std::vector<int>{100}, "A failed optional autocenter request is not retried every physical tick");
    auto failing = std::make_shared<Trace>();
    failing->fail_create = true;
    WheelHaptics haptics(std::make_unique<Sink>(failing, SDL_HAPTIC_CONSTANT));
    expect(!haptics.apply(constant()) && failing->run.empty(), "Effect allocation failure never runs an invalid identifier");
}
} // namespace

int main() {
    try {
        checks();
        std::cout << "PASS: wheel effect units, direction, independent capabilities and hardware-free lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
