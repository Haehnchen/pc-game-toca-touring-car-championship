#include "formats/glb.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes.at(offset + index) = std::uint8_t(value >> (index * 8));
    }
}

Bytes glb(std::string json, Bytes binary) {
    while (json.size() % 4) {
        json += ' ';
    }
    while (binary.size() % 4) {
        binary.push_back(0);
    }
    Bytes bytes(20);
    put32(bytes, 0, 0x46546c67);
    put32(bytes, 4, 2);
    put32(bytes, 8, std::uint32_t(28 + json.size() + binary.size()));
    put32(bytes, 12, std::uint32_t(json.size()));
    put32(bytes, 16, 0x4e4f534a);
    bytes.insert(bytes.end(), json.begin(), json.end());
    const auto offset = bytes.size();
    bytes.resize(offset + 8);
    put32(bytes, offset, std::uint32_t(binary.size()));
    put32(bytes, offset + 4, 0x004e4942);
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    return bytes;
}

struct Fixture {
    std::string json = R"({"asset":{"version":"2.0"},"extensionsUsed":["EXT_texture_webp"],"extensionsRequired":["EXT_texture_webp"],
"buffers":[{"byteLength":138}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":34}],
"accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"images":[{"name":"REN","mimeType":"image/webp","bufferView":2}],"textures":[{"extensions":{"EXT_texture_webp":{"source":0}}}],
"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.0666666667,0.1333333333,0.2,1]}},{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"baseColorTexture":{"index":0}}}],
"meshes":[{"name":"body","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0},{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":1}]}],
"nodes":[{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"children":[1],"extras":{"toca_id_flags":2147483649,"toca_variants":[]}},{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1],"mesh":0,"extras":{"toca_id_flags":17,"toca_variants":[0,65535]}}],"scenes":[{"nodes":[0]}],"scene":0})";
    Bytes binary = Bytes(104);

    Fixture() {
        constexpr std::array<float, 24> vertices{0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1};
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            put32(binary, index * 4, std::bit_cast<std::uint32_t>(vertices[index]));
        }
        binary[98] = 1;
        binary[100] = 2;
        const Bytes image{0x52, 0x49, 0x46, 0x46, 0x1a, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0e, 0x00, 0x00, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x10,
            0x0f, 0x10, 0x11, 0xf3, 0x1f, 0x22, 0x23, 0xfa, 0x1f};
        binary.insert(binary.end(), image.begin(), image.end());
    }

    void replace(std::string_view from, std::string_view to) {
        const auto position = json.find(from);
        expect(position != std::string::npos, "Missing synthetic JSON replacement");
        json.replace(position, from.size(), to);
    }

    Bytes encode() const {
        return glb(json, binary);
    }

    void ghost_positions() {
        // Append an independent float32 table; both primitives must share it,
        // even though only the second primitive is an inner housing.
        binary.resize(176);
        constexpr std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 0, .2f};
        for (std::size_t index = 0; index < positions.size(); ++index) {
            put32(binary, 140 + index * 4, std::bit_cast<std::uint32_t>(positions[index]));
        }
        replace(R"("byteLength":138)", R"("byteLength":176)");
        replace(R"("byteLength":34}])", R"("byteLength":34},{"buffer":0,"byteOffset":140,"byteLength":36}])");
        replace(R"("type":"SCALAR"}])", R"("type":"SCALAR"},{"bufferView":3,"componentType":5126,"count":3,"type":"VEC3"}])");
        for (unsigned primitive = 0; primitive < 2; ++primitive) {
            replace(R"("TEXCOORD_0":2})", R"("TEXCOORD_0":2,"_TOCA_GHOST_POSITION":4})");
        }
        replace(R"("indices":3,"material":1)", R"("indices":3,"material":1,"extras":{"toca_ghost_hidden":true})");
    }

    void compress() {
        // meshoptimizer0.25 version0 streams: the same float32 vertices and
        // exact index sequence as above, with no uncompressed fallback bytes.
        const Bytes vertices{0xa0, 0x00, 0x00, 0x01, 0x3c, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x3c, 0x00, 0x00, 0x00, 0x7e, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0c,
            0x00, 0x00, 0x00, 0xff, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x3c, 0x00, 0x00,
            0x00, 0xff, 0xff, 0x01, 0x3c, 0x00, 0x00, 0x00, 0x7e, 0x7d, 0x00, 0x00, 0x01, 0x0c, 0x00, 0x00, 0x00, 0xff, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        const Bytes image(binary.begin() + 104, binary.end());
        binary = vertices;
        binary.resize(112);
        binary.insert(binary.end(), {0xd0, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00});
        binary.insert(binary.end(), image.begin(), image.end());
        replace(R"("extensionsRequired":["EXT_texture_webp"])", R"("extensionsRequired":["EXT_texture_webp","EXT_meshopt_compression"])");
        replace(R"("buffers":[{"byteLength":138}])", R"("buffers":[{"byteLength":154},{"byteLength":102,"extensions":{"EXT_meshopt_compression":{"fallback":true}}}])");
        replace(R"({"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32})",
            R"({"buffer":1,"byteOffset":0,"byteLength":96,"byteStride":32,"extensions":{"EXT_meshopt_compression":{"buffer":0,"byteOffset":0,"byteLength":109,"byteStride":32,"count":3,"mode":"ATTRIBUTES","filter":"NONE"}}})");
        replace(R"({"buffer":0,"byteOffset":96,"byteLength":6})",
            R"({"buffer":1,"byteOffset":96,"byteLength":6,"extensions":{"EXT_meshopt_compression":{"buffer":0,"byteOffset":112,"byteLength":8,"byteStride":2,"count":3,"mode":"INDICES","filter":"NONE"}}})");
        replace(R"("byteOffset":104)", R"("byteOffset":120)");
    }
};

