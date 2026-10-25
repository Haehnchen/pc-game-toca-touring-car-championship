#pragma once
#include "platform/display.hpp"
#include "render/gpu_lifetime.hpp"
#include "render/pass_bindings.hpp"
#include "render/source_projection.hpp"
#include "render/world_geometry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toca::gpu {
constexpr std::uint32_t background = 0xff101820;

inline void check(bool ok, const char* operation) {
    if (!ok) {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}

using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

// D3DTLVERTEX layout at TOCA 1.1a's transformed draw site 0x0048e837.
struct Vertex {
    float x, y, z, rhw;
    std::uint32_t diffuse, specular;
    float u, v;
};

static_assert(sizeof(Vertex) == 32);

enum class Anisotropy : Uint8 {
    off,
    x2,
    x4
};

// The second profile reproduces the observed reference wrapper, not game math.
enum class RasterProfile : Uint8 {
    native,
    reference_dgvoodoo263
};

struct State {
    SDL_GPUTexture* texture = nullptr;
    bool depth = false, blend = false, key = false, fog = false, wire = false;
    bool depth_write = true, lines = false, screen = false, linear = false, clamp = false;
    // Pull a coplanar material layer forward by one D16 depth unit.
    bool depth_bias = false;
    float alpha_cutoff = 0;
    bool mipmapped = false;
    // Explicit opaque terrain policy; ordinary draws retain their sampler.
    Anisotropy anisotropy = Anisotropy::off;
    // Original ground lamp images use ZFUNC=ALWAYS while retaining writes.
    bool depth_always = false;
    // Generated backing must yield to original artwork in the same depth bin.
    bool depth_less = false;
    // P8 uploads store the source index-zero key in alpha. Palette textures
    // have no material alpha; their opacity comes from the vertex color.
    bool palette = false;
    // Explicit source format of reconstructed RGBA uploads (personal names).
    // Used only by the opt-in reference rasterizer; ordinary RGBA stays RGBA.
    bool reference_indexed = false;

    bool operator==(const State&) const = default;
};

struct Viewport {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Viewport&) const = default;
};

struct Draw {
    State state;
    Uint32 first, count;
    Sint32 vertex_offset = 0;
    std::optional<Viewport> viewport{};
    bool overlay = false;
    std::shared_ptr<const WorldMesh> mesh{};
    Uint32 world_view = 0;
    Uint32 model_instance = UINT32_MAX;
    Uint32 instance_count = 1;
    bool dynamic_world = false;
};

enum class ColorLoad : Uint8 {
    clear,
    preserve
};

struct Frame {
    SDL_FColor clear_color{16.f / 255, 24.f / 255, 32.f / 255, 1};
    ColorLoad color_load = ColorLoad::clear;
    std::vector<Vertex> vertices;
    std::vector<Uint16> indices;
    std::vector<Draw> draws;
    std::vector<WorldProjection> world_views;
    std::vector<ModelInstance> model_instances;
    std::vector<WorldVertex> dynamic_world_vertices;
    std::vector<WorldVertex> dynamic_world_lines;
    // Optional authored 640x480 artwork reflected into the centered UI's side areas.
    SDL_GPUTexture* side_backdrop = nullptr;

    struct CoverImage {
        SDL_GPUTexture* texture;
        std::uint32_t width, height;
    };

    // Replace the UI with this centered, uniformly scaled image whenever the
    // acquired drawable differs from the original 4:3 aspect ratio.
    std::optional<CoverImage> non_4_3_image;
    // Uniformly cover the acquired drawable behind the centered UI.
    std::optional<CoverImage> background_image;
    // Scene resource policy: Mega requests2x world sampling; UI stays1x.
    bool multisample = false;

    void clear() {
        clear_color = {16.f / 255, 24.f / 255, 32.f / 255, 1};
        color_load = ColorLoad::clear;
        vertices.clear();
        indices.clear();
        draws.clear();
        world_views.clear();
        model_instances.clear();
        dynamic_world_vertices.clear();
        dynamic_world_lines.clear();
        side_backdrop = nullptr;
        non_4_3_image.reset();
        background_image.reset();
        multisample = false;
    }

    void reflect_world(float width, float height, bool horizontal, bool vertical) {
        for (auto& vertex : vertices) {
            if (horizontal) {
                vertex.x = width - vertex.x;
            }
            if (vertical) {
                vertex.y = height - vertex.y;
            }
        }
        for (auto& view : world_views) {
            if (horizontal) {
                view.reflection[0] = 1 - view.reflection[0];
                view.reflection[2] = width - view.depth_size[2] - view.reflection[2];
            }
            if (vertical) {
                view.reflection[1] = 1 - view.reflection[1];
                view.reflection[3] = height - view.depth_size[3] - view.reflection[3];
            }
        }
    }

    std::size_t world_vertex_count() const {
        std::size_t count = vertices.size();
        for (const auto& draw : draws) {
            if (draw.mesh || draw.dynamic_world) {
                count += std::size_t(draw.count) * draw.instance_count;
            }
        }
        return count;
    }

    Uint32 world_view(const WorldProjection& view) {
        if (world_views.empty() || world_views.back() != view) {
            if (world_views.size() == UINT32_MAX) {
                throw std::runtime_error("Too many world projections");
            }
            world_views.push_back(view);
        }
        return Uint32(world_views.size() - 1);
    }

    static bool finite_world_corner(const WorldVertex& corner) {
        return std::ranges::all_of(corner.position,
                   [](float value) {
                       return std::isfinite(value);
                   })
               && std::isfinite(corner.u) && std::isfinite(corner.v);
    }

    void validate_dynamic_world() const {
        if (dynamic_world_vertices.size() % 3 || dynamic_world_vertices.size() > UINT32_MAX / sizeof(WorldVertex)
            || !std::ranges::all_of(dynamic_world_vertices, finite_world_corner)) {
            throw std::runtime_error("Dynamic world geometry requires finite complete triangles within GPU byte capacity");
        }
        if (dynamic_world_lines.size() % 2 || dynamic_world_lines.size() > UINT32_MAX / sizeof(WorldVertex) || !std::ranges::all_of(dynamic_world_lines, finite_world_corner)) {
            throw std::runtime_error("Dynamic world geometry requires finite complete lines within GPU byte capacity");
        }
    }

    // Prelit world-space effects share the resident shader's original clipping,
    // projection and packed-color arithmetic, without creating a resident mesh.
    void world_triangle(State state, const std::array<WorldVertex, 3>& corners, const WorldProjection& projection) {
        if (state.lines || dynamic_world_vertices.size() % 3 || dynamic_world_vertices.size() > UINT32_MAX / sizeof(WorldVertex) - corners.size()
            || !std::ranges::all_of(corners, finite_world_corner)) {
            throw std::runtime_error("Dynamic world triangle requires finite corners, triangle state and GPU byte capacity");
        }
        const auto view = world_view(projection);
        const auto first = Uint32(dynamic_world_vertices.size() * 2);
        dynamic_world_vertices.insert(dynamic_world_vertices.end(), corners.begin(), corners.end());
        if (!draws.empty()) {
            auto& previous = draws.back();
            if (previous.dynamic_world && !previous.mesh && previous.state == state && previous.world_view == view && !previous.viewport && !previous.overlay
                && previous.vertex_offset == 0 && previous.model_instance == UINT32_MAX && previous.instance_count == 1 && previous.first % 6 == 0 && previous.count % 6 == 0
                && Uint64(previous.first) + previous.count == first) {
                previous.count += 6;
                return;
            }
        }
        Draw draw{state, first, 6};
        draw.world_view = view;
        draw.dynamic_world = true;
        draws.push_back(std::move(draw));
    }

    // Lines retain raw endpoint addressing for the GPU's line-list clipping.
    void world_line(State state, const std::array<WorldVertex, 2>& endpoints, const WorldProjection& projection) {
        if (!state.lines || dynamic_world_lines.size() % 2 || dynamic_world_lines.size() > UINT32_MAX / sizeof(WorldVertex) - endpoints.size()
            || !std::ranges::all_of(endpoints, finite_world_corner)) {
            throw std::runtime_error("Dynamic world line requires finite endpoints, line state and GPU byte capacity");
        }
        const auto view = world_view(projection);
        const auto first = Uint32(dynamic_world_lines.size());
        dynamic_world_lines.insert(dynamic_world_lines.end(), endpoints.begin(), endpoints.end());
        if (!draws.empty()) {
            auto& previous = draws.back();
            if (previous.dynamic_world && !previous.mesh && previous.state == state && previous.world_view == view && !previous.viewport && !previous.overlay
                && previous.vertex_offset == 0 && previous.model_instance == UINT32_MAX && previous.instance_count == 1 && previous.first % 2 == 0 && previous.count % 2 == 0
                && Uint64(previous.first) + previous.count == first) {
                previous.count += 2;
                return;
            }
        }
        Draw draw{state, first, 2};
        draw.world_view = view;
        draw.dynamic_world = true;
        draws.push_back(std::move(draw));
    }

    void mark_overlay(std::size_t first) {
        if (first > draws.size()) {
            throw std::runtime_error("Overlay start exceeds frame draws");
        }
        for (std::size_t index = first; index < draws.size(); ++index) {
            draws[index].overlay = true;
        }
    }

