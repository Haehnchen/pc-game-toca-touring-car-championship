#include "scene/car_light_glow.hpp"
#include "scene/car_lights.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void glow() {
    using namespace toca::scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr std::array<std::int32_t, 3> behind{-655360, 0, 0}, ahead{655360, 0, 0};
    // 434f00 rear emitters4dc408..4dc47c, including final -3333 offset.
    constexpr std::array<std::array<std::int32_t, 3>, 10> centers{{{-154795, 6422, 43253}, {-145489, 11927, 44761}, {-146603, 13172, 39387}, {-152174, 15204, 45481},
        {-150339, 16252, 42991}, {-151387, 4128, 43253}, {-151060, 5373, 48234}, {-146865, 6684, 42729}, {0, 0, 0}, {-189923, 27721, 64487}}};
    CarLights lights{true, true, true};
    for (std::uint32_t car = 0; car < 10; ++car) {
        const auto frame = car_light_glow_frame(car, 1, lights, identity, identity, behind);
        if (car == 8) {
            expect(frame.count == 0, "Original446d30 excludes Type8 lamp effects");
            continue;
        }
        expect(frame.count == 2 && !frame.front && frame.angle_fade_q16 == 126, "Source rear facing retains the quantized lookup rather than an ideal zero-degree fade");
        expect(frame.sprites[0].center == centers[car], "Every normal/Patriot rear glow must use its authored emitter center");
        auto opposite = centers[car];
        opposite[2] = -opposite[2];
        expect(frame.sprites[1].center == opposite, "Original rear glow mirrors its authored center across the vehicle");
        expect(frame.sprites[0].minimum == std::array<std::int32_t, 3>{-0x6666, -0x6666, 0} && frame.sprites[0].maximum == std::array<std::int32_t, 3>{0x6666, 0x6666, 0},
            "Rear halos retain the original0.8-unit billboard dimensions");
    }
    constexpr std::array<std::uint8_t, 5> brake_shades{59, 59, 99, 99, 99}, tail_shades{29, 29, 69, 69, 59}, head_shades{99, 99, 199, 199, 149};
    for (std::uint32_t weather = 1; weather <= 5; ++weather) {
        expect(car_light_glow_frame(5, weather, lights, identity, identity, behind).sprites[0].shade0 == brake_shades[weather - 1],
            "Brake glow must retain the weather-specific source intensity after angle attenuation");
        lights.brake = false;
        expect(car_light_glow_frame(5, weather, lights, identity, identity, behind).sprites[0].shade0 == tail_shades[weather - 1],
            "Tail glow must use its separate dim weather intensity");
        lights.brake = true;
        const auto front = car_light_glow_frame(5, weather, lights, identity, identity, ahead);
        expect(front.front && front.count == 2 && front.sprites[0].shade0 == head_shades[weather - 1], "Headlamp glow must select HGLOW and source weather intensities");
        expect(front.sprites[0].minimum == std::array<std::int32_t, 3>{-0x8000, -0x8000, 0}, "Headlamp halo retains its separate one-unit billboard size");
    }
    const auto side = car_light_glow_frame(5, 3, lights, identity, identity, {0, 0, 655360});
    expect(!side.front && side.angle_fade_q16 == 64800 && side.sprites[0].shade0 == 1, "Side-on lamps retain the source almost-complete fade and rear branch boundary");
    lights.broken[2] = true;
    const auto broken_rear = car_light_glow_frame(5, 3, lights, identity, identity, behind);
    expect(broken_rear.count == 1 && broken_rear.sprites[0].center[2] < 0, "Rear damage slot2 suppresses the positive-side glow");
    lights.broken[0] = true;
    const auto broken_front = car_light_glow_frame(5, 3, lights, identity, identity, ahead);
    expect(broken_front.count == 1 && broken_front.sprites[0].center[2] < 0, "Front damage slot0 suppresses the positive-side glow");
    lights.broken = {};
    constexpr std::array<float, 16> body{0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 3, 4, 5, 1};
    const auto moved = car_light_glow_frame(5, 3, lights, identity, body, behind);
    expect(!moved.front && moved.sprites[0].center == std::array<std::int32_t, 3>{3 * 65536 - 43253, 4 * 65536 + 4128, 5 * 65536 - 151387},
        "Body animation carries emitter position while physical root independently selects facing");
    const auto alternate = car_light_glow_frame(10, 3, lights, identity, identity, behind);
    expect(alternate.count == 2 && alternate.sprites[0].center == centers[2], "Alternate Vectra must reuse its physical model2 lamp emitters");
    lights.headlights = false;
    expect(car_light_glow_frame(5, 3, lights, identity, identity, ahead).count == 0, "Rear illumination must not emit a front halo");
    lights.tail = lights.brake = false;
    expect(car_light_glow_frame(5, 3, lights, identity, identity, behind).count == 0, "Released daytime brakes must clear their halo immediately");
    lights = {true, true, true};
    expect(car_light_glow_frame(5, 3, lights, identity, identity, {}).count == 0, "Source4367f0 excludes a coincident camera before normalization");
}