template <class Edit> void rejects(Edit edit) {
    Fixture fixture;
    edit(fixture);
    try {
        (void)toca::parse_glb(fixture.encode());
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Malformed embedded model was accepted");
}
} // namespace

int main() {
    try {
        const auto model = toca::parse_glb(Fixture{}.encode());
        expect(model.nodes.size() == 2 && model.nodes[0].parent == -1 && model.nodes[1].parent == 0 && model.nodes[0].id_flags == 2147483649u,
            "Original hierarchy or unsigned node flags changed");
        expect(model.nodes[1].variants == std::vector<std::uint16_t>{0, 65535} && model.nodes[1].transform[12] == 2 && model.nodes[1].transform[14] == 4,
            "Original matrix and damage variants changed");
        expect(model.meshes.size() == 1 && model.meshes[0].vertices.size() == 3 && model.meshes[0].faces.size() == 2 && model.meshes[0].vertices[2].v == 1,
            "Shared vertex tables or runtime UV values changed");
        expect(model.meshes[0].faces[0].texture == -1 && model.meshes[0].faces[0].color == std::array<std::uint8_t, 4>{17, 34, 51, 255} && model.meshes[0].faces[1].texture == 0,
            "Flat material colors and image slots changed");
        Fixture ghost_hidden;
        ghost_hidden.ghost_positions();
        const auto marked = toca::parse_glb(ghost_hidden.encode());
        expect(!marked.meshes[0].faces[0].ghost_hidden && marked.meshes[0].faces[1].ghost_hidden,
            "Ghost-only face metadata must survive GLB decoding without changing ordinary faces");
        expect(model.meshes[0].ghost_positions.empty() && marked.meshes[0].ghost_positions.size() == 3 && marked.meshes[0].ghost_positions[2] == std::array<float, 3>{0, 0, .2f}
                   && marked.meshes[0].vertices[2].position == model.meshes[0].vertices[2].position,
            "Authored Ghost positions must remain separate from ordinary geometry");
        rejects([](auto& fixture) {
            fixture.replace(R"("indices":3,"material":1)", R"("indices":3,"material":1,"extras":{"toca_ghost_hidden":true})");
        });
        rejects([](auto& fixture) {
            fixture.ghost_positions();
            fixture.replace(R"("componentType":5126,"count":3,"type":"VEC3"}])", R"("componentType":5126,"count":2,"type":"VEC3"}])");
        });
        rejects([](auto& fixture) {
            fixture.ghost_positions();
            put32(fixture.binary, 172, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
        });
        rejects([](auto& fixture) {
            fixture.ghost_positions();
            fixture.replace(R"(,"_TOCA_GHOST_POSITION":4)", "");
        });
        rejects([](auto& fixture) {
            fixture.ghost_positions();
            fixture.replace(R"("_TOCA_GHOST_POSITION":4)", R"("_TOCA_GHOST_POSITION":0)");
        });
        expect(model.textures.size() == 1 && model.textures[0].name == "REN" && model.textures[0].encoding == toca::ImageEncoding::webp && model.textures[0].bmp.size() == 34,
            "Embedded WebP payload changed");
        Fixture scenery_normals;
        // Croft's CR03BUS2_1 has Y=4, which must retain its original lighting
        // contribution. Signed16.16 extrema also remain untouched as float32.
        put32(scenery_normals.binary, 12, std::bit_cast<std::uint32_t>(-32768.f));
        put32(scenery_normals.binary, 16, std::bit_cast<std::uint32_t>(4.f));
        put32(scenery_normals.binary, 20, std::bit_cast<std::uint32_t>(32768.f));
        const auto scenery = toca::parse_glb(scenery_normals.encode());
        expect(scenery.meshes[0].vertices[0].normal == std::array<float, 3>{-32768.f, 4.f, 32768.f}, "Canonical signed16.16 scenery normals were changed");
        Fixture compressed;
        compressed.compress();
        const auto decoded = toca::parse_glb(compressed.encode());
        expect(decoded.meshes[0].vertices.size() == model.meshes[0].vertices.size() && decoded.meshes[0].faces.size() == model.meshes[0].faces.size()
                   && decoded.nodes[1].variants == model.nodes[1].variants && decoded.textures[0].bmp == model.textures[0].bmp,
            "Compressed geometry changed hierarchy, materials or image payloads");
        for (std::size_t index = 0; index < model.meshes[0].vertices.size(); ++index) {
            const auto& actual = decoded.meshes[0].vertices[index];
            const auto& expected = model.meshes[0].vertices[index];
            expect(actual.position == expected.position && actual.normal == expected.normal && actual.u == expected.u && actual.v == expected.v,
                "Meshopt compression changed exact float vertex attributes");
        }
        expect(decoded.meshes[0].faces[0].indices == model.meshes[0].faces[0].indices && decoded.meshes[0].faces[1].indices == model.meshes[0].faces[1].indices,
            "Meshopt compression changed triangle corner order");
        rejects([](auto& fixture) {
            fixture.compress();
            fixture.binary[0] = 0;
        });
        rejects([](auto& fixture) {
            fixture.compress();
            fixture.replace(R"("count":3,"mode":"ATTRIBUTES")", R"("count":4294967295,"mode":"ATTRIBUTES")");
        });
        rejects([](auto& fixture) {
            fixture.compress();
            fixture.replace(R"("byteLength":109)", R"("byteLength":99999)");
        });
        rejects([](auto& fixture) {
            fixture.compress();
            fixture.replace(R"("byteLength":102,"extensions")", R"("byteLength":102,"uri":"outside.bin","extensions")");
        });
        rejects([](auto& fixture) {
            fixture.replace("\"byteLength\":138", "\"byteLength\":138,\"uri\":\"outside.bin\"");
        });
        rejects([](auto& fixture) {
            fixture.replace("\"mimeType\":\"image/webp\"", "\"uri\":\"outside.webp\",\"mimeType\":\"image/webp\"");
        });
        rejects([](auto& fixture) {
            fixture.replace("[0,65535]", "[0,65536]");
        });
        rejects([](auto& fixture) {
            fixture.replace("\"count\":3", "\"count\":999999999");
        });
        rejects([](auto& fixture) {
            fixture.replace("\"byteOffset\":12", "\"byteOffset\":13");
        });
        rejects([](auto& fixture) {
            fixture.binary[100] = 9;
        });
        rejects([](auto& fixture) {
            put32(fixture.binary, 0, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
        });
        rejects([](auto& fixture) {
            put32(fixture.binary, 16, std::bit_cast<std::uint32_t>(std::nextafter(32768.f, std::numeric_limits<float>::infinity())));
        });
        rejects([](auto& fixture) {
            fixture.replace("\"toca_id_flags\":17", "\"toca_id_flags\":4294967296");
        });
        rejects([](auto& fixture) {
            fixture.replace(R"("indices":3,"material":1)", R"("indices":3,"material":1,"extras":{"toca_ghost_hidden":false})");
        });
        std::cout << "PASS: embedded GLB geometry, original hierarchy/damage/materials, WebP slots and bounded validation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
