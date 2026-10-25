#pragma once

#include "audio/skid.hpp"

#include <algorithm>
#include <bit>

namespace toca::audio {
struct SourceWheelSlip {
    std::int32_t current = 0, strength = 0x8000;
};

//44b690 uses wrapped signed32 arithmetic for the absolute slip sum and Q16
//multiply. Only airborne byte1 disables the wheel.
inline SourceWheelSlip source_wheel_slip(std::int32_t longitudinal, std::int32_t lateral, std::uint8_t airborne) {
    const auto absolute = [](std::int32_t value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        return value < 0 ? 0u - bits : bits;
    };
    const auto amount = std::bit_cast<std::int32_t>(absolute(longitudinal) + absolute(lateral));
    if (airborne == 1 || amount < 0x4ccd) {
        return {};
    }
    const auto strength = std::bit_cast<std::int32_t>((std::uint32_t(amount) - 0x4cccu) * 6u);
    return {1, std::min(strength, 0x10000)};
}

struct SourceWheelInput {
    std::array<std::int32_t, 4> longitudinal_slip{}, lateral_slip{};
    std::array<std::uint8_t, 4> airborne{};
};

struct SourceWheelQueueState {
    std::array<std::int32_t, 4> current{}, previous{};
};

struct SourceWheelQueue {
    std::array<std::int16_t, 4> events{-1, -1, -1, -1};
    std::array<std::int32_t, 4> strengths{};
};

struct SourceWheelQueueActor {
    std::uint8_t identity = 0;
    bool human = false;
    std::uint8_t visibility = 0;
    SourceWheelInput input;
    SourceWheelQueueState state;
    SourceWheelQueue queue;
    std::int32_t pending_mute = 0;
};

struct SourceWheelQueueContext {
    std::uint32_t view = 0, local_players = 1;
    bool disabled = false;
};

inline void produce_source_wheel_queue(SourceWheelQueueActor& actor) {
    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
        actor.state.previous[wheel] = actor.state.current[wheel];
        const auto slip = source_wheel_slip(actor.input.longitudinal_slip[wheel], actor.input.lateral_slip[wheel], actor.input.airborne[wheel]);
        actor.state.current[wheel] = slip.current;
        actor.queue.strengths[wheel] = slip.strength;
        actor.queue.events[wheel] = slip.current == 0 ? 9 : actor.state.previous[wheel] == 0 ? 7 : 8;
    }
}

//446b50's numerical wheel transaction. Render transforms, segment geometry and
//frame callback scheduling remain outside this boundary.
inline std::vector<std::uint32_t> prepare_source_wheel_queues(std::span<SourceWheelQueueActor> actors, const SourceWheelQueueContext& context = {}) {
    if (actors.empty() || actors.size() > 16 || context.local_players < 1 || context.local_players > 2 || context.view >= context.local_players || context.view >= actors.size()) {
        throw std::runtime_error("Source wheel queues require an active original fleet/view");
    }
    if (context.view == 0) {
        for (auto& actor : actors) {
            actor.queue.events.fill(-1);
        }
    }
    std::vector<std::uint32_t> order;
    if (context.disabled) {
        return order;
    }
    for (std::size_t slot = 0; slot < actors.size(); ++slot) {
        auto& actor = actors[slot];
        if (actor.identity != slot) {
            throw std::runtime_error("Source wheel queue actor identity disagrees with its fleet slot");
        }
        if (context.local_players != 1 && slot != context.view) {
            continue;
        }
        if (context.local_players == 1 && actor.visibility == 0 && !actor.human) {
            actor.pending_mute = 1; //425e70;25e90 settles this after frame audio.
            for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                if (actor.state.current[wheel] != 0) {
                    actor.state.current[wheel] = actor.state.previous[wheel] = 0; //44c430.
                }
            }
            continue;
        }
        produce_source_wheel_queue(actor);
        order.push_back(std::uint32_t(slot));
    }
    return order;
}

//25e90 clears a marked channel's mask and active flag, preserving material and
//the separate4d39f8 force-restart state. Return whether playback must stop.
inline bool settle_source_skid_mute(std::int32_t& pending, SourceSkidState& state) {
    if (pending != 1) {
        return false;
    }
    pending = 0;
    const auto stop = state.mask != 0;
    state.mask = 0;
    return stop;
}
} // namespace toca::audio
