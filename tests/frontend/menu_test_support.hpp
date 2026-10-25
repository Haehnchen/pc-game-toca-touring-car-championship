#pragma once

#include "frontend/controller.hpp"

#include <iostream>
#include <stdexcept>

namespace toca::frontend::test {
inline void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

template <class Function> void rejects(Function function, const char* reason) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, reason);
}

// Deliver one fresh edge or physical binding without carrying held input or
// advancing additional time. Timing-sensitive checks still pass their own clocks.
class MenuInput {
    Controller& controller_;

public:
    explicit MenuInput(Controller& controller) : controller_(controller) {}

    std::vector<Cue> operator()(Action action) const {
        InputSnapshot event;
        event.pressed = action_mask(action);
        return controller_.update(event, {0, 1});
    }

    std::vector<Cue> capture(PhysicalBinding binding, bool focused = true) const {
        InputSnapshot event;
        event.physical_binding = binding;
        event.focused = focused;
        return controller_.update(event, {0, 1});
    }

    std::vector<Cue> wait(std::uint64_t elapsed_ns, bool focused = true) const {
        InputSnapshot event;
        event.focused = focused;
        return controller_.update(event, {0, elapsed_ns});
    }

    void text(std::string value) const {
        InputSnapshot event;
        event.text_input = std::move(value);
        (void)controller_.update(event, {0, 1});
    }
};

template <class Function> int run(Function function) {
    try {
        function();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace toca::frontend::test
