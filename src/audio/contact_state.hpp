#pragma once

#include "audio/material_state.hpp"
#include "audio/spatial.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace toca::audio {
struct SourceContactListener {
    SpatialListener spatial;
    std::int32_t view = 2;
    bool showcase = false;
};

//424070 loads camera+cc/d0/d4 as floats, multiplies by the initialized float
//scale and rounds with FISTP. Positions and heading are observed camera inputs.
inline SourceContactListener source_contact_listener(
    const std::array<std::uint32_t, 3>& position, std::uint32_t scale, std::int16_t heading, bool stereo, std::int32_t view, bool showcase) {
    SourceContactListener result{{{}, heading, stereo}, view, showcase};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto value = std::nearbyint(double(std::bit_cast<float>(position[axis])) * double(std::bit_cast<float>(scale)));
        if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Source contact camera position exceeds signed Q16 coordinates");
        }
        result.spatial.position_q16[axis] = std::int32_t(value);
    }
    return result;
}

struct SourceContactGains {
    //244410 category4/6 in internal, external, third camera bank order.
    std::array<std::int32_t, 3> local{0x10000, 0xa666, 0x9999}, remote{0, 0x7333, 0x6666};
};

inline std::int32_t source_contact_gain(const SourceCameraMix& mix, const SourceContactGains& gains, bool local) {
    const auto& bank = local ? gains.local : gains.remote;
    return mix.primary != 0 ? bank[0] : mix.secondary == 1 ? bank[1] : mix.secondary == 2 ? bank[2] : 0;
}

inline std::int32_t source_contact_multiply(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::uint32_t(std::uint64_t(std::int64_t(first) * second) >> 16));
}

struct SourceContactSpatial {
    bool accepted = false;
    std::int32_t volume = 0;
    std::uint32_t pan = 64;
    std::uint16_t angle = 0;
};

//424b50 selects the loudest listener, including the later listener on ties.
//424070 refreshes the shared camera mix even for rejected spatial queries.
inline SourceContactSpatial source_contact_spatial(
    const SpatialVector& position, std::int32_t strength, std::span<const SourceContactListener> listeners, SourceCameraMix& camera, bool half_distance = false) {
    if (listeners.empty() || listeners.size() > 4) {
        throw std::runtime_error("Source contact spatial query requires one to four active listeners");
    }
    SourceContactSpatial best;
    for (const auto& listener : listeners) {
        select_source_camera_mix(camera, listener.view, listener.showcase);
        const auto spatial = source_spatial(position, listener.spatial, half_distance);
        const auto volume = spatial.in_range ? std::clamp(source_contact_multiply(spatial.fade_q16, strength) >> 16, 0, 100) : 0;
        if (volume >= best.volume) {
            best = {volume > 0, volume, spatial.pan, spatial.angle};
        }
    }
    return best;
}

struct SourceImpactInput {
    SpatialVector position{};
    std::uint32_t kind = 1;
    std::int32_t strength = 0;
    bool local = true, enabled = true;
};

struct SourceImpactDecision {
    bool threshold = false, spatial = false, drew = false;
    std::int32_t sample = -1, spatial_volume = 0, volume = 0;
    std::uint32_t pan = 64;
    std::uint16_t angle = 0;
};