void ground_reflections() {
    using namespace toca::scene;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr std::array<float, 16> raised{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1};
    constexpr std::array<std::int32_t, 3> behind{-655360, 0, 0}, ahead{655360, 0, 0};
    CarLights lights{true, true, true};
    CarLightGroundInput ground;
    const auto rear = [&](std::uint32_t weather = 3) {
        return car_light_glow_frame(5, weather, lights, identity, raised, behind, &ground);
    };
    const auto front = [&](std::uint32_t weather = 3) {
        return car_light_glow_frame(5, weather, lights, identity, raised, ahead, &ground);
    };
    expect(car_light_glow_frame(5, 3, lights, identity, raised, behind).ground_count == 0, "Missing physical input cannot invent wet-ground reflections");
    for (std::uint32_t weather = 1; weather <= 5; ++weather) {
        const auto reflected = rear(weather);
        expect(reflected.count == 2 && reflected.ground_count == (weather < 3 ? 0u : 2u), "Only wet weather adds ground records, independently of the existing halos");
        if (weather >= 3) {
            expect(reflected.ground_sprites[0].shade0 == (weather == 5 ? 79 : 149), "Ground brake intensity is separate from the halo's100 and has the original snow80 branch");
            expect(front(weather).ground_sprites[0].shade0 == (weather == 5 ? 89 : 149), "Reflected headlamps use150 wet/90 snow before source angle attenuation");
        }
    }
    auto reflected = rear();
    expect(reflected.ground_sprites[0].center == std::array<std::int32_t, 3>{-151387, -122093, 43253}
               && reflected.ground_sprites[1].center == std::array<std::int32_t, 3>{-151387, -122093, -43253},
        "Rear ground records mirror the animated emitter around the wheel plane and add3333");
    expect(reflected.ground_sprites[0].minimum == std::array<std::int32_t, 3>{-0x8f56, -0x3331, 0}
               && reflected.ground_sprites[0].maximum == std::array<std::int32_t, 3>{0x8f56, 0x3331, 0},
        "Rear reflections retain their separate camera-facing source rectangle");
    const auto reflected_front = front();
    expect(reflected_front.ground_sprites[0].center == std::array<std::int32_t, 3>{146997, -127140, 32964},
        "Front ground center mirrors its transformed lamp Y without the rear offset");
    expect(reflected_front.ground_sprites[0].minimum == std::array<std::int32_t, 3>{-0xb328, -0x3ffc, 0}
               && reflected_front.ground_sprites[0].maximum == std::array<std::int32_t, 3>{0xb328, 0x3ffc, 0},
        "Front reflections preserve their separate source extents");
    // Wheel2 touches its plane; wheel3 is half a unit above local lamp Y.
    // Unequal source planes must move and attenuate each side independently.
    ground.ground_height[3] = 32768;
    ground.wheel_position[3][1] = 32768 + 4128 + 32768;
    reflected = rear();
    expect(reflected.ground_sprites[0].center[1] == -122093 && reflected.ground_sprites[1].center[1] == -56557 && reflected.ground_sprites[0].shade0 == 149
               && reflected.ground_sprites[1].shade0 == 74,
        "Each rear wheel supplies its own plane and distance; attenuation follows quantized149 rather than unrounded150");
    ground.wheel_position[3][1] = ground.ground_height[3] + 4128 + 65536;
    reflected = rear();
    expect(reflected.ground_count == 2 && reflected.ground_sprites[1].shade0 == 0 && reflected.sprites[1].shade0 == 99,
        "Flight by one unit above local lamp height fully fades the ground record while retaining its halo");
    ground.wheel_position[2][1] = 1;
    expect(rear().ground_sprites[0].shade0 == 158, "Original ground attenuation has no lower clamp when a positive wheel distance remains below local lamp height");
    ground = {};
    const auto low_rear = car_light_glow_frame(5, 3, lights, identity, identity, behind, &ground);
    expect(low_rear.ground_sprites[0].center[1] == -0x4ccc, "Rear reflected offset clamps below the wheel plane at the original4ccc boundary");
    ground.surface[3] = 1;
    reflected = rear();
    expect(reflected.ground_count == 1 && reflected.ground_sprites[0].center[2] > 0 && reflected.count == 2,
        "436790 selects road surface0 per wheel without suppressing the separate lamp halo");
    ground.surface[3] = 0;
    ground.surface[0] = 2;
    expect(front().ground_count == 1 && front().ground_sprites[0].center[2] < 0, "Front reflection eligibility reads front wheel surfaces independently of the rear wheels");
    ground = {};
    lights.broken[2] = true;
    expect(rear().ground_count == 1 && rear().ground_sprites[0].center[2] < 0, "Broken rear slots suppress the matching ground record as well as its halo");
    lights.broken[0] = true;
    expect(front().ground_count == 1 && front().ground_sprites[0].center[2] < 0, "Broken front slots suppress the matching reflected headlamp");
    lights.broken = {};
    lights.brake = false;
    expect(rear().ground_sprites[0].shade0 == 69 && rear(5).ground_sprites[0].shade0 == 59, "Tail ground records retain70 wet/60 snow instead of brake intensities");
    lights.brake = true;
    ground.distant_body = true;
    ground.body_height = 32768;
    ground.ground_height.fill(100 * 65536);
    for (auto& wheel : ground.wheel_position) {
        wheel[1] = 200 * 65536;
    }
    expect(rear().ground_sprites[0].center[1] == 74515 && rear().ground_sprites[0].shade0 == 149 && front().ground_sprites[0].center[1] == 69468,
        "Distant body mode uses authored lamp Y plus body half-height, independently of physical plane and flight distance");
    ground = {};
    constexpr std::array<float, 16> tilted{0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1};
    const auto tilted_frame = car_light_glow_frame(5, 3, lights, identity, tilted, behind, &ground);
    expect(tilted_frame.ground_sprites[0].center == std::array<std::int32_t, 3>{3 * 65536 - 4128, -97650, 5 * 65536 + 43253},
        "Ground displacement starts at the animated tilted emitter while physical root still selects its visible end");
    for (std::uint32_t car = 0; car <= 10; ++car) {
        const auto frame = car_light_glow_frame(car, 3, lights, identity, raised, behind, &ground);
        expect(frame.ground_count == (car == 8 ? 0u : 2u), "All source lamp families and alternate Vectra reflect except Type8");
    }
    const auto vectra = car_light_glow_frame(2, 3, lights, identity, raised, behind, &ground);
    const auto alternate = car_light_glow_frame(10, 3, lights, identity, raised, behind, &ground);
    expect(vectra.ground_sprites[0].center == alternate.ground_sprites[0].center, "Alternate Vectra reflections reuse model2 source emitters");
    lights.headlights = lights.tail = lights.brake = false;
    expect(rear().ground_count == 0 && front().ground_count == 0, "Unlit lamps cannot leave reflected images behind");
}

