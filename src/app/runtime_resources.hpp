#pragma once

#include "frontend/cheats.hpp"

#include <cstdint>
#include <filesystem>

namespace toca {
namespace frontend {
class Controller;
}

void create_output_parent(const std::filesystem::path& path);
std::filesystem::path resource_root();
std::filesystem::path mega_model_directory(std::uint32_t detail);
frontend::cheats::State scene_cheats(const frontend::Controller& menu);
} // namespace toca
