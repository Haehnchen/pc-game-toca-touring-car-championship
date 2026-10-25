#include "app/input.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace toca {
void InputState::begin_poll(std::uint64_t timestamp_ns) {
    snapshot_.timestamp_ns = timestamp_ns;
    snapshot_.pressed = 0;
    snapshot_.released = 0;
    snapshot_.any_key_pressed = false;
    snapshot_.text_input.clear();
    snapshot_.physical_binding.reset();
}

void InputState::set_action(Action action, bool down) {
    const auto mask = action_mask(action);
    const bool held = (snapshot_.held & mask) != 0;
    if (down && snapshot_.focused && !held) {
        snapshot_.held |= mask;
        snapshot_.pressed |= mask;
    } else if (!down && held) {
        snapshot_.held &= ~mask;
        snapshot_.released |= mask;
    }
}

void InputState::key_pressed() {
    if (snapshot_.focused) {
        snapshot_.any_key_pressed = true;
    }
}

void InputState::text_input(std::string_view text) {
    if (snapshot_.focused) {
        snapshot_.text_input.append(text);
    }
}

void InputState::set_physical_binding(PhysicalBinding binding) {
    if (snapshot_.focused && !snapshot_.physical_binding) {
        snapshot_.physical_binding = binding;
    }
}

void InputState::set_analog(std::optional<AnalogControls> analog) {
    if (snapshot_.focused) {
        snapshot_.analog = analog;
    }
}

void InputState::set_focus(bool focused) {
    snapshot_.focused = focused;
    if (!focused) {
        snapshot_.released |= snapshot_.held;
        snapshot_.held = 0;
        snapshot_.pressed = 0;
        snapshot_.any_key_pressed = false;
        snapshot_.text_input.clear();
        snapshot_.physical_binding.reset();
        snapshot_.analog.reset();
    }
}

void InputState::set_pointer(float x, float y, bool inside) {
    snapshot_.pointer_x = x;
    snapshot_.pointer_y = y;
    snapshot_.pointer_inside = inside;
}

void InputState::request_quit() {
    snapshot_.quit_requested = true;
}

const InputSnapshot& InputState::snapshot() const {
    return snapshot_;
}

void TimedActions::reset(std::uint64_t timestamp_ns, bool focused) {
    pending_.clear();
    state_ = InputState{};
    state_.set_focus(focused);
    state_.begin_poll(timestamp_ns);
    consumed_ns_ = timestamp_ns;
    queued_held_ = 0;
    queued_analog_.reset();
    queued_focused_ = focused;
}

void TimedActions::enqueue(Transition transition) {
    transition.timestamp_ns = std::max(transition.timestamp_ns, consumed_ns_);
    const auto position = std::upper_bound(pending_.begin(), pending_.end(), transition.timestamp_ns, [](std::uint64_t timestamp, const Transition& queued) {
        return timestamp < queued.timestamp_ns;
    });
    pending_.insert(position, transition);
}

void TimedActions::set_action(std::uint64_t timestamp_ns, Action action, bool down) {
    enqueue({timestamp_ns, action, down, Kind::action, std::nullopt});
    if (!down) {
        queued_held_ &= ~action_mask(action);
    } else if (queued_focused_) {
        queued_held_ |= action_mask(action);
    }
}

void TimedActions::set_focus(std::uint64_t timestamp_ns, bool focused) {
    enqueue({timestamp_ns, Action::left, focused, Kind::focus, std::nullopt});
    queued_focused_ = focused;
    if (!focused) {
        queued_held_ = 0;
        queued_analog_.reset();
    }
}

void TimedActions::set_analog(std::uint64_t timestamp_ns, std::optional<AnalogControls> analog) {
    enqueue({timestamp_ns, Action::left, false, Kind::analog, analog});
    if (queued_focused_) {
        queued_analog_ = analog;
    }
}

void TimedActions::synchronize(std::uint64_t timestamp_ns, std::uint32_t held, std::optional<AnalogControls> analog) {
    if (!queued_focused_) {
        held = 0;
        analog.reset();
    }
    constexpr std::array actions{Action::left, Action::right, Action::up, Action::down, Action::confirm, Action::back, Action::pointer_primary, Action::backspace, Action::gear_up,
        Action::gear_down, Action::camera_next, Action::camera_rear, Action::camera_trackside, Action::horn};
    for (const auto action : actions) {
        const auto mask = action_mask(action);
        if (((queued_held_ ^ held) & mask) != 0) {
            set_action(timestamp_ns, action, (held & mask) != 0);
        }
    }
    if (queued_analog_ != analog) {
        set_analog(timestamp_ns, analog);
    }
}

InputSnapshot TimedActions::take(std::uint64_t deadline_ns) {
    if (deadline_ns < consumed_ns_) {
        throw std::logic_error("Gameplay input deadlines must be monotonic");
    }
    state_.begin_poll(deadline_ns);
    while (!pending_.empty() && pending_.front().timestamp_ns <= deadline_ns) {
        const auto transition = pending_.front();
        pending_.pop_front();
        if (transition.kind == Kind::focus) {
            state_.set_focus(transition.value);
        } else if (transition.kind == Kind::analog) {
            state_.set_analog(transition.analog);
        } else {
            state_.set_action(transition.action, transition.value);
        }
    }
    consumed_ns_ = deadline_ns;
    return state_.snapshot();
}
} // namespace toca
