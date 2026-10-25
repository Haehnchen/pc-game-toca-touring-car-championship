#pragma once

#include "formats/model.hpp"

#include <filesystem>
#include <span>

namespace toca {
// Embedded GLB2 assets with original node IDs/damage bindings in node extras.
// Embedded WebP80 and lossless EXT_meshopt_compression geometry are supported.
// External buffers/images, animation and skins are rejected.
Model parse_glb(std::span<const std::uint8_t> bytes);
Model load_glb(const std::filesystem::path& path);
} // namespace toca
