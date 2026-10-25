#include "scene/championship_finale_view.hpp"

#include "assets/store.hpp"
#include "formats/cm3d.hpp"
#include "formats/glb.hpp"
#include "formats/model_validation.hpp"
#include "image/decode.hpp"
#include "scene/matrix.hpp"
#include "scene/model_geometry.hpp"
#include "scene/model_textures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace toca::scene {
namespace {
using matrix::identity;
using matrix::Matrix;
using matrix::multiply;

CarPose car_pose(const ChampionshipFinaleCarPose& source) {
    CarPose result;
    result.object_to_world = source.object_to_world;
    result.steering_angle = source.steering_angle;
    result.wheel_angles = source.wheel_angles;
    result.visual_angles = source.visual_angles;
    return result;
}

std::uint8_t upper(char value) {
    return std::uint8_t(value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value);
}

int text_width(const frontend::Font& font, std::string_view text) {
    int width = 0;
    std::uint8_t previous = 0;
    for (char value : text) {
        const auto character = upper(value);
        if (character == ' ' || character == '_' || character == '^') {
            width += font.space_width;
        } else if (const auto* glyph = font.find(character)) {
            width += glyph->width + font.advance - font.pair_spacing(character, previous);
        }
        previous = character;
    }
    return width;
}

// Original game fonts are indexed masks:1 is the face,2/3 its dark edge.
// Their palette indices must not be interpreted through the track palette.
gpu::Texture font_texture(gpu::Renderer& renderer, const frontend::Font& font) {
    Surface rgba(SDL_CreateSurface(font.atlas->w, font.atlas->h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    gpu::check(bool(rgba), "Create finale credit font texture");
    for (int y = 0; y < font.atlas->h; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(font.atlas->pixels) + y * font.atlas->pitch;
        auto* target = static_cast<std::uint8_t*>(rgba->pixels) + y * rgba->pitch;
        for (int x = 0; x < font.atlas->w; ++x) {
            const auto face = source[x] == 1 ? std::uint8_t(255) : std::uint8_t(0);
            target[x * 4] = target[x * 4 + 1] = target[x * 4 + 2] = face;
            target[x * 4 + 3] = source[x] == 0 ? 0 : 255;
        }
    }
    return renderer.upload(rgba.get());
}
} // namespace

ChampionshipFinaleResources load_championship_finale_resources(const AssetStore& store, const std::filesystem::path& mega_models) {
    const auto archive = store.open_archive("data/champfin.bfp");
    ChampionshipFinaleResources result;
    result.pace = parse_cm3d(archive.member(0).bytes());
    result.transporter = parse_cm3d(archive.member(1).bytes());
    if (!mega_models.empty()) {
        const auto replace = [&](Model& model, std::string_view role) {
            const auto path = mega_models / "finale" / (std::string(role) + ".glb");
            try {
                if (!std::filesystem::exists(path)) {
                    return;
                }
                auto replacement = load_glb(path);
                if (!model_structure_matches(model, replacement) || !model_node_transforms_match(model, replacement)
                    || std::ranges::any_of(replacement.meshes, [](const auto& mesh) {
                           return mesh.faces.empty();
                       })) {
                    throw std::runtime_error("Finale replacement must retain original actors, pivots, variants and material slots");
                }
                for (const auto& texture : replacement.textures) {
                    (void)decode_model_texture(texture);
                }
                model = std::move(replacement);
                std::clog << "Mega finale: " << role << ' ' << path.string() << '\n';
            } catch (const std::runtime_error& error) {
                std::clog << "Mega finale unavailable: " << path.string() << ": " << error.what() << "; using original vehicle\n";
            }
        };
        replace(result.pace, "pace-car");
        replace(result.transporter, "transporter");
    }
    for (std::size_t team = 0; team < result.teams.size(); ++team) {
        result.teams[team] = parse_cm3d(archive.member(team + 2).bytes());
    }
    const auto credits = archive.member(10);
    result.credits.assign(credits.bytes().begin(), credits.bytes().end());
    const auto images = store.open_archive("data/image640.bfp");
    for (std::size_t font = 0; font < result.fonts.size(); ++font) {
        result.fonts[font] = frontend::decode_font(images.member(23 + font).bytes());
    }
    return result;
}

Matrix championship_finale_trophy_transform(const ChampionshipFinaleCarPose& transporter, std::uint32_t rank) {
    constexpr std::array<std::array<std::int32_t, 3>, 3> offsets{{{-35127, 34078, 0}, {-112984, 24838, 18677}, {26017, 18022, 15400}}};
    if (rank >= offsets.size()) {
        throw std::runtime_error("Finale transporter attachment requires rank0..2");
    }
    auto local = identity;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        local[12 + axis] = float(offsets[rank][axis]) / 65536.f;
    }
    return multiply(local, transporter.object_to_world);
}

struct ChampionshipFinaleView::Impl {
    const SceneResources& scene;
    const ChampionshipFinaleResources& resources;
    SceneView world;

