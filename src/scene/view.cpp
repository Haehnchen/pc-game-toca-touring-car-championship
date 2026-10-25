#include "scene/view.hpp"

#include "core/contracts.hpp"
#include "frontend/resources.hpp"
#include "image/decode.hpp"
#include "scene/car_light_glow.hpp"
#include "scene/catalog.hpp"
#include "scene/engine_effects_render.hpp"
#include "scene/grid_lines.hpp"
#include "scene/matrix.hpp"
#include "scene/model_geometry.hpp"
#include "scene/model_textures.hpp"
#include "scene/render_geometry.hpp"
#include "scene/skid_marks_render.hpp"
#include "scene/sun_render.hpp"
#include "scene/surface_effects_render.hpp"
#include "scene/terrain_lighting_cache.hpp"
#include "scene/visibility.hpp"
#include "scene/weather_render.hpp"
#include "simulation/damage_parts.hpp"
#include "simulation/fixed.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace toca::scene {
namespace {
using detail::scene_textures;
using detail::validate;
using detail::world_projection;
using detail::WorldChunk;
using detail::WorldChunkBuilder;

std::shared_ptr<ModelMeshes> scene_meshes(gpu::Renderer& renderer, std::shared_ptr<ModelMeshes> meshes) {
    if (!meshes) {
        meshes = std::make_shared<ModelMeshes>(renderer);
    }
    meshes->require_renderer(renderer);
    return meshes;
}

LitVertex transformed(const LitVertex& vertex, const std::array<float, 16>& matrix) {
    auto result = vertex;
    for (std::size_t row = 0; row < 3; ++row) {
        result.position[row] = matrix[row] * vertex.position[0] + matrix[4 + row] * vertex.position[1] + matrix[8 + row] * vertex.position[2] + matrix[12 + row];
        if (!std::isfinite(result.position[row])) {
            throw std::runtime_error("Scene transformed position is not finite");
        }
    }
    if (!std::isfinite(vertex.u) || !std::isfinite(vertex.v)) {
        throw std::runtime_error("Scene texture coordinates are not finite");
    }
    return result;
}

LitVertex intersection(const LitVertex& first, const LitVertex& second, float near) {
    const auto fraction = (near - first.position[2]) / (second.position[2] - first.position[2]);
    auto result = first.position[2] <= near ? first : second;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        result.position[axis] = first.position[axis] + (second.position[axis] - first.position[axis]) * fraction;
    }
    result.position[2] = near;
    result.u = first.u + (second.u - first.u) * fraction;
    result.v = first.v + (second.v - first.v) * fraction;
    // 00487ee9 copies the outside endpoint's packed color while interpolating UV.
    return result;
}

gpu::Vertex projected(const LitVertex& vertex, const Projection& p) {
    const auto reciprocal = 1.f / vertex.position[2];
    return {(1 + vertex.position[0] * p.x * reciprocal) * float(p.width) * .5f, (1 - vertex.position[1] * p.y * reciprocal) * float(p.height) * .5f,
        vertex.position[2] == p.near_plane ? 0.f : p.depth_scale + p.depth_offset * reciprocal, reciprocal, vertex.color, 0xff000000, vertex.u, vertex.v};
}

std::uint32_t append_projected_triangle_unchecked(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const CameraInputs& camera);
std::uint32_t append_view_triangle_unchecked(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const CameraInputs& camera);

void append_world_triangle(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const gpu::WorldProjection& projection) {
    std::array<gpu::WorldVertex, 3> corners;
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        const auto& vertex = vertices[corner];
        corners[corner] = {vertex.position, vertex.color, vertex.u, vertex.v};
    }
    frame.world_triangle(state, corners, projection);
}

void append_car_shadow(gpu::Frame& frame, SDL_GPUTexture* texture, const CarShadowFootprint& shadow, const gpu::WorldProjection& projection) {
    // 498430's prelit world vertices. Use the949d3!=0 capability branch:
    // both depth test and writes disabled, no alternate4000-Q16 Y lift.
    constexpr std::array<std::array<float, 2>, 4> core_uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    std::array<LitVertex, 15> vertices;
    const auto vertex = [&](const auto& point, std::uint32_t color, float u, float v) {
        std::array<float, 3> position;
        std::ranges::transform(point, position.begin(), [](auto component) {
            return float(component) / 65536.f;
        });
        return LitVertex{position, color, u, v};
    };
    for (std::size_t index = 0; index < 4; ++index) {
        vertices[index] = vertex(shadow.points[index], 0xc8000000u, core_uv[index][0], core_uv[index][1]);
    }
    const auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
        // 486fbf sets TEXTUREADDRESS=CLAMP; shadow edge UVs include 0/1.
        append_world_triangle(frame, {.texture = texture, .blend = true, .linear = true, .clamp = true}, {vertices[a], vertices[b], vertices[c]}, projection);
    };
    if (shadow.type == CarShadowType::rectangle) {
        // Type0 is a four-vertex triangle strip in record order1,2,0,3.
        triangle(1, 2, 0);
        triangle(0, 2, 3);
        return;
    }
    constexpr std::array<std::size_t, 6> one_side{0, 1, 4, 5, 6, 3};
    constexpr std::array<std::size_t, 10> two_sides{0, 1, 4, 5, 6, 7, 8, 9, 10, 3};
    if (shadow.type != CarShadowType::one_side && shadow.type != CarShadowType::two_sides) {
        throw std::runtime_error("Car shadow requires original draw type0..2");
    }
    const auto perimeter = shadow.type == CarShadowType::one_side ? std::span<const std::size_t>(one_side) : std::span<const std::size_t>(two_sides);
    const auto center = 4 + perimeter.size();
    for (std::size_t index = 0; index < perimeter.size(); ++index) {
        vertices[4 + index] = vertex(shadow.points[perimeter[index]], 0xaa000000u, 0, index % 2 == 0 ? .7f : .3f);
    }
    const auto midpoint = simulation::fixed::add(shadow.points[0], simulation::fixed::scale(simulation::fixed::sub(shadow.points[5], shadow.points[0]), 0x8000));
    vertices[center] = vertex(midpoint, 0xaa000000u, .5f, .5f);
    // 98130's index arrays: the unchanged core plus a closed extension fan.
    triangle(0, 1, 3);
    triangle(1, 2, 3);
    for (std::size_t index = 5; index < center; ++index) {
        triangle(index, center, index - 1);
    }
    triangle(4, center, center - 1);
}
} // namespace

namespace {
std::uint32_t append_projected_triangle_unchecked(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const CameraInputs& camera) {
    std::array<LitVertex, 3> input;
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = transformed(vertices[index], camera.world_to_view);
    }
    return append_view_triangle_unchecked(frame, state, input, camera);
}

std::uint32_t append_view_triangle_unchecked(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& input, const CameraInputs& camera) {
    std::array<LitVertex, 4> clipped;
    std::size_t count = 0;
    auto previous = input.back();
    for (const auto& current : input) {
        const bool inside = current.position[2] > camera.projection.near_plane;
        if ((previous.position[2] > camera.projection.near_plane) != inside) {
            clipped[count++] = intersection(previous, current, camera.projection.near_plane);
        }
        if (inside) {
            clipped[count++] = current;
        }
        previous = current;
    }
    if (count < 3) {
        return 0;
    }
    const auto& p = camera.projection;
    for (unsigned plane = 0; plane < 5; ++plane) {
        const bool outside = std::ranges::all_of(std::span(clipped).first(count), [&](const auto& vertex) {
            const auto x = vertex.position[0] * p.x, y = vertex.position[1] * p.y, z = vertex.position[2];
            switch (plane) {
                case 0:
                    return x < -z;
                case 1:
                    return x > z;
                case 2:
                    return y < -z;
                case 3:
                    return y > z;
                default:
                    return z > p.far_plane;
            }
        });
        if (outside) {
            return 0;
        }
    }
    std::array<gpu::Vertex, 4> output;
    for (std::size_t index = 0; index < count; ++index) {
        output[index] = projected(clipped[index], p);
    }
    float area = 0;
    for (std::size_t index = 1; index + 1 < count; ++index) {
        area += (output[index].x - output[0].x) * (output[index + 1].y - output[0].y) - (output[index].y - output[0].y) * (output[index + 1].x - output[0].x);
    }
    // 0048f5f9 sets D3DCULL_CCW. Positive TL area is clockwise with Y down.
    if (area <= 0) {
        return 0;
    }
    const auto offset = frame.batch_vertex_offset(count, (count - 2) * 3);
    const auto base = Uint16(frame.vertices.size() - std::size_t(offset));
    frame.draws.push_back({state, Uint32(frame.indices.size()), Uint32((count - 2) * 3), offset});
    for (std::size_t index = 0; index < count; ++index) {
        frame.vertices.push_back(output[index]);
    }
    for (std::size_t index = 1; index + 1 < count; ++index) {
        frame.indices.insert(frame.indices.end(), {base, Uint16(base + index), Uint16(base + index + 1)});
    }
    return std::uint32_t(count - 2);
}
} // namespace

std::uint32_t append_projected_triangle(gpu::Frame& frame, gpu::State state, const std::array<LitVertex, 3>& vertices, const CameraInputs& camera) {
    validate(camera);
    return append_projected_triangle_unchecked(frame, state, vertices, camera);
}

