// GPU regression checks; the inspection camera is test data, not recovered game logic.
#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "image/decode.hpp"
#include "model_scene.hpp"
#include "render/gpu_renderer.hpp"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace {
using namespace toca::gpu;
constexpr int width = 640, height = 480;

void sdl_check(bool ok, const char* operation) {
    check(ok, operation);
}

#include "checks.hpp"

enum class LifetimeAction {
    end_copy,
    end_render,
    cancel,
    submit,
    fenced,
    map,
    unmap
};

struct LifetimeCommands {
    inline static std::array<LifetimeAction, 16> actions{};
    inline static std::size_t size = 0;
    inline static bool fail_submit = false;

    static void record(LifetimeAction action) {
        actions.at(size++) = action;
    }

    static void end(SDL_GPUCopyPass*) {
        record(LifetimeAction::end_copy);
    }

    static void end(SDL_GPURenderPass*) {
        record(LifetimeAction::end_render);
    }

    static bool cancel(SDL_GPUCommandBuffer*) {
        record(LifetimeAction::cancel);
        return true;
    }

    static bool submit(SDL_GPUCommandBuffer*) {
        record(LifetimeAction::submit);
        return !fail_submit;
    }

    static SDL_GPUFence* submit_fenced(SDL_GPUCommandBuffer*) {
        record(LifetimeAction::fenced);
        return fail_submit ? nullptr : reinterpret_cast<SDL_GPUFence*>(std::uintptr_t(1));
    }
};

struct LifetimeTransfer {
    inline static bool fail_map = false;

    static void* map(SDL_GPUDevice*, SDL_GPUTransferBuffer*, bool) {
        LifetimeCommands::record(LifetimeAction::map);
        return fail_map ? nullptr : reinterpret_cast<void*>(std::uintptr_t(1));
    }

    static void unmap(SDL_GPUDevice*, SDL_GPUTransferBuffer*) {
        LifetimeCommands::record(LifetimeAction::unmap);
    }
};

void verify_resource_lifetimes() {
    using Commands = detail::CommandScope<LifetimeCommands>;
    const auto command = reinterpret_cast<SDL_GPUCommandBuffer*>(std::uintptr_t(1));
    const auto copy = reinterpret_cast<SDL_GPUCopyPass*>(std::uintptr_t(2));
    const auto render = reinterpret_cast<SDL_GPURenderPass*>(std::uintptr_t(3));
    const auto expect_actions = [](std::initializer_list<LifetimeAction> expected) {
        check(LifetimeCommands::size == expected.size() && std::equal(expected.begin(), expected.end(), LifetimeCommands::actions.begin()),
            "GPU error cleanup must end active passes and consume commands exactly once");
        LifetimeCommands::size = 0;
    };
    {
        Commands commands(nullptr);
    }
    expect_actions({});
    {
        Commands commands(command);
        commands.copy(nullptr); // A failed BeginCopyPass still leaves owned commands.
    }
    expect_actions({LifetimeAction::cancel});
    try {
        Commands commands(command);
        commands.copy(copy);
        throw std::bad_alloc();
    } catch (const std::bad_alloc&) {
    }
    expect_actions({LifetimeAction::end_copy, LifetimeAction::cancel});
    try {
        Commands commands(command);
        commands.acquired_swapchain();
        commands.render(render);
        throw std::bad_alloc();
    } catch (const std::bad_alloc&) {
    }
    expect_actions({LifetimeAction::end_render, LifetimeAction::submit});
    {
        Commands commands(command);
        commands.acquired_swapchain(); // Resource failure before starting a pass.
    }
    expect_actions({LifetimeAction::submit});
    LifetimeCommands::fail_submit = true;
    {
        Commands commands(command);
        commands.acquired_swapchain();
        check(!commands.submit(), "Submission failure fixture");
    }
    expect_actions({LifetimeAction::submit});
    {
        Commands commands(command);
        commands.copy(copy);
        commands.end_copy();
        check(!commands.submit_fenced(), "Fenced submission failure fixture");
    }
    expect_actions({LifetimeAction::end_copy, LifetimeAction::fenced});
    LifetimeCommands::fail_submit = false;
    try {
        detail::TransferMapping<LifetimeTransfer> mapped(nullptr, nullptr, false);
        check(mapped.get() != nullptr, "Mapped readback fixture");
        throw std::bad_alloc(); // Deterministic post-map allocation failure, without host OOM.
    } catch (const std::bad_alloc&) {
    }
    expect_actions({LifetimeAction::map, LifetimeAction::unmap});
    LifetimeTransfer::fail_map = true;
    {
        detail::TransferMapping<LifetimeTransfer> mapped(nullptr, nullptr, false);
        check(mapped.get() == nullptr, "Failed mapping fixture");
    }
    expect_actions({LifetimeAction::map});
    LifetimeTransfer::fail_map = false;
    for (const bool existing : {false, true}) {
        auto first = existing ? std::make_unique<int>(1) : nullptr;
        auto second = existing ? std::make_unique<int>(2) : nullptr;
        const auto* original_first = first.get();
        const auto* original_second = second.get();
        try {
            detail::replace_pair(
                first, second,
                [] {
                    return std::make_unique<int>(3);
                },
                []() -> std::unique_ptr<int> {
                    throw std::bad_alloc();
                });
        } catch (const std::bad_alloc&) {
        }
        check(first.get() == original_first && second.get() == original_second, "Failed color/depth pair allocation must retain both previous resources");
        detail::replace_pair(
            first, second,
            [] {
                return std::make_unique<int>(3);
            },
            [] {
                return std::make_unique<int>(4);
            });
        check(*first == 3 && *second == 4, "Color/depth allocation must recover after either empty or populated pair failure");
    }
    std::cout << "PASS: failed GPU passes, submissions, mappings and paired allocations retain valid ownership\n";
}