void mega_lenses() {
    using namespace toca;
    using namespace toca::scene;
    Model model;
    for (const bool front : {false, true}) {
        ModelMesh mesh;
        for (const auto yz : {std::array{-2.f, -2.f}, std::array{2.f, -2.f}, std::array{-2.f, 2.f}, std::array{2.f, 2.f}}) {
            mesh.vertices.push_back({{front ? 3.f : -3.f, yz[0], yz[1]}, {front ? 1.f : -1.f, 0, 0}, (yz[1] + 2) / 4, (yz[0] + 2) / 4});
        }
        mesh.faces = {{{0, 1, 2}, 7, {}}, {{1, 3, 2}, 7, {}}, {{0, 1, 2}, -1, {}}};
        ModelNode node{};
        node.id_flags = front ? 24 : 10;
        node.variants = {std::uint16_t(model.meshes.size())};
        model.nodes.push_back(node);
        model.meshes.push_back(mesh);
    }
    const auto lenses = mega_car_lamp_lenses(model, 5);
    expect(!lenses.empty(), "Mega lenses must clip actual textured model glass");
    std::array<double, 2> area{};
    std::array<bool, 4> sides{};
    const auto triangle_area = [](const auto& vertices) {
        const auto& a = vertices[0];
        const auto& b = vertices[1];
        const auto& c = vertices[2];
        return std::abs(double(b[1] - a[1]) * (c[2] - a[2]) - double(b[2] - a[2]) * (c[1] - a[1])) * .5;
    };
    for (const auto& triangle : lenses) {
        expect(triangle.texture == 7 && triangle.node == std::size_t(triangle.front) && triangle.node_id_flags == (triangle.front ? 24u : 10u),
            "Mega emission must retain the original glass node/texture and exclude untextured housing");
        const auto negative = triangle.vertices[0].position[2] < 0;
        expect(triangle.broken_index == (triangle.front ? negative ? 1 : 0 : negative ? 3 : 2), "Mega glass must retain the matching original damage side");
        sides[triangle.broken_index] = true;
        std::array<std::array<float, 3>, 3> positions;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& vertex = triangle.vertices[index];
            positions[index] = vertex.position;
            expect(std::abs(vertex.position[0] - (triangle.front ? 3.00025f : -3.00025f)) < 1e-6f, "Mega emission must follow the rebuilt glass depth and normal bias");
            expect(std::abs(vertex.u - (vertex.position[2] + 2) / 4) < 1e-6f && std::abs(vertex.v - (vertex.position[1] + 2) / 4) < 1e-6f,
                "Clipping must interpolate the real atlas UVs at every new lens edge");
        }
        area[std::size_t(triangle.front)] += triangle_area(positions);
    }
    for (const bool front : {false, true}) {
        const auto contour = car_lamp_frame(5, 3, {true, true, true}, front);
        double expected = 0;
        for (std::size_t triangle = 0; triangle < contour.count; ++triangle) {
            expected += triangle_area(contour.triangles[triangle].positions);
        }
        expect(std::abs(area[std::size_t(front)] - expected) < 1e-6, "Mega glass clipping must cover exactly the authored lamp silhouette without enlarging its footprint");
    }
    expect(sides == std::array{true, true, true, true}, "Mega lens extraction must retain all four independently breakable lights");
}

