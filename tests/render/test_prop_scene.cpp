#include "assets/store.hpp"
#include "core/random.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "simulation/fixed.hpp"
#include "simulation/prop_contacts.hpp"
#include "world_geometry.hpp"

#include <iostream>

namespace {
using namespace toca;
using namespace toca::scene;
using namespace toca::simulation;
using namespace toca::simulation::fixed;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool descendant(const Model& model, std::uint32_t node, std::uint32_t root) {
    auto ancestor = std::int32_t(node);
    while (ancestor >= 0 && std::uint32_t(ancestor) != root) {
        ancestor = model.nodes[std::size_t(ancestor)].parent;
    }
    return ancestor >= 0;
}

HorizonModelFrame& model_frame(PropFrame& frame, PropKind kind) {
    return kind == PropKind::cones ? frame.cones : kind == PropKind::posts ? frame.posts : frame.signs;
}

CameraInputs camera(const Model& model, const HorizonModelFrame& frame, std::uint32_t root) {
    const auto matrices = horizon_actor_world_transforms(model, frame);
    for (std::uint32_t index = root; index < model.nodes.size(); ++index) {
        const auto& node = model.nodes[index];
        if (!descendant(model, index, root) || node.variants.empty() || node.variants[0] == 0xffff) {
            continue;
        }
        const auto& mesh = model.meshes.at(node.variants[0]);
        for (const auto& face : mesh.faces) {
            std::array<std::array<float, 3>, 3> points;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& p = mesh.vertices.at(face.indices[corner]).position;
                const auto& matrix = matrices[index];
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    points[corner][axis] = p[0] * matrix[axis] + p[1] * matrix[4 + axis] + p[2] * matrix[8 + axis] + matrix[12 + axis];
                }
            }
            std::array<float, 3> center{}, normal{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                center[axis] = (points[0][axis] + points[1][axis] + points[2][axis]) / 3;
                const auto j = (axis + 1) % 3, k = (axis + 2) % 3;
                normal[axis] = (points[1][j] - points[0][j]) * (points[2][k] - points[0][k]) - (points[1][k] - points[0][k]) * (points[2][j] - points[0][j]);
            }
            const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (length == 0) {
                continue;
            }
            for (auto& component : normal) {
                component /= length;
            }
            if (std::abs(normal[0]) + std::abs(normal[2]) < .1f) {
                normal[0] += .25f;
            }
            std::array<SceneCamera, 1> authored{};
            Vector target;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                target[axis] = std::int32_t(center[axis] * one);
                authored[0].transform[12 + axis] = std::int32_t((center[axis] + normal[axis] * 4) * one);
            }
            return {FinishCamera(authored).world_to_view(target), default_projection(3, 640, 480)};
        }
    }
    throw std::runtime_error("Original prop probe has no authored drawable triangle");
}

