#include "assets/store.hpp"
#include "frontend/car_name.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// IEEE/zlib CRC32 over RGBA pixels, excluding surface row padding.
std::uint32_t raster_crc(SDL_Surface* surface) {
    expect(surface->format == SDL_PIXELFORMAT_RGBA32, "Car name raster must retain RGBA pixels");
    std::uint32_t result = 0xffffffffu;
    for (int y = 0; y < surface->h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < surface->w * 4; ++x) {
            result ^= row[x];
            for (unsigned bit = 0; bit < 8; ++bit) {
                result = (result >> 1) ^ ((result & 1) ? 0xedb88320u : 0);
            }
        }
    }
    return result ^ 0xffffffffu;
}

void reference(const char* root) {
    const toca::AssetStore store(root);
    const toca::frontend::CarNameFont font(store);

    struct Sample {
        std::string_view name;
        std::uint32_t crc;
    };

    // Independent canonical WIN3.FON raster fingerprints, including all four
    // source length-spacing branches, odd-width centering and right clipping.
    constexpr std::array samples{Sample{"DANIEL", 0x85d74cc0}, Sample{"PLAYER1", 0x1341289e}, Sample{"PLAYER 1", 0xcd91c458}, Sample{"ABCDEFGHI", 0xd52809e7},
        Sample{"WWWWWWWWW", 0x868a8e21}, Sample{"MENU", 0xd753b490}, Sample{"PLATO", 0x3a11b112}, Sample{"AUDI GHOST", 0x5b869590}};
    for (const auto& sample : samples) {
        const auto raster = font.render(sample.name);
        expect(raster->w == 128 && raster->h == 16, "Original car name raster must retain 128x16 cells");
        expect(raster_crc(raster.get()) == sample.crc, "Original car name glyphs, spacing, centering or clipping changed");
        for (int y = 0; y < raster->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(raster->pixels) + y * raster->pitch;
            for (int x = 0; x < raster->w; ++x) {
                const auto* pixel = row + x * 4;
                expect((pixel[0] == 0 || pixel[0] == 255) && pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[2] == pixel[3],
                    "Car names must contain white ink and transparent background only");
                expect(y < 14 || pixel[3] == 0, "Original fourteen-pixel glyphs must retain two empty bottom rows");
            }
        }
    }
    const auto blank = font.render("");
    const auto gated = font.render("DANIEL", false);
    const auto unsupported = font.render("daniel");
    expect(raster_crc(blank.get()) == 0xd8f49994 && raster_crc(gated.get()) == raster_crc(blank.get()) && raster_crc(unsupported.get()) == raster_crc(blank.get()),
        "The 320px gate, empty names and missing original glyphs must remain transparent");
    const auto terminated = font.render(std::string_view("MENU\0PLATO", 10));
    expect(raster_crc(terminated.get()) == 0xd753b490, "Original NUL termination must control both name content and length spacing");
    const auto long_stored_name = font.render(std::string(255, 'W'));
    expect(raster_crc(long_stored_name.get()) == 0x868a8e21, "Stored names up to 255 bytes must retain original three-pixel spacing and clip to the 128px bitmap");
    bool rejected = false;
    try {
        (void)font.render(std::string(256, 'W'));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Car name input must retain the bounded 255-byte stored-name limit");
    std::cout << "PASS: canonical car name font raster, original length spacing, clipping, alpha and 320px gate\n";
}

const std::uint8_t* pixel(SDL_Surface* surface, int x, int y) {
    return static_cast<const std::uint8_t*>(surface->pixels) + y * surface->pitch + x * 4;
}

std::array<int, 4> ink_bounds(SDL_Surface* surface) {
    std::array result{surface->w, surface->h, 0, 0};
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            if (pixel(surface, x, y)[3]) {
                result[0] = std::min(result[0], x);
                result[1] = std::min(result[1], y);
                result[2] = std::max(result[2], x + 1);
                result[3] = std::max(result[3], y + 1);
            }
        }
    }
    return result;
}