void patriot_lenses() {
    using namespace toca;
    using namespace toca::scene;
    Model model;
    model.nodes.push_back({.parent = -1, .transform = {}, .id_flags = 30, .variants = {0}});
    model.meshes.emplace_back();
    auto& mesh = model.meshes.front();
    const auto end_panel = [&](float x, bool front, std::int16_t texture, bool outward) {
        const auto first = std::uint16_t(mesh.vertices.size());
        for (const auto yz : {std::array{-2.f, -2.f}, std::array{2.f, -2.f}, std::array{-2.f, 2.f}, std::array{2.f, 2.f}}) {
            mesh.vertices.push_back({{x, yz[0], yz[1]}, {front ? 1.f : -1.f, 0, 0}, (yz[1] + 2) / 4, (yz[0] + 2) / 4});
        }
        for (auto indices : {std::array<std::uint16_t, 3>{first, std::uint16_t(first + 1), std::uint16_t(first + 2)},
                 std::array<std::uint16_t, 3>{std::uint16_t(first + 1), std::uint16_t(first + 3), std::uint16_t(first + 2)}}) {
            if (front != outward) {
                std::swap(indices[1], indices[2]);
            }
            mesh.faces.push_back({indices, texture, {}});
        }
    };
    end_panel(2.86f, true, 7, true);
    end_panel(-2.65f, false, 7, true);
    const auto original = mega_car_lamp_lenses(model, 9);
    std::array<bool, 4> sides{};
    for (const auto& triangle : original) {
        expect(triangle.node == 0 && triangle.node_id_flags == 30, "Patriot emission follows the actual body30 node");
        const bool negative = triangle.vertices[0].position[2] < 0;
        expect(triangle.broken_index == (triangle.front ? negative ? 1 : 0 : negative ? 2 : 3), "Patriot retains its reversed authored rear lamp sides");
        sides[triangle.broken_index] = true;
    }
    expect(sides == std::array{true, true, true, true}, "Patriot's embedded glass must emit on all four sides");
    // Identical projected footprints on interior or inward-facing body panels
    // must not acquire the lamp emission from the embedded body30 geometry.
    end_panel(2.f, true, 3, true);
    end_panel(-2.f, false, 3, true);
    end_panel(2.86f, true, 3, false);
    end_panel(-2.65f, false, 3, false);
    const auto filtered = mega_car_lamp_lenses(model, 9);
    expect(filtered.size() == original.size(), "Patriot lamp extraction excludes body faces outside the authored X slab and inward-facing faces");
    for (const auto& triangle : filtered) {
        expect(triangle.texture == 7, "Only the actual outward end glass may emit, excluding interior paint/plate panels");
    }
}
} // namespace