    struct ModelPass {
        const Model& model;
        std::vector<ModelTextures::Texture> textures;
        std::vector<bool> palette;
        std::vector<ModelGeometry> geometry;

        ModelPass(ModelTextures& uploads, ModelMeshes& meshes, const Model& source) : model(source) {
            for (const auto& texture : source.textures) {
                const auto uploaded = uploads.get(texture);
                textures.push_back(uploaded.texture);
                palette.push_back(uploaded.palette);
            }
            geometry.reserve(source.meshes.size());
            for (const auto& mesh : source.meshes) {
                geometry.push_back(prepare_model_geometry(mesh, meshes));
            }
        }
    } pace, transporter;

    std::vector<ModelPass> teams;
    std::vector<gpu::Texture> fonts;
    bool mega = false;
    mutable ChampionshipFinaleDrawCounts counts;

    Impl(gpu::Renderer& renderer, const SceneResources& source, const ChampionshipFinaleResources& assets, std::uint32_t language, const std::filesystem::path& artwork_directory)
        : scene(source), resources(assets), world(renderer, source), pace(*world.model_textures(), *world.model_meshes(), assets.pace),
          transporter(*world.model_textures(), *world.model_meshes(), assets.transporter), mega(source.setup.selection.detail == graphics_mega_detail) {
        if (source.setup.selection.track_id != 1 || language > 4) {
            throw std::runtime_error("Championship finale requires Silverstone and original language0..4");
        }
        teams.reserve(assets.teams.size());
        for (const auto& model : assets.teams) {
            teams.emplace_back(*world.model_textures(), *world.model_meshes(), model);
        }
        for (std::size_t index = 0; index < assets.fonts.size(); ++index) {
            if (mega) {
                const auto artwork = index == 0 ? frontend::load_big_font_artwork(artwork_directory, {255, 255, 255, 255}, {0, 0, 0, 255})
                                                : frontend::load_small_font_artwork(artwork_directory, {255, 255, 255, 255}, {0, 0, 0, 255});
                fonts.push_back(renderer.upload(artwork.get()));
            } else {
                fonts.push_back(font_texture(renderer, assets.fonts[index]));
            }
        }
    }

    static std::uint32_t append_model(gpu::Frame& frame, const ModelPass& pass, const std::vector<Matrix>& transforms, const CameraInputs& camera, const Lighting& lighting) {
        std::uint32_t triangles = 0;
        const auto& projection = camera.projection;
        const auto view = frame.world_view({camera.world_to_view, {projection.x, projection.y, projection.near_plane, projection.far_plane},
            {projection.depth_scale, projection.depth_offset, float(projection.width), float(projection.height)}});
        for (std::size_t node_index = 0; node_index < pass.model.nodes.size(); ++node_index) {
            const auto& node = pass.model.nodes[node_index];
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto& geometry = pass.geometry.at(node.variants.front());
            if (geometry.runs.empty()) {
                continue;
            }
            if (frame.model_instances.size() >= UINT32_MAX) {
                throw std::runtime_error("Finale exceeds GPU instance capacity");
            }
            const auto instance = Uint32(frame.model_instances.size());
            frame.model_instances.push_back({transforms.at(node_index), {lighting.direction[0], lighting.direction[1], lighting.direction[2], lighting.diffuse},
                {lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], 16}});
            for (const auto& run : geometry.runs) {
                const auto texture = run.texture < 0 ? nullptr : pass.textures.at(std::size_t(run.texture)).get();
                frame.draws.push_back({{.texture = texture,
                                           .depth = true,
                                           .key = texture != nullptr,
                                           .clamp = texture && pass.model.textures.at(std::size_t(run.texture)).encoding == ImageEncoding::bmp,
                                           .palette = texture && pass.palette.at(std::size_t(run.texture))},
                    run.first, run.count, 0, {}, false, geometry.mesh, view, instance});
                if (run.count / 6 > UINT32_MAX - triangles) {
                    throw std::runtime_error("Finale exceeds source triangle count capacity");
                }
                triangles += run.count / 6;
            }
        }
        return triangles;
    }

