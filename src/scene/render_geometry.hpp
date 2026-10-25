#pragma once

#include "render/gpu_renderer.hpp"
#include "scene/camera.hpp"
#include "scene/lighting.hpp"

namespace toca::scene {
class ModelTextures;

struct LitVertex {
    std::array<float, 3> position;
    std::uint32_t color = 0xffffffff;
    float u = 0, v = 0;
};

namespace detail {
void validate(const CameraInputs& camera);
gpu::WorldProjection world_projection(const CameraInputs& camera);
std::shared_ptr<ModelTextures> scene_textures(gpu::Renderer& renderer, std::shared_ptr<ModelTextures> textures);

struct WorldChunkBuilder {
    std::vector<gpu::WorldVertex> vertices;
    std::vector<gpu::Draw> draws;
    void triangle(gpu::State state, const std::array<LitVertex, 3>& corners);
};

struct WorldChunk {
    std::optional<Lighting> lighting;
    std::vector<gpu::Draw> draws;

    template <class Prepare> void append(gpu::Frame& frame, gpu::Renderer& renderer, const CameraInputs& camera, const Lighting& light, Prepare prepare) {
        if (!lighting || *lighting != light) {
            WorldChunkBuilder built;
            prepare(built);
            if (!built.vertices.empty()) {
                auto mesh = renderer.world_mesh(std::move(built.vertices));
                for (auto& draw : built.draws) {
                    draw.mesh = mesh;
                }
            }
            draws = std::move(built.draws);
            lighting = light;
        }
        const auto view = frame.world_view(world_projection(camera));
        for (auto draw : draws) {
            draw.world_view = view;
            frame.draws.push_back(std::move(draw));
        }
    }
};

} // namespace detail
} // namespace toca::scene
