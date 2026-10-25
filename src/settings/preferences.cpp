#include "settings/preferences.hpp"

#include "settings/storage.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace toca {
namespace {
constexpr std::array<std::string_view, 9> control_names{"throttle", "brake", "left", "right", "gear_up", "gear_down", "camera_rear", "camera_next", "horn"};

std::string_view trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

std::uint32_t integer(std::string_view text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("Expected an unsigned integer: " + std::string(text));
    }
    return value;
}

std::int32_t signed_integer(std::string_view text) {
    std::int32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("Expected a signed calibration value: " + std::string(text));
    }
    return value;
}

bool boolean(std::string_view text) {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    throw std::runtime_error("Expected on or off: " + std::string(text));
}

PhysicalBinding binding(std::string_view value) {
    if (value.size() < 2) {
        throw std::runtime_error("Expected a keyboard, gamepad or calibrated joystick binding");
    }
    PhysicalBinding result;
    switch (value.front()) {
        case 'k':
            result.kind = BindingKind::key;
            break;
        case 'b':
            result.kind = BindingKind::button;
            break;
        case 'a':
            result.kind = BindingKind::axis;
            break;
        case 'j':
            result.kind = BindingKind::joystick_button;
            break;
        case 'r':
            result.kind = BindingKind::joystick_axis;
            break;
        default:
            throw std::runtime_error("Unknown input binding kind");
    }
    value.remove_prefix(1);
    if (result.kind == BindingKind::joystick_axis) {
        const auto calibration = value.find(':');
        if (calibration == std::string_view::npos) {
            throw std::runtime_error("Joystick axis needs minimum, rest and maximum calibration");
        }
        auto fields = value.substr(calibration + 1);
        value = value.substr(0, calibration);
        for (auto* endpoint : {&result.calibration_min, &result.calibration_center, &result.calibration_max}) {
            const auto next = fields.find(':');
            const bool last = endpoint == &result.calibration_max;
            if ((next == std::string_view::npos) != last) {
                throw std::runtime_error("Joystick axis needs exactly three calibration values");
            }
            *endpoint = signed_integer(fields.substr(0, next));
            if (!last) {
                fields.remove_prefix(next + 1);
            }
        }
    }
    if (result.kind == BindingKind::axis || result.kind == BindingKind::joystick_axis) {
        if (value.empty() || (value.back() != '+' && value.back() != '-')) {
            throw std::runtime_error("Axis binding needs + or - direction");
        }
        result.direction = value.back() == '+' ? 1 : -1;
        value.remove_suffix(1);
    }
    const auto code = integer(value);
    if (code > 511) {
        throw std::runtime_error("Input binding code is out of range");
    }
    result.code = std::int32_t(code);
    return result;
}

void write_binding(std::ostream& output, const PhysicalBinding& value) {
    const auto kind = value.kind == BindingKind::key               ? 'k'
                      : value.kind == BindingKind::button          ? 'b'
                      : value.kind == BindingKind::axis            ? 'a'
                      : value.kind == BindingKind::joystick_button ? 'j'
                                                                   : 'r';
    output << kind << value.code;
    if (value.kind == BindingKind::axis || value.kind == BindingKind::joystick_axis) {
        output << (value.direction > 0 ? '+' : '-');
    }
    if (value.kind == BindingKind::joystick_axis) {
        output << ':' << value.calibration_min << ':' << value.calibration_center << ':' << value.calibration_max;
    }
}

float gain(std::string_view text) {
    float value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
        throw std::runtime_error("Expected a finite volume between 0 and 1");
    }
    return value;
}

} // namespace

void set_preference(Settings& settings, std::string_view key, std::string_view value) {
    if (key.starts_with("p2_") && (key.substr(3).starts_with("input_") || key.substr(3).starts_with("key_") || key.substr(3).starts_with("pad_"))) {
        Settings player;
        player.input = settings.input_player2;
        set_preference(player, key.substr(3), value);
        settings.input_player2 = player.input;
        return;
    }
    if (key == "width") {
        settings.video.width = integer(value);
    } else if (key == "height") {
        settings.video.height = integer(value);
    } else if (key == "fullscreen") {
        settings.video.fullscreen = boolean(value);
    } else if (key == "vsync") {
        settings.video.vsync = boolean(value);
    } else if (key == "fps_limit") {
        settings.video.fps_limit = integer(value);
    } else if (key == "graphics_detail") {
        settings.graphics.detail = integer(value);
    } else if (key == "split_vertical") {
        settings.graphics.split_vertical = boolean(value);
    } else if (key == "volume") {
        settings.audio.master_gain = gain(value);
    } else if (key == "mute") {
        settings.audio.muted = boolean(value);
    } else if (key == "effects_volume") {
        settings.audio.effects_volume = integer(value);
    } else if (key == "music_volume") {
        settings.audio.music_volume = integer(value);
    } else if (key == "game_music") {
        settings.audio.game_music = boolean(value);
    } else if (key == "stereo_effects") {
        settings.audio.stereo_effects = boolean(value);
    } else if (key == "input_device") {
        settings.input.device_id = value;
    } else if (key == "input_sensitivity") {
        settings.input.sensitivity = integer(value);
    } else if (key == "input_deadzone") {
        settings.input.deadzone_percent = integer(value);
    } else if (key == "input_rumble") {
        settings.input.rumble_enabled = boolean(value);
    } else {
        for (std::size_t index = 0; index < control_names.size(); ++index) {
            if (key == "key_" + std::string(control_names[index])) {
                settings.input.keyboard[index] = binding(value);
                return;
            }
            if (key == "pad_" + std::string(control_names[index])) {
                settings.input.gamepad[index] = binding(value);
                return;
            }
        }
        throw std::runtime_error("Unknown preference: " + std::string(key));
    }
}

