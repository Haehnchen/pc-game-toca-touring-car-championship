#include "assets/store.hpp"
#include "scene/resources.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

bool close(float first, float second) {
    return std::abs(first - second) < .00001f;
}

toca::scene::CameraInputs identity_camera() {
    return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, toca::scene::default_projection(3, 640, 480)};
}

void synthetic() {
    using namespace toca::scene;
    toca::TerrainChunk chunk;
    for (const std::array<float, 3> normal : {std::array<float, 3>{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {0, 0, .5f}}) {
        toca::ModelVertex vertex{};
        vertex.normal = normal;
        chunk.mesh.vertices.push_back(vertex);
    }
    chunk.mesh.faces.push_back({{0, 1, 2}, -1, {255, 255, 255, 255}});
    chunk.face_flags.push_back(0);
    const Lighting lighting{{0, 0, 1}, 1, {10, 20, 30}};
    const auto colors = prepare_terrain_colors(chunk, lighting);
    expect(
        colors == std::vector<std::uint32_t>{0xffffffff, 0xff0a0a0a, 0xff0a0a0a, 0xff5d5d5d}, "Terrain must use original quantized angle lighting and red ambient copied to RGB");
    chunk.face_flags[0] = 0x80;
    const auto overridden = prepare_terrain_colors(chunk, lighting);
    expect(overridden[0] == 0xff828282 && overridden[1] == 0xff828282 && overridden[2] == 0xff828282 && overridden[3] == colors[3],
        "Face bit0x80 must override every referenced vertex AFTER terrain lighting");

    const auto camera = identity_camera();
    const std::array<LitVertex, 3> triangle{{{{-.05f, -.05f, 1}, 0xffff0000, 0, 0}, {{0, .05f, 1}, 0xff00ff00, 1, 0}, {{.05f, -.05f, 1}, 0xff0000ff, 0, 1}}};
    toca::gpu::Frame regular;
    expect(append_projected_triangle(regular, {.depth = true}, triangle, camera) == 1 && regular.vertices.size() == 3 && regular.indices.size() == 3,
        "Visible original triangle was lost");
    const auto& vertex = regular.vertices[0];
    expect(close(vertex.x, (1 - .05f * camera.projection.x) * 320) && close(vertex.y, (1 + .05f * camera.projection.y) * 240) && vertex.rhw == 1
               && vertex.z == camera.projection.depth_scale + camera.projection.depth_offset && vertex.diffuse == 0xffff0000 && vertex.specular == 0xff000000,
        "Original viewport, rounded depth, reciprocal W or packed colors changed");
    auto reversed = triangle;
    std::swap(reversed[1], reversed[2]);
    toca::gpu::Frame backface;
    expect(append_projected_triangle(backface, {.depth = true}, reversed, camera) == 0 && backface.vertices.empty(), "Original D3DCULL_CCW backface was emitted");

    auto crossing = triangle;
    crossing[0].position = {-.02f, -.02f, -.5f};
    toca::gpu::Frame clipped;
    expect(append_projected_triangle(clipped, {.depth = true}, crossing, camera) == 2 && clipped.vertices.size() == 4 && clipped.indices.size() == 6,
        "One near-clipped vertex must produce two triangles");
    expect(clipped.vertices[0].rhw == 4 && clipped.vertices[1].rhw == 4 && clipped.vertices[0].z == 0 && clipped.vertices[1].z == 0 && close(clipped.vertices[0].v, .5f)
               && close(clipped.vertices[1].u, .5f) && clipped.vertices[0].diffuse == crossing[0].color && clipped.vertices[1].diffuse == crossing[0].color,
        "Near intersections must interpolate UV, retain outside packed color and force source z0/RHW4");
    crossing[1].position = {-.05f, .05f, -.5f};
    toca::gpu::Frame two_outside;
    expect(append_projected_triangle(two_outside, {.depth = true}, crossing, camera) == 1, "Two near-clipped vertices must leave one triangle");
    crossing[2].position[2] = 0;
    toca::gpu::Frame outside;
    expect(append_projected_triangle(outside, {.depth = true}, crossing, camera) == 0 && outside.vertices.empty(), "Triangle behind near plane was emitted");

    auto distant = triangle;
    for (auto& point : distant) {
        point.position[0] *= 375;
        point.position[1] *= 375;
        point.position[2] = 375;
    }
    toca::gpu::Frame old_range, mega_range, beyond_mega;
    auto mega_camera = camera;
    mega_camera.projection = default_projection(4, 640, 480);
    expect(append_projected_triangle(old_range, {.depth = true}, distant, camera) == 0 && old_range.vertices.empty(), "Very High must retain its original250-unit far rejection");
    expect(append_projected_triangle(mega_range, {.depth = true}, distant, mega_camera) == 1, "Mega must emit visible geometry between250 and500 units");
    for (auto& point : distant) {
        point.position[2] = 501;
    }
    expect(append_projected_triangle(beyond_mega, {.depth = true}, distant, mega_camera) == 0 && beyond_mega.vertices.empty(),
        "Mega must reject geometry beyond its500-unit far plane");

    toca::gpu::Frame saturated;
    saturated.vertices.resize(65535);
    expect(append_projected_triangle(saturated, {.depth = true}, triangle, camera) == 1 && saturated.vertices.size() == 65538 && saturated.indices == std::vector<Uint16>{0, 1, 2}
               && saturated.draws[0].vertex_offset == 65535,
        "Crossing16-bit indices must start a local batch without losing geometry");
    expect(append_projected_triangle(saturated, {.depth = true}, triangle, camera) == 1 && saturated.indices == std::vector<Uint16>{0, 1, 2, 3, 4, 5}
               && saturated.draws[1].vertex_offset == 65535,
        "Adjacent scene triangles must share the same local index batch");
    const std::array<ContactSparkLine, 1> sparks{{{{-1000, -1000, 65536}, {1000, 1000, 65536}, 0xffffffff}}};
    append_contact_sparks(saturated, sparks, camera);
    expect(saturated.draws.back().vertex_offset == 65535 && saturated.indices[saturated.indices.size() - 2] == 6 && saturated.indices.back() == 7,
        "Contact lines after a large scene must use the current local index batch");
    std::cout << "PASS: terrain angle/face lighting, source near clipping, UV/color/depth/RHW and local16-bit batches\n";
}

void large_frame_pixels(toca::gpu::Renderer& renderer) {
    using namespace toca::scene;
    const auto camera = identity_camera();
    const auto triangle = [](float center, std::uint32_t color) {
        return std::array<LitVertex, 3>{{{{center - .15f, -.2f, 1}, color}, {{center, .2f, 1}, color}, {{center + .15f, -.2f, 1}, color}}};
    };
    toca::gpu::Frame frame;
    const auto left = triangle(-.3f, 0xffff0000), right = triangle(.3f, 0xff00ff00);
    for (unsigned index = 0; index < 21846; ++index) {
        append_projected_triangle(frame, {}, left, camera);
    }
    append_projected_triangle(frame, {}, right, camera);
    expect(frame.vertices.size() > 65536 && frame.draws.back().vertex_offset > 0, "GPU batch fixture must cross the16-bit vertex boundary");
    for (const auto& draw : frame.draws) {
        for (std::size_t index = draw.first; index < std::size_t(draw.first) + draw.count; ++index) {
            expect(std::size_t(draw.vertex_offset) + frame.indices[index] < frame.vertices.size(), "Batched GPU draw references a missing vertex");
        }
    }
    expect(renderer.draw(frame, false), "Large scene GPU target was unavailable");
    const auto pixels = renderer.readback();
    expect(pixels[240 * 640 + 200] == 0xffff0000 && pixels[240 * 640 + 440] == 0xff00ff00, "Real GPU batching must preserve geometry both before and after the16-bit boundary");
    renderer.save_screenshot("terrain-large-frame.png");
    std::cout << "PASS: real GPU local16-bit batches with " << frame.vertices.size() << " vertices and preserved red/green triangles\n";
}

std::vector<std::uint8_t> invented_bmp(std::uint32_t size, bool black_key) {
    const auto pitch = (size * 3 + 3) & ~3u;
    std::vector<std::uint8_t> bytes(54 + pitch * size);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            bytes[offset++] = std::uint8_t(value >> shift);
        }
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put(2, std::uint32_t(bytes.size()));
    put(10, 54);
    put(14, 40);
    put(18, size);
    put(22, size);
    bytes[26] = 1;
    bytes[28] = 24;
    put(34, pitch * size);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const auto value = std::uint8_t((x + y) % 2 ? 48 : 224);
            std::fill_n(bytes.begin() + 54 + y * pitch + x * 3, 3, value);
        }
    }
    if (black_key) {
        std::fill_n(bytes.begin() + 54, 3, 0);
    }
    return bytes;
}

