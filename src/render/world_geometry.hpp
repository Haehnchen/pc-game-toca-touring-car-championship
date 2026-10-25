#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace toca::gpu {
// Three consecutive corners describe one source polygon. The vertex shader
// emits its original near-clipped fan (at most two triangles), preserving the
// outside endpoint's packed color rather than hardware color interpolation.
// Hardware-line streams instead retain two source endpoints per primitive.
struct WorldVertex {
    std::array<float, 3> position;
    std::uint32_t color = 0xffffffff;
    float u = 0, v = 0;
    std::array<float, 2> padding{};
};

static_assert(sizeof(WorldVertex) == 32);
static_assert(offsetof(WorldVertex, color) == 12 && offsetof(WorldVertex, u) == 16);

struct ModelAttribute {
    std::array<float, 3> normal;
    std::uint32_t flags = 0; // bit0: shallow ghost interior
    std::array<float, 3> ghost_position;
    std::uint32_t padding = 0;
};

struct ModelInstance {
    std::array<float, 16> object_to_world;
    std::array<float, 4> direction_diffuse;
    // Ambient RGB bytes; flags: sky=1, ghost=2, ghost wheel=4, name UV=8,
    // translation-first object positions=16 (separate finale CPU consumer).
    // Original untextured face RGB with opaque alpha=32.
    std::array<std::uint32_t, 4> ambient_flags;
    // Original name row's lower bound and height, preserving source division.
    std::array<float, 4> uv_remap{0, 1, 0, 0};
};

static_assert(sizeof(ModelAttribute) == 32 && offsetof(ModelAttribute, ghost_position) == 16);
static_assert(sizeof(ModelInstance) == 112 && offsetof(ModelInstance, ambient_flags) == 80 && offsetof(ModelInstance, uv_remap) == 96);

struct WorldProjection {
    std::array<float, 16> world_to_view;
    std::array<float, 4> scale_near_far;
    std::array<float, 4> depth_size;
    // Optional flips about camera width/height, followed by additive TL offsets.
    // Compose reflections after source culling, before overlays are appended.
    std::array<float, 4> reflection{};
    // Filled by the renderer: draw target dimensions/origin can differ from
    // the source camera's logical pixel dimensions after composition/resize.
    std::array<float, 4> viewport{};
    // Renderer-prepared48741e clip transform for resident world geometry.
    // Streamed source consumers retain their separate construction contract.
    std::array<float, 16> clip_transform{};
    // Renderer-only comparison context: TL viewport scale XY, profile flag Z.
    // Source matrices and the game projection remain independent of this tail.
    std::array<float, 4> raster{};
    bool operator==(const WorldProjection&) const = default;
};

static_assert(sizeof(WorldProjection) == 208);

struct WorldMesh {
    SDL_GPUDevice* device;
    SDL_GPUBuffer* buffer;
    // Retained immutable source corners also support deterministic inspection.
    // Camera changes never rewrite or upload this data.
    std::vector<WorldVertex> vertices;
    std::vector<ModelAttribute> attributes;

    ~WorldMesh() {
        SDL_ReleaseGPUBuffer(device, buffer);
    }

    WorldMesh(SDL_GPUDevice* owner, SDL_GPUBuffer* uploaded, std::vector<WorldVertex> source, std::vector<ModelAttribute> model_attributes = {})
        : device(owner), buffer(uploaded), vertices(std::move(source)), attributes(std::move(model_attributes)) {}

    WorldMesh(const WorldMesh&) = delete;
    WorldMesh& operator=(const WorldMesh&) = delete;
};
} // namespace toca::gpu
