#pragma once

#include "formats/cm3d.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::detail {
// The terrain and static consumers both run 00496980 on raw Q16 fields.
// Keep their placement/selection policies separate from this shared mesh bake.
class FixedCm3dBaker {
    using FixedVector = std::array<std::int32_t, 3>;
    std::span<const std::uint8_t> bytes_;
    const Model& model_;
    const char* truncated_message_;
    std::vector<std::size_t> meshes_;
    std::vector<std::vector<FixedVector>> normals_;

    std::int32_t fixed(std::size_t position) const {
        return std::bit_cast<std::int32_t>(u32(position));
    }

    FixedVector vector(std::size_t position) const {
        return {fixed(position), fixed(position + 4), fixed(position + 8)};
    }

    static FixedVector transform(const FixedVector& value, const std::array<std::int32_t, 16>& matrix, bool position) {
        FixedVector result;
        for (std::size_t row = 0; row < result.size(); ++row) {
            std::uint32_t sum = position ? std::bit_cast<std::uint32_t>(matrix[12 + row]) : 0;
            for (std::size_t column = 0; column < value.size(); ++column) {
                // FUN_0047c7e0/0047c900 shifts EACH signed product before adding.
                // Unsigned wrapping reproduces its original x86 32-bit additions.
                const auto product = std::int64_t(matrix[column * 4 + row]) * value[column];
                sum += std::uint32_t(std::uint64_t(product) >> 16);
            }
            result[row] = std::bit_cast<std::int32_t>(sum);
        }
        return result;
    }

    static std::array<float, 3> converted(const FixedVector& value) {
        return {float(value[0]) / 65536.f, float(value[1]) / 65536.f, float(value[2]) / 65536.f};
    }

public:
    // parse_cm3d must validate the container, references and sections first.
    FixedCm3dBaker(std::span<const std::uint8_t> bytes, const Model& model, const char* truncated_message) : bytes_(bytes), model_(model), truncated_message_(truncated_message) {
        auto position = std::size_t(u32(24));
        for (const auto& mesh : model_.meshes) {
            meshes_.push_back(position);
            auto& working = normals_.emplace_back();
            for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
                working.push_back(vector(position + 48 + vertex * 32 + 20));
            }
            position += u32(position + 12);
        }
    }

    std::uint32_t u32(std::size_t position) const {
        if (position > bytes_.size() || bytes_.size() - position < 4) {
            throw std::runtime_error(truncated_message_);
        }
        return std::uint32_t(bytes_[position]) | (std::uint32_t(bytes_[position + 1]) << 8) | (std::uint32_t(bytes_[position + 2]) << 16)
               | (std::uint32_t(bytes_[position + 3]) << 24);
    }

    std::array<std::int32_t, 16> node_matrix(std::size_t position) const {
        std::array<std::int32_t, 16> matrix;
        for (std::size_t index = 0; index < matrix.size(); ++index) {
            matrix[index] = fixed(position + 8 + index * 4);
        }
        return matrix;
    }

    ModelMesh bake_mesh(std::size_t mesh_index, const std::array<std::int32_t, 16>& matrix) {
        auto mesh = model_.meshes.at(mesh_index);
        const auto start = meshes_.at(mesh_index) + 48;
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const auto vertex = start + index * 32;
            auto& target = mesh.vertices[index];
            target.position = converted(transform(vector(vertex), matrix, true));
            // FUN_00496980 mutates the source mesh normals after each use.
            // Repeated references therefore consume the previous fixed result.
            auto& normal = normals_.at(mesh_index).at(index);
            normal = transform(normal, matrix, false);
            target.normal = converted(normal);
            // Read ORIGINAL UVs. Generic decoded UVs are not a reversible input.
            auto u = float(fixed(vertex + 12)) / 65536.f;
            if (u > 1.f) {
                u -= 1.f;
            }
            const auto v = float(fixed(vertex + 16)) / 65536.f + 1.f;
            target.u = std::clamp(u, .02f, .99f);
            target.v = std::clamp(v, .02f, .99f);
        }
        return mesh;
    }

    std::vector<std::uint8_t> face_flags(std::size_t mesh_index) const {
        const auto& mesh = model_.meshes.at(mesh_index);
        const auto faces = meshes_.at(mesh_index) + 48 + mesh.vertices.size() * 32;
        std::vector<std::uint8_t> flags;
        for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
            flags.push_back(bytes_[faces + index * 24 + 23]);
        }
        return flags;
    }
};
} // namespace toca::detail