void terrain_append(toca::gpu::Renderer& renderer, const toca::Terrain& source) {
    using namespace toca;
    using namespace toca::scene;
    auto terrain = source;
    terrain.chunks.push_back(terrain.chunks[0]);
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        for (auto& vertex : terrain.chunks[chunk].mesh.vertices) {
            vertex.position[0] += chunk == 0 ? -.25f : .25f;
        }
    }
    TerrainView view(renderer, terrain, true);
    const auto camera = identity_camera();
    const Lighting lighting{{0, 0, 1}, 0, {255, 255, 255}};
    const std::array<std::uint32_t, 2> order{1, 0};
    const auto resident = view.frame(camera, lighting, std::span<const std::uint32_t>(order));
    const auto standalone = toca::test::materialize_world(resident);
    expect(resident.vertices.empty() && resident.indices.empty() && !resident.draws.empty() && resident.draws.front().mesh,
        "Terrain must retain world geometry instead of preparing and uploading camera-dependent vertices");
    auto moved = camera;
    moved.world_to_view[12] = .125f;
    const auto reused = view.frame(moved, lighting, std::span<const std::uint32_t>(order));
    expect(reused.draws.front().mesh == resident.draws.front().mesh && reused.world_views != resident.world_views,
        "Camera changes must update projection data while retaining the same terrain mesh");
    auto dim = lighting;
    dim.ambient = {64, 64, 64};
    const auto relit = view.frame(camera, dim, std::span<const std::uint32_t>(order));
    expect(relit.draws.front().mesh != resident.draws.front().mesh && relit.draws.front().mesh->vertices.front().color == 0xff404040,
        "Changing actual lighting must replace cached geometry colors without mutating queued frames");
    expect(standalone.draws.size() == 14 && standalone.vertices.size() == 42 && close(standalone.vertices[0].x, (1 + .05f * camera.projection.x) * 320)
               && close(standalone.vertices[21].x, (1 - .45f * camera.projection.x) * 320),
        "Selected terrain chunks must retain the requested right-then-left geometry order");

    gpu::Frame gpu_combined;
    auto& combined = gpu_combined;
    combined.quad({.blend = true}, 0, 0, 24, 24, 0xffff00ff);
    combined.draws[0].viewport = gpu::Viewport{3, 5, 640, 480};
    combined.draws[0].overlay = true;
    combined.vertices.resize(65535); // Force the terrain suffix into a fresh local16-bit batch.
    const auto image = standalone.draws[0].state.texture;
    combined.side_backdrop = image;
    combined.non_4_3_image = gpu::Frame::CoverImage{image, 8, 8};
    combined.background_image = gpu::Frame::CoverImage{image, 16, 16};
    combined.multisample = true;
    const auto prefix = combined;
    view.append(combined, camera, lighting, std::span<const std::uint32_t>(order));
    const auto projected = toca::test::materialize_world(combined);
    expect(projected.vertices.size() == prefix.vertices.size() + standalone.vertices.size() && projected.indices.size() == prefix.indices.size() + standalone.indices.size()
               && projected.draws.size() == prefix.draws.size() + standalone.draws.size()
               && std::memcmp(projected.vertices.data(), prefix.vertices.data(), prefix.vertices.size() * sizeof(gpu::Vertex)) == 0
               && std::equal(prefix.indices.begin(), prefix.indices.end(), projected.indices.begin()) && projected.draws[0].state == prefix.draws[0].state
               && projected.draws[0].first == prefix.draws[0].first && projected.draws[0].count == prefix.draws[0].count
               && projected.draws[0].vertex_offset == prefix.draws[0].vertex_offset && projected.draws[0].viewport == prefix.draws[0].viewport && projected.draws[0].overlay,
        "Appending terrain must preserve every existing vertex, index and draw");
    expect(combined.side_backdrop == image && combined.non_4_3_image && combined.background_image && combined.non_4_3_image->texture == image && combined.non_4_3_image->width == 8
               && combined.non_4_3_image->height == 8 && combined.background_image->texture == image && combined.background_image->width == 16
               && combined.background_image->height == 16 && combined.multisample,
        "Appending terrain must preserve the caller's image and multisample metadata");
    expect(std::memcmp(projected.vertices.data() + prefix.vertices.size(), standalone.vertices.data(), standalone.vertices.size() * sizeof(gpu::Vertex)) == 0,
        "Appending terrain must preserve standalone projected geometry, colors and UVs");
    for (std::size_t index = 0; index < standalone.draws.size(); ++index) {
        const auto& expected = standalone.draws[index];
        const auto& actual = projected.draws[index + prefix.draws.size()];
        expect(actual.state == expected.state && actual.count == expected.count && actual.first == expected.first + prefix.indices.size() && actual.vertex_offset == 65535
                   && !actual.viewport && !actual.overlay,
            "Terrain appended after a full batch must retain material policy and local16-bit addressing");
    }
    expect(std::equal(standalone.indices.begin(), standalone.indices.end(), projected.indices.begin() + std::ptrdiff_t(prefix.indices.size())),
        "Terrain append must start its selected chunks at the new batch's local zero");
    const std::array<std::uint32_t, 0> none{};
    const auto draw_count = combined.draws.size();
    view.append(combined, camera, lighting, std::span<const std::uint32_t>(none));
    expect(combined.draws.size() == draw_count, "An empty terrain selection must append no geometry");

    combined.side_backdrop = nullptr;
    combined.non_4_3_image.reset();
    combined.background_image.reset();
    combined.multisample = false;
    combined.draws[0].viewport.reset();
    combined.draws[0].overlay = false;
    expect(renderer.draw(combined, false), "Appended terrain GPU target unavailable");
    const auto pixels = renderer.readback();
    const auto center = [&](float x) {
        return unsigned((1 + x * camera.projection.x) * 320);
    };
    expect(pixels[8 * 640 + 8] == 0xffff00ff && pixels[240 * 640 + center(-.25f)] == 0xffffffff && pixels[240 * 640 + center(.25f)] == 0xffffffff,
        "Appended terrain must render both selected chunks after the16-bit boundary while retaining the prefix");
}

