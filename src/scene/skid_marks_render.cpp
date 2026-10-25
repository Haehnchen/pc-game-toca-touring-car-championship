#include "scene/skid_marks_render.hpp"

#include <algorithm>

namespace toca::scene {
void append_skid_marks(gpu::Frame& frame, const CameraInputs& camera, const SkidMarkFrame& marks, std::span<const std::uint16_t> visible_sectors, std::uint8_t view_mask) {
    const auto projection = checked_world_projection(camera);
    // 441d10 uses LVERTEX diffuse colors, texture handle zero and48fae2(2).
    // Z-test/write remain enabled from486fbf/489069; no depth bias is added.
    const gpu::State state{.depth = true, .blend = true};
    for (const auto& mark : marks.quads) {
        if (mark.sector == 0xffff || !(mark.visibility & view_mask) || (!visible_sectors.empty() && std::ranges::find(visible_sectors, mark.sector) == visible_sectors.end())) {
            continue;
        }
        std::array<gpu::WorldVertex, 4> vertices;
        for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
            auto& vertex = vertices[corner];
            for (std::size_t axis = 0; axis < vertex.position.size(); ++axis) {
                vertex.position[axis] = float(mark.vertices[corner][axis]) / 65536.f;
            }
            vertex.color = mark.color;
        }
        for (const auto& triangle : {std::array{0u, 2u, 1u}, std::array{0u, 3u, 2u}}) {
            frame.world_triangle(state, {vertices[triangle[0]], vertices[triangle[1]], vertices[triangle[2]]}, projection);
        }
    }
}
} // namespace toca::scene
