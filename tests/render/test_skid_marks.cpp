#include "scene/skid_marks_render.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::int32_t one = 65536;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SkidMarkQuad descriptor(std::uint32_t color = 0x80602010) {
    // Original441d10 indices0,2,1 /0,3,2 have clockwise screen winding.
    return {{{{-one, one / 2, 4 * one}, {-one, -one / 2, 4 * one}, {one, -one / 2, 4 * one}, {one, one / 2, 4 * one}}}, color, 7, 1};
}

SkidMarkFrame single(std::uint32_t color = 0x80602010) {
    SkidMarkFrame result;
    result.quads.push_back(descriptor(color));
    result.persistent = 1;
    return result;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const gpu::Frame& frame, const std::string& name = {}) {
    auto submitted = frame;
    if (submitted.draws.empty()) {
        submitted.quad({}, 0, 0, 1, 1, gpu::background);
    }
    expect(renderer.draw(submitted, false), "Skid marks require their private GPU drawable");
    if (!name.empty()) {
        renderer.save_screenshot(name);
    }
    const auto pixels = renderer.readback();
    if (!frame.dynamic_world_vertices.empty()) {
        auto expected = test::materialize_world(frame);
        if (expected.draws.empty()) {
            expected.quad({}, 0, 0, 1, 1, gpu::background);
        }
        expect(renderer.draw(expected, false), "CPU materialized skid oracle requires its private GPU drawable");
        expect(pixels == renderer.readback(), "Streamed skid shader pixels must exactly equal original CPU clipping/projection, depth and blending");
    }
    return pixels;
}

void source_geometry(const CameraInputs& camera) {
    const auto marks = single();
    gpu::Frame frame;
    append_skid_marks(frame, camera, marks);
    expect(frame.draws.size() == 1 && frame.vertices.empty() && frame.indices.empty() && frame.dynamic_world_vertices.size() == 6 && frame.world_views.size() == 1,
        "441d10 must stream the two original source triangles without CPU projection or resident allocation");
    const auto& draw = frame.draws.front();
    expect(draw.dynamic_world && !draw.mesh && draw.first == 0 && draw.count == 12 && draw.vertex_offset == 0 && draw.world_view == 0 && draw.model_instance == UINT32_MAX
               && draw.instance_count == 1,
        "Contiguous skid source triangles must share a dynamic six-output-corners-per-triangle range");
    const auto& state = frame.draws.front().state;
    expect(state.depth && state.depth_write && state.blend && !state.texture && !state.key && !state.fog && !state.screen,
        "Skid marks retain untextured alpha blending and the original world depth test/write");
    constexpr std::array source_corners{0u, 2u, 1u, 0u, 3u, 2u};
    for (std::size_t corner = 0; corner < source_corners.size(); ++corner) {
        const auto& vertex = frame.dynamic_world_vertices[corner];
        expect(vertex.color == 0x80602010 && vertex.u == 0 && vertex.v == 0, "Source road color and alpha must reach the prelit shader unchanged with no texture coordinates");
        for (std::size_t axis = 0; axis < vertex.position.size(); ++axis) {
            expect(vertex.position[axis] == float(marks.quads[0].vertices[source_corners[corner]][axis]) / 65536.f,
                "Q16 source corners and original quad indices must remain0,2,1 /0,3,2");
        }
    }

    auto selection = marks;
    selection.quads.push_back(descriptor());
    selection.quads.back().sector = 8;
    selection.quads.back().visibility = 2;
    const std::array<std::uint16_t, 1> sectors{8};
    gpu::Frame hidden, selected, fallback;
    append_skid_marks(hidden, camera, selection, sectors);
    append_skid_marks(selected, camera, selection, sectors, 2);
    append_skid_marks(fallback, camera, selection);
    expect(hidden.draws.empty() && selected.draws.size() == 1 && selected.dynamic_world_vertices.size() == 6 && fallback.draws.size() == 1
               && fallback.dynamic_world_vertices.size() == 6,
        "441d10 must intersect stored view masks with47db00's selected sectors, allowing all sectors when no selection is active");
    selection.quads[0].sector = 0xffff;
    gpu::Frame invalid;
    append_skid_marks(invalid, camera, selection);
    expect(invalid.draws.empty(), "Native bounded sector handling must reject the original invalid-sector sentinel");
    auto bad_camera = camera;
    bad_camera.projection.near_plane = 0;
    gpu::Frame rejected;
    bool invalid_camera = false;
    try {
        append_skid_marks(rejected, bad_camera, marks);
    } catch (const std::runtime_error&) {
        invalid_camera = true;
    }
    expect(invalid_camera && rejected.draws.empty() && rejected.dynamic_world_vertices.empty() && rejected.world_views.empty(),
        "Skid submission must reject invalid cameras before appending source geometry");
}

