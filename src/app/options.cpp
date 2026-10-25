#include "app/options.hpp"

#include "settings/preferences.hpp"

#include <charconv>
#include <stdexcept>

namespace toca {
Options parse_options(std::span<const std::string_view> arguments) {
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto name = arguments[index];
        if (name == "--help") {
            options.help = true;
            continue;
        }
        if (name == "--save-settings") {
            options.save_settings = true;
            continue;
        }
        if (name == "--audio-offline") {
            options.audio_offline = true;
            continue;
        }
        if (name == "--step-input") {
            options.step_input = true;
            continue;
        }
        if (index + 1 == arguments.size()) {
            throw std::runtime_error("Missing value for " + std::string(name));
        }
        const auto value = arguments[++index];
        if (value.empty()) {
            throw std::runtime_error("Empty value for " + std::string(name));
        }
        if (name == "--data-root") {
            options.data_root = value;
        } else if (name == "--config") {
            options.config_path = value;
        } else if (name == "--state-dir") {
            options.state_root = value;
        } else if (name == "--screenshot") {
            options.screenshot = value;
        } else if (name == "--trace") {
            options.trace = value;
        } else if (name == "--gpu-driver") {
            if (value != "vulkan" && value != "direct3d12" && value != "metal") {
                throw std::runtime_error("--gpu-driver needs vulkan, direct3d12 or metal");
            }
            options.gpu_driver = value;
        } else if (name == "--network-host") {
            std::uint16_t port = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                throw std::runtime_error("--network-host needs a port from 0 to 65535");
            }
            options.network_host = port;
        } else if (name == "--menu-background") {
            std::uint32_t background = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), background);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || background >= 8) {
                throw std::runtime_error("--menu-background needs an index from 0 to 7");
            }
            options.menu_background = background;
        } else if (name == "--network-join") {
            options.network_join = value;
        } else if (name == "--network-name") {
            if (value.size() > 9 || value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ") != std::string_view::npos) {
                throw std::runtime_error("--network-name needs up to nine letters, digits or spaces");
            }
            options.network_name = value;
        } else if (name == "--frames") {
            const auto result = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || options.frames == 0) {
                throw std::runtime_error("--frames needs a positive integer");
            }
        } else if (name == "--resolution") {
            const auto separator = value.find('x');
            if (separator == std::string_view::npos) {
                throw std::runtime_error("--resolution needs WIDTHxHEIGHT");
            }
            options.preferences["width"] = value.substr(0, separator);
            options.preferences["height"] = value.substr(separator + 1);
        } else if (name == "--width" || name == "--height" || name == "--fullscreen" || name == "--vsync" || name == "--volume" || name == "--mute") {
            options.preferences[std::string(name.substr(2))] = value;
        } else if (name == "--fps-limit") {
            options.preferences["fps_limit"] = value;
        } else if (name == "--graphics-detail") {
            options.preferences["graphics_detail"] = value;
        } else {
            throw std::runtime_error("Unknown option: " + std::string(name));
        }
    }
    // Validate CLI values before initializing SDL or creating user storage.
    if (options.step_input && (!options.audio_offline || options.trace.empty())) {
        throw std::runtime_error("--step-input requires --audio-offline and --trace");
    }
    if (options.network_host && !options.network_join.empty()) {
        throw std::runtime_error("Choose either --network-host or --network-join");
    }
    if (options.step_input && (options.network_host || !options.network_join.empty())) {
        throw std::runtime_error("Network play requires the real clock; --step-input is unavailable");
    }
    (void)resolve_settings({}, options);
    return options;
}

Settings resolve_settings(Settings saved, const Options& options) {
    for (const auto& [key, value] : options.preferences) {
        set_preference(saved, key, value);
    }
    validate_settings(saved);
    return saved;
}

std::string_view option_help() {
    return "TOCA native startup\n"
           "  --data-root DIR        Read-only original 1.1a game data\n"
           "  --resolution WIDTHxHEIGHT | --width N --height N (default 1280x720)\n"
           "  --fullscreen on|off    Window mode (default on)\n"
           "  --vsync on|off         Presentation synchronization (default on)\n"
           "  --fps-limit N          Presentation ceiling (default 120), 0 disables the cap\n"
           "  --graphics-detail 0..4 Low/medium/high/very high/Mega detail (default 4)\n"
           "  --gpu-driver vulkan|direct3d12|metal\n"
           "                         GPU backend (Windows default Vulkan; other platforms SDL default)\n"
           "  --volume 0..1 --mute on|off\n"
           "  --save-settings        Save effective display/detail/audio preferences\n"
           "  --config FILE          Native preferences file\n"
           "  --state-dir DIR        Separate writable save-data root\n"
           "  --frames N             Exit after N presented frames\n"
           "  --screenshot FILE      Save final frame as PNG\n"
           "  --trace FILE           Write per-frame CSV diagnostics\n"
           "  --audio-offline        Exercise audio through offline output\n"
           "  --menu-background 0..7 Diagnostic original menu-car choice\n"
           "  --network-host PORT    Diagnostic LAN lobby host, 0 chooses a free port\n"
           "  --network-join IP:PORT Diagnostic direct IPv4 lobby connection\n"
           "  --network-name NAME    Diagnostic driver name, at most nine characters\n"
           "  --step-input           Diagnostic stdin driving keys for both local players at each40ms deadline (requires offline audio and trace)\n"
           "  --help\n";
}
} // namespace toca