void probe(gpu::Renderer& renderer, const SceneResources& source, PropKind kind) {
    const auto& original = kind == PropKind::cones ? source.cones : kind == PropKind::posts ? source.posts : source.signs;
    expect(!original.actors.empty(), "Canonical Donington prop family has no registered actor");
    auto selected = original;
    selected.actors.resize(1);
    const auto root = selected.actors.front().root_node;
    const PropResources empty;
    const HpcSurface surface(source.collision_hpc);
    PropContacts contacts(
        kind == PropKind::cones ? selected : empty, kind == PropKind::posts ? selected : empty, kind == PropKind::signs ? selected : empty, source.ground.sector_centers, surface);
    for (unsigned tick = 0; tick < 2; ++tick) {
        contacts.advance();
    }
    SceneResources isolated{};
    isolated.setup = source.setup;
    (kind == PropKind::cones ? isolated.cones : kind == PropKind::posts ? isolated.posts : isolated.signs) = selected;
    SceneView view(renderer, isolated);
    auto initial = contacts.frame();
    const auto inputs = camera(selected.model, model_frame(initial, kind), root);
    const auto visible_frame = [&] {
        auto result = contacts.frame();
        auto& model = model_frame(result, kind);
        for (std::uint32_t index = 0; index < model.visible.size(); ++index) {
            if (!descendant(selected.model, index, root)) {
                model.visible[index] = 0;
            }
        }
        return result;
    };
    const auto name = kind == PropKind::cones ? "cone" : kind == PropKind::posts ? "post" : "sign";
    const auto render = [&](const char* phase, std::span<const PropFragmentTriangle> fragments = {}) {
        const auto frame = view.frame(inputs, scene_lighting(source.setup), {unit}, {}, {}, visible_frame(), fragments);
        expect(!test::materialize_world(frame).vertices.empty() && renderer.draw(frame, false), "Original prop geometry did not reach the GPU");
        renderer.save_screenshot(std::string("prop-") + name + "-" + phase + ".png");
        auto result = renderer.readback();
        if (!fragments.empty()) {
            expect(frame.dynamic_world_vertices.size() == fragments.size() * 6
                       && std::ranges::any_of(frame.draws,
                           [&](const auto& draw) {
                               return draw.dynamic_world && !draw.mesh && draw.count == fragments.size() * 12 && draw.state.depth && draw.state.depth_write && !draw.state.blend
                                      && !draw.state.texture;
                           }),
                "Broken sign shards must retain both source windings in their shared prelit depth pass");
            expect(renderer.draw(test::materialize_world(frame), false), "CPU broken-sign fragment reference target unavailable");
            expect(result == renderer.readback(), "Production sign fragment pixels must exactly equal CPU clipping/projection");
        }
        expect(std::ranges::count_if(result,
                   [](auto value) {
                       return value != gpu::background;
                   })
                   > 16,
            "Original prop probe produced no visible pixels");
        return result;
    };
    const auto before = render("initial");
    const auto& transform = model_frame(initial, kind).transforms.at(root);
    PropVehicle car;
    car.body.basis = {Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};
    car.body.position = {transform[12] - one, transform[13], transform[14]};
    car.body.position[1] += kind == PropKind::posts ? 0x8666 : kind == PropKind::signs ? one : 0;
    car.velocity_q16 = {kind == PropKind::signs ? 0x4000 : 0x8000, 0, 0};
    expect(contacts.contact(car).contacts == 1, "Diagnostic vehicle must hit the actual registered prop through original contact logic");
    for (unsigned tick = 0; tick < 3; ++tick) {
        contacts.advance();
    }
    expect(render("contact") != before, "Actual cone tumble/post bend/sign tip must change real GPU pixels");
    if (kind == PropKind::signs) {
        contacts.reset();
        car.velocity_q16 = {one, 0, 0};
        expect(contacts.contact(car).contacts == 1 && contacts.frame().breaks.size() == 1, "Fast sign contact must issue the original break request");
        expect(test::materialize_world(view.frame(inputs, scene_lighting(source.setup), {unit}, {}, {}, visible_frame())).vertices.empty(),
            "Broken sign hierarchy must disappear from the GPU model pass");
        SourceRandom random(7);
        auto tables = make_prop_fragment_tables(random);
        PropFragments fragments(tables, source.setup.weather);
        const auto& event = contacts.frame().breaks.front();
        expect(fragments.break_sign(event.root_transform, event.vehicle_velocity_q16).has_value(), "Original sign break must create shards and event21");
        const auto first = render("fragments", fragments.triangles());
        fragments.update();
        expect(render("fragments-moving", fragments.triangles()) != first, "Original sign fragment motion must change real GPU pixels");
    }
    std::cout << "PASS: authored " << name << " contact/state transforms reach real GPU pixels\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "Expected canonical data root");
        const AssetStore store(argv[1]);
        const auto resources = load_scene_resources(store, resolve_setup({.track_id = 0, .weather_choice = 1}));
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private prop GPU fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA prop contacts", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private prop GPU window");
        gpu::Renderer renderer(window.get());
        for (const auto kind : {PropKind::cones, PropKind::posts, PropKind::signs}) {
            probe(renderer, resources, kind);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