Texture palette_texture(Renderer& gpu, bool black_key = false) {
    Surface indexed(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
    check(bool(indexed), "Create indexed surface");
    auto* palette = SDL_CreateSurfacePalette(indexed.get());
    check(palette, "Create palette");
    const std::array<SDL_Color, 4> colors = black_key ? std::array<SDL_Color, 4>{{{0, 200, 0, 255}, {0, 0, 0, 255}, {1, 1, 1, 255}, {0, 0, 0, 255}}}
                                                      : std::array<SDL_Color, 4>{{{255, 0, 255, 255}, {0, 200, 0, 255}, {0, 0, 200, 255}, {240, 240, 240, 255}}};
    check(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()), "Set palette");
    auto* indices = static_cast<Uint8*>(indexed->pixels);
    indices[0] = 0;
    indices[1] = 1;
    indices[indexed->pitch] = 2;
    indices[indexed->pitch + 1] = 3;
    if (!black_key) {
        check(SDL_SetSurfaceColorKey(indexed.get(), true, 0), "Set color key");
    } else {
        // This fixture exercises the RGB fallback, independently of P8 index keys.
        Surface rgba(SDL_ConvertSurface(indexed.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        check(bool(rgba), "Convert RGB black-key fixture");
        return gpu.upload(rgba.get(), true);
    }
    return gpu.upload(indexed.get(), black_key);
}

Frame checks_frame(SDL_GPUTexture* palette) {
    Frame f;
    f.quad({.texture = palette, .key = true}, 20, 20, 160, 160);
    f.quad({.depth = true}, 220, 20, 160, 160, 0xff00c800, .2f);
    f.quad({.depth = true}, 220, 20, 160, 160, 0xffc80000, .8f);
    f.quad({}, 420, 20, 160, 160, 0xff0000c8);
    f.quad({.blend = true}, 420, 20, 160, 160, 0x80c80000);
    f.quad({.fog = true}, 20, 240, 80, 160, 0xffc80000, .5f, 0);
    f.quad({.fog = true}, 100, 240, 80, 160, 0xffc80000, .5f, 0x80000000);
    f.quad({.fog = true}, 220, 240, 160, 160, 0xffc80000);
    const auto base = Uint16(f.vertices.size());
    f.draws.push_back({{}, Uint32(f.indices.size()), 3});
    f.vertices.insert(f.vertices.end(), {{420, 400, .5f, 1, 0xfff0c800, 0, 0, 0}, {500, 240, .5f, 1, 0xfff0c800, 0, 0, 0}, {580, 400, .5f, 1, 0xfff0c800, 0, 0, 0}});
    for (const auto i : {0, 1, 2}) {
        f.indices.push_back(Uint16(base + i));
    }
    return f;
}

void verify_buffer_growth(Renderer& gpu) {
    Frame baseline;
    baseline.quad({}, 20, 20, 160, 160, 0xffc80000);
    baseline.quad({}, 220, 240, 160, 160, 0xff0000c8);
    check(gpu.draw(baseline), "Draw initial buffer contents");
    const auto expected = gpu.readback();
    const auto rejects = [&](auto operation) {
        bool rejected = false;
        try {
            operation();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "Invalid GPU operation must reject before recording commands");
        check(gpu.draw(baseline) && gpu.readback() == expected, "A rejected GPU operation must allow the next valid frame with exact pixels");
    };
    rejects([&] {
        gpu.draw_offscreen(baseline, {0, height});
    });
    const Uint32 pixel = 0xffffffff;
    rejects([&] {
        gpu.update(nullptr, &pixel, UINT32_MAX, UINT32_MAX);
    });
    expect(expected, 50, 50, 0xc80000, "initial vertex/index upload");
    expect(expected, 300, 300, 0x0000c8, "initial vertex/index upload");
    const auto verify = [&](const Frame& frame, const char* message) {
        check(gpu.draw(frame), message);
        check(gpu.readback() == expected, message);
        return gpu.last_frame_timing();
    };

    // Referenced vertices move beyond the initial buffer, while indices retain
    // their size. Unused prefix vertices must not affect the rendered content.
    auto vertex_growth = baseline;
    vertex_growth.vertices.insert(vertex_growth.vertices.begin(), 1024, Vertex{});
    for (auto& draw : vertex_growth.draws) {
        draw.vertex_offset = 1024;
    }
    const auto vertex_allocated = verify(vertex_growth, "Vertex-only buffer growth must preserve both quads");
    auto vertex_fits = vertex_growth;
    vertex_fits.vertices.insert(vertex_fits.vertices.begin(), 16, Vertex{});
    for (auto& draw : vertex_fits.draws) {
        draw.vertex_offset += 16;
    }
    const auto vertex_reused = verify(vertex_fits, "A slightly larger vertex upload must preserve both quads");
    check(
        vertex_reused.buffer_growths == 0 && vertex_reused.vertex_capacity == vertex_allocated.vertex_capacity, "Fitting vertex uploads must reuse their GPU and staging capacity");

    // Move the referenced indices beyond their old capacity without growing the
    // vertex buffer. The upload offset uses this frame's smaller vertex count.
    auto index_growth = baseline;
    index_growth.indices.insert(index_growth.indices.begin(), 12288, 0);
    for (auto& draw : index_growth.draws) {
        draw.first += 12288;
    }
    const auto index_allocated = verify(index_growth, "Index-only buffer growth must preserve both quads");
    auto index_fits = index_growth;
    index_fits.indices.insert(index_fits.indices.end(), 16, 0);
    const auto index_reused = verify(index_fits, "A slightly larger index upload must preserve both quads");
    check(index_reused.buffer_growths == 0 && index_reused.index_capacity == index_allocated.index_capacity, "Fitting index uploads must reuse their GPU and staging capacity");

    // Both GPU buffers now fit individually, but their combined upload exceeds
    // earlier frames. Only staging grows; later small frames still pack tightly.
    auto staging_growth = index_growth;
    staging_growth.vertices = vertex_growth.vertices;
    staging_growth.vertices.resize(index_reused.vertex_capacity / sizeof(Vertex));
    staging_growth.indices.resize(index_reused.index_capacity / sizeof(Uint16));
    for (auto& draw : staging_growth.draws) {
        draw.vertex_offset = 1024;
    }
    const auto staging_allocated = verify(staging_growth, "Combined staging growth must preserve both quads");
    check(staging_allocated.vertex_capacity == index_reused.vertex_capacity && staging_allocated.index_capacity == index_reused.index_capacity
              && staging_allocated.staging_capacity > index_reused.staging_capacity,
        "Combined fitting GPU buffers must grow only their packed staging upload");
    verify(baseline, "Smaller uploads must retain their actual vertex/index offsets");
    const auto retained = verify(staging_growth, "Returning to a large upload must retain pixels and buffer capacity");
    check(retained.buffer_growths == 0 && retained.vertex_capacity == staging_allocated.vertex_capacity && retained.index_capacity == staging_allocated.index_capacity
              && retained.staging_capacity == staging_allocated.staging_capacity,
        "Large-small-large uploads must retain their independent high-water capacities");
    std::cout << "PASS: independent vertex/index growth, combined staging growth and smaller uploads preserve GPU pixels\n";
}

void verify_binding_reuse(Renderer& gpu, SDL_GPUTexture* texture) {
    Frame frame;
    for (int index = 0; index < 4; ++index) {
        frame.quad({.texture = texture, .key = true}, 20.f + index * 100, 20, 80, 80);
        frame.indices.push_back(0); // Keep identical state in separate draw calls.
    }
    check(gpu.draw(frame, false), "Draw repeated pass bindings");
    const auto expected = gpu.readback();
    const auto counts = gpu.last_frame_timing().bindings;
    check(gpu.last_frame_timing().draw_calls == 4 && counts.pipelines == 1 && counts.viewports == 1 && counts.scissors == 1 && counts.samplers == 1 && counts.vertex_uniforms == 1
              && counts.fragment_uniforms == 1 && counts.storage_buffers == 0,
        "Separate identical draws must share actual pass-local GPU bindings");
    for (int index = 0; index < 4; ++index) {
        expect(expected, 80 + index * 100, 40, 0x00c800, "repeated texture binding retains every quad");
    }
    check(
        gpu.draw(frame, false) && gpu.readback() == expected && gpu.last_frame_timing().bindings == counts, "A new render pass must restore its initial bindings and exact pixels");
    std::cout << "PASS: separate draws reuse GPU bindings and restore them for every new pass\n";
}

void verify_draw_runs(Renderer& gpu, SDL_GPUTexture* palette) {
    auto frame = checks_frame(palette);
    // Overlapping translucent polygons must retain primitive order when joined.
    for (unsigned index = 0; index < 12; ++index) {
        frame.quad({.blend = true}, 160 + index * 5, 130 + index * 3, 180, 180, index % 2 ? 0x6000ff00 : 0x60ff0000);
    }
    for (const auto state : {State{.texture = palette, .key = true}, State{.texture = palette, .linear = true}, State{.wire = true}, State{.blend = true, .screen = true}}) {
        frame.quad(state, 50, 60, 100, 100, 0x80ffffff);
        frame.quad(state, 80, 90, 100, 100, 0x80ffffff);
    }
    frame.draws.push_back({{.lines = true}, 0, 2});
    frame.draws.push_back({{.lines = true}, 2, 2});
    frame.draws.push_back({{.lines = true}, 6, 2}); // Deliberately noncontiguous.
    gpu.draw(frame);
    const auto joined = gpu.readback();

    auto separate = frame;
    separate.indices.clear();
    for (auto& draw : separate.draws) {
        const auto first = draw.first;
        draw.first = Uint32(separate.indices.size());
        separate.indices.insert(separate.indices.end(), frame.indices.begin() + first, frame.indices.begin() + first + draw.count);
        // Unused indices separate every draw without changing its primitives.
        separate.indices.push_back(0);
    }
    gpu.draw(separate);
    check(joined == gpu.readback(), "Joined draw runs must match separate draws exactly");
    std::cout << "PASS: joined draw runs preserve overlapping blend, state changes and index gaps\n";
}

struct ModelScene : toca::ModelSceneData {
    std::vector<Texture> textures;

    ModelScene(Renderer& gpu, toca::Model decoded) : ModelSceneData(std::move(decoded)) {
        for (const auto& t : model.textures) {
            auto bitmap = toca::decode_bmp(t.bmp);
            textures.push_back(gpu.upload(bitmap.get(), true));
        }
    }

    Frame frame(float yaw, bool wire, bool key) const {
        Frame f;
        const auto eye = center + glm::normalize(glm::vec3(1.5f, .85f, 2)) * radius * 2.8f;
        const auto vp = glm::perspectiveLH_ZO(glm::radians(40.f), float(width) / height, radius * .01f, radius * 10) * glm::lookAtLH(eye, center, glm::vec3(0, 1, 0));
        const auto rotation = glm::translate(glm::mat4(1), center) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1), -center);
        for (size_t n = 0; n < model.nodes.size(); ++n) {
            const auto& node = model.nodes[n];
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto mesh_index = node.variants.front();
            const auto& mesh = model.meshes[mesh_index];
            const auto world = rotation * transforms[n];
            for (const auto& batch : batches[mesh_index]) {
                if (f.vertices.size() + mesh.vertices.size() > 65536) {
                    throw std::runtime_error("Inspection frame exceeds 16-bit indices");
                }
                const auto base = Uint16(f.vertices.size());
                f.draws.push_back({{batch.texture < 0 ? nullptr : textures.at(batch.texture).get(), true, false, key && batch.texture >= 0, false, wire}, Uint32(f.indices.size()),
                    Uint32(batch.indices.size())});
                for (const auto i : batch.indices) {
                    f.indices.push_back(Uint16(base + i));
                }
                for (const auto& v : mesh.vertices) {
                    const auto clip = vp * world * glm::vec4(glm::make_vec3(v.position.data()), 1);
                    const auto normal = glm::normalize(glm::mat3(world) * glm::make_vec3(v.normal.data()));
                    const auto light = std::clamp(112.f / 255 + .8f * std::max(0.f, glm::dot(normal, glm::normalize(glm::vec3(.4f, .8f, .6f)))), 0.f, 1.f);
                    Uint32 color = batch.color & 0xff000000;
                    for (const int shift : {0, 8, 16}) {
                        color |= Uint32(std::lround(((batch.color >> shift) & 255) * light)) << shift;
                    }
                    f.vertices.push_back(
                        {(clip.x / clip.w + 1) * width * .5f - .5f, (1 - clip.y / clip.w) * height * .5f - .5f, clip.z / clip.w, 1 / clip.w, color, 0xff000000, v.u, v.v});
                }
            }
        }
        return f;
    }
};

