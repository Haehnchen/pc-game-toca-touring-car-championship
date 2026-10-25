#pragma once

#include "image/decode.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace toca {
std::filesystem::path artwork_root();
// Logical paths are relative to original/ and mega/. Only an enabled, present
// Mega override replaces the original; an invalid selected file remains an error.
std::optional<std::filesystem::path> find_artwork(const std::filesystem::path& root, std::string_view name, bool mega);
Surface load_artwork(const std::filesystem::path& root, std::string_view name, bool mega);
// Explicit atlas directories used by component callers share the same decoder.
Surface load_artwork_file(const std::filesystem::path& path);
} // namespace toca
