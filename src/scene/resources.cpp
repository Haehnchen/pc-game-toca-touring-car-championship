#include "scene/resources.hpp"

#include "assets/store.hpp"
#include "core/contracts.hpp"
#include "core/crc32.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/cm3d.hpp"
#include "formats/glb.hpp"
#include "formats/model_validation.hpp"
#include "formats/static_model.hpp"
#include "image/decode.hpp"
#include "scene/catalog.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace toca::scene {
namespace {
class Source {
    std::span<const std::uint8_t> bytes_;

public:
    explicit Source(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint16_t u16(std::size_t offset) const {
        if (offset > bytes_.size() || bytes_.size() - offset < 2) {
            throw std::runtime_error("Scene: truncated original field");
        }
        return std::uint16_t(bytes_[offset]) | (std::uint16_t(bytes_[offset + 1]) << 8);
    }

    std::uint32_t u32(std::size_t offset) const {
        return u16(offset) | (std::uint32_t(u16(offset + 2)) << 16);
    }

    std::int32_t fixed(std::size_t offset) const {
        return std::bit_cast<std::int32_t>(u32(offset));
    }
};

BfpMember read(BfpCursor& cursor) {
    auto member = cursor.read();
    if (!member) {
        throw std::runtime_error("Scene archive ended inside the original resource sequence");
    }
    return *member;
}

void skip(BfpCursor& cursor) {
    if (!cursor.skip()) {
        throw std::runtime_error("Scene archive ended before the selected resource");
    }
}

std::vector<std::uint8_t> copy(const BfpMember& member) {
    const auto bytes = member.bytes();
    return {bytes.begin(), bytes.end()};
}

template <class Decode> auto decode(const BfpMember& member, std::string_view archive, Decode consumer) {
    try {
        return consumer(member.bytes());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::string(archive) + " member " + std::to_string(member.entry().index) + ": " + error.what());
    }
}

bool incomplete_horizon_palette(const ModelTexture& texture) {
    const Source source(texture.bmp);
    return source.u16(28) == 8 && source.u32(46) == 255;
}

void load_horizon_palette(HorizonActorResources& resources, const AssetStore& store) {
    const auto incomplete = [](const Model& model) {
        return std::ranges::any_of(model.textures, incomplete_horizon_palette);
    };
    if (!incomplete(resources.moving) && !incomplete(resources.rotorcraft)) {
        return;
    }
    const auto donor_texture = [](const Model& model) -> const ModelTexture* {
        const auto found = std::ranges::find_if(model.textures, [](const auto& texture) {
            return texture.name == "PSP1";
        });
        return found == model.textures.end() ? nullptr : &*found;
    };
    // Donington's same HOR member includes the complete common palette in
    // PSP1. Other circuits use that original donor only when needed; no
    // generated colors or original asset edits enter the renderer.
    const ModelTexture* donor = donor_texture(resources.moving);
    Model donor_model;
    if (!donor) {
        const auto archive = store.open_archive("data/donlhor.bfp");
        donor_model = decode(archive.member(0), "data/donlhor.bfp", parse_cm3d);
        donor = donor_texture(donor_model);
    }
    if (!donor) {
        throw std::runtime_error("Scene: original Donington PSP1 palette donor is missing");
    }
    const Source source(donor->bmp);
    const auto count = source.u32(46) == 0 ? 256u : source.u32(46);
    if (source.u32(14) != 40 || source.u16(28) != 8 || count != 256 || source.u32(10) != 54 + 256 * 4 || donor->bmp.size() < 54 + 256 * 4) {
        throw std::runtime_error("Scene: original PSP1 donor requires a complete 256-entry BMP palette");
    }
    resources.texture_palette.assign(donor->bmp.begin() + 54, donor->bmp.begin() + 54 + 256 * 4);
    for (const auto* model : {&resources.moving, &resources.rotorcraft}) {
        for (const auto& texture : model->textures) {
            if (!incomplete_horizon_palette(texture)) {
                continue;
            }
            const Source partial(texture.bmp);
            if (partial.u32(14) != 40 || partial.u32(10) != 54 + 255 * 4 || texture.bmp.size() < 54 + 255 * 4
                || !std::equal(resources.texture_palette.begin(), resources.texture_palette.begin() + 255 * 4, texture.bmp.begin() + 54)) {
                throw std::runtime_error("Scene: incomplete HOR palette does not match the original complete palette");
            }
        }
    }
}

std::uint32_t texture_checksum(const ModelTexture& texture, std::span<const std::uint8_t> palette) {
    if (palette.empty() || !incomplete_horizon_palette(texture)) {
        return crc32(texture.bmp);
    }
    // Keep the canonical resource bytes intact; only the cache key uses this
    // completed image, so missing overrides retain the normal decode path.
    const auto restored = restore_bmp_palette(texture.bmp, palette, "Mega horizon texture requires the matching complete original palette");
    return crc32(restored);
}

void validate_scene_model(const Model& original, const Model& replacement) {
    const auto require = [](bool valid) {
        if (!valid) {
            throw std::runtime_error("Mega scenery must retain original nodes, transforms, IDs, variants, mesh names and texture prefix");
        }
    };
    require(model_structure_matches(original, replacement, ModelTextureSlots::preserve_prefix) && model_node_transforms_match(original, replacement));
    for (const auto& texture : replacement.textures) {
        (void)decode_model_texture(texture);
    }
}

Terrain bake_scene_model(const Model& model, const Terrain& original) {
    Terrain result;
    result.textures = model.textures;
    result.sector_centers = original.sector_centers;
    result.chunks.reserve(original.chunks.size());
    // Canonical static registration supplies node/variant identities, sector
    // origins and allocation order. Replace their geometry in those same slots.
    // Like494a40's static branch, children use the last root's matrix directly.
    std::vector<std::size_t> roots;
    std::size_t root = 0;
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        if (model.nodes[index].parent == -1) {
            root = index;
        }
        roots.push_back(root);
    }
    for (const auto& source : original.chunks) {
        TerrainChunk chunk;
        chunk.node_index = source.node_index;
        chunk.variant_index = source.variant_index;
        chunk.origin_xz = source.origin_xz;
        const auto& node = model.nodes.at(chunk.node_index);
        chunk.mesh = model.meshes.at(node.variants.at(chunk.variant_index));
        const auto& matrix = model.nodes.at(roots.at(chunk.node_index)).transform;
        for (auto& vertex : chunk.mesh.vertices) {
            const auto position = vertex.position, normal = vertex.normal;
            for (std::size_t row = 0; row < 3; ++row) {
                vertex.position[row] = matrix[12 + row];
                vertex.normal[row] = 0;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    vertex.position[row] += matrix[axis * 4 + row] * position[axis];
                    vertex.normal[row] += matrix[axis * 4 + row] * normal[axis];
                }
            }
            // GLB UVs already use runtime orientation; static scenery keeps
            // the original texture-edge inset without applying it twice.
            vertex.u = std::clamp(vertex.u, .02f, .99f);
            vertex.v = std::clamp(vertex.v, .02f, .99f);
        }
        chunk.face_flags.assign(chunk.mesh.faces.size(), 0);
        result.chunks.push_back(std::move(chunk));
    }
    return result;
}
} // namespace

