#include "image/artwork.hpp"

#include "assets/store.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace toca {
std::filesystem::path artwork_root() {
    const auto* base = SDL_GetBasePath();
    if (!base) {
        throw std::runtime_error("Cannot locate UI artwork directory");
    }
    return std::filesystem::path(base) / "assets/ui";
}

std::optional<std::filesystem::path> find_artwork(const std::filesystem::path& root, std::string_view name, bool mega) {
    const AssetStore artwork(root);
    auto selected = artwork.find("original/" + std::string(name));
    if (mega) {
        if (const auto replacement = artwork.find("mega/" + std::string(name))) {
            selected = replacement;
        }
    }
    return selected;
}

Surface load_artwork_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open UI artwork: " + path.string());
    }
    const auto length = file.tellg();
    if (length <= 0 || length > 16 * 1024 * 1024) {
        throw std::runtime_error("UI artwork requires a bounded WebP file: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length) || file.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Cannot read complete UI artwork: " + path.string());
    }
    try {
        return decode_webp(bytes);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("Invalid UI artwork " + path.string() + ": " + error.what());
    }
}

Surface load_artwork(const std::filesystem::path& root, std::string_view name, bool mega) {
    const auto selected = find_artwork(root, name, mega);
    if (!selected) {
        throw std::runtime_error("Required UI artwork not found: " + std::string(name));
    }
    return load_artwork_file(*selected);
}
} // namespace toca