void terrain_mipmap_policy(toca::gpu::Renderer& renderer) {
    using namespace toca;
    using namespace toca::scene;
    Terrain terrain;
    terrain.textures = {{"OPAQUE", invented_bmp(8, false)}, {"ALIAS", invented_bmp(8, false)}, {"KEYED", invented_bmp(8, true)}, {"ONEPIXEL", invented_bmp(1, false)}};
    // Invented 8x8 WebP80 fixtures, not game imagery: a solid opaque square
    // and the same square with one transparent corner.
    const std::vector<std::uint8_t> opaque_webp{0x52, 0x49, 0x46, 0x46, 0x38, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x20, 0x2c, 0x00, 0x00, 0x00, 0xd0, 0x01,
        0x00, 0x9d, 0x01, 0x2a, 0x08, 0x00, 0x08, 0x00, 0x01, 0x40, 0x26, 0x25, 0xa0, 0x02, 0x74, 0xba, 0x01, 0xf8, 0x00, 0x03, 0xb0, 0x00, 0xfe, 0xe0, 0xb9, 0x7f, 0xf3, 0x60,
        0x57, 0x35, 0xcf, 0xbf, 0xdf, 0xfe, 0x66, 0x8c, 0x25, 0x24, 0x78, 0x68, 0x00, 0x00};
    const std::vector<std::uint8_t> alpha_webp{0x52, 0x49, 0x46, 0x46, 0x66, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x58, 0x0a, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x07, 0x00, 0x00, 0x07, 0x00, 0x00, 0x41, 0x4c, 0x50, 0x48, 0x14, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x30, 0xff, 0x11, 0x11, 0x42, 0x41, 0xda, 0x06, 0x8c, 0x7f,
        0xd3, 0xdd, 0x15, 0x11, 0xd1, 0xff, 0x40, 0x0f, 0x56, 0x50, 0x38, 0x20, 0x2c, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00, 0x9d, 0x01, 0x2a, 0x08, 0x00, 0x08, 0x00, 0x01, 0x40,
        0x26, 0x25, 0xa0, 0x02, 0x74, 0xba, 0x01, 0xf8, 0x00, 0x03, 0xb0, 0x00, 0xfe, 0xe0, 0xb9, 0x7f, 0xf3, 0x60, 0x57, 0x35, 0xcf, 0xbf, 0xdf, 0xfe, 0x66, 0x8c, 0x25, 0x24,
        0x78, 0x68, 0x00, 0x00};
    terrain.textures.push_back({"WEBP", opaque_webp, ImageEncoding::webp});
    terrain.textures.push_back({"WEBPALPHA", alpha_webp, ImageEncoding::webp});
    TerrainChunk chunk;
    chunk.mesh.vertices = {{{-.2f, -.2f, 1}, {0, 0, 1}, 0, 0}, {{0, .2f, 1}, {0, 0, 1}, .5f, 1}, {{.2f, -.2f, 1}, {0, 0, 1}, 1, 0}};
    for (std::int16_t slot = 0; slot < std::int16_t(terrain.textures.size()); ++slot) {
        chunk.mesh.faces.push_back({{0, 1, 2}, slot, {255, 255, 255, 255}});
        chunk.face_flags.push_back(0);
    }
    chunk.mesh.faces.push_back({{0, 1, 2}, -1, {255, 255, 255, 255}});
    chunk.face_flags.push_back(0);
    terrain.chunks.push_back(std::move(chunk));
    TerrainView original(renderer, terrain), mega(renderer, terrain, true);
    const auto before = toca::test::materialize_world(original.frame(identity_camera(), {}));
    const auto after = toca::test::materialize_world(mega.frame(identity_camera(), {}));
    expect(before.draws.size() == 7 && after.draws.size() == before.draws.size(), "Terrain material slots must retain their face bindings");
    constexpr std::array<bool, 7> expected{true, true, false, false, true, false, false};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(!before.draws[index].state.mipmapped && after.draws[index].state.mipmapped == expected[index],
            "Only requested opaque multi-pixel BMP/WebP terrain images may use mipmaps");
    }
    expect(after.draws[0].state.texture == after.draws[1].state.texture && before.draws[0].state.texture == before.draws[1].state.texture,
        "Identical terrain bytes with different names must share their GPU image without changing material indices");
    expect(after.draws[0].state.texture != after.draws[2].state.texture && after.draws[4].state.alpha_cutoff == .5f && !after.draws[6].state.texture,
        "Opaque deduplication must preserve keyed, WebP and untextured draw policies");
    terrain_append(renderer, terrain);
    std::cout << "PASS: terrain opaque BMP/WebP mip policy, keyed/alpha/1x1 boundaries and exact-byte GPU reuse\n";
}