void verify_black_key(Renderer& gpu) {
    auto texture = palette_texture(gpu, true);
    Frame f;
    f.quad({.texture = texture.get(), .depth = true, .key = true}, 20, 20, 160, 160, 0xffffffff, .2f);
    f.quad({.depth = true}, 20, 20, 160, 160, 0xff0000c8, .8f);
    gpu.draw(f);
    const auto p = gpu.readback();
    expect(p, 50, 50, 0x00c800, "nonblack palette index zero");
    expect(p, 150, 50, 0x0000c8, "black texel must not occlude later geometry");
    expect(p, 50, 150, 0x010101, "near-black stays opaque");
    expect(p, 150, 150, 0x0000c8, "duplicate black palette entry");
    std::cout << "PASS: RGB black key, duplicate palette entries, near-black and depth\n";
}

void verify_source_interpolation(Renderer& gpu) {
    // Direct3D2/dgVoodoo TL diagnostic: xy=(32,32),(608,32),(32,448),
    // RHW=(1,1/4,1/16). Presented RGB8, rather than its converted RGB565 lock.
    Frame frame;
    frame.vertices = {{32, 32, .5f, 1, 0xffff0000, 0, 0, 0}, {608, 32, .5f, .25f, 0xff00ff00, 0, 0, 0}, {32, 448, .5f, .0625f, 0xff0000ff, 0, 0, 0}};
    frame.indices = {0, 1, 2};
    frame.draws.push_back({{}, 0, 3});
    check(gpu.draw(frame), "Draw source varying-RHW diffuse triangle");
    const auto diffuse = gpu.readback();
    expect(diffuse, 176, 136, 0xdd1c07, "source perspective diffuse and visible RGB8 precision");
    expect(diffuse, 320, 136, 0xa3520a, "source perspective diffuse across the triangle");
    expect(diffuse, 176, 240, 0xb92e17, "source perspective diffuse along the triangle");

    frame.clear_color = {0, 0, 1, 1};
    frame.draws.front().state.blend = true;
    for (unsigned index = 0; index < frame.vertices.size(); ++index) {
        frame.vertices[index].diffuse = (Uint32(index == 0 ? 0 : index == 1 ? 128 : 255) << 24) | 0x00ffffff;
    }
    check(gpu.draw(frame), "Draw source perspective diffuse alpha");
    const auto alpha = gpu.readback();
    expect(alpha, 176, 136, 0x1515ff, "source perspective diffuse alpha over blue");
    expect(alpha, 320, 136, 0x3333ff, "source perspective alpha across the triangle");
    expect(alpha, 176, 240, 0x2e2eff, "source perspective alpha along the triangle");

    frame.draws.front().state = {.fog = true};
    for (auto& vertex : frame.vertices) {
        vertex.specular = vertex.diffuse & 0xff000000;
        vertex.diffuse = 0xff000000;
    }
    check(gpu.draw(frame), "Draw source perspective specular fog factor");
    // The diagnostic's white fog gives234; the adapter's fixed blue200 gives184.
    expect(gpu.readback(), 176, 136, 0x0000b8, "source perspective specular-alpha fog interpolation");
    std::cout << "PASS: source perspective diffuse, material alpha, fog and unquantized visible RGB8\n";
}

