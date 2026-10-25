#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "image/artwork.hpp"
#include "image/decode.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value);
    bytes.at(offset + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.at(offset + shift / 8) = std::uint8_t(value >> shift);
    }
}

template <class Function> void rejects(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Malformed resource was accepted");
}

Bytes bmp(unsigned bits) {
    const std::size_t palette_bytes = bits == 8 ? 8 : 0;
    const std::size_t pixel_offset = 54 + palette_bytes;
    Bytes bytes(pixel_offset + 8);
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(bytes, 2, std::uint32_t(bytes.size()));
    put32(bytes, 10, std::uint32_t(pixel_offset));
    put32(bytes, 14, 40);
    put32(bytes, 18, 2);
    put32(bytes, 22, bits == 8 ? 2 : 1);
    put16(bytes, 26, 1);
    put16(bytes, 28, std::uint16_t(bits));
    put32(bytes, 34, 8);
    if (bits == 8) {
        put32(bytes, 46, 2);
        bytes[54] = 0x33;
        bytes[55] = 0x22;
        bytes[56] = 0x11;
        bytes[58] = 0xef;
        bytes[59] = 0xcd;
        bytes[60] = 0xab;
        bytes[pixel_offset] = 1;
        bytes[pixel_offset + 5] = 1;
    } else {
        bytes[pixel_offset] = 0x33;
        bytes[pixel_offset + 1] = 0x22;
        bytes[pixel_offset + 2] = 0x11;
        bytes[pixel_offset + 3] = 0xef;
        bytes[pixel_offset + 4] = 0xcd;
        bytes[pixel_offset + 5] = 0xab;
    }
    return bytes;
}

