#include "scene/terrain_view.hpp"

#include "scene/mega_ground_edges.hpp"
#include "scene/mega_ground_transitions.hpp"
#include "scene/mega_ground_uv.hpp"
#include "scene/model_textures.hpp"
#include "scene/render_geometry.hpp"
#include "scene/terrain_lighting_cache.hpp"

#include <algorithm>

namespace toca::scene {
using detail::scene_textures;
using detail::validate;
using detail::WorldChunk;
using detail::WorldChunkBuilder;

struct TerrainView::Impl {
    struct TextureSlot {
        SDL_GPUTexture* texture;
        bool mipmapped, palette;
    };

    const Terrain& terrain;
    gpu::Renderer& renderer;
    TerrainLightingCache lighting_cache{prepare_terrain_colors_into};
    std::vector<WorldChunk> geometry;
    std::shared_ptr<ModelTextures> model_textures;
    std::vector<TextureSlot> slots;
    std::vector<std::optional<MegaGroundUvMapping>> uv_mappings;

    struct FaceUv {
        std::array<std::array<float, 2>, 3> corners;
        bool repeat;
    };

    std::vector<std::vector<std::optional<FaceUv>>> neutral_uvs;
    std::optional<std::size_t> grass_slot, gravel_slot;
    std::vector<std::vector<std::optional<MegaGroundTransition>>> transitions;
    MegaGroundEdges ground_edges;
    MegaGroundDirectEdges direct_edges;

    Impl(gpu::Renderer& renderer, const Terrain& source, bool mipmaps, std::optional<std::uint32_t> mega_track_id, std::shared_ptr<ModelTextures> textures)
        : terrain(source), renderer(renderer), geometry(source.chunks.size()), model_textures(scene_textures(renderer, std::move(textures))),
          ground_edges(mipmaps && mega_track_id ? prepare_mega_ground_edges(source, *mega_track_id) : MegaGroundEdges{}),
          direct_edges(mipmaps && mega_track_id ? prepare_mega_ground_direct_edges(source, *mega_track_id) : MegaGroundDirectEdges{}) {
        if (!direct_edges.normals.empty()) {
            if (ground_edges.normals.empty()) {
                ground_edges.normals = std::move(direct_edges.normals);
            } else {
                for (std::size_t chunk = 0; chunk < direct_edges.normals.size(); ++chunk) {
                    for (std::size_t vertex = 0; vertex < direct_edges.normals[chunk].size(); ++vertex) {
                        if (direct_edges.normals[chunk][vertex]) {
                            ground_edges.normals[chunk][vertex] = direct_edges.normals[chunk][vertex];
                        }
                    }
                }
            }
        }

        const auto transition_profile = mega_track_id ? mega_ground_transition_profile(*mega_track_id) : std::nullopt;
        const auto direct_profile = mega_track_id ? mega_ground_direct_profile(*mega_track_id) : std::nullopt;
        const auto grass_name = transition_profile ? transition_profile->grass : direct_profile ? direct_profile->grass : std::string_view{};
        const auto gravel_name = transition_profile ? transition_profile->gravel : direct_profile ? direct_profile->gravel : std::string_view{};
        for (std::size_t index = 0; index < terrain.textures.size(); ++index) {
            const auto& texture = terrain.textures[index];
            uv_mappings.push_back(mega_ground_uv_mapping(mipmaps && mega_track_id.has_value(), mega_track_id.value_or(0), texture.name));
            if (mipmaps && (transition_profile || direct_profile) && texture.encoding == ImageEncoding::webp) {
                if (texture.name == grass_name) {
                    grass_slot = index;
                } else if (texture.name == gravel_name) {
                    gravel_slot = index;
                }
            }
            const auto uploaded = model_textures->get(texture, {}, mipmaps);
            slots.push_back({uploaded.texture.get(), uploaded.mip_levels > 1, uploaded.palette});
        }
        if (std::ranges::any_of(uv_mappings, [](const auto& mapping) {
                return mapping.has_value();
            })) {
            neutral_uvs.resize(terrain.chunks.size());
            for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
                const auto& chunk = terrain.chunks[chunk_index];
                auto& prepared = neutral_uvs[chunk_index];
                prepared.resize(chunk.mesh.faces.size());
                for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
                    const auto& face = chunk.mesh.faces[face_index];
                    if (face.texture < 0 || !uv_mappings.at(std::size_t(face.texture))) {
                        continue;
                    }
                    const auto& mapping = *uv_mappings.at(std::size_t(face.texture));
                    std::array<std::array<float, 3>, 3> positions;
                    for (std::size_t corner = 0; corner < positions.size(); ++corner) {
                        positions[corner] = chunk.mesh.vertices.at(face.indices[corner]).position;
                    }
                    if (!mapping.supports_triangle(positions)) {
                        continue;
                    }
                    auto& remap = prepared[face_index].emplace();
                    remap.repeat = mapping.repeat;
                    for (std::size_t corner = 0; corner < remap.corners.size(); ++corner) {
                        remap.corners[corner] = mapping.coordinates(positions[corner]);
                    }
                }
            }
        }
        if (transition_profile && grass_slot && gravel_slot && uv_mappings[*grass_slot] && uv_mappings[*gravel_slot]) {
            transitions.resize(terrain.chunks.size());
            for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
                const auto& chunk = terrain.chunks[chunk_index];
                auto& prepared = transitions[chunk_index];
                prepared.resize(chunk.mesh.faces.size());
                for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
                    const auto& face = chunk.mesh.faces[face_index];
                    if (face.texture < 0 || !is_mega_ground_transition(true, *mega_track_id, terrain.textures.at(std::size_t(face.texture)))) {
                        continue;
                    }
                    std::array<ModelVertex, 3> source;
                    std::array<std::array<float, 3>, 3> positions;
                    for (std::size_t corner = 0; corner < source.size(); ++corner) {
                        source[corner] = chunk.mesh.vertices.at(face.indices[corner]);
                        positions[corner] = source[corner].position;
                    }
                    if (!uv_mappings[*grass_slot]->supports_triangle(positions)) {
                        continue;
                    }
                    const auto refine_edges = !ground_edges.faces.empty() && ground_edges.faces[chunk_index][face_index];
                    const auto caps = ground_edges.caps.empty() ? std::span<const MegaGroundTransitionCap>{}
                                                                : std::span<const MegaGroundTransitionCap>(ground_edges.caps[chunk_index][face_index]);
                    prepared[face_index] = prepare_mega_ground_transition(*mega_track_id, source, refine_edges ? transition_profile->feather_half_width : .05f, caps);
                }
            }
        }
    }
};

