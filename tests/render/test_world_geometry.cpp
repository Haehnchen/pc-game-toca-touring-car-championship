#include "scene/model_meshes.hpp"
#include "world_geometry.hpp"

#include <SDL3/SDL_main.h>

#include <cmath>
#include <iostream>

namespace {
using namespace toca::gpu;
using toca::platform::Extent;
using toca::test::materialize_world;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr Extent extent{128, 96};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void source_projection_goldens() {
    // Numeric 47e730/48741e vectors from the unchanged Donington source draw.
    // No model/media input or local reference installation is required.
    WorldProjection view{{0.3541601598262787f, -0.15705527365207672f, -0.9219112992286682f, 0, 1.9336925394242144e-09f, 0.9858007431030273f, -0.16793936491012573f, 0,
                             0.9351902008056641f, 0.05947742983698845f, 0.34913134574890137f, 0, 169.11480712890625f, -19.04476547241211f, -52.600135803222656f, 1},
        {83886.f / 65536, 104857.f / 65536, .25f, 250}, {65601.f / 65536, -16400.f / 65536, 640, 480}};
    auto object = identity;
    object[12] = -111.3759765625f;
    object[13] = 8.440643310546875f;
    object[14] = -138.65615844726562f;
    auto expected_wv = view.world_to_view;
    expected_wv[12] = -7.176195140345953e-06f;
    expected_wv[13] = -1.4787005186080933f;
    expected_wv[14] = .25190797448158264f;
    require(source_matrix_product(object, view.world_to_view) == expected_wv, "Source WV coefficients must retain the 53-bit sum and explicit float32 store");
    const SourceMatrix expected_wvp{.4533245861530304f, -.2512870132923126f, -.9228256940841675f, -.9219112992286682f, 2.4751241056719664e-09f, 1.5772721767425537f,
        -.16810593008995056f, -.16793936491012573f, 1.1970423460006714f, .09516334533691406f, .349477618932724f, .34913134574890137f, -9.185520866594743e-06f, -2.3659071922302246f,
        .0019136815099045634f, .25190797448158264f};
    const auto clip = source_clip_matrix(view, object);
    require(clip == expected_wvp, "Source WVP must compose from the stored WV coefficients before its own float32 store");
    const std::array<toca::scene::LitVertex, 3> local{
        {{{0, 0, 10}, 0xffffffff, 1, 1}, {{0, 7.8288421630859375f, 10}, 0xffffffff, 1, 0}, {{-7.0710601806640625f, 0, 7.0710601806640625f}, 0xffffffff, 0, 1}}};
    const std::array<Vertex, 3> expected{{{1343.3250732421875f, 330.67742919921875f, std::bit_cast<float>(0x3f7fff58u), .26714956760406494f, 0xffffffff, 0xff000000, 1, 1},
        {1897.3565673828125f, -840.5843505859375f, std::bit_cast<float>(0x3f7fff58u), .41178518533706665f, 0xffffffff, 0xff000000, 1, 0},
        {502.1344909667969f, 237.82159423828125f, std::bit_cast<float>(0x3f7fff58u), .10823065042495728f, 0xffffffff, 0xff000000, 0, 1}}};
    for (std::size_t index = 0; index < local.size(); ++index) {
        const auto actual = toca::test::source_projected_vertex(local[index], clip, view, true);
        require(std::bit_cast<std::array<Uint32, 8>>(actual.tl) == std::bit_cast<std::array<Uint32, 8>>(expected[index]),
            "Source local-to-WVP projection must reproduce captured TL float bits, including RHW and sky depth");
    }
    SourceMatrix boundary{};
    boundary[0] = .25f;
    boundary[12] = 1.f / 262144;
    boundary[15] = .25f;
    const auto flagged = toca::test::source_projected_vertex({{1, 0, 0}}, boundary, view);
    require(flagged.clip[0] > flagged.clip[3] && flagged.flags == 1, "Source side and near flags must use rounded Q16 raw dots rather than float32 plane comparisons");
    boundary = {};
    boundary[15] = -1;
    require(toca::test::source_projected_vertex({{0, 0, 0}}, boundary, view).flags == 21,
        "Source negative-W outcodes must retain right/top branch precedence rather than setting both opposing side bits");
}

void source_lighting_golden() {
    // 487afe's coefficient/partial stores change this final RGB rounding bit.
    const std::array<float, 3> normal{0.000579833984375f, 0.662384033203125f, -0.4360809326171875f};
    const std::array<float, 16> basis{-0.4882887601852417f, -0.01160931121557951f, -0.5204557776451111f, 0.0f, 0.6071678400039673f, 0.4545142948627472f, 0.4515748918056488f, 0.0f,
        0.5737306475639343f, -0.36214393377304077f, -0.16969209909439087f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const toca::scene::Lighting light{{0.8718262910842896f, 0.5686068534851074f, 0.9395696520805359f}, 0.9129237532615662f, {228, 191, 237}};
    require(toca::scene::model_vertex_color(normal, basis, {175, 62, 249, 131}, light) == 0x83f14dffu,
        "Source lighting must retain its nine coefficient stores, three partial stores and separate ambient/diffuse stores");
}

void source_near_goldens() {
    using toca::test::SourceProjectedVertex;
    const WorldProjection view{identity, {1, 1, .25f, 16}, {16.f / 15, -16.f / 15, 640, 480}};
    const auto vertex = [](float x, float y, float w, float u, float v, Uint32 flags) {
        return SourceProjectedVertex{{x, y, 0, w}, {0, 0, 0, 1, 0xffffffff, 0xff000000, u, v}, flags};
    };
    std::array<SourceProjectedVertex, 68> input;
    for (auto& value : input) {
        value.clip[3] = 1;
    }
    input[37] = vertex(-.7685021162033081f, -5.407811641693115f, -5.9030232429504395f, .019999999552965164f, .9850006103515625f, 21);
    input[38] = vertex(-6.218571662902832f, -3.0980045795440674f, 2.5710995197296143f, .9849853515625f, .9850006103515625f, 10);
    input[39] = vertex(-17.16918182373047f, -3.944443941116333f, -1.3203076124191284f, .9849853515625f, .019999999552965164f, 11);
    input[59] = vertex(-6.218571662902832f, -3.0980045795440674f, 2.5710995197296143f, .019999999552965164f, .9850006103515625f, 10);
    input[60] = vertex(-.7685021162033081f, -5.407811641693115f, -5.9030232429504395f, .9849853515625f, .9850006103515625f, 21);
    input[61] = vertex(10.647893905639648f, -3.5030274391174316f, 1.2866052389144897f, .9849853515625f, .019999999552965164f, 12);
    // The ten admitted diagnostic triangles retain source IDs. Unrelated
    // rejected geometry is omitted; only six transformed edge vectors are stored.
    constexpr std::array<std::array<Uint16, 3>, 10> submitted{
        {{27, 26, 25}, {25, 28, 27}, {31, 30, 29}, {29, 32, 31}, {33, 36, 35}, {39, 38, 37}, {54, 53, 52}, {56, 36, 55}, {58, 57, 31}, {61, 60, 59}}};
    std::vector<Uint16> indices;
    std::vector<Vertex> clipped;
    for (const auto ids : submitted) {
        const std::array face{input[ids[0]], input[ids[1]], input[ids[2]]};
        const auto output = toca::test::source_near_indices(face, ids, .25f, [&](unsigned inside, unsigned outside, bool anchor_inside) {
            clipped.push_back(toca::test::source_near_vertex(face[inside], face[outside], view, anchor_inside));
            return Uint16(68 + clipped.size() - 1);
        });
        indices.insert(indices.end(), output.values.begin(), output.values.begin() + output.count);
    }
    const std::vector<Uint16> expected_indices{27, 26, 25, 25, 28, 27, 31, 30, 29, 29, 32, 31, 33, 36, 35, 68, 38, 69, 54, 53, 52, 56, 36, 55, 58, 57, 31, 61, 70, 59, 70, 71, 59};
    require(indices == expected_indices, "487ee9 must retain all 33 captured indices and its non-fan triangle order");
    const std::array<Vertex, 4> expected{{{-16000.33203125f, 3698.763427734375f, 0, 4, 0xffffffff, 0xff000000, .9849853515625f, .40940871834754944f},
        {-5728.990234375f, 3821.444091796875f, 0, 4, 0xffffffff, 0xff000000, .7206716537475586f, .9850006103515625f},
        {11842.39453125f, 3866.553955078125f, 0, 4, 0xffffffff, 0xff000000, .9849853515625f, .1591344028711319f},
        {-5728.990234375f, 3821.444091796875f, 0, 4, 0xffffffff, 0xff000000, .28431370854377747f, .9850006103515625f}}};
    require(clipped.size() == expected.size(), "Source clipping diagnostic must generate exactly four new TL vertices");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(std::bit_cast<std::array<Uint32, 8>>(clipped[index]) == std::bit_cast<std::array<Uint32, 8>>(expected[index]),
            "487ee9's inside/outside anchors and float stores must retain every captured clipped TL/UV/color bit");
    }
}

WorldProjection projection(Extent size = extent) {
    return {identity, {1, 1, 1, 16}, {16.f / 15, -16.f / 15, float(size.width), float(size.height)}};
}

std::vector<WorldVertex> triangle(unsigned outside = 0, bool on_near = false) {
    constexpr std::array<float, 3> x{-.7f, .65f, -.15f}, y{.65f, .5f, -.7f};
    constexpr std::array<Uint32, 3> colors{0xffff8040, 0xff40ff80, 0xff8040ff};
    constexpr std::array<std::array<float, 2>, 3> uv{{{.05f, .05f}, {.95f, .1f}, {.3f, .95f}}};
    std::vector<WorldVertex> result;
    for (unsigned corner = 0; corner < 3; ++corner) {
        const auto z = outside & (1u << corner) ? (on_near ? 1.f : .5f) : 4.f;
        result.push_back({{x[corner] * z, y[corner] * z, z}, colors[corner], uv[corner][0], uv[corner][1]});
    }
    return result;
}

void resident(Frame& frame, const std::shared_ptr<const WorldMesh>& mesh, State state = {}, Uint32 first = 0, Uint32 count = 0, Uint32 view = 0) {
    Draw draw{state, first, count ? count : Uint32(mesh->vertices.size() * 2)};
    draw.mesh = mesh;
    draw.world_view = view;
    frame.draws.push_back(draw);
}

std::vector<ModelAttribute> attributes(const std::vector<WorldVertex>& vertices, bool hidden = false) {
    std::vector<ModelAttribute> result;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto& position = vertices[index].position;
        result.push_back({{.125f * float(index % 3), -.25f, .875f}, hidden ? 1u : 0u, {position[0] + .35f, position[1] * .75f, position[2] + .125f}});
    }
    return result;
}

