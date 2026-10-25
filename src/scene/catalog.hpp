#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace toca::scene {
struct TrackContent {
    std::string_view archive_prefix;
    std::uint32_t loading_image;
    std::array<std::int32_t, 3> light_direction_q16;
    std::uint32_t music_track;
};

// Original track IDs: 44e717/44e673/44e5cf archives, 439820 loading image,
// 44eda9/49da90 fixed light direction and 426ec0 Single Race CD table.
inline constexpr std::array<TrackContent, 10> track_catalog{{
    {"data/donl", 3, {24300, 48600, 36450}, 3},
    {"data/silv", 6, {28800, 57600, -11520}, 4},
    {"data/thru", 8, {33675, 44900, 33675}, 5},
    {"data/bran", 1, {-31440, 52400, -23580}, 6},
    {"data/oult", 5, {-27750, 37000, -46250}, 3},
    {"data/dons", 9, {24300, 48600, 36450}, 4},
    {"data/crof", 2, {43100, 43100, 23705}, 5},
    {"data/knoc", 4, {-87325, 99800, 54890}, 6},
    {"data/snet", 7, {-30675, 40900, 40900}, 3},
    // No case9 in 44eda9: retain the compiled initial light deterministically,
    // instead of inheriting the previous scene's mutated global light position.
    {"data/land", 10, {37834, 37834, 37834}, 4},
}};

constexpr bool track_supported(std::uint32_t track_id) {
    return track_id < track_catalog.size();
}

constexpr const TrackContent& track_content(std::uint32_t track_id) {
    if (!track_supported(track_id)) {
        throw std::runtime_error("Track scene content is not implemented");
    }
    return track_catalog[track_id];
}
} // namespace toca::scene
