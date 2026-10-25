#pragma once

#include "core/contracts.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toca {
struct Options {
    std::filesystem::path data_root;
    std::filesystem::path config_path;
    std::filesystem::path state_root;
    std::filesystem::path screenshot;
    std::filesystem::path trace;
    std::map<std::string, std::string> preferences;
    std::optional<std::uint16_t> network_host;
    std::optional<std::uint32_t> menu_background;
    std::string gpu_driver;
    std::string network_join;
    std::string network_name = "DRIVER";
    std::uint64_t frames = 0;
    bool help = false;
    bool save_settings = false;
    bool audio_offline = false;
    bool step_input = false;
};

Options parse_options(std::span<const std::string_view> arguments);
Settings resolve_settings(Settings saved, const Options& options);
std::string_view option_help();
} // namespace toca