std::uint32_t pixel(SDL_Surface* surface, int x, int y) {
    toca::Surface rgba(SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    expect(bool(rgba), "Convert decoded resource for pixel check");
    std::uint32_t result;
    std::memcpy(&result, static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch + x * 4, 4);
    return result;
}

const Bytes& alpha_webp() {
    static const Bytes bytes{0x52, 0x49, 0x46, 0x46, 0x1a, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0e, 0x00, 0x00, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x10,
        0x0f, 0x10, 0x11, 0xf3, 0x1f, 0x22, 0x23, 0xfa, 0x1f};
    return bytes;
}

void image_tests() {
    auto indexed = toca::decode_bmp(bmp(8));
    expect(indexed->w == 2 && indexed->h == 2, "Indexed BMP dimensions changed");
    auto* palette = SDL_GetSurfacePalette(indexed.get());
    expect(
        palette && palette->ncolors == 2 && palette->colors[0].r == 0x11 && palette->colors[0].g == 0x22 && palette->colors[0].b == 0x33, "Indexed BMP palette was not preserved");
    expect(pixel(indexed.get(), 0, 0) == 0xff112233 && pixel(indexed.get(), 0, 1) == 0xffabcdef, "BMP row order/padding changed");
    auto rgb = toca::decode_bmp(bmp(24));
    expect(rgb->w == 2 && rgb->h == 1 && pixel(rgb.get(), 0, 0) == 0xff112233 && pixel(rgb.get(), 1, 0) == 0xffabcdef, "24-bit BMP decoding changed");
    auto reduced = bmp(8);
    reduced.insert(reduced.begin() + 62, 253 * 4, 0);
    put32(reduced, 2, std::uint32_t(reduced.size()));
    put32(reduced, 10, 1074);
    put32(reduced, 46, 255);
    reduced[1074] = 255; // HOR references the omitted final entry.
    Bytes complete_palette(1024);
    std::copy_n(reduced.begin() + 54, 1020, complete_palette.begin());
    complete_palette[1020] = 0x56;
    complete_palette[1021] = 0x34;
    complete_palette[1022] = 0x12;
    const auto original_reduced = reduced, original_palette = complete_palette;
    const auto restored = toca::decode_bmp(reduced, complete_palette);
    expect(pixel(restored.get(), 0, 1) == 0xff123456 && pixel(restored.get(), 0, 0) == 0xff112233,
        "Omitted indexed color must come from the matching supplied bank without moving bitmap rows");
    expect(reduced == original_reduced && complete_palette == original_palette, "Shared-palette decoding must preserve the original image and donor bytes");
    for (const auto& [offset, value] : {std::pair{10u, 1078u}, std::pair{30u, 1u}}) {
        auto malformed = reduced;
        put32(malformed, offset, value);
        rejects([&] {
            (void)toca::decode_bmp(malformed, complete_palette);
        });
    }
    rejects([&] {
        (void)toca::decode_bmp(std::span(reduced).first(1073), complete_palette);
    });
    rejects([&] {
        (void)toca::decode_bmp(reduced, std::span(complete_palette).first(1023));
    });
    // A complete bank is only a repair input for the identified 255-entry form.
    const auto ordinary = toca::decode_bmp(bmp(8), complete_palette);
    const auto true_color = toca::decode_bmp(bmp(24), complete_palette);
    expect(pixel(ordinary.get(), 0, 0) == 0xff112233 && pixel(ordinary.get(), 0, 1) == 0xffabcdef && pixel(true_color.get(), 1, 0) == 0xffabcdef,
        "Supplying a shared bank must retain ordinary indexed and true-color BMP decoding");
    complete_palette[0] ^= 1;
    rejects([&] {
        (void)toca::decode_bmp(reduced, complete_palette);
    });
    rejects([] {
        (void)toca::decode_bmp({});
    });
    const auto bytes = bmp(8);
    rejects([&] {
        (void)toca::decode_bmp(std::span(bytes).first(30));
    });

    const auto& webp = alpha_webp();
    const auto compressed = toca::decode_model_texture({"generated", webp, toca::ImageEncoding::webp});
    expect(compressed->w == 2 && compressed->h == 1 && pixel(compressed.get(), 0, 0) == 0xff000000 && (pixel(compressed.get(), 1, 0) >> 24) == 0,
        "WebP must retain opaque black ink and explicit transparent alpha");
    const auto standalone = toca::decode_webp(webp);
    expect(pixel(standalone.get(), 0, 0) == 0xff000000 && (pixel(standalone.get(), 1, 0) >> 24) == 0, "Standalone WebP must preserve explicit alpha and opaque black");
    rejects([] {
        (void)toca::decode_webp({});
    });
    rejects([&] {
        (void)toca::decode_model_texture({"truncated", Bytes(webp.begin(), webp.begin() + 20), toca::ImageEncoding::webp});
    });
    std::cout << "PASS: original BMP and bounded WebP decoding with opaque black and explicit alpha\n";
}

Bytes model(std::uint32_t flag) {
    Bytes bytes(112);
    std::memcpy(bytes.data(), "CM3D", 4);
    put32(bytes, 4, 100);
    put32(bytes, 8, 32);
    put16(bytes, 12, 1);
    put32(bytes, 20, 32);
    put32(bytes, 24, 112);
    put32(bytes, 28, 112);
    put32(bytes, 32, 80);
    put16(bytes, 36, 0xffff);
    put32(bytes, 104, flag);
    return bytes;
}

Bytes mesh_model(std::uint32_t flag, std::uint32_t first_x) {
    auto bytes = model(flag);
    bytes.resize(284);
    put16(bytes, 14, 1);
    put32(bytes, 24, 116);
    put32(bytes, 28, 284);
    put32(bytes, 32, 84);
    bytes[108] = 1;
    put16(bytes, 112, 0);
    std::memcpy(bytes.data() + 116, "body", 4);
    put32(bytes, 128, 168);
    put16(bytes, 132, 3);
    put16(bytes, 134, 1);
    put32(bytes, 164, first_x);
    put16(bytes, 262, 1);
    put16(bytes, 264, 2);
    put16(bytes, 268, 0xffff);
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    expect(bool(output), "Write synthetic resource");
}

Bytes mega_model(std::uint32_t flag = 125, std::string_view nodes = {}) {
    std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":104}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32},{"buffer":0,"byteOffset":96,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,0,0]},
{"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"meshes":[{"name":"body","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],
"nodes":[)";
    if (nodes.empty()) {
        json += R"({"matrix":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"mesh":0,"extras":{"toca_id_flags":)" + std::to_string(flag) + R"(,"toca_variants":[0]}})";
    } else {
        json += nodes;
    }
    json += R"(],"scenes":[{"nodes":[0]}],"scene":0})";
    json.append((4 - json.size() % 4) % 4, ' ');
    Bytes bytes(12 + 8 + json.size() + 8 + 104);
    put32(bytes, 0, 0x46546c67);
    put32(bytes, 4, 2);
    put32(bytes, 8, std::uint32_t(bytes.size()));
    put32(bytes, 12, std::uint32_t(json.size()));
    put32(bytes, 16, 0x4e4f534a);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    const auto offset = 20 + json.size();
    put32(bytes, offset, 104);
    put32(bytes, offset + 4, 0x004e4942);
    put32(bytes, offset + 8, std::bit_cast<std::uint32_t>(2.0f));
    put16(bytes, offset + 8 + 98, 1);
    put16(bytes, offset + 8 + 100, 2);
    return bytes;
}

Bytes car_archive(const std::vector<Bytes>& members) {
    Bytes archive(12 + members.size() * 24);
    put32(archive, 0, std::uint32_t(members.size()));
    put32(archive, 4, std::uint32_t(members.size() * 24));
    put32(archive, 8, std::uint32_t(std::ranges::max(members, {}, &Bytes::size).size()));
    std::uint32_t offset = 0;
    for (std::size_t index = 0; index < members.size(); ++index) {
        // All metadata names deliberately collide: the original consumer selects indices.
        std::memcpy(archive.data() + 12 + index * 24, "same-name", 9);
        put32(archive, 28 + index * 24, offset);
        put32(archive, 32 + index * 24, std::uint32_t(members[index].size()));
        archive.insert(archive.end(), members[index].begin(), members[index].end());
        offset += std::uint32_t(members[index].size());
    }
    return archive;
}

struct FixtureDirectory {
    std::filesystem::path directory;

    explicit FixtureDirectory(std::string_view name) : directory(std::filesystem::current_path() / name) {
        expect(std::filesystem::create_directory(directory), "Synthetic resource directory already exists");
        std::filesystem::create_directory(directory / "data");
    }

    ~FixtureDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

void frontend_image_test() {
    const FixtureDirectory fixture("frontend-image-fixture");
    const auto images = fixture.directory / "artwork";
    std::filesystem::create_directories(images / "original");
    std::filesystem::create_directory(images / "mega");
    // Invented lossless 640x480 opaque black image, independent of game media.
    const Bytes webp{0x52, 0x49, 0x46, 0x46, 0x2e, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x21, 0x00, 0x00, 0x00, 0x2f, 0x7f, 0xc2, 0x77, 0x00, 0x07,
        0x10, 0x11, 0xfd, 0x0f, 0x08, 0x48, 0x92, 0xfe, 0xff, 0x0f, 0x23, 0xfa, 0x9f, 0xf1, 0x9f, 0xff, 0xfc, 0xe7, 0x3f, 0xff, 0xf9, 0xcf, 0x7f, 0xfe, 0xf3, 0xff, 0x03, 0x00};
    for (const std::string_view name : {"title", "cpyright"}) {
        const auto load = [&] {
            return name == "title" ? toca::frontend::load_title(images) : toca::frontend::load_copyright(images);
        };
        const auto stem = images / "original" / name;
        write_bytes(images / "mega" / (std::string(name) + ".webp"), {1});
        write_bytes(stem.string() + ".webp", webp);
        const auto image = load();
        expect(image->w == 640 && image->h == 480 && pixel(image.get(), 0, 0) == 0xff000000 && pixel(image.get(), 639, 479) == 0xff000000,
            "Frontend artwork must load the complete WebP image with opaque black pixels");
        auto wrong_size = webp;
        wrong_size[21] = 0x7e; // Valid WebP header declares 639 rather than 640 pixels.
        write_bytes(stem.string() + ".webp", wrong_size);
        rejects([&] {
            (void)load();
        });
        write_bytes(stem.string() + ".webp", Bytes(webp.begin(), webp.begin() + 30));
        rejects([&] {
            (void)load();
        });
        std::filesystem::remove(stem.string() + ".webp");
        rejects([&] {
            (void)load();
        });
    }
    const auto copyright = images / "original/cpyright.webp";
    const auto mega = images / "mega/cpyright.webp";
    write_bytes(copyright, webp);
    std::filesystem::remove(mega);
    expect(toca::frontend::load_copyright(images, true)->w == 640, "Missing Mega copyright must retain the original image");
    // Invented lossless 1280x960 black image proves that the override need not use original texel dimensions.
    const Bytes high_resolution{0x52, 0x49, 0x46, 0x46, 0x54, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x48, 0x00, 0x00, 0x00, 0x2f, 0xff, 0xc4, 0xef,
        0x00, 0x07, 0x10, 0x11, 0xfd, 0x0f, 0x08, 0x48, 0x92, 0xfe, 0xff, 0x27, 0x23, 0xfa, 0x9f, 0xf1, 0x9f, 0xff, 0xfc, 0xe7, 0x3f, 0xff, 0xf9, 0xcf, 0x7f, 0xfe, 0xf3, 0x9f,
        0xff, 0xfc, 0xe7, 0x3f, 0xff, 0xf9, 0xcf, 0x7f, 0xfe, 0xf3, 0x9f, 0xff, 0xfc, 0xe7, 0x3f, 0xff, 0xf9, 0xcf, 0x7f, 0xfe, 0xf3, 0x9f, 0xff, 0xfc, 0xe7, 0x3f, 0xff, 0xf9,
        0xcf, 0x7f, 0xfe, 0xf3, 0x9f, 0xff, 0xfc, 0xe7, 0x3f, 0xff, 0x1f, 0x01};
    write_bytes(mega, high_resolution);
    const auto replacement = toca::frontend::load_copyright(images, true);
    expect(replacement->w == 1280 && replacement->h == 960, "Enabled Mega copyright must retain its higher-resolution 4:3 image");
    expect(toca::frontend::load_copyright(images, false)->w == 640, "Original detail must retain the original copyright image");
    write_bytes(mega, {1});
    expect(toca::frontend::load_copyright(images, false)->w == 640, "Original detail must not decode the Mega copyright image");
    rejects([&] {
        (void)toca::frontend::load_copyright(images, true);
    });
    auto wrong_aspect = webp;
    wrong_aspect[21] = 0x7e;
    write_bytes(mega, wrong_aspect);
    rejects([&] {
        (void)toca::frontend::load_copyright(images, true);
    });
    std::cout << "PASS: frontend WebP dimensions, opaque black, truncation, missing artwork and 4:3 Mega copyright selection\n";
}

void artwork_selection_test() {
    const FixtureDirectory fixture("artwork-selection-fixture");
    const auto root = fixture.directory / "images";
    std::filesystem::create_directories(root / "original/nested");
    std::filesystem::create_directories(root / "mega/nested");
    const auto original = root / "original/nested/image.webp";
    const auto replacement = root / "mega/nested/image.webp";
    const auto& webp = alpha_webp();
    write_bytes(original, webp);
    expect(toca::find_artwork(root, "nested/image.webp", false) == original && toca::find_artwork(root, "nested/image.webp", true) == original,
        "A missing Mega override must retain the same original image in both detail modes");
    const auto base = toca::load_artwork(root, "nested/image.webp", false);
    expect(base->w == 2 && base->h == 1 && pixel(base.get(), 0, 0) == 0xff000000 && (pixel(base.get(), 1, 0) >> 24) == 0,
        "Resolved original artwork must preserve its dimensions, opaque black and explicit alpha");
    write_bytes(replacement, webp);
    expect(toca::find_artwork(root, "nested/image.webp", true) == replacement && toca::find_artwork(root, "nested/image.webp", false) == original,
        "Only enabled Mega artwork may override the original image with the same logical name");
    write_bytes(original, {1});
    expect(pixel(toca::load_artwork(root, "nested/image.webp", true).get(), 0, 0) == 0xff000000,
        "The resolver must decode only the selected Mega image, without decoding an unused original");
    rejects([&] {
        (void)toca::load_artwork(root, "nested/image.webp", false);
    });
    write_bytes(original, webp);
    write_bytes(replacement, {2});
    expect(pixel(toca::load_artwork(root, "nested/image.webp", false).get(), 0, 0) == 0xff000000, "Disabled Mega artwork must not be decoded");
    rejects([&] {
        (void)toca::load_artwork(root, "nested/image.webp", true);
    });
    std::filesystem::remove(replacement);
    expect((pixel(toca::load_artwork(root, "nested/image.webp", true).get(), 1, 0) >> 24) == 0, "Missing Mega artwork must load the original image without losing transparency");
    write_bytes(replacement, webp);
    std::filesystem::remove(original);
    expect(!toca::find_artwork(root, "nested/image.webp", false) && toca::find_artwork(root, "nested/image.webp", true) == replacement,
        "A separate Mega-only logical variant must be available only when requested");
    expect(toca::load_artwork_file(replacement)->w == 2, "Explicit component atlas paths must share the same bounded WebP loader");
    std::filesystem::remove(replacement);
    expect(!toca::find_artwork(root, "nested/image.webp", true), "Missing original and Mega images must remain absent");
    rejects([&] {
        (void)toca::load_artwork(root, "nested/image.webp", true);
    });
    for (const auto& name :
        {std::string("../image.webp"), std::string("/image.webp"), std::string("nested//image.webp"), std::string("C:\\image.webp"), std::string("bad\0image.webp", 14)}) {
        rejects([&] {
            (void)toca::find_artwork(root, name, true);
        });
    }
    const auto outside = fixture.directory / "outside.webp";
    write_bytes(outside, webp);
    std::error_code error;
    std::filesystem::create_symlink(outside, original, error);
    if (!error) {
        rejects([&] {
            (void)toca::find_artwork(root, "nested/image.webp", false);
        });
    }
    std::cout << "PASS: original/Mega artwork selection, optional overrides, chosen-file errors, alpha and safe logical paths\n";
}

void synthetic_car_test() {
    const FixtureDirectory fixture("frontend-resource-fixture");
    const auto& directory = fixture.directory;
    std::vector<Bytes> members(34);
    for (std::size_t index = 0; index < members.size(); ++index) {
        members[index] = model(std::uint32_t(100 + index));
    }
    members[0] = {42};
    members[1] = model(11);
    members[2] = model(22);
    members[3] = model(33);
    members[25] = mesh_model(125, 65536);
    const auto archive = car_archive(members);
    write_bytes(directory / "data/allcars.bfp", archive);
    const toca::AssetStore store(directory);
    const auto car = toca::frontend::load_car_models(store, 0);
    expect(car.car_id == 0 && car.member_indices == std::array<std::size_t, 3>{1, 2, 3}, "Honda triple indices changed");
    expect(car.models[0].nodes.at(0).id_flags == 11 && car.models[1].nodes.at(0).id_flags == 22 && car.models[2].nodes.at(0).id_flags == 33,
        "Archive-owned model bytes lost original triple order");
    const std::array<std::size_t, 11> starts{1, 4, 7, 13, 16, 19, 22, 25, 28, 31, 10};
    for (std::uint32_t id = 0; id < starts.size(); ++id) {
        expect(toca::frontend::car_member_indices(id) == std::array<std::size_t, 3>{starts[id], starts[id] + 1, starts[id] + 2}, "Original car-ID/member mapping changed");
    }
    rejects([] {
        (void)toca::frontend::car_member_indices(11);
    });
    const auto replacement_path = directory / "car-7.glb";
    const auto replacement = mega_model();
    write_bytes(replacement_path, replacement);
    const auto original = toca::frontend::load_car_models(store, 7);
    const auto upgraded = toca::frontend::load_car_models(store, 7, directory);
    expect(original.models[0].meshes[0].vertices[0].position[0] == 1.0f && upgraded.models[0].meshes[0].vertices[0].position[0] == 2.0f,
        "Mega GLB did not replace geometry independently of the reference");
    expect(upgraded.member_indices == original.member_indices && upgraded.models[1].nodes[0].id_flags == 126 && upgraded.models[2].nodes[0].id_flags == 127,
        "Renault replacement changed medium/low member order");
    for (std::uint32_t id = 0; id < starts.size(); ++id) {
        if (id != 7) {
            const auto untouched = toca::frontend::load_car_models(store, id, directory);
            expect(untouched.models[0].meshes.empty(), "Renault replacement changed a different car");
        }
    }
    for (const auto& incompatible : {mega_model(126), Bytes{1, 2, 3}}) {
        write_bytes(replacement_path, incompatible);
        const auto fallback = toca::frontend::load_car_models(store, 7, directory);
        expect(fallback.models[0].meshes[0].vertices[0].position[0] == 1.0f && fallback.models[0].nodes[0].id_flags == 125,
            "Invalid or incompatible Mega files must fall back to the original highest model");
    }
    const auto missing = toca::frontend::load_car_models(store, 7, directory / "missing");
    expect(missing.models[0].meshes[0].vertices[0].position[0] == 1.0f, "Absent Mega models must retain the original highest model");
    std::cout << "PASS: original triples, Mega GLB geometry, preserved low/medium and per-car missing/invalid fallback\n";
}

using WheelMatrices = std::array<std::array<float, 16>, 5>;

WheelMatrices wheel_matrices() {
    WheelMatrices result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (index != 0) {
            result[index][12] = index <= 2 ? 1.f : -1.f;
            result[index][13] = -.25f;
            result[index][14] = index % 2 ? .625f : -.625f;
        }
    }
    return result;
}

Bytes wheel_model() {
    const auto body = mesh_model(300, 65536);
    const auto matrices = wheel_matrices();
    Bytes bytes(body.begin(), body.begin() + 32);
    bytes.resize(32 + matrices.size() * 84);
    bytes.insert(bytes.end(), body.begin() + 116, body.end());
    put16(bytes, 12, std::uint16_t(matrices.size()));
    put32(bytes, 24, 32 + std::uint32_t(matrices.size()) * 84);
    put32(bytes, 28, std::uint32_t(bytes.size()));
    for (std::size_t index = 0; index < matrices.size(); ++index) {
        const auto offset = 32 + index * 84;
        put32(bytes, offset, 84);
        put16(bytes, offset + 4, index ? 0 : 0xffff);
        for (std::size_t component = 0; component < 16; ++component) {
            put32(bytes, offset + 8 + component * 4, std::uint32_t(std::int32_t(std::lround(matrices[index][component] * 65536))));
        }
        put32(bytes, offset + 72, index ? std::uint32_t(index) : 300);
        bytes[offset + 76] = 1;
    }
    return bytes;
}

std::string wheel_nodes(const WheelMatrices& matrices) {
    std::ostringstream json;
    json.precision(std::numeric_limits<float>::max_digits10);
    for (std::size_t index = 0; index < matrices.size(); ++index) {
        json << (index ? "," : "") << R"({"matrix":[)";
        for (std::size_t component = 0; component < 16; ++component) {
            json << (component ? "," : "") << matrices[index][component];
        }
        json << R"(],"mesh":0,"extras":{"toca_id_flags":)" << (index ? index : 300) << R"(,"toca_variants":[0]})";
        if (index == 0) {
            json << R"(,"children":[1,2,3,4])";
        }
        json << '}';
    }
    return json.str();
}

void wheel_stance_test() {
    const FixtureDirectory fixture("frontend-wheel-stance-fixture");
    std::vector<Bytes> members(34, model(500));
    members[25] = wheel_model();
    write_bytes(fixture.directory / "data/allcars.bfp", car_archive(members));
    const toca::AssetStore store(fixture.directory);
    const auto original = toca::frontend::load_car_models(store, 7);
    const auto load = [&](const WheelMatrices& matrices) {
        write_bytes(fixture.directory / "car-7.glb", mega_model(300, wheel_nodes(matrices)));
        return toca::frontend::load_car_models(store, 7, fixture.directory);
    };
    const auto outset = [](float front, float rear) {
        auto matrices = wheel_matrices();
        for (std::size_t index = 1; index < matrices.size(); ++index) {
            matrices[index][14] += (index % 2 ? 1.f : -1.f) * (index <= 2 ? front : rear);
        }
        return matrices;
    };
    for (const auto& matrices : {outset(.055f, .03f), outset(.1f, .1f)}) {
        const auto upgraded = load(matrices);
        expect(upgraded.models[0].meshes[0].vertices[0].position[0] == 2.f, "Symmetric authored wheel stance was rejected");
        for (std::size_t index = 0; index < matrices.size(); ++index) {
            expect(upgraded.models[0].nodes[index].transform == matrices[index], "Accepted visual wheel pivots changed during loading");
            expect(original.models[0].nodes[index].transform == wheel_matrices()[index], "Mega loading changed the original wheel profile");
        }
        expect(upgraded.models[1].nodes[0].transform == original.models[1].nodes[0].transform && upgraded.models[2].nodes[0].transform == original.models[2].nodes[0].transform,
            "Authored wheel stance changed the original medium/low profiles");
    }
    const auto reject_stance = [&](const WheelMatrices& matrices, const char* message) {
        expect(load(matrices).models[0].meshes[0].vertices[0].position[0] == 1.f, message);
    };
    auto invalid = outset(.055f, .03f);
    invalid[2][14] += .01f;
    reject_stance(invalid, "Unequal left/right wheel outsets were accepted");
    reject_stance(outset(-.01f, .03f), "Inward wheel displacement was accepted");
    reject_stance(outset(.101f, .03f), "Wheel stance beyond 100mm per side was accepted");
    for (const auto component : {0u, 12u, 13u}) {
        invalid = outset(.055f, .03f);
        invalid[1][component] += .01f;
        reject_stance(invalid, "Wheel stance changed a source basis or nonaxial position");
    }
    invalid = outset(.055f, .03f);
    invalid[0][14] += .01f;
    reject_stance(invalid, "Wheel stance allowed a body-node translation");
    std::cout << "PASS: symmetric bounded Mega wheel stance, exact other transforms and original profile preservation\n";
}

void livery_tests() {
    toca::Model model;
    for (const auto* name : {"REN", "ListA", "listb", "Num2", "Num22"}) {
        model.textures.push_back({name, {}});
    }
    const auto livery = toca::frontend::car_livery_materials(model, 7);
    expect(livery.number_a == 3 && livery.number_b == 4 && livery.list_a == 1 && livery.list_b == 2, "Driver slots must resolve their original names case-insensitively");
    expect(livery.texture_slot(0x4000000fu, 4, false) == 3 && livery.texture_slot(15, 2, false) == 1, "First driver side must use number/name A");
    expect(livery.texture_slot(16, 3, true) == 4 && livery.texture_slot(16, 1, true) == 2, "Second driver side must use number/name B");
    expect(livery.texture_slot(9, 3, true) == 4 && livery.texture_slot(21, 1, true) == 2, "Second driver roof/rear must use number/name B");
    expect(livery.texture_slot(15, 2, true) == 2 && livery.texture_slot(16, 1, false) == 1, "Other side must retain its already correct driver slot");
    expect(livery.texture_slot(9, 1, true) == 1 && livery.texture_slot(21, 3, true) == 3, "Source swaps must stay limited to their actual numbered/named nodes");
    // Captured Renault NoQual actor14 has49a780 pairing (node15 Num2);
    // original TT actor0 has no pairing (node15 retains authored Num22).
    expect(livery.texture_slot(15, 4, false, true) == 3 && livery.texture_slot(15, 4, false, false) == 4,
        "Paired Race and unpaired Time Trial must preserve their distinct published door numbers");
    for (const bool second : {false, true}) {
        for (std::uint32_t node = 0; node < 25; ++node) {
            for (std::int16_t slot = -1; slot < 5; ++slot) {
                expect(livery.texture_slot(node, slot, second, false) == slot, "Unpaired actors must retain every authored number/name material");
            }
            expect(livery.texture_slot(node, -1, second) == -1 && livery.texture_slot(node, 0, second) == 0, "Paint and untextured faces must retain their source material");
            for (const auto render_id : {2u, 8u, 9u, 10u}) {
                const auto independent = toca::frontend::car_livery_materials(model, render_id);
                expect(independent.texture_slot(node, 3, second) == 3 && independent.texture_slot(node, 2, second) == 2,
                    "Independent Vectra and bonus-car sources must keep their material slots");
            }
        }
    }
    rejects([&] {
        (void)toca::frontend::car_livery_materials(model, 11);
    });
    model.textures.clear();
    expect(toca::frontend::car_livery_materials(model, 7).texture_slot(15, 4, false) == 4, "Absent name/number slots must keep the original material");
    std::cout << "PASS: source-backed first/second driver node material mappings with shared model slots\n";
}

void reference_tests(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    std::size_t model_count = 0, mesh_count = 0;
    for (std::uint32_t id = 0; id < 11; ++id) {
        const auto car = toca::frontend::load_car_models(store, id);
        for (const auto& model : car.models) {
            ++model_count;
            mesh_count += model.meshes.size();
            for (const auto& texture : model.textures) {
                const auto decoded = toca::decode_bmp(texture.bmp);
                expect(decoded->w > 0 && decoded->h > 0, "Embedded car BMP dimensions are invalid");
            }
        }
        const auto& high = car.models[0];
        const auto livery = toca::frontend::car_livery_materials(high, id);
        const bool paired = id != 2 && id != 8 && id != 9 && id != 10;
        std::array<std::size_t, 2> changed{};
        for (const auto& node : high.nodes) {
            for (const auto mesh : node.variants) {
                if (mesh == 0xffff) {
                    continue;
                }
                for (const auto& face : high.meshes.at(mesh).faces) {
                    for (const bool second : {false, true}) {
                        const auto mapped = livery.texture_slot(node.id_flags, face.texture, second);
                        changed[second] += mapped != face.texture;
                        expect(mapped >= -1 && (mapped < 0 || std::size_t(mapped) < high.textures.size()), "Driver mapping exceeded the existing texture slots");
                    }
                }
            }
        }
        expect((changed[0] > 0) == paired && (changed[1] > 0) == paired, "Original team driver bindings must match both source high-model variants");
        if (id == 0) {
            expect(car.models[0].nodes.size() == 25 && car.models[0].meshes.size() == 50 && car.models[0].textures.size() == 9, "Honda first member geometry changed");
        }
    }
    expect(model_count == 33 && mesh_count == 696, "Direct original archive corpus counts changed");
    std::cout << "PASS: all 33 directly archived car models (696 meshes), including embedded BMPs\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            image_tests();
            frontend_image_test();
            artwork_selection_test();
            synthetic_car_test();
            wheel_stance_test();
            livery_tests();
        } else if (argc == 2) {
            reference_tests(argv[1]);
        } else {
            throw std::runtime_error("Expected an optional canonical data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