    void append_credits(gpu::Frame& frame, std::span<const ChampionshipFinaleCredit> credits, std::uint32_t width, std::uint32_t height) const {
        //419a50/464f58: centered at0x3333, BIG/LIL rows, returned pixel
        //bottom+3 then sourceQ16 spacing0x51e. Credits remain upright.
        const auto scale = std::min(float(width) / 640, float(height) / 480);
        const auto left = (float(width) - 640 * scale) / 2, top = (float(height) - 480 * scale) / 2;
        std::uint32_t y_q16 = 0x3333;
        for (const auto& line : credits) {
            const auto index = line.heading ? 0u : 1u;
            const auto& font = resources.fonts[index];
            int x = 320 - text_width(font, line.text) / 2;
            const auto y = std::uint32_t((std::uint64_t(y_q16) * 480) >> 16);
            std::uint8_t previous = 0;
            for (char value : line.text) {
                const auto character = upper(value);
                if (character == ' ' || character == '_' || character == '^') {
                    x += font.space_width;
                } else if (const auto* glyph = font.find(character)) {
                    x -= font.pair_spacing(character, previous);
                    if (frame.vertices.size() + 4 > 65536) {
                        throw std::runtime_error("Championship credits exceed native frame capacity");
                    }
                    const auto first = frame.vertices.size();
                    const bool smooth = mega;
                    frame.quad({.texture = fonts[index].get(), .blend = smooth, .key = !smooth, .depth_write = false, .linear = smooth, .clamp = smooth}, left + float(x) * scale,
                        top + float(y) * scale, float(glyph->width) * scale, float(glyph->height) * scale);
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        auto& vertex = frame.vertices[first + corner];
                        vertex.u = (float(glyph->x) + vertex.u * glyph->width) / float(font.atlas->w);
                        vertex.v = (float(glyph->y) + vertex.v * glyph->height) / float(font.atlas->h);
                    }
                    x += glyph->width + font.advance;
                    ++counts.credit_glyphs;
                }
                previous = character;
            }
            y_q16 = (((y + std::uint32_t(font.line_height) + 3) << 16) / 480) + 0x51e;
        }
    }
};

ChampionshipFinaleView::ChampionshipFinaleView(
    gpu::Renderer& renderer, const SceneResources& scene, const ChampionshipFinaleResources& resources, std::uint32_t language, const std::filesystem::path& artwork_directory)
    : impl_(std::make_unique<Impl>(renderer, scene, resources, language, artwork_directory)) {}

ChampionshipFinaleView::~ChampionshipFinaleView() = default;

const ChampionshipFinaleDrawCounts& ChampionshipFinaleView::draw_counts() const {
    return impl_->counts;
}

gpu::Frame ChampionshipFinaleView::frame(const ChampionshipFinaleFrame& state, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors, std::uint32_t width,
    std::uint32_t height, bool mirror, bool upside) const {
    gpu::Frame result;
    frame_into(result, state, flag_variants, actors, width, height, mirror, upside);
    return result;
}

void ChampionshipFinaleView::frame_into(gpu::Frame& frame, const ChampionshipFinaleFrame& state, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors,
    std::uint32_t width, std::uint32_t height, bool mirror, bool upside) const {
    if (width == 0 || height == 0) {
        throw std::runtime_error("Championship finale requires a drawable viewport");
    }
    auto& view = *impl_;
    view.counts = {};
    CameraInputs camera{state.camera_world_to_view, default_projection(view.scene.setup.selection.detail, width, height)};
    camera.registered_camera_position = state.camera_position_q16;
    camera.hide_player = true;
    const auto lighting = scene_lighting(view.scene.setup);
    const auto pace_pose = car_pose(state.pace), transporter_pose = car_pose(state.transporter);
    view.world.frame_into(frame, camera, lighting, transporter_pose, flag_variants, actors);
    view.counts.world_vertices = frame.world_vertex_count();
    view.counts.model_triangles[0] = Impl::append_model(frame, view.pace, car_world_transforms(view.resources.pace, pace_pose, 3), camera, lighting);
    view.counts.model_triangles[1] = Impl::append_model(frame, view.transporter, car_world_transforms(view.resources.transporter, transporter_pose, 3), camera, lighting);
    for (std::uint32_t rank = 0; rank < state.teams.size(); ++rank) {
        if (state.teams[rank] >= view.teams.size()) {
            throw std::runtime_error("Championship finale team is outside0..7");
        }
        const auto& pass = view.teams[state.teams[rank]];
        view.counts.model_triangles[2 + rank] =
            Impl::append_model(frame, pass, model_world_transforms(pass.model, championship_finale_trophy_transform(state.transporter, rank)), camera, lighting);
    }
    frame.reflect_world(float(width), float(height), mirror, upside);
    const auto overlay_start = frame.draws.size();
    view.append_credits(frame, state.credits, width, height);
    frame.mark_overlay(overlay_start);
}
} // namespace toca::scene
