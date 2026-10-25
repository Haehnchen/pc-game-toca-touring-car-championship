#pragma once

#include "audio/skid.hpp"

#include <algorithm>

namespace toca::audio {
struct SourceCameraMix {
    std::int32_t primary = 0, secondary = 1;
};

//4257d0: source views0/3 and showcase use the third mix; invalid views retain
//the preceding context. Output muting does not disable this state transaction.
inline void select_source_camera_mix(SourceCameraMix& mix, std::int32_t view, bool showcase) {
    if (showcase || view == 0 || view == 3) {
        mix = {0, 2};
    } else if (view == 2) {
        mix = {0, 1};
    } else if (view == 1 || view == 4 || view == 5) {
        mix = {1, 0};
    }
}

struct SourceMaterialDecision {
    bool admitted = false, changed = false, fade = false;
    std::uint8_t current = 0;
};

//425400 through the material/restart transaction, before playback. The
//descending wheel order belongs to its ground caller, not this single event.
inline SourceMaterialDecision advance_source_material(SourceSkidState& state, std::array<std::uint8_t, 4>& wheels, std::uint32_t wheel, std::uint32_t incoming, bool enabled,
    bool human, bool wet, SourceCameraMix& mix, std::int32_t view, bool showcase) {
    if (wheel >= 4 || (incoming > 3 && incoming != 16)) {
        throw std::runtime_error("Source material event requires wheel0..3 and material0..3/16");
    }
    SourceMaterialDecision result;
    if (!enabled || !human) {
        return result;
    }
    result.admitted = true;
    const auto previous = wheels[wheel];
    const auto current = incoming == 16 || incoming == 1 ? previous : std::uint8_t(incoming);
    result.current = current;
    select_source_camera_mix(mix, view, showcase);
    if (current != previous) {
        const auto lower = current < previous || (wet && previous == 0 && mix.primary == 0);
        if (lower) {
            wheels[wheel] = current;
            result.changed = current != state.surface && std::ranges::all_of(wheels, [current](auto value) {
                return value == current;
            });
        } else {
            result.changed = current != state.surface && std::ranges::find(wheels, current) != wheels.end();
            wheels[wheel] = current;
        }
        if (result.changed) {
            result.fade = lower || state.surface == 3 || (state.surface == 2 && current == 0);
            state.surface = current;
            state.reset = 1;
        }
    }
    return result;
}
} // namespace toca::audio
