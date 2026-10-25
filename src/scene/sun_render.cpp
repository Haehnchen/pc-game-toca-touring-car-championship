#include "scene/sun_render.hpp"

#include "image/decode.hpp"

#include <stdexcept>

namespace toca::scene {
SunView::SunView(gpu::Renderer& renderer, const SunResources& resources) {
    if (resources.textures.size() != textures_.size()) {
        throw std::runtime_error("Sun lens view requires seven original image slots");
    }
    for (std::size_t index = 0; index < textures_.size(); ++index) {
        const auto& texture = resources.textures[index];
        const auto surface = decode_model_texture(texture);
        const auto expected = int(lens_dimensions[index]);
        if (surface->w != surface->h || (texture.encoding == ImageEncoding::bmp && surface->w != expected)
            || (texture.encoding == ImageEncoding::webp && (surface->w <= 0 || surface->w > 256))) {
            throw std::runtime_error("Lens texture dimensions exceed the original/Mega contract");
        }
        // Screen blending uses RGB black as neutral; never apply black-key
        // alpha or reinterpret a palette index as an opacity mask.
        textures_[index] = renderer.upload(surface.get());
        linear_[index] = texture.encoding == ImageEncoding::webp;
    }
}

void SunView::append(gpu::Frame& frame, const CameraInputs& camera, std::uint32_t track_id) const {
    const auto overlay_start = frame.draws.size();
    for (const auto& quad : lens_quads({camera.world_to_view, camera.projection}, track_id)) {
        const auto first = frame.vertices.size();
        frame.quad(
            {.texture = textures_[quad.texture].get(), .blend = true, .depth_write = false, .screen = true, .linear = linear_[quad.texture], .clamp = !linear_[quad.texture]},
            quad.x, quad.y, quad.width, quad.height, 0x50ffffff, 0);
        frame.vertices[first].u = frame.vertices[first + 3].u = quad.u0;
        frame.vertices[first + 1].u = frame.vertices[first + 2].u = quad.u1;
        frame.vertices[first].v = frame.vertices[first + 1].v = quad.v0;
        frame.vertices[first + 2].v = frame.vertices[first + 3].v = quad.v1;
    }
    // These screen-space sprites blend over the resolved world, including
    // when Mega requests multisampling for its scene geometry.
    frame.mark_overlay(overlay_start);
}
} // namespace toca::scene