void terrain_mipmap_rendering(
    toca::gpu::Renderer& renderer, const toca::scene::SceneResources& resources, const std::array<float, 16>& authored_world, const toca::scene::Lighting& lighting) {
    using namespace toca;
    using namespace toca::scene;
    TerrainView mipmapped(renderer, resources.ground, true);
    for (const auto extent : {platform::Extent{1920, 1080}, platform::Extent{2560, 1440}}) {
        for (const bool far : {false, true}) {
            auto world = authored_world;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                world[12 + axis] += world[axis] * .48f + world[8 + axis] * (.8f - (far ? 60.f : 0.f));
            }
            const auto inverse = glm::inverse(glm::make_mat4(world.data()));
            CameraInputs camera{{}, default_projection(graphics_mega_detail, extent.width, extent.height)};
            std::copy_n(glm::value_ptr(inverse), 16, camera.world_to_view.begin());
            const auto prepared = mipmapped.frame(camera, lighting);
            expect(!prepared.draws.empty()
                       && std::ranges::any_of(prepared.draws,
                           [](const auto& draw) {
                               return draw.state.mipmapped;
                           }),
                "Canonical near/far terrain view must contain mipmapped opaque materials");
            for (const bool enabled : {false, true}) {
                auto frame = prepared;
                frame.multisample = true; // Match the same Mega MSAA policy in both samples.
                if (!enabled) {
                    for (auto& draw : frame.draws) {
                        draw.state.mipmapped = false;
                    }
                }
                expect(renderer.draw_offscreen(frame, extent), "Terrain mipmap render target was unavailable");
                const auto mip_draws = renderer.last_frame_timing().mipmapped_draw_calls;
                expect(enabled ? mip_draws > 0 : mip_draws == 0, "Completed terrain draw did not use its selected mip sampler");
                renderer.save_screenshot(
                    "terrain-mip-" + std::to_string(extent.width) + "x" + std::to_string(extent.height) + (far ? "-far" : "-near") + (enabled ? "-on.png" : "-off.png"));
            }
        }
    }
    std::cout << "PASS: terrain mipmap sampler on/off with shared textures, geometry and MSAA at near/far1080p/1440p poses\n";
}