void shader_culling(gpu::Renderer& renderer, const CameraInputs& camera) {
    const auto baseline = render(renderer, {});
    for (const bool on_near : {false, true}) {
        for (unsigned mask = 0; mask < 16; ++mask) {
            auto marks = single();
            for (std::size_t corner = 0; corner < 4; ++corner) {
                auto& point = marks.quads[0].vertices[corner];
                point[0] /= 8;
                point[1] /= 8;
                point[2] = mask & (1u << corner) ? (on_near ? one / 4 : one / 8) : one / 2;
            }
            gpu::Frame frame;
            append_skid_marks(frame, camera, marks);
            expect(frame.draws.size() == 1 && frame.draws[0].count == 12 && frame.dynamic_world_vertices.size() == 6,
                "Near-plane classification must remain in the shader while both source skid triangles are streamed");
            const auto pixels = render(renderer, frame);
            if (mask == 0 || mask == 15) {
                expect((pixels == baseline) == (mask == 15), "Fully visible and fully near-clipped skid quads must produce the original GPU coverage");
            }
        }
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto marks = single();
        if (mode == 0) {
            std::swap(marks.quads[0].vertices[1], marks.quads[0].vertices[3]);
        } else if (mode == 1) {
            marks.quads[0].vertices.fill(marks.quads[0].vertices[0]);
        } else {
            for (auto& point : marks.quads[0].vertices) {
                point[2] = std::int32_t(camera.projection.far_plane + 1) * one;
            }
        }
        gpu::Frame frame;
        append_skid_marks(frame, camera, marks);
        expect(frame.dynamic_world_vertices.size() == 6 && render(renderer, frame) == baseline,
            "The shared shader must reject reversed, degenerate and far-plane skid geometry without CPU culling");
    }
}

void blend_and_depth(gpu::Renderer& renderer, const CameraInputs& camera) {
    gpu::Frame frame;
    frame.quad({}, 0, 0, 640, 480, 0xff8090a0);
    append_skid_marks(frame, camera, single());
    const auto pixels = render(renderer, frame, "skid-alpha.png");
    const auto center = pixels[240 * 640 + 320];
    constexpr std::array expected{112, 88, 88};
    for (unsigned channel = 0; channel < 3; ++channel) {
        expect(std::abs(int((center >> ((2 - channel) * 8)) & 255) - expected[channel]) <= 1, "Original skid color must use source-alpha/inverse-source-alpha GPU blending");
    }
    frame.quad({.depth = true}, 250, 150, 140, 180, 0xff0000ff, .99f);
    expect(render(renderer, frame)[240 * 640 + 320] == center, "Skid marks must retain depth writes before later opaque world geometry");

    gpu::Frame occluded;
    occluded.quad({.depth = true}, 0, 0, 640, 480, 0xffff0000, .5f);
    const auto baseline = render(renderer, occluded);
    append_skid_marks(occluded, camera, single());
    expect(render(renderer, occluded) == baseline, "Original skid marks must test existing foreground depth");

    auto ordered = single(0x80ff0000);
    ordered.quads.push_back(descriptor(0x800000ff));
    ordered.active = 1;
    gpu::Frame overlapping;
    overlapping.quad({}, 0, 0, 640, 480, 0xff000000);
    append_skid_marks(overlapping, camera, ordered);
    expect(overlapping.draws.size() == 2 && overlapping.draws.back().dynamic_world && overlapping.draws.back().first == 0 && overlapping.draws.back().count == 24
               && overlapping.dynamic_world_vertices[0].color == 0x80ff0000 && overlapping.dynamic_world_vertices[6].color == 0x800000ff,
        "The shared streamed range must retain persistent source corners before current live corners");
    const auto layered = render(renderer, overlapping, "skid-persistent-before-active.png")[240 * 640 + 320];
    expect((layered & 255) > ((layered >> 16) & 255), "Current skid segments must blend after the physical persistent ring slots");
}

