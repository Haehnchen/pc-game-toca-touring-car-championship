#include "scene/contact_effects.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

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

std::vector<std::uint32_t> render(gpu::Renderer& renderer, gpu::Frame frame, const char* name = nullptr) {
    if (frame.draws.empty()) {
        // The renderer requires a draw even for the retired-effects baseline.
        frame.quad({}, 0, 0, 640, 480, gpu::background);
    }
    expect(renderer.draw(frame, false), "Contact effect fixture requires its private drawable");
    if (name) {
        renderer.save_screenshot(name);
    }
    const auto pixels = renderer.readback();
    if (!frame.dynamic_world_vertices.empty() || !frame.dynamic_world_lines.empty()) {
        auto reference = test::materialize_world(frame);
        if (reference.draws.empty()) {
            reference.quad({}, 0, 0, 640, 480, gpu::background);
        }
        expect(renderer.draw(reference, false), "CPU contact presentation reference target unavailable");
        expect(pixels == renderer.readback(), "Production impact fragment and spark pixels must exactly equal CPU clipping/projection");
    }
    return pixels;
}

std::size_t visible(const std::vector<std::uint32_t>& pixels) {
    return std::ranges::count_if(pixels, [](auto color) {
        return color != gpu::background;
    });
}
} // namespace

int main() {
    try {
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private collision effects check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA original collision effects", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private collision effects window");
        gpu::Renderer renderer(window.get());
        const CameraInputs camera{identity, default_projection(3, 640, 480)};
        PropFragmentTables tables;
        tables.positive.fill(one / 2);
        tables.signed_values.fill(one / 4);
        tables.color_indices.fill(0);
        tables.counts.fill(3);
        PropFragments fragments(tables, 1);
        ContactSparks sparks(tables);
        fragments.impact({{0, 0, 4 * one}, {}, one, {0, 0}});
        sparks.emit({{-one / 2, 0, 4 * one}, {one, one / 4, 0}, {}, one, false});
        SceneResources resources;
        SceneView view(renderer, resources);
        const auto frame = [&] {
            return view.frame(camera, {}, {identity}, {}, {}, {}, fragments.triangles(), nullptr, {}, sparks.lines());
        };
        const auto initial_frame = frame();
        const auto triangles = fragments.triangles();
        expect(!triangles.empty() && initial_frame.dynamic_world_vertices.size() == triangles.size() * 6 && initial_frame.draws.size() >= 2,
            "Shared fragments must retain both source windings as streamed prelit world corners before the spark pass");
        const auto& fragment_draw = initial_frame.draws.front();
        expect(fragment_draw.dynamic_world && !fragment_draw.mesh && fragment_draw.first == 0 && fragment_draw.count == triangles.size() * 12 && fragment_draw.state.depth
                   && fragment_draw.state.depth_write && !fragment_draw.state.blend && !fragment_draw.state.texture,
            "Source fragment windings must merge without changing opaque untextured depth material");
        for (std::size_t triangle = 0; triangle < triangles.size(); ++triangle) {
            for (std::size_t winding = 0; winding < 2; ++winding) {
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const auto source_corner = winding && corner < 2 ? 1 - corner : corner;
                    const auto& vertex = initial_frame.dynamic_world_vertices[triangle * 6 + winding * 3 + corner];
                    const auto& source = triangles[triangle].vertices[source_corner];
                    const std::array<float, 3> position{float(source[0]) / 65536, float(source[1]) / 65536, float(source[2]) / 65536};
                    expect(vertex.position == position && vertex.color == triangles[triangle].color && vertex.u == 0 && vertex.v == 0,
                        "Impact fragments lost source order, opposite winding, prelit palette or zero UVs");
                }
            }
        }
        const auto source_lines = sparks.lines();
        expect(initial_frame.dynamic_world_lines.size() == source_lines.size() * 2 && initial_frame.draws.size() == 2 && initial_frame.vertices.empty()
                   && initial_frame.indices.empty() && initial_frame.draws[1].dynamic_world && initial_frame.draws[1].state.lines && initial_frame.draws[1].first == 0
                   && initial_frame.draws[1].count == source_lines.size() * 2,
            "Ordinary scene frame must stream source spark lines after fragments with independent hardware-line counts");
        for (std::size_t index = 0; index < source_lines.size(); ++index) {
            for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
                const auto& source = endpoint ? source_lines[index].from : source_lines[index].to;
                const auto& vertex = initial_frame.dynamic_world_lines[index * 2 + endpoint];
                const std::array<float, 3> position{float(source[0]) / one, float(source[1]) / one, float(source[2]) / one};
                expect(vertex.position == position && vertex.color == (endpoint ? source_lines[index].color & 0x00ffffff : source_lines[index].color) && vertex.u == 0
                           && vertex.v == 0,
                    "Spark source stream must retain extended-before-origin Q16 endpoints, packed color, fade and zero UVs");
            }
        }
        const auto first = render(renderer, initial_frame, "contact-effects-emitted.png");
        expect(visible(first) > 10, "Original generated impact fragments and sparks did not reach actual GPU pixels");
        fragments.update();
        sparks.update();
        const auto moved = render(renderer, frame(), "contact-effects-moving.png");
        expect(moved != first, "Published source fragment/spark flight must move actual scene pixels");
        const auto cursor = tables.cursor;
        expect(render(renderer, frame()) == moved && tables.cursor == cursor, "Repeated presentation must not advance particle state or shared random tables");
        for (unsigned tick = 1; tick <= 25; ++tick) {
            fragments.update();
            sparks.update();
        }
        expect(visible(render(renderer, frame(), "contact-effects-expired.png")) == 0, "Source effect retirement must remove all collision pixels");

        ContactSparkLine line{{0, -one, 4 * one}, {0, one, 4 * one}};
        gpu::Frame lines;
        append_contact_sparks(lines, std::span(&line, 1), camera);
        expect(lines.vertices.empty() && lines.indices.empty() && lines.dynamic_world_vertices.empty() && lines.dynamic_world_lines.size() == 2 && lines.draws.size() == 1
                   && lines.draws[0].dynamic_world && !lines.draws[0].mesh && lines.draws[0].first == 0 && lines.draws[0].count == 2 && lines.draws[0].vertex_offset == 0
                   && lines.draws[0].state.lines && lines.draws[0].state.depth && lines.draws[0].state.depth_write && lines.draws[0].state.blend
                   && lines.dynamic_world_lines[0].position == std::array<float, 3>{0, 1, 4} && lines.dynamic_world_lines[1].position == std::array<float, 3>{0, -1, 4}
                   && lines.dynamic_world_lines[0].color == 0xfffff096 && lines.dynamic_world_lines[1].color == 0x00fff096,
            "446f20/47cb80 must stream hardware lines with ordinary depth writes and opaque extended/transparent origin endpoints");
        const auto streak = render(renderer, lines, "contact-spark-fade.png");
        expect(visible(streak) > 100 && visible(streak) < 400, "Sparks must remain thin hardware streaks with a visible alpha gradient");
        const auto energy = [&](unsigned row) {
            unsigned result = 0;
            for (unsigned column = 318; column <= 321; ++column) {
                result += (streak[row * 640 + column] >> 16) & 255;
            }
            return result;
        };
        expect(energy(170) > energy(300), "Source extended endpoint must remain brighter than the emission origin");
        auto depth_writes = lines;
        depth_writes.quad({.depth = true}, 280, 130, 80, 210, 0xff0000ff, .99f);
        const auto retained = render(renderer, depth_writes);
        expect(std::ranges::count_if(retained,
                   [](auto color) {
                       return color != gpu::background && color != 0xff0000ff;
                   })
                   > 100,
            "Spark hardware lines must retain depth writes against later farther geometry");
        gpu::Frame blocked;
        blocked.quad({.depth = true}, 0, 0, 640, 480, 0xff123456, .5f);
        const auto barrier = render(renderer, blocked);
        append_contact_sparks(blocked, std::span(&line, 1), camera);
        expect(render(renderer, blocked, "contact-spark-occluded.png") == barrier, "Collision sparks behind nearer car/scenery geometry must be depth rejected");
        line = {{0, 0, -one}, {one, 0, -one / 2}};
        gpu::Frame hidden;
        append_contact_sparks(hidden, std::span(&line, 1), camera);
        expect(hidden.dynamic_world_lines.size() == 2 && test::materialize_world(hidden).vertices.empty(),
            "Sparks behind near must retain source endpoints while GPU clipping rejects projected geometry");
        expect(visible(render(renderer, hidden)) == 0, "Fully clipped spark lines must leave the background untouched");
        line = {{1000, 0, one}, {-1000, 0, 0x1999}};
        gpu::Frame crossing;
        append_contact_sparks(crossing, std::span(&line, 1), camera);
        const auto clipped = test::materialize_world(crossing);
        expect(crossing.dynamic_world_lines.size() == 2 && clipped.vertices.size() == 2 && clipped.vertices.front().z == 0 && (clipped.vertices.front().diffuse >> 24) < 255,
            "A spark crossing near must retain a finite clipped endpoint and interpolated fade");
        expect(visible(render(renderer, crossing)) > 0, "A spark crossing near must retain visible clipped hardware-line pixels");
        for (const auto depth : {one / 4 - one / 64, one / 4, one / 4 + one / 64}) {
            line = {{0, -one / 32, depth}, {0, one / 32, depth}};
            gpu::Frame near_line;
            append_contact_sparks(near_line, std::span(&line, 1), camera);
            const auto reference = test::materialize_world(near_line);
            expect(near_line.dynamic_world_lines.size() == 2 && (depth < one / 4 ? reference.vertices.empty() : reference.vertices.size() == 2),
                "Spark line near clipping must reject below near and retain endpoints exactly on near");
            const auto pixels = render(renderer, near_line);
            expect(depth < one / 4 ? visible(pixels) == 0 : visible(pixels) > 20, "Spark lines below, on and above near must match original CPU visibility and depth");
        }
        for (const auto reversed : {false, true}) {
            const std::array<std::int32_t, 3> outside{-one / 32, one / 32, one / 8}, inside{one / 32, -one / 32, one / 2};
            line = {reversed ? inside : outside, reversed ? outside : inside};
            gpu::Frame near_crossing;
            append_contact_sparks(near_crossing, std::span(&line, 1), camera);
            expect(visible(render(renderer, near_crossing)) > 20, "Spark near crossing in either endpoint order must retain clipped pixels and fade");
        }
        std::cout << "PASS: ordinary scene collision particles, GPU movement/retirement, original line fade, depth and near clipping\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