void mega(const char* root, const std::filesystem::path& artwork) {
    const toca::AssetStore store(root);
    const toca::frontend::CarNameFont font(store, true, artwork);

    struct Sample {
        std::string_view name;
        std::array<int, 4> bounds;
    };

    // Independent WIN3 layout positions: all four spacing branches, odd-width
    // centering (MENU/PLATO), blank advances and an overflowing nine-wide name.
    constexpr std::array samples{Sample{"DANIEL", {56, 0, 190, 28}}, Sample{"PLAYER1", {48, 0, 202, 28}}, Sample{"PLAYER 1", {46, 0, 204, 28}},
        Sample{"ABCDEFGHI", {48, 0, 206, 28}}, Sample{"WWWWWWWWW", {0, 0, 256, 28}}, Sample{"MENU", {74, 0, 174, 28}}, Sample{"PLATO", {66, 0, 182, 28}},
        Sample{"AUDI GHOST", {36, 0, 216, 28}}, Sample{"A A", {92, 0, 154, 28}}};
    bool antialiased = false, smooth_blocks = false;
    for (const auto& sample : samples) {
        const auto raster = font.render(sample.name);
        expect(raster->w == 256 && raster->h == 32 && raster->format == SDL_PIXELFORMAT_RGBA32, "Mega car names must render at exactly 256x32");
        expect(ink_bounds(raster.get()) == sample.bounds, "Mega ink bounds must retain exactly doubled original spacing, glyph cells, centering and clipping");
        for (int y = 0; y < raster->h; ++y) {
            for (int x = 0; x < raster->w; ++x) {
                const auto* rgba = pixel(raster.get(), x, y);
                if (rgba[3]) {
                    expect(rgba[0] >= 240 && rgba[1] >= 240 && rgba[2] >= 240, "Mega names must retain white glyph artwork without an outline");
                    antialiased |= rgba[3] < 255;
                }
                expect(y < 28 || rgba[3] == 0, "Mega glyphs must retain four transparent bottom rows");
                if (x % 2 == 0 && y % 2 == 0) {
                    smooth_blocks |= rgba[3] != pixel(raster.get(), x + 1, y)[3] || rgba[3] != pixel(raster.get(), x, y + 1)[3] || rgba[3] != pixel(raster.get(), x + 1, y + 1)[3];
                }
            }
        }
    }
    expect(antialiased && smooth_blocks, "Mega names need smooth antialiased contours instead of doubled original pixel blocks");

    const auto blank = font.render("");
    expect(ink_bounds(blank.get()) == std::array{256, 32, 0, 0}, "Empty Mega names must remain transparent");
    expect(raster_crc(font.render("DANIEL", false).get()) == raster_crc(blank.get()) && raster_crc(font.render("daniel").get()) == raster_crc(blank.get()),
        "The width gate and unsupported glyphs must remain transparent in Mega");
    expect(raster_crc(font.render(std::string_view("MENU\0PLATO", 10)).get()) == raster_crc(font.render("MENU").get()), "Mega names must retain original NUL termination");
    expect(raster_crc(font.render("A_A").get()) == raster_crc(font.render("A A").get()) && raster_crc(font.render("A^A").get()) == raster_crc(font.render("A A").get()),
        "Mega blank characters must keep their original eight-pixel advance before doubling");
    expect(raster_crc(font.render(std::string(255, 'W')).get()) == raster_crc(font.render("WWWWWWWWW").get()), "Long Mega names must clip with the original three-pixel spacing");
    bool rejected = false;
    try {
        (void)font.render(std::string(256, 'W'));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Mega names must retain the 255-byte stored-name limit");

    // Compare one glyph against its actual 2x atlas cell. The copied artwork
    // must survive byte-for-byte; rasterizing at128px and resizing loses AA.
    const auto path = artwork / "car-name.webp";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    expect(bool(file), "Cannot read Mega name atlas for cell comparison");
    const auto size = file.tellg();
    expect(size > 0 && size <= 16 * 1024 * 1024, "Unexpected Mega name atlas size");
    toca::ModelTexture encoded{"car-name.webp", std::vector<std::uint8_t>(std::size_t(size)), toca::ImageEncoding::webp};
    file.seekg(0);
    expect(bool(file.read(reinterpret_cast<char*>(encoded.bmp.data()), size)), "Cannot read complete Mega name atlas");
    const auto atlas = toca::decode_model_texture(encoded);
    expect(atlas->w == 512 && atlas->h == 512, "Mega name atlas must retain exactly 512x512 cells");
    const auto a = font.render("A");
    expect(ink_bounds(a.get()) == std::array{114, 0, 132, 28}, "Single Mega glyph must double the original rounded centering before placement");
    for (int y = 0; y < 28; ++y) {
        for (int x = 0; x < 18; ++x) {
            const auto* source = pixel(atlas.get(), x, y);
            const auto* target = pixel(a.get(), 114 + x, y);
            expect(source[3] ? std::equal(source, source + 4, target) : target[3] == 0, "Mega glyphs must copy the actual 2x atlas cell without resampling");
        }
    }
    const auto o = font.render("O");
    expect(pixel(o.get(), 124, 14)[3] <= 8 && pixel(o.get(), 117, 14)[3] > 240 && ink_bounds(o.get()) == std::array{116, 0, 132, 28},
        "Mega O must preserve its opaque stem and nearly transparent antialiased counter");
    std::cout << "PASS: 2x Mega car name artwork, exact layout and atlas cells, antialiasing, white alpha, bounds and original gates\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Expected canonical data root and Mega font artwork directory");
        }
        reference(argv[1]);
        mega(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
