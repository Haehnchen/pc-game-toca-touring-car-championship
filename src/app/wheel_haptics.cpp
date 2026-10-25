#include "app/wheel_haptics.hpp"

#include "app/wheel_feedback.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace toca {
namespace {
using HapticHandle = std::unique_ptr<SDL_Haptic, decltype(&SDL_CloseHaptic)>;

class SDLSink final : public WheelHapticSink {
    HapticHandle handle_;

public:
    explicit SDLSink(HapticHandle handle) : handle_(std::move(handle)) {}

    Uint32 features() const override {
        return SDL_GetHapticFeatures(handle_.get());
    }

    int axes() const override {
        return SDL_GetNumHapticAxes(handle_.get());
    }

    bool set_autocenter(int percent) override {
        return SDL_SetHapticAutocenter(handle_.get(), percent);
    }

    int create(const SDL_HapticEffect& effect) override {
        return SDL_CreateHapticEffect(handle_.get(), &effect);
    }

    bool update(int id, const SDL_HapticEffect& effect) override {
        return SDL_UpdateHapticEffect(handle_.get(), id, &effect);
    }

    bool run(int id) override {
        return SDL_RunHapticEffect(handle_.get(), id, 1);
    }

    void stop(int id) override {
        (void)SDL_StopHapticEffect(handle_.get(), id);
    }

    void destroy(int id) override {
        SDL_DestroyHapticEffect(handle_.get(), id);
    }
};

Sint16 magnitude(std::int64_t value) {
    return Sint16(std::int64_t(std::clamp<std::int64_t>(value, -10000, 10000)) * 32767 / 10000);
}

Uint32 duration(std::uint32_t microseconds) {
    return microseconds == std::numeric_limits<std::uint32_t>::max() ? SDL_HAPTIC_INFINITY : std::max(1u, microseconds / 1000u);
}

SDL_HapticDirection direction(const WheelFeedbackCommand& command) {
    SDL_HapticDirection result{};
    result.type = SDL_HAPTIC_CARTESIAN;
    // SDL requires a direction vector. The original sine's (0,0) uses steering X.
    result.dir[0] = command.direction_x == 0 && command.direction_y == 0 ? 1 : command.direction_x;
    result.dir[1] = command.direction_y;
    return result;
}
} // namespace

WheelHaptics::WheelHaptics(std::unique_ptr<WheelHapticSink> sink) : sink_(std::move(sink)) {}

WheelHaptics::~WheelHaptics() {
    stop();
    if (constant_ >= 0) {
        sink_->destroy(constant_);
    }
    if (sine_ >= 0) {
        sink_->destroy(sine_);
    }
}

std::unique_ptr<WheelHaptics> WheelHaptics::open(SDL_Joystick* joystick) {
    if (!SDL_IsJoystickHaptic(joystick)) {
        return {};
    }
    HapticHandle handle(SDL_OpenHapticFromJoystick(joystick), SDL_CloseHaptic);
    if (!handle) {
        return {};
    }
    auto result = std::make_unique<WheelHaptics>(std::make_unique<SDLSink>(std::move(handle)));
    return result->supported() ? std::move(result) : nullptr;
}

bool WheelHaptics::supported() const {
    return sink_ && (sink_->features() & (SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SINE)) != 0;
}

void WheelHaptics::activate() {
    if (activation_attempted_) {
        return;
    }
    activation_attempted_ = true;
    // 47e920 enables the device's own autocenter; this is not a synthesized spring.
    if (sink_->features() & SDL_HAPTIC_AUTOCENTER) {
        autocenter_active_ = sink_->set_autocenter(100);
    }
}

bool WheelHaptics::play(int& id, const SDL_HapticEffect& effect) {
    if (id < 0) {
        id = sink_->create(effect);
    } else if (!sink_->update(id, effect)) {
        sink_->stop(id);
        return false;
    }
    if (id < 0) {
        return false;
    }
    if (!sink_->run(id)) {
        sink_->stop(id);
        return false;
    }
    return true;
}

bool WheelHaptics::apply(const WheelFeedbackCommand& command) {
    using Kind = WheelFeedbackCommand::Kind;
    if (command.kind == Kind::stop_all) {
        stop();
        return true;
    }
    if (command.kind == Kind::stop_sine) {
        if (sine_ >= 0) {
            sink_->stop(sine_);
        }
        return true;
    }
    SDL_HapticEffect effect{};
    if (command.kind == Kind::constant) {
        if (!(sink_->features() & SDL_HAPTIC_CONSTANT)) {
            return false;
        }
        effect.constant.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction = direction(command);
        effect.constant.length = duration(command.duration_us);
        effect.constant.level = magnitude(command.magnitude);
        if (sink_->axes() == 1) {
            // A one-axis wheel can reproduce only the source force's X component.
            // Explicit steering encoding avoids an all-zero Cartesian vector for Y impacts.
            effect.constant.direction = {};
            effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
            effect.constant.level = command.direction_x == 0 ? 0 : magnitude(command.direction_x < 0 ? -std::int64_t(command.magnitude) : command.magnitude);
        }
        return play(constant_, effect);
    }
    if (!(sink_->features() & SDL_HAPTIC_SINE)) {
        return false;
    }
    effect.periodic.type = SDL_HAPTIC_SINE;
    effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
    effect.periodic.length = duration(command.duration_us);
    effect.periodic.period = Uint16(std::clamp(command.period_us / 1000, 1, 65535));
    effect.periodic.magnitude = magnitude(command.magnitude);
    return play(sine_, effect);
}

void WheelHaptics::stop() {
    if (autocenter_active_) {
        (void)sink_->set_autocenter(0);
        autocenter_active_ = false;
    }
    activation_attempted_ = false;
    if (constant_ >= 0) {
        sink_->stop(constant_);
    }
    if (sine_ >= 0) {
        sink_->stop(sine_);
    }
}
} // namespace toca