gpu::WorldProjection checked_world_projection(const CameraInputs& camera) {
    validate(camera);
    return world_projection(camera);
}

void append_contact_sparks(gpu::Frame& frame, std::span<const ContactSparkLine> lines, const CameraInputs& camera) {
    const auto projection = checked_world_projection(camera);
    for (const auto& line : lines) {
        const auto vertex = [&](const auto& point, std::uint32_t color) {
            gpu::WorldVertex result{{}, color};
            std::ranges::transform(point, result.position.begin(), [](auto value) {
                return float(value) / 65536.f;
            });
            return result;
        };
        // 446f20 queues the extended endpoint first;47cb80 makes the source
        // origin transparent and retains RGB. The hardware draws one-pixel lines.
        frame.world_line({.depth = true, .blend = true, .lines = true}, {vertex(line.to, line.color), vertex(line.from, line.color & 0x00ffffff)}, projection);
    }
}

namespace {
using matrix::identity;
using matrix::Matrix;
using matrix::multiply;

} // namespace

struct SceneView::Impl {
    const SceneResources& resources;
    gpu::Renderer& renderer;
    std::shared_ptr<ModelTextures> model_textures;
    std::shared_ptr<ModelMeshes> model_meshes;
    TerrainView terrain;
    std::vector<QueuedWorldLine> grid_lines;
    gpu::Texture car_shadow_texture;
    std::array<gpu::Texture, 2> car_glow_textures;
    std::unique_ptr<WeatherView> weather;
    std::unique_ptr<SurfaceEffectsView> surface_effects;
    std::unique_ptr<EngineEffectsView> engine_effects;
    std::unique_ptr<SunView> sun;
    std::optional<Cockpit> cockpit;
    Visibility spatial;
    // Reuse presentation copy capacities without retaining its root migrations
    // or detail bytes: every frame starts from the simulation's spatial state.
    std::optional<Visibility> presentation_visibility;

    struct ModelPass {
        Model model;
        frontend::CarLiveryMaterials livery;
        std::vector<ModelTextures::Texture> textures;
        std::vector<bool> mipmapped, palette;
        std::vector<std::optional<std::array<float, 2>>> name_rows;
        std::vector<CarLampLensTriangle> lamp_lenses;
        std::array<std::vector<ModelTextures::Texture>, 2> lamp_emission_textures;
        std::vector<std::uint32_t> roots, all_nodes;
        std::vector<std::vector<std::uint32_t>> root_nodes;
        std::vector<bool> ghost_wheels;
        bool original_materials = false;
        std::vector<ModelGeometry> geometry;

        explicit ModelPass(ModelTextures& uploads, ModelMeshes& meshes, Model source, std::span<const std::uint8_t> palette = {}, std::optional<std::uint32_t> car_id = {},
            bool emission = false, bool ghost_material = false)
            : model(std::move(source)) {
            geometry.reserve(model.meshes.size());
            for (const auto& mesh : model.meshes) {
                geometry.push_back(prepare_model_geometry(mesh, meshes));
            }
            const bool mega = car_id && std::ranges::any_of(model.meshes, [](const auto& mesh) {
                return !mesh.ghost_positions.empty();
            });
            original_materials = !mega && std::ranges::all_of(model.textures, [](const auto& texture) {
                return texture.encoding == ImageEncoding::bmp;
            });
            if (car_id) {
                livery = frontend::car_livery_materials(model, *car_id);
                if (emission) {
                    lamp_lenses = mega_car_lamp_lenses(model, *car_id);
                }
            }
            name_rows.resize(model.textures.size());
            // 49bf7d binds the personal name on side nodes15/16 and rear21.
            // Mega artwork fits those same rows to its authored glyph band;
            // derive that interval from the model instead of editing its UVs.
            if (car_id && (*car_id < 8 || *car_id == 10)) {
                for (const auto& node : model.nodes) {
                    const auto id = node.id_flags & 0xffffffu;
                    if (id != 15 && id != 16 && id != 21) {
                        continue;
                    }
                    for (const auto variant : node.variants) {
                        if (variant == 0xffff) {
                            continue;
                        }
                        const auto& mesh = model.meshes.at(variant);
                        for (const auto& face : mesh.faces) {
                            if (face.texture < 0) {
                                continue;
                            }
                            const auto& slot_name = model.textures.at(std::size_t(face.texture)).name;
                            if (slot_name != "ListA" && slot_name != "ListB") {
                                continue;
                            }
                            auto& row = name_rows.at(std::size_t(face.texture));
                            for (const auto index : face.indices) {
                                const auto v = mesh.vertices.at(index).v;
                                if (!row) {
                                    row = std::array{v, v};
                                } else {
                                    (*row)[0] = std::min((*row)[0], v);
                                    (*row)[1] = std::max((*row)[1], v);
                                }
                            }
                        }
                    }
                }
                // 49ae57 copies a fixed16-pixel row of the original128 atlas.
                // Retain source inset/subpixel coordinates rather than fitting
                // its geometry to the replacement bitmap's outer edges.
                const auto row = float(*car_id == 10 ? 2 : *car_id) / 8.f;
                for (std::size_t slot = 0; slot < name_rows.size(); ++slot) {
                    if (name_rows[slot] && model.textures[slot].encoding == ImageEncoding::bmp) {
                        name_rows[slot] = std::array{row, row + .125f};
                    }
                }
            }
            std::array<std::vector<bool>, 2> emitting;
            if (!lamp_lenses.empty()) {
                for (std::size_t side = 0; side < emitting.size(); ++side) {
                    emitting[side].resize(model.textures.size());
                    lamp_emission_textures[side].resize(model.textures.size());
                }
            }
            for (const auto& lens : lamp_lenses) {
                for (bool second : {false, true}) {
                    const auto slot = livery.texture_slot(lens.node_id_flags, lens.texture, second);
                    if (slot >= 0) {
                        emitting[lens.front][std::size_t(slot)] = true;
                    }
                }
            }
            for (std::size_t index = 0; index < model.textures.size(); ++index) {
                const auto& texture = model.textures[index];
                const auto requested_emission = lamp_lenses.empty() ? std::array<bool, 2>{} : std::array<bool, 2>{emitting[0][index], emitting[1][index]};
                const auto uploaded = uploads.get_model(texture, palette, requested_emission, ghost_material);
                textures.push_back(uploaded.texture);
                mipmapped.push_back(uploaded.mip_levels > 1);
                this->palette.push_back(uploaded.palette);
                for (std::size_t front = 0; front < emitting.size(); ++front) {
                    if (requested_emission[front]) {
                        lamp_emission_textures[front][index] = uploaded.emission[front];
                    }
                }
            }
            roots.reserve(model.nodes.size());
            all_nodes.reserve(model.nodes.size());
            root_nodes.resize(model.nodes.size());
            ghost_wheels.reserve(model.nodes.size());
            for (std::size_t index = 0; index < model.nodes.size(); ++index) {
                const auto parent = model.nodes[index].parent;
                if (parent < -1 || (parent >= 0 && std::size_t(parent) >= index)) {
                    throw std::runtime_error("Scene model parent must precede child");
                }
                const auto root = parent < 0 ? std::uint32_t(index) : roots[std::size_t(parent)];
                roots.push_back(root);
                const auto id = model.nodes[index].id_flags & 0xffffffu;
                ghost_wheels.push_back(mega && ((id >= 1 && id <= 4) || (parent >= 0 && ghost_wheels[std::size_t(parent)])));
            }
            // 48d367 draws each node, then its children, then its sibling.
            // Allocation order can put wheels before a body's children; with
            // ghost blending and Z writes that changes the visible overlap.
            std::vector<std::vector<std::uint32_t>> children(model.nodes.size());
            std::vector<std::uint32_t> pending;
            for (std::size_t index = 0; index < model.nodes.size(); ++index) {
                const auto parent = model.nodes[index].parent;
                if (parent >= 0) {
                    children[std::size_t(parent)].push_back(std::uint32_t(index));
                }
            }
            for (std::size_t index = model.nodes.size(); index != 0; --index) {
                if (model.nodes[index - 1].parent < 0) {
                    pending.push_back(std::uint32_t(index - 1));
                }
            }
            while (!pending.empty()) {
                const auto index = pending.back();
                pending.pop_back();
                all_nodes.push_back(index);
                root_nodes[roots[index]].push_back(index);
                for (auto child = children[index].rbegin(); child != children[index].rend(); ++child) {
                    pending.push_back(*child);
                }
            }
        }
    } car, horizon, trees, fence, objects, cones, posts, signs, flags, moving_horizon, rotorcraft;

    std::unique_ptr<ModelPass> detached_car;
    std::array<std::unique_ptr<ModelPass>, 3> ghost_car, local_ghost_car;
    const frontend::CarModels* local_car_source = nullptr;
    std::array<std::array<std::unique_ptr<ModelPass>, 3>, 11> rival_models;
    std::array<std::uint32_t, 16> rival_car_ids{};
    std::uint32_t local_car_id = 0;
    FleetScene cars;
    ModelTextures::Texture player_name, ghost_name;
    std::array<ModelTextures::Texture, 16> rival_names;

