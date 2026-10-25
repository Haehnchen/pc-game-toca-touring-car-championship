#include "assets/store.hpp"
#include "formats/cm3d.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t root = 32, child = 112, mesh = 196, vertex = 244, face = 340, texture = 364, bmp = 380;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& data, std::size_t offset, std::uint16_t value) {
    data.at(offset) = value & 255;
    data.at(offset + 1) = value >> 8;
}

void put32(Bytes& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        data.at(offset + i) = (value >> (8 * i)) & 255;
    }
}

// Invented two-node model and one textured triangle; no proprietary bytes.
Bytes fixture() {
    Bytes data(438);
    data[0] = 'C';
    data[1] = 'M';
    data[2] = '3';
    data[3] = 'D';
    put32(data, 4, 100);
    put32(data, 8, 32);
    put16(data, 12, 2);
    put16(data, 14, 1);
    put16(data, 16, 1);
    put32(data, 20, root);
    put32(data, 24, mesh);
    put32(data, 28, texture);
    put32(data, root, 80);
    put16(data, root + 4, 0xffff);
    put32(data, child, 84);
    put16(data, child + 4, 0);
    for (const auto node : {root, child}) {
        for (unsigned i = 0; i < 4; ++i) {
            put32(data, node + 8 + i * 20, 65536);
        }
    }
    put32(data, child + 56, std::uint32_t(-163840)); // X translation: -2.5
    put32(data, child + 72, 0xa0000001);
    data[child + 76] = 1;
    put16(data, child + 80, 0);
    data[mesh] = 'T';
    put32(data, mesh + 12, 168);
    put16(data, mesh + 16, 3);
    put16(data, mesh + 18, 1);
    put32(data, vertex, std::uint32_t(-32768));
    put32(data, vertex + 12, 81920); // Original U=1.25, decoded U=0.25.
    put32(data, vertex + 16, std::uint32_t(-32768)); // Original V=-0.5.
    put32(data, vertex + 32, 65536);
    put32(data, vertex + 68, 65536);
    for (unsigned i = 0; i < 3; ++i) {
        put32(data, vertex + i * 32 + 24, 65536);
    }
    put16(data, face + 2, 0);
    put16(data, face + 4, 1);
    put16(data, face + 6, 2);
    put16(data, face + 8, 0);
    data[face + 10] = 10;
    data[face + 11] = 20;
    data[face + 12] = 30;
    data[face + 13] = 255;
    data[texture] = 'B';
    put32(data, texture + 12, 58);
    data[bmp] = 'B';
    data[bmp + 1] = 'M';
    put32(data, bmp + 2, 58);
    put32(data, bmp + 10, 54);
    put32(data, bmp + 14, 40);
    put32(data, bmp + 18, 1);
    put32(data, bmp + 22, 1);
    put16(data, bmp + 26, 1);
    put16(data, bmp + 28, 24);
    put32(data, bmp + 34, 4);
    data[bmp + 54] = 30;
    data[bmp + 55] = 20;
    data[bmp + 56] = 10;
    return data;
}

