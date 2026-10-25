#pragma once

#include "core/contracts.hpp"

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

namespace toca {
void set_preference(Settings& settings, std::string_view key, std::string_view value);
void validate_settings(const Settings& settings);
Settings read_preferences(std::istream& input);
Settings load_preferences(const std::filesystem::path& path);
std::string serialize_preferences(const Settings& settings);
void save_preferences(const std::filesystem::path& path, const Settings& settings);
} // namespace toca
