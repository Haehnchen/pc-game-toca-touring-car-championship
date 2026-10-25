#include "image/decode.hpp"

#include "formats/bmp_palette.hpp"

#include <stdexcept>
#include <string>
#include <vector>
#include <webp/decode.h>

namespace toca {
namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}
} // namespace

Surface decode_model_texture(const ModelTexture& texture, std::span<const std::uint8_t> complete_palette) {
    if (texture.encoding == ImageEncoding::bmp) {
        return decode_bmp(texture.bmp, complete_palette);
    }
    return decode_webp(texture.bmp);
}

Surface decode_webp(std::span<const std::uint8_t> bytes) {
    WebPBitstreamFeatures features{};
    if (bytes.empty() || bytes.size() > 16 * 1024 * 1024 || WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK || features.has_animation || features.width <= 0
        || features.height <= 0 || features.width > 4096 || features.height > 4096) {
        throw std::runtime_error("Image requires bounded, nonanimated WebP pixels");
    }
    Surface result(SDL_CreateSurface(features.width, features.height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    check(bool(result), "Allocate decoded WebP texture");
    if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), static_cast<std::uint8_t*>(result->pixels), std::size_t(result->pitch) * result->h, result->pitch)) {
        throw std::runtime_error("Cannot decode WebP image");
    }
    return result;
}

Surface decode_bmp(std::span<const std::uint8_t> bytes, std::span<const std::uint8_t> complete_palette) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot decode an empty BMP resource");
    }
    std::vector<std::uint8_t> restored;
    if (!complete_palette.empty()) {
        const auto u32 = [&](std::size_t offset) {
            if (offset > bytes.size() || bytes.size() - offset < 4) {
                throw std::runtime_error("Truncated BMP shared-palette field");
            }
            return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
        };
        if (complete_palette.size() != 1024) {
            throw std::runtime_error("BMP shared palette requires all 256 original BGR0 entries");
        }
        if (bytes.size() >= 54 && bytes[0] == 'B' && bytes[1] == 'M' && u32(14) == 40 && bytes[28] == 8 && bytes[29] == 0) {
            const auto count = u32(46);
            if (count == 255) {
                restored = restore_bmp_palette(bytes, complete_palette, "BMP omitted palette entries do not match the supplied original bank");
                bytes = restored;
            }
        }
    }
    auto* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
    check(stream != nullptr, "Open BMP resource bytes");
    Surface surface(SDL_LoadBMP_IO(stream, true), SDL_DestroySurface);
    check(bool(surface), "Decode BMP resource bytes");
    return surface;
}

} // namespace toca