    // Keep each source's16-bit indices local, even when the combined scene
    // exceeds65536 vertices. Pane coordinates and depth remain independent.
    void append_view(const Frame& source, Viewport viewport) {
        if (&source == this || viewport.width == 0 || viewport.height == 0 || source.side_backdrop || source.non_4_3_image || source.background_image) {
            throw std::runtime_error("Frame composition requires an independent pixel-space view");
        }
        source.validate_dynamic_world();
        if (dynamic_world_vertices.size() + source.dynamic_world_vertices.size() > UINT32_MAX / sizeof(WorldVertex)
            || dynamic_world_lines.size() + source.dynamic_world_lines.size() > UINT32_MAX / sizeof(WorldVertex) || vertices.size() + source.vertices.size() > INT32_MAX
            || indices.size() + source.indices.size() > UINT32_MAX || world_views.size() + source.world_views.size() > UINT32_MAX
            || model_instances.size() + source.model_instances.size() > UINT32_MAX) {
            throw std::runtime_error("Composed frame exceeds GPU address capacity");
        }
        const auto vertex_base = Sint32(vertices.size());
        const auto index_base = Uint32(indices.size());
        const auto world_base = Uint32(world_views.size());
        const auto model_base = Uint32(model_instances.size());
        const auto dynamic_base = Uint32(dynamic_world_vertices.size() * 2);
        const auto line_base = Uint32(dynamic_world_lines.size());
        for (const auto& draw : source.draws) {
            if (draw.dynamic_world) {
                const auto primitive_size = draw.state.lines ? 2u : 6u;
                const auto corner_limit = draw.state.lines ? source.dynamic_world_lines.size() : source.dynamic_world_vertices.size() * 2;
                if (draw.mesh || draw.viewport || draw.vertex_offset != 0 || draw.world_view >= source.world_views.size() || draw.first % primitive_size || draw.count == 0
                    || draw.count % primitive_size || Uint64(draw.first) + draw.count > corner_limit || draw.model_instance != UINT32_MAX || draw.instance_count != 1) {
                    throw std::runtime_error("Frame composition requires valid unnested dynamic world draws");
                }
                continue;
            }
            if (draw.mesh) {
                if (draw.viewport || draw.world_view >= source.world_views.size() || draw.first % 6 || draw.count % 6
                    || Uint64(draw.first) + draw.count > draw.mesh->vertices.size() * 2 || draw.instance_count == 0
                    || (draw.model_instance != UINT32_MAX
                        && (Uint64(draw.model_instance) + draw.instance_count > source.model_instances.size() || draw.mesh->attributes.size() != draw.mesh->vertices.size()))
                    || (draw.model_instance == UINT32_MAX && (draw.instance_count != 1 || !draw.mesh->attributes.empty()))) {
                    throw std::runtime_error("Frame composition requires valid unnested world draws");
                }
                continue;
            }
            if (draw.viewport || Uint64(draw.first) + draw.count > source.indices.size() || draw.vertex_offset < 0 || draw.model_instance != UINT32_MAX
                || draw.instance_count != 1) {
                throw std::runtime_error("Frame composition requires valid unnested draws");
            }
            for (std::size_t index = draw.first; index < std::size_t(draw.first) + draw.count; ++index) {
                if (Uint64(source.indices[index]) + Uint64(draw.vertex_offset) >= source.vertices.size()) {
                    throw std::runtime_error("Frame draw index exceeds its vertices");
                }
            }
        }
        vertices.insert(vertices.end(), source.vertices.begin(), source.vertices.end());
        indices.insert(indices.end(), source.indices.begin(), source.indices.end());
        world_views.insert(world_views.end(), source.world_views.begin(), source.world_views.end());
        model_instances.insert(model_instances.end(), source.model_instances.begin(), source.model_instances.end());
        dynamic_world_vertices.insert(dynamic_world_vertices.end(), source.dynamic_world_vertices.begin(), source.dynamic_world_vertices.end());
        dynamic_world_lines.insert(dynamic_world_lines.end(), source.dynamic_world_lines.begin(), source.dynamic_world_lines.end());
        for (auto draw : source.draws) {
            if (draw.dynamic_world) {
                draw.first += draw.state.lines ? line_base : dynamic_base;
                draw.world_view += world_base;
            } else if (draw.mesh) {
                draw.world_view += world_base;
                if (draw.model_instance != UINT32_MAX) {
                    draw.model_instance += model_base;
                }
            } else {
                draw.first += index_base;
                draw.vertex_offset += vertex_base;
            }
            draw.viewport = viewport;
            draws.push_back(draw);
        }
        multisample = multisample || source.multisample;
    }

    // Keep16-bit indices local to consecutive batches, preserving draw order
    // while allowing a detailed scene and its overlays to exceed65536 vertices.
    Sint32 batch_vertex_offset(std::size_t vertex_count, std::size_t index_count) const {
        if (vertex_count > 65536 || vertices.size() > INT32_MAX - vertex_count || index_count > UINT32_MAX || indices.size() > UINT32_MAX - index_count) {
            throw std::runtime_error("Frame batch exceeds GPU address capacity");
        }
        const auto previous = draws.empty() ? 0 : draws.back().vertex_offset;
        if (previous < 0 || std::size_t(previous) > vertices.size()) {
            throw std::runtime_error("Frame batch has an invalid vertex offset");
        }
        return vertices.size() - std::size_t(previous) + vertex_count <= 65536 ? previous : Sint32(vertices.size());
    }

    void quad(State state, float x, float y, float w, float h, Uint32 color = 0xffffffff, float z = .5f, Uint32 fog = 0xff000000) {
        const auto offset = batch_vertex_offset(4, 6);
        const auto base = Uint16(vertices.size() - std::size_t(offset));
        draws.push_back({state, Uint32(indices.size()), 6, offset});
        vertices.insert(vertices.end(), {{x - .5f, y - .5f, z, 1, color, fog, 0, 0}, {x + w - .5f, y - .5f, z, 1, color, fog, 1, 0},
                                            {x + w - .5f, y + h - .5f, z, 1, color, fog, 1, 1}, {x - .5f, y + h - .5f, z, 1, color, fog, 0, 1}});
        for (const auto i : {0, 1, 2, 0, 2, 3}) {
            indices.push_back(Uint16(base + i));
        }
    }
};

struct Release {
    SDL_GPUDevice* device = nullptr;

    void operator()(SDL_GPUTexture* p) const {
        SDL_ReleaseGPUTexture(device, p);
    }

    void operator()(SDL_GPUBuffer* p) const {
        SDL_ReleaseGPUBuffer(device, p);
    }

    void operator()(SDL_GPUTransferBuffer* p) const {
        SDL_ReleaseGPUTransferBuffer(device, p);
    }

    void operator()(SDL_GPUShader* p) const {
        SDL_ReleaseGPUShader(device, p);
    }

    void operator()(SDL_GPUSampler* p) const {
        SDL_ReleaseGPUSampler(device, p);
    }

    void operator()(SDL_GPUGraphicsPipeline* p) const {
        SDL_ReleaseGPUGraphicsPipeline(device, p);
    }

    void operator()(SDL_GPUFence* p) const {
        SDL_ReleaseGPUFence(device, p);
    }
};
template <class T> using Resource = std::unique_ptr<T, Release>;
using Texture = Resource<SDL_GPUTexture>;

// CPU wall time spent inside the renderer; these are not GPU timestamps.
struct FrameTiming {
    Uint64 wait_ns = 0, upload_ns = 0, encode_ns = 0, submit_ns = 0;
    Uint64 draw_calls = 0, buffer_growths = 0;
    Uint64 mipmapped_draw_calls = 0;
    Uint64 anisotropic_draw_calls = 0;
    Uint64 world_draw_calls = 0, dynamic_world_draw_calls = 0, dynamic_line_draw_calls = 0;
    Uint64 model_draw_calls = 0, model_instances = 0, instanced_draw_calls = 0;
    BindingCounts bindings;
    Uint32 vertex_capacity = 0, index_capacity = 0, instance_capacity = 0, dynamic_world_capacity = 0, dynamic_line_capacity = 0, staging_capacity = 0;
    Uint32 samples = 1;
};

// Small state adapter, not an implementation of DirectDraw or the D3D COM API.
// SDL owns Vulkan/D3D12/Metal. Shaders are compiled offline from one HLSL source.
class Renderer {
    const RasterProfile raster_profile_;
    std::unique_ptr<SDL_GPUDevice, decltype(&SDL_DestroyGPUDevice)> device;
    SDL_Window* window;
    Texture color, next_color, depth, white, multisample_color, multisample_depth;
    bool color_valid = false, next_color_valid = false;
    bool multisample_supported_ = false;
    Resource<SDL_GPUShader> vertex_shader, fragment_shader, world_shader;
    Resource<SDL_GPUSampler> sampler, nearest_clamp_sampler, backdrop_sampler, linear_sampler, linear_clamp_sampler, mipmap_sampler, mipmap_clamp_sampler;
    std::array<Resource<SDL_GPUSampler>, 2> anisotropic_samplers, anisotropic_clamp_samplers;
    std::array<Resource<SDL_GPUGraphicsPipeline>, 8192> pipelines;
    Resource<SDL_GPUBuffer> vertices, indices, model_instances, dynamic_world_vertices, dynamic_world_lines, model_lighting;
    std::vector<std::int16_t> model_lighting_source;
    Resource<SDL_GPUTransferBuffer> staging;
    Uint32 vertex_capacity = 0, index_capacity = 0, instance_capacity = 0, dynamic_world_capacity = 0, dynamic_line_capacity = 0, staging_capacity = 0;
    Frame cover_frame_, background_frame_;
    std::vector<PreparedModelInstance> prepared_instances_;
    std::vector<Uint32> instance_view_bases_;

