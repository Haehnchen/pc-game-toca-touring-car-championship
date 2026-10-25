#include "scene/model_textures.hpp"

#include <SDL3/SDL_main.h>

#include <cmath>
#include <iostream>
#include <span>
#include <string_view>

namespace {
using namespace toca::gpu;
using toca::platform::Extent;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Paint> Surface image(Uint32 width, Uint32 height, Paint paint) {
    Surface result(SDL_CreateSurface(int(width), int(height), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    check(bool(result), "Create mipmap fixture");
    for (Uint32 y = 0; y < height; ++y) {
        for (Uint32 x = 0; x < width; ++x) {
            const auto color = paint(x, y);
            std::memcpy(static_cast<Uint8*>(result->pixels) + y * result->pitch + x * 4, color.data(), color.size());
        }
    }
    return result;
}

Surface constant(Uint32 width, Uint32 height, std::array<Uint8, 4> color) {
    return image(width, height, [color](Uint32, Uint32) {
        return color;
    });
}

void tile(Frame& frame, State state, float x, float y, float width, float height, float offset_u = 0, float offset_v = 0) {
    const auto first = frame.vertices.size();
    frame.quad(state, x, y, width, height);
    for (auto& vertex : std::span(frame.vertices).subspan(first)) {
        vertex.u += offset_u;
        vertex.v += offset_v;
    }
}

std::vector<Uint32> render(Renderer& renderer, const Frame& frame, Extent extent) {
    require(renderer.draw_offscreen(frame, extent), "Render mipmap fixture");
    return renderer.readback();
}

void minification_and_sampler_isolation(Renderer& renderer) {
    const auto checker = image(64, 32, [](Uint32 x, Uint32 y) {
        const auto value = Uint8((x + y) % 2 ? 255 : 0);
        return std::array<Uint8, 4>{value, value, value, 255};
    });
    Uint32 levels = 0, original_levels = 0;
    const auto filtered = renderer.upload(checker.get(), false, true, &levels);
    const auto original = renderer.upload(checker.get(), false, false, &original_levels);
    require(levels == 7 && original_levels == 1, "Rectangular64x32 texture must receive its full chain only when requested");
    Frame frame;
    frame.quad({}, 0, 0, 112, 32, 0xff000000);
    // Offset the source grid so base-level bilinear sampling aliases at96,
    // while the correctly generated mip chain converges to the128 average.
    tile(frame, {.texture = filtered.get(), .linear = true}, 8, 8, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = filtered.get(), .mipmapped = true}, 32, 8, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = filtered.get(), .linear = true}, 56, 8, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = filtered.get()}, 80, 8, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = original.get(), .linear = true}, 96, 8, 8, 8, .25f / 64, .25f / 32);
    const auto pixels = render(renderer, frame, {112, 32});
    for (Uint32 y = 9; y < 15; ++y) {
        for (Uint32 x = 1; x < 7; ++x) {
            const auto linear = pixels[y * 112 + 8 + x];
            const auto mip = pixels[y * 112 + 32 + x];
            const auto point = pixels[y * 112 + 80 + x];
            const auto channel = int(mip & 255);
            require(std::abs(channel - 128) <= 2 && ((mip >> 8) & 255) == (mip & 255) && ((mip >> 16) & 255) == (mip & 255),
                "Strong checker minification must retain its stable gray area average");
            require(std::abs(int(linear & 255) - channel) > 16, "Mipmapped sampling must suppress actual base-level checker aliasing");
            require(pixels[y * 112 + 56 + x] == linear && pixels[y * 112 + 96 + x] == linear,
                "Ordinary bilinear sampling must retain its original level-zero pixels before and after a mipmapped draw");
            require(point == 0xff000000 || point == 0xffffffff, "Ordinary point sampling must retain its hard source texels");
        }
    }
}

