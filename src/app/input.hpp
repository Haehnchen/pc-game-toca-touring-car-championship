#pragma once

#include "core/contracts.hpp"

#include <deque>
#include <string_view>

namespace toca {
class InputState {
    InputSnapshot snapshot_;

public:
    void begin_poll(std::uint64_t timestamp_ns);
    void set_action(Action action, bool down);
    void key_pressed();
    void text_input(std::string_view text);
    void set_physical_binding(PhysicalBinding binding);
    void set_analog(std::optional<AnalogControls> analog);
    void set_focus(bool focused);
    void set_pointer(float x, float y, bool inside);
    void request_quit();
    const InputSnapshot& snapshot() const;
};

// Timestamped gameplay input. Presentation polls enqueue transitions; only a
// simulation deadline consumes them. Late host events take effect next tick.
class TimedActions {
    enum class Kind {
        action,
        focus,
        analog
    };

    struct Transition {
        std::uint64_t timestamp_ns;
        Action action;
        bool value;
        Kind kind;
        std::optional<AnalogControls> analog;
    };

    std::deque<Transition> pending_;
    InputState state_;
    std::uint64_t consumed_ns_ = 0;
    std::uint32_t queued_held_ = 0;
    std::optional<AnalogControls> queued_analog_;
    bool queued_focused_ = true;
    void enqueue(Transition transition);

public:
    void reset(std::uint64_t timestamp_ns, bool focused);
    void set_action(std::uint64_t timestamp_ns, Action action, bool down);
    void set_focus(std::uint64_t timestamp_ns, bool focused);
    void set_analog(std::uint64_t timestamp_ns, std::optional<AnalogControls> analog);
    // Complete device state, diffed against this queue's own delivery state.
    // reset() invalidates that state so unchanged physical input is delivered
    // to a newly initialized session rather than lost in an outer host cache.
    void synchronize(std::uint64_t timestamp_ns, std::uint32_t held, std::optional<AnalogControls> analog);
    InputSnapshot take(std::uint64_t deadline_ns);
};
} // namespace toca