//424860 through sample/gain selection, before external voice allocation.
//Thunder25 bypasses spatial processing; all other random variants are drawn
//after spatial admission and before zero gain or a full voice pool can reject.
inline SourceImpactDecision source_impact_event(
    const SourceImpactInput& input, std::span<const SourceContactListener> listeners, SourceCameraMix& camera, const SourceContactGains& gains, SourceRandom* random) {
    SourceImpactDecision result;
    if (!input.enabled) {
        return result;
    }
    if (input.kind < 1 || input.kind > 27) {
        throw std::runtime_error("Source impact event requires kind1..27");
    }
    const auto strength = input.kind == 1 ? source_contact_multiply(input.strength, 0xe666) : input.strength;
    if (strength <= 0xf5b) {
        return result;
    }
    result.threshold = true;
    const auto draw = [&] {
        if (!random) {
            throw std::runtime_error("Source contact variation requires the shared original random stream");
        }
        result.drew = true;
        return random->next();
    };
    if (input.kind == 25) {
        result.sample = 76 + std::int32_t(draw() & 1u);
        result.volume = std::bit_cast<std::int32_t>(std::uint32_t(strength) * 100u); //4798cb clamps after this unshifted multiplication.
        return result;
    }
    const auto amplified = input.kind == 1 || input.kind == 17 || input.kind == 26 || input.kind == 27;
    const auto spatial =
        source_contact_spatial(input.position, amplified ? std::bit_cast<std::int32_t>(std::uint32_t(strength) << 4) : strength, listeners, camera, input.kind == 27);
    result.spatial = spatial.accepted;
    result.spatial_volume = spatial.volume;
    result.pan = spatial.pan;
    result.angle = spatial.angle;
    if (!spatial.accepted) {
        return result;
    }
    switch (input.kind) {
        case 1:
        case 27: {
            const auto band = strength < 0x6666 ? strength : std::bit_cast<std::int32_t>(std::uint32_t(strength) - 0x6666u);
            const auto severity = std::bit_cast<std::int32_t>(std::uint32_t(band) * 10u) >> 16;
            result.sample = strength < 0x6666 ? std::min(severity + 33, 36) : std::min(severity + 25, 30);
            break;
        }
        case 2:
            result.sample = 46;
            break;
        case 3:
            result.sample = 31 + std::int32_t(draw() & 1u);
            break;
        case 4:
            result.sample = 37 + std::int32_t(draw() & 1u);
            break;
        case 5:
            result.sample = 51 + std::int32_t(draw() % 5u);
            break;
        case 6:
            result.sample = 45;
            break;
        case 17:
        case 26:
            result.sample = 64;
            break;
        case 18:
        case 19:
        case 20:
            result.sample = 65;
            break;
        case 21:
            result.sample = 66;
            break;
        default:
            result.sample = std::int32_t(input.kind);
            break;
    }
    const auto gain = source_contact_gain(camera, gains, input.local);
    if (gain != 0) {
        result.volume = std::min(source_contact_multiply(spatial.volume << 16, gain) >> 16, 127);
    }
    return result;
}

enum class SourceScrapeAction : std::uint8_t {
    ignore,
    play,
    update,
    stop
};

struct SourceScrapeInput {
    SpatialVector position{};
    std::int32_t handle = -1, strength = 0;
    std::uint32_t kind = 10;
    bool enabled = true, playing = false;
};

struct SourceScrapeDecision {
    bool spatial = false, drew = false;
    std::int32_t sample = -1, spatial_volume = 0, volume = 0;
    std::uint32_t pan = 64;
    std::uint16_t angle = 0;
    SourceScrapeAction action = SourceScrapeAction::ignore;
};

//4250a0: every enabled valid-handle kind10 draws after its spatial query, even
//when rejected or already playing. Kind11 updates; kind12 stops without a draw.
inline SourceScrapeDecision source_scrape_event(
    const SourceScrapeInput& input, std::span<const SourceContactListener> listeners, SourceCameraMix& camera, const SourceContactGains& gains, SourceRandom& random) {
    SourceScrapeDecision result;
    if (!input.enabled || input.handle == -1) {
        return result;
    }
    if (input.handle < 0 || input.handle >= 4 || input.kind < 10 || input.kind > 12) {
        throw std::runtime_error("Source scrape event requires handle0..3 and kind10..12");
    }
    const auto spatial = source_contact_spatial(input.position, std::bit_cast<std::int32_t>(std::uint32_t(input.strength) << 2), listeners, camera);
    result.spatial = spatial.accepted;
    result.spatial_volume = spatial.volume;
    result.pan = spatial.pan;
    result.angle = spatial.angle;
    if (input.kind == 10) {
        (void)random.next();
        result.drew = true;
        result.sample = 50;
    } else if (input.kind == 12) {
        if (input.playing) {
            result.action = SourceScrapeAction::stop;
        }
        return result;
    } else {
        result.sample = 11;
    }
    const auto gain = source_contact_gain(camera, gains, true) / 2;
    if (gain != 0 && spatial.accepted) {
        result.volume = std::min(source_contact_multiply(spatial.volume << 16, gain) >> 16, 100);
        if (result.volume > 0) {
            if (input.kind == 10 && !input.playing) {
                result.action = SourceScrapeAction::play;
            } else if (input.kind == 11 && input.playing) {
                result.action = SourceScrapeAction::update;
            }
        }
    }
    return result;
}
} // namespace toca::audio