void rejects(std::span<const std::uint8_t> data) {
    try {
        (void)toca::parse_cm3d(data);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Malformed CM3D accepted");
}

void synthetic_tests() {
    const auto data = fixture();
    const auto model = toca::parse_cm3d(data);
    expect(model.nodes.size() == 2 && model.meshes.size() == 1 && model.textures.size() == 1, "Incorrect section counts");
    expect(model.nodes[0].parent == -1 && model.nodes[0].variants.empty() && model.nodes[1].parent == 0 && model.nodes[1].variants == std::vector<std::uint16_t>{0}
               && model.nodes[1].transform[12] == -2.5f && model.nodes[1].id_flags == 0xa0000001,
        "Hierarchy, odd variant padding or signed matrix conversion changed");
    const auto& m = model.meshes[0];
    expect(m.name == "T" && m.vertices.size() == 3 && m.faces.size() == 1 && m.vertices[0].position[0] == -0.5f && m.vertices[0].normal[1] == 1 && m.vertices[0].u == 0.25f
               && m.vertices[0].v == 0.5f,
        "Vertex layout or original UV adjustment changed");
    expect(m.faces[0].indices == std::array<std::uint16_t, 3>{0, 1, 2} && m.faces[0].texture == 0 && m.faces[0].color == std::array<std::uint8_t, 4>{10, 20, 30, 255}
               && model.textures[0].name == "B" && model.textures[0].bmp == Bytes(data.begin() + bmp, data.end()),
        "Face or embedded texture data changed");

    for (std::size_t length = 0; length < data.size(); ++length) {
        rejects(std::span(data).first(length));
    }
    for (const auto [offset, value] : std::vector<std::pair<std::size_t, std::uint32_t>>{
             {0, 0}, {4, 101}, {8, 31}, {20, 31}, {24, 31}, {28, 0xffffffff}, {root, 79}, {child, 82}, {mesh + 12, 167}, {texture + 12, 0xffffffff}, {bmp, 0}}) {
        auto broken = data;
        put32(broken, offset, value);
        rejects(broken);
    }
    for (const auto [offset, value] :
        std::vector<std::pair<std::size_t, std::uint16_t>>{{root + 4, 0}, {root + 4, 0xfffe}, {child + 4, 1}, {child + 80, 1}, {face + 2, 3}, {face + 8, 1}, {face + 8, 0xfffe}}) {
        auto broken = data;
        put16(broken, offset, value);
        rejects(broken);
    }
    auto tail = data;
    tail.push_back(0);
    const auto terminated = toca::parse_cm3d(tail);
    expect(!model.trailing_null_byte && terminated.trailing_null_byte && terminated.textures[0].bmp == model.textures[0].bmp,
        "Observed single null end-byte variant must be retained separately from counted texture data");
    tail.back() = 1;
    rejects(tail);
    tail.back() = 0;
    tail.push_back(0);
    rejects(tail);

    auto solid = data;
    put16(solid, face + 8, 0xffff);
    put16(solid, child + 80, 0xffff);
    put32(solid, vertex + 12, 65536);
    put32(solid, vertex + 16, std::uint32_t(-65536));
    const auto edge = toca::parse_cm3d(solid);
    expect(edge.meshes[0].faces[0].texture == -1 && edge.nodes[1].variants[0] == 0xffff && edge.meshes[0].vertices[0].u == 1 && edge.meshes[0].vertices[0].v == 0,
        "Absent mesh, solid material or UV boundary rejected");
    std::cout << "PASS: hierarchy, fixed-point vertices/UVs, materials, embedded BMP, "
                 "all 438 truncations and malformed fields\n";
}

void reference_tests(const std::filesystem::path& directory) {
    const toca::AssetStore store(directory);
    const auto archive = store.open_archive("data/allcars.bfp");
    std::size_t models = 0, meshes = 0;
    for (std::size_t index = 0; index < archive.size(); ++index) {
        const auto bytes = archive.member(index).bytes();
        if (bytes.size() < 4 || !std::equal(bytes.begin(), bytes.begin() + 4, "CM3D")) {
            continue;
        }
        const auto model = toca::parse_cm3d(bytes);
        ++models;
        meshes += model.meshes.size();
    }
    expect(models == 33 && meshes == 696, "Car corpus count changed");
    const auto honda = toca::parse_cm3d(archive.member(1).bytes());
    std::size_t triangles = 0;
    for (const auto& node : honda.nodes) {
        if (!node.variants.empty() && node.variants[0] != 0xffff) {
            triangles += honda.meshes.at(node.variants[0]).faces.size();
        }
    }
    expect(honda.nodes.size() == 25 && honda.meshes.size() == 50 && honda.textures.size() == 9 && triangles == 317, "Honda geometry counts changed");
    expect(honda.nodes[2].parent == 0 && honda.nodes[2].variants[0] == 49 && std::abs(honda.nodes[2].transform[12] - 1.378f) < 0.001f, "Honda wheel placement changed");
    std::cout << "PASS: 33 bundled car models, 696 meshes; Honda hierarchy and 317 triangles\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic_tests();
        } else if (argc == 2) {
            reference_tests(argv[1]);
        } else {
            throw std::runtime_error("Expected optional bundled game-data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
