#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace toca::frontend::cheats {
enum class Code : std::uint32_t {
    micro,
    chun,
    stars,
    toon,
    mayhem,
    low_gravity,
    rain_up,
    cat_dog,
    no_hits,
    upside,
    handy,
    codies,
    disco,
    follow,
    lock,
    bonus_cars,
    copter,
    boost,
    all_tracks,
    impossible,
    garage,
    patscreem
};

enum class Category {
    unlocks,
    camera,
    effects,
    racing
};

struct Overrides {
    bool bonus_cars = false;
    bool all_tracks = false;
    bool bonus_track = false;
    auto operator<=>(const Overrides&) const = default;
};

struct Progress {
    bool special_championship = false;
    bool bonus_car_8 = false;
    bool bonus_car_9 = false;
    std::uint32_t track_threshold = 1;
    auto operator<=>(const Progress&) const = default;
};

struct State {
    Overrides overrides;
    Progress progress;
    // Original action codes toggle these fields even though their progress
    // writes run on both redemptions. They determine activation speech only.
    bool lock_code = false;
    bool garage_code = false;
    // Other original +48..+9c code flags. The four fields above and in
    // Overrides remain authoritative for lock/garage/bonus_cars/all_tracks.
    std::array<bool, 22> flags{};
    auto operator<=>(const State&) const = default;
};

struct Entry {
    Code code;
    std::string_view text;
    // Whether the corresponding current runtime consumer is complete.
    // allowed(Code) separately describes original code redemption.
    bool supported;
};

struct Result {
    bool recognized = false;
    bool accepted = false;
    bool activated = false;
    bool changed = false;
};

std::span<const Entry> catalog();
std::span<const Code> category_codes(Category category);
std::string_view code_text(Code code);
std::optional<Code> find_code(std::string_view text);
bool effect_ready(Code code);
bool active(const State& state, Code code);
bool allowed(Code code, std::uint32_t mode = 0);
State for_mode(const State& state, std::uint32_t mode);
Result apply(State& state, Code code, std::uint32_t mode = 0);
bool menu_active(const State& state, Code code);
Result set_menu_active(State& state, Code code, bool enabled);
Result redeem(State& state, std::string_view text, std::uint32_t mode = 0);
bool car_available(const State& state, std::uint32_t car_id);
bool showdown_available(const State& state);
bool track_available(const State& state, std::uint32_t track_id);
std::uint32_t track_max_id(const State& state);
} // namespace toca::frontend::cheats
