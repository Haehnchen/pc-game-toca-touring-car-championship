#include "network/content.hpp"

#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "native_data_identity.hpp"
#include "scene/catalog.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace toca::network {
std::uint32_t simulation_content_crc32(const AssetStore& assets) {
    std::vector<std::string> paths{"data/config.bfp", "data/allcars.bfp"};
    for (const auto& track : scene::track_catalog) {
        paths.push_back(std::string(track.archive_prefix) + "keep.bfp");
        paths.push_back(std::string(track.archive_prefix) + "free.bfp");
    }
    std::ranges::sort(paths);
    std::vector<std::uint8_t> manifest;
    const auto word = [&](std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            manifest.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
        }
    };
    constexpr std::string_view tables = "native/builtin-data";
    manifest.insert(manifest.end(), tables.begin(), tables.end());
    manifest.push_back(0);
    word(native_data::manifest_size);
    word(native_data::manifest_crc32);
    for (const auto& path : paths) {
        const auto bytes = assets.read_file(path);
        manifest.insert(manifest.end(), path.begin(), path.end());
        manifest.push_back(0);
        word(static_cast<std::uint32_t>(bytes.size()));
        word(crc32(bytes));
    }
    return crc32(manifest);
}
} // namespace toca::network
