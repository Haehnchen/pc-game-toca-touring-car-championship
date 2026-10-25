#pragma once

#include "core/random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace toca::audio {
struct SourceEngineState {
    std::int32_t previous_gear = -1;
    std::uint32_t phase = 0;
    std::int32_t shift = 0, whine_throttle = 0, throttle = 0, throttle_mirror = 0, whine_volume = 0;
};

struct SourceEngineValues {
    std::int32_t rpm = 2000, speed = 0, gear = 0, throttle = 0, flag0 = 0, flag1 = 0;
};

struct SourceEngineCall {
    SourceEngineValues values;
    bool enabled = true, bonus = false, playing = true;
    std::uint32_t local_players = 1;
    std::int32_t mix = 0x5999;
};

struct SourceWhineCurve {
    std::int32_t minimum = 0, maximum = 193;
    std::span<const std::uint16_t> rates;
    std::span<const std::uint8_t> gains;
    std::span<const std::int32_t, 24> offsets;
};

struct SourceWhineDecision {
    bool reached = false, rate_written = false, volume_written = false;
    std::int32_t offset = 0, index = -1, volume = 0;
    std::uint32_t rate = 11025;
};

inline std::int32_t source_engine_multiply(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::uint32_t(std::uint64_t(std::int64_t(first) * second) >> 16));
}

//44a6f0's state conversion after its unrelated HUD smoothing calculations.
inline SourceEngineValues source_session_engine_values(SourceEngineValues values, std::int32_t session_state) {
    if (session_state != 4 && session_state != 6 && session_state != 7 && session_state != 9 && session_state != 10) {
        values.rpm = 2000;
        values.speed = values.gear = values.throttle = 0;
    }
    if (session_state == 9) {
        values.rpm = std::max(2000, (values.rpm / 5) * 4);
        values.speed = std::min(260, values.rpm / 100 + 100);
        values.gear = 1;
    }
    return values;
}

inline SourceEngineValues source_engine_caller(
    std::int32_t engine_speed, std::int32_t rpm_scale, std::int32_t throttle, std::int32_t gear, std::int32_t speed_q16, std::int32_t session_state) {
    SourceEngineValues values;
    values.rpm = std::clamp(2000 + (source_engine_multiply(source_engine_multiply(engine_speed, rpm_scale), 0x19640000) >> 16), 2000, 8500);
    const auto raw_throttle = source_engine_multiply(throttle, 0x640000);
    values.throttle = raw_throttle < 50 ? 0 : std::min(raw_throttle, 100);
    values.gear = gear;
    values.speed = std::min(speed_q16 >> 16, 260);
    return source_session_engine_values(values, session_state);
}

//414240 before14780: the gate, normalization and shift decrement precede the
//split-player return;14740's throttle arithmetic occurs only for one local.
inline bool prepare_source_engine_update(SourceEngineState& state, SourceEngineCall& call) {
    if (!call.enabled) {
        return false;
    }
    if (call.values.gear < 0 || call.values.gear > 6) {
        call.values.gear = 1;
    }
    if (state.shift != 0) {
        state.shift = std::bit_cast<std::int32_t>(std::uint32_t(state.shift) - 1u);
    }
    if (call.local_players != 1) {
        return false;
    }
    state.throttle += std::clamp(call.values.throttle - state.throttle, -10, 20);
    state.throttle_mirror = state.throttle;
    return true;
}

inline SourceWhineDecision advance_source_whine(SourceEngineState& state, const SourceEngineCall& call, const SourceWhineCurve& curve, SourceRandom& random) {
    SourceWhineDecision result;
    if (call.bonus) {
        return result;
    }
    if (curve.rates.size() < 2 || curve.rates.size() != curve.gains.size() || curve.maximum <= curve.minimum) {
        throw std::runtime_error("WHINE requires its initialized original curve");
    }
    result.reached = true;
    const auto gear = call.values.gear, speed = call.values.speed;
    if (state.previous_gear != gear) {
        state.shift = 2;
        if (state.previous_gear < gear) {
            state.phase = random.next() > 0x5ffe ? 0u : 25u;
        }
    }
    if (gear > 0 && gear < 5) {
        ++state.phase;
        const auto phase = std::bit_cast<std::int32_t>(state.phase);
        if (phase < 24) {
            if (phase < 0) {
                throw std::runtime_error("WHINE phase refers outside its original shift table");
            }
            result.offset = curve.offsets[std::size_t(phase)];
        }
    }
    state.previous_gear = gear;
    const auto silence = [&] {
        result.rate_written = result.volume_written = call.playing;
        result.rate = 11025;
        result.volume = 0;
    };
    if (speed >= curve.maximum || gear == 0) {
        silence();
        return result;
    }
    state.whine_throttle = call.values.throttle == 100 ? std::min(state.whine_throttle + 10, 100) : 0;
    result.index = speed < curve.minimum ? -1 : std::int32_t((std::int64_t(speed - curve.minimum) * std::int64_t(curve.rates.size() - 1)) / (curve.maximum - curve.minimum));
    if (gear < 5) {
        const auto product = std::bit_cast<std::int32_t>(std::uint32_t(result.offset) * std::uint32_t(result.index));
        const auto high = std::int32_t((std::int64_t(product) * -0x51eb851f) >> 32);
        result.index = std::max(result.index + (high >> 5) - (high >> 31), -1);
    } else {
        result.offset = 0;
    }
    if (result.index == -1) {
        silence();
        return result;
    }
    if (!call.playing) {
        if (state.whine_volume != 0) {
            state.whine_volume = std::max(state.whine_volume - 10, 0);
        }
        return result;
    }
    if (std::size_t(result.index) >= curve.rates.size()) {
        throw std::runtime_error("WHINE modulation refers outside its initialized curve");
    }
    result.rate_written = true;
    result.rate = curve.rates[std::size_t(result.index)];
    if (call.values.flag0 == 0 && call.values.flag1 == 0) {
        auto volume = std::int32_t(curve.gains[std::size_t(result.index)]) - result.offset;
        if (volume < 0) {
            volume = 10;
        }
        volume = (((volume * state.whine_throttle) / 100) * call.mix) >> 16;
        if (state.shift != 0) {
            volume = std::max(volume - 20, 0);
        }
        state.whine_volume = result.volume = volume;
        result.volume_written = true;
    }
    return result;
}
} // namespace toca::audio