ModelInstance model_instance(Uint32 flags = 0) {
    return {identity, {.25f, -.375f, .75f, .65f}, {48, 80, 112, flags}};
}

void model(Frame& frame, const std::shared_ptr<const WorldMesh>& mesh, ModelInstance instance = model_instance(), State state = {.depth = true}) {
    const auto index = Uint32(frame.model_instances.size());
    frame.model_instances.push_back(instance);
    resident(frame, mesh, state);
    frame.draws.back().model_instance = index;
}

std::vector<Uint32> render(Renderer& renderer, const Frame& frame, Extent size = extent) {
    require(renderer.draw_offscreen(frame, size), "Resident geometry offscreen target unavailable");
    return renderer.readback();
}

std::vector<Uint32> compare_to(Renderer& renderer, const Frame& frame, const Frame& oracle, const std::string& label, Extent size = extent) {
    require(std::ranges::none_of(oracle.draws,
                [](const Draw& draw) {
                    return bool(draw.mesh) || draw.dynamic_world;
                }),
        "CPU oracle must expand every world draw");
    const auto expected = oracle.draws.empty() ? std::vector<Uint32>(std::size_t(size.width) * size.height, background) : render(renderer, oracle, size);
    const auto actual = render(renderer, frame, size);
    require(expected.size() == actual.size(), "Resident and CPU target dimensions differ");
    // Source division and packed-color normalization must reproduce the TL
    // consumer exactly, including textured interpolation and blend boundaries.
    for (std::size_t pixel = 0; pixel < actual.size(); ++pixel) {
        if (actual[pixel] == expected[pixel]) {
            continue;
        }
        if ((actual[pixel] == background) != (expected[pixel] == background)) {
            throw std::runtime_error(label + ": resident/CPU coverage mismatch at " + std::to_string(pixel % size.width) + "," + std::to_string(pixel / size.width));
        }
        for (unsigned shift = 0; shift < 32; shift += 8) {
            if (((actual[pixel] >> shift) & 255) != ((expected[pixel] >> shift) & 255)) {
                throw std::runtime_error(label + ": resident/CPU pixel mismatch at " + std::to_string(pixel % size.width) + "," + std::to_string(pixel / size.width)
                                         + " actual=" + std::to_string(actual[pixel]) + " expected=" + std::to_string(expected[pixel]));
            }
        }
    }
    return actual;
}

std::vector<Uint32> compare(Renderer& renderer, const Frame& frame, const std::string& label, Extent size = extent) {
    return compare_to(renderer, frame, materialize_world(frame), label, size);
}

// Independently retain the separate finale CPU consumer's operation order.
// This expected frame never reads ModelInstance flags or materialize_world.
Frame legacy_finale_cpu(const std::vector<WorldVertex>& vertices, const std::vector<ModelAttribute>& source_attributes, const std::array<float, 16>& matrix,
    const WorldProjection& view, const toca::scene::Lighting& lighting, State state) {
    const auto& scale = view.scale_near_far;
    const auto& depth = view.depth_size;
    const toca::scene::CameraInputs camera{view.world_to_view, {scale[0], scale[1], scale[2], scale[3], depth[0], depth[1], Uint32(depth[2]), Uint32(depth[3])}};
    Frame result;
    for (std::size_t first = 0; first < vertices.size(); first += 3) {
        std::array<toca::scene::LitVertex, 3> face;
        for (std::size_t corner = 0; corner < face.size(); ++corner) {
            const auto& vertex = vertices[first + corner];
            const auto& attribute = source_attributes[first + corner];
            std::array<float, 3> world{matrix[12], matrix[13], matrix[14]}, normal{};
            for (std::size_t component = 0; component < 3; ++component) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    world[axis] += vertex.position[component] * matrix[component * 4 + axis];
                    normal[axis] += attribute.normal[component] * matrix[component * 4 + axis];
                }
            }
            const std::array<Uint8, 4> material{Uint8(vertex.color >> 16), Uint8(vertex.color >> 8), Uint8(vertex.color), Uint8(vertex.color >> 24)};
            face[corner] = {world, toca::scene::model_vertex_color(normal, material, lighting), vertex.u, vertex.v};
        }
        toca::scene::append_projected_triangle(result, state, face, camera);
    }
    return result;
}

