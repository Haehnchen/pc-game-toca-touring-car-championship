#pragma once

#include "frontend/cheats.hpp"

#include <filesystem>
#include <istream>
#include <string>

namespace toca {
frontend::cheats::State read_extras(std::istream& input);
frontend::cheats::State load_extras(const std::filesystem::path& path);
std::string serialize_extras(const frontend::cheats::State& state);
void save_extras(const std::filesystem::path& path, const frontend::cheats::State& state);
} // namespace toca
