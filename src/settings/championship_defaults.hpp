#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace toca::championship_defaults {
enum Award : std::uint32_t {
    none = 0,
    bronze = 1,
    silver = 2,
    gold = 3
};

struct Record {
    std::string_view driver;
    std::uint32_t points, round;
    std::array<std::uint32_t, 4> awards;
};

// Preserve descending points and the established tie order of the nine defaults.
inline constexpr std::array<Record, 9> records{{
    {.driver = "PATRICK", .points = 225, .round = 24, .awards = {gold, gold, silver, bronze}},
    {.driver = "BRYAN", .points = 225, .round = 23, .awards = {gold, silver, bronze, none}},
    {.driver = "LES", .points = 171, .round = 18, .awards = {silver, silver, none, none}},
    {.driver = "IAIN", .points = 144, .round = 15, .awards = {silver, bronze, none, none}},
    {.driver = "ANDY", .points = 114, .round = 12, .awards = {silver, none, none, none}},
    {.driver = "PHIL", .points = 83, .round = 9, .awards = {silver, none, none, none}},
    {.driver = "SCOTT", .points = 62, .round = 7, .awards = {bronze, none, none, none}},
    {.driver = "JON", .points = 30, .round = 4, .awards = {none, none, none, none}},
    {.driver = "RICH", .points = 6, .round = 2, .awards = {none, none, none, none}},
}};
} // namespace toca::championship_defaults