int main() {
    using namespace toca::scene;
    try {
        glow();
        ground_reflections();
        mega_lenses();
        patriot_lenses();
        CarLights lights{true, true, false};
        const auto front = car_lamp_frame(0, 3, lights, true);
        expect(front.count == 4 && front.triangles[0].color == 0xffffffe6, "Headlights require two yellow-white triangles per side");
        //434f00: first source coordinate229ba/fa5f/6041, X offset28f.
        expect(front.triangles[0].positions[0] == std::array<float, 3>{142409.f / 65536, -1441.f / 65536, 24641.f / 65536}, "Headlights must retain the source car-body positions");
        for (std::size_t v = 0; v < 3; ++v) {
            auto reflected = front.triangles[0].positions[v];
            reflected[2] = -reflected[2];
            expect(reflected == front.triangles[2].positions[v == 0 ? 0 : 3 - v], "499580 mirrors the second lamp and reverses its winding");
        }
        const auto tail = car_lamp_frame(0, 3, lights, false);
        expect(tail.count == 8 && tail.triangles[0].color == 0xffc80000, "High-detail car0 tail lamps include both source sections");
        const auto medium = car_lamp_frame(0, 1, lights, false);
        expect(medium.count == 4 && medium.triangles[0].positions[0] == std::array<float, 3>{-147062.f / 65536, 9436.f / 65536, -37879.f / 65536},
            "Medium tail lamps require their adjusted body coordinates");
        lights.brake = true;
        expect(car_lamp_frame(0, 3, lights, false).triangles[0].color == 0xffff0000, "Brake lamps replace the dim tail color");
        lights.broken[3] = true;
        const auto broken = car_lamp_frame(0, 3, lights, false);
        expect(broken.count == 4 && broken.triangles[0].positions[0][2] > 0, "Rear damage slot3 suppresses side0 only");
        lights.broken[1] = true;
        expect(car_lamp_frame(0, 3, lights, true).count == 2, "Front damage slot1 suppresses the mirrored headlight");
        lights.broken = {};
        const auto vectra = car_lamp_frame(2, 3, lights, true);
        const auto alternate = car_lamp_frame(10, 3, lights, true);
        expect(vectra.count == 6 && alternate.triangles[0].positions == vectra.triangles[0].positions, "Alternate Vectra retains its three-triangle headlamp geometry");
        expect(car_lamp_frame(0, 4, lights, false).count == 8, "Mega uses the high-detail lamp polygons");
        expect(car_lamp_frame(9, 3, lights, false).count == 8, "Patriot retains its four-triangle rear lamp fan");
        std::cout << "PASS: source lamp geometry, halos, wet reflections, wheel planes, distance, weather, view angle, damage, transforms and bonus vehicles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
