#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace toca::audio::detail {
// Track ownership and its game-side controls always have the same lifetime.
template <class Track> struct VoiceSlot {
    Track track;
    std::uint64_t serial = 0;
    float gain = 0;
    MIX_StereoGains stereo{1, 1};
    std::optional<std::uint32_t> original_volume{};
};

// The factory returns an owned track only after backend initialization succeeds.
// Failed growth destroys staged tracks and leaves every existing slot intact.
template <class Track, class Create> void resize_voice_pool(std::vector<VoiceSlot<Track>>& pool, std::size_t count, Create&& create) {
    using Slot = VoiceSlot<Track>;
    static_assert(std::is_nothrow_move_constructible_v<Slot> && std::is_nothrow_move_assignable_v<Slot>);
    if (count <= pool.size()) {
        pool.erase(pool.begin() + std::ptrdiff_t(count), pool.end());
        return;
    }
    std::vector<Slot> staged;
    staged.reserve(count - pool.size());
    while (staged.size() < count - pool.size()) {
        staged.push_back(Slot{create()});
    }
    // Reserve before the first mutation; all following moves are nonthrowing.
    pool.reserve(count);
    for (auto& slot : staged) {
        pool.push_back(std::move(slot));
    }
}
} // namespace toca::audio::detail