void generated_marks(gpu::Renderer& renderer) {
    // View the actual ground plane from above, without changing CPU quads.
    constexpr std::array<float, 16> overhead{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 10, 1};
    const CameraInputs camera{overhead, default_projection(3, 640, 480)};
    for (const auto weather : {1u, 3u, 4u}) {
        SkidMarks marks({{.track_id = 0}, weather, 0}, [](const auto&) {
            return std::uint16_t(7);
        });
        SurfaceEffectVehicle car;
        car.velocity = {one, 0, 0};
        for (std::size_t index = 0; index < car.wheels.size(); ++index) {
            auto& wheel = car.wheels[index];
            wheel.position = {index < 2 ? one : -one, 0, index % 2 == 0 ? -one : one};
            wheel.forward = {one, 0, 0};
            wheel.side = {0, 0, one};
            wheel.longitudinal_slip = one;
        }
        std::array cars{car};
        const auto image = [&](const std::string& suffix) {
            gpu::Frame frame;
            frame.quad({}, 0, 0, 640, 480, gpu::background);
            append_skid_marks(frame, camera, marks.frame());
            return render(renderer, frame, "skid-generated-" + std::to_string(weather) + suffix + ".png");
        };
        marks.update(cars);
        const auto baseline = image("-start");
        for (auto& wheel : cars[0].wheels) {
            wheel.position[0] += one;
        }
        marks.update(cars);
        expect(marks.frame().active == 4 && marks.frame().persistent == 0, "Actual four-wheel slip must generate four current source segments");
        const auto first = image("-active");
        expect(first != baseline, "Original slip-generated ground quads must produce real GPU pixels");
        for (auto& wheel : cars[0].wheels) {
            wheel.position[0] += one;
        }
        marks.update(cars);
        const auto extended = image("-extended");
        expect(extended != first, "Explicit wheel travel must visibly extend source-generated skid geometry");
        for (auto& wheel : cars[0].wheels) {
            wheel.longitudinal_slip = 0;
        }
        marks.update(cars);
        expect(marks.frame().persistent == 4 && marks.frame().active == 0, "Released slip must commit the four current segments into the original persistent ring");
        expect(image("-stored") == extended, "Committing the current quads must preserve their exact GPU footprint");
        for (int tick = 0; tick < 100; ++tick) {
            marks.update(cars);
        }
        const auto aged = image("-aged");
        expect(aged == (weather == 3 ? baseline : extended), "Only rain/storm road marks must fade; dry and snow road marks remain visible");
        marks.reset();
        expect(image("-reset") == baseline, "Original grid reset must clear both current and persistent visible skid geometry");
    }
}

void widescreen(SDL_Window* window, gpu::Renderer& renderer) {
    float ratio = 0;
    for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
        gpu::check(SDL_SetWindowSize(window, size[0], size[1]), "Resize skid target");
        gpu::check(SDL_SyncWindow(window), "Synchronize skid target size");
        SDL_PumpEvents();
        const CameraInputs camera{identity, default_projection(3, size[0], size[1])};
        gpu::Frame frame;
        append_skid_marks(frame, camera, single(0xffffffff));
        const auto pixels = render(renderer, frame, "skid-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        expect(pixels.size() == std::size_t(size[0] * size[1]), "Skid target must follow actual drawable dimensions");
        int left = size[0], top = size[1], right = -1, bottom = -1;
        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                if (pixels[std::size_t(y * size[0] + x)] != gpu::background) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        }
        expect(right > left && bottom > top, "Original skid quad must reach visible pixels at every supported aspect");
        const auto current = float(right - left + 1) / float(bottom - top + 1);
        if (ratio) {
            expect(std::abs(current - ratio) < .03f, "Shared widescreen projection must preserve skid geometry proportions");
        }
        ratio = current;
    }
}
} // namespace

int main() {
    try {
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private skid fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA skid marks", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private skid GPU window");
        gpu::Renderer renderer(window.get());
        const CameraInputs camera{identity, default_projection(3, 640, 480)};
        source_geometry(camera);
        shader_culling(renderer, camera);
        blend_and_depth(renderer, camera);
        generated_marks(renderer);
        widescreen(window.get(), renderer);
        std::cout << "PASS: original untextured skid geometry, alpha/depth, view/sector selection and widescreen GPU pixels\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