    void prepare_ghost_models(std::array<std::unique_ptr<ModelPass>, 3>& passes, const frontend::CarModels& source) {
        for (std::size_t lod = 0; lod < passes.size(); ++lod) {
            passes[lod] = std::make_unique<ModelPass>(*model_textures, *model_meshes,
                prepare_car_model(source.models[lod], source.car_id, resources.setup.selection.detail, std::uint32_t(lod)), std::span<const std::uint8_t>{}, source.car_id, false,
                true);
        }
    }

    static std::uint32_t ghost_actor(const CarPose& ghost) {
        const auto actor = ghost.actor_id == UINT32_MAX ? 1u : ghost.actor_id;
        if (actor == 0 || actor >= 16) {
            throw std::runtime_error("Ghost scene role requires an independent source actor1..15");
        }
        return actor;
    }

    ModelTextures::Texture upload_name(gpu::Renderer& renderer, SDL_Surface* bitmap) const {
        model_textures->require_renderer(renderer);
        if (!bitmap) {
            return {};
        }
        const int scale = resources.setup.selection.detail == graphics_mega_detail ? 2 : 1;
        if (bitmap->w != 128 * scale || bitmap->h != 16 * scale || bitmap->format != SDL_PIXELFORMAT_RGBA32) {
            throw std::runtime_error("Car name requires128x16 RGBA, doubled for Mega");
        }
        return ModelTextures::Texture(renderer.upload(bitmap));
    }

    static const CarPose* rival_pose(std::span<const RivalPose> rivals, std::uint32_t actor) {
        const auto found = std::ranges::find(rivals, actor, &RivalPose::actor_id);
        return found == rivals.end() ? nullptr : &found->car;
    }

    std::array<std::unique_ptr<ModelPass>, 5> cockpit_models;

    struct StaticPass {
        const Terrain& source;
        std::vector<ModelTextures::Texture> textures;
        std::vector<bool> mipmapped, palette;
        mutable TerrainLightingCache lighting_cache{prepare_terrain_colors_into};
        mutable std::vector<WorldChunk> geometry;

        StaticPass(ModelTextures& uploads, const Terrain& data) : source(data), geometry(data.chunks.size()) {
            for (const auto& texture : source.textures) {
                const auto uploaded = uploads.get_model(texture);
                textures.push_back(uploaded.texture);
                mipmapped.push_back(uploaded.mip_levels > 1);
                palette.push_back(uploaded.palette);
            }
        }
    } trees_baked, fence_baked, objects_baked, cones_baked, posts_baked, signs_baked, flags_baked;

    enum class ActorKind {
        scenery,
        cone,
        post,
        sign,
        flag,
        player,
        ghost,
        rival,
        moving,
        rotor
    };

    struct StaticBinding {
        const StaticPass* pass;
        std::uint32_t chunk;
    };

    struct RootBinding {
        const ModelPass* pass;
        std::uint32_t node;
        ActorKind kind;
        std::uint32_t actor = 0;
        bool second_driver = false;
    };

    std::vector<StaticBinding> static_bindings;
    std::vector<RootBinding> root_bindings;
    mutable std::vector<std::array<float, 3>> visibility_positions;
    mutable std::vector<std::uint8_t> visibility_masks;
    std::vector<std::uint32_t> ground_chunks;
    std::uint32_t player_root = no_visibility_entry, follow_root = no_visibility_entry, cockpit_root = no_visibility_entry;

