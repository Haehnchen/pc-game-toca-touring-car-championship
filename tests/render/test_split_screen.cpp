#include "scene/model_meshes.hpp"
#include "scene/model_textures.hpp"
#include "scene/split_screen.hpp"
#include "scene/view.hpp"

#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void composition_policy() {
    using namespace toca;
    const auto panes = scene::split_viewports(640, 480, false);
    gpu::Frame original;
    original.quad({}, 0, 0, 640, 240, 0xffff0000);
    gpu::Frame original_split;
    original_split.append_view(original, panes[0]);
    original_split.append_view(original, panes[1]);
    expect(!original_split.multisample, "Composing original-profile views must retain single-sample presentation");

    auto mega = original;
    mega.multisample = true;
    gpu::Frame mega_split;
    mega_split.append_view(mega, panes[0]);
    mega_split.append_view(original, panes[1]);
    expect(mega_split.multisample, "A following single-sample view must preserve the composed Mega multisample request");

    gpu::Frame pause;
    pause.quad({.blend = true}, 0, 0, 640, 480, 0x80000000);
    pause.mark_overlay(0);
    mega_split.append_view(pause, {0, 0, 640, 480});
    expect(mega_split.multisample && mega_split.draws.back().overlay, "Full-window pause composition must preserve the world sample policy and overlay phase");
}

