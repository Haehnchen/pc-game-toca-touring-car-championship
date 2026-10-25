#pragma once

#include "render/world_geometry.hpp"
#include "scene/matrix.hpp"

namespace toca::gpu {
using SourceMatrix = std::array<float, 16>;
inline constexpr SourceMatrix source_identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// 48741e stores each WV coefficient as float, then composes/stores WVP.
// Its x87 PC=53 sums have a column-specific order; products of two float32
// inputs are exact in binary64. Keep the two matrix stores explicit.
inline SourceMatrix source_matrix_product(const SourceMatrix& local, const SourceMatrix& parent) {
    return scene::matrix::source_product(local, parent);
}

inline SourceMatrix source_clip_matrix(const WorldProjection& view, const SourceMatrix& object = source_identity) {
    SourceMatrix projection{};
    projection[0] = view.scale_near_far[0];
    projection[5] = view.scale_near_far[1];
    projection[10] = view.depth_size[0];
    projection[11] = 1;
    projection[14] = view.depth_size[1];
    const auto world_view = source_matrix_product(object, view.world_to_view);
    return source_matrix_product(world_view, projection);
}

struct PreparedModelInstance {
    ModelInstance instance;
    SourceMatrix clip_transform;
};

static_assert(sizeof(PreparedModelInstance) == 176 && offsetof(PreparedModelInstance, clip_transform) == 112);
} // namespace toca::gpu
