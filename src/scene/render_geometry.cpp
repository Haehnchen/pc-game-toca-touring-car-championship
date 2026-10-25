#include "scene/render_geometry.hpp"

#include "scene/model_textures.hpp"

#include <algorithm>
#include <cmath>

namespace toca::scene::detail {
std::shared_ptr<ModelTextures> scene_textures(gpu::Renderer& renderer, std::shared_ptr<ModelTextures> textures) {
    if (!textures) {
        textures = std::make_shared<ModelTextures>(renderer);
    }
    textures->require_renderer(renderer);
    return textures;
}

void validate(const CameraInputs& camera) {
    const auto& p = camera.projection;
    if (p.width == 0 || p.height == 0 || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.near_plane) || !std::isfinite(p.far_plane) || !std::isfinite(p.depth_scale)
        || !std::isfinite(p.depth_offset) || p.near_plane <= 0 || p.far_plane <= p.near_plane || p.x <= 0 || p.y <= 0
        || !std::ranges::all_of(camera.world_to_view, [](float value) {
               return std::isfinite(value);
           })) {
        throw std::runtime_error("Scene projection requires finite original camera parameters and a nonempty target");
    }
}

gpu::WorldProjection world_projection(const CameraInputs& camera) {
    const auto& p = camera.projection;
    return {camera.world_to_view, {p.x, p.y, p.near_plane, p.far_plane}, {p.depth_scale, p.depth_offset, float(p.width), float(p.height)}};
}

void WorldChunkBuilder::triangle(gpu::State state, const std::array<LitVertex, 3>& corners) {
    if (vertices.size() > UINT32_MAX / 2 - 3) {
        throw std::runtime_error("Resident geometry exceeds GPU triangle capacity");
    }
    const auto first = Uint32(vertices.size() * 2);
    for (const auto& vertex : corners) {
        if (!std::isfinite(vertex.u) || !std::isfinite(vertex.v) || !std::ranges::all_of(vertex.position, [](float value) {
                return std::isfinite(value);
            })) {
            throw std::runtime_error("Resident geometry requires finite source corners");
        }
        vertices.push_back({vertex.position, vertex.color, vertex.u, vertex.v});
    }
    if (!draws.empty() && draws.back().state == state) {
        draws.back().count += 6;
    } else {
        draws.push_back({state, first, 6});
    }
}
} // namespace toca::scene::detail