    void register_static(const StaticPass& pass) {
        const auto ids = spatial.register_static(pass.source);
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (ids[index] != no_visibility_entry) {
                if (static_bindings.size() <= ids[index]) {
                    static_bindings.resize(ids[index] + 1);
                }
                static_bindings[ids[index]] = {&pass, std::uint32_t(index)};
            }
        }
    }

    void register_model(const ModelPass& pass, ActorKind kind, std::uint32_t actor = 0, bool second_driver = false) {
        for (std::size_t index = 0; index < pass.model.nodes.size(); ++index) {
            const auto& node = pass.model.nodes[index];
            if (node.parent >= 0 || (node.id_flags & 0x20000000u) || (node.id_flags & 0xffffffu) == 999) {
                continue;
            }
            const auto& matrix = node.transform;
            const auto id = spatial.register_root({matrix[12], matrix[13], matrix[14]}, node.id_flags, kind == ActorKind::ghost ? 0 : 15);
            if (id != root_bindings.size()) {
                throw std::runtime_error("Scene visibility root registration order disagrees");
            }
            root_bindings.push_back({&pass, std::uint32_t(index), kind, actor, second_driver});
            if (kind == ActorKind::player) {
                player_root = id;
            }
        }
    }

    void select_visibility(Visibility& selection, const CameraInputs& camera, const CarPose& player, const HorizonFrame& actors, const PropFrame& props, const CarPose* ghost,
        std::span<const RivalPose> rivals) const {
        auto& positions = visibility_positions;
        auto& masks = visibility_masks;
        positions.clear();
        masks.clear();
        for (std::size_t id = 0; id < root_bindings.size(); ++id) {
            const auto& binding = root_bindings[id];
            const auto* animation = binding.kind == ActorKind::cone     ? &props.cones
                                    : binding.kind == ActorKind::post   ? &props.posts
                                    : binding.kind == ActorKind::sign   ? &props.signs
                                    : binding.kind == ActorKind::moving ? &actors.moving
                                    : binding.kind == ActorKind::rotor  ? &actors.rotorcraft
                                                                        : nullptr;
            auto pose = binding.pass->model.nodes[binding.node].transform;
            std::uint8_t mask = 15;
            if (binding.kind == ActorKind::player) {
                pose = multiply(pose, player.object_to_world);
                mask = player.render_selection ? player.render_selection->root_mask : 15;
            } else if (binding.kind == ActorKind::ghost) {
                mask = ghost ? ghost->render_selection ? ghost->render_selection->root_mask : 15 : 0;
                pose = multiply(pose, ghost ? ghost->object_to_world : player.object_to_world);
            } else if (binding.kind == ActorKind::rival) {
                const auto* rival = rival_pose(rivals, binding.actor);
                mask = rival ? rival->render_selection ? rival->render_selection->root_mask : 15 : 0;
                pose = multiply(pose, rival ? rival->object_to_world : player.object_to_world);
            } else if (animation && !animation->transforms.empty()) {
                for (std::size_t component = 0; component < pose.size(); ++component) {
                    pose[component] = float(animation->transforms.at(binding.node)[component]) / 65536.f;
                }
                mask = animation->visible.at(binding.node) ? 15 : 0;
            }
            positions.push_back({pose[12], pose[13], pose[14]});
            masks.push_back(mask);
        }
        positions.push_back({player.object_to_world[12], player.object_to_world[13], player.object_to_world[14]});
        masks.push_back(15);
        positions.push_back({player.object_to_world[12], player.object_to_world[13], player.object_to_world[14]});
        masks.push_back(15);
        if (selection.frame().root_masks.empty()) {
            selection.initialize_roots(positions);
        }
        for (std::size_t id = 0; id < positions.size(); ++id) {
            selection.update_root(std::uint32_t(id), positions[id], masks[id]);
        }
        const auto source_root = camera.view == 1 ? cockpit_root : (camera.view == 4 || camera.view == 5) && player_root != no_visibility_entry ? player_root : follow_root;
        const std::array ancestors{camera.registered_camera_position ? selection.data().sector(*camera.registered_camera_position) : selection.root_sector(source_root)};
        selection.select(visibility_camera(camera.world_to_view, camera.projection), ancestors);
    }

    static std::shared_ptr<ModelTextures> car_textures(
        gpu::Renderer& renderer, std::shared_ptr<ModelTextures> textures, const frontend::CarModels& local, std::span<const RivalModel> rivals) {
        auto result = scene_textures(renderer, std::move(textures));
        std::vector<std::pair<std::uint32_t, const frontend::CarModels*>> ordered;
        std::uint32_t local_actor = 0;
        while (std::ranges::any_of(rivals, [&](const auto& rival) {
            return rival.actor_id == local_actor;
        })) {
            ++local_actor;
        }
        ordered.emplace_back(local_actor, &local);
        for (const auto& rival : rivals) {
            ordered.emplace_back(rival.actor_id, rival.car);
        }
        std::ranges::sort(ordered, {}, &decltype(ordered)::value_type::first);
        // 44df6e loads actor slots in order, each HIGH/MEDIUM/LOW. 496250
        // reuses the first named upload even if later BMP bytes differ.
        for (const auto& [actor, car] : ordered) {
            (void)actor;
            for (const auto& model : car->models) {
                for (const auto& texture : model.textures) {
                    if (texture.encoding == ImageEncoding::bmp) {
                        (void)result->get_model(texture);
                    }
                }
            }
        }
        return result;
    }

    Impl(gpu::Renderer& renderer, const SceneResources& source, const WeatherResources* weather_resources, const SurfaceEffectResources* surface_resources,
        const EngineEffectResources* engine_resources, std::span<const RivalModel> rivals, const frontend::CarModels& local_car, const CockpitResources& local_cockpit,
        bool sun_enabled, std::shared_ptr<ModelTextures> textures, std::shared_ptr<ModelMeshes> meshes)
        : resources(source), renderer(renderer), model_textures(car_textures(renderer, std::move(textures), local_car, rivals)),
          model_meshes(scene_meshes(renderer, std::move(meshes))),
          terrain(renderer, source.ground, source.setup.selection.detail == graphics_mega_detail,
              source.setup.selection.detail == graphics_mega_detail ? std::optional(source.setup.selection.track_id) : std::nullopt, model_textures),
          spatial(source.ground), car(*model_textures, *model_meshes,
                                      prepare_car_model(local_car.models[local_car_model_index(source.setup.selection.detail)], local_car.car_id, source.setup.selection.detail,
                                          local_car_model_index(source.setup.selection.detail)),
                                      {}, local_car.car_id, source.setup.selection.detail == graphics_mega_detail),
          horizon(*model_textures, *model_meshes, source.horizon), trees(*model_textures, *model_meshes, source.trees), fence(*model_textures, *model_meshes, source.fence),
          objects(*model_textures, *model_meshes, source.objects), cones(*model_textures, *model_meshes, prepare_cone_model(source.cones.model, source.setup.selection.track_id)),
          posts(*model_textures, *model_meshes, source.posts.model), signs(*model_textures, *model_meshes, source.signs.model),
          flags(*model_textures, *model_meshes, source.flags.model),
          moving_horizon(*model_textures, *model_meshes, source.moving_horizon.moving, source.moving_horizon.texture_palette),
          rotorcraft(*model_textures, *model_meshes, source.moving_horizon.rotorcraft, source.moving_horizon.texture_palette), trees_baked(*model_textures, source.trees_baked),
          fence_baked(*model_textures, source.fence_baked), objects_baked(*model_textures, source.objects_baked), cones_baked(*model_textures, source.cones.baked),
          posts_baked(*model_textures, source.posts.baked), signs_baked(*model_textures, source.signs.baked), flags_baked(*model_textures, source.flags.baked) {
        renderer.set_model_lighting_table(model_lighting_angles());
        local_car_source = &local_car;
        grid_lines = prepare_grid_lines(source);
        if (!local_cockpit.models[0].nodes.empty()) {
            cockpit.emplace(local_car.car_id, source.setup.weather);
        }
        for (std::size_t index = 0; index < cockpit_models.size(); ++index) {
            if (!local_cockpit.models[index].nodes.empty()) {
                cockpit_models[index] = std::make_unique<ModelPass>(*model_textures, *model_meshes, local_cockpit.models[index]);
            }
        }
        if (weather_resources) {
            weather = std::make_unique<WeatherView>(renderer, *weather_resources);
        }
        if (surface_resources) {
            surface_effects = std::make_unique<SurfaceEffectsView>(renderer, *surface_resources);
        }
        if (engine_resources) {
            engine_effects = std::make_unique<EngineEffectsView>(renderer, *engine_resources);
        }
        if (sun_enabled && !source.sun.textures.empty()) {
            sun = std::make_unique<SunView>(renderer, source.sun);
        }
        for (std::size_t index = 0; index < car_glow_textures.size(); ++index) {
            if (!source.car_glow_bmps[index].empty()) {
                car_glow_textures[index] = renderer.upload(decode_bmp(source.car_glow_bmps[index]).get());
            }
        }
        local_car_id = local_car.car_id;
        if (local_car_model_index(source.setup.selection.detail) != 0) {
            detached_car = std::make_unique<ModelPass>(*model_textures, *model_meshes, local_car.models[0], std::span<const std::uint8_t>{}, local_car.car_id);
        }
        if (!source.car_shadow_color_bmp.empty() || !source.car_shadow_alpha_bmp.empty()) {
            const auto color = decode_bmp(source.car_shadow_color_bmp);
            const auto alpha = decode_bmp(source.car_shadow_alpha_bmp);
            gpu::Surface rgba(SDL_ConvertSurface(color.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            gpu::check(bool(rgba), "Convert original car shadow RGB");
            if (rgba->w != 64 || rgba->h != 64 || alpha->w != 64 || alpha->h != 64 || alpha->format != SDL_PIXELFORMAT_INDEX8) {
                throw std::runtime_error("Original car shadow requires64x64 RGB and indexed alpha images");
            }
            // 484bc2 reads SHADAM byte indices, not palette RGB or a black key.
            // The matched original uses A4R4G4B4: discard the low alpha
            // nibble before bilinear sampling, then expand UNORM4 to RGBA8.
            for (int y = 0; y < rgba->h; ++y) {
                auto* pixels = static_cast<std::uint8_t*>(rgba->pixels) + y * rgba->pitch;
                const auto* opacity = static_cast<const std::uint8_t*>(alpha->pixels) + y * alpha->pitch;
                for (int x = 0; x < rgba->w; ++x) {
                    pixels[x * 4 + 3] = (opacity[x] >> 4) * 17;
                }
            }
            car_shadow_texture = renderer.upload(rgba.get());
        }
        // 44f0a3 ->45c4c0/45c4d0 resets registered cones to an upright basis
        // at their authored positions. Unregistered scenegraph roots stay intact.
        for (const auto& actor : source.cones.actors) {
            auto& matrix = cones.model.nodes.at(actor.root_node).transform;
            for (std::size_t row = 0; row < 3; ++row) {
                std::copy_n(identity.begin() + row * 4, 3, matrix.begin() + row * 4);
            }
        }
        // 44e7bb/47de00: source allocation order. Static entries prepend in
        // each sector; roots append. The sky ID999 bypasses registration.
        register_static(trees_baked);
        register_model(trees, ActorKind::scenery);
        register_static(fence_baked);
        register_model(fence, ActorKind::scenery);
        register_static(objects_baked);
        register_model(objects, ActorKind::scenery);
        register_static(cones_baked);
        register_model(cones, ActorKind::cone);
        register_static(posts_baked);
        register_model(posts, ActorKind::post);
        register_static(signs_baked);
        register_model(signs, ActorKind::sign);
        register_static(flags_baked);
        register_model(flags, ActorKind::flag);
        register_model(car, ActorKind::player);
        register_model(car, ActorKind::ghost);
        for (const auto& rival : rivals) {
            const auto car_id = rival.car->car_id;
            rival_car_ids.at(rival.actor_id) = car_id;
            auto& models = rival_models.at(car_id);
            if (!models[0]) {
                for (std::size_t lod = 0; lod < models.size(); ++lod) {
                    models[lod] = std::make_unique<ModelPass>(*model_textures, *model_meshes,
                        prepare_car_model(rival.car->models[lod], car_id, source.setup.selection.detail, std::uint32_t(lod)), std::span<const std::uint8_t>{}, car_id,
                        source.setup.selection.detail == graphics_mega_detail && lod == 0);
                }
            }
            register_model(*models[0], ActorKind::rival, rival.actor_id, rival.second_driver);
        }
        follow_root = spatial.register_root({0, 0, 0}, 0);
        cockpit_root = spatial.register_root({0, 0, 0}, 0);
    }

    struct ModelFrame {
        const ModelPass* pass = nullptr;
        Matrix pose{};
        const CarPose* car_pose = nullptr;
        const HorizonModelFrame* actor_frame = nullptr;
        std::uint32_t detail = 0;
        bool hide_front_wheels = false;
        std::vector<Matrix> matrices;
        std::vector<bool> hidden;
    };

    std::vector<ModelFrame> frame_models;
    std::size_t frame_models_active = 0;

    const ModelFrame& prepare_model(
        const ModelPass& pass, const Matrix& pose, const CarPose* car_pose, const HorizonModelFrame* actor_frame, std::uint32_t detail, bool hide_front_wheels) {
        // Inactive slots retain only storage. Their metadata may refer to a
        // replaced model or a previous caller's poses and must never be read.
        const auto active = std::span(frame_models).first(frame_models_active);
        const auto found = std::ranges::find_if(active, [&](const auto& value) {
            return value.pass == &pass && value.pose == pose && value.car_pose == car_pose && value.actor_frame == actor_frame && value.detail == detail
                   && value.hide_front_wheels == hide_front_wheels;
        });
        if (found != active.end()) {
            return *found;
        }
        if (frame_models_active == frame_models.size()) {
            frame_models.emplace_back();
        }
        auto& result = frame_models[frame_models_active];
        if (actor_frame) {
            horizon_actor_world_transforms_into(result.matrices, pass.model, *actor_frame);
        } else if (car_pose) {
            car_world_transforms_into(result.matrices, pass.model, *car_pose, detail);
        } else {
            model_world_transforms_into(result.matrices, pass.model, pose);
        }
        result.hidden.assign(pass.model.nodes.size(), false);
        const auto* selection = car_pose && car_pose->render_selection ? &*car_pose->render_selection : nullptr;
        if (selection
            && ((!selection->node_masks.empty() && selection->node_masks.size() != pass.model.nodes.size())
                || (!selection->node_variants.empty() && selection->node_variants.size() != pass.model.nodes.size()))) {
            throw std::runtime_error("Published car node selection must cover the selected canonical family");
        }
        for (std::size_t index = 0; index < pass.model.nodes.size(); ++index) {
            const auto& node = pass.model.nodes[index];
            const auto id = node.id_flags & 0xffffffu;
            result.hidden[index] = (hide_front_wheels && (id == 1 || id == 2)) || (actor_frame && actor_frame->visible[index] == 0)
                                   || (selection && !selection->node_masks.empty() && (selection->node_masks[index] & 1) == 0)
                                   || (node.parent >= 0 && result.hidden[std::size_t(node.parent)]);
        }
        result.pass = &pass;
        result.pose = pose;
        result.car_pose = car_pose;
        result.actor_frame = actor_frame;
        result.detail = detail;
        result.hide_front_wheels = hide_front_wheels;
        ++frame_models_active; // Publish only a completely prepared slot.
        return result;
    }

    void draw_model(gpu::Frame& frame, const ModelPass& pass, const Matrix& pose, const CameraInputs& camera, const Lighting& lighting, bool sky = false, bool dynamic_only = false,
        const CarPose* car_pose = nullptr, std::uint32_t detail = 3, std::span<const std::uint8_t> variants = {}, const HorizonModelFrame* actor_frame = nullptr,
        bool ghost = false, std::optional<std::uint32_t> selected_root = {}, bool hide_front_wheels = false, bool second_driver = false, std::optional<std::uint32_t> sky_node = {},
        SDL_GPUTexture* name = nullptr, bool smooth_name = false) {
        if (!variants.empty() && variants.size() != pass.model.nodes.size()) {
            throw std::runtime_error("Scene mesh selection must match its model nodes");
        }
        // Sector ordering can revisit many roots of one model. Prepare its
        // hierarchy once for this frame/actor, then visit only the chosen root.
        const auto& prepared = prepare_model(pass, pose, car_pose, actor_frame, detail, hide_front_wheels);
        if (selected_root && *selected_root >= pass.root_nodes.size()) {
            return;
        }
        const auto& nodes = selected_root ? pass.root_nodes[*selected_root] : pass.all_nodes;
        auto projection_camera = camera;
        // 0048ea47 ignores far rejection before forcing the sky depth.
        if (sky) {
            projection_camera.projection.far_plane = std::numeric_limits<float>::max();
        }
        for (const auto index : nodes) {
            const auto& node = pass.model.nodes[index];
            const auto id = node.id_flags & 0xffffffu;
            // 494a40 marks a node from its first variant's DASH prefix;
            // 48e837 then disables both Z testing and writes for that node.
            const bool dashboard = !node.variants.empty() && node.variants.front() != UINT16_MAX && pass.model.meshes.at(node.variants.front()).name.starts_with("DASH");
            const auto root = pass.roots[index];
            const bool baked = (pass.model.nodes[root].id_flags & 0x20000000u) != 0;
            // 00489069 tests the node's own camera mask in allocation order.
            if (prepared.hidden[index] || (sky_node && index != *sky_node) || (dynamic_only && baked) || node.variants.empty()) {
                continue;
            }
            const auto selected_variant = car_pose && car_pose->render_selection && !car_pose->render_selection->node_variants.empty()
                                              ? car_pose->render_selection->node_variants[index]
                                              : std::int16_t(car_pose           ? car_damage_variant(node.id_flags, car_pose->damage_levels)
                                                             : variants.empty() ? 0
                                                                                : variants[index]);
            if (selected_variant < 0) {
                continue;
            }
            const auto variant = std::size_t(selected_variant);
            const auto mesh_index = node.variants.at(variant);
            if (mesh_index == 0xffff) {
                continue;
            }
            const auto& geometry = pass.geometry.at(mesh_index);
            const auto view = frame.world_view(world_projection(projection_camera));
            for (const auto& run : geometry.runs) {
                const auto texture_slot = pass.livery.texture_slot(node.id_flags, run.texture, second_driver, !car_pose || car_pose->remap_livery);
                const auto& name_row = run.texture < 0 ? std::optional<std::array<float, 2>>{} : pass.name_rows.at(std::size_t(run.texture));
                const bool named = name && (id == 15 || id == 16 || id == 21) && name_row && (*name_row)[1] > (*name_row)[0];
                const bool antialias = named && smooth_name;
                const auto texture = named ? name : texture_slot < 0 ? nullptr : pass.textures.at(std::size_t(texture_slot)).get();
                const bool webp = texture && texture_slot >= 0 && pass.model.textures.at(std::size_t(texture_slot)).encoding == ImageEncoding::webp;
                const bool mipmapped = !named && texture_slot >= 0 && pass.mipmapped.at(std::size_t(texture_slot));
                const auto flags = (sky ? 1u : 0u) | (ghost ? 2u : 0u) | (ghost && pass.ghost_wheels[index] ? 4u : 0u) | (named ? 8u : 0u)
                                   | (pass.original_materials && run.texture < 0 ? 32u : 0u);
                if (frame.model_instances.size() >= UINT32_MAX) {
                    throw std::runtime_error("Model frame exceeds GPU instance capacity");
                }
                const auto instance = Uint32(frame.model_instances.size());
                frame.model_instances.push_back({prepared.matrices[index], {lighting.direction[0], lighting.direction[1], lighting.direction[2], lighting.diffuse},
                    {lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], flags},
                    named ? std::array<float, 4>{(*name_row)[0], (*name_row)[1] - (*name_row)[0], 0, 0} : std::array<float, 4>{0, 1, 0, 0}});
                frame.draws.push_back({{.texture = texture,
                                           .depth = !sky && !dashboard,
                                           .blend = ghost || antialias,
                                           .key = !sky && texture != nullptr,
                                           .depth_write = !dashboard, // 489069 retains ghost Z writes.
                                           .linear = antialias || webp,
                                           .clamp = antialias || (texture && !webp),
                                           .alpha_cutoff = !antialias && !sky && webp ? .5f : 0.f,
                                           .mipmapped = mipmapped,
                                           .anisotropy = mipmapped ? gpu::Anisotropy::x2 : gpu::Anisotropy::off,
                                           .palette = !named && texture_slot >= 0 && pass.palette.at(std::size_t(texture_slot)),
                                           .reference_indexed = named && !smooth_name},
                    run.first, run.count, 0, {}, false, geometry.mesh, view, instance});
            }
        }
    }

    static void draw_static(gpu::Frame& frame, gpu::Renderer& renderer, const StaticPass& pass, const CameraInputs& camera, const Lighting& lighting, std::uint32_t chunk_index) {
        pass.geometry.at(chunk_index).append(frame, renderer, camera, lighting, [&](WorldChunkBuilder& built) {
            const auto& chunk = pass.source.chunks.at(chunk_index);
            const auto& colors = pass.lighting_cache.colors(chunk, lighting);
            for (const auto& face : chunk.mesh.faces) {
                std::array<LitVertex, 3> triangle;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const auto vertex_index = face.indices[corner];
                    const auto& vertex = chunk.mesh.vertices.at(vertex_index);
                    triangle[corner] = {vertex.position, colors.at(vertex_index), vertex.u, vertex.v};
                }
                const auto texture = face.texture < 0 ? nullptr : pass.textures.at(std::size_t(face.texture)).get();
                // Back-to-back DA09AD boards can share a D16 depth bin even
                // with disjoint planes. Added metal yields to source artwork.
                const bool board_backing = texture && chunk.mesh.name.starts_with("DA09AD") && pass.source.textures.at(std::size_t(face.texture)).name == "MegaSurface";
                built.triangle({.texture = texture,
                                   .depth = true,
                                   .key = texture != nullptr,
                                   .linear = texture && pass.source.textures.at(std::size_t(face.texture)).encoding == ImageEncoding::webp,
                                   .clamp = texture && pass.source.textures.at(std::size_t(face.texture)).encoding == ImageEncoding::bmp,
                                   .alpha_cutoff = texture && pass.source.textures.at(std::size_t(face.texture)).encoding == ImageEncoding::webp ? .5f : 0.f,
                                   .mipmapped = texture && pass.mipmapped.at(std::size_t(face.texture)),
                                   .anisotropy = texture && pass.mipmapped.at(std::size_t(face.texture)) ? gpu::Anisotropy::x2 : gpu::Anisotropy::off,
                                   .depth_less = board_backing,
                                   .palette = texture && pass.palette.at(std::size_t(face.texture))},
                    triangle);
            }
        });
    }

    void draw_damage_parts(gpu::Frame& frame, std::span<const simulation::DetachedPart> parts, const CameraInputs& camera, const Lighting& lighting, const CarPose& player) const {
        for (const auto& part : parts) {
            if (!part.active) {
                continue;
            }
            const auto rival = std::ranges::find_if(root_bindings, [&](const auto& binding) {
                return binding.kind == ActorKind::rival && binding.actor == part.actor_id;
            });
            const auto& pass = rival != root_bindings.end() ? *rival_models.at(rival_car_ids.at(part.actor_id))[0] : detached_car ? *detached_car : car;
            const bool second_driver = rival != root_bindings.end() ? rival->second_driver : player.second_driver;
            if (part.source_slot > 4) {
                throw std::runtime_error("Detached car component requires its original high-detail car family and slot0..4");
            }
            // 416320 clones variant2 of the high-detail component twice.
            // Front/rear components exchange meshes on the reversed face.
            for (const bool reverse : {false, true}) {
                const auto slot = reverse && part.source_slot < 2 ? 1 - part.source_slot : part.source_slot;
                const auto id = car_damage_node(std::uint8_t(slot));
                const auto node = std::ranges::find_if(pass.model.nodes, [&](const auto& value) {
                    return (value.id_flags & 0xffffffu) == id;
                });
                if (node == pass.model.nodes.end()) {
                    throw std::runtime_error("Detached component is absent from its original high-detail model");
                }
                const auto mesh_index = node->variants.at(2);
                if (mesh_index == 0xffff) {
                    continue;
                }
                const auto matrix = car_damage_part_transform(part, reverse);
                const auto& geometry = pass.geometry.at(mesh_index);
                const auto view = frame.world_view(world_projection(camera));
                for (const auto& run : geometry.runs) {
                    const auto texture_slot = pass.livery.texture_slot(node->id_flags, run.texture, second_driver);
                    const auto texture = texture_slot < 0 ? nullptr : pass.textures.at(std::size_t(texture_slot)).get();
                    const auto instance = Uint32(frame.model_instances.size());
                    frame.model_instances.push_back({matrix, {lighting.direction[0], lighting.direction[1], lighting.direction[2], lighting.diffuse},
                        {lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], 0}});
                    frame.draws.push_back({{.texture = texture,
                                               .depth = true,
                                               .key = texture != nullptr,
                                               .clamp = texture && pass.model.textures.at(std::size_t(texture_slot)).encoding == ImageEncoding::bmp,
                                               .alpha_cutoff = texture && pass.model.textures.at(std::size_t(texture_slot)).encoding == ImageEncoding::webp ? .5f : 0.f,
                                               .palette = texture && pass.palette.at(std::size_t(texture_slot))},
                        run.first, run.count, 0, {}, false, geometry.mesh, view, instance});
                }
            }
        }
    }
};

