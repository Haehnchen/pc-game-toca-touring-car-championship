#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace toca::test::scene_models {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct TrackFixture {
    const char* name;
    std::uint32_t loading_member;
    std::array<std::int32_t, 3> light_direction_q16;
    std::uint32_t default_weather;
};

// Independent expected values from 439820/44eda9/49da90 and 4512b0.
// Lavaland has no 44eda9 assignment: retain the executable's fresh-start light.
inline constexpr std::array<TrackFixture, 10> tracks{{
    {"donington", 3, {24300, 48600, 36450}, 2},
    {"silverstone", 6, {28800, 57600, -11520}, 5},
    {"thruxton", 8, {33675, 44900, 33675}, 2},
    {"brands-hatch", 1, {-31440, 52400, -23580}, 2},
    {"oulton-park", 5, {-27750, 37000, -46250}, 1},
    {"donington-short", 9, {24300, 48600, 36450}, 3},
    {"croft", 2, {43100, 43100, 23705}, 2},
    {"knockhill", 4, {-87325, 99800, 54890}, 1},
    {"snetterton", 7, {-30675, 40900, 40900}, 2},
    {"lavaland", 10, {37834, 37834, 37834}, 1},
}};

inline void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void put16(Bytes& bytes, std::size_t position, std::uint16_t value) {
    bytes.at(position) = std::uint8_t(value);
    bytes.at(position + 1) = std::uint8_t(value >> 8);
}

inline void put32(Bytes& bytes, std::size_t position, std::uint32_t value) {
    put16(bytes, position, std::uint16_t(value));
    put16(bytes, position + 2, std::uint16_t(value >> 16));
}

} // namespace toca::test::scene_models