void verify_indexed_key(Renderer& gpu) {
    Surface indexed(SDL_CreateSurface(4, 1, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
    check(bool(indexed), "Create raw P8 key fixture");
    auto* palette = SDL_CreateSurfacePalette(indexed.get());
    check(palette, "Create raw P8 palette");
    const std::array<SDL_Color, 4> colors{{{8, 8, 8, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {1, 1, 1, 255}}};
    check(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()), "Set raw P8 colors");
    const std::array<Uint8, 4> indices{0, 1, 2, 3};
    std::copy(indices.begin(), indices.end(), static_cast<Uint8*>(indexed->pixels));
    auto texture = gpu.upload(indexed.get(), true);
    Frame frame;
    frame.clear_color = {0, 0, 1, 1};
    frame.quad({.texture = texture.get(), .depth = true, .blend = true, .key = true, .palette = true}, 20, 20, 320, 80, 0xffffffff, .2f);
    frame.quad({.depth = true}, 20, 20, 320, 80, 0xff0000ff, .8f);
    frame.quad({.texture = texture.get(), .blend = true, .palette = true}, 20, 140, 320, 80);
    check(gpu.draw(frame), "Draw source P8 index key and key-disabled material");
    const auto result = gpu.readback();
    expect(result, 60, 60, 0x0000ff, "nonblack P8 index zero must not occlude later geometry");
    expect(result, 140, 60, 0, "nonzero black P8 index stays opaque");
    expect(result, 220, 60, 0, "duplicate nonzero black P8 index stays opaque");
    expect(result, 300, 60, 0x010101, "near-black nonzero P8 index stays opaque");
    expect(result, 60, 180, 0x080808, "disabled P8 key retains index-zero RGB and material opacity");
    std::cout << "PASS: raw P8 index-zero key, duplicate black indices, near-black and disabled key\n";

    Surface fringe(SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
    check(bool(fringe), "Create source P8 filtering fixture");
    auto* fringe_palette = SDL_CreateSurfacePalette(fringe.get());
    check(fringe_palette, "Create source P8 filtering palette");
    const std::array<SDL_Color, 2> fringe_colors{{{8, 8, 8, 255}, {255, 0, 0, 255}}};
    check(SDL_SetPaletteColors(fringe_palette, fringe_colors.data(), 0, fringe_colors.size()), "Set source P8 filtering colors");
    for (int row = 0; row < fringe->h; ++row) {
        auto* pixels = static_cast<Uint8*>(fringe->pixels) + row * fringe->pitch;
        std::copy_n(std::array<Uint8, 4>{0, 0, 1, 1}.begin(), 4, pixels);
    }
    auto filtered = gpu.upload(fringe.get(), true);
    Frame source;
    source.clear_color = {0, 0, 1, 1};
    source.quad({.texture = filtered.get(), .blend = true, .key = true, .clamp = true, .palette = true}, 32.5f, 32.5f, 576, 416);
    check(gpu.draw(source), "Draw source point-filtered P8 diagnostic");
    const auto point = gpu.readback();
    expect(point, 176, 136, 0x0000ff, "source point P8 key region");
    expect(point, 388, 200, 0xff0000, "source point P8 opaque region");
    source.draws.front().state.linear = true;
    check(gpu.draw(source), "Draw source linearly filtered P8 diagnostic");
    const auto linear = gpu.readback();
    expect(linear, 176, 136, 0x0000ff, "source linear P8 exact-key region");
    expect(linear, 388, 200, 0xf80000, "source linear P8 fringe retains full diffuse opacity");

    Frame materials;
    materials.clear_color = {0, 0, 1, 1};
    for (unsigned row = 0; row < 2; ++row) {
        const auto first = materials.vertices.size();
        materials.quad({.texture = filtered.get(), .blend = true, .key = true, .linear = true, .clamp = true, .palette = true}, 20, 20.f + row * 100, 80, 80,
            row == 0 ? 0xffffffff : 0x80ffffff);
        for (auto vertex = first; vertex < materials.vertices.size(); ++vertex) {
            // Quarter-way from key RGB(8,8,8) to red: RGB=(69.75,6,6).
            // Constant UV avoids wrapper pixel-bias/half-tie boundary differences.
            materials.vertices[vertex].u = .4375f;
            materials.vertices[vertex].v = .5f;
        }
    }
    check(gpu.draw(materials), "Draw P8 filtered key-mask with independent diffuse alpha");
    const auto opacity = gpu.readback();
    expect(opacity, 60, 60, 0x460606, "P8 filtered key-mask must not reduce opaque material alpha");
    expect(opacity, 60, 160, 0x230382, "P8 filtered key-mask preserves half-opacity diffuse material");
    std::cout << "PASS: source point/linear P8 filtering and independent diffuse material opacity\n";
}

void verify_reference_raster_filtering(Renderer& native_gpu) {
    const auto render_vectors = [](Renderer& gpu) {
        Surface indexed(SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
        check(bool(indexed), "Create raster-profile indexed fixture");
        auto* palette = SDL_CreateSurfacePalette(indexed.get());
        check(palette, "Create raster-profile palette");
        const std::array<SDL_Color, 2> colors{{{8, 8, 8, 255}, {255, 0, 0, 255}}};
        check(SDL_SetPaletteColors(palette, colors.data(), 0, colors.size()), "Set raster-profile colors");
        for (int row = 0; row < indexed->h; ++row) {
            auto* pixels = static_cast<Uint8*>(indexed->pixels) + row * indexed->pitch;
            std::copy_n(std::array<Uint8, 4>{0, 0, 1, 1}.begin(), 4, pixels);
        }
        auto indexed_texture = gpu.upload(indexed.get(), true);
        Surface rgba(SDL_ConvertSurface(indexed.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        check(bool(rgba), "Create raster-profile ordinary RGBA fixture");
        auto rgba_texture = gpu.upload(rgba.get(), true);
        const std::array<State, 7> states{{
            {.texture = indexed_texture.get(), .key = true, .clamp = true, .palette = true},
            {.texture = indexed_texture.get(), .key = true, .linear = true, .clamp = true, .palette = true},
            {.texture = indexed_texture.get(), .clamp = true, .palette = true},
            {.texture = indexed_texture.get(), .linear = true, .clamp = true, .palette = true},
            {.texture = rgba_texture.get(), .key = true, .linear = true, .clamp = true},
            {.texture = indexed_texture.get(), .key = true, .clamp = true, .palette = true},
            {.texture = indexed_texture.get(), .key = true, .linear = true, .clamp = true, .reference_indexed = true},
        }};
        Frame frame;
        frame.clear_color = {0, 0, 1, 1};
        for (unsigned row = 0; row < states.size(); ++row) {
            const auto first = frame.vertices.size();
            frame.quad(states[row], 10, 4.f + row * 16, 80, 12);
            for (auto vertex = first; vertex < frame.vertices.size(); ++vertex) {
                // Constant UV isolates filtering from the profiles' TL conventions.
                // The reference linear value stays clear of the final UNORM tie.
                frame.vertices[vertex].u = row == 5 ? .875f : states[row].linear ? .5002f : .49999f;
                frame.vertices[vertex].v = .5f;
            }
        }
        check(gpu.draw_offscreen(frame, {width, height}), "Draw raster-profile filter vectors");
        return gpu.readback();
    };
    const auto native = render_vectors(native_gpu);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA reference raster checks", width, height, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    check(bool(window), "Create reference raster window");
    Renderer reference_gpu(window.get(), RasterProfile::reference_dgvoodoo263);
    const auto reference = render_vectors(reference_gpu);
    expect(native, 40, 8, 0x0000ff, "native point P8 retains the keyed texel");
    expect(reference, 40, 8, 0xff0000, "reference point P8 uses the captured texel phase");
    expect(reference, 40, 24, 0x840404, "reference linear P8 filters palette bytes before normalization");
    // Native linear export can round its UNORM half-tie differently by backend.
    // Bypass cases must instead match exactly between profiles on that backend.
    for (const unsigned row : {2u, 3u, 4u}) {
        const auto pixel = (8 + row * 16) * width + 40;
        check(reference.at(pixel) == native.at(pixel), "Reference raster must preserve key-off sky and ordinary RGBA sampling");
    }
    expect(native, 40, 88, 0xff0000, "native point P8 opaque texel");
    expect(reference, 40, 88, 0xff0000, "reference point P8 opaque texel");
    expect(reference, 40, 104, 0x840404, "reconstructed original P8 names use the reference byte filter");
    check(native.at(104 * width + 40) == native.at(24 * width + 40), "Native raster ignores reconstructed indexed metadata");
    std::cout << "PASS: seven filter cases under native and explicit reference raster profiles\n";
}

void verify_authored_cutout(Renderer& gpu) {
    auto texture = gpu.texture(4, 1);
    const Uint8 pixels[]{200, 20, 10, 1, 200, 20, 10, 127, 200, 20, 10, 128, 200, 20, 10, 255};
    gpu.update(texture.get(), pixels, 4, 1);
    Frame frame;
    frame.quad({.texture = texture.get(), .depth = true, .key = true}, 20, 20, 320, 80, 0xffffffff, .2f);
    frame.quad({.depth = true}, 20, 20, 320, 80, 0xff0000c8, .8f);
    frame.quad({.texture = texture.get(), .depth = true, .key = true, .alpha_cutoff = .5f}, 20, 140, 320, 80, 0xffffffff, .2f);
    frame.quad({.depth = true}, 20, 140, 320, 80, 0xff0000c8, .8f);
    frame.quad({.texture = texture.get(), .depth = true, .key = true}, 20, 260, 320, 80, 0xffffffff, .2f);
    frame.quad({.depth = true}, 20, 260, 320, 80, 0xff0000c8, .8f);
    gpu.draw(frame);
    const auto result = gpu.readback();
    for (const int x : {60, 140, 220, 300}) {
        expect(result, x, 60, 0xc8140a, "Original key must retain every nonzero alpha value");
        expect(result, x, 300, 0xc8140a, "Original key must be restored after an authored cutout draw");
    }
    expect(result, 60, 180, 0x0000c8, "Authored alpha1 fringe must not color or occlude later world geometry");
    expect(result, 140, 180, 0x0000c8, "Authored alpha127 must fall below the half-alpha cutout threshold");
    expect(result, 220, 180, 0xc8140a, "Authored alpha128 must retain the intended foliage silhouette");
    expect(result, 300, 180, 0xc8140a, "Authored opaque foliage must retain its RGB material");
    std::cout << "PASS: authored half-alpha cutout, original exact-zero key and depth-write masking\n";
}

void verify_filtering(Renderer& gpu) {
    auto texture = gpu.texture(2, 2);
    const Uint8 pixels[]{0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    gpu.update(texture.get(), pixels, 2, 2);
    Frame frame;
    for (const bool linear : {false, true, false}) {
        const auto x = 20.f + float(frame.draws.size()) * 100;
        const auto first = frame.vertices.size();
        frame.quad({.texture = texture.get(), .linear = linear}, x, 20, 80, 80);
        for (auto index = first; index < frame.vertices.size(); ++index) {
            frame.vertices[index].u = .375f;
            frame.vertices[index].v = .5f;
        }
    }
    gpu.draw(frame);
    const auto result = gpu.readback();
    expect(result, 50, 50, 0, "point sampler");
    expect(result, 150, 50, 0x404040, "bilinear sampler quarter-texel interpolation", 1);
    expect(result, 250, 50, 0, "point sampler restored after ground filtering");
    std::cout << "PASS: per-draw source point/bilinear sampling and restoration\n";

    auto edges = gpu.texture(2, 2);
    const Uint8 edge_pixels[]{200, 0, 0, 255, 0, 200, 0, 255, 0, 0, 200, 255, 240, 240, 240, 255};
    gpu.update(edges.get(), edge_pixels, 2, 2);

    struct AddressProbe {
        float u, v;
        Uint32 repeated, clamped;
    };

    constexpr std::array probes{AddressProbe{-.25f, .25f, 0x00c800, 0xc80000}, AddressProbe{1.25f, .25f, 0xc80000, 0x00c800}, AddressProbe{.25f, -.25f, 0x0000c8, 0xc80000},
        AddressProbe{.25f, 1.25f, 0xc80000, 0x0000c8}};
    Frame addressing;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < probes.size(); ++column) {
            const auto first = addressing.vertices.size();
            addressing.quad({.texture = edges.get(), .clamp = row == 1}, 20.f + column * 100, 20.f + row * 100, 80, 80);
            for (auto vertex = first; vertex < addressing.vertices.size(); ++vertex) {
                addressing.vertices[vertex].u = probes[column].u;
                addressing.vertices[vertex].v = probes[column].v;
            }
        }
    }
    check(gpu.draw(addressing), "Draw nearest texture address modes");
    const auto addressed = gpu.readback();
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < probes.size(); ++column) {
            expect(addressed, int(50 + column * 100), int(50 + row * 100), row == 1 ? probes[column].clamped : probes[column].repeated,
                "Nearest sampling must clamp all four texture edges and restore repeat addressing");
        }
    }
    std::cout << "PASS: nearest U/V edge clamping and repeat restoration\n";
}

void verify_upload_burst(Renderer& gpu) {
    constexpr std::size_t count = 1200;
    constexpr std::array<Uint32, 3> sizes{8, 32, 64};
    std::vector<Texture> textures;
    textures.reserve(count);
    Frame frame;
    const auto color = [](std::size_t index) {
        return std::array<Uint8, 4>{Uint8(index + 17), Uint8((index >> 8) + 19), Uint8(index * 37 + 23), 255};
    };
    for (std::size_t index = 0; index < count; ++index) {
        // Grow every upload slot, then alternate smaller/larger copies without
        // presenting. Keep every destination alive to detect staging overwrite.
        const auto size = sizes[index < 9 ? index / 3 : index % 3];
        const auto rgba = color(index);
        std::vector<Uint8> pixels(size * size * 4);
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            std::copy(rgba.begin(), rgba.end(), pixels.begin() + pixel);
        }
        textures.push_back(gpu.texture(size, size));
        gpu.update(textures.back().get(), pixels.data(), size, size);
        frame.quad({.texture = textures.back().get()}, float(index % 40) * 16, float(index / 40) * 16, 16, 16);
    }
    const Uint8 orange[]{255, 128, 0, 255};
    gpu.update(textures.front().get(), orange, 1, 1);
    gpu.draw(frame);
    const auto result = gpu.readback();
    for (std::size_t index = 0; index < count; ++index) {
        const auto rgba = color(index);
        const auto rgb = Uint32(rgba[0]) << 16 | Uint32(rgba[1]) << 8 | rgba[2];
        expect(result, int(index % 40) * 16 + 8, int(index / 40) * 16 + 8, rgb, "texture upload burst preserves each destination");
    }
    expect(result, 0, 0, 0xff8000, "partial update after texture upload burst");
    std::cout << "PASS:1200 texture uploads before presentation, changing sizes and partial update preserve GPU pixels\n";
}

void verify_pipeline_preparation(Renderer& gpu) {
    Frame loading;
    loading.quad({}, 0, 0, width, height, 0xff204080);
    check(gpu.draw(loading), "Present loading before world pipeline preparation");

    Frame scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto projection = scene.world_view({identity, {1, 1, 1, 16}, {16.f / 15, -16.f / 15, width, height}});
    const auto mesh = gpu.world_mesh({{{-2.8f, 2.6f, 4}, 0xff00c800, 0, 0}, {{2.6f, 2, 4}, 0xff00c800, 0, 0}, {{-.6f, -2.8f, 4}, 0xff00c800, 0, 0}});
    scene.draws.push_back({{.depth = true}, 0, 6, 0, {}, false, mesh, projection});
    scene.multisample = true;
    const auto overlay = scene.draws.size();
    scene.quad({}, 20, 20, 40, 40);
    scene.mark_overlay(overlay);

    const auto caller = SDL_GetCurrentThreadID();
    std::size_t serviced = 0;
    const auto service = [&] {
        check(SDL_GetCurrentThreadID() == caller, "Loading service must stay on the presentation thread");
        check(gpu.draw(loading), "Keep loading visible during world pipeline preparation");
        ++serviced;
    };
    gpu.prepare_pipelines(scene, false, service);
    check(serviced != 0, "Uncached world pipeline preparation must service loading");
    expect(gpu.readback(), width / 2, height / 2, 0x204080, "Pipeline preparation must retain the loading image");
    check(gpu.draw(scene, false), "Present prepared world pipelines");
    const auto pixels = gpu.readback();
    expect(pixels, width / 2, height / 2, 0x00c800, "Prepared world pipeline triangle");
    expect(pixels, 40, 40, 0xffffff, "Prepared single-sample overlay over scene MSAA");
    gpu.prepare_pipelines(scene, false, [] {
        throw std::runtime_error("Cached pipelines must not start preparation work");
    });

    // A callback interruption must join compilation and release uncommitted
    // pipelines before the caller can resume using or destroy the renderer.
    scene.draws.front().state.depth_bias = true;
    bool interrupted = false;
    try {
        gpu.prepare_pipelines(scene, false, [] {
            throw std::runtime_error("Stop pipeline preparation");
        });
    } catch (const std::runtime_error& failure) {
        interrupted = std::string_view(failure.what()) == "Stop pipeline preparation";
    }
    check(interrupted, "Pipeline preparation must preserve callback errors");
    gpu.prepare_pipelines(scene, false, service);
    check(gpu.draw(scene, false) && gpu.readback() == pixels, "Interrupted preparation must leave the renderer reusable with exact pixels");
    std::cout << "PASS: world pipeline preparation services loading, retains MSAA overlays and joins on callback errors\n";
}

void verify_color_history(Renderer& gpu) {
    const auto pixel = [](const std::vector<Uint32>& image, int w, int x, int y, Uint32 rgb, const char* message) {
        check((image.at(std::size_t(y) * w + x) & 0xffffff) == rgb, message);
    };
    const auto frame = [](Uint32 ink, SDL_FColor background, bool sampled = false) {
        Frame result;
        result.color_load = ColorLoad::preserve;
        result.clear_color = background;
        result.multisample = sampled;
        result.quad({.depth = true}, 4, 4, 12, 12, ink, .1f);
        return result;
    };
    for (const bool sampled : {false, true}) {
        // A new extent resets BOTH targets; allocating the second target must
        // never reset the first target's initialized content or rotation.
        const toca::platform::Extent size{sampled ? 66u : 64u, 48};
        auto a = frame(0xffff0000, {0, 1, 0, 1}, sampled);
        a.quad({}, 24, 4, 12, 12, 0xffffff00);
        a.draws.back().overlay = true;
        check(gpu.draw_offscreen(a, size), "Initialize history target A");
        pixel(gpu.readback(), size.width, 50, 30, 0x00ff00, "New history target A must clear safely");
        auto b = frame(0xff0000ff, {0, 0, 1, 1}, sampled);
        check(gpu.draw_offscreen(b, size), "Initialize history target B");
        pixel(gpu.readback(), size.width, 50, 30, 0x0000ff, "New history target B must clear independently");
        auto third = frame(0xffffffff, {1, 0, 1, 1}, sampled);
        third.vertices.clear();
        third.indices.clear();
        third.draws.clear();
        third.quad({.depth = true}, 4, 4, 4, 4, 0xffffffff, .9f);
        check(gpu.draw_offscreen(third, size), "Load history target A");
        const auto loaded = gpu.readback();
        pixel(loaded, size.width, 10, 10, 0xff0000, "Frame3 must inherit A, not B");
        pixel(loaded, size.width, 5, 5, 0xffffff, "Preserved color must not preserve depth");
        pixel(loaded, size.width, 50, 30, 0x00ff00, "Preserve must ignore the new clear color");
        pixel(loaded, size.width, 28, 8, 0xffff00, "History must include prior single-sample overlays");
        // Changing sampling policy must still use the same resolved A/B history.
        third.multisample = !sampled;
        check(gpu.draw_offscreen(third, size), "Load history B across sampling policy");
        pixel(gpu.readback(), size.width, 10, 10, 0x0000ff, "Sampling changes must preserve B's resolved color");
        third.color_load = ColorLoad::clear;
        check(gpu.draw_offscreen(third, size), "Explicitly clear history A");
        pixel(gpu.readback(), size.width, 50, 30, 0xff00ff, "Explicit clear must replace history color");
        third.color_load = ColorLoad::preserve;
        check(gpu.draw_offscreen(third, size, true), "UI clear contract");
        pixel(gpu.readback(), size.width, 50, 30, 0xff00ff, "UI must clear despite world preserve policy");
        check(gpu.draw_offscreen(third, {size.width + 1, size.height}), "Resize history pair");
        pixel(gpu.readback(), size.width + 1, 50, 30, 0xff00ff, "Resizing must initialize a new history pair safely");
    }
    Frame reusable;
    reusable.color_load = ColorLoad::preserve;
    reusable.clear();
    check(reusable.color_load == ColorLoad::clear, "Frame reuse must reset its color load policy");
    std::cout << "PASS: two-target color history, safe allocation/resize, fresh depth, MSAA overlays and UI clear\n";
}

int run(const Options& options, bool pipeline_preparation) {
    check(SDL_Init(SDL_INIT_VIDEO), "Initialize video");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA GPU checks", width, height, 0), SDL_DestroyWindow);
    check(bool(window), "Create window");
    Renderer gpu(window.get());
    if (pipeline_preparation) {
        verify_pipeline_preparation(gpu);
        return 0;
    }
    verify_color_history(gpu);
    verify_buffer_growth(gpu);
    auto palette = palette_texture(gpu);
    verify_binding_reuse(gpu, palette.get());
    Surface bitmap(nullptr, SDL_DestroySurface);
    Texture image;
    Frame image_frame;
    if (!options.image.empty()) {
        if (std::filesystem::path(options.image).extension() == ".webp") {
            std::ifstream input(options.image, std::ios::binary);
            check(bool(input), "Open committed WebP");
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            bitmap = toca::decode_webp(bytes);
        } else {
            bitmap.reset(SDL_LoadBMP(options.image.c_str()));
        }
        check(bool(bitmap), "Load image");
        if (bitmap->w != width || bitmap->h != height) {
            throw std::runtime_error("Expected a 640x480 image");
        }
        image = gpu.upload(bitmap.get());
        image_frame.quad({.texture = image.get()}, 0, 0, width, height);
    }
    std::unique_ptr<ModelScene> model;
    if (!options.data_root.empty()) {
        const toca::AssetStore assets(options.data_root);
        auto car = toca::frontend::load_car_models(assets, 0);
        model = std::make_unique<ModelScene>(gpu, std::move(car.models[0]));
    }
    gpu.draw(checks_frame(palette.get()));
    auto pixels = gpu.readback();
    verify_checks(pixels);
    verify_draw_runs(gpu, palette.get());
    verify_black_key(gpu);
    verify_source_interpolation(gpu);
    verify_indexed_key(gpu);
    verify_reference_raster_filtering(gpu);
    verify_authored_cutout(gpu);
    verify_filtering(gpu);
    verify_upload_burst(gpu);
    Frame perspective;
    const auto pv = perspective_vertices();
    perspective.vertices.assign(pv.begin(), pv.end());
    perspective.indices = {0, 1, 2, 0, 2, 3};
    perspective.draws.push_back({{.texture = palette.get()}, 0, 6});
    gpu.draw(perspective);
    verify_perspective(gpu.readback());
    const Uint8 orange[]{255, 128, 0, 255};
    gpu.update(palette.get(), orange, 1, 1);
    gpu.draw(checks_frame(palette.get()));
    const auto updated = gpu.readback();
    expect(updated, 50, 50, 0xff8000, "texture update");
    expect(updated, 150, 50, 0x00c800, "partial update preserves other texels");
    std::cout << "PASS: partial texture update reaches GPU and preserves adjacent texels\n";
    if (image) {
        gpu.draw(image_frame);
        pixels = gpu.readback();
        Surface expected(SDL_ConvertSurface(bitmap.get(), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        check(bool(expected), "Convert expected pixels");
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(expected->pixels) + y * expected->pitch);
            for (int x = 0; x < width; ++x) {
                expect(pixels, x, y, row[x], "decoded image");
            }
        }
        std::cout << "PASS: all 307200 decoded image pixels match GPU output\n";
    }
    if (model) {
        gpu.draw(model->frame(0, false, true));
        pixels = gpu.readback();
        const auto covered = std::count_if(pixels.begin(), pixels.end(), [](auto p) {
            return p != background;
        });
        if (covered < 5000 || covered > width * height * .8) {
            throw std::runtime_error("CM3D render is empty or exceeds the inspection framing");
        }
        gpu.draw(model->frame(.6f, false, true));
        if (gpu.readback() == pixels) {
            throw std::runtime_error("Model rotation has no effect");
        }
        std::cout << "PASS: CM3D GPU render covers " << covered << " pixels; rotation changes the frame\n";
    }
    if (!options.screenshot.empty()) {
        screenshot(pixels, options.screenshot);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        verify_resource_lifetimes();
        Options options;
        bool pipeline_preparation = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--image" && i + 1 < argc) {
                options.image = argv[++i];
            } else if (arg == "--data-root" && i + 1 < argc) {
                options.data_root = argv[++i];
            } else if (arg == "--screenshot" && i + 1 < argc) {
                options.screenshot = argv[++i];
            } else if (arg == "--pipeline-preparation") {
                pipeline_preparation = true;
            } else if (arg == "--help") {
                std::cout << "Usage: toca-gpu-tests [--image 640x480.webp | --data-root bundled-game-directory] "
                             "[--screenshot output.png] [--pipeline-preparation]\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
            }
        }
        if (!options.image.empty() && !options.data_root.empty()) {
            throw std::runtime_error("Choose either --image or --data-root");
        }
        return run(options, pipeline_preparation);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