void apply_mega_scene_resources(SceneResources& resources, const std::filesystem::path& mega_models) {
    if (resources.setup.selection.detail != graphics_mega_detail || mega_models.empty()) {
        return;
    }
    const auto directory = mega_models / "scene";
    std::map<std::uint32_t, std::optional<std::vector<std::uint8_t>>> textures;
    std::size_t replaced_textures = 0, replaced_models = 0;
    const auto replace_textures = [&](auto& model, std::span<const std::uint8_t> palette = {}) {
        for (auto& texture : model.textures) {
            if (texture.encoding != ImageEncoding::bmp) {
                continue;
            }
            const auto checksum = texture_checksum(texture, palette);
            auto [entry, inserted] = textures.try_emplace(checksum);
            if (inserted) {
                std::array<char, 9> key{};
                std::snprintf(key.data(), key.size(), "%08x", checksum);
                const auto path = directory / (std::string(key.data()) + ".webp");
                try {
                    if (std::filesystem::exists(path)) {
                        std::ifstream input(path, std::ios::binary | std::ios::ate);
                        const auto size = input.tellg();
                        if (!input || size <= 0 || size > 16 * 1024 * 1024) {
                            throw std::runtime_error("invalid bounded WebP file size");
                        }
                        ModelTexture replacement{texture.name, std::vector<std::uint8_t>(std::size_t(size)), ImageEncoding::webp};
                        input.seekg(0);
                        if (!input.read(reinterpret_cast<char*>(replacement.bmp.data()), size)) {
                            throw std::runtime_error("cannot read complete WebP texture");
                        }
                        (void)decode_model_texture(replacement);
                        entry->second = std::move(replacement.bmp);
                        ++replaced_textures;
                    }
                } catch (const std::runtime_error& error) {
                    std::clog << "Mega texture unavailable: " << path.string() << ": " << error.what() << "; using original texture\n";
                }
            }
            if (entry->second) {
                texture.bmp = *entry->second;
                texture.encoding = ImageEncoding::webp;
            }
        }
    };
    const auto replace_model = [&](Model& model, Terrain* baked, std::string_view family, std::span<const std::uint8_t> palette = {}) {
        replace_textures(model, palette);
        if (baked) {
            replace_textures(*baked);
        }
        if (model.nodes.empty()) {
            return;
        }
        const auto path = directory / ("track-" + std::to_string(resources.setup.selection.track_id)) / (std::string(family) + ".glb");
        try {
            if (!std::filesystem::exists(path)) {
                return;
            }
            auto replacement = load_glb(path);
            validate_scene_model(model, replacement);
            // Shared texture overrides also replace the corresponding original
            // material slots embedded in a generated family GLB.
            for (std::size_t index = 0; index < model.textures.size(); ++index) {
                if (model.textures[index].encoding == ImageEncoding::webp) {
                    replacement.textures[index] = model.textures[index];
                }
            }
            auto baked_replacement = baked ? std::optional(bake_scene_model(replacement, *baked)) : std::nullopt;
            model = std::move(replacement);
            if (baked) {
                *baked = std::move(*baked_replacement);
            }
            ++replaced_models;
        } catch (const std::runtime_error& error) {
            std::clog << "Mega scenery unavailable: " << path.string() << ": " << error.what() << "; using original geometry\n";
        }
    };
    replace_textures(resources.ground);
    replace_model(resources.trees, &resources.trees_baked, "trees");
    replace_model(resources.fence, &resources.fence_baked, "fence");
    replace_model(resources.objects, &resources.objects_baked, "objects");
    replace_model(resources.cones.model, &resources.cones.baked, "cones");
    replace_model(resources.posts.model, &resources.posts.baked, "posts");
    replace_model(resources.signs.model, &resources.signs.baked, "signs");
    replace_model(resources.flags.model, &resources.flags.baked, "flags");
    replace_model(resources.horizon, nullptr, "horizon");
    replace_textures(resources.sun);
    replace_model(resources.moving_horizon.moving, nullptr, "moving", resources.moving_horizon.texture_palette);
    replace_model(resources.moving_horizon.rotorcraft, nullptr, "rotorcraft", resources.moving_horizon.texture_palette);
    for (auto& model : resources.cockpit.models) {
        replace_textures(model);
    }
    if (replaced_textures || replaced_models) {
        std::clog << "Mega scene: " << replaced_models << " model families, " << replaced_textures << " shared WebP textures\n";
    }
}