    struct TextureUpload {
        Resource<SDL_GPUTransferBuffer> buffer;
        Resource<SDL_GPUFence> fence;
        Uint32 capacity = 0;
    };

    std::array<TextureUpload, 3> texture_uploads;
    std::size_t next_texture_upload = 0;
    platform::Extent extent_;
    FrameTiming frame_timing_;

    static Uint32 grown_capacity(Uint32 current, Uint32 required) {
        if (required <= current) {
            return current;
        }
        // Keep at most 50% spare capacity (or one small 256-byte allocation).
        // Widen before growth so requests at the address limit cannot wrap.
        return Uint32(std::min<Uint64>(UINT32_MAX, std::max<Uint64>(256, Uint64(required) + required / 2)));
    }

    template <class T> Resource<T> own(T* p, const char* operation = "Create GPU resource") const {
        check(p != nullptr, operation);
        return Resource<T>(p, Release{device.get()});
    }

    Resource<SDL_GPUTransferBuffer> transfer(Uint32 size, SDL_GPUTransferBufferUsage usage, const std::string& purpose) {
        const SDL_GPUTransferBufferCreateInfo info{usage, size, 0};
        const auto operation = "Create " + purpose + " transfer buffer (" + std::to_string(size) + " bytes)";
        return own(SDL_CreateGPUTransferBuffer(device.get(), &info), operation.c_str());
    }

    TextureUpload& prepare_upload(Uint32 size, const std::string& purpose) {
        auto& upload = texture_uploads[next_texture_upload];
        next_texture_upload = (next_texture_upload + 1) % texture_uploads.size();
        // Fence ownership bounds both outstanding commands and staging bytes.
        if (upload.fence) {
            auto* fence = upload.fence.get();
            check(SDL_WaitForGPUFences(device.get(), true, &fence, 1), "Wait for upload reuse");
            upload.fence.reset();
        }
        if (upload.capacity < size) {
            upload.buffer = transfer(size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, purpose);
            upload.capacity = size;
        }
        return upload;
    }

    Resource<SDL_GPUShader> shader(const char* name, SDL_GPUShaderStage stage, bool world = false) {
        const auto formats = SDL_GetGPUShaderFormats(device.get());
        const auto format = formats & SDL_GPU_SHADERFORMAT_SPIRV  ? SDL_GPU_SHADERFORMAT_SPIRV
                            : formats & SDL_GPU_SHADERFORMAT_DXIL ? SDL_GPU_SHADERFORMAT_DXIL
                                                                  : SDL_GPU_SHADERFORMAT_MSL;
        const auto extension = format == SDL_GPU_SHADERFORMAT_SPIRV ? ".spv" : format == SDL_GPU_SHADERFORMAT_DXIL ? ".dxil" : ".msl";
        const auto* base = SDL_GetBasePath();
        check(base != nullptr, "Locate compiled shaders");
        auto path = std::filesystem::path(base) / "shaders" / (std::string(name) + extension);
#ifdef TOCA_SHADER_DIR
        if (!std::filesystem::is_regular_file(path)) {
            path = std::filesystem::path(TOCA_SHADER_DIR) / (std::string(name) + extension);
        }
#endif
        size_t size = 0;
        std::unique_ptr<void, decltype(&SDL_free)> code(SDL_LoadFile(path.string().c_str(), &size), SDL_free);
        check(bool(code), "Load compiled shader");
        SDL_GPUShaderCreateInfo info{};
        info.code = static_cast<const Uint8*>(code.get());
        info.code_size = size;
        info.entrypoint = format == SDL_GPU_SHADERFORMAT_MSL ? "main0" : "main";
        info.format = format;
        info.stage = stage;
        info.num_uniform_buffers = world ? 2 : 1;
        info.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1 : 0;
        info.num_storage_buffers = world ? 4 : 0;
        return own(SDL_CreateGPUShader(device.get(), &info));
    }

    bool hardware_draw(const Frame&, const Draw& draw) const {
        return draw.dynamic_world && raster_profile_ == RasterProfile::reference_dgvoodoo263;
    }

    bool reflected_hardware_draw(const Frame& frame, const Draw& draw) const {
        const auto& reflection = frame.world_views.at(draw.world_view).reflection;
        return (reflection[0] != 0) != (reflection[1] != 0);
    }

    static std::size_t pipeline_key(const State& state, bool multisample, bool world, bool hardware, bool reflected) {
        return state.depth | (state.blend << 1) | (state.wire << 2) | (state.depth_write << 3) | (state.lines << 4) | (state.screen << 5) | (multisample << 6)
               | (state.depth_bias << 7) | (state.depth_always << 8) | (world << 9) | (state.depth_less << 10) | (hardware << 11) | (reflected << 12);
    }

