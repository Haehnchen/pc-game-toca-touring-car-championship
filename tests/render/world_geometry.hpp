#pragma once

#include "render/source_projection.hpp"
#include "render/world_geometry.hpp"
#include "scene/view.hpp"

namespace toca::test {
struct SourceProjectedVertex {
    std::array<float, 4> clip{};
    gpu::Vertex tl{};
    std::uint32_t flags = 0;
};

// Independent binary64 oracle for 47e730's x87 PC53 arithmetic and float stores.
inline std::int32_t source_q16_flag(double value) {
    const auto rounded = std::nearbyint(value * 65536.0);
    return !std::isfinite(rounded) || rounded < INT32_MIN || rounded > INT32_MAX ? INT32_MIN : std::int32_t(rounded);
}

inline SourceProjectedVertex source_projected_vertex(const scene::LitVertex& vertex, const gpu::SourceMatrix& matrix, const gpu::WorldProjection& view, bool sky = false) {
    SourceProjectedVertex result;
    std::array<double, 4> raw;
    for (std::size_t column = 0; column < 4; ++column) {
        const auto term = [&](std::size_t axis) {
            return double(matrix[axis * 4 + column]) * double(vertex.position[axis]);
        };
        double sum = column == 0 ? term(1) + term(0) : term(2) + term(1);
        sum += column == 0 ? term(2) : term(0);
        raw[column] = sum + double(matrix[12 + column]);
        result.clip[column] = float(raw[column]);
    }
    const auto x = source_q16_flag(raw[0]), y = source_q16_flag(raw[1]), w = source_q16_flag(raw[3]);
    const auto negate = [](std::int32_t value) {
        return std::bit_cast<std::int32_t>(0u - std::bit_cast<std::uint32_t>(value));
    };
    result.flags = (w <= source_q16_flag(view.scale_near_far[2]) ? 1u : 0u) | (w < x ? 4u : w < negate(x) ? 2u : 0u) | (w < y ? 16u : w < negate(y) ? 8u : 0u);
    result.tl = {0, 0, 0, 1, vertex.color, 0xff000000, vertex.u, vertex.v};
    if (!(result.flags & 1)) {
        const auto reciprocal = float(1.0 / double(result.clip[3]));
        const auto half_width = double(view.depth_size[2]) * .5, half_height = double(view.depth_size[3]) * .5;
        result.tl.x = float((double(result.clip[0]) * reciprocal) * half_width + half_width);
        result.tl.y = float(half_height - (double(result.clip[1]) * reciprocal) * half_height);
        result.tl.z = sky ? std::bit_cast<float>(0x3f7fff58u) : float(raw[2] * reciprocal);
        result.tl.rhw = reciprocal;
    }
    return result;
}

inline gpu::Vertex source_near_vertex(const SourceProjectedVertex& inside, const SourceProjectedVertex& outside, const gpu::WorldProjection& view, bool anchor_inside) {
    const auto near = view.scale_near_far[2];
    const auto denominator = float(double(inside.clip[3]) - outside.clip[3]);
    const auto& anchor = anchor_inside ? inside : outside;
    const auto distance = float(double(anchor.clip[3]) - near);
    std::array<double, 2> cross;
    for (std::size_t axis = 0; axis < cross.size(); ++axis) {
        const auto slope = (double(inside.clip[axis]) - outside.clip[axis]) / denominator;
        const auto displacement = slope * distance;
        cross[axis] = double(anchor.clip[axis]) - displacement;
    }
    const auto numerator = anchor_inside ? double(distance) : double(inside.clip[3]) - near;
    const auto fraction = float(numerator / denominator);
    const auto reciprocal = float(1.0 / double(near));
    const auto half_width = double(view.depth_size[2]) * .5, half_height = double(view.depth_size[3]) * .5;
    return {float((cross[0] * reciprocal) * half_width + half_width), float(-(cross[1] * reciprocal) * half_height + half_height), 0, reciprocal, outside.tl.diffuse, 0xff000000,
        float((double(outside.tl.u) - inside.tl.u) * fraction + inside.tl.u), float((double(outside.tl.v) - inside.tl.v) * fraction + inside.tl.v)};
}

struct SourceTriangleIndices {
    std::array<Uint16, 6> values{};
    std::size_t count = 0;
};

// 487ee9 dispatches by stored W after counting unrounded-dot Q16 near flags.
// Keep its actual triangle topology; a generic polygon fan changes interpolation.
template <class Intersection>
inline SourceTriangleIndices source_near_indices(const std::array<SourceProjectedVertex, 3>& vertices, std::array<Uint16, 3> indices, float near, Intersection cross) {
    if ((vertices[0].flags & vertices[1].flags & vertices[2].flags & 0x1e) != 0) {
        return {};
    }
    const auto a = indices[0], b = indices[1], c = indices[2];
    const auto count = (vertices[0].flags & 1) + (vertices[1].flags & 1) + (vertices[2].flags & 1);
    if (count == 0) {
        return {{a, b, c}, 3};
    }
    if (count == 2) {
        if (vertices[0].clip[3] <= near) {
            if (vertices[1].clip[3] <= near) {
                if (vertices[2].clip[3] <= near) {
                    return {};
                }
                const auto x = cross(2u, 0u, true), y = cross(2u, 1u, true);
                return {{x, y, c}, 3};
            }
            const auto x = cross(1u, 0u, true), y = cross(1u, 2u, true);
            return {{x, b, y}, 3};
        }
        const auto x = cross(0u, 1u, true), y = cross(0u, 2u, true);
        return {{a, x, y}, 3};
    }
    if (count == 1) {
        if (vertices[0].clip[3] > near) {
            if (vertices[1].clip[3] > near) {
                if (vertices[2].clip[3] > near) {
                    return {};
                }
                const auto x = cross(0u, 2u, false), y = cross(1u, 2u, false);
                return {{a, b, x, y, x, b}, 6};
            }
            const auto x = cross(0u, 1u, false), y = cross(2u, 1u, false);
            return {{a, x, c, x, y, c}, 6};
        }
        const auto x = cross(1u, 0u, false), y = cross(2u, 0u, false);
        return {{x, b, c, x, c, y}, 6};
    }
    return {};
}

inline void materialize_source_triangle(
    gpu::Frame& result, const gpu::Draw& draw, const std::array<scene::LitVertex, 3>& triangle, const gpu::WorldProjection& view, const gpu::SourceMatrix& transform, bool sky) {
    std::array<SourceProjectedVertex, 3> input;
    std::array<gpu::Vertex, 5> output;
    for (std::size_t corner = 0; corner < input.size(); ++corner) {
        input[corner] = source_projected_vertex(triangle[corner], transform, view, sky);
        output[corner] = input[corner].tl;
    }
    if (!sky && std::ranges::all_of(input, [&](const auto& vertex) {
            return vertex.clip[3] > view.scale_near_far[3];
        })) {
        return;
    }
    std::size_t count = 3;
    const auto indices = source_near_indices(input, {0, 1, 2}, view.scale_near_far[2], [&](unsigned inside, unsigned outside, bool anchor_inside) {
        output[count] = source_near_vertex(input[inside], input[outside], view, anchor_inside);
        return Uint16(count++);
    });
    if (!indices.count) {
        return;
    }
    float area = 0;
    for (std::size_t first = 0; first < indices.count; first += 3) {
        const auto& a = output[indices.values[first]];
        const auto& b = output[indices.values[first + 1]];
        const auto& c = output[indices.values[first + 2]];
        area += (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }
    if (area <= 0) {
        return;
    }
    const auto offset = result.batch_vertex_offset(count, indices.count);
    const auto base = Uint16(result.vertices.size() - std::size_t(offset));
    result.draws.push_back({draw.state, Uint32(result.indices.size()), Uint32(indices.count), offset});
    result.vertices.insert(result.vertices.end(), output.begin(), output.begin() + count);
    for (std::size_t index = 0; index < indices.count; ++index) {
        result.indices.push_back(Uint16(base + indices.values[index]));
    }
}

// Retain the original hardware-line consumer independently of the shader.
// Equality at near survives; clipping interpolates/truncates packed channels.
inline void materialize_line(gpu::Frame& result, const gpu::Draw& draw, std::array<gpu::WorldVertex, 2> line, const gpu::WorldProjection& view) {
    const auto& matrix = view.world_to_view;
    const auto& scale = view.scale_near_far;
    const auto& depth = view.depth_size;
    for (auto& vertex : line) {
        const auto position = vertex.position;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = matrix[axis] * position[0] + matrix[4 + axis] * position[1] + matrix[8 + axis] * position[2] + matrix[12 + axis];
        }
    }
    const auto near = scale[2];
    if (line[0].position[2] < near && line[1].position[2] < near) {
        return;
    }
    if ((line[0].position[2] < near) != (line[1].position[2] < near)) {
        const auto outside = line[0].position[2] < near ? 0u : 1u, inside = 1u - outside;
        const auto fraction = (near - line[outside].position[2]) / (line[inside].position[2] - line[outside].position[2]);
        for (std::size_t axis = 0; axis < 2; ++axis) {
            line[outside].position[axis] += (line[inside].position[axis] - line[outside].position[axis]) * fraction;
        }
        line[outside].position[2] = near;
        line[outside].u += (line[inside].u - line[outside].u) * fraction;
        line[outside].v += (line[inside].v - line[outside].v) * fraction;
        std::uint32_t color = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const auto a = float((line[outside].color >> shift) & 255), b = float((line[inside].color >> shift) & 255);
            color |= std::uint32_t(std::clamp(a + (b - a) * fraction, 0.f, 255.f)) << shift;
        }
        line[outside].color = color;
    }
    const auto offset = result.batch_vertex_offset(2, 2);
    const auto base = Uint16(result.vertices.size() - std::size_t(offset));
    gpu::Draw expanded{draw.state, Uint32(result.indices.size()), 2, offset};
    expanded.viewport = draw.viewport;
    expanded.overlay = draw.overlay;
    result.draws.push_back(expanded);
    result.indices.insert(result.indices.end(), {base, Uint16(base + 1)});
    for (const auto& vertex : line) {
        const auto reciprocal = 1.f / vertex.position[2];
        auto x = (1 + vertex.position[0] * scale[0] * reciprocal) * depth[2] * .5f;
        auto y = (1 - vertex.position[1] * scale[1] * reciprocal) * depth[3] * .5f;
        if (view.reflection[0] != 0) {
            x = depth[2] - x;
        }
        if (view.reflection[1] != 0) {
            y = depth[3] - y;
        }
        x += view.reflection[2];
        y += view.reflection[3];
        const auto z = vertex.position[2] == near ? 0.f : depth[0] + depth[1] * reciprocal;
        result.vertices.push_back({x, y, z, reciprocal, vertex.color, 0xff000000, vertex.u, vertex.v});
    }
}