void nonseamless_terrain_edges(Renderer& renderer) {
    const auto surface = image(64, 64, [](Uint32, Uint32 y) {
        const auto value = Uint8(y < 32 ? 224 : 32);
        return std::array<Uint8, 4>{value, value, value, 255};
    });
    const auto texture = renderer.upload(surface.get(), false, true);
    Frame frame;
    // Minify horizontally by 8x, holding V at the original terrain's inset.
    // At mip 3, the old repeat sampler mixed the opposite bright/dark edge
    // into the shared road center despite every UV being inside 0..1.
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (unsigned edge = 0; edge < 2; ++edge) {
            const auto first = frame.vertices.size();
            frame.quad({.texture = texture.get(), .linear = true, .clamp = mode == 1, .mipmapped = mode != 2}, float(mode * 16), float(edge * 16), 8, 8);
            for (auto& vertex : std::span(frame.vertices).subspan(first)) {
                vertex.v = edge ? .9850006f : .02f;
            }
        }
    }
    const auto pixels = render(renderer, frame, {48, 32});
    for (unsigned edge = 0; edge < 2; ++edge) {
        const auto expected = edge ? 32 : 224;
        for (unsigned y = 2; y < 6; ++y) {
            for (unsigned x = 2; x < 6; ++x) {
                const auto row = (edge * 16 + y) * 48;
                const auto repeat = int(pixels[row + x] & 255);
                const auto clamped = int(pixels[row + 16 + x] & 255);
                const auto original = int(pixels[row + 32 + x] & 255);
                require(std::abs(clamped - expected) <= 1, "Minified terrain edges must not sample the unrelated opposite border");
                require(std::abs(repeat - expected) > 40, "Explicit repeat sampling must retain its actual opposite-edge contribution");
                require(std::abs(original - expected) <= 1, "The original level-zero UV inset must retain its original edge pixels");
            }
        }
    }
}

void chain_dimensions_and_constant_color(Renderer& renderer) {
    struct Size {
        Uint32 width, height, levels;
    };

    // Include single-axis and non-power-of-two chains; every final level is1x1.
    for (const auto size : {Size{1, 1, 1}, {1, 33, 6}, {48, 16, 6}, {128, 32, 8}, {5, 3, 3}}) {
        const auto surface = constant(size.width, size.height, {40, 120, 200, 255});
        Uint32 levels = 0;
        const auto texture = renderer.upload(surface.get(), false, true, &levels);
        require(levels == size.levels, "Opaque rectangular texture must report every level through1x1");
        Frame frame;
        frame.quad({}, 0, 0, 16, 16, 0xff000000);
        tile(frame, {.texture = texture.get(), .mipmapped = levels > 1}, 8, 8, 1, 1);
        const auto pixels = render(renderer, frame, {16, 16});
        require(pixels[8 * 16 + 8] == 0xff2878c8, "Full minification must preserve a constant color through rectangular and1x1 mip chains");
    }
}