std::array<GridSlot, 32> decode_start_grid(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 32 * 20) {
        throw std::runtime_error("Scene: SGRID requires thirty-two original 20-byte records");
    }
    const Source source(bytes);
    std::array<GridSlot, 32> grid;
    for (std::size_t index = 0; index < grid.size(); ++index) {
        auto& slot = grid[index];
        const auto position = index * 20;
        slot.first_xz = {source.fixed(position), source.fixed(position + 4)};
        slot.second_xz = {source.fixed(position + 8), source.fixed(position + 12)};
        slot.heading = std::bit_cast<std::int16_t>(source.u16(position + 16));
        slot.reserved = source.u16(position + 18);
    }
    return grid;
}

std::vector<SceneCamera> decode_scene_cameras(std::span<const std::uint8_t> bytes) {
    const Source source(bytes);
    // CAMERAS.C3D has transform nodes but zero mesh/texture section offsets.
    // Its original consumer 004510bf never treats it as a drawable Model.
    if (bytes.size() < 32 || source.u32(0) != 0x44334d43 || source.u32(4) != 100 || source.u32(8) != 32 || source.u32(20) != 32 || source.u16(14) != 0 || source.u16(16) != 0) {
        throw std::runtime_error("Scene: expected transform-only CM3D camera resource");
    }
    const auto count = source.u16(12);
    if (count > 50) {
        throw std::runtime_error("Scene: camera resource exceeds the original fifty-node capacity");
    }
    std::vector<SceneCamera> result;
    std::size_t position = 32;
    for (std::size_t index = 0; index < count; ++index) {
        const auto size = source.u32(position);
        if (size != 80 || position > bytes.size() || size > bytes.size() - position || bytes[position + 76] != 0) {
            throw std::runtime_error("Scene: invalid transform-only camera node");
        }
        SceneCamera camera;
        for (std::size_t field = 0; field < camera.transform.size(); ++field) {
            camera.transform[field] = source.fixed(position + 8 + field * 4);
        }
        result.push_back(camera);
        position += size;
    }
    if (position != bytes.size()) {
        throw std::runtime_error("Scene: unaccounted camera resource bytes");
    }
    return result;
}

