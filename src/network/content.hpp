#pragma once

#include <cstdint>

namespace toca {
class AssetStore;

namespace network {
// CRC32 of the embedded table identity followed by sorted canonical file records.
// Generated Mega assets, soundtrack and local settings do not participate.
std::uint32_t simulation_content_crc32(const AssetStore& assets);
} // namespace network
} // namespace toca
