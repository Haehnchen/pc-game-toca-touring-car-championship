#include "scene/weather_render.hpp"

#include "image/decode.hpp"
#include "simulation/fixed.hpp"

#include <stdexcept>

namespace toca::scene {
using namespace simulation::fixed;

std::array<LitVertex, 4> weather_sprite_vertices(const WeatherSprite& sprite, const WeatherCamera& camera) {
    const std::array<std::array<std::int32_t, 2>, 4> offsets{
        {{sprite.minimum[1], sprite.minimum[0]}, {sprite.minimum[1], sprite.maximum[0]}, {sprite.maximum[1], sprite.minimum[0]}, {sprite.maximum[1], sprite.maximum[0]}}};
    constexpr std::array<std::array<float, 2>, 4> uv{{{0, 1}, {0, 0}, {1, 1}, {1, 0}}};
    const auto color = (std::uint32_t(sprite.alpha) << 24) | (std::uint32_t(sprite.brightness) * 0x10101);
    std::array<LitVertex, 4> result;
    for (std::size_t vertex = 0; vertex < result.size(); ++vertex) {
        result[vertex].color = color;
        result[vertex].u = uv[vertex][0];
        result[vertex].v = uv[vertex][1];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto offset = add(mul(camera.right[axis], offsets[vertex][0]), mul(camera.up[axis], offsets[vertex][1]));
            // 497180 stores each converted fixed offset and center as float
            // before adding them, rather than adding their integers first.
            result[vertex].position[axis] = float(offset) / one + float(sprite.center[axis]) / one;
        }
    }
    return result;
}

WeatherView::WeatherView(gpu::Renderer& renderer, const WeatherResources& resources) {
    for (std::size_t index = 0; index < textures_.size(); ++index) {
        const auto surface = decode_bmp(resources.textures[index]);
        textures_[index] = renderer.upload(surface.get(), true);
        palette_[index] = SDL_ISPIXELFORMAT_INDEXED(surface->format);
    }
}

void WeatherView::append_sprites(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const {
    if (weather.sprites.empty()) {
        return;
    }
    const auto projection = checked_world_projection(camera);
    const auto source_camera = weather_camera(camera.world_to_view);
    for (const auto& sprite : weather.sprites) {
        const auto texture = std::size_t(sprite.texture);
        if (texture >= textures_.size()) {
            throw std::runtime_error("Weather sprite requires its original texture index");
        }
        auto separation = sub(sprite.center, source_camera.position);
        separation[1] = 0;
        // 497180 rejects a large billboard within2 horizontal units. Source
        // snow/Cats/Dogs rectangles are smaller, but preserve the draw rule.
        if (dot(separation, separation) < 0x40000 && dot(sub(sprite.maximum, sprite.minimum), sub(sprite.maximum, sprite.minimum)) > 0xa0000) {
            continue;
        }
        const auto vertices = weather_sprite_vertices(sprite, source_camera);
        const gpu::State state{.texture = textures_[texture].get(), .depth = true, .blend = true, .key = true, .depth_write = false, .clamp = true, .palette = palette_[texture]};
        for (const auto& indices : {std::array{0u, 3u, 2u}, std::array{0u, 1u, 3u}}) {
            std::array<gpu::WorldVertex, 3> triangle;
            for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                const auto& vertex = vertices[indices[corner]];
                triangle[corner] = {vertex.position, vertex.color, vertex.u, vertex.v};
            }
            frame.world_triangle(state, triangle, projection);
        }
    }
}

void WeatherView::append_lines(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const {
    if (weather.lines.empty()) {
        return;
    }
    const auto projection = checked_world_projection(camera);
    for (const auto& line : weather.lines) {
        // 47cb80 retains RGB but clears alpha on the second world endpoint.
        std::array<gpu::WorldVertex, 2> vertices;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            vertices[0].position[axis] = float(line.from[axis]) / one;
            vertices[1].position[axis] = float(line.to[axis]) / one;
        }
        vertices[0].color = line.color;
        vertices[1].color = line.color & 0x00ffffff;
        frame.world_line({.depth = true, .blend = true, .lines = true}, vertices, projection);
    }
}

void WeatherView::append_lightning(gpu::Frame& frame, const CameraInputs& camera, const WeatherFrame& weather) const {
    if (weather.lightning_alpha != 0) {
        // 4632c0's sixteen equal white quads are visually one rectangle.
        frame.quad({.blend = true}, 0, 0, float(camera.projection.width), float(camera.projection.height), (std::uint32_t(weather.lightning_alpha) << 24) | 0xffffff);
    }
}
} // namespace toca::scene
