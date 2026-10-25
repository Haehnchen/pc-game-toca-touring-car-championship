#pragma once
#include "formats/cm3d.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace toca {
struct ModelBatch {
    int texture;
    std::uint32_t color;
    std::vector<std::uint16_t> indices;
};

// Decoded geometry and hierarchy, independent of GPU objects.
struct ModelSceneData {
    Model model;
    std::vector<glm::mat4> transforms;
    std::vector<std::vector<ModelBatch>> batches;
    glm::vec3 center{};
    float radius = 1;
    std::size_t triangles = 0;

    explicit ModelSceneData(Model decoded) : model(std::move(decoded)) {
        for (const auto& mesh : model.meshes) {
            auto& groups = batches.emplace_back();
            for (const auto& face : mesh.faces) {
                const auto& c = face.color;
                const std::uint32_t color = face.texture >= 0 ? 0xffffffffu : (std::uint32_t(c[3]) << 24) | (std::uint32_t(c[0]) << 16) | (std::uint32_t(c[1]) << 8) | c[2];
                if (groups.empty() || groups.back().texture != face.texture || groups.back().color != color) {
                    groups.push_back({face.texture, color, {}});
                }
                auto& indices = groups.back().indices;
                indices.insert(indices.end(), face.indices.begin(), face.indices.end());
            }
        }
        glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(-minimum);
        for (const auto& node : model.nodes) {
            const auto local = glm::make_mat4(node.transform.data());
            const auto world = node.parent < 0 ? local : transforms.at(node.parent) * local;
            transforms.push_back(world);
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto& mesh = model.meshes.at(node.variants.front());
            triangles += mesh.faces.size();
            for (const auto& vertex : mesh.vertices) {
                const auto p = world * glm::vec4(glm::make_vec3(vertex.position.data()), 1);
                minimum = glm::min(minimum, glm::vec3(p));
                maximum = glm::max(maximum, glm::vec3(p));
            }
        }
        if (!triangles) {
            throw std::runtime_error("The first model variant has no triangles");
        }
        center = (minimum + maximum) * 0.5f;
        radius = glm::length(maximum - minimum) * 0.5f;
        if (!std::isfinite(radius) || radius <= 0) {
            throw std::runtime_error("Invalid model bounds");
        }
        std::cout << "CM3D: " << model.nodes.size() << " nodes, " << model.meshes.size() << " meshes, " << model.textures.size()
                  << " embedded textures; first variant: " << triangles << " triangles\n";
    }
};
} // namespace toca
