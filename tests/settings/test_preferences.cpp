#include "app/options.hpp"
#include "app/paths.hpp"
#include "settings/preferences.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

struct PrivateDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("toca-preferences-" + std::to_string(std::random_device{}()));

    PrivateDirectory() {
        std::filesystem::create_directory(path);
    }

    ~PrivateDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

#if !defined(_WIN32) && !defined(__APPLE__)
struct DataHomeOverride {
    std::optional<std::string> previous;

    explicit DataHomeOverride(const std::filesystem::path& path) {
        if (const auto* value = SDL_getenv("XDG_DATA_HOME")) {
            previous = value;
        }
        require(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", path.c_str(), true), "Cannot set private test data home");
    }

    ~DataHomeOverride() {
        if (previous) {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", previous->c_str(), true);
        } else {
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME");
        }
    }
};
#endif
} // namespace

int main() {
    try {
        using namespace toca;
        PrivateDirectory directory;
        const auto config = directory.path / "user/preferences.cfg";
        const auto fresh = resolve_settings(load_preferences(config), Options{});
        require(fresh.graphics.detail == graphics_mega_detail, "A fresh start must use Mega graphics detail");
        require(!fresh.graphics.split_vertical, "A fresh start must retain the original Horizontal split preference");
        require(fresh.input_player2 == default_player2_input() && fresh.input_player2.keyboard[0].code == SDL_SCANCODE_W && fresh.input_player2.keyboard[3].code == SDL_SCANCODE_D,
            "Player two must default to the independent shared-keyboard layout");
        require(fresh.input.device_id.empty(), "Automatic keyboard/gamepad input must remain an explicit opt-in");
        auto automatic = fresh;
        automatic.input.device_id = automatic_input_device;
        automatic.input.keyboard[0].code = SDL_SCANCODE_I;
        automatic.input.gamepad[4] = {BindingKind::button, SDL_GAMEPAD_BUTTON_SOUTH};
        std::istringstream automatic_text(serialize_preferences(automatic));
        require(read_preferences(automatic_text) == automatic, "Automatic input must round-trip player one's independently configured keyboard and gamepad maps");
        rejects(
            [&] {
                auto invalid = automatic;
                invalid.input_player2.device_id = automatic_input_device;
                validate_settings(invalid);
            },
            "Automatic input must not be accepted for the second local player");
        auto local_settings = fresh;
        local_settings.input_player2.device_id = "0123456789abcdef0123456789abcdef";
        local_settings.input_player2.sensitivity = 1;
        local_settings.input_player2.deadzone_percent = 12;
        local_settings.input_player2.rumble_enabled = false;
        local_settings.input_player2.keyboard[8].code = SDL_SCANCODE_3;
        std::istringstream local_text(serialize_preferences(local_settings));
        require(read_preferences(local_text) == local_settings && local_settings.input == fresh.input,
            "The second player's device, full binding maps and feedback settings must round-trip independently");
        auto wheel_settings = fresh;
        wheel_settings.input.device_id = "joystick:0123456789abcdef0123456789abcdef";
        wheel_settings.input.gamepad = default_joystick_bindings();
        wheel_settings.input.gamepad[0] = {BindingKind::joystick_axis, 7, -1, -32768, 32767, 32767};
        wheel_settings.input.gamepad[1] = {BindingKind::joystick_axis, 8, 1, -32768, -32768, 32767};
        wheel_settings.input.gamepad[8] = {BindingKind::joystick_button, 127};
        wheel_settings.input_player2 = wheel_settings.input;
        wheel_settings.input_player2.gamepad[2].calibration_center = 512;
        std::istringstream wheel_text(serialize_preferences(wheel_settings));
        require(read_preferences(wheel_text) == wheel_settings,
            "Both raw wheel profiles must preserve signed full-travel pedal calibration, centered steering and high-index controls");
        rejects(
            [&] {
                auto invalid = wheel_settings;
                invalid.input.gamepad[1] = invalid.input.gamepad[0];
                invalid.input.gamepad[1].calibration_center = 32000;
                validate_settings(invalid);
            },
            "Calibration changes cannot make duplicate physical axis bindings distinct");
        rejects(
            [&] {
                auto invalid = local_settings;
                invalid.input_player2.keyboard[1] = invalid.input_player2.keyboard[0];
                validate_settings(invalid);
            },
            "Duplicate second-player bindings must be rejected by preference validation");
        require(fresh.video.width == 1280 && fresh.video.height == 720 && fresh.video.fullscreen && fresh.video.vsync && fresh.video.fps_limit == 120,
            "A fresh start must use fullscreen 1280x720 with VSync and a 120 FPS limit");
        require(fresh.audio.master_gain == 1.0f && !fresh.audio.muted && fresh.audio.effects_volume == 100 && fresh.audio.music_volume == 100 && fresh.audio.game_music
                    && fresh.audio.stereo_effects,
            "A fresh start must use full original sound volumes, music and stereo");
        std::istringstream display_only("version=1\nwidth=1280\nheight=720\n");
        require(read_preferences(display_only).graphics.detail == graphics_mega_detail, "Unspecified graphics detail must use the Mega default");
        std::istringstream saved_text(
            "# user "
            "settings\nversion=1\nwidth=1280\nheight=800\nfullscreen=off\nvsync=off\nfps_limit=60\nvolume=0.625\nmute=on\neffects_volume=70\nmusic_volume=40\ngame_music="
            "off\nstereo_effects=off\ngraphics_detail=1\nsplit_vertical=on\n");
        const auto saved = read_preferences(saved_text);
        const std::array<std::string_view, 12> arguments{
            "--resolution", "1920x1080", "--fps-limit", "120", "--mute", "off", "--frames", "5", "--trace", "trace.csv", "--gpu-driver", "vulkan"};
        const auto options = parse_options(arguments);
        require(options.gpu_driver == "vulkan" && parse_options({}).gpu_driver.empty(), "The GPU driver override must remain unset until explicitly selected");
        for (const auto driver : {"vulkan", "direct3d12", "metal"}) {
            const std::array<std::string_view, 3> gpu_arguments{"--gpu-driver", driver, "--save-settings"};
            const auto gpu_options = parse_options(gpu_arguments);
            require(gpu_options.gpu_driver == driver && gpu_options.preferences.empty() && resolve_settings(saved, gpu_options) == saved,
                "Each GPU backend must be a startup-only override, even when saving settings");
        }
        for (const auto& invalid_gpu :
            {std::vector<std::string_view>{"--gpu-driver"}, {"--gpu-driver", ""}, {"--gpu-driver", "auto"}, {"--gpu-driver", "opengl"}, {"--gpu-driver", "Vulkan"}}) {
            rejects(
                [&] {
                    (void)parse_options(invalid_gpu);
                },
                "GPU backend selection must require a supported lowercase driver name");
        }
        require(!options.step_input && !parse_options({}).step_input, "Ordinary CLI and empty options must retain the real host clock/input path");
        const std::array<std::string_view, 2> background_arguments{"--menu-background", "7"};
        require(parse_options(background_arguments).menu_background == 7 && !parse_options({}).menu_background,
            "Menu background override must accept the last original index and remain unset by default");
        for (const auto& invalid_background : {std::vector<std::string_view>{"--menu-background", "8"}, {"--menu-background", "-1"}}) {
            rejects(
                [&] {
                    (void)parse_options(invalid_background);
                },
                "Menu background override must stay inside the eight original choices");
        }
        const std::array<std::string_view, 4> network_arguments{"--network-host", "0", "--network-name", "DRIVER"};
        const auto network = parse_options(network_arguments);
        require(network.network_host == 0 && network.network_name == "DRIVER" && network.preferences.empty(), "Network diagnostics must remain outside saved preferences");
        for (const auto& invalid :
            {std::vector<std::string_view>{"--network-host", "65536"}, {"--network-host", "-1"}, {"--network-host", "1000", "--network-join", "127.0.0.1:1000"},
                {"--network-name", "TOO-LONG-NAME"}, {"--network-host", "1000", "--step-input", "--audio-offline", "--trace", "step.csv"}}) {
            rejects(
                [&] {
                    (void)parse_options(invalid);
                },
                "Invalid network diagnostics must be rejected before startup");
        }
        const std::array<std::string_view, 4> step_arguments{"--step-input", "--audio-offline", "--trace", "step.csv"};
        const auto stepping = parse_options(step_arguments);
        require(stepping.step_input && stepping.audio_offline && stepping.trace == "step.csv" && stepping.preferences.empty(),
            "Step input is an explicit offline traced diagnostic, independent of saved gameplay/display preferences");
        for (const auto& invalid_step_arguments : {std::vector<std::string_view>{"--step-input"}, {"--step-input", "--audio-offline"}, {"--step-input", "--trace", "step.csv"}}) {
            rejects(
                [&] {
                    (void)parse_options(invalid_step_arguments);
                },
                "Step-input CLI must require both offline audio and trace output before application startup");
        }
        const auto effective = resolve_settings(saved, options);
        require(effective.video.width == 1920 && effective.video.height == 1080 && effective.video.fps_limit == 120, "CLI must override saved display values");
        require(effective.graphics.detail == 1 && effective.graphics.split_vertical, "Saved graphics detail and split orientation must survive display CLI overrides");
        const std::array<std::string_view, 2> detail_arguments{"--graphics-detail", "4"};
        require(
            resolve_settings(saved, parse_options(detail_arguments)).graphics.detail == graphics_mega_detail, "Graphics detail CLI must explicitly select Mega above Very High");
        require(!effective.video.fullscreen && !effective.video.vsync && effective.audio.master_gain == .625f && !effective.audio.muted,
            "Saved window mode and VSync values must survive alongside CLI audio overrides");
        require(effective.audio.effects_volume == 70 && effective.audio.music_volume == 40 && !effective.audio.game_music && !effective.audio.stereo_effects,
            "Saved original effects/music controls must survive master CLI overrides");
        save_preferences(config, effective);
        require(load_preferences(config) == effective, "Native preferences must round trip exactly");
        const auto serialized = serialize_preferences(effective);
        require(serialized.find("trace") == std::string::npos && serialized.find("frames") == std::string::npos && serialized.find("gpu_driver") == std::string::npos,
            "Diagnostics and startup GPU selection must not enter saved preferences");
        auto replacement = effective;
        replacement.video.width = 640;
        replacement.graphics.detail = graphics_mega_detail;
        replacement.graphics.split_vertical = false;
        replacement.input.device_id = "030000005e0400008e02000000000000";
        replacement.input.keyboard[0] = {BindingKind::key, SDL_SCANCODE_W};
        replacement.input.keyboard[1] = {BindingKind::key, SDL_SCANCODE_S};
        replacement.input.gamepad[4] = {BindingKind::button, SDL_GAMEPAD_BUTTON_SOUTH};
        replacement.input.gamepad[5] = {BindingKind::axis, SDL_GAMEPAD_AXIS_RIGHTY, -1};
        replacement.input.sensitivity = 3;
        replacement.input.deadzone_percent = 15;
        replacement.input.keyboard[6] = {BindingKind::key, SDL_SCANCODE_R};
        replacement.input.keyboard[7] = {BindingKind::key, SDL_SCANCODE_T};
        replacement.input.keyboard[8] = {BindingKind::key, SDL_SCANCODE_H};
        replacement.input.gamepad[6] = {BindingKind::button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
        replacement.input.rumble_enabled = false;
        save_preferences(config, replacement);
        require(load_preferences(config) == replacement, "Replacement must retain Mega, all nine keyboard/gamepad bindings, axis direction, device, vibration and calibration");
        for (const auto& file : std::filesystem::directory_iterator(config.parent_path())) {
            require(file.path() == config, "Preference writes must not leave temporary files");
        }
        const auto unicode_config = directory.path / std::filesystem::path(u8"user settings/Grüße 日本語/preferences.cfg");
        save_preferences(unicode_config, effective);
        require(load_preferences(unicode_config) == effective, "Preference creation must support Unicode path components and spaces");
        save_preferences(unicode_config, replacement);
        require(load_preferences(unicode_config) == replacement, "Preference replacement must support Unicode path components and spaces");
        const auto blocked = unicode_config.parent_path() / "blocked.cfg";
        const auto preserved = blocked / "preferences.cfg";
        save_preferences(preserved, effective);
        rejects(
            [&] {
                save_preferences(blocked, replacement);
            },
            "Replacing a nonempty directory must report a failed preference save");
        require(load_preferences(preserved) == effective && load_preferences(unicode_config) == replacement,
            "Failed replacement must preserve the destination directory contents and neighboring preferences");
        for (const auto& file : std::filesystem::directory_iterator(unicode_config.parent_path())) {
            require(file.path() == unicode_config || file.path() == blocked, "Failed replacement must remove its temporary file");
        }
        std::filesystem::remove(preserved);
        std::filesystem::remove(blocked);
        save_preferences(blocked, replacement);
        require(load_preferences(blocked) == replacement, "A failed preference replacement must be retryable after removing the obstruction");
        for (const auto& file : std::filesystem::directory_iterator(unicode_config.parent_path())) {
            require(file.path() == unicode_config || file.path() == blocked, "Retried preference writes must not leave temporary files");
        }
        for (const auto text : {"version=1\nwidth=0\n", "version=1\nvolume=nan\n", "version=1\nvolume=1.1\n", "version=1\nvsync=yes\n", "version=1\nfps_limit=1001\n",
                 "version=1\neffects_volume=101\n", "version=1\nmusic_volume=101\n", "version=1\ngame_music=yes\n", "version=1\nstereo_effects=yes\n",
                 "version=1\ngraphics_detail=5\n", "version=1\ngpu_driver=vulkan\n", "version=1\nsplit_vertical=yes\n", "version=1\nwidth=640\nwidth=800\n",
                 "version=1\nwidht=640\n", "version=2\n", "width=640\n", "version=1\ninput_device=not-a-guid\n", "version=1\np2_input_device=auto\n",
                 "version=1\ninput_sensitivity=0\n", "version=1\ninput_deadzone=31\n", "version=1\nkey_throttle=k41\n", "version=1\nkey_throttle=k9\n",
                 "version=1\nkey_brake=k82\n", "version=1\npad_brake=a5+\n", "version=1\npad_throttle=a5\n", "version=1\npad_throttle=b26\n", "version=1\npad_throttle=k4\n",
                 "version=1\ninput_device=joystick:not-a-guid\n", "version=1\npad_throttle=r0-\n", "version=1\npad_throttle=r32-:-32768:0:32767\n",
                 "version=1\npad_throttle=j128\n", "version=1\npad_throttle=r0-:-32769:0:32767\n", "version=1\npad_throttle=r0-:-32768:32768:32767\n",
                 "version=1\npad_throttle=r0-:0:0:32767\n", "version=1\npad_throttle=r0-:-32768:0:32767:1\n"}) {
            rejects(
                [&] {
                    std::istringstream input(text);
                    (void)read_preferences(input);
                },
                "Invalid preferences must fail visibly");
        }
        for (const auto args : {std::array<std::string_view, 2>{"--frames", "0"}, {"--volume", "-1"}, {"--resolution", "640x480junk"}, {"--width", "4294967296"},
                 {"--vsync", "true"}, {"--unknown", "1"}, {"--graphics-detail", "5"}}) {
            rejects(
                [&] {
                    (void)parse_options(args);
                },
                "Invalid CLI values must fail");
        }
        const auto data = directory.path / "data";
        std::filesystem::create_directories(data);
        rejects(
            [&] {
                require_writable_outside_data(data / "preferences.cfg", data);
            },
            "Writes inside game data must fail");
        rejects(
            [&] {
                require_writable_outside_data(data, data);
            },
            "The game-data root must not be writable state");
        require_writable_outside_data(directory.path / "data-other/preferences.cfg", data);
        const auto bundled = directory.path / "bundled-data";
        std::filesystem::create_directory(bundled);
        Options bundled_options;
        bundled_options.data_root = data;
        bundled_options.config_path = directory.path / "user/preferences.cfg";
        bundled_options.state_root = directory.path / "user/saves";
        const auto bundled_paths = application_paths(bundled_options, bundled);
        require(bundled_paths.data_root == std::filesystem::canonical(data) && bundled_paths.bundled_data_root == std::filesystem::canonical(bundled),
            "Application paths must retain both read-only data roots");
        auto bundled_defaults = bundled_options;
        bundled_defaults.data_root.clear();
        require(application_paths(bundled_defaults, bundled).data_root == std::filesystem::canonical(bundled),
            "Default game data must come from the complete Git bundle without an original installation");
        for (const auto field : {&Options::config_path, &Options::state_root, &Options::screenshot, &Options::trace}) {
            auto invalid = bundled_options;
            invalid.*field = bundled / "output";
            rejects(
                [&] {
                    (void)application_paths(invalid, bundled);
                },
                "Configuration, saves, captures and traces must stay outside bundled data");
        }
        require_writable_outside_data(directory.path / "bundled-data-other/preferences.cfg", data, bundled);
        require(application_paths(bundled_options).bundled_data_root.empty(), "Existing component callers must work without bundled data");
#if !defined(_WIN32) && !defined(__APPLE__)
        Options defaults;
        defaults.data_root = data;
        {
            DataHomeOverride redirected(data);
            rejects(
                [&] {
                    (void)application_paths(defaults);
                },
                "Default storage redirected into game data must fail before creating it");
            require(std::filesystem::is_empty(data), "Rejected default storage must leave game data unchanged");
        }
        {
            const auto fresh_home = directory.path / "fresh-user-data";
            DataHomeOverride redirected(fresh_home);
            const auto paths = application_paths(defaults);
            require(paths.config == fresh_home / "toca/toca/preferences.cfg", "Default storage must follow the SDL/XDG user-data location");
            require(!std::filesystem::exists(fresh_home), "Path discovery must not create user storage");
        }
#endif
#ifndef _WIN32
        std::filesystem::create_directory_symlink(data, directory.path / "alias");
        rejects(
            [&] {
                require_writable_outside_data(directory.path / "alias/preferences.cfg", data);
            },
            "Symlink aliases into game data must fail");
        std::filesystem::create_directory_symlink(bundled, directory.path / "bundled-alias");
        rejects(
            [&] {
                require_writable_outside_data(directory.path / "bundled-alias/preferences.cfg", data, bundled);
            },
            "Symlink aliases into bundled data must fail");
#endif
        std::cout << "Native preference precedence, round trips, replacement and read-only paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