void validate_settings(const Settings& settings) {
    if (settings.video.width == 0 || settings.video.width > 16384 || settings.video.height == 0 || settings.video.height > 16384) {
        throw std::runtime_error("Resolution dimensions must be between 1 and 16384");
    }
    if (settings.video.fps_limit > 1000) {
        throw std::runtime_error("FPS limit must be 0 (uncapped) or between 1 and 1000");
    }
    if (settings.graphics.detail >= graphics_detail_count) {
        throw std::runtime_error("Graphics detail must be between 0 and 4");
    }
    if (!std::isfinite(settings.audio.master_gain) || settings.audio.master_gain < 0 || settings.audio.master_gain > 1) {
        throw std::runtime_error("Volume must be between 0 and 1");
    }
    if (settings.audio.effects_volume > 100 || settings.audio.music_volume > 100) {
        throw std::runtime_error("Effects and music volumes must be between 0 and 100");
    }
    for (const auto* player : {&settings.input, &settings.input_player2}) {
        const auto& input = *player;
        if (input.sensitivity < 1 || input.sensitivity > 3 || input.deadzone_percent > 30) {
            throw std::runtime_error("Input sensitivity must be 1..3 and deadzone 0..30 percent");
        }
        if (input.device_id == automatic_input_device) {
            if (player != &settings.input) {
                throw std::runtime_error("Automatic input is only available for player one");
            }
        } else {
            const auto id = std::string_view(input.device_id);
            const auto guid = id.starts_with("joystick:") ? id.substr(9) : id;
            if (!id.empty() && (guid.size() != 32 || guid.find_first_not_of("0123456789abcdef") != std::string_view::npos)) {
                throw std::runtime_error("Input device must be an SDL gamepad/joystick GUID, auto or empty for keyboard");
            }
        }
        for (std::size_t index = 0; index < control_names.size(); ++index) {
            if (!valid_keyboard_binding(input.keyboard[index], index) || !valid_gamepad_binding(input.gamepad[index])) {
                throw std::runtime_error("Invalid control binding: " + std::string(control_names[index]));
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (same_physical_binding(input.keyboard[index], input.keyboard[previous]) || same_physical_binding(input.gamepad[index], input.gamepad[previous])) {
                    throw std::runtime_error("Duplicate control binding: " + std::string(control_names[index]));
                }
            }
        }
    }
}

Settings read_preferences(std::istream& input) {
    Settings settings;
    std::set<std::string> keys;
    std::string line;
    bool version = false;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto content = trim(line);
        if (content.empty() || content.front() == '#') {
            continue;
        }
        const auto separator = content.find('=');
        if (separator == std::string_view::npos) {
            throw std::runtime_error("Preference line " + std::to_string(line_number) + " needs key=value");
        }
        const auto key = trim(content.substr(0, separator));
        const auto value = trim(content.substr(separator + 1));
        if (!keys.emplace(key).second) {
            throw std::runtime_error("Duplicate preference: " + std::string(key));
        }
        if (key == "version") {
            if (value != "1") {
                throw std::runtime_error("Unsupported native preferences version: " + std::string(value));
            }
            version = true;
        } else {
            set_preference(settings, key, value);
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Reading preferences failed");
    }
    if (!version) {
        throw std::runtime_error("Native preferences need version=1");
    }
    validate_settings(settings);
    return settings;
}

Settings load_preferences(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        if (!std::filesystem::exists(path)) {
            return {};
        }
        throw std::runtime_error("Cannot read preferences: " + path.string());
    }
    try {
        return read_preferences(file);
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

std::string serialize_preferences(const Settings& settings) {
    validate_settings(settings);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "version=1\nwidth=" << settings.video.width << "\nheight=" << settings.video.height << "\nfullscreen=" << (settings.video.fullscreen ? "on" : "off")
           << "\nvsync=" << (settings.video.vsync ? "on" : "off") << "\nfps_limit=" << settings.video.fps_limit << "\ngraphics_detail=" << settings.graphics.detail
           << "\nsplit_vertical=" << (settings.graphics.split_vertical ? "on" : "off") << "\nvolume=" << std::setprecision(std::numeric_limits<float>::max_digits10)
           << settings.audio.master_gain << "\nmute=" << (settings.audio.muted ? "on" : "off") << "\neffects_volume=" << settings.audio.effects_volume
           << "\nmusic_volume=" << settings.audio.music_volume << "\ngame_music=" << (settings.audio.game_music ? "on" : "off")
           << "\nstereo_effects=" << (settings.audio.stereo_effects ? "on" : "off") << '\n';
    for (std::size_t player = 0; player < 2; ++player) {
        const auto& input = player == 0 ? settings.input : settings.input_player2;
        const std::string_view prefix = player == 0 ? "" : "p2_";
        output << prefix << "input_device=" << input.device_id << '\n'
               << prefix << "input_sensitivity=" << input.sensitivity << '\n'
               << prefix << "input_deadzone=" << input.deadzone_percent << '\n'
               << prefix << "input_rumble=" << (input.rumble_enabled ? "on" : "off") << '\n';
        for (std::size_t index = 0; index < control_names.size(); ++index) {
            output << prefix << "key_" << control_names[index] << '=';
            write_binding(output, input.keyboard[index]);
            output << '\n' << prefix << "pad_" << control_names[index] << '=';
            write_binding(output, input.gamepad[index]);
            output << '\n';
        }
    }
    return output.str();
}

void save_preferences(const std::filesystem::path& path, const Settings& settings) {
    write_state_file(path, serialize_preferences(settings));
}
} // namespace toca