void reference(const std::filesystem::path& root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    const auto resources = load_scene_resources(store, resolve_setup({}));
    const auto& authored = resources.cameras.at(0).transform;
    std::array<float, 16> world;
    for (std::size_t index = 0; index < world.size(); ++index) {
        world[index] = float(authored[index]) / 65536.f;
    }
    const auto inverse = glm::inverse(glm::make_mat4(world.data()));
    CameraInputs camera{{}, default_projection(3, 640, 480)};
    std::copy_n(glm::value_ptr(inverse), 16, camera.world_to_view.begin());
    // Explicit fixture inputs from the Donington/cloudy light parameters.
    // Native float normalization here is NOT the original fixed normalize consumer.
    const auto direction = glm::normalize(glm::vec3(50, 100, 75));
    const Lighting lighting{{direction.x, direction.y, direction.z}, float((std::int64_t(58982) * 0xcccc) >> 16) / 65536.f, {127, 127, 127}};

    toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated terrain video check");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA terrain consumer check", 640, 480, 0), SDL_DestroyWindow);
    toca::gpu::check(bool(window), "Create isolated terrain window");
    toca::gpu::Renderer renderer(window.get());
    large_frame_pixels(renderer);
    TerrainView view(renderer, resources.ground);
    const auto resident = view.frame(camera, lighting);
    const auto frame = toca::test::materialize_world(resident);
    expect(!frame.draws.empty() && frame.vertices.size() <= 65536, "Authored camera produced no bounded terrain geometry");
    for (const auto& vertex : frame.vertices) {
        expect(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) && std::isfinite(vertex.rhw) && vertex.rhw > 0,
            "Canonical terrain prepared invalid TL coordinates");
    }
    expect(renderer.draw(resident, false), "Private terrain window did not provide a drawable frame");
    const auto pixels = renderer.readback();
    const auto visible = std::ranges::count_if(pixels, [](auto color) {
        return color != toca::gpu::background;
    });
    expect(visible > 100, "Canonical terrain did not reach the real GPU render target");
    renderer.save_screenshot("terrain-authored-camera.png");
    expect(renderer.draw(frame, false), "CPU terrain oracle target was unavailable");
    const auto oracle = renderer.readback();
    if (pixels != oracle) {
        renderer.save_screenshot("terrain-cpu-projection.png");
        const auto first = std::mismatch(pixels.begin(), pixels.end(), oracle.begin()).first - pixels.begin();
        throw std::runtime_error("Resident terrain differs from CPU projection at " + std::to_string(first % 640) + "," + std::to_string(first / 640)
                                 + " resident=" + std::to_string(pixels[first]) + " cpu=" + std::to_string(oracle[first]));
    }
    std::cout << "PASS: " << frame.vertices.size() << " TL vertices, " << frame.indices.size() / 3 << " triangles and " << visible
              << " terrain pixels through authored camera0; this is a terrain consumer fixture, not the initial race camera\n";
    terrain_mipmap_policy(renderer);
    terrain_mipmap_rendering(renderer, resources, world, lighting);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 3 && std::string_view(argv[1]) == "--reference") {
            reference(argv[2]);
        } else {
            throw std::runtime_error("Expected no arguments or --reference CANONICAL_ROOT");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
