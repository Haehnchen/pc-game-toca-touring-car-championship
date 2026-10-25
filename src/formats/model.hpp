#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace toca {
struct ModelVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    float u, v;
};

static_assert(sizeof(ModelVertex) == 32);

struct ModelFace {
    std::array<std::uint16_t, 3> indices;
    std::int16_t texture;
    std::array<std::uint8_t, 4> color;
    // Authored interior marker; Ghost uses the mesh's alternate positions.
    bool ghost_hidden = false;
};

struct ModelMesh {
    std::string name;
    std::vector<ModelVertex> vertices;
    std::vector<ModelFace> faces;
    // Mega interior faces use a shallow, body-attached shape only in Ghost.
    std::vector<std::array<float, 3>> ghost_positions{};
};

struct ModelNode {
    std::int16_t parent;
    std::array<float, 16> transform;
    std::uint32_t id_flags;
    std::vector<std::uint16_t> variants;
};

enum class ImageEncoding {
    bmp,
    webp
};

struct ModelTexture {
    std::string name;
    // Encoded image bytes. Canonical resources retain their original BMP;
    // native GLB assets carry WebP with explicit alpha instead of a black key.
    std::vector<std::uint8_t> bmp;
    ImageEncoding encoding = ImageEncoding::bmp;
};

struct Model {
    std::vector<ModelNode> nodes;
    std::vector<ModelMesh> meshes;
    std::vector<ModelTexture> textures;
    bool trailing_null_byte = false;
};
} // namespace toca
