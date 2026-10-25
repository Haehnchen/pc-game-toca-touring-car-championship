#include "frontend/cheats.hpp"

#include <algorithm>

namespace toca::frontend::cheats {
namespace {
constexpr std::array entries{Entry{Code::micro, "CMMICRO", true}, Entry{Code::chun, "CMCHUN", true}, Entry{Code::stars, "CMSTARS", true}, Entry{Code::toon, "CMTOON", true},
    Entry{Code::mayhem, "CMMAYHEM", true}, Entry{Code::low_gravity, "CMLOGRAV", true}, Entry{Code::rain_up, "CMRAINUP", true}, Entry{Code::cat_dog, "CMCATDOG", true},
    Entry{Code::no_hits, "CMNOHITS", true}, Entry{Code::upside, "CMUPSIDE", true}, Entry{Code::handy, "CMHANDY", true}, Entry{Code::codies, "CODIES", true},
    Entry{Code::disco, "CMDISCO", false}, Entry{Code::follow, "CMFOLLOW", true}, Entry{Code::lock, "CMLOCK", true}, Entry{Code::bonus_cars, "GONGOGO", true},
    Entry{Code::copter, "CMCOPTER", true}, Entry{Code::boost, "XBOOSTME", true}, Entry{Code::all_tracks, "JHAMMO", true}, Entry{Code::impossible, "CMIMPOSS", true},
    Entry{Code::garage, "CMGARAGE", true}, Entry{Code::patscreem, "PATSCREEM", true}};
}

std::span<const Entry> catalog() {
    return entries;
}

std::span<const Code> category_codes(Category category) {
    static constexpr std::array unlocks{Code::all_tracks, Code::bonus_cars, Code::patscreem};
    static constexpr std::array camera{Code::micro, Code::chun, Code::handy, Code::follow, Code::copter};
    static constexpr std::array effects{Code::stars, Code::toon, Code::upside, Code::low_gravity, Code::rain_up, Code::cat_dog};
    static constexpr std::array racing{Code::boost, Code::mayhem, Code::no_hits, Code::codies, Code::impossible};
    switch (category) {
        case Category::unlocks:
            return unlocks;
        case Category::camera:
            return camera;
        case Category::effects:
            return effects;
        case Category::racing:
            return racing;
    }
    return {};
}

std::string_view code_text(Code code) {
    const auto index = static_cast<std::uint32_t>(code);
    return index < entries.size() ? entries[index].text : std::string_view{};
}

std::optional<Code> find_code(std::string_view text) {
    // The recovered lookup uses strcmp; the name editor supplies uppercase.
    const auto entry = std::ranges::find(entries, text, &Entry::text);
    return entry == entries.end() ? std::nullopt : std::optional(entry->code);
}

bool effect_ready(Code code) {
    const auto index = static_cast<std::uint32_t>(code);
    return index < entries.size() && entries[index].supported;
}

bool active(const State& state, Code code) {
    switch (code) {
        case Code::lock:
            return state.lock_code;
        case Code::garage:
            return state.garage_code;
        case Code::bonus_cars:
            return state.overrides.bonus_cars;
        case Code::all_tracks:
            return state.overrides.all_tracks;
        default: {
            const auto index = static_cast<std::uint32_t>(code);
            return index < state.flags.size() && state.flags[index];
        }
    }
}

bool allowed(Code code, std::uint32_t mode) {
    if (code_text(code).empty() || code == Code::disco) {
        return false;
    }
    if (mode == 1) {
        return code == Code::micro || code == Code::chun || code == Code::handy || code == Code::impossible;
    }
    if (mode == 3) {
        return code == Code::micro || code == Code::chun || code == Code::handy || code == Code::bonus_cars || code == Code::all_tracks;
    }
    return true;
}

State for_mode(const State& state, std::uint32_t mode) {
    auto result = state;
    for (std::uint32_t index = 0; index < result.flags.size(); ++index) {
        if (!allowed(static_cast<Code>(index), mode)) {
            result.flags[index] = false;
        }
    }
    if (!allowed(Code::bonus_cars, mode)) {
        result.overrides.bonus_cars = false;
    }
    if (!allowed(Code::all_tracks, mode)) {
        result.overrides.all_tracks = result.overrides.bonus_track = false;
    }
    if (!allowed(Code::lock, mode)) {
        result.lock_code = false;
    }
    if (!allowed(Code::garage, mode)) {
        result.garage_code = false;
    }
    return result;
}

Result apply(State& state, Code code, std::uint32_t mode) {
    Result result{!code_text(code).empty()};
    if (!result.recognized || !allowed(code, mode)) {
        return result;
    }
    result.accepted = true;
    const auto before = state;
    switch (code) {
        case Code::bonus_cars:
            state.overrides.bonus_cars = !state.overrides.bonus_cars;
            result.activated = state.overrides.bonus_cars;
            break;
        case Code::all_tracks:
            state.overrides.all_tracks = !state.overrides.all_tracks;
            result.activated = state.overrides.all_tracks;
            break;
        case Code::garage:
            state.garage_code = !state.garage_code;
            state.progress.bonus_car_8 = true;
            state.progress.bonus_car_9 = true;
            result.activated = state.garage_code;
            break;
        case Code::lock:
            state.lock_code = !state.lock_code;
            state.progress = {};
            result.activated = state.lock_code;
            break;
        default:
            state.flags[static_cast<std::uint32_t>(code)] = !active(state, code);
            result.activated = active(state, code);
            if (code == Code::patscreem) {
                // 476bfb performs this write on both flag transitions.
                state.progress.special_championship = true;
            }
            break;
    }
    result.changed = state != before;
    return result;
}

bool menu_active(const State& state, Code code) {
    // The direct track control includes Lavaland, unlike typed JHAMMO.
    return code == Code::all_tracks ? state.overrides.all_tracks && state.overrides.bonus_track : active(state, code);
}

Result set_menu_active(State& state, Code code, bool enabled) {
    bool listed = false;
    for (const auto category : {Category::unlocks, Category::camera, Category::effects, Category::racing}) {
        listed |= std::ranges::find(category_codes(category), code) != category_codes(category).end();
    }
    Result result{listed};
    if (!listed) {
        return result;
    }
    result.accepted = true;
    const auto before = state;
    if (code == Code::all_tracks) {
        state.overrides.all_tracks = enabled;
        state.overrides.bonus_track = enabled;
    } else if (code == Code::bonus_cars) {
        state.overrides.bonus_cars = enabled;
    } else {
        state.flags[static_cast<std::uint32_t>(code)] = enabled;
    }
    result.changed = state != before;
    result.activated = enabled && result.changed;
    return result;
}

Result redeem(State& state, std::string_view text, std::uint32_t mode) {
    const auto code = find_code(text);
    return code ? apply(state, *code, mode) : Result{};
}

bool car_available(const State& state, std::uint32_t car_id) {
    if (car_id < 8) {
        return true;
    }
    return car_id < 10 && (state.overrides.bonus_cars || (car_id == 8 ? state.progress.bonus_car_8 : state.progress.bonus_car_9));
}

bool showdown_available(const State& state) {
    return state.progress.special_championship || active(state, Code::patscreem);
}

bool track_available(const State& state, std::uint32_t track_id) {
    if (track_id >= 10 || (active(state, Code::copter) && track_id != 1 && track_id != 3 && track_id != 4 && track_id != 7 && track_id != 8)) {
        return false;
    }
    return state.overrides.all_tracks || track_id <= state.progress.track_threshold || (track_id == 9 && state.overrides.bonus_track);
}

std::uint32_t track_max_id(const State& state) {
    // JHAMMO bypasses availability, but not this independent original browse
    // limit. The direct track switch adds an explicit bonus-track override.
    return state.overrides.bonus_track || state.progress.track_threshold >= 9 ? 9 : 8;
}
} // namespace toca::frontend::cheats
