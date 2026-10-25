#pragma once

#include "core/crc32.hpp"
#include "render/gpu_renderer.hpp"

#include <cmath>
#include <map>

namespace toca::scene {
// Immutable object-space corners shared by models and split views of one
// scene. Poses, lighting and material selection remain frame-owned.
class ModelMeshes {
    gpu::Renderer& renderer_;
    std::map<std::uint32_t, std::vector<std::shared_ptr<const gpu::WorldMesh>>> entries_;

    template <class T> static std::span<const std::uint8_t> bytes(const std::vector<T>& values) {
        return {reinterpret_cast<const std::uint8_t*>(values.data()), values.size() * sizeof(T)};
    }

public:
    explicit ModelMeshes(gpu::Renderer& renderer) : renderer_(renderer) {}

    ModelMeshes(const ModelMeshes&) = delete;
    ModelMeshes& operator=(const ModelMeshes&) = delete;

    void require_renderer(const gpu::Renderer& renderer) const {
        if (&renderer != &renderer_) {
            throw std::runtime_error("Shared model meshes require the same renderer");
        }
    }

    std::shared_ptr<const gpu::WorldMesh> get(std::vector<gpu::WorldVertex> vertices, std::vector<gpu::ModelAttribute> attributes) {
        if (vertices.size() != attributes.size()) {
            throw std::runtime_error("Model mesh attributes must match source corners");
        }
        const auto finite = [](const auto& values) {
            return std::ranges::all_of(values, [](float value) {
                return std::isfinite(value);
            });
        };
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const auto& vertex = vertices[index];
            const auto& attribute = attributes[index];
            if (!finite(vertex.position) || !std::isfinite(vertex.u) || !std::isfinite(vertex.v) || !finite(attribute.normal) || !finite(attribute.ghost_position)) {
                throw std::runtime_error("Model mesh requires finite source geometry, UVs and normals");
            }
        }
        const auto vertex_bytes = bytes(vertices), attribute_bytes = bytes(attributes);
        std::vector<std::uint8_t> encoded;
        encoded.reserve(vertex_bytes.size() + attribute_bytes.size());
        encoded.insert(encoded.end(), vertex_bytes.begin(), vertex_bytes.end());
        encoded.insert(encoded.end(), attribute_bytes.begin(), attribute_bytes.end());
        // CRC32 selects candidates; compare all bytes to preserve distinct
        // materials, UVs, normals and shallow ghost geometry on collisions.
        auto& candidates = entries_[crc32(encoded)];
        for (const auto& mesh : candidates) {
            if (std::ranges::equal(bytes(mesh->vertices), vertex_bytes) && std::ranges::equal(bytes(mesh->attributes), attribute_bytes)) {
                return mesh;
            }
        }
        auto mesh = renderer_.world_mesh(std::move(vertices), std::move(attributes));
        candidates.push_back(mesh);
        return mesh;
    }
};
} // namespace toca::scene