Texture gradient(Renderer& renderer) {
    Surface image(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    check(bool(image), "Create resident geometry texture");
    for (int y = 0; y < image->h; ++y) {
        for (int x = 0; x < image->w; ++x) {
            const std::array<Uint8, 4> color{Uint8(32 + x * 24), Uint8(32 + y * 24), Uint8(200 - x * 12), 255};
            std::memcpy(static_cast<Uint8*>(image->pixels) + y * image->pitch + x * 4, color.data(), color.size());
        }
    }
    return renderer.upload(image.get());
}

void near_clipping(Renderer& renderer) {
    const auto texture = gradient(renderer);
    for (const bool on_near : {false, true}) {
        for (unsigned outside = 0; outside < 8; ++outside) {
            Frame frame;
            frame.world_views.push_back(projection());
            resident(frame, renderer.world_mesh(triangle(outside, on_near)), {.texture = texture.get(), .depth = true, .linear = true, .clamp = true});
            const auto oracle = materialize_world(frame);
            require((outside == 7) == oracle.draws.empty(), "Near-plane fixture must cover every visible/invisible clipping case");
            if (!on_near && outside != 7) {
                const auto outsiders = std::popcount(outside);
                require(oracle.indices.size() == (outsiders == 1 ? 6 : 3), "CPU fixture must exercise the expected one/two-triangle clipped fan");
            }
            const auto pixels = compare(renderer, frame, "Near clipping mask " + std::to_string(outside) + (on_near ? " at boundary" : ""));
            require((outside == 7)
                        == std::ranges::all_of(pixels,
                            [](Uint32 value) {
                                return value == background;
                            }),
                "Near clipping must render real pixels only for surviving source triangles");
        }
    }
    auto corners = triangle();
    for (auto& corner : corners) {
        corner.color = 0xff20c080;
    }
    Frame solid;
    solid.world_views.push_back(projection());
    resident(solid, renderer.world_mesh(std::move(corners)));
    const auto expected = render(renderer, materialize_world(solid));
    const auto actual = render(renderer, solid);
    require(actual == expected && actual[40 * extent.width + 60] == 0xff20c080, "Constant-color resident geometry must retain exact CPU pixels and coverage");
}

void rejection_and_view_transform(Renderer& renderer) {
    std::vector<WorldVertex> rejected;
    for (unsigned plane = 0; plane < 6; ++plane) {
        auto corners = triangle();
        for (auto& corner : corners) {
            if (plane < 4) {
                corner.position[plane / 2] += plane % 2 ? 20.f : -20.f;
            } else if (plane == 4) {
                corner.position[2] = 20;
            }
        }
        if (plane == 5) {
            std::swap(corners[1], corners[2]);
        }
        rejected.insert(rejected.end(), corners.begin(), corners.end());
    }
    Frame frame;
    frame.world_views.push_back(projection());
    resident(frame, renderer.world_mesh(std::move(rejected)));
    require(materialize_world(frame).draws.empty(), "Reject fixture must cover all four frustum planes, far plane and backface");
    const auto pixels = compare(renderer, frame, "Frustum/far/backface rejection");
    require(std::ranges::all_of(pixels,
                [](Uint32 pixel) {
                    return pixel == background;
                }),
        "Rejected resident geometry must leave the entire target clear");

    auto view = projection();
    // Row-vector affine basis: rotate around Z and translate all three axes.
    constexpr float angle = .27f;
    view.world_to_view[0] = view.world_to_view[5] = std::cos(angle);
    view.world_to_view[1] = std::sin(angle);
    view.world_to_view[4] = -std::sin(angle);
    view.world_to_view[12] = .3f;
    view.world_to_view[13] = -.2f;
    view.world_to_view[14] = .4f;
    auto mesh = renderer.world_mesh(triangle(1));
    const std::weak_ptr<const WorldMesh> retained = mesh;
    frame = {};
    frame.world_views = {view, projection()};
    resident(frame, mesh);
    mesh.reset();
    require(!retained.expired(), "A queued frame must retain its immutable mesh independently of its scene owner");
    const auto rotated = compare(renderer, frame, "Nonidentity affine view");
    frame.draws[0].world_view = 1;
    const auto ordinary = compare(renderer, frame, "Retained mesh with changed camera");
    require(rotated != ordinary && frame.draws[0].mesh == retained.lock(), "Camera changes must reuse the retained mesh and change actual projected pixels");
    frame.draws[0].world_view = 0;
    require(compare(renderer, frame, "Repeated retained camera") == rotated, "Returning to a prior camera must retain exact pixels");
}

Frame mixed(Renderer& renderer, Extent size = extent) {
    auto corners = triangle();
    for (auto& corner : corners) {
        corner.color = 0xffff0000;
    }
    auto front = triangle();
    for (auto& corner : front) {
        corner.position[0] *= .65f;
        corner.position[1] *= .65f;
        corner.position[2] *= .5f;
        corner.color = 0x800000ff;
    }
    corners.insert(corners.end(), front.begin(), front.end());
    const auto mesh = renderer.world_mesh(std::move(corners));
    Frame frame;
    frame.world_views = {projection(size)};
    frame.quad({}, 0, 0, float(size.width), float(size.height), 0xff102030);
    resident(frame, mesh, {.depth = true}, 0, 6);
    // Ordinary TL geometry between resident draws exercises mixed ordering and
    // buffer offsets, rather than comparing two isolated render pipelines.
    frame.quad({.blend = true}, float(size.width) * .35f, float(size.height) * .35f, float(size.width) * .2f, float(size.height) * .2f, 0x8000ff00);
    resident(frame, mesh, {.depth = true, .blend = true, .depth_write = false}, 6, 6);
    const auto overlay = frame.draws.size();
    frame.quad({}, 3, 3, 8, 6, 0xffffff00);
    frame.mark_overlay(overlay);
    return frame;
}

void ordering_reflections_and_composition(Renderer& renderer) {
    auto frame = mixed(renderer);
    const auto ordinary = compare(renderer, frame, "Mixed opaque/blended resident/TL order");
    require(ordinary[5 * extent.width + 5] == 0xffffff00, "TL overlay must remain visible after resident world draws");
    for (unsigned mask = 1; mask < 4; ++mask) {
        auto reflected = frame;
        reflected.world_views[0].reflection = {float(mask & 1), float((mask >> 1) & 1), 0, 0};
        const auto pixels = compare(renderer, reflected, "Post-cull reflection " + std::to_string(mask));
        require(pixels != ordinary && pixels[5 * extent.width + 5] == 0xffffff00, "World reflections must change geometry while retaining the TL overlay");
    }
    for (const bool vertical : {false, true}) {
        constexpr Extent size{127, 97};
        const auto first_width = vertical ? size.width / 2 : size.width;
        const auto first_height = vertical ? size.height : size.height / 2;
        const Viewport first{0, 0, first_width, first_height};
        const Viewport second{vertical ? first_width : 0, vertical ? 0 : first_height, size.width - (vertical ? first_width : 0), size.height - (vertical ? 0 : first_height)};
        auto left = mixed(renderer, {first.width, first.height}), right = mixed(renderer, {second.width, second.height});
        right.world_views[0].world_to_view[12] = .5f;
        right.world_views[0].reflection[0] = 1;
        Frame combined;
        combined.append_view(left, first);
        combined.append_view(right, second);
        Frame hud;
        hud.quad({}, 1, 1, 4, 4, 0xffff00ff);
        hud.mark_overlay(0);
        combined.append_view(hud, {0, 0, size.width, size.height});
        const auto oracle = materialize_world(combined);
        require(oracle.draws.back().overlay && oracle.draws.back().viewport == Viewport{0, 0, size.width, size.height},
            "CPU materialization must preserve full-window overlay metadata after split views");
        for (const bool multisample : {false, true}) {
            combined.multisample = multisample;
            const auto pixels = compare(renderer, combined, "Odd-sized split composition", size);
            require(pixels[2 * size.width + 2] == 0xffff00ff, "Split resident views must retain the following full-window HUD");
        }
    }
}

void large_resident_geometry(Renderer& renderer) {
    constexpr std::size_t triangles = 23000;
    std::vector<WorldVertex> corners;
    corners.reserve(triangles * 3);
    for (std::size_t index = 0; index < triangles; ++index) {
        auto source = triangle();
        if (index + 1 != triangles) {
            for (auto& corner : source) {
                corner.position[0] += 20;
            }
        }
        corners.insert(corners.end(), source.begin(), source.end());
    }
    Frame frame;
    frame.world_views = {projection()};
    const auto mesh = renderer.world_mesh(std::move(corners));
    require(mesh->vertices.size() > 65536, "Resident fixture must cross the original16-bit index boundary");
    resident(frame, mesh);
    const auto pixels = compare(renderer, frame, "Resident geometry beyond65536 source corners");
    require(std::ranges::any_of(pixels,
                [](Uint32 value) {
                    return value != background;
                }),
        "The final resident triangle beyond65536 corners must render");
    frame.draws[0].first = Uint32((triangles - 1) * 6);
    frame.draws[0].count = 6;
    require(compare(renderer, frame, "Nonzero resident range beyond65536 corners") == pixels, "Resident subranges must retain their full32-bit first/count addressing");
}

void prepared_projection_pixel_space(Renderer& renderer) {
    Frame prepared;
    prepared.world_views.push_back(projection());
    resident(prepared, renderer.world_mesh(triangle(1)));
    constexpr Extent resized{160, 112};
    compare(renderer, prepared, "Prepared camera with a different target extent", resized);
    Frame composed;
    composed.append_view(prepared, {7, 5, 110, 74});
    compare(renderer, composed, "Prepared camera with a different pane extent", resized);

    // Reflect an independently materialized TL frame using caller dimensions.
    // This catches incorrect resident reflection pivots as well as offsets;
    // the expected pixels do not reuse the encoded resident reflection.
    auto reflected = prepared;
    auto expected = materialize_world(prepared);
    reflected.reflect_world(float(resized.width), float(resized.height), true, true);
    expected.reflect_world(float(resized.width), float(resized.height), true, true);
    compare_to(renderer, reflected, expected, "Reflection around a different target extent", resized);
    reflected.reflect_world(120, 84, true, true);
    expected.reflect_world(120, 84, true, true);
    compare_to(renderer, reflected, expected, "Successive target reflections around different pivots", resized);

    reflected = prepared;
    expected = materialize_world(prepared);
    reflected.reflect_world(110, 74, true, true);
    expected.reflect_world(110, 74, true, true);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        Frame resident_pane, expected_pane;
        resident_pane.append_view(reflected, {7, 5, 110, 74});
        expected_pane.append_view(expected, {7, 5, 110, 74});
        compare_to(renderer, resident_pane, expected_pane, "Reflection around different pane pivots " + std::to_string(repeat), resized);
        if (repeat == 0) {
            reflected.reflect_world(124, 82, true, true);
            expected.reflect_world(124, 82, true, true);
        }
    }
}

