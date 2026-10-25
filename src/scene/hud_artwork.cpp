#include "scene/hud_artwork.hpp"

#include "image/artwork.hpp"

#include <stdexcept>
#include <string>

namespace toca::scene {
namespace {
Surface load_atlas(const std::filesystem::path& directory, std::string_view filename, int size) {
    auto atlas = directory.empty() ? load_artwork(artwork_root(), "hud/" + std::string(filename), true) : load_artwork_file(directory / filename);
    if (atlas->w != size || atlas->h != size) {
        throw std::runtime_error("Mega HUD artwork has incompatible original-atlas dimensions: " + std::string(filename));
    }
    return atlas;
}
} // namespace

HudArtwork load_hud_artwork(const std::filesystem::path& directory, bool kilometres) {
    return {load_atlas(directory, kilometres ? "primary-km.webp" : "primary-miles.webp", 256 * hud_artwork_scale), load_atlas(directory, "secondary.webp", 256 * hud_artwork_scale),
        load_atlas(directory, "start-housing.webp", 128 * hud_artwork_scale), load_atlas(directory, "start-red.webp", 64 * hud_artwork_scale),
        load_atlas(directory, "start-green.webp", 64 * hud_artwork_scale)};
}
} // namespace toca::scene