SceneResources load_scene_resources(const AssetStore& store, const SceneSetup& setup, const std::filesystem::path& mega_models) {
    const auto checked = resolve_setup(setup.selection, setup.horizon_index);
    if (checked.weather != setup.weather || checked.horizon_index != setup.horizon_index) {
        throw std::runtime_error("Scene setup does not match its original resolved weather/horizon");
    }
    // 44e717/44e673/44e5cf select the archive family; 44e7bb shares
    // the member sequence and detail switches between these tracks.
    const auto prefix = std::string(track_content(setup.selection.track_id).archive_prefix);
    const auto keep_path = prefix + "keep.bfp", free_path = prefix + "free.bfp", horizon_path = prefix + "hor.bfp";
    SceneResources result;
    result.setup = setup;
    const auto keep = store.open_archive(keep_path);
    auto retained = keep.cursor();
    result.collision_hpc = copy(read(retained));
    result.route_points = copy(read(retained));
    result.pits = decode(read(retained), keep_path, decode_pit_route);
    result.grid = decode(read(retained), keep_path, decode_start_grid);

    const auto free = store.open_archive(free_path);
    auto transient = free.cursor();
    const auto static_model = [&](const BfpMember& member, Model& model, Terrain& baked) {
        auto decoded = decode(member, free_path, parse_static_model);
        model = std::move(decoded.model);
        baked = std::move(decoded.baked);
    };
    if (setup.selection.detail >= graphics_mega_detail - 1) {
        skip(transient);
    } else {
        result.route_points = copy(read(transient));
        result.low_detail_points = true;
    }
    result.ground = decode(read(transient), free_path, parse_terrain);
    // 44e7bb skips Lavaland's empty TREES member instead of decoding it.
    if (setup.selection.track_id == 9) {
        skip(transient);
    } else {
        static_model(read(transient), result.trees, result.trees_baked);
    }
    static_model(read(transient), result.fence, result.fence_baked);
    result.cameras = decode(read(transient), free_path, decode_scene_cameras);
    const auto object_index = setup.selection.detail >= graphics_mega_detail - 1 ? 5u : setup.selection.detail == 0 ? 7u : 6u;
    for (std::uint32_t index = 5; index < object_index; ++index) {
        skip(transient);
    }
    static_model(read(transient), result.objects, result.objects_baked);
    // 44d4b3/59e/668/72f consume the four remaining FREE members.
    for (auto index = object_index + 1; index < 8; ++index) {
        skip(transient);
    }
    const auto prop = [&](PropKind kind) {
        return decode(read(transient), free_path, [&](auto bytes) {
            return decode_props(bytes, kind);
        });
    };
    result.cones = prop(PropKind::cones);
    if (setup.selection.track_id == 9) {
        skip(transient);
        skip(transient);
    } else {
        result.posts = prop(PropKind::posts);
        result.signs = prop(PropKind::signs);
    }
    if (setup.selection.detail == 0) {
        skip(transient);
    } else {
        result.flags = prop(PropKind::flags);
    }
    try {
        result.car = frontend::load_car_models(store, setup.selection.car_id, setup.selection.detail == graphics_mega_detail ? mega_models : std::filesystem::path{});
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("data/allcars.bfp car " + std::to_string(setup.selection.car_id) + ": " + error.what());
    }
    result.cockpit = load_cockpit_resources(store, setup.selection.car_id, setup.selection.detail == graphics_mega_detail ? mega_models : std::filesystem::path{});

    // 498130's alpha-capable path: SHADPC RGB, then SHADAM alpha.
    const auto shadow_images = store.open_archive("data/lowimage.bfp");
    result.car_shadow_color_bmp = copy(shadow_images.member(0));
    result.car_shadow_alpha_bmp = copy(shadow_images.member(1));
    const auto lamp_images = store.open_archive("data/image640.bfp");
    result.car_glow_bmps[0] = copy(lamp_images.member(17));
    result.car_glow_bmps[1] = copy(lamp_images.member(18));

    const auto horizons = store.open_archive(horizon_path);
    auto horizon = horizons.cursor();
    result.horizon_actors = copy(read(horizon));
    result.horizon_auxiliary = copy(read(horizon));
    result.moving_horizon = decode_horizon_actors(result.horizon_actors, result.horizon_auxiliary, setup);
    load_horizon_palette(result.moving_horizon, store);
    for (std::uint32_t index = 0; index < setup.horizon_index; ++index) {
        skip(horizon);
    }
    result.horizon = decode(read(horizon), horizon_path, parse_cm3d);
    result.sun = load_sun_resources(store, setup);
    apply_mega_scene_resources(result, mega_models);
    return result;
}
} // namespace toca::scene
