#pragma once

#include "core/crc32.hpp"
#include "image/decode.hpp"
#include "render/gpu_renderer.hpp"

#include <array>
#include <map>
#include <string>

namespace toca::scene {
// Immutable uploads shared within one scene and renderer. Draw/animation state
// remains with each view; no textures survive in a process-wide cache.
class ModelTextures {
public:
    using Texture = std::shared_ptr<SDL_GPUTexture>;

    struct Uploaded {
        Texture texture;
        Uint32 mip_levels = 1;
        // Indexed source alpha stores a colour-key mask, not material opacity.
        bool palette = false;
        std::array<Texture, 2> emission;
    };

private:
    struct Entry {
        ImageEncoding encoding;
        std::vector<std::uint8_t> bytes, palette;
        bool mipmaps;
        Uploaded uploaded;
    };

    gpu::Renderer& renderer_;
    std::map<std::uint32_t, std::vector<Entry>> entries_;
    // 496250 retains the first12-byte name, separately for ordinary and
    // ghost materials (+28). Mega artwork keeps its exact-byte identity.
    std::array<std::map<std::string, Uploaded>, 2> source_names_;

    static gpu::Surface emission_surface(SDL_Surface* image, bool front) {
        gpu::Surface result(SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        gpu::check(bool(result), "Convert car lamp emission texture");
        for (int y = 0; y < result->h; ++y) {
            auto* pixels = static_cast<std::uint8_t*>(result->pixels) + y * result->pitch;
            for (int x = 0; x < result->w; ++x) {
                auto* pixel = pixels + x * 4;
                const float alpha = float(pixel[3]) / 255.f;
                // Keep glass patterns and clear indicators; black stays neutral.
                const auto red = float(pixel[0]), green = float(pixel[1]), blue = float(pixel[2]);
                const auto mask = front ? alpha : alpha * std::clamp((red - std::max(green, blue) - 8.f) / 64.f, 0.f, 1.f);
                pixel[0] = std::uint8_t(std::min(red * (front ? .9f : 1.4f), 235.f) * mask);
                pixel[1] = std::uint8_t(green * (front ? .85f : .25f) * mask);
                pixel[2] = std::uint8_t(blue * (front ? .72f : .15f) * mask);
                pixel[3] = 255;
            }
        }
        return result;
    }

public:
    explicit ModelTextures(gpu::Renderer& renderer) : renderer_(renderer) {}

    ModelTextures(const ModelTextures&) = delete;
    ModelTextures& operator=(const ModelTextures&) = delete;

    void require_renderer(const gpu::Renderer& renderer) const {
        if (&renderer != &renderer_) {
            throw std::runtime_error("Shared model textures require the same renderer");
        }
    }

    Uploaded get_model(const ModelTexture& texture, std::span<const std::uint8_t> palette = {}, std::array<bool, 2> emission = {}, bool ghost = false) {
        // Authored Mega images may minify through opaque mip levels. Original
        // BMPs retain their palette/black-key sampling; the uploader also keeps
        // every WebP with alpha at level zero. Callers use mip_levels > 1 to
        // select the mipmapped sampler, including for one-pixel images.
        if (texture.encoding != ImageEncoding::bmp || texture.name.empty()) {
            return get(texture, palette, texture.encoding == ImageEncoding::webp, emission);
        }
        auto& names = source_names_[ghost ? 1 : 0];
        const auto key = texture.name.substr(0, 12);
        if (const auto found = names.find(key); found != names.end()) {
            return found->second;
        }
        const auto uploaded = get(texture, palette, false, emission);
        names.emplace(key, uploaded);
        return uploaded;
    }

    Uploaded get(const ModelTexture& texture, std::span<const std::uint8_t> palette = {}, bool mipmaps = false, std::array<bool, 2> emission = {}) {
        // CRC32 selects candidates; exact encoded bytes, palette and upload
        // policy guard collisions and preserve keyed/mipmapped variants.
        auto& candidates = entries_[crc32(texture.bmp)];
        auto found = std::ranges::find_if(candidates, [&](const Entry& entry) {
            return entry.encoding == texture.encoding && entry.bytes == texture.bmp && std::ranges::equal(entry.palette, palette) && entry.mipmaps == mipmaps;
        });
        const bool missing = found == candidates.end();
        if (!missing && (!emission[0] || found->uploaded.emission[0]) && (!emission[1] || found->uploaded.emission[1])) {
            return found->uploaded;
        }
        const auto surface = decode_model_texture(texture, palette);
        Uploaded uploaded = missing ? Uploaded{} : found->uploaded;
        if (missing) {
            uploaded.palette = SDL_ISPIXELFORMAT_INDEXED(surface->format);
            uploaded.texture = Texture(renderer_.upload(surface.get(), texture.encoding == ImageEncoding::bmp, mipmaps, &uploaded.mip_levels));
        }
        for (std::size_t front = 0; front < emission.size(); ++front) {
            if (emission[front] && !uploaded.emission[front]) {
                uploaded.emission[front] = Texture(renderer_.upload(emission_surface(surface.get(), front != 0).get()));
            }
        }
        if (missing) {
            candidates.push_back({texture.encoding, texture.bmp, {palette.begin(), palette.end()}, mipmaps, uploaded});
        } else {
            found->uploaded = uploaded;
        }
        return uploaded;
    }
};
} // namespace toca::scene
