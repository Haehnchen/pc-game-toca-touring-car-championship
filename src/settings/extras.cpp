#include "settings/extras.hpp"

#include "settings/storage.hpp"

#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace toca {
namespace {
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool boolean(std::string_view text) {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    throw std::runtime_error("Extras flag requires on or off");
}

void validate(const frontend::cheats::State& state) {
    if (state.progress.track_threshold < 1 || state.progress.track_threshold > 9) {
        throw std::runtime_error("Earned track threshold must be between 1 and 9");
    }
}
} // namespace

frontend::cheats::State read_extras(std::istream& input) {
    frontend::cheats::State state;
    std::set<std::string> keys;
    std::string line;
    bool version = false;
    while (std::getline(input, line)) {
        const auto text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const auto separator = text.find('=');
        if (separator == std::string_view::npos) {
            throw std::runtime_error("Extras entry needs key=value");
        }
        const auto key = trim(text.substr(0, separator)), value = trim(text.substr(separator + 1));
        if (!keys.emplace(key).second) {
            throw std::runtime_error("Duplicate Extras key: " + std::string(key));
        }
        if (key == "version") {
            if (value != "1") {
                throw std::runtime_error("Unsupported Extras state version");
            }
            version = true;
        } else if (key == "all_tracks") {
            state.overrides.all_tracks = boolean(value);
        } else if (key == "bonus_cars") {
            state.overrides.bonus_cars = boolean(value);
        } else if (key == "bonus_track") {
            state.overrides.bonus_track = boolean(value);
        } else if (key == "earned_special_championship") {
            state.progress.special_championship = boolean(value);
        } else if (key == "earned_car_8") {
            state.progress.bonus_car_8 = boolean(value);
        } else if (key == "earned_car_9") {
            state.progress.bonus_car_9 = boolean(value);
        } else if (key == "earned_track_threshold") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), state.progress.track_threshold);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                throw std::runtime_error("Invalid earned track threshold");
            }
        } else if (key == "lock_code") {
            state.lock_code = boolean(value);
        } else if (key == "garage_code") {
            state.garage_code = boolean(value);
        } else if (key.starts_with("code_")) {
            const auto code = frontend::cheats::find_code(key.substr(5));
            if (!code || !frontend::cheats::allowed(*code) || *code == frontend::cheats::Code::lock || *code == frontend::cheats::Code::garage
                || *code == frontend::cheats::Code::bonus_cars || *code == frontend::cheats::Code::all_tracks) {
                throw std::runtime_error("Unknown standalone Extras code flag: " + std::string(key));
            }
            state.flags[static_cast<std::size_t>(*code)] = boolean(value);
        } else {
            throw std::runtime_error("Unknown Extras key: " + std::string(key));
        }
    }
    if (!input.eof() || !version) {
        throw std::runtime_error("Cannot read Extras state with version=1");
    }
    validate(state);
    return state;
}

frontend::cheats::State load_extras(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        if (!std::filesystem::exists(path)) {
            return {};
        }
        throw std::runtime_error("Cannot read Extras: " + path.string());
    }
    try {
        return read_extras(input);
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

std::string serialize_extras(const frontend::cheats::State& state) {
    validate(state);
    const auto value = [](bool flag) {
        return flag ? "on" : "off";
    };
    std::ostringstream output;
    output << "version=1\nall_tracks=" << value(state.overrides.all_tracks) << "\nbonus_cars=" << value(state.overrides.bonus_cars)
           << "\nbonus_track=" << value(state.overrides.bonus_track) << "\nearned_special_championship=" << value(state.progress.special_championship)
           << "\nearned_car_8=" << value(state.progress.bonus_car_8) << "\nearned_car_9=" << value(state.progress.bonus_car_9)
           << "\nearned_track_threshold=" << state.progress.track_threshold << "\nlock_code=" << value(state.lock_code) << "\ngarage_code=" << value(state.garage_code) << '\n';
    for (const auto& entry : frontend::cheats::catalog()) {
        using Code = frontend::cheats::Code;
        if (entry.code != Code::lock && entry.code != Code::garage && entry.code != Code::bonus_cars && entry.code != Code::all_tracks && entry.code != Code::disco) {
            output << "code_" << entry.text << '=' << value(frontend::cheats::active(state, entry.code)) << '\n';
        }
    }
    return output.str();
}

void save_extras(const std::filesystem::path& path, const frontend::cheats::State& state) {
    write_state_file(path, serialize_extras(state));
}
} // namespace toca