SceneView::SceneView(gpu::Renderer& renderer, const SceneResources& resources, const WeatherResources* weather, const SurfaceEffectResources* surface_effects,
    const EngineEffectResources* engine_effects, std::span<const RivalModel> rivals, const frontend::CarModels* local_car, const CockpitResources* local_cockpit, bool sun_enabled,
    std::shared_ptr<ModelTextures> textures, std::shared_ptr<ModelMeshes> meshes)
    : impl_(std::make_unique<Impl>(renderer, resources, weather, surface_effects, engine_effects, rivals, local_car ? *local_car : resources.car,
          local_cockpit ? *local_cockpit : resources.cockpit, sun_enabled, std::move(textures), std::move(meshes))) {}

SceneView::~SceneView() = default;

std::shared_ptr<ModelTextures> SceneView::model_textures() const {
    return impl_->model_textures;
}

std::shared_ptr<ModelMeshes> SceneView::model_meshes() const {
    return impl_->model_meshes;
}

void SceneView::set_player_name(gpu::Renderer& renderer, SDL_Surface* bitmap) {
    impl_->player_name = impl_->upload_name(renderer, bitmap);
}

void SceneView::set_rival_name(gpu::Renderer& renderer, std::uint32_t actor, SDL_Surface* bitmap) {
    impl_->rival_names.at(actor) = impl_->upload_name(renderer, bitmap);
}