// Independent presentation oracle: keep ordinary TL buffers intact and expand
// resident/dynamic geometry through the original CPU clipping/projection consumers.
inline gpu::Frame materialize_world(const gpu::Frame& source) {
    auto result = source;
    result.draws.clear();
    result.world_views.clear();
    result.model_instances.clear();
    result.dynamic_world_vertices.clear();
    result.dynamic_world_lines.clear();
    for (const auto& draw : source.draws) {
        if (!draw.mesh && !draw.dynamic_world) {
            result.draws.push_back(draw);
            continue;
        }
        if (draw.dynamic_world && (draw.mesh || draw.model_instance != UINT32_MAX || draw.instance_count != 1)) {
            throw std::runtime_error("Invalid dynamic geometry oracle draw");
        }
        if (draw.dynamic_world && draw.state.lines) {
            if (draw.world_view >= source.world_views.size() || draw.first % 2 || draw.count % 2 || draw.vertex_offset != 0
                || Uint64(draw.first) + draw.count > source.dynamic_world_lines.size()) {
                throw std::runtime_error("Invalid dynamic line oracle draw");
            }
            for (std::size_t first = draw.first; first < std::size_t(draw.first) + draw.count; first += 2) {
                materialize_line(result, draw, {source.dynamic_world_lines[first], source.dynamic_world_lines[first + 1]}, source.world_views[draw.world_view]);
            }
            continue;
        }
        const auto& corners = draw.dynamic_world ? source.dynamic_world_vertices : draw.mesh->vertices;
        if (draw.world_view >= source.world_views.size() || draw.first % 6 || draw.count % 6 || draw.vertex_offset != 0 || (Uint64(draw.first) + draw.count) / 2 > corners.size()) {
            throw std::runtime_error("Invalid world triangle oracle draw");
        }
        const auto& view = source.world_views[draw.world_view];
        const auto& scale = view.scale_near_far;
        const auto& depth = view.depth_size;
        const scene::CameraInputs camera{view.world_to_view, {scale[0], scale[1], scale[2], scale[3], depth[0], depth[1], Uint32(depth[2]), Uint32(depth[3])}};
        const bool model = draw.model_instance != UINT32_MAX;
        if (model
            && (draw.mesh->attributes.size() != draw.mesh->vertices.size() || !draw.instance_count
                || Uint64(draw.model_instance) + draw.instance_count > source.model_instances.size())) {
            throw std::runtime_error("Invalid model triangle oracle draw");
        }
        for (Uint32 instance_index = 0; instance_index < (model ? draw.instance_count : 1); ++instance_index) {
            const auto* instance = model ? &source.model_instances[draw.model_instance + instance_index] : nullptr;
            const bool source_resident = !draw.dynamic_world && (!instance || !(instance->ambient_flags[3] & 16));
            const auto source_transform = gpu::source_clip_matrix(view, instance ? instance->object_to_world : gpu::source_identity);
            auto projection_camera = camera;
            const bool sky = instance && (instance->ambient_flags[3] & 1);
            if (sky) {
                projection_camera.projection.far_plane = std::numeric_limits<float>::max();
            }
            for (std::size_t index = draw.first / 2; index < (std::size_t(draw.first) + draw.count) / 2; index += 3) {
                std::array<scene::LitVertex, 3> triangle;
                for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                    const auto& vertex = corners[index + corner];
                    triangle[corner] = {vertex.position, vertex.color, vertex.u, vertex.v};
                    if (instance) {
                        const auto& attribute = draw.mesh->attributes[index + corner];
                        const auto& matrix = instance->object_to_world;
                        const auto flags = instance->ambient_flags[3];
                        const bool ghost = flags & 2, hidden = ghost && (attribute.flags & 1);
                        std::array<float, 3> normal{};
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            for (std::size_t component = 0; component < 3; ++component) {
                                normal[axis] += attribute.normal[component] * matrix[component * 4 + axis];
                            }
                        }
                        const scene::Lighting lighting{{instance->direction_diffuse[0], instance->direction_diffuse[1], instance->direction_diffuse[2]},
                            instance->direction_diffuse[3], {Uint8(instance->ambient_flags[0]), Uint8(instance->ambient_flags[1]), Uint8(instance->ambient_flags[2])}};
                        const std::array<Uint8, 4> material{
                            Uint8(vertex.color >> 16), Uint8(vertex.color >> 8), Uint8(vertex.color), (flags & 32) ? Uint8(255) : Uint8(vertex.color >> 24)};
                        triangle[corner].color = sky                      ? 0xffffffffu
                                                 : ghost && !(flags & 32) ? scene::ghost_vertex_color(attribute.normal, matrix, lighting)
                                                 : source_resident        ? scene::model_vertex_color(attribute.normal, matrix, material, lighting)
                                                                          : scene::model_vertex_color(normal, material, lighting);
                        if (ghost && (hidden || (flags & 4))) {
                            triangle[corner].color = (triangle[corner].color & 0xffffffu) | 0x60000000u;
                        }
                        const auto& position = hidden ? attribute.ghost_position : vertex.position;
                        if (source_resident) {
                            triangle[corner].position = position;
                        } else {
                            for (std::size_t axis = 0; axis < 3; ++axis) {
                                if (flags & 16) {
                                    triangle[corner].position[axis] = matrix[12 + axis];
                                    for (std::size_t component = 0; component < 3; ++component) {
                                        triangle[corner].position[axis] += position[component] * matrix[component * 4 + axis];
                                    }
                                } else {
                                    triangle[corner].position[axis] =
                                        matrix[axis] * position[0] + matrix[4 + axis] * position[1] + matrix[8 + axis] * position[2] + matrix[12 + axis];
                                }
                            }
                        }
                        // The shallow ghost replacement restores source UVs after
                        // personal-name remapping in the original CPU consumer.
                        if ((flags & 8) && !hidden) {
                            triangle[corner].v = (vertex.v - instance->uv_remap[0]) / instance->uv_remap[1];
                        }
                    }
                }
                const auto first_vertex = result.vertices.size(), first_draw = result.draws.size();
                if (source_resident) {
                    materialize_source_triangle(result, draw, triangle, view, source_transform, sky);
                } else {
                    scene::append_projected_triangle(result, draw.state, triangle, projection_camera);
                }
                for (std::size_t vertex = first_vertex; vertex < result.vertices.size(); ++vertex) {
                    if (sky) {
                        result.vertices[vertex].z = std::bit_cast<float>(0x3f7fff58u);
                    }
                    if (view.reflection[0] != 0) {
                        result.vertices[vertex].x = depth[2] - result.vertices[vertex].x;
                    }
                    if (view.reflection[1] != 0) {
                        result.vertices[vertex].y = depth[3] - result.vertices[vertex].y;
                    }
                    result.vertices[vertex].x += view.reflection[2];
                    result.vertices[vertex].y += view.reflection[3];
                }
                for (std::size_t expanded = first_draw; expanded < result.draws.size(); ++expanded) {
                    result.draws[expanded].viewport = draw.viewport;
                    result.draws[expanded].overlay = draw.overlay;
                }
            }
        }
    }
    return result;
}
} // namespace toca::test