void opacity_and_black_key(Renderer& renderer) {
    const auto translucent = constant(16, 8, {64, 128, 192, 127});
    Uint32 translucent_levels = 0;
    const auto alpha_texture = renderer.upload(translucent.get(), false, true, &translucent_levels);
    require(translucent_levels == 1, "Partial alpha textures must retain their original single level");
    const auto keyed = image(16, 8, [](Uint32 x, Uint32) {
        return x < 8 ? std::array<Uint8, 4>{0, 0, 0, 255} : std::array<Uint8, 4>{0, 200, 0, 255};
    });
    Uint32 keyed_levels = 0;
    const auto key_texture = renderer.upload(keyed.get(), true, true, &keyed_levels);
    require(keyed_levels == 1, "RGB-black key conversion must exclude newly transparent images from mip generation");
    const auto opaque = constant(16, 8, {0, 200, 0, 255});
    Uint32 opaque_levels = 0;
    const auto opaque_texture = renderer.upload(opaque.get(), true, true, &opaque_levels);
    require(opaque_levels == 5, "Black-key-capable images without transparent texels must still receive their full opaque chain");
    Frame frame;
    frame.quad({}, 0, 0, 64, 24, 0xff000000);
    tile(frame, {.texture = alpha_texture.get(), .blend = true}, 0, 0, 16, 8);
    frame.quad({}, 24, 0, 16, 8, 0xffff8000);
    tile(frame, {.texture = key_texture.get(), .key = true}, 24, 0, 16, 8);
    tile(frame, {.texture = opaque_texture.get(), .mipmapped = true}, 48, 0, 1, 1);
    const auto pixels = render(renderer, frame, {64, 24});
    const auto alpha = pixels[4 * 64 + 4];
    require(std::abs(int((alpha >> 16) & 255) - 32) <= 1 && std::abs(int((alpha >> 8) & 255) - 64) <= 1 && std::abs(int(alpha & 255) - 96) <= 1,
        "Excluded alpha texture must retain its actual blended opacity");
    require(pixels[4 * 64 + 28] == 0xffff8000 && pixels[4 * 64 + 36] == 0xff00c800, "Excluded black-key texture must retain transparent holes and opaque source color");
    require(pixels[48] == 0xff00c800, "Opaque black-key-capable image must preserve color through its generated chain");
}

toca::ModelTexture webp(const char* name, std::string_view hex) {
    toca::ModelTexture result{name, {}, toca::ImageEncoding::webp};
    const auto nibble = [](char value) {
        return unsigned(value <= '9' ? value - '0' : value - 'a' + 10);
    };
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        result.bmp.push_back(std::uint8_t(nibble(hex[index]) * 16 + nibble(hex[index + 1])));
    }
    return result;
}