void SceneView::set_ghost_name(gpu::Renderer& renderer, SDL_Surface* bitmap) {
    impl_->ghost_name = impl_->upload_name(renderer, bitmap);
}

void SceneView::set_ghost_model(gpu::Renderer& renderer, const frontend::CarModels& model) {
    impl_->model_textures->require_renderer(renderer);
    impl_->prepare_ghost_models(impl_->ghost_car, model);
}

void SceneView::gear_changed() {
    if (impl_->cockpit) {
        impl_->cockpit->shift();
    }
}

std::uint8_t SceneView::cockpit_shift_phase() const {
    return impl_->cockpit ? impl_->cockpit->shift_phase() : 0;
}

void SceneView::update_visibility(
    const CameraInputs& camera, const CarPose& car, const HorizonFrame& actors, const PropFrame& props, const CarPose* ghost, std::span<const RivalPose> rivals) {
    if (camera.view == 1 && impl_->cockpit) {
        impl_->cockpit->update(car.steering_angle);
    }
    //4271d0 reads the retained car-parent world position before47f410's
    //post-model migration. The cockpit camera migrates its own head anchor.
    const auto parent = impl_->player_root < impl_->visibility_positions.size() ? std::optional(impl_->visibility_positions[impl_->player_root]) : std::nullopt;
    impl_->select_visibility(impl_->spatial, camera, car, actors, props, ghost, rivals);
    //44d91f registers an empty physical parent for each car. 4608b0 publishes
    //body translation there; cockpit hiding only changes the model children.
    const CarScenePose player{0, {car.object_to_world[12], car.object_to_world[13], car.object_to_world[14]},
        impl_->player_root == no_visibility_entry ? std::uint8_t(0) : impl_->spatial.frame().root_mask(impl_->player_root), true,
        impl_->player_root == no_visibility_entry ? std::uint8_t(0) : std::uint8_t(1)};
    std::array<CarScenePose, 16> poses;
    if (rivals.size() > poses.size()) {
        throw std::runtime_error("Scene visibility exceeds sixteen rival roles");
    }
    std::size_t count = 0;
    for (const auto& rival : rivals) {
        const auto& pose = rival.car.object_to_world;
        poses[count++] = {rival.actor_id, {pose[12], pose[13], pose[14]}, rival_visibility(rival.actor_id)};
    }
    if (ghost) {
        if (count == poses.size()) {
            throw std::runtime_error("Ghost and rivals exceed sixteen scene roles");
        }
        if (Impl::ghost_actor(*ghost) == (car.actor_id == UINT32_MAX ? 0u : car.actor_id)) {
            throw std::runtime_error("Ghost scene role duplicates the local player");
        }
        std::uint8_t mask = 0;
        for (std::size_t index = 0; index < impl_->root_bindings.size(); ++index) {
            if (impl_->root_bindings[index].kind == Impl::ActorKind::ghost) {
                mask |= impl_->spatial.frame().root_mask(std::uint32_t(index));
            }
        }
        const auto& pose = ghost->object_to_world;
        poses[count++] = {Impl::ghost_actor(*ghost), {pose[12], pose[13], pose[14]}, mask};
    }
    const bool source_views = camera.canonical_single_player && camera.view >= 1 && camera.view <= 5 && !camera.registered_camera_position
                              && impl_->resources.setup.selection.detail < graphics_mega_detail;
    const auto world = camera.published_world_position.value_or(visibility_camera(camera.world_to_view, camera.projection).world_position);
    std::array<std::int32_t, 3> forward{};
    if (source_views) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto value = std::nearbyint(double(car.object_to_world[axis]) * 65536.0);
            if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
                throw std::runtime_error("Source scene body forward is outside fixed coordinates");
            }
            forward[axis] = std::int32_t(value);
        }
    }
    impl_->cars.update(impl_->spatial.data(), impl_->spatial.frame(),
        {source_views ? fleet_scene_observer(camera.view, false, world, player.position, forward, parent) : world, impl_->resources.setup.selection.detail,
            !camera.hide_player && local_car_visible(camera.view), source_views},
        player, std::span(poses).first(count), source_views);
}

const VisibilityFrame& SceneView::visibility() const {
    return impl_->spatial.frame();
}

FleetSceneContext SceneView::scene_context(std::uint32_t local_actor) const {
    return impl_->cars.context(local_actor);
}

std::uint8_t SceneView::player_visibility() const {
    // 4271d0 only recomputes the car-sector effect gate for multiplayer.
    // Supported single-player modes retain the initial/manual view bit1.
    return impl_->player_root == no_visibility_entry ? 0 : 1;
}

bool SceneView::rival_sector_visible(std::uint32_t actor) const {
    return impl_->cars.rival(actor).sector_visible;
}

std::uint8_t SceneView::rival_visibility(std::uint32_t actor) const {
    std::uint8_t result = 0;
    for (std::size_t index = 0; index < impl_->root_bindings.size(); ++index) {
        const auto& binding = impl_->root_bindings[index];
        if (binding.kind == Impl::ActorKind::rival && binding.actor == actor) {
            result |= impl_->spatial.frame().root_mask(std::uint32_t(index));
        }
    }
    return result;
}

void SceneView::reset_contact_registration() {
    impl_->cars.reset_registration();
}

RootSectorRegistration SceneView::player_contact_registration() const {
    return impl_->cars.player().physical;
}

RootSectorRegistration SceneView::rival_contact_registration(std::uint32_t actor) const {
    return impl_->cars.rival(actor).physical;
}

std::uint8_t SceneView::rival_model_detail(std::uint32_t actor) const {
    return impl_->cars.rival(actor).model_detail;
}

gpu::Frame SceneView::frame(const CameraInputs& camera, const Lighting& lighting, const CarPose& car, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors,
    const PropFrame& props, std::span<const PropFragmentTriangle> fragments, const WeatherFrame* weather, std::span<const simulation::DetachedPart> damage_parts,
    std::span<const ContactSparkLine> sparks, const SurfaceEffectFrame* surface_effects, const SkidMarkFrame* skid_marks, const CarPose* ghost, bool ghost_uses_player_car,
    const EngineEffectFrame* engine_effects, std::span<const RivalPose> rivals, float presentation_alpha) const {
    gpu::Frame result;
    frame_into(result, camera, lighting, car, flag_variants, actors, props, fragments, weather, damage_parts, sparks, surface_effects, skid_marks, ghost, ghost_uses_player_car,
        engine_effects, rivals, presentation_alpha);
    return result;
}