    Resource<SDL_GPUGraphicsPipeline> create_pipeline(const State& state, bool multisample, bool world, bool hardware, bool reflected) const {
        const SDL_GPUVertexBufferDescription buffer{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const std::array<SDL_GPUVertexAttribute, 4> attributes{{{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0}, {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 20}, {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}}};
        SDL_GPUColorTargetDescription target{};
        target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        target.blend_state.enable_blend = state.blend;
        target.blend_state.src_color_blendfactor = state.screen ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        target.blend_state.dst_color_blendfactor = state.screen ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = world ? world_shader.get() : vertex_shader.get();
        info.fragment_shader = fragment_shader.get();
        if (!world) {
            info.vertex_input_state = {&buffer, 1, attributes.data(), Uint32(attributes.size())};
        }
        const bool quadrilateral_lines = hardware && state.lines;
        info.primitive_type = state.lines && !quadrilateral_lines ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = state.wire ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = hardware && !quadrilateral_lines ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = reflected ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.rasterizer_state.enable_depth_bias = state.depth_bias;
        info.rasterizer_state.depth_bias_constant_factor = state.depth_bias ? -1.f : 0.f;
        info.depth_stencil_state.enable_depth_test = state.depth;
        info.depth_stencil_state.enable_depth_write = state.depth && state.depth_write;
        info.depth_stencil_state.compare_op = state.depth_always ? SDL_GPU_COMPAREOP_ALWAYS : state.depth_less ? SDL_GPU_COMPAREOP_LESS : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.multisample_state.sample_count = multisample ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
        info.target_info = {&target, 1, SDL_GPU_TEXTUREFORMAT_D16_UNORM, true, 0, 0, 0};
        return own(SDL_CreateGPUGraphicsPipeline(device.get(), &info));
    }

    SDL_GPUGraphicsPipeline* pipeline(const State& state, bool multisample = false, bool world = false, bool hardware = false, bool reflected = false) {
        auto& cached = pipelines[pipeline_key(state, multisample, world, hardware, reflected)];
        if (!cached) {
            cached = create_pipeline(state, multisample, world, hardware, reflected);
        }
        return cached.get();
    }

    template <class Visit> void visit_frame_pipelines(const Frame& frame, bool ui, Visit visit) const {
        const bool multisample = !ui && frame.multisample && multisample_supported_;
        if (multisample && frame.color_load == ColorLoad::preserve) {
            visit(State{}, true, false, false, false);
        }
        if (frame.non_4_3_image || frame.background_image || frame.side_backdrop) {
            visit(State{}, false, false, false, false);
        }
        for (const auto& draw : frame.draws) {
            const bool hardware = hardware_draw(frame, draw);
            visit(draw.state, multisample && !draw.overlay, bool(draw.mesh) || draw.dynamic_world, hardware, hardware && reflected_hardware_draw(frame, draw));
        }
    }

public:
    explicit Renderer(SDL_Window* w, RasterProfile raster_profile = RasterProfile::native)
        : raster_profile_(raster_profile), device(SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
#ifdef NDEBUG
                                                      false,
#else
                                                      true,
#endif
                                                      nullptr),
                                               SDL_DestroyGPUDevice),
          window(w) {
        check(bool(device), "Create SDL GPU device");
        check(SDL_ClaimWindowForGPUDevice(device.get(), window), "Claim GPU window");
        const auto properties = SDL_GetGPUDeviceProperties(device.get());
        std::cout << "Renderer: SDL GPU / " << SDL_GetGPUDeviceDriver(device.get()) << " / "
                  << SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown adapter") << " / "
                  << SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "unknown driver version") << " / video " << SDL_GetCurrentVideoDriver() << '\n';
        if (SDL_GPUTextureSupportsSampleCount(device.get(), SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_SAMPLECOUNT_2)
            && SDL_GPUTextureSupportsSampleCount(device.get(), SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_SAMPLECOUNT_2)) {
            multisample_supported_ = true;
        } else {
            std::clog << "2x MSAA unavailable for scene color/depth; Mega uses single-sample scene rendering\n";
        }
        vertex_shader = shader("legacy.vert", SDL_GPU_SHADERSTAGE_VERTEX);
        fragment_shader = shader("legacy.frag", SDL_GPU_SHADERSTAGE_FRAGMENT);
        world_shader = shader(raster_profile_ == RasterProfile::reference_dgvoodoo263 ? "world-reference.vert" : "world.vert", SDL_GPU_SHADERSTAGE_VERTEX, true);
        SDL_GPUSamplerCreateInfo sample{};
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nearest_clamp_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sample.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        backdrop_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sample.min_filter = sample.mag_filter = SDL_GPU_FILTER_LINEAR;
        linear_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        linear_clamp_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sample.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sample.max_lod = std::numeric_limits<float>::max();
        mipmap_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        mipmap_clamp_sampler = own(SDL_CreateGPUSampler(device.get(), &sample));
        // SDL3's Vulkan, D3D12 and Metal backends expose the same fixed
        // anisotropy ratio. Keep the trilinear LOD and border policy intact.
        sample.enable_anisotropy = true;
        for (std::size_t index = 0; index < anisotropic_samplers.size(); ++index) {
            sample.max_anisotropy = index == 0 ? 2.f : 4.f;
            sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            anisotropic_samplers[index] = own(SDL_CreateGPUSampler(device.get(), &sample));
            sample.address_mode_u = sample.address_mode_v = sample.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            anisotropic_clamp_samplers[index] = own(SDL_CreateGPUSampler(device.get(), &sample));
        }
        white = texture(1, 1);
        const Uint8 pixel[]{255, 255, 255, 255};
        update(white.get(), pixel, 1, 1);
    }

    ~Renderer() {
        SDL_WaitForGPUIdle(device.get());
        SDL_ReleaseWindowFromGPUDevice(device.get(), window);
    }

    Texture texture(Uint32 w, Uint32 h, SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1, Uint32 mip_levels = 1) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = w;
        info.height = h;
        info.layer_count_or_depth = 1;
        info.num_levels = mip_levels;
        info.sample_count = samples;
        const auto operation = "Create GPU texture (" + std::to_string(w) + "x" + std::to_string(h) + ", format " + std::to_string(format) + ")";
        return own(SDL_CreateGPUTexture(device.get(), &info), operation.c_str());
    }

    void update(SDL_GPUTexture* target, const void* rgba, Uint32 w, Uint32 h, Uint32 mip_levels = 1) {
        if (w == 0 || h == 0 || mip_levels == 0 || mip_levels > Uint32(std::bit_width(std::max(w, h)))) {
            throw std::runtime_error("Texture upload requires nonempty dimensions and a valid mip count");
        }
        if (Uint64(w) * h > UINT32_MAX / 4) {
            throw std::runtime_error("Texture upload exceeds GPU byte capacity");
        }
        // Prepare fallible scratch allocations before recording any commands.
        // SDL3.4.2 needs level-zero scratch images for rectangular mip blits.
        std::vector<Texture> mip_images;
        mip_images.reserve(mip_levels - 1);
        for (Uint32 level = 1; level < mip_levels; ++level) {
            mip_images.push_back(texture(
                std::max(1u, w >> level), std::max(1u, h >> level), SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET));
        }
        const auto size = w * h * 4;
        auto& upload = prepare_upload(size, "texture upload " + std::to_string(w) + "x" + std::to_string(h));
        {
            detail::TransferMapping mapped(device.get(), upload.buffer.get(), false);
            check(mapped.get() != nullptr, "Map texture upload");
            std::memcpy(mapped.get(), rgba, size);
        }
        detail::CommandScope commands(SDL_AcquireGPUCommandBuffer(device.get()));
        auto* cmd = commands.get();
        check(cmd, "Acquire upload commands");
        auto* copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
        check(copy, "Begin texture copy");
        const SDL_GPUTextureTransferInfo source{upload.buffer.get(), 0, w, h};
        const SDL_GPUTextureRegion dest{target, 0, 0, 0, 0, 0, w, h, 1};
        // Preserve untouched texels on partial updates; SDL orders the upload after earlier draws.
        SDL_UploadToGPUTexture(copy, &source, &dest, false);
        commands.end_copy();
        // Keep the scratch images alive through submission; SDL retires them afterward.
        auto* previous = target;
        for (Uint32 level = 1; level < mip_levels; ++level) {
            const auto width = std::max(1u, w >> level), height = std::max(1u, h >> level);
            SDL_GPUBlitInfo blit{};
            blit.source = {previous, 0, 0, 0, 0, std::max(1u, w >> (level - 1)), std::max(1u, h >> (level - 1))};
            blit.destination = {mip_images[level - 1].get(), 0, 0, 0, 0, width, height};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(cmd, &blit);
            auto* mip_copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
            check(mip_copy, "Begin mip-level copy");
            const SDL_GPUTextureLocation mip_source{mip_images[level - 1].get(), 0, 0, 0, 0, 0};
            const SDL_GPUTextureLocation mip_destination{target, level, 0, 0, 0, 0};
            SDL_CopyGPUTextureToTexture(mip_copy, &mip_source, &mip_destination, width, height, 1, false);
            commands.end_copy();
            previous = mip_images[level - 1].get();
        }
        upload.fence = own(commands.submit_fenced(), "Submit texture upload");
    }

    std::shared_ptr<const WorldMesh> world_mesh(std::vector<WorldVertex> source, std::vector<ModelAttribute> attributes = {}) {
        if (source.empty() || source.size() % 3 != 0 || source.size() > UINT32_MAX / (sizeof(WorldVertex) + (attributes.empty() ? 0 : sizeof(ModelAttribute)))
            || (!attributes.empty() && attributes.size() != source.size())) {
            throw std::runtime_error("World mesh requires complete triangles within GPU address capacity");
        }
        const auto corner_bytes = Uint32(source.size() * sizeof(WorldVertex));
        const auto size = Uint32(corner_bytes + attributes.size() * sizeof(ModelAttribute));
        const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, size, 0};
        auto buffer = own(SDL_CreateGPUBuffer(device.get(), &info), "Create resident world mesh");
        auto& upload = prepare_upload(size, "world mesh");
        {
            detail::TransferMapping mapped(device.get(), upload.buffer.get(), false);
            check(mapped.get() != nullptr, "Map world mesh upload");
            std::memcpy(mapped.get(), source.data(), corner_bytes);
            if (!attributes.empty()) {
                std::memcpy(static_cast<Uint8*>(mapped.get()) + corner_bytes, attributes.data(), attributes.size() * sizeof(ModelAttribute));
            }
        }
        detail::CommandScope commands(SDL_AcquireGPUCommandBuffer(device.get()));
        auto* cmd = commands.get();
        check(cmd, "Acquire mesh upload commands");
        auto* copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
        check(copy, "Begin mesh copy");
        const SDL_GPUTransferBufferLocation from{upload.buffer.get(), 0};
        const SDL_GPUBufferRegion to{buffer.get(), 0, size};
        SDL_UploadToGPUBuffer(copy, &from, &to, false);
        commands.end_copy();
        upload.fence = own(commands.submit_fenced(), "Submit world mesh upload");
        auto result = std::make_shared<WorldMesh>(device.get(), buffer.get(), std::move(source), std::move(attributes));
        buffer.release();
        return result;
    }

    void set_model_lighting_table(std::span<const std::int16_t> source) {
        if (source.size() != 4096) {
            throw std::runtime_error("Model lighting requires the recovered 4096-entry angle table");
        }
        if (!model_lighting_source.empty()) {
            if (!std::ranges::equal(source, model_lighting_source)) {
                throw std::runtime_error("Model lighting table cannot change after upload");
            }
            return;
        }
        std::array<std::int32_t, 4096> packed;
        std::copy(source.begin(), source.end(), packed.begin());
        const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, sizeof(packed), 0};
        auto buffer = own(SDL_CreateGPUBuffer(device.get(), &info), "Create model lighting table");
        std::vector<std::int16_t> retained_source(source.begin(), source.end());
        auto& upload = prepare_upload(sizeof(packed), "model lighting");
        {
            detail::TransferMapping mapped(device.get(), upload.buffer.get(), false);
            check(mapped.get() != nullptr, "Map model lighting upload");
            std::memcpy(mapped.get(), packed.data(), sizeof(packed));
        }
        detail::CommandScope commands(SDL_AcquireGPUCommandBuffer(device.get()));
        auto* cmd = commands.get();
        check(cmd, "Acquire model lighting upload commands");
        auto* copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
        check(copy, "Begin model lighting copy");
        const SDL_GPUTransferBufferLocation from{upload.buffer.get(), 0};
        const SDL_GPUBufferRegion to{buffer.get(), 0, sizeof(packed)};
        SDL_UploadToGPUBuffer(copy, &from, &to, false);
        commands.end_copy();
        upload.fence = own(commands.submit_fenced(), "Submit model lighting upload");
        model_lighting_source.swap(retained_source);
        model_lighting = std::move(buffer);
    }

    Texture upload(SDL_Surface* source, bool source_key = false, bool mipmaps = false, Uint32* mip_levels = nullptr) {
        Surface rgba(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        check(bool(rgba), "Convert palette/BMP");
        std::vector<Uint8> packed(rgba->w * rgba->h * 4);
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(packed.data() + y * rgba->w * 4, static_cast<Uint8*>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
        }
        if (source_key && SDL_ISPIXELFORMAT_INDEXED(source->format)) {
            // 483261 preserves P8 indices; 484111 sets packed source key zero.
            // Keep RGB intact for passes that disable keying (e.g. the sky).
            const auto bits = SDL_BITSPERPIXEL(source->format);
            const auto mask = (1u << bits) - 1;
            for (int y = 0; y < source->h; ++y) {
                const auto* row = static_cast<const Uint8*>(source->pixels) + y * source->pitch;
                for (int x = 0; x < source->w; ++x) {
                    const auto bit = unsigned(x) * bits;
                    const auto shift = bits == 8 || SDL_PIXELORDER(source->format) == SDL_BITMAPORDER_1234 ? bit % 8 : 8 - bits - bit % 8;
                    const auto index = (row[bit / 8] >> shift) & mask;
                    packed[(std::size_t(y) * source->w + x) * 4 + 3] = index == 0 ? 0 : 255;
                }
            }
        } else if (source_key) {
            // Nonindexed/alpha-format conversion keys exact RGB black.
            for (size_t i = 0; i < packed.size(); i += 4) {
                packed[i + 3] = (packed[i] | packed[i + 1] | packed[i + 2]) == 0 ? 0 : 255;
            }
        }
        bool opaque = mipmaps;
        if (mipmaps) {
            for (std::size_t index = 3; index < packed.size(); index += 4) {
                if (packed[index] != 255) {
                    opaque = false;
                    break;
                }
            }
        }
        const auto levels = mipmaps && opaque ? Uint32(std::bit_width(std::max(Uint32(rgba->w), Uint32(rgba->h)))) : 1u;
        const auto usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
        auto result = texture(rgba->w, rgba->h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, SDL_GPU_SAMPLECOUNT_1, levels);
        update(result.get(), packed.data(), rgba->w, rgba->h, levels);
        if (mip_levels) {
            *mip_levels = levels;
        }
        return result;
    }

    platform::Extent drawable_extent() const {
        return extent_;
    }

    RasterProfile raster_profile() const {
        return raster_profile_;
    }

    const FrameTiming& last_frame_timing() const {
        return frame_timing_;
    }

    Uint32 scene_sample_count() const {
        return frame_timing_.samples;
    }

    void wait_idle() {
        check(SDL_WaitForGPUIdle(device.get()), "Wait for rendering completion");
    }

    bool set_vsync(bool requested) {
        auto mode = requested ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (!SDL_WindowSupportsGPUPresentMode(device.get(), window, mode)) {
            std::cerr << "Requested immediate presentation is unavailable; using VSync\n";
            mode = SDL_GPU_PRESENTMODE_VSYNC;
        }
        check(SDL_SetGPUSwapchainParameters(device.get(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode), "Set GPU presentation mode");
        return mode == SDL_GPU_PRESENTMODE_VSYNC;
    }

    // Call on the rendering thread before activating a newly loaded scene.
    // Only immutable shader/device inputs reach the worker; cache updates,
    // loading presentation and all command buffers stay on the caller thread.
    void prepare_pipelines(const Frame& frame, bool ui, const std::function<void()>& service) {
        struct Request {
            State state;
            bool multisample, world, hardware, reflected;
            std::size_t key;
        };

        std::vector<Request> requests;
        std::array<bool, 8192> requested{};
        visit_frame_pipelines(frame, ui, [&](State state, bool multisample, bool world, bool hardware, bool reflected) {
            const auto key = pipeline_key(state, multisample, world, hardware, reflected);
            if (!pipelines[key] && !requested[key]) {
                state.texture = nullptr; // Pipeline creation never needs a scene texture.
                requests.push_back({state, multisample, world, hardware, reflected, key});
                requested[key] = true;
            }
        });
        if (requests.empty()) {
            return;
        }
        auto worker = std::async(std::launch::async, [this, &requests] {
            std::vector<Resource<SDL_GPUGraphicsPipeline>> prepared;
            prepared.reserve(requests.size());
            for (const auto& request : requests) {
                prepared.push_back(create_pipeline(request.state, request.multisample, request.world, request.hardware, request.reflected));
            }
            return prepared;
        });
        // The future also joins on callback/error unwinding, before requests,
        // shader handles or the GPU device can be destroyed.
        if (service) {
            service();
        }
        while (worker.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
            if (service) {
                service();
            }
        }
        auto prepared = worker.get();
        for (std::size_t index = 0; index < requests.size(); ++index) {
            auto& cached = pipelines[requests[index].key];
            if (!cached) {
                cached = std::move(prepared[index]);
            }
        }
    }

private:
    bool draw_internal(const Frame& source_frame, bool ui, std::optional<platform::Extent> offscreen) {
        frame_timing_ = {};
        source_frame.validate_dynamic_world();
        if ((source_frame.vertices.empty() || source_frame.indices.empty()) && !std::ranges::any_of(source_frame.draws, [](const auto& draw) {
                return bool(draw.mesh) || draw.dynamic_world;
            })) {
            throw std::runtime_error("Empty GPU frame");
        }
        for (const auto& draw : source_frame.draws) {
            if (raster_profile_ == RasterProfile::reference_dgvoodoo263 && (draw.state.palette || draw.state.reference_indexed)
                && (draw.state.mipmapped || draw.state.anisotropy != Anisotropy::off)) {
                throw std::runtime_error("Reference indexed raster requires explicit level-zero point/linear sampling without anisotropy");
            }
            if (draw.dynamic_world) {
                const auto primitive_size = draw.state.lines ? 2u : 6u;
                const auto corner_limit = draw.state.lines ? source_frame.dynamic_world_lines.size() : source_frame.dynamic_world_vertices.size() * 2;
                if (draw.mesh || draw.vertex_offset != 0 || draw.world_view >= source_frame.world_views.size() || draw.first % primitive_size || draw.count == 0
                    || draw.count % primitive_size || Uint64(draw.first) + draw.count > corner_limit || draw.model_instance != UINT32_MAX || draw.instance_count != 1) {
                    throw std::runtime_error("Dynamic world draw requires prelit primitives, a projection and a valid corner range");
                }
                if (draw.state.lines && hardware_draw(source_frame, draw) && (Uint64(draw.first) + draw.count) * 3 > UINT32_MAX) {
                    throw std::runtime_error("Reference line expansion exceeds GPU vertex addressing");
                }
            }
            if (draw.mesh
                && (draw.mesh->device != device.get() || draw.world_view >= source_frame.world_views.size() || draw.first % 6 || draw.count % 6
                    || Uint64(draw.first) + draw.count > draw.mesh->vertices.size() * 2)) {
                throw std::runtime_error("World draw requires a valid resident mesh, projection and triangle range");
            }
            if (draw.instance_count == 0 || (draw.model_instance == UINT32_MAX && (draw.instance_count != 1 || (draw.mesh && !draw.mesh->attributes.empty())))
                || (draw.model_instance != UINT32_MAX
                    && (!draw.mesh || draw.mesh->attributes.size() != draw.mesh->vertices.size() || !model_lighting
                        || Uint64(draw.model_instance) + draw.instance_count > source_frame.model_instances.size()))) {
                throw std::runtime_error("Model draw requires resident attributes, lighting and a valid instance range");
            }
        }
        if (source_frame.model_instances.size() > UINT32_MAX / sizeof(ModelInstance)) {
            throw std::runtime_error("Model instances exceed GPU byte capacity");
        }
        for (const auto& instance : source_frame.model_instances) {
            const auto finite = [](float value) {
                return std::isfinite(value);
            };
            if (!std::ranges::all_of(instance.object_to_world, finite) || !std::ranges::all_of(instance.direction_diffuse, finite)
                || !std::ranges::all_of(instance.uv_remap, finite) || instance.direction_diffuse[3] < 0 || instance.ambient_flags[0] > 255 || instance.ambient_flags[1] > 255
                || instance.ambient_flags[2] > 255 || instance.ambient_flags[3] > 63 || ((instance.ambient_flags[3] & 8) && instance.uv_remap[1] <= 0)) {
                throw std::runtime_error("Model instance requires finite transforms, lighting and a valid name row");
            }
        }
        if (source_frame.non_4_3_image && (!source_frame.non_4_3_image->texture || source_frame.non_4_3_image->width == 0 || source_frame.non_4_3_image->height == 0)) {
            throw std::runtime_error("Alternative UI image requires a texture and positive dimensions");
        }
        if (source_frame.background_image && (!source_frame.background_image->texture || source_frame.background_image->width == 0 || source_frame.background_image->height == 0)) {
            throw std::runtime_error("UI background image requires a texture and positive dimensions");
        }
        if (offscreen && (offscreen->width == 0 || offscreen->height == 0)) {
            throw std::runtime_error("Offscreen rendering requires a nonempty extent");
        }
        const bool multisample = !ui && source_frame.multisample && multisample_supported_;
        visit_frame_pipelines(source_frame, ui, [&](const State& state, bool sampled, bool world, bool hardware, bool reflected) {
            pipeline(state, sampled, world, hardware, reflected);
        });
        const auto wait_started = SDL_GetTicksNS();
        detail::CommandScope commands(SDL_AcquireGPUCommandBuffer(device.get()));
        auto* cmd = commands.get();
        check(cmd, "Acquire frame commands");
        SDL_GPUTexture* swap = nullptr;
        Uint32 sw = offscreen ? offscreen->width : 0, sh = offscreen ? offscreen->height : 0;
        if (!offscreen) {
            const auto acquired = SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &sw, &sh);
            if (swap) {
                commands.acquired_swapchain();
            }
            check(acquired, "Acquire swapchain");
        }
        const auto upload_started = SDL_GetTicksNS();
        frame_timing_.wait_ns = upload_started - wait_started;
        if (!offscreen && !swap) {
            const auto submit_started = SDL_GetTicksNS();
            check(commands.submit(), "Submit minimized frame");
            frame_timing_.submit_ns = SDL_GetTicksNS() - submit_started;
            return false;
        }
        if (extent_.width != sw || extent_.height != sh) {
            auto new_color = texture(sw, sh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
            auto new_next = texture(sw, sh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
            auto new_depth = texture(sw, sh, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
            color = std::move(new_color);
            next_color = std::move(new_next);
            depth = std::move(new_depth);
            color_valid = next_color_valid = false;
            multisample_color.reset();
            multisample_depth.reset();
            extent_ = {sw, sh};
        }
        // The reference flips two permanent world targets: frame3 inherits frame1.
        // New/resized targets are initialized even when the scene requests LOAD.
        const bool preserve_color = !ui && source_frame.color_load == ColorLoad::preserve && next_color_valid;
        const bool seed_multisample = multisample && preserve_color;
        frame_timing_.samples = multisample ? 2 : 1;
        if (multisample && !multisample_color) {
            detail::replace_pair(
                multisample_color, multisample_depth,
                [&] {
                    return texture(sw, sh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, SDL_GPU_SAMPLECOUNT_2);
                },
                [&] {
                    return texture(sw, sh, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, SDL_GPU_SAMPLECOUNT_2);
                });
        }
        auto& cover_frame = cover_frame_;
        cover_frame.clear();
        const bool cover = ui && source_frame.non_4_3_image && std::uint64_t(sw) * 3 != std::uint64_t(sh) * 4;
        if (cover) {
            const auto& image = *source_frame.non_4_3_image;
            const auto scale = std::max(float(sw) / float(image.width), float(sh) / float(image.height));
            const auto width = float(image.width) * scale, height = float(image.height) * scale;
            cover_frame.quad({.texture = image.texture}, (float(sw) - width) / 2, (float(sh) - height) / 2, width, height);
        }
        const auto& frame = cover ? cover_frame : source_frame;
        const bool centered_ui = ui && !cover;
        const auto transform = platform::ui_transform(extent_);
        const bool background_image = centered_ui && frame.background_image.has_value();
        const bool backdrop = centered_ui && !background_image && frame.side_backdrop && transform.x > 0;
        auto& background_frame = background_frame_;
        background_frame.clear();
        if (seed_multisample) {
            background_frame.quad({.texture = next_color.get(), .clamp = true}, 0, 0, float(sw), float(sh));
        } else if (background_image) {
            const auto& image = *frame.background_image;
            const auto scale = std::max(float(sw) / float(image.width), float(sh) / float(image.height));
            const auto width = float(image.width) * scale, height = float(image.height) * scale;
            background_frame.quad({.texture = image.texture, .linear = true, .clamp = true}, (float(sw) - width) / 2, (float(sh) - height) / 2, width, height);
        } else if (backdrop) {
            const auto right = transform.x + transform.width;
            const auto outer_u = transform.x / transform.width;
            // Physical-pixel quads extend the photograph at the same scale as
            // the UI. Mirroring continues each edge without repeating UI art.
            background_frame.vertices = {{-.5f, -.5f, .5f, 1, 0xffffffff, 0xff000000, -outer_u, 0}, {transform.x - .5f, -.5f, .5f, 1, 0xffffffff, 0xff000000, 0, 0},
                {transform.x - .5f, float(sh) - .5f, .5f, 1, 0xffffffff, 0xff000000, 0, 1}, {-.5f, float(sh) - .5f, .5f, 1, 0xffffffff, 0xff000000, -outer_u, 1},
                {right - .5f, -.5f, .5f, 1, 0xffffffff, 0xff000000, 1, 0}, {float(sw) - .5f, -.5f, .5f, 1, 0xffffffff, 0xff000000, 1 + outer_u, 0},
                {float(sw) - .5f, float(sh) - .5f, .5f, 1, 0xffffffff, 0xff000000, 1 + outer_u, 1}, {right - .5f, float(sh) - .5f, .5f, 1, 0xffffffff, 0xff000000, 1, 1}};
            background_frame.indices = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        }
        prepared_instances_.clear();
        instance_view_bases_.assign(frame.world_views.size(), UINT32_MAX);
        for (const auto& draw : frame.draws) {
            if (draw.model_instance == UINT32_MAX || instance_view_bases_[draw.world_view] != UINT32_MAX) {
                continue;
            }
            if (frame.model_instances.size() > UINT32_MAX / sizeof(PreparedModelInstance) - prepared_instances_.size()) {
                throw std::runtime_error("Prepared model projections exceed GPU byte capacity");
            }
            instance_view_bases_[draw.world_view] = Uint32(prepared_instances_.size());
            const auto& projection = frame.world_views[draw.world_view];
            for (const auto& instance : frame.model_instances) {
                prepared_instances_.push_back({instance, source_clip_matrix(projection, instance.object_to_world)});
            }
        }
        const auto bytes = [&](std::size_t count, std::size_t stride) {
            if (count > UINT32_MAX / stride) {
                throw std::runtime_error("Frame upload exceeds GPU byte capacity");
            }
            return Uint32(count * stride);
        };
        const auto frame_vb = bytes(frame.vertices.size(), sizeof(Vertex));
        const auto frame_ib = bytes(frame.indices.size(), sizeof(Uint16));
        const auto background_vb = bytes(background_frame.vertices.size(), sizeof(Vertex));
        const auto background_ib = bytes(background_frame.indices.size(), sizeof(Uint16));
        const auto instance_bytes = bytes(prepared_instances_.size(), sizeof(PreparedModelInstance));
        const auto dynamic_world_bytes = bytes(frame.dynamic_world_vertices.size(), sizeof(WorldVertex));
        const auto dynamic_line_bytes = bytes(frame.dynamic_world_lines.size(), sizeof(WorldVertex));
        const auto geometry_bytes = Uint64(frame_vb) + frame_ib + background_vb + background_ib;
        // Keep storage uploads aligned even when a TL index list has odd length.
        const auto instance_offset = instance_bytes ? (geometry_bytes + 15) & ~Uint64(15) : geometry_bytes;
        const auto dynamic_world_offset = dynamic_world_bytes ? (instance_offset + instance_bytes + 15) & ~Uint64(15) : instance_offset + instance_bytes;
        const auto dynamic_line_offset = dynamic_line_bytes ? (dynamic_world_offset + dynamic_world_bytes + 15) & ~Uint64(15) : dynamic_world_offset + dynamic_world_bytes;
        const auto upload_bytes = dynamic_line_offset + dynamic_line_bytes;
        if (upload_bytes > UINT32_MAX) {
            throw std::runtime_error("Frame upload exceeds GPU byte capacity");
        }
        const auto vb = Uint32(Uint64(frame_vb) + background_vb);
        const auto ib = Uint32(Uint64(frame_ib) + background_ib);
        const auto upload_size = Uint32(upload_bytes);
        const auto new_vertex_capacity = grown_capacity(vertex_capacity, vb);
        const auto new_index_capacity = grown_capacity(index_capacity, ib);
        const auto new_instance_capacity = grown_capacity(instance_capacity, instance_bytes);
        const auto new_dynamic_world_capacity = grown_capacity(dynamic_world_capacity, dynamic_world_bytes);
        const auto new_dynamic_line_capacity = grown_capacity(dynamic_line_capacity, dynamic_line_bytes);
        const auto new_staging_capacity = grown_capacity(staging_capacity, upload_size);
        if (new_vertex_capacity != vertex_capacity || new_index_capacity != index_capacity || new_instance_capacity != instance_capacity
            || new_dynamic_world_capacity != dynamic_world_capacity || new_dynamic_line_capacity != dynamic_line_capacity || new_staging_capacity != staging_capacity) {
            Resource<SDL_GPUBuffer> new_vertices, new_indices, new_instances, new_dynamic_world, new_dynamic_lines;
            Resource<SDL_GPUTransferBuffer> new_staging;
            if (vb > vertex_capacity) {
                const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_VERTEX, new_vertex_capacity, 0};
                const auto operation = "Create GPU vertex buffer (" + std::to_string(new_vertex_capacity) + " bytes)";
                new_vertices = own(SDL_CreateGPUBuffer(device.get(), &info), operation.c_str());
            }
            if (ib > index_capacity) {
                const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_INDEX, new_index_capacity, 0};
                const auto operation = "Create GPU index buffer (" + std::to_string(new_index_capacity) + " bytes)";
                new_indices = own(SDL_CreateGPUBuffer(device.get(), &info), operation.c_str());
            }
            if (instance_bytes > instance_capacity) {
                const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, new_instance_capacity, 0};
                new_instances = own(SDL_CreateGPUBuffer(device.get(), &info), "Create model instance buffer");
            }
            if (dynamic_world_bytes > dynamic_world_capacity) {
                const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, new_dynamic_world_capacity, 0};
                new_dynamic_world = own(SDL_CreateGPUBuffer(device.get(), &info), "Create dynamic world corner buffer");
            }
            if (dynamic_line_bytes > dynamic_line_capacity) {
                const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, new_dynamic_line_capacity, 0};
                new_dynamic_lines = own(SDL_CreateGPUBuffer(device.get(), &info), "Create dynamic world line buffer");
            }
            if (upload_size > staging_capacity) {
                new_staging = transfer(new_staging_capacity, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, "frame upload");
            }
            // Commit only after every requested allocation succeeds. Each
            // resource retains its own allocated capacity.
            if (new_vertices) {
                vertices = std::move(new_vertices);
                vertex_capacity = new_vertex_capacity;
            }
            if (new_indices) {
                indices = std::move(new_indices);
                index_capacity = new_index_capacity;
            }
            if (new_instances) {
                model_instances = std::move(new_instances);
                instance_capacity = new_instance_capacity;
            }
            if (new_dynamic_world) {
                dynamic_world_vertices = std::move(new_dynamic_world);
                dynamic_world_capacity = new_dynamic_world_capacity;
            }
            if (new_dynamic_lines) {
                dynamic_world_lines = std::move(new_dynamic_lines);
                dynamic_line_capacity = new_dynamic_line_capacity;
            }
            if (new_staging) {
                staging = std::move(new_staging);
                staging_capacity = new_staging_capacity;
            }
            ++frame_timing_.buffer_growths;
        }
        frame_timing_.vertex_capacity = vertex_capacity;
        frame_timing_.index_capacity = index_capacity;
        frame_timing_.instance_capacity = instance_capacity;
        frame_timing_.dynamic_world_capacity = dynamic_world_capacity;
        frame_timing_.dynamic_line_capacity = dynamic_line_capacity;
        frame_timing_.staging_capacity = staging_capacity;
        if (upload_size != 0) {
            {
                detail::TransferMapping mapping(device.get(), staging.get(), true);
                auto* mapped = static_cast<Uint8*>(mapping.get());
                check(mapped, "Map frame upload");
                if (frame_vb) {
                    std::memcpy(mapped, frame.vertices.data(), frame_vb);
                }
                if (frame_ib) {
                    std::memcpy(mapped + vb, frame.indices.data(), frame_ib);
                }
                if (background_vb != 0) {
                    std::memcpy(mapped + frame_vb, background_frame.vertices.data(), background_vb);
                    std::memcpy(mapped + vb + frame_ib, background_frame.indices.data(), background_ib);
                }
                if (instance_bytes) {
                    std::memcpy(mapped + instance_offset, prepared_instances_.data(), instance_bytes);
                }
                if (dynamic_world_bytes) {
                    std::memcpy(mapped + dynamic_world_offset, frame.dynamic_world_vertices.data(), dynamic_world_bytes);
                }
                if (dynamic_line_bytes) {
                    std::memcpy(mapped + dynamic_line_offset, frame.dynamic_world_lines.data(), dynamic_line_bytes);
                }
            }
            auto* copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
            check(copy, "Begin frame copy");
            const SDL_GPUTransferBufferLocation vs{staging.get(), 0}, is{staging.get(), vb};
            const SDL_GPUBufferRegion vd{vertices.get(), 0, vb}, id{indices.get(), 0, ib};
            if (vb) {
                SDL_UploadToGPUBuffer(copy, &vs, &vd, true);
            }
            if (ib) {
                SDL_UploadToGPUBuffer(copy, &is, &id, true);
            }
            if (instance_bytes) {
                const SDL_GPUTransferBufferLocation from{staging.get(), Uint32(instance_offset)};
                const SDL_GPUBufferRegion to{model_instances.get(), 0, instance_bytes};
                SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            if (dynamic_world_bytes) {
                const SDL_GPUTransferBufferLocation from{staging.get(), Uint32(dynamic_world_offset)};
                const SDL_GPUBufferRegion to{dynamic_world_vertices.get(), 0, dynamic_world_bytes};
                SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            if (dynamic_line_bytes) {
                const SDL_GPUTransferBufferLocation from{staging.get(), Uint32(dynamic_line_offset)};
                const SDL_GPUBufferRegion to{dynamic_world_lines.get(), 0, dynamic_line_bytes};
                SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            commands.end_copy();
        }
        const auto encode_started = SDL_GetTicksNS();
        frame_timing_.upload_ns = encode_started - upload_started;
        if (seed_multisample) {
            // Seed every sample from the final resolved history, including 1x overlays.
            // This separate pass avoids sampling the resolve attachment in its own pass.
            SDL_GPUColorTargetInfo target{};
            target.texture = multisample_color.get();
            target.load_op = SDL_GPU_LOADOP_DONT_CARE;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo z{};
            z.texture = multisample_depth.get();
            z.load_op = SDL_GPU_LOADOP_DONT_CARE;
            z.store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto* pass = commands.render(SDL_BeginGPURenderPass(cmd, &target, 1, &z));
            check(pass, "Begin color history seed");
            PassBindings bindings(cmd, pass, frame_timing_.bindings);
            const SDL_GPUBufferBinding vertex_binding{vertices.get(), 0}, index_binding{indices.get(), 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            bindings.viewport({0, 0, float(sw), float(sh), 0, 1});
            const float dimensions[]{float(sw), float(sh), 0, 0, 2.f / float(sw), -2.f / float(sh), 0, 0};
            bindings.vertex_uniform(0, dimensions);
            bindings.pipeline(pipeline({}, true));
            bindings.sampler({next_color.get(), nearest_clamp_sampler.get()});
            const float material[]{0, 0, 200.f / 255, 1, 0, 0, 0, 0, 0, 0, 0, 0};
            bindings.fragment_uniform(material);
            SDL_DrawGPUIndexedPrimitives(pass, 6, 1, Uint32(frame.indices.size()), Sint32(frame.vertices.size()), 0);
            ++frame_timing_.draw_calls;
            commands.end_render();
        }
        const auto render_pass = [&](SDL_GPUTexture* destination, bool overlay_phase, bool use_multisample) {
            SDL_GPUColorTargetInfo target{};
            target.texture = destination;
            target.clear_color = frame.clear_color;
            target.load_op = overlay_phase || preserve_color ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
            target.store_op = use_multisample ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
            target.resolve_texture = use_multisample ? next_color.get() : nullptr;
            SDL_GPUDepthStencilTargetInfo z{};
            z.texture = use_multisample ? multisample_depth.get() : depth.get();
            z.clear_depth = 1;
            z.load_op = SDL_GPU_LOADOP_CLEAR;
            z.store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto* pass = commands.render(SDL_BeginGPURenderPass(cmd, &target, 1, &z));
            check(pass, "Begin GPU rendering");
            PassBindings bindings(cmd, pass, frame_timing_.bindings);
            const SDL_GPUBufferBinding vertex_binding{vertices.get(), 0}, index_binding{indices.get(), 0};
            if (vb) {
                SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            }
            if (ib) {
                SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            }
            if (background_vb != 0 && !seed_multisample) {
                const SDL_GPUViewport full{0, 0, float(sw), float(sh), 0, 1};
                bindings.viewport(full);
                const float dimensions[]{float(sw), float(sh), 0, 0, 2.f / float(sw), -2.f / float(sh), float(raster_profile_ == RasterProfile::reference_dgvoodoo263), 0};
                bindings.vertex_uniform(0, dimensions);
                bindings.pipeline(pipeline({}));
                const SDL_GPUTextureSamplerBinding binding{
                    background_image ? frame.background_image->texture : frame.side_backdrop, background_image ? linear_clamp_sampler.get() : backdrop_sampler.get()};
                bindings.sampler(binding);
                const float material[]{0, 0, 200.f / 255, 1, 0, 0, 0, 0, 0, 0, 0, 0};
                bindings.fragment_uniform(material);
                SDL_DrawGPUIndexedPrimitives(pass, Uint32(background_frame.indices.size()), 1, Uint32(frame.indices.size()), Sint32(frame.vertices.size()), 0);
                ++frame_timing_.draw_calls;
            }
            // A full-window UI photograph also leaves room for the turning
            // frame outside its centered canvas. Preserve the scale/origin,
            // but clip the moving rails at the drawable instead of at 4:3.
            const bool expanded_ui = background_image;
            const SDL_GPUViewport viewport = centered_ui && !expanded_ui ? SDL_GPUViewport{transform.x, transform.y, transform.width, transform.height, 0, 1}
                                                                         : SDL_GPUViewport{0, 0, float(sw), float(sh), 0, 1};
            bindings.viewport(viewport);
            const float logical_viewport[]{expanded_ui   ? float(sw) / transform.scale
                                           : centered_ui ? 640.f
                                                         : float(sw),
                expanded_ui   ? float(sh) / transform.scale
                : centered_ui ? 480.f
                              : float(sh),
                expanded_ui ? transform.x / transform.scale : 0, expanded_ui ? transform.y / transform.scale : 0,
                2.f
                    / (expanded_ui    ? float(sw) / transform.scale
                        : centered_ui ? 640.f
                                      : float(sw)),
                -2.f
                    / (expanded_ui    ? float(sh) / transform.scale
                        : centered_ui ? 480.f
                                      : float(sh)),
                float(raster_profile_ == RasterProfile::reference_dgvoodoo263), 0};
            bindings.vertex_uniform(0, logical_viewport);
            for (std::size_t next = 0; next < frame.draws.size();) {
                const auto& item = frame.draws[next++];
                if (multisample && item.overlay != overlay_phase) {
                    continue;
                }
                const auto& state = item.state;
                auto count = item.count;
                auto instances = item.instance_count;
                const Uint32 primitive_size = state.lines ? 2 : 3;
                // Join only consecutive complete primitives with identical state.
                // Keep index order, including transparent/depth-sensitive polygons.
                while (instances == 1 && next < frame.draws.size() && count % primitive_size == 0 && frame.draws[next].count % primitive_size == 0
                       && frame.draws[next].state == state && frame.draws[next].viewport == item.viewport && frame.draws[next].vertex_offset == item.vertex_offset
                       && frame.draws[next].mesh == item.mesh && frame.draws[next].dynamic_world == item.dynamic_world && frame.draws[next].world_view == item.world_view
                       && Uint64(item.first) + count == frame.draws[next].first && frame.draws[next].model_instance == item.model_instance
                       && frame.draws[next].instance_count == item.instance_count && frame.draws[next].overlay == item.overlay
                       && Uint64(count) + frame.draws[next].count <= UINT32_MAX) {
                    count += frame.draws[next++].count;
                }
                // Preserve painter order: only adjacent identical ranges can
                // become instances, and a different material stops the group.
                while (item.model_instance != UINT32_MAX && count == item.count && next < frame.draws.size() && frame.draws[next].state == state
                       && hardware_draw(frame, frame.draws[next]) == hardware_draw(frame, item) && frame.draws[next].viewport == item.viewport
                       && frame.draws[next].mesh == item.mesh && frame.draws[next].dynamic_world == item.dynamic_world && frame.draws[next].world_view == item.world_view
                       && frame.draws[next].overlay == item.overlay && frame.draws[next].vertex_offset == item.vertex_offset && frame.draws[next].first == item.first
                       && frame.draws[next].count == count && Uint64(item.model_instance) + instances == frame.draws[next].model_instance
                       && Uint64(instances) + frame.draws[next].instance_count <= UINT32_MAX) {
                    instances += frame.draws[next++].instance_count;
                }
                const auto pane = item.viewport.value_or(Viewport{0, 0, sw, sh});
                const SDL_GPUViewport draw_viewport = item.viewport ? SDL_GPUViewport{float(pane.x), float(pane.y), float(pane.width), float(pane.height), 0, 1} : viewport;
                const SDL_Rect scissor{int(pane.x), int(pane.y), int(pane.width), int(pane.height)};
                bindings.viewport(draw_viewport);
                bindings.scissor(scissor);
                const float dimensions[]{item.viewport ? float(pane.width) : logical_viewport[0], item.viewport ? float(pane.height) : logical_viewport[1],
                    item.viewport ? 0 : logical_viewport[2], item.viewport ? 0 : logical_viewport[3], item.viewport ? 2.f / float(pane.width) : logical_viewport[4],
                    item.viewport ? -2.f / float(pane.height) : logical_viewport[5], logical_viewport[6], 0};
                if (item.mesh || item.dynamic_world) {
                    auto projection = frame.world_views.at(item.world_view);
                    projection.viewport = {dimensions[0], dimensions[1], dimensions[2], dimensions[3]};
                    projection.clip_transform = source_clip_matrix(projection);
                    projection.raster = {dimensions[4], dimensions[5], dimensions[6], 0};
                    bindings.vertex_uniform(0, projection);
                    const bool model = item.model_instance != UINT32_MAX;
                    const bool dynamic_line = item.dynamic_world && state.lines;
                    const auto mode = dynamic_line ? 2u : model ? 1u : item.dynamic_world ? 3u : 0u;
                    const std::array<Uint32, 4> model_draw{
                        mode, model ? instance_view_bases_.at(item.world_view) + item.model_instance : 0, model ? Uint32(item.mesh->vertices.size()) : 0, 0};
                    bindings.vertex_uniform(1, model_draw);
                    auto* corners = item.dynamic_world ? (dynamic_line ? dynamic_world_lines.get() : dynamic_world_vertices.get()) : item.mesh->buffer;
                    const std::array<SDL_GPUBuffer*, 4> buffers{corners, corners, model ? model_instances.get() : corners, model ? model_lighting.get() : corners};
                    bindings.storage(buffers);
                } else {
                    bindings.vertex_uniform(0, dimensions);
                }
                const bool hardware = hardware_draw(frame, item);
                bindings.pipeline(pipeline(state, use_multisample, bool(item.mesh) || item.dynamic_world, hardware, hardware && reflected_hardware_draw(frame, item)));
                // Honor texture boundaries at every mip level: the original
                // terrain UV inset alone cannot prevent coarse-mip wrapping.
                auto* selected_sampler = state.mipmapped ? (state.clamp ? mipmap_clamp_sampler.get() : mipmap_sampler.get())
                                         : state.linear  ? (state.clamp ? linear_clamp_sampler.get() : linear_sampler.get())
                                                         : (state.clamp ? nearest_clamp_sampler.get() : sampler.get());
                // Only opaque uploads receive mip chains. The key flag itself
                // is not an opacity test: original terrain also sets it on
                // completely opaque images. Vertex-faded opaque ground can
                // explicitly request the same filtering as its opaque neighbor.
                // HUD/overlays retain their existing sampling.
                const bool anisotropic = state.mipmapped && state.anisotropy != Anisotropy::off && !ui && !item.overlay;
                if (anisotropic) {
                    const auto index = state.anisotropy == Anisotropy::x4 ? 1u : 0u;
                    selected_sampler = state.clamp ? anisotropic_clamp_samplers[index].get() : anisotropic_samplers[index].get();
                }
                const SDL_GPUTextureSamplerBinding binding{state.texture ? state.texture : white.get(), selected_sampler};
                bindings.sampler(binding);
                const float material[]{0, 0, 200.f / 255, 1, float(state.key), float(state.fog), state.alpha_cutoff, float(state.palette),
                    float(raster_profile_ == RasterProfile::reference_dgvoodoo263 && state.key && (state.palette || state.reference_indexed)), float(state.linear),
                    float(state.clamp), float(state.reference_indexed)};
                bindings.fragment_uniform(material);
                if (item.mesh || item.dynamic_world) {
                    const bool quadrilateral_lines = hardware && state.lines;
                    if (quadrilateral_lines && (Uint64(item.first) + count) * 3 > UINT32_MAX) {
                        throw std::runtime_error("Reference line expansion exceeds GPU vertex addressing");
                    }
                    SDL_DrawGPUPrimitives(pass, quadrilateral_lines ? count * 3 : count, instances, quadrilateral_lines ? item.first * 3 : item.first, 0);
                    ++frame_timing_.world_draw_calls;
                    frame_timing_.dynamic_world_draw_calls += item.dynamic_world;
                    frame_timing_.dynamic_line_draw_calls += item.dynamic_world && state.lines;
                    if (item.model_instance != UINT32_MAX) {
                        ++frame_timing_.model_draw_calls;
                        frame_timing_.model_instances += instances;
                        frame_timing_.instanced_draw_calls += instances > 1;
                    }
                } else {
                    SDL_DrawGPUIndexedPrimitives(pass, count, 1, item.first, item.vertex_offset, 0);
                }
                ++frame_timing_.draw_calls;
                frame_timing_.mipmapped_draw_calls += state.mipmapped;
                frame_timing_.anisotropic_draw_calls += anisotropic;
            }
            commands.end_render();
        };
        render_pass(multisample ? multisample_color.get() : next_color.get(), false, multisample);
        if (multisample && std::ranges::any_of(frame.draws, &Draw::overlay)) {
            render_pass(next_color.get(), true, false);
        }
        if (swap) {
            SDL_GPUBlitInfo blit{};
            blit.source = {next_color.get(), 0, 0, 0, 0, sw, sh};
            blit.destination = {swap, 0, 0, 0, 0, sw, sh};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            SDL_BlitGPUTexture(cmd, &blit);
        }
        const auto submit_started = SDL_GetTicksNS();
        frame_timing_.encode_ns = submit_started - encode_started;
        check(commands.submit(), "Submit frame");
        std::swap(color, next_color);
        std::swap(color_valid, next_color_valid);
        color_valid = true;
        frame_timing_.submit_ns = SDL_GetTicksNS() - submit_started;
        return true;
    }

public:
    bool draw(const Frame& source_frame, bool ui = true) {
        return draw_internal(source_frame, ui, {});
    }

    bool draw_offscreen(const Frame& source_frame, platform::Extent extent, bool ui = false) {
        return draw_internal(source_frame, ui, extent);
    }

    std::vector<Uint32> readback() {
        const auto [width, height] = extent_;
        if (!color || width == 0 || height == 0) {
            throw std::runtime_error("No drawable frame is available for readback");
        }
        if (Uint64(width) * height > UINT32_MAX / 4) {
            throw std::runtime_error("Readback exceeds GPU byte capacity");
        }
        std::vector<Uint32> pixels(std::size_t(width) * height);
        auto download = transfer(width * height * 4, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, "readback " + std::to_string(width) + "x" + std::to_string(height));
        detail::CommandScope commands(SDL_AcquireGPUCommandBuffer(device.get()));
        auto* cmd = commands.get();
        check(cmd, "Acquire readback commands");
        auto* copy = commands.copy(SDL_BeginGPUCopyPass(cmd));
        check(copy, "Begin readback");
        const SDL_GPUTextureRegion source{color.get(), 0, 0, 0, 0, 0, width, height, 1};
        const SDL_GPUTextureTransferInfo dest{download.get(), 0, width, height};
        SDL_DownloadFromGPUTexture(copy, &source, &dest);
        commands.end_copy();
        auto fence = own(commands.submit_fenced());
        auto* raw = fence.get();
        check(SDL_WaitForGPUFences(device.get(), true, &raw, 1), "Wait for readback");
        detail::TransferMapping mapping(device.get(), download.get(), false);
        auto* rgba = static_cast<Uint8*>(mapping.get());
        check(rgba, "Map readback");
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = 0xff000000u | (Uint32(rgba[4 * i]) << 16) | (Uint32(rgba[4 * i + 1]) << 8) | rgba[4 * i + 2];
        }
        return pixels;
    }

    void save_screenshot(const std::string& path) {
        auto pixels = readback();
        const auto [width, height] = extent_;
        Surface surface(SDL_CreateSurfaceFrom(int(width), int(height), SDL_PIXELFORMAT_XRGB8888, pixels.data(), int(width * 4)), SDL_DestroySurface);
        check(bool(surface), "Create screenshot surface");
        check(SDL_SavePNG(surface.get(), path.c_str()), "Save window readback");
    }
};
} // namespace toca::gpu
