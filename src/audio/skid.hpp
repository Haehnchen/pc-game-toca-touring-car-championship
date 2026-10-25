#pragma once

#include "core/random.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace toca::audio {
struct SourceSkidState {
    std::uint8_t surface = 0, mask = 0;
    std::int32_t reset = 0;
};

enum class SourceSkidAction : std::uint8_t {
    ignore,
    stop,
    update
};

struct SourceSkidDecision {
    std::int32_t sample = -1;
    SourceSkidAction action = SourceSkidAction::ignore;
    bool restart = false, fade = false;
};

// 424d50 up to its spatial query. Sample selection precedes distance, gain,
// voice availability and (for grass) the non-human rejection.
inline SourceSkidDecision source_skid_event(SourceSkidState& state, std::int16_t kind, std::uint32_t wheel, bool human, bool enabled, SourceRandom* random) {
    if (wheel >= 4 || state.surface > 3 || (kind != 7 && kind != 8 && kind != 9)) {
        throw std::runtime_error("Source skid dispatch requires wheel0..3, surface0..3 and event7..9");
    }
    SourceSkidDecision result;
    if (!enabled) {
        return result;
    }
    const auto bit = std::uint8_t(1u << wheel);
    if (kind == 9) {
        if ((state.mask & bit) != 0) {
            state.mask &= std::uint8_t(~bit);
            if (state.mask == 0) {
                result.action = SourceSkidAction::stop;
            }
        }
        return result;
    }
    const auto previous = state.mask;
    state.mask |= bit;
    if (kind == 7 && previous != 0 && state.reset == 0) {
        return result;
    }
    result.restart = state.reset != 0 || kind == 7;
    result.fade = result.restart;
    if (result.restart) {
        state.reset = 0;
    }
    const auto draw = [&] {
        if (!random) {
            throw std::runtime_error("Driving skids require the shared original random stream");
        }
        return random->next();
    };
    switch (state.surface) {
        case 0:
            result.sample = 39 + std::int32_t(draw() % 6);
            break;
        case 1:
            return result;
        case 2:
            result.sample = 47 + std::int32_t(draw() & 1);
            if (!human) {
                return result;
            }
            break;
        case 3:
            result.sample = 49; //424ec3 assigns this before the ownership test.
            if (!human) {
                return result;
            }
            break;
    }
    result.action = SourceSkidAction::update;
    return result;
}

inline SourceSkidDecision source_skid_event(SourceSkidState& state, std::int16_t kind, std::uint32_t wheel, bool human, bool enabled, SourceRandom& random) {
    return source_skid_event(state, kind, wheel, human, enabled, &random);
}

struct SourceSkidActor {
    std::uint8_t identity = 0;
    bool human = false;
    SourceSkidState state;
    std::array<std::int16_t, 4> events{-1, -1, -1, -1};
    std::array<std::int32_t, 4> strengths{};
    std::array<std::uint8_t, 4> wheel_kinds{};
};

struct SourceSkidDispatch {
    std::uint32_t actor = 0, wheel = 0;
    std::int16_t kind = -1;
    SourceSkidDecision decision;
    std::uint32_t rng = 0;
    SourceSkidState state;
};

//44c540 consumes previously prepared44b690 queues in ascending slot/wheel
//order. Queue production and view callbacks are separate source boundaries.
inline std::vector<SourceSkidDispatch> dispatch_source_skids(std::span<SourceSkidActor> actors, bool enabled, SourceRandom& random) {
    std::vector<SourceSkidDispatch> result;
    result.reserve(actors.size() * 4);
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        auto& input = actors[actor];
        if (input.identity != actor) {
            throw std::runtime_error("Source skid actor identity disagrees with its fleet slot");
        }
        for (std::uint32_t wheel = 0; wheel < 4; ++wheel) {
            const auto kind = input.events[wheel];
            if (kind != -1) {
                const auto decision = source_skid_event(input.state, kind, wheel, input.human, enabled, random);
                result.push_back({std::uint32_t(actor), wheel, kind, decision, random.state(), input.state});
            }
        }
    }
    return result;
}
} // namespace toca::audio