void SceneView::frame_into(gpu::Frame& result, const CameraInputs& camera, const Lighting& lighting, const CarPose& car, std::span<const std::uint8_t> flag_variants,
    const HorizonFrame& actors, const PropFrame& props, std::span<const PropFragmentTriangle> fragments, const WeatherFrame* weather,
    std::span<const simulation::DetachedPart> damage_parts, std::span<const ContactSparkLine> sparks, const SurfaceEffectFrame* surface_effects, const SkidMarkFrame* skid_marks,
    const CarPose* ghost, bool ghost_uses_player_car, const EngineEffectFrame* engine_effects, std::span<const RivalPose> rivals, float presentation_alpha) const {
    validate(camera);
    const auto projection = world_projection(camera);
    if (flag_variants.size() != impl_->flags.model.nodes.size()) {
        throw std::runtime_error("Scene flag animation must match the loaded flag hierarchy");
    }
    result.clear();
    result.color_load = impl_->resources.setup.selection.detail >= 2 && impl_->resources.setup.selection.track_id != 9 ? gpu::ColorLoad::preserve : gpu::ColorLoad::clear;
    result.multisample = impl_->resources.setup.selection.detail == graphics_mega_detail;
    impl_->frame_models_active = 0;
    if (impl_->resources.setup.selection.detail < 2) {
        // 452376/44e7bb: the ordinary device path omits the sky model at
        // detail0/1. 44f0a3 setup ->486e75 supplies the retained clear color.
        // Follow the matched reference device's 4949c9()==0 branch.
        constexpr std::array<std::uint32_t, 6> colors{0, 0xff64b4d0, 0xffb2d9e7, 0xffa4adbe, 0xff4a616b, 0xff28313a};
        const auto color = impl_->resources.setup.selection.track_id == 9 ? 0xffad590cu : colors.at(impl_->resources.setup.weather);
        result.quad({.depth_write = false}, 0, 0, float(camera.projection.width), float(camera.projection.height), color);
    } else {
        // 4315a0 alternates the two storm nodes with nonzero flash-table
        // alpha. The lit node uses05L on two panels and05 on the rest.
        // Lavaland and Extras select different horizons before this gate.
        const auto storm = impl_->resources.setup.horizon_index == 4;
        std::optional<std::uint32_t> sky_node;
        if (storm) {
            sky_node = weather && weather->lightning_alpha != 0 ? 2u : 1u;
        }
        impl_->draw_model(result, impl_->horizon, horizon_world_transform(camera, impl_->resources.setup.selection.track_id), camera, lighting, true, false, nullptr, 3, {},
            nullptr, false, {}, false, false, sky_node);
    }
    // 489069 draws the separately allocated HOR root before terrain. It
    // bypasses the ground sectors and uses48f5f9's original2000-unit depth.
    auto horizon_camera = camera;
    horizon_camera.projection.near_plane = .25f;
    horizon_camera.projection.far_plane = 2000.f;
    horizon_camera.projection.depth_scale = float(0x10008) / 65536.f;
    horizon_camera.projection.depth_offset = -float(0x4002) / 65536.f;
    impl_->draw_model(result, impl_->moving_horizon, identity, horizon_camera, lighting, false, false, nullptr, 3, {}, &actors.moving);
    impl_->draw_model(result, impl_->rotorcraft, identity, horizon_camera, lighting, false, false, nullptr, 3, {}, &actors.rotorcraft);
    impl_->presentation_visibility = impl_->spatial;
    auto& selection = *impl_->presentation_visibility;
    impl_->select_visibility(selection, camera, car, actors, props, ghost, rivals);
    const auto& visible = selection.frame();
    auto& ground_chunks = impl_->ground_chunks;
    ground_chunks.clear();
    for (const auto sector : visible.terrain_sectors) {
        const auto chunk = selection.data().sectors[sector].ground_chunk;
        if (chunk != no_visibility_entry) {
            ground_chunks.push_back(chunk);
        }
    }
    const auto selected_chunks = selection.data().sectors.empty() ? std::optional<std::span<const std::uint32_t>>{} : std::optional<std::span<const std::uint32_t>>(ground_chunks);
    impl_->terrain.append(result, camera, lighting, selected_chunks);
    if (skid_marks) {
        append_skid_marks(result, camera, *skid_marks, visible.ordered_sectors);
    }
    // 442500 draws443030 after terrain/skids, before498430's shadows.
    const auto camera_world = camera.published_world_position.value_or(visibility_camera(camera.world_to_view, camera.projection).world_position);
    std::array<std::int32_t, 3> camera_q16;
    std::ranges::transform(camera_world, camera_q16.begin(), [](float value) {
        return std::int32_t(std::nearbyint(double(value) * 65536.0));
    });
    const auto generated_lines = camera.queued_grid_lines ? std::vector<QueuedWorldLine>{} : queued_grid_lines(impl_->grid_lines, impl_->resources.setup, camera_q16);
    const auto& grid_lines = camera.queued_grid_lines ? *camera.queued_grid_lines : generated_lines;
    for (const auto& line : grid_lines) {
        const auto vertex = [&](const auto& point) {
            gpu::WorldVertex output{{}, line.color};
            std::ranges::transform(point, output.position.begin(), [](auto value) {
                return float(value) / 65536.f;
            });
            return output;
        };
        const auto always = impl_->resources.setup.selection.track_id != 4 && impl_->resources.setup.selection.track_id != 7;
        // 443030's4/7 ZBIAS16 is ignored by the measured wrapper: actual
        // rasterizer bias0, unchanged GPU XYZ, LEQUAL; other tracks use ALWAYS.
        result.world_line({.depth = true, .lines = true, .depth_always = always}, {vertex(line.from), vertex(line.to)}, projection);
    }
    std::vector<std::pair<std::uint32_t, const CarShadowFootprint*>> shadows;
    if (car.shadow && !camera.hide_player && local_car_visible(camera.view)) {
        shadows.emplace_back(car.actor_id == UINT32_MAX ? 0u : car.actor_id, &*car.shadow);
    }
    for (const auto& rival : rivals) {
        if (rival.car.shadow) {
            shadows.emplace_back(rival.actor_id, &*rival.car.shadow);
        }
    }
    std::ranges::stable_sort(shadows, {}, &decltype(shadows)::value_type::first);
    for (const auto& [actor, shadow] : shadows) {
        (void)actor;
        if (!impl_->car_shadow_texture) {
            throw std::runtime_error("Car shadow pose requires its original scene textures");
        }
        // 439280 visits actor slots in ascending order before498430 submits
        // the shared queue, including a local player in a later grid slot.
        append_car_shadow(result, impl_->car_shadow_texture.get(), *shadow, projection);
    }
    std::array<std::vector<SurfaceEffectSprite>, 2> lamp_glows;
    // 442210 admits32 individual descriptors, independently of497070's halo
    // queue. Retain source actor/side order when front and rear images mix.
    std::vector<std::pair<SurfaceEffectSprite, bool>> lamp_ground;
    for (const auto& entry : visible.draws) {
        if (!entry.dynamic) {
            const auto& binding = impl_->static_bindings.at(entry.id);
            Impl::draw_static(result, impl_->renderer, *binding.pass, camera, lighting, binding.chunk);
            continue;
        }
        if (entry.id == impl_->follow_root || entry.id == impl_->cockpit_root) {
            continue; // Source camera anchor has no geometry.
        }
        const auto& binding = impl_->root_bindings.at(entry.id);
        using Kind = Impl::ActorKind;
        const auto kind = binding.kind;
        if (kind == Kind::player && (camera.hide_player || !local_car_visible(camera.view))) {
            continue;
        }
        const auto* animation = kind == Kind::cone     ? &props.cones
                                : kind == Kind::post   ? &props.posts
                                : kind == Kind::sign   ? &props.signs
                                : kind == Kind::moving ? &actors.moving
                                : kind == Kind::rotor  ? &actors.rotorcraft
                                                       : nullptr;
        if (animation && animation->transforms.empty()) {
            animation = nullptr;
        }
        const auto* pose = kind == Kind::player ? &car : kind == Kind::ghost ? ghost : kind == Kind::rival ? Impl::rival_pose(rivals, binding.actor) : nullptr;
        const auto model_detail = kind == Kind::rival || kind == Kind::ghost
                                      ? pose && pose->render_selection ? pose->render_selection->model_detail
                                                                       : impl_->cars.rival(kind == Kind::ghost && pose ? Impl::ghost_actor(*pose) : binding.actor).model_detail
                                      : local_car_model_index(impl_->resources.setup.selection.detail);
        if (model_detail > 2 || (pose && pose->render_selection && pose->render_selection->root_mask > 15)) {
            throw std::runtime_error("Published car rendering requires a model family0..2 and four viewport bits");
        }
        if (kind == Kind::ghost && ghost_uses_player_car && !impl_->local_ghost_car[0]) {
            impl_->prepare_ghost_models(impl_->local_ghost_car, *impl_->local_car_source);
        }
        const auto* pass = kind == Kind::ghost   ? (ghost_uses_player_car ? impl_->local_ghost_car : impl_->ghost_car)[model_detail].get()
                           : kind == Kind::rival ? impl_->rival_models[impl_->rival_car_ids[binding.actor]][model_detail].get()
                                                 : binding.pass;
        if (!pass) {
            throw std::runtime_error("Visible ghost requires its recorded car model");
        }
        // 4608b0 animates the selected model family, not the global detail.
        const auto animation_detail = kind == Kind::rival || kind == Kind::ghost ? (model_detail == 0 ? 3u : 1u) : impl_->resources.setup.selection.detail;
        impl_->draw_model(result, *pass, pose ? pose->object_to_world : identity, camera, lighting, false, !pose, pose, animation_detail,
            kind == Kind::flag ? flag_variants : std::span<const std::uint8_t>{}, animation, kind == Kind::ghost, binding.node, kind == Kind::player && camera.view == 4,
            kind == Kind::rival ? binding.second_driver : pose && pose->second_driver, {},
            kind == Kind::player  ? impl_->player_name.get()
            : kind == Kind::rival ? impl_->rival_names.at(binding.actor).get()
            : kind == Kind::ghost ? (impl_->ghost_name ? impl_->ghost_name.get() : impl_->player_name.get())
                                  : nullptr,
            impl_->resources.setup.selection.detail == graphics_mega_detail);
        if (pose && (kind == Kind::player || kind == Kind::rival) && (pose->lights.headlights || pose->lights.tail || pose->lights.brake)) {
            const auto position = weather_camera(camera.world_to_view).position;
            float facing = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                facing += pose->object_to_world[axis] * (float(position[axis]) / 65536.f - pose->object_to_world[12 + axis]);
            }
            const auto car_id = kind == Kind::rival ? impl_->rival_car_ids[binding.actor] : impl_->local_car_id;
            const auto lamps = car_lamp_frame(car_id, impl_->resources.setup.selection.detail, pose->lights, facing > 0);
            const auto* lamp_transform = &pose->object_to_world;
            if (lamps.count != 0 && animation_detail >= 2) {
                // 4367f0 selects dynamics[1], the HIGH body300 node, while
                // MEDIUM lamps use dynamics[0], the physical car root.
                const auto body = std::ranges::find_if(pass->model.nodes, [](const auto& node) {
                    return (node.id_flags & 0xffffffu) == 300;
                });
                if (body == pass->model.nodes.end()) {
                    throw std::runtime_error("High-detail car lamps require the source body node");
                }
                const auto& prepared = impl_->prepare_model(*pass, pose->object_to_world, pose, nullptr, animation_detail, kind == Kind::player && camera.view == 4);
                lamp_transform = &prepared.matrices[std::size_t(body - pass->model.nodes.begin())];
            }
            if (lamps.count != 0 && !pass->lamp_lenses.empty()) {
                const auto& prepared = impl_->prepare_model(*pass, pose->object_to_world, pose, nullptr, animation_detail, kind == Kind::player && camera.view == 4);
                for (const auto& lens : pass->lamp_lenses) {
                    const auto& node = pass->model.nodes[lens.node];
                    if (lens.front != (facing > 0) || pose->lights.broken[lens.broken_index] || prepared.hidden[lens.node]
                        || node.variants.at(car_damage_variant(node.id_flags, pose->damage_levels)) != node.variants.front()) {
                        continue;
                    }
                    const auto slot =
                        pass->livery.texture_slot(lens.node_id_flags, lens.texture, kind == Kind::rival ? binding.second_driver : pose->second_driver, pose->remap_livery);
                    if (slot < 0 || !pass->lamp_emission_textures[lens.front][std::size_t(slot)]) {
                        continue;
                    }
                    const auto brightness = lens.front ? 255u : pose->lights.brake ? 255u : 90u;
                    const auto color = 0xff000000u | brightness * 0x10101u;
                    std::array<LitVertex, 3> triangle;
                    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                        const auto& source = lens.vertices[vertex];
                        triangle[vertex] = transformed({source.position, color, source.u, source.v}, prepared.matrices[lens.node]);
                    }
                    append_world_triangle(result,
                        {.texture = pass->lamp_emission_textures[lens.front][std::size_t(slot)].get(),
                            .depth = true,
                            .blend = true,
                            .depth_write = false,
                            .screen = true,
                            .linear = true},
                        triangle, projection);
                }
            } else {
                for (std::size_t index = 0; index < lamps.count; ++index) {
                    std::array<LitVertex, 3> triangle;
                    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                        triangle[vertex] = transformed({lamps.triangles[index].positions[vertex], lamps.triangles[index].color}, *lamp_transform);
                    }
                    append_world_triangle(result, {.depth = true}, triangle, projection);
                }
            }
            const auto glow = car_light_glow_frame(
                car_id, impl_->resources.setup.weather, pose->lights, pose->object_to_world, *lamp_transform, position, pose->light_ground ? &*pose->light_ground : nullptr);
            if (impl_->car_glow_textures[glow.front]) {
                lamp_glows[glow.front].insert(lamp_glows[glow.front].end(), glow.sprites.begin(), glow.sprites.begin() + std::ptrdiff_t(glow.count));
                for (std::size_t index = 0; index < glow.ground_count && lamp_ground.size() < 32; ++index) {
                    lamp_ground.emplace_back(glow.ground_sprites[index], glow.front);
                }
            }
        }
    }
    impl_->draw_damage_parts(result, damage_parts, camera, lighting, car);
    const auto source_camera = weather_camera(camera.world_to_view);
    for (const auto& [sprite, front] : lamp_ground) {
        // 442530 Flags1 uses the same camera axes/UVs as the visible halo,
        // with a displaced center, opaque alpha and grey RGB. Its ALWAYS
        // comparison keeps depth writes, and has no497180 near-car rejection.
        const auto vertices = surface_effect_sprite_vertices(sprite, 6, source_camera);
        const gpu::State state{.texture = impl_->car_glow_textures[front].get(), .depth = true, .blend = true, .key = true, .screen = true, .clamp = true, .depth_always = true};
        for (const auto& indices : {std::array{0u, 3u, 2u}, std::array{0u, 1u, 3u}}) {
            append_world_triangle(result, state, {vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]}, projection);
        }
    }
    std::size_t queued = 0;
    for (std::size_t front = 0; front < lamp_glows.size(); ++front) {
        queued = append_surface_effect_sprites(result, camera, impl_->car_glow_textures[front].get(), lamp_glows[front], 6, queued);
    }
    if (weather) {
        if (!impl_->weather) {
            throw std::runtime_error("Weather frame requires its original scene textures");
        }
        impl_->weather->append_sprites(result, camera, *weather);
    }
    queued += weather ? weather->sprites.size() : 0;
    if (surface_effects) {
        if (!impl_->surface_effects) {
            throw std::runtime_error("Surface effect frame requires its original scene textures");
        }
        queued = impl_->surface_effects->append_batch(result, camera, surface_effects->batches[0], queued);
    }
    if (engine_effects) {
        if (!impl_->engine_effects) {
            throw std::runtime_error("Engine effect frame requires its original BANG2 texture");
        }
        queued = impl_->engine_effects->append(result, camera, *engine_effects, queued);
    }
    if (surface_effects) {
        for (std::size_t batch = 1; batch < surface_effects->batches.size(); ++batch) {
            queued = impl_->surface_effects->append_batch(result, camera, surface_effects->batches[batch], queued);
        }
    }
    // 446f20 submits the shared sign/impact fragments after scene geometry;4992b0 emits both
    // windings explicitly. Their weather-scaled colors bypass model lighting.
    for (const auto& fragment : fragments) {
        std::array<LitVertex, 3> triangle;
        for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
            std::array<float, 3> point;
            std::ranges::transform(fragment.vertices[corner], point.begin(), [](auto value) {
                return float(value) / 65536.f;
            });
            triangle[corner] = {point, fragment.color};
        }
        append_world_triangle(result, {.depth = true}, triangle, projection);
        std::swap(triangle[0], triangle[1]);
        append_world_triangle(result, {.depth = true}, triangle, projection);
    }
    if (weather) {
        impl_->weather->append_lines(result, camera, *weather);
    }
    append_contact_sparks(result, sparks, camera);
    // 489069 defers the local cockpit root until precipitation/sparks.
    if (camera.view == 1 && impl_->cockpit) {
        const auto cockpit = car.cockpit ? impl_->cockpit->frame(car.object_to_world, *car.cockpit)
                                         : impl_->cockpit->frame(car.object_to_world, camera.cockpit_variant, camera.large_hands, presentation_alpha);
        // 42de80 appends the wiper parent, dashboard, gloves, then wheel to
        // the physical root; 48d367 traverses that sibling order.
        for (const auto index : {3u, 2u, 0u, 1u, 4u}) {
            if (cockpit.visible[index] && impl_->cockpit_models[index]) {
                impl_->draw_model(result, *impl_->cockpit_models[index], cockpit.transforms[index], camera, lighting);
            }
        }
    }
    if (weather) {
        impl_->weather->append_lightning(result, camera, *weather);
    }
    if (impl_->sun) {
        impl_->sun->append(result, camera, impl_->resources.setup.selection.track_id);
    }
}

} // namespace toca::scene