void model_texture_policy(Renderer& renderer) {
    toca::scene::ModelTextures textures(renderer);
    // Lossless WebP fixtures: 64x32 black/white checker, 16x8 half-cutout
    // green, constant RGBA(64,128,192,127), and a single green texel.
    const auto checker = webp("checker", "524946462e000000574542505650384c220000002f3fc007000f30fff33ffff31f781012102b9df4535623fa4f3049536dc71889bb07");
    const auto cutout = webp("cutout", "5249464624000000574542505650384c170000002f0fc001100f30c811f31f0cb160327fe9ce1422fa1f3a00");
    const auto translucent = webp("translucent", "524946461e000000574542505650384c110000002f0fc001100750c00215b87f8188e87f0000");
    const auto single = webp("single", "524946461c000000574542505650384c0f0000002f000000000750e488fe0722a2ff0100");
    const auto opaque = textures.get_model(checker);
    auto alias = checker;
    alias.name = "another-model-slot";
    require(opaque.mip_levels == 7 && textures.get_model(alias).texture == opaque.texture,
        "Opaque Mega model slots must receive and share a complete mip chain regardless of material name");
    const auto base = textures.get(checker);
    require(base.mip_levels == 1 && base.texture != opaque.texture && textures.get(checker, {}, true).texture == opaque.texture,
        "Model/terrain mip policies must share uploads while retaining separate original single-level uploads");
    const auto holes = textures.get_model(cutout), alpha = textures.get_model(translucent);
    require(holes.mip_levels == 1 && alpha.mip_levels == 1 && textures.get_model(single).mip_levels == 1,
        "Cutout, partial-alpha and one-texel model images must retain level-zero sampling");
    const auto lamps = textures.get_model(checker, {}, {true, true});
    require(lamps.texture == opaque.texture && lamps.mip_levels == opaque.mip_levels && lamps.emission[0] && lamps.emission[1]
                && lamps.emission == textures.get_model(alias, {}, {true, true}).emission,
        "Model mip policy must preserve shared independent lamp derivatives");

    // A two-pixel indexed BMP with an omitted final palette entry exercises
    // the original palette restoration and RGB-black key after policy routing.
    toca::ModelTexture indexed{"indexed", std::vector<std::uint8_t>(1078)};
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            indexed.bmp[offset + shift / 8] = std::uint8_t(value >> shift);
        }
    };
    indexed.bmp[0] = 'B';
    indexed.bmp[1] = 'M';
    put32(2, 1078);
    put32(10, 1074);
    put32(14, 40);
    put32(18, 2);
    put32(22, 1);
    indexed.bmp[26] = 1;
    indexed.bmp[28] = 8;
    put32(46, 255);
    indexed.bmp[1075] = 255;
    std::vector<std::uint8_t> palette(1024);
    palette[1022] = 255;
    const auto red = textures.get_model(indexed, palette);
    require(red.mip_levels == 1 && red.texture == textures.get(indexed, palette).texture, "Original indexed model textures must share the unchanged single-level palette upload");
    palette[1022] = 0;
    palette[1021] = 255;
    const auto green = textures.get_model(indexed, palette);
    require(green.mip_levels == 1 && green.texture != red.texture, "Original model uploads must retain distinct restored palettes");
    auto solid_bmp = indexed;
    solid_bmp.bmp[1074] = 255;
    require(textures.get_model(solid_bmp, palette).mip_levels == 1 && textures.get(solid_bmp, palette, true).mip_levels == 2,
        "Even fully opaque original model BMPs must retain their original level-zero policy");

    Frame frame;
    frame.quad({}, 0, 0, 112, 32, 0xffff8000);
    tile(frame, {.texture = opaque.texture.get(), .key = true, .mipmapped = opaque.mip_levels > 1}, 0, 0, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = base.texture.get(), .key = true}, 16, 0, 8, 8, .25f / 64, .25f / 32);
    tile(frame, {.texture = holes.texture.get(), .key = true, .mipmapped = holes.mip_levels > 1}, 32, 0, 16, 8);
    tile(frame, {.texture = alpha.texture.get(), .blend = true, .mipmapped = alpha.mip_levels > 1}, 56, 0, 16, 8);
    tile(frame, {.texture = red.texture.get(), .key = true}, 80, 0, 8, 8);
    tile(frame, {.texture = green.texture.get(), .key = true}, 96, 0, 8, 8);
    const auto pixels = render(renderer, frame, {112, 32});
    require(std::abs(int(pixels[4 * 112 + 4] & 255) - 128) <= 2, "Model mipmaps must suppress actual opaque checker minification");
    const auto point = pixels[4 * 112 + 20];
    require(point == 0xff000000, "Keyed model draws must retain WebP RGB-black texels with opaque alpha; only original BMPs convert black to a cutout");
    require(pixels[4 * 112 + 36] == 0xffff8000 && pixels[4 * 112 + 44] == 0xff00c800, "Model cutouts must retain their original holes and opaque colors");
    const auto blended = pixels[4 * 112 + 60];
    require(std::abs(int((blended >> 16) & 255) - 160) <= 1 && std::abs(int((blended >> 8) & 255) - 128) <= 1 && std::abs(int(blended & 255) - 96) <= 1,
        "Model partial alpha must retain its actual source opacity");
    require(pixels[4 * 112 + 81] == 0xffff8000 && pixels[4 * 112 + 86] == 0xffff0000 && pixels[4 * 112 + 97] == 0xffff8000 && pixels[4 * 112 + 102] == 0xff00ff00,
        "Original model policy must retain palette-specific colors and RGB-black cutouts");
}
} // namespace

int main() {
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated mipmap video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA mipmap checks", 128, 64, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        check(bool(window), "Create isolated mipmap window");
        Renderer renderer(window.get());
        minification_and_sampler_isolation(renderer);
        nonseamless_terrain_edges(renderer);
        chain_dimensions_and_constant_color(renderer);
        opacity_and_black_key(renderer);
        model_texture_policy(renderer);
        renderer.wait_idle();
        std::cout
            << "PASS: opaque mip chains, strong minification, terrain edge isolation, rectangular sizes, alpha/black-key exclusion, model policy/sharing and sampler isolation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
