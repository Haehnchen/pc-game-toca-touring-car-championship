#pragma once

#include <cstddef>
#include <cstdint>

namespace toca::network {
using PlayerId = std::uint32_t;
using ActorId = std::uint8_t;
using Generation = std::uint32_t;
using MonotonicMillis = std::uint64_t;

// Transport identities belong to an endpoint, never to the game roster.
enum class ConnectionId : std::uint32_t {
    invalid = 0
};
enum class Channel : std::uint8_t {
    control = 0,
    input = 1,
    latest_state = 2
};

inline constexpr std::size_t max_players = 8;
inline constexpr std::size_t max_message_bytes = 1024;
inline constexpr std::size_t max_reliable_bytes = 64 * 1024;
} // namespace toca::network
