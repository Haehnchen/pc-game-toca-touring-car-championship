#pragma once

#include "formats/model.hpp"

#include <filesystem>
#include <span>

namespace toca {
// Version 100 layout recovered from TOCA 1.1a at 0x00494a40 / 0x00495ee0.
// Unknown flags are not interpreted; source bytes remain in the reference.
Model parse_cm3d(std::span<const std::uint8_t> data);
Model load_cm3d(const std::filesystem::path& path);
} // namespace toca