void shared_model_meshes(Renderer& renderer) {
    toca::scene::ModelMeshes meshes(renderer);
    meshes.require_renderer(renderer);
    const auto vertices = triangle();
    const auto source_attributes = attributes(vertices);
    const auto shared = meshes.get(vertices, source_attributes);
    require(meshes.get(std::vector(vertices), std::vector(source_attributes)) == shared, "Separate identical model inputs must share one immutable upload");
    auto altered_vertices = vertices;
    altered_vertices[0].color ^= 0x10000;
    require(meshes.get(altered_vertices, source_attributes) != shared, "Model material bytes must distinguish shared resources");
    altered_vertices = vertices;
    altered_vertices[0].u += .125f;
    require(meshes.get(altered_vertices, source_attributes) != shared, "Model UVs must distinguish shared resources");
    for (unsigned kind = 0; kind < 3; ++kind) {
        auto altered = source_attributes;
        if (kind == 0) {
            altered[0].normal[0] += .125f;
        } else if (kind == 1) {
            altered[0].flags ^= 1;
        } else {
            altered[0].ghost_position[0] += .125f;
        }
        require(meshes.get(vertices, altered) != shared, "Model normals, ghost flags and shallow positions must distinguish shared resources");
    }
    for (unsigned kind = 0; kind < 4; ++kind) {
        auto invalid_vertices = vertices;
        auto invalid_attributes = source_attributes;
        const auto invalid = std::numeric_limits<float>::quiet_NaN();
        if (kind == 0) {
            invalid_vertices[0].position[0] = invalid;
        } else if (kind == 1) {
            invalid_vertices[0].v = invalid;
        } else if (kind == 2) {
            invalid_attributes[0].normal[1] = invalid;
        } else {
            invalid_attributes[0].ghost_position[2] = invalid;
        }
        bool rejected = false;
        try {
            meshes.get(std::move(invalid_vertices), std::move(invalid_attributes));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Model uploads must reject nonfinite geometry, UVs, normals and ghost positions");
    }
}

void model_transforms_lighting_and_clipping(Renderer& renderer) {
    const auto texture = gradient(renderer);
    for (unsigned mask = 0; mask < 8; ++mask) {
        const auto vertices = triangle(mask);
        const auto mesh = renderer.world_mesh(vertices, attributes(vertices));
        for (unsigned variant = 0; variant < 3; ++variant) {
            auto instance = model_instance();
            auto view = projection();
            if (variant == 1) {
                // Rotate model normals with its row-vector position basis.
                constexpr float angle = .23f;
                instance.object_to_world[0] = instance.object_to_world[5] = std::cos(angle);
                instance.object_to_world[1] = std::sin(angle);
                instance.object_to_world[4] = -std::sin(angle);
                instance.object_to_world[12] = .125f;
                instance.object_to_world[13] = -.25f;
                view.world_to_view[12] = -.125f;
                view.world_to_view[14] = .125f;
            } else if (variant == 2) {
                instance.direction_diffuse = {-.25f, .125f, -.875f, .875f};
                instance.ambient_flags = {180, 36, 92, 0};
            }
            Frame frame;
            frame.world_views = {view};
            model(frame, mesh, instance, {.texture = variant == 1 ? texture.get() : nullptr, .depth = true, .linear = true, .clamp = true});
            compare(renderer, frame, "Model object/camera transform, lighting and near mask " + std::to_string(mask) + "/" + std::to_string(variant));
        }
    }
    auto vertices = triangle();
    for (auto& vertex : vertices) {
        for (auto& position : vertex.position) {
            position *= 8;
        }
    }
    const auto mesh = renderer.world_mesh(vertices, attributes(vertices));
    Frame sky;
    sky.world_views = {projection()};
    model(sky, mesh, model_instance(1), {.depth = false});
    const auto oracle = materialize_world(sky);
    require(!oracle.vertices.empty()
                && std::ranges::all_of(oracle.vertices,
                    [](const Vertex& vertex) {
                        return vertex.z == std::bit_cast<float>(0x3f7fff58u) && vertex.diffuse == 0xffffffffu;
                    }),
        "Sky models must bypass far rejection and force original depth and white color after culling");
    compare(renderer, sky, "Sky white material, ignored far plane and forced depth");
    sky.model_instances[0].ambient_flags[3] = 0;
    require(materialize_world(sky).draws.empty(), "Ordinary model far rejection must remain active");
    compare(renderer, sky, "Ordinary model far rejection");
    std::swap(vertices[1], vertices[2]);
    sky.draws.clear();
    sky.model_instances.clear();
    model(sky, renderer.world_mesh(vertices, attributes(vertices)), model_instance(1));
    require(materialize_world(sky).draws.empty(), "Sky models must retain original backface culling");
    compare(renderer, sky, "Sky backface rejection before forced depth");
}

void finale_translation_first(Renderer& renderer) {
    const toca::scene::Lighting lighting{{.25f, -.375f, .75f}, .65f, {48, 80, 112}};
    // Adding the small Y component to -1e8 loses it before the final translation.
    // Starting from translation cancels the large X first and retains the shape.
    auto vertices = triangle();
    vertices[0].position = {-1e8f, -2.5f, 1.f};
    vertices[1].position = {-1e8f, 2.5f, -.5f};
    vertices[2].position = {-1e8f, -1.f, -1.f};
    const auto source_attributes = attributes(vertices);
    auto instance = model_instance(16);
    instance.object_to_world = {1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1e8f, 0, 6, 1};
    Frame frame;
    frame.world_views = {projection()};
    model(frame, renderer.world_mesh(vertices, source_attributes), instance);
    const auto expected = legacy_finale_cpu(vertices, source_attributes, instance.object_to_world, frame.world_views[0], lighting, {.depth = true});
    require(!expected.indices.empty(), "Translation-first cancellation must retain a visible finale triangle");
    compare_to(renderer, frame, expected, "Finale translation-first adversarial cancellation");
    auto ordinary = frame;
    ordinary.model_instances[0].ambient_flags[3] = 0;
    require(!materialize_world(ordinary).draws.empty(), "Source WVP arithmetic must retain the small local components without a rounded world-position intermediate");
    compare(renderer, ordinary, "Ordinary Source WVP adversarial cancellation");
    compare(renderer, frame, "Independent resident oracle translation-first cancellation");

    const auto texture = gradient(renderer);
    auto parent = identity, child = identity, object = identity;
    const auto rotate = [](std::array<float, 16>& matrix, float angle) {
        matrix[0] = matrix[5] = std::cos(angle);
        matrix[1] = std::sin(angle);
        matrix[4] = -std::sin(angle);
    };
    rotate(parent, -.28f);
    rotate(child, .09f);
    rotate(object, .17f);
    parent[12] = .2f;
    parent[13] = .1f;
    parent[14] = -.125f;
    child[12] = -.1f;
    child[13] = .14f;
    child[14] = .125f;
    object[12] = .13f;
    object[13] = -.17f;
    object[14] = .25f;
    toca::Model hierarchy;
    hierarchy.nodes = {{-1, parent, 0, {}}, {0, child, 0, {}}};
    const auto transforms = toca::scene::model_world_transforms(hierarchy, object);
    auto view = projection();
    rotate(view.world_to_view, -.11f);
    view.world_to_view[12] = -.125f;
    view.world_to_view[13] = .125f;
    for (const bool on_near : {false, true}) {
        for (unsigned mask = 0; mask < 8; ++mask) {
            for (unsigned node = 0; node < transforms.size(); ++node) {
                auto corners = triangle(mask, on_near);
                if (node == 1) {
                    for (auto& vertex : corners) {
                        vertex.color = 0xffffffff;
                    }
                }
                const auto normals = attributes(corners);
                auto actor = model_instance(16);
                actor.object_to_world = transforms[node];
                actor.direction_diffuse = {-.25f, .125f, -.875f, .875f};
                actor.ambient_flags = {180, 36, 92, 16};
                auto camera = view;
                camera.world_to_view[14] = node == 0 ? -.125f : -.25f;
                const State state{.texture = node == 1 ? texture.get() : nullptr, .depth = true, .key = node == 1};
                Frame resident_frame;
                resident_frame.world_views = {camera};
                model(resident_frame, renderer.world_mesh(corners, normals), actor, state);
                const toca::scene::Lighting actor_lighting{{-.25f, .125f, -.875f}, .875f, {180, 36, 92}};
                const auto cpu = legacy_finale_cpu(corners, normals, transforms[node], camera, actor_lighting, state);
                require((mask == 7) == cpu.draws.empty(), "Finale hierarchy must exercise every surviving/rejected near mask");
                if (!on_near && mask != 7) {
                    require(cpu.indices.size() == (std::popcount(mask) == 1 ? 6 : 3), "Finale hierarchy must retain the original clipped fan");
                }
                compare_to(renderer, resident_frame, cpu,
                    "Finale hierarchy, lighting/material and near mask " + std::to_string(mask) + "/" + std::to_string(node) + (on_near ? " at boundary" : ""));
            }
        }
    }
}

void model_ghosts_and_personal_names(Renderer& renderer) {
    const auto texture = gradient(renderer);
    auto vertices = triangle(1);
    for (auto& vertex : vertices) {
        vertex.v = .25f + vertex.v * .5f;
    }
    std::vector<Uint32> ordinary;
    for (const bool hidden : {false, true}) {
        const auto mesh = renderer.world_mesh(vertices, attributes(vertices, hidden));
        for (const Uint32 flags : {0u, 2u, 6u, 8u, 10u}) {
            Frame frame;
            frame.world_views = {projection()};
            frame.quad({}, 0, 0, float(extent.width), float(extent.height), 0xff604020);
            auto instance = model_instance(flags);
            instance.uv_remap = {.25f, .5f, 0, 0};
            model(frame, mesh, instance, {.texture = texture.get(), .depth = true, .blend = bool(flags & 2), .linear = true, .clamp = true});
            const auto oracle = materialize_world(frame);
            if (flags & 2) {
                const auto expected_alpha = hidden || (flags & 4) ? 96u : 150u;
                require(std::ranges::all_of(oracle.vertices.begin() + 4, oracle.vertices.end(),
                            [&](const Vertex& vertex) {
                                return vertex.diffuse >> 24 == expected_alpha;
                            }),
                    "Ghost body, wheel and shallow interiors must retain their distinct alpha");
            }
            const auto pixels = compare(renderer, frame, "Model ghost/name flags " + std::to_string(flags) + (hidden ? " shallow" : " exterior"));
            if (!hidden && flags == 0) {
                ordinary = pixels;
            } else {
                require(pixels != ordinary || (hidden && flags == 0), "Ghost, wheel and personal-name variants must change actual pixels");
            }
        }
    }
}

void ghost_software_material(Renderer& renderer) {
    auto vertices = triangle();
    std::vector<ModelAttribute> normals(vertices.size(), {{0, 0, 0}, 0, {}});
    const auto mesh = renderer.world_mesh(vertices, normals);
    Frame frame;
    frame.clear_color = {0, 0, 0, 1};
    frame.world_views = {projection()};
    auto instance = model_instance(2);
    instance.direction_diffuse[3] = 0;
    instance.ambient_flags = {152, 152, 152, 2};
    model(frame, mesh, instance, {.blend = true});
    const auto pixels = render(renderer, frame);
    // 487afe ambient: RNE((152/255)*(150,150,255))=(89,89,152).
    // Alpha150 over black produces(52,52,89); using the unused hardware
    // ambient material(100,100,180) instead would produce(35,35,63).
    require((pixels.at(48 * extent.width + 64) & 0xffffffu) == 0x343459u, "Race ghosts must use the software face diffuse for both lighting terms");
    const toca::scene::Lighting light{{0, 0, 1}, 0, {152, 152, 152}};
    require(toca::scene::ghost_vertex_color({0, 0, 0}, light) == 0x96595998u, "CPU ghost oracle must preserve the source software material's ambient RGB and alpha");
}

void original_untextured_ghost_material(Renderer& renderer) {
    auto vertices = triangle();
    for (auto& vertex : vertices) {
        vertex.color = 0x2500ffffu;
    }
    const std::vector<ModelAttribute> normals(vertices.size(), {{0, 0, 0}, 0, {}});
    Frame frame;
    frame.clear_color = {1, 0, 1, 1};
    frame.world_views = {projection()};
    auto instance = model_instance(2 | 32);
    instance.direction_diffuse[3] = 0;
    instance.ambient_flags = {255, 255, 255, 2 | 32};
    model(frame, renderer.world_mesh(vertices, normals), instance, {.blend = true});
    const auto pixels = render(renderer, frame);
    // 495912..91e keep untextured face RGB and write alphaFF even when
    // the CM3D face's own alpha and the ghost texture material differ.
    require(pixels.at(48 * extent.width + 64) == 0xff00ffffu, "Original untextured ghosts must retain opaque face cyan instead of the textured ghost tint");
    compare(renderer, frame, "Original untextured ghost face material");
}

void reference_hardware_streams() {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("TOCA reference hardware stream checks", int(extent.width), int(extent.height), SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    check(bool(window), "Create reference hardware stream window");
    Renderer renderer(window.get(), RasterProfile::reference_dgvoodoo263);
    constexpr float w = 0x1p-23f;
    constexpr Uint32 color = 0xffc86432u;
    WorldProjection view{identity, {1, 1, w / 4, 1}, {1, -w / 4, float(extent.width), float(extent.height)}};
    view.world_to_view = {};
    view.world_to_view[0] = 1;
    view.world_to_view[4] = 0x1.000002p0f;
    view.world_to_view[9] = 1;
    view.world_to_view[14] = w;
    view.world_to_view[15] = 1;
    // Source VS877b's staged DP4 produces X=(-w,+w,0). Separate float
    // MUL+ADD rounds each large Y product first and collapses all three X
    // values to zero. These analytic vectors do not use materialize_world.
    const float top = .4947916666666667f * w;
    const std::array<WorldVertex, 3> triangle_vertices{{
        {{-8388608, 8388607, top}, color},
        {{-8388610, 8388609, top}, color},
        {{-8388609, 8388608, -w / 2}, color},
    }};
    Frame triangle_frame;
    triangle_frame.clear_color = {0, 0, 0, 1};
    triangle_frame.world_triangle({}, triangle_vertices, view);
    const auto triangle_pixels = render(renderer, triangle_frame);
    require(triangle_pixels.at(48 * extent.width + 64) == color, "Reference D3DLVERTEX triangles must retain the staged fused view transform");
    // The top edge projects toY24.25 before the captured W/viewport bias,
    // thenY24.75. Pixel centerY24.5 must remain outside, Y25.5 inside.
    require(triangle_pixels.at(24 * extent.width + 64) == 0xff000000u && triangle_pixels.at(25 * extent.width + 64) == color,
        "Reference hardware projection must retain its half-pixel viewport convention");

    view.world_to_view[9] = 0;
    view.world_to_view[10] = 1;
    view.world_to_view[14] = 0;
    view.scale_near_far = {1, 1, w, 4 * w};
    view.depth_size[0] = 4.f / 3;
    view.depth_size[1] = -(4.f / 3) * w;
    const std::array<WorldVertex, 2> endpoints{{{{-8388608, 8388607, w / 2}, color}, {{-8388610, 8388609, 2 * w}, color}}};
    Frame line_frame;
    line_frame.clear_color = {0, 0, 0, 1};
    line_frame.world_line({.lines = true}, endpoints, view);
    const auto line_pixels = render(renderer, line_frame);
    // After the same FMA cancellation, endpoints have(X,Z)=(-w,w/2)
    // and(+w,2w). Hardware near clipping atZ=w starts atX=-w/3:
    // screenX43+1/6..96.5, Y48.5. Omitting clipping colorsX32; losing
    // the fused DP4 instead collapses the entire line ontoX64.5.
    require(line_pixels.at(48 * extent.width + 80) == color && line_pixels.at(48 * extent.width + 32) == 0xff000000u,
        "Reference D3DLVERTEX lines must retain fused projection and hardware near clipping");

    const WorldProjection quad_view{identity, {1, 1, 1, 16}, {16.f / 15, -16.f / 15, float(extent.width), float(extent.height)}};
    Frame gradient_frame;
    gradient_frame.clear_color = {0, 0, 0, 1};
    const std::array<WorldVertex, 2> beyond_far{{{{-24, 0, 32}, color}, {{24, 0, 32}, color}}};
    gradient_frame.world_line({.lines = true}, beyond_far, quad_view);
    // The hardware half-pixel bias puts this horizontal segment exactly at
    // X32..96,Y48. Width1.375 covers both rows47/48; duplicated endpoint
    // attributes give the same alpha on both sides of the centerline.
    const std::array<WorldVertex, 2> gradient{{{{-1.015625f, 1.f / 48, 2}, 0x00ffffffu}, {{.984375f, 1.f / 48, 2}, 0xffffffffu}}};
    gradient_frame.world_line({.blend = true, .lines = true}, gradient, quad_view);
    const auto gradient_pixels = render(renderer, gradient_frame);
    for (const auto y : {47u, 48u}) {
        require(gradient_pixels.at(y * extent.width + 48) == 0xff424242u && gradient_pixels.at(y * extent.width + 80) == 0xffc1c1c1u,
            "Reference quadrilateral lines must cover their captured width and interpolate the endpoint gradient equally across their width");
        require(gradient_pixels.at(y * extent.width + 31) == 0xff000000u && gradient_pixels.at(y * extent.width + 96) == 0xff000000u,
            "Reference quadrilateral lines must end at their original segment endpoints");
    }
    require(gradient_pixels.at(46 * extent.width + 64) == 0xff000000u && gradient_pixels.at(49 * extent.width + 64) == 0xff000000u && gradient_frame.dynamic_world_lines.size() == 4
                && gradient_frame.draws[1].first == 2 && gradient_frame.draws[1].count == 2 && renderer.last_frame_timing().dynamic_line_draw_calls == 2,
        "Reference line expansion must retain raw endpoint ranges, reject far lines and avoid widening beyond its specified rectangle");

    Frame coverage_frame;
    coverage_frame.clear_color = {0, 0, 0, 1};
    // Exact hardware center Y=48+50/256: the captured 1.4 line excludes
    // pixel center 47.5. A nominal 1.4 triangle rectangle includes it,
    // while its independently measured 1.375 coverage excludes it.
    const std::array<WorldVertex, 2> phase{{{{-1.5078125f, 13.f / 1024, 2}, color}, {{1.4921875f, 13.f / 1024, 2}, color}}};
    coverage_frame.world_line({.lines = true}, phase, quad_view);
    const auto coverage_pixels = render(renderer, coverage_frame);
    require(coverage_pixels.at(47 * extent.width + 64) == 0xff000000u && coverage_pixels.at(48 * extent.width + 64) == color,
        "Reference rectangular-line coverage must retain the measured subpixel boundary of the captured rasterizer");

    Frame crossing_frame;
    crossing_frame.clear_color = {0, 0, 0, 1};
    const std::array<WorldVertex, 2> crossing_far{{{{-6, 0, 8}, color}, {{24, 0, 32}, color}}};
    crossing_frame.world_line({.lines = true}, crossing_far, quad_view);
    const auto crossing_pixels = render(renderer, crossing_frame);
    // Z16 cuts the world segment atX4, whose biased screenX is80.5.
    require(crossing_pixels.at(48 * extent.width + 64) == color && crossing_pixels.at(48 * extent.width + 90) == 0xff000000u,
        "Reference quadrilateral lines must clip the original segment at the far plane before pixel-space expansion");
    renderer.wait_idle();
}

void reference_far_plane(Renderer& native_renderer) {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("TOCA reference quantized far plane", int(extent.width), int(extent.height), SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    check(bool(window), "Create reference far-plane window");
    Renderer reference_renderer(window.get(), RasterProfile::reference_dgvoodoo263);
    const WorldProjection view{identity, {1, 1, .25f, 250}, {65601.f / 65536, -16400.f / 65536, float(extent.width), float(extent.height)}};
    constexpr Uint32 color = 0xff20c080u;
    const auto pixels_at_depth = [&](Renderer& renderer, float w) {
        // The source Q16 projection gives Z/W=1 at W=16400/65,
        // about 252.31, while the native distance limit remains 250.
        const std::vector<WorldVertex> vertices{{{-w / 2, w / 2, w}, color}, {{w / 2, w / 2, w}, color}, {{0, -w / 2, w}, color}};
        Frame frame;
        frame.world_views = {view};
        resident(frame, renderer.world_mesh(vertices), {.depth = true});
        return render(renderer, frame);
    };
    require(pixels_at_depth(native_renderer, 249).at(48 * extent.width + 64) == color, "Native resident geometry inside its nominal far distance must remain visible");
    const auto native_beyond = pixels_at_depth(native_renderer, 251);
    require(std::ranges::all_of(native_beyond,
                [](Uint32 value) {
                    return value == background;
                }),
        "Native resident geometry must retain its nominal far-distance rejection");
    require(pixels_at_depth(reference_renderer, 251).at(48 * extent.width + 64) == color,
        "Reference resident geometry beyond nominal far must survive when the quantized source clip Z remains below W");
    const auto reference_beyond = pixels_at_depth(reference_renderer, 254);
    require(std::ranges::all_of(reference_beyond,
                [](Uint32 value) {
                    return value == background;
                }),
        "Reference resident geometry beyond the quantized source clip Z limit must still be rejected by GPU clipping");
    reference_renderer.wait_idle();
}

void ordered_model_instances(Renderer& renderer) {
    auto vertices = triangle();
    for (auto& vertex : vertices) {
        vertex.color = 0x80ffffff;
    }
    const auto mesh = renderer.world_mesh(vertices, attributes(vertices));
    Frame frame;
    frame.world_views = {projection(), projection()};
    frame.world_views[1].world_to_view[12] = .25f;
    auto red = model_instance(), blue = model_instance();
    red.direction_diffuse[3] = blue.direction_diffuse[3] = 0;
    red.ambient_flags = {255, 40, 20, 0};
    blue.ambient_flags = {20, 40, 255, 0};
    const State alpha{.depth = true, .blend = true, .depth_write = false};
    model(frame, mesh, red, alpha);
    model(frame, mesh, blue, alpha);
    const auto ordered = compare(renderer, frame, "Adjacent ordered alpha model instances");
    const auto joined = renderer.last_frame_timing();
    require(joined.model_draw_calls == 1 && joined.model_instances == 2 && joined.instanced_draw_calls == 1,
        "Adjacent matching model ranges must issue one actual two-instance GPU draw");
    auto explicit_instances = frame;
    explicit_instances.draws.resize(1);
    explicit_instances.draws[0].instance_count = 2;
    require(compare(renderer, explicit_instances, "Explicit ordered model instance range") == ordered, "Explicit instance ranges must retain draw order");
    auto reversed = frame;
    std::swap(reversed.model_instances[0], reversed.model_instances[1]);
    require(compare(renderer, reversed, "Reversed alpha instance order") != ordered, "Transparent model order must affect overlapping pixels");
    const auto texture = gradient(renderer);
    for (unsigned barrier = 0; barrier < 6; ++barrier) {
        auto separated = frame;
        if (barrier == 0) {
            separated.draws[1].state.texture = texture.get();
        } else if (barrier == 1) {
            Frame tl;
            tl.quad({.blend = true}, 30, 30, 18, 18, 0x8000ff00);
            separated.vertices = tl.vertices;
            separated.indices = tl.indices;
            separated.draws.insert(separated.draws.begin() + 1, tl.draws[0]);
        } else if (barrier == 2) {
            separated.draws[1].world_view = 1;
        } else if (barrier == 3) {
            separated.draws[1].overlay = true;
        } else if (barrier == 4) {
            separated.draws[1].viewport = Viewport{1, 0, extent.width - 1, extent.height};
        } else {
            // Equal instance contents at a noncontiguous address may not join.
            separated.model_instances.push_back(blue);
            separated.draws[1].model_instance = 2;
        }
        compare(renderer, separated, "Model instancing barrier " + std::to_string(barrier));
        const auto calls = renderer.last_frame_timing();
        require(calls.model_draw_calls == 2 && calls.model_instances == 2 && calls.instanced_draw_calls == 0,
            "Texture, TL, view, overlay, viewport and instance-address barriers must prevent merging");
    }
    auto depth = frame;
    depth.draws[0].state.depth_write = depth.draws[1].state.depth_write = true;
    depth.model_instances[1].object_to_world[14] = 1;
    const auto depth_pixels = compare(renderer, depth, "Ordered model depth writes");
    std::swap(depth.model_instances[0], depth.model_instances[1]);
    require(compare(renderer, depth, "Reversed model depth writes") != depth_pixels, "Instance order must preserve transparent depth writes");
}

void model_split_reflections_and_retention(Renderer& renderer) {
    Frame prepared;
    prepared.world_views = {projection()};
    std::weak_ptr<const WorldMesh> retained;
    {
        toca::scene::ModelMeshes cache(renderer);
        const auto vertices = triangle(1);
        auto mesh = cache.get(vertices, attributes(vertices));
        retained = mesh;
        model(prepared, mesh, model_instance(16));
    }
    require(!retained.expired(), "Queued model frames must retain uploads after scene cache destruction");
    const auto old = prepared;
    const auto old_pixels = compare(renderer, old, "Retained old model pose and lighting");
    prepared.model_instances[0].object_to_world[12] += .5f;
    prepared.model_instances[0].ambient_flags[0] = 200;
    require(compare(renderer, prepared, "New pose with shared model upload") != old_pixels, "New pose/lighting must change pixels without mutating an older frame");
    require(compare(renderer, old, "Old queued pose after new frame") == old_pixels && old.draws[0].mesh == prepared.draws[0].mesh,
        "Older frames must retain exact instance data while reusing immutable corners");
    for (const bool vertical : {false, true}) {
        constexpr Extent size{127, 97};
        const auto w = vertical ? size.width / 2 : size.width;
        const auto h = vertical ? size.height : size.height / 2;
        Frame first = old, second = prepared;
        first.reflect_world(128, 96, true, false);
        second.reflect_world(128, 96, false, true);
        Frame combined;
        combined.append_view(first, {0, 0, w, h});
        combined.append_view(second, {vertical ? w : 0, vertical ? 0 : h, size.width - (vertical ? w : 0), size.height - (vertical ? 0 : h)});
        require(combined.model_instances.size() == 2 && combined.draws[0].model_instance == 0 && combined.draws[1].model_instance == 1,
            "Split composition must copy instances and rebase the second player's draw index");
        for (const bool multisample : {false, true}) {
            combined.multisample = multisample;
            compare(renderer, combined, "Reflected split model instances", size);
        }
        auto expected = materialize_world(combined);
        combined.reflect_world(float(size.width), float(size.height), true, true);
        expected.reflect_world(float(size.width), float(size.height), true, true);
        compare_to(renderer, combined, expected, "Composed model reflection offsets", size);
    }
}

void binding_and_instance_reuse(Renderer& renderer) {
    const auto source = triangle();
    const auto mesh = renderer.world_mesh(source);
    Frame repeated;
    repeated.world_views = {projection()};
    for (unsigned index = 0; index < 3; ++index) {
        resident(repeated, mesh);
    }
    const auto first_pixels = compare(renderer, repeated, "Repeated resident buffer and projection bindings");
    auto calls = renderer.last_frame_timing();
    require(calls.draw_calls == 3 && calls.bindings.pipelines == 1 && calls.bindings.storage_buffers == 1 && calls.bindings.vertex_uniforms == 3
                && calls.bindings.fragment_uniforms == 1 && calls.bindings.samplers == 1,
        "Separate resident ranges must reuse their projection, mode, mesh and material bindings");
    require(compare(renderer, repeated, "Resident bindings reset on the next pass") == first_pixels && renderer.last_frame_timing().bindings == calls.bindings,
        "Resident binding caches must reset for each new render pass");

    const auto model_mesh = renderer.world_mesh(source, attributes(source));
    Frame retained;
    retained.world_views = {projection()};
    const auto count = renderer.last_frame_timing().instance_capacity / sizeof(ModelInstance) + 32;
    for (std::size_t index = 0; index < count; ++index) {
        auto instance = model_instance();
        instance.object_to_world[12] = float(index % 3) * .125f;
        instance.ambient_flags[0] = Uint32(index % 200);
        model(retained, model_mesh, instance, {});
    }
    const auto retained_pixels = compare(renderer, retained, "Model instance buffer growth");
    const auto capacity = renderer.last_frame_timing();
    auto slightly_larger = retained;
    auto instance = model_instance();
    instance.object_to_world[13] = .375f;
    model(slightly_larger, model_mesh, instance, {});
    compare(renderer, slightly_larger, "Model instance spare capacity and last instance upload");
    require(renderer.last_frame_timing().buffer_growths == 0 && renderer.last_frame_timing().instance_capacity == capacity.instance_capacity,
        "A fitting model instance upload must reuse its GPU and staging capacity");
    compare(renderer, repeated, "Small world frame after model uploads");
    require(compare(renderer, retained, "Retained model upload after a smaller frame") == retained_pixels && renderer.last_frame_timing().buffer_growths == 0,
        "Large-small-large instance uploads must retain exact old poses, lighting and GPU capacity");
}

void dynamic_clipping_and_order(Renderer& renderer) {
    const auto texture = gradient(renderer);
    for (const bool on_near : {false, true}) {
        for (unsigned mask = 0; mask < 8; ++mask) {
            const auto source = triangle(mask, on_near);
            Frame frame;
            frame.world_triangle({.texture = texture.get(), .depth = true, .blend = true, .depth_write = false, .screen = bool(mask & 1), .linear = true},
                {source[0], source[1], source[2]}, projection());
            require(frame.vertices.empty() && frame.indices.empty() && frame.dynamic_world_vertices.size() == 3 && frame.draws[0].dynamic_world && !frame.draws[0].mesh,
                "Dynamic triangles must upload source corners without CPU projection or resident meshes");
            compare(renderer, frame, "Dynamic near mask " + std::to_string(mask) + (on_near ? " at boundary" : ""));
            require(renderer.last_frame_timing().dynamic_world_draw_calls == 1, "Dynamic-only frames must issue an actual world-shader draw");
        }
    }

    auto red = triangle(1), blue = triangle();
    for (auto& vertex : red) {
        vertex.color = 0x80e02040;
    }
    for (auto& vertex : blue) {
        vertex.color = 0x804020e0;
    }
    const State blend{.texture = texture.get(), .blend = true, .linear = true};
    Frame frame;
    frame.quad({}, 0, 0, float(extent.width), float(extent.height), 0xff102030);
    frame.world_triangle(blend, {red[0], red[1], red[2]}, projection());
    resident(frame, renderer.world_mesh(triangle()), {.depth = true});
    frame.quad({.blend = true}, 40, 30, 32, 24, 0x8000ff00);
    frame.world_triangle(blend, {blue[0], blue[1], blue[2]}, projection());
    auto expected = materialize_world(frame);
    const auto original = compare_to(renderer, frame, expected, "Interleaved dynamic/resident/TL alpha order");
    require(renderer.last_frame_timing().dynamic_world_draw_calls == 2, "Resident and TL draws must remain barriers between dynamic ranges");
    auto reversed = frame;
    std::swap(reversed.draws[1], reversed.draws[4]);
    require(compare(renderer, reversed, "Reversed dynamic alpha order") != original, "Dynamic transparent source order must affect overlapping pixels");
    frame.reflect_world(128, 96, true, true);
    expected.reflect_world(128, 96, true, true);
    compare_to(renderer, frame, expected, "Dynamic reflection after original culling");
    const auto overlay = frame.draws.size();
    frame.quad({}, 2, 2, 4, 4, 0xffffff00);
    frame.mark_overlay(overlay);
    for (const bool vertical : {false, true}) {
        constexpr Extent size{127, 97};
        const Uint32 w = vertical ? size.width / 2 : size.width, h = vertical ? size.height : size.height / 2;
        Frame combined;
        combined.append_view(frame, {0, 0, w, h});
        combined.append_view(reversed, {vertical ? w : 0, vertical ? 0 : h, size.width - (vertical ? w : 0), size.height - (vertical ? 0 : h)});
        for (const bool multisample : {false, true}) {
            combined.multisample = multisample;
            compare(renderer, combined, "Dynamic odd split panes and overlay", size);
        }
    }
}

void dynamic_upload_reuse(Renderer& renderer) {
    const auto source = triangle();
    Frame retained;
    const auto triangles = renderer.last_frame_timing().dynamic_world_capacity / (3 * sizeof(WorldVertex)) + 32;
    for (std::size_t index = 0; index < triangles; ++index) {
        auto face = source;
        if (index + 1 != triangles) {
            for (auto& vertex : face) {
                vertex.position[0] += 20;
            }
        }
        retained.world_triangle({}, {face[0], face[1], face[2]}, projection());
    }
    require(
        retained.draws.size() == 1 && retained.world_vertex_count() == triangles * 6, "Adjacent dynamic triangles must share one ordered range and participate in frame counts");
    const auto pixels = compare(renderer, retained, "Dynamic upload growth and nonzero visible tail");
    const auto capacity = renderer.last_frame_timing();
    auto larger = retained;
    auto moved = source;
    for (auto& vertex : moved) {
        vertex.position[0] += .75f;
    }
    larger.world_triangle({}, {moved[0], moved[1], moved[2]}, projection());
    require(compare(renderer, larger, "Dynamic spare capacity with new trailing triangle") != pixels, "A changed final triangle must reach the reused dynamic upload");
    require(renderer.last_frame_timing().buffer_growths == 0 && renderer.last_frame_timing().dynamic_world_capacity == capacity.dynamic_world_capacity,
        "Fitting dynamic uploads must reuse GPU and staging capacity");
    auto reused = retained;
    const auto cpu_capacity = reused.dynamic_world_vertices.capacity();
    reused.clear();
    require(reused.dynamic_world_vertices.empty() && reused.dynamic_world_vertices.capacity() >= cpu_capacity && reused.draws.empty() && reused.world_views.empty(),
        "Clearing a live frame must retain dynamic source capacity and remove all draws/projections");
    reused.world_triangle({}, {moved[0], moved[1], moved[2]}, projection());
    compare(renderer, reused, "Small dynamic upload after a larger frame");
    require(compare(renderer, retained, "Retained dynamic frame after upload reuse") == pixels && renderer.last_frame_timing().buffer_growths == 0,
        "Large-small-large uploads must retain original dynamic source data and GPU capacity");
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto broken = retained;
        if (invalid == 0) {
            broken.draws[0].count += 6;
        } else if (invalid == 1) {
            broken.draws[0].state.lines = true;
        } else if (invalid == 2) {
            broken.draws[0].model_instance = 0;
        } else {
            broken.dynamic_world_vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        }
        for (const bool compose : {false, true}) {
            bool rejected = false;
            try {
                if (compose) {
                    Frame combined;
                    combined.append_view(broken, {0, 0, extent.width, extent.height});
                } else {
                    renderer.draw_offscreen(broken, extent);
                }
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Invalid dynamic corner ranges, line/model state and nonfinite source data must fail before upload/composition");
        }
    }
}

void dynamic_lines(Renderer& renderer) {
    constexpr std::array<float, 3> depths{.5f, 1.f, 4.f};
    for (const auto a : depths) {
        for (const auto b : depths) {
            Frame frame;
            const std::array<WorldVertex, 2> endpoints{{{{-.3f, .2f, a}, 0xe1fc1075}, {{.7f, -.3f, b}, 0x2960c3e2}}};
            frame.world_line({.lines = true}, endpoints, projection());
            const auto expected = materialize_world(frame);
            const auto pixels = compare_to(renderer, frame, expected, "Line below/equal/above near with truncated channel clipping");
            require(frame.vertices.empty() && frame.indices.empty() && frame.dynamic_world_vertices.empty() && frame.dynamic_world_lines.size() == 2 && frame.draws[0].dynamic_world
                        && renderer.last_frame_timing().dynamic_line_draw_calls == 1,
                "Line-only frames must retain world endpoints and issue an actual hardware-line shader draw");
            const auto blank = std::ranges::all_of(pixels, [](Uint32 value) {
                return value == background;
            });
            require(expected.vertices.empty() == (a < 1 && b < 1), "Line clipping must retain endpoints exactly on near");
            if (a < 1 && b < 1) {
                require(blank, "Both-below-near lines must leave every target pixel untouched");
            } else if (a > 1 || b > 1 || (a == 1 && b == 1)) {
                require(!blank, "Nondegenerate surviving lines must cover actual pixels");
            }
        }
    }
    // Cover source float arithmetic with a nonidentity camera and a line that
    // crosses the viewport; retain hardware clipping rather than triangle culling.
    auto view = projection();
    view.world_to_view[0] = view.world_to_view[5] = .96875f;
    view.world_to_view[1] = .25f;
    view.world_to_view[4] = -.25f;
    view.world_to_view[12] = .125f;
    view.world_to_view[14] = .25f;
    const std::array<WorldVertex, 2> line{{{{-6, 1, 2}, 0xfff09630}, {{6, -1, 4}, 0x00f09630}}};
    Frame mixed_frame;
    mixed_frame.quad({}, 0, 0, float(extent.width), float(extent.height), 0xff203040);
    mixed_frame.world_line({.depth = true, .blend = true, .lines = true}, line, view);
    const auto face = triangle(1);
    mixed_frame.world_triangle({.depth = true, .blend = true}, {face[0], face[1], face[2]}, projection());
    resident(mixed_frame, renderer.world_mesh(triangle()), {.depth = true});
    mixed_frame.world_line({.blend = true, .lines = true}, line, view);
    mixed_frame.quad({.blend = true}, 44, 34, 18, 18, 0x804080c0);
    compare(renderer, mixed_frame, "Mixed TL/resident/streamed triangles and hardware lines");
    require(renderer.last_frame_timing().dynamic_line_draw_calls == 2, "Triangle, resident and TL barriers must retain separate line draw ranges");
    auto expected = materialize_world(mixed_frame);
    mixed_frame.reflect_world(128, 96, true, true);
    expected.reflect_world(128, 96, true, true);
    compare_to(renderer, mixed_frame, expected, "Line reflection after source clipping");
    for (const bool vertical : {false, true}) {
        constexpr Extent size{127, 97};
        const Uint32 w = vertical ? size.width / 2 : size.width, h = vertical ? size.height : size.height / 2;
        Frame combined;
        combined.append_view(mixed_frame, {0, 0, w, h});
        combined.append_view(mixed_frame, {vertical ? w : 0, vertical ? 0 : h, size.width - (vertical ? w : 0), size.height - (vertical ? 0 : h)});
        const auto first_overlay = combined.draws.size();
        combined.quad({}, 1, 1, 3, 3, 0xffff00ff);
        combined.mark_overlay(first_overlay);
        for (const bool multisample : {false, true}) {
            combined.multisample = multisample;
            compare(renderer, combined, "Odd split line/triangle source offsets and MSAA overlay", size);
        }
    }

    Frame retained;
    const auto count = renderer.last_frame_timing().dynamic_line_capacity / (2 * sizeof(WorldVertex)) + 32;
    for (std::size_t index = 0; index < count; ++index) {
        auto shifted = line;
        if (index + 1 != count) {
            for (auto& endpoint : shifted) {
                endpoint.position[1] += 100;
            }
        }
        retained.world_line({.blend = true, .lines = true}, shifted, view);
    }
    const auto pixels = compare(renderer, retained, "Line upload growth and visible tail at a nonzero offset");
    require(retained.draws.size() == 1 && retained.world_vertex_count() == count * 2, "Adjacent lines must merge without losing endpoint or frame counts");
    const auto capacity = renderer.last_frame_timing().dynamic_line_capacity;
    auto reused = retained;
    const auto cpu_capacity = reused.dynamic_world_lines.capacity();
    reused.clear();
    require(reused.dynamic_world_lines.empty() && reused.dynamic_world_lines.capacity() == cpu_capacity, "Clearing live frames must retain line source capacity");
    reused.world_line({.lines = true}, line, projection());
    compare(renderer, reused, "Small line frame after a larger upload");
    require(compare(renderer, retained, "Old line frame after cyclic upload reuse") == pixels && renderer.last_frame_timing().dynamic_line_capacity == capacity
                && renderer.last_frame_timing().buffer_growths == 0,
        "Large-small-large line uploads must retain source data and reuse GPU/staging capacity");
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto broken = retained;
        if (invalid == 0) {
            broken.draws[0].first = 1;
        } else if (invalid == 1) {
            broken.draws[0].count += 2;
        } else if (invalid == 2) {
            broken.dynamic_world_lines.pop_back();
        } else {
            broken.dynamic_world_lines[0].position[2] = std::numeric_limits<float>::quiet_NaN();
        }
        for (const bool compose : {false, true}) {
            bool rejected = false;
            try {
                if (compose) {
                    Frame combined;
                    combined.append_view(broken, {0, 0, extent.width, extent.height});
                } else {
                    renderer.draw_offscreen(broken, extent);
                }
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Incomplete, nonfinite or invalid line endpoint ranges must fail before GPU submission/composition");
        }
    }
}

} // namespace

int main() {
    try {
        source_projection_goldens();
        source_lighting_golden();
        source_near_goldens();
        check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated resident geometry video");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA resident world geometry checks", 128, 96, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        check(bool(window), "Create isolated resident geometry window");
        Renderer renderer(window.get());
        renderer.set_model_lighting_table(toca::scene::model_lighting_angles());
        near_clipping(renderer);
        rejection_and_view_transform(renderer);
        ordering_reflections_and_composition(renderer);
        large_resident_geometry(renderer);
        prepared_projection_pixel_space(renderer);
        shared_model_meshes(renderer);
        model_transforms_lighting_and_clipping(renderer);
        finale_translation_first(renderer);
        model_ghosts_and_personal_names(renderer);
        ghost_software_material(renderer);
        original_untextured_ghost_material(renderer);
        reference_hardware_streams();
        reference_far_plane(renderer);
        ordered_model_instances(renderer);
        model_split_reflections_and_retention(renderer);
        binding_and_instance_reuse(renderer);
        dynamic_clipping_and_order(renderer);
        dynamic_upload_reuse(renderer);
        dynamic_lines(renderer);
        renderer.wait_idle();
        std::cout << "PASS: resident world/model geometry matches CPU transforms, lighting, clipping, ghost/name/sky materials, ordered instancing, reflections, split views and "
                     "retained meshes and reusable dynamic triangle/line uploads\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