TerrainView::TerrainView(gpu::Renderer& renderer, const Terrain& terrain, bool mipmaps, std::optional<std::uint32_t> mega_track_id, std::shared_ptr<ModelTextures> textures)
    : impl_(std::make_unique<Impl>(renderer, terrain, mipmaps, mega_track_id, std::move(textures))) {}

TerrainView::~TerrainView() = default;

gpu::Frame TerrainView::frame(const CameraInputs& camera, const Lighting& lighting, std::optional<std::span<const std::uint32_t>> chunks) const {
    gpu::Frame result;
    append(result, camera, lighting, chunks);
    return result;
}

void TerrainView::append(gpu::Frame& result, const CameraInputs& camera, const Lighting& lighting, std::optional<std::span<const std::uint32_t>> chunks) const {
    validate(camera);
    const auto count = chunks ? chunks->size() : impl_->terrain.chunks.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto chunk_index = chunks ? (*chunks)[index] : index;
        const auto& chunk = impl_->terrain.chunks.at(chunk_index);
        impl_->geometry.at(chunk_index).append(result, impl_->renderer, camera, lighting, [&](WorldChunkBuilder& built) {
            const auto& colors =
                impl_->lighting_cache.colors(chunk, lighting, impl_->ground_edges.normals.empty() ? TerrainLightingNormals{} : impl_->ground_edges.normals.at(chunk_index));
            for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
                const auto& face = chunk.mesh.faces[face_index];
                const auto* transition = impl_->transitions.empty() ? nullptr : &impl_->transitions.at(chunk_index).at(face_index);
                const auto append_layer = [&](const MegaGroundTransitionTriangle& source, std::size_t material, bool blend, bool refined) {
                    const std::array source_colors{colors.at(face.indices[0]), colors.at(face.indices[1]), colors.at(face.indices[2])};
                    std::array<LitVertex, 3> triangle;
                    const auto& mapping = impl_->uv_mappings.at(material);
                    for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                        const auto& vertex = source[corner];
                        std::array<float, 2> uv{};
                        if (mapping) {
                            uv = mapping->coordinates(vertex.position);
                        } else {
                            for (std::size_t original = 0; original < face.indices.size(); ++original) {
                                const auto& authored = chunk.mesh.vertices.at(face.indices[original]);
                                uv[0] += vertex.source_weights[original] * authored.u;
                                uv[1] += vertex.source_weights[original] * authored.v;
                            }
                        }
                        triangle[corner] = {vertex.position, mega_ground_transition_color(vertex, source_colors), uv[0], uv[1]};
                    }
                    const auto& slot = impl_->slots.at(material);
                    built.triangle({.texture = slot.texture,
                                       .depth = true,
                                       .blend = blend,
                                       .depth_write = !blend,
                                       .linear = true,
                                       .clamp = !mapping || !mapping->repeat,
                                       .depth_bias = blend,
                                       .mipmapped = slot.mipmapped,
                                       .anisotropy = slot.mipmapped && (!blend || refined) ? gpu::Anisotropy::x2 : gpu::Anisotropy::off,
                                       .palette = slot.palette},
                        triangle);
                };
                if (transition && *transition) {
                    const auto refined = !impl_->ground_edges.faces.empty() && impl_->ground_edges.faces[chunk_index][face_index];
                    append_layer((*transition)->grass, *impl_->grass_slot, false, refined);
                    for (const auto& gravel : (*transition)->gravel) {
                        append_layer(gravel, *impl_->gravel_slot, true, refined);
                    }
                    continue;
                }
                std::array<LitVertex, 3> triangle;
                const auto* uv_face = impl_->neutral_uvs.empty() ? nullptr : &impl_->neutral_uvs.at(chunk_index).at(face_index);
                for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                    const auto index = face.indices[corner];
                    const auto& vertex = chunk.mesh.vertices.at(index);
                    triangle[corner] = {vertex.position, colors.at(index), vertex.u, vertex.v};
                    if (uv_face && *uv_face) {
                        triangle[corner].u = (*uv_face)->corners[corner][0];
                        triangle[corner].v = (*uv_face)->corners[corner][1];
                    }
                }
                const auto* slot = face.texture < 0 ? nullptr : &impl_->slots.at(std::size_t(face.texture));
                auto* texture = slot ? slot->texture : nullptr;
                // 489069 selects D3DTFG/D3DTFN_LINEAR for ground, marks and
                // shadows, then restores POINT before sector objects and cars.
                built.triangle({.texture = texture,
                                   .depth = true,
                                   .key = texture != nullptr,
                                   .linear = true,
                                   // Source486fbf retains CLAMP for original materials.
                                   // Mega world-mapped neutral grain repeats instead.
                                   .clamp = slot
                                            && (impl_->terrain.textures.at(std::size_t(face.texture)).encoding == ImageEncoding::bmp
                                                || (slot->mipmapped && !(uv_face && *uv_face && (*uv_face)->repeat))),
                                   .alpha_cutoff = texture && impl_->terrain.textures.at(std::size_t(face.texture)).encoding == ImageEncoding::webp ? .5f : 0.f,
                                   .mipmapped = slot && slot->mipmapped,
                                   .anisotropy = slot && slot->mipmapped ? gpu::Anisotropy::x2 : gpu::Anisotropy::off,
                                   .palette = slot && slot->palette},
                    triangle);
                if (impl_->gravel_slot && impl_->uv_mappings[*impl_->gravel_slot] && !impl_->direct_edges.gravel.empty()) {
                    for (const auto& gravel : impl_->direct_edges.gravel.at(chunk_index).at(face_index)) {
                        append_layer(gravel, *impl_->gravel_slot, true, true);
                    }
                }
                if (!impl_->ground_edges.bridge_indices.empty()) {
                    for (const auto bridge_index : impl_->ground_edges.bridge_indices[chunk_index][face_index]) {
                        const auto& bridge = impl_->ground_edges.bridges[bridge_index];
                        const auto material = std::size_t(bridge.texture);
                        for (const auto& triangle : bridge.triangles) {
                            append_layer(triangle, material, false, true);
                        }
                    }
                }
            }
        });
    }
}

} // namespace toca::scene
