#pragma once

#include <SDL3/SDL_haptic.h>

#include <memory>

namespace toca {
struct WheelFeedbackCommand;

// Narrow SDL effect sink permits lifecycle/translation checks without touching hardware.
class WheelHapticSink {
public:
    virtual ~WheelHapticSink() = default;
    virtual Uint32 features() const = 0;
    virtual int axes() const = 0;
    virtual bool set_autocenter(int) = 0;
    virtual int create(const SDL_HapticEffect&) = 0;
    virtual bool update(int, const SDL_HapticEffect&) = 0;
    virtual bool run(int) = 0;
    virtual void stop(int) = 0;
    virtual void destroy(int) = 0;
};

class WheelHaptics {
    std::unique_ptr<WheelHapticSink> sink_;
    int constant_ = -1, sine_ = -1;
    bool autocenter_active_ = false, activation_attempted_ = false;
    bool play(int&, const SDL_HapticEffect&);

public:
    explicit WheelHaptics(std::unique_ptr<WheelHapticSink>);
    ~WheelHaptics();
    static std::unique_ptr<WheelHaptics> open(SDL_Joystick*);
    bool supported() const;
    void activate();
    bool apply(const WheelFeedbackCommand&);
    void stop();
};
} // namespace toca
