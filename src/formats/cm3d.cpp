#include "cm3d.hpp"

#include <bit>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace toca {
namespace {
void require(bool valid, const char* reason) {
    if (!valid) {
        throw std::runtime_error(std::string("CM3D: ") + reason);
    }
}

class Bytes {
public:
    explicit Bytes(std::span<const std::uint8_t> data) : data_(data) {}

    std::span<const std::uint8_t> at(std::size_t offset, std::size_t length) const {
        require(offset <= data_.size() && length <= data_.size() - offset, "truncated section");
        return data_.subspan(offset, length);
    }

    std::uint16_t u16(std::size_t offset) const {
        auto p = at(offset, 2);
        return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
    }

    std::int16_t i16(std::size_t offset) const {
        return std::bit_cast<std::int16_t>(u16(offset));
    }

    std::uint32_t u32(std::size_t offset) const {
        auto p = at(offset, 4);
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    }

    float fixed(std::size_t offset) const {
        return static_cast<float>(std::bit_cast<std::int32_t>(u32(offset))) / 65536.0f;
    }

    std::string name(std::size_t offset, std::size_t length) const {
        auto p = at(offset, length);
        std::string result;
        for (auto c : p) {
            if (c == 0) {
                break;
            }
            result += static_cast<char>(c);
        }
        return result;
    }

private:
    std::span<const std::uint8_t> data_;
};
} // namespace

Model parse_cm3d(std::span<const std::uint8_t> data) {
    const Bytes bytes(data);
    require(bytes.name(0, 4) == "CM3D", "bad magic");
    require(bytes.u32(4) == 100 && bytes.u32(8) == 32, "unsupported header/version");
    const auto node_count = bytes.u16(12), mesh_count = bytes.u16(14), texture_count = bytes.u16(16);
    const std::size_t node_start = bytes.u32(20), mesh_start = bytes.u32(24), texture_start = bytes.u32(28);
    require(node_start == 32 && node_start <= mesh_start && mesh_start <= texture_start && texture_start <= data.size(), "invalid section offsets");
    Model model;
    std::size_t offset = node_start;
    for (unsigned i = 0; i < node_count; ++i) {
        require(offset <= mesh_start && mesh_start - offset >= 80, "truncated node");
        const std::size_t size = bytes.u32(offset);
        const auto count = bytes.at(offset + 76, 1)[0];
        require(size == ((80u + 2u * count + 3u) & ~3u) && size <= mesh_start - offset, "invalid node size");
        ModelNode node{};
        node.parent = bytes.i16(offset + 4);
        require(node.parent >= -1 && node.parent < static_cast<int>(i), "invalid parent order");
        for (unsigned n = 0; n < 16; ++n) {
            node.transform[n] = bytes.fixed(offset + 8 + n * 4);
        }
        node.id_flags = bytes.u32(offset + 72);
        for (unsigned n = 0; n < count; ++n) {
            const auto index = bytes.u16(offset + 80 + n * 2);
            require(index == 0xffff || index < mesh_count, "invalid mesh reference");
            node.variants.push_back(index);
        }
        model.nodes.push_back(std::move(node));
        offset += size;
    }
    require(offset == mesh_start, "unaccounted node bytes");

    for (unsigned i = 0; i < mesh_count; ++i) {
        require(offset <= texture_start && texture_start - offset >= 48, "truncated mesh");
        const std::size_t size = bytes.u32(offset + 12);
        const auto vertex_count = bytes.u16(offset + 16), face_count = bytes.u16(offset + 18);
        require(size == 48u + 32u * vertex_count + 24u * face_count && size <= texture_start - offset, "invalid mesh size");
        ModelMesh mesh;
        mesh.name = bytes.name(offset, 12);
        std::size_t cursor = offset + 48;
        for (unsigned n = 0; n < vertex_count; ++n, cursor += 32) {
            ModelVertex vertex{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                vertex.position[axis] = bytes.fixed(cursor + axis * 4);
                vertex.normal[axis] = bytes.fixed(cursor + 20 + axis * 4);
            }
            // Match the UV adjustments in 0x00494a40, rather than flipping/guessing V.
            vertex.u = bytes.fixed(cursor + 12);
            if (vertex.u > 1.0f) {
                vertex.u -= 1.0f;
            }
            vertex.v = bytes.fixed(cursor + 16) + 1.0f;
            mesh.vertices.push_back(vertex);
        }
        for (unsigned n = 0; n < face_count; ++n, cursor += 24) {
            ModelFace face{};
            for (unsigned corner = 0; corner < 3; ++corner) {
                face.indices[corner] = bytes.u16(cursor + 2 + corner * 2);
                require(face.indices[corner] < vertex_count, "face vertex out of range");
            }
            face.texture = bytes.i16(cursor + 8);
            require(face.texture >= -1 && face.texture < texture_count, "face texture out of range");
            const auto rgba = bytes.at(cursor + 10, 4);
            for (unsigned channel = 0; channel < 4; ++channel) {
                face.color[channel] = rgba[channel];
            }
            mesh.faces.push_back(face);
        }
        model.meshes.push_back(std::move(mesh));
        offset += size;
    }
    require(offset == texture_start, "unaccounted mesh bytes");

    // 0x00495ee0: 12-byte name, u32 payload size, then embedded BMP bytes.
    for (unsigned i = 0; i < texture_count; ++i) {
        ModelTexture texture;
        texture.name = bytes.name(offset, 12);
        const auto size = bytes.u32(offset + 12);
        const auto bmp = bytes.at(offset + 16, size);
        require(bmp.size() >= 54 && bmp[0] == 'B' && bmp[1] == 'M', "invalid embedded BMP");
        texture.bmp.assign(bmp.begin(), bmp.end());
        model.textures.push_back(std::move(texture));
        offset += 16 + std::size_t(size);
    }
    // Original 00494a40/00495ee0 consume counted sections only. The scene
    // OBJECTS/LOWOBJ/ZEROBJ files retain one unused null after those sections.
    model.trailing_null_byte = offset < data.size() && data.size() - offset == 1 && data[offset] == 0;
    require(offset == data.size() || model.trailing_null_byte, "unaccounted texture bytes");
    return model;
}

Model load_cm3d(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open model: " + path.string());
    }
    const auto size = file.tellg();
    require(size >= 0 && size <= 256 * 1024 * 1024, "invalid file size");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read model: " + path.string());
    }
    return parse_cm3d(data);
}
} // namespace toca