void texture_reuse(toca::gpu::Renderer& renderer) {
    using namespace toca;
    auto textures = std::make_shared<scene::ModelTextures>(renderer);
    std::vector<std::uint8_t> bmp(70);
    const auto put32 = [](auto& bytes, std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            bytes[offset + shift / 8] = std::uint8_t(value >> shift);
        }
    };
    bmp[0] = 'B';
    bmp[1] = 'M';
    put32(bmp, 2, 70);
    put32(bmp, 10, 54);
    put32(bmp, 14, 40);
    put32(bmp, 18, 2);
    put32(bmp, 22, 2);
    bmp[26] = 1;
    bmp[28] = 24;
    for (const auto offset : {56, 59, 64, 67}) {
        bmp[offset] = 255;
    }
    ModelTexture image{"red", bmp};
    const auto original = textures->get(image);
    auto alias = image;
    alias.name = "another-slot";
    expect(textures->get(alias).texture == original.texture, "Identical model images must share an upload across material names");
    const auto mipmapped = textures->get(image, {}, true);
    expect(mipmapped.texture != original.texture && mipmapped.mip_levels == 2 && original.mip_levels == 1, "Sharing must retain distinct original and mipmapped upload policies");
    const auto lamps = textures->get(image, {}, false, {true, true});
    expect(lamps.texture == original.texture && lamps.emission[0] && lamps.emission[1] && lamps.emission[0] != lamps.emission[1]
               && textures->get(alias, {}, false, {true, true}).emission == lamps.emission,
        "Lamp derivatives must preserve the original upload and share independent front/rear images");
    alias.bmp[56] = 127;
    alias.bmp.insert(alias.bmp.end(), {0x55, 0x1e, 0xa0, 0x5e});
    expect(crc32(alias.bmp) == crc32(image.bmp) && textures->get(alias).texture != original.texture, "Different encoded images with the same CRC32 must not reuse an upload");
    alias = image;
    alias.encoding = ImageEncoding::webp;
    bool rejected = false;
    try {
        (void)textures->get(alias);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "The cache must retain image encoding checks even for matching bytes");

    // The same partial BMP can legitimately use different matching final colors.
    ModelTexture indexed{"indexed", std::vector<std::uint8_t>(1078)};
    std::copy_n(bmp.begin(), 54, indexed.bmp.begin());
    put32(indexed.bmp, 2, 1078);
    put32(indexed.bmp, 10, 1074);
    put32(indexed.bmp, 18, 1);
    put32(indexed.bmp, 22, 1);
    indexed.bmp[28] = 8;
    put32(indexed.bmp, 46, 255);
    indexed.bmp[1074] = 255;
    std::vector<std::uint8_t> palette(1024);
    palette[1022] = 255;
    const auto red = textures->get(indexed, palette);
    palette[1022] = 0;
    palette[1021] = 255;
    const auto green = textures->get(indexed, palette);
    expect(red.texture != green.texture, "A shared original BMP must retain its supplied palette identity");
    gpu::Frame frame;
    frame.quad({.texture = red.texture.get()}, 0, 0, 32, 32, 0xffffffff);
    frame.quad({.texture = green.texture.get()}, 32, 0, 32, 32, 0xffffffff);
    expect(renderer.draw(frame, false), "Shared texture comparison target unavailable");
    const auto pixels = renderer.readback();
    expect(pixels[8] == 0xffff0000 && pixels[40] == 0xff00ff00, "Palette-specific shared textures must retain their actual GPU colors");

    scene::SceneResources resources;
    resources.setup = scene::resolve_setup({});
    resources.car.car_id = 0;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ModelMesh model;
    model.vertices = {{{-2, 2, 8}, {0, 0, -1}, 0, 0}, {{2, 2, 8}, {0, 0, -1}, 1, 0}, {{-2, -2, 8}, {0, 0, -1}, 0, 1}};
    model.faces = {{{0, 1, 2}, -1, {255, 80, 20, 255}}};
    resources.objects.nodes.push_back({-1, identity, 0, {0}});
    resources.objects.meshes.push_back(std::move(model));
    resources.cockpit.models[0].nodes.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, 0, {}});
    auto first = std::make_unique<scene::SceneView>(renderer, resources, nullptr, nullptr, nullptr, std::span<const scene::RivalModel>{}, nullptr, nullptr, false, textures);
    const auto shared_meshes = first->model_meshes();
    scene::SceneView second(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false, first->model_textures(), shared_meshes);
    expect(second.model_textures() == first->model_textures(), "Split views must share the same immutable texture owner");
    expect(second.model_meshes() == shared_meshes, "Split views must share the same immutable model mesh owner");
    const scene::CameraInputs camera{identity, scene::default_projection(3, 640, 480)};
    const scene::Lighting light{{0, 0, -1}, 0, {255, 255, 255}};
    const auto retained = first->frame(camera, light, {identity}, {}, {});
    const auto counterpart = second.frame(camera, light, {identity}, {}, {});
    const auto mesh_draw = [](const gpu::Frame& source) -> const gpu::Draw& {
        const auto found = std::ranges::find_if(source.draws, [](const auto& draw) {
            return draw.mesh && !draw.mesh->attributes.empty();
        });
        if (found == source.draws.end()) {
            throw std::runtime_error("Split resource fixture must submit resident model geometry");
        }
        return *found;
    };
    expect(mesh_draw(retained).mesh == mesh_draw(counterpart).mesh && mesh_draw(retained).mesh->buffer == mesh_draw(counterpart).mesh->buffer,
        "Both split views must submit the identical immutable model mesh buffer");
    expect(renderer.draw(retained, false), "Retained split model target unavailable");
    const auto retained_pixels = renderer.readback();
    first->gear_changed();
    expect(first->cockpit_shift_phase() != 0 && second.cockpit_shift_phase() == 0, "Sharing resources must not share mutable cockpit animation");
    first.reset();
    expect(second.model_textures()->get(image).texture == original.texture, "Destroying one view must retain uploads used by the other");
    auto moved = camera;
    moved.world_to_view[12] = .75f;
    const auto changed = second.frame(moved, light, {identity}, {}, {});
    expect(
        mesh_draw(changed).mesh == mesh_draw(retained).mesh && renderer.draw(changed, false), "Later split frames must retain the shared model buffer after the first view dies");
    expect(renderer.readback() != retained_pixels, "The second player's independent camera must change its model pixels");
    expect(
        renderer.draw(retained, false) && renderer.readback() == retained_pixels, "Old split frames must retain exact pose and lighting after later frames and view destruction");

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> other_window(SDL_CreateWindow("TOCA isolated texture owner", 32, 32, 0), SDL_DestroyWindow);
    gpu::check(bool(other_window), "Create independent renderer window");
    gpu::Renderer other_renderer(other_window.get());
    rejected = false;
    try {
        scene::SceneView wrong_device(other_renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false, textures);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Shared uploads must reject a different GPU renderer before constructing a view");
    rejected = false;
    try {
        scene::SceneView wrong_mesh_device(other_renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false, {}, shared_meshes);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Shared model meshes must reject a different GPU renderer before constructing a view");
}

void composition(toca::gpu::Renderer& renderer, SDL_Window* window, std::uint32_t width, std::uint32_t height, bool vertical) {
    using namespace toca;
    gpu::check(SDL_SetWindowSize(window, int(width), int(height)), "Resize private split view");
    gpu::check(SDL_SyncWindow(window), "Wait for private split resize");
    SDL_PumpEvents();
    const auto panes = scene::split_viewports(width, height, vertical);
    gpu::Frame composed;
    for (std::size_t player = 0; player < panes.size(); ++player) {
        const auto& pane = panes[player];
        gpu::Frame frame;
        // Deliberately cross every pane boundary. The second pane uses the same
        // material and farther depth, so a leaked draw/depth state is visible.
        frame.quad({.depth = true}, -100, -100, float(pane.width) + 200, float(pane.height) + 200, player == 0 ? 0xffff0000 : 0xff00ff00, player == 0 ? .1f : .9f);
        frame.vertices.resize(65532);
        frame.quad({}, float(pane.width) - 8, float(pane.height) - 8, 8, 8, 0xff0000ff);
        composed.append_view(frame, pane);
    }
    expect(composed.vertices.size() == 131072 && composed.draws[2].vertex_offset == 65536, "Composed views lost their independent16-bit index bases");
    // A following full-window draw must restore the viewport, scissor and index
    // base after either split orientation, even beyond the16-bit vertex count.
    composed.quad({}, 0, 0, 4, float(height), 0xffffffff);
    expect(renderer.draw(composed, false), "Split-screen drawable unavailable");
    const auto extent = renderer.drawable_extent();
    if (extent.width != width || extent.height != height) {
        throw std::runtime_error("Private split drawable is " + std::to_string(extent.width) + "x" + std::to_string(extent.height) + ", expected " + std::to_string(width) + "x"
                                 + std::to_string(height));
    }
    const auto pixels = renderer.readback();
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto player = vertical ? x >= panes[1].x : y >= panes[1].y;
            const auto& pane = panes[player];
            const auto local_x = x - pane.x, local_y = y - pane.y;
            const auto color = x < 4 ? 0xffffffffu : local_x >= pane.width - 8 && local_y >= pane.height - 8 ? 0xff0000ffu : player ? 0xff00ff00u : 0xffff0000u;
            expect(pixels[std::size_t(y) * width + x] == color, "Split draw leaked viewport, scissor, depth or index state");
        }
    }
}
} // namespace

int main() {
    try {
        using namespace toca;
        composition_policy();
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private split renderer");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA split-screen checks", 641, 481, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private split window");
        gpu::Renderer renderer(window.get());
        texture_reuse(renderer);
        for (const auto extent : {platform::Extent{641, 481}, {1281, 721}, {1281, 801}}) {
            for (const bool vertical : {false, true}) {
                composition(renderer, window.get(), extent.width, extent.height, vertical);
            }
        }
        std::cout << "PASS: both split orientations, odd drawable sizes, isolated depth/scissor and large independent index buffers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
