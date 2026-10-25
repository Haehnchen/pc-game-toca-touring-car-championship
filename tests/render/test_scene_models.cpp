#include "formats/static_model.hpp"
#include "scene/camera.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/lighting.hpp"
#include "scene/model_pose.hpp"
#include "scene/setup.hpp"
#include "scene_models/fixtures.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
using namespace toca::test::scene_models;

void identity(Bytes& bytes, std::size_t position) {
    for (std::size_t index = 0; index < 4; ++index) {
        put32(bytes, position + index * 20, 65536);
    }
}

Bytes static_fixture() {
    constexpr std::size_t mesh = 196, vertices = mesh + 48, faces = vertices + 3 * 32, end = faces + 24;
    Bytes bytes(end);
    std::memcpy(bytes.data(), "CM3D", 4);
    put32(bytes, 4, 100);
    put32(bytes, 8, 32);
    put16(bytes, 12, 2);
    put16(bytes, 14, 1);
    put32(bytes, 20, 32);
    put32(bytes, 24, mesh);
    put32(bytes, 28, end);
    put32(bytes, 32, 80);
    put16(bytes, 36, 0xffff);
    identity(bytes, 40);
    put32(bytes, 40 + 12 * 4, 1000 * 65536); // Must not become a parent transform.
    put32(bytes, 112, 84);
    identity(bytes, 120);
    put32(bytes, 120, 32768);
    put32(bytes, 120 + 12 * 4, 2 * 65536);
    put32(bytes, 120 + 13 * 4, 3 * 65536);
    put32(bytes, 120 + 14 * 4, 4 * 65536);
    bytes[112 + 76] = 1;
    put32(bytes, mesh + 12, end - mesh);
    put16(bytes, mesh + 16, 3);
    put16(bytes, mesh + 18, 1);
    const std::array<std::array<std::int32_t, 5>, 3> coordinates{{{-65535, 0, 0, 229376, -98304}, {0, 65536, 0, 98304, -32768}, {0, 0, 65536, -32768, 0}}};
    for (std::size_t index = 0; index < coordinates.size(); ++index) {
        for (std::size_t field = 0; field < 5; ++field) {
            put32(bytes, vertices + index * 32 + field * 4, std::bit_cast<std::uint32_t>(coordinates[index][field]));
        }
        put32(bytes, vertices + index * 32 + 20, std::bit_cast<std::uint32_t>(std::int32_t(-65535)));
        put32(bytes, vertices + index * 32 + 28, 65536);
    }
    put16(bytes, faces + 4, 1);
    put16(bytes, faces + 6, 2);
    put16(bytes, faces + 8, 0xffff);
    bytes[faces + 13] = 255;
    bytes[faces + 23] = 0x80;
    return bytes;
}

void synthetic() {
    using namespace toca::scene;
    toca::Model model;
    auto root = unit;
    root[12] = 10;
    root[14] = 5;
    auto child = unit;
    child[13] = 2;
    model.nodes = {{-1, root, 0, {}}, {0, child, 300, {0, 1, 2, 3}}};
    auto body = unit;
    body[0] = 0;
    body[2] = 1;
    body[8] = -1;
    body[10] = 0;
    body[12] = 100;
    const auto matrices = model_world_transforms(model, body);
    expect(matrices[1][12] == 95 && matrices[1][13] == 2 && matrices[1][14] == 10, "Model child must use row-vector local*parent*body hierarchy");
    HorizonModelFrame actors;
    for (const auto& node : model.nodes) {
        std::array<std::int32_t, 16> fixed;
        std::ranges::transform(node.transform, fixed.begin(), [](auto value) {
            return std::int32_t(value * 65536.f);
        });
        actors.transforms.push_back(fixed);
        actors.visible.push_back(1);
    }
    actors.transforms[0][12] = 40 * 65536;
    actors.transforms[1][13] = -3 * 65536;
    const auto moving = horizon_actor_world_transforms(model, actors);
    expect(moving[1][12] == 40 && moving[1][13] == -3 && moving[1][14] == 5, "Moving HOR matrices must replace authored locals and compose the rotor/body hierarchy");
    toca::Model wheels;
    wheels.nodes = {{-1, unit, 0, {}}, {0, child, 1, {}}, {0, child, 2, {}}, {0, child, 3, {}}, {0, child, 4, {}}, {1, unit, 20, {}}};
    const auto turned = car_world_transforms(wheels, {unit, 1024, {1024, 0, 1024, 0}}, 3);
    expect(car_world_transforms(wheels, {unit, 1024, {1024, 0, 1024, 0}}, 4) == turned, "Mega must retain Very High steering, wheel spin, pivots and child transforms");
    expect(turned[1][12] == 0 && turned[1][13] == 2 && turned[1][14] == 0 && turned[1][1] == 1 && turned[1][6] == 1,
        "Front steering/spin must compose about the local wheel axis and retain the model pivot");
    expect(turned[2][2] == -1 && turned[3][1] == 1 && turned[3][4] == -1 && turned[5] == turned[1], "Rear spin and child propagation must use original high-detail wheel bases");
    const auto medium = car_world_transforms(wheels, {unit, 1024, {1024, 0, 1024, 0}}, 1);
    expect(medium[1][2] == -1 && medium[2][2] == 1 && medium[2][8] == -1 && medium[3] == child,
        "Medium wheels must retain opposite front orientation without applying high-detail rear spin");
    // One caller-owned buffer crosses model families, sizes and failed inputs.
    std::vector<std::array<float, 16>> scratch;
    model_world_transforms_into(scratch, model, body);
    expect(scratch.size() == 2 && scratch[1][12] == 95 && scratch[1][13] == 2 && scratch[1][14] == 10, "Into hierarchy must retain exact row-vector composition");
    car_world_transforms_into(scratch, wheels, {unit, 1024, {1024, 0, 1024, 0}}, 3);
    expect(scratch == turned, "Growing into wheel output must retain all independently checked steering/spin matrices");
    const auto storage = scratch.data();
    const auto capacity = scratch.capacity();
    model_world_transforms_into(scratch, model, body);
    // The root matrix may itself be a prior output element.
    model_world_transforms_into(scratch, model, scratch.front());
    expect(scratch.size() == 2 && scratch[1][12] == 90 && scratch[1][13] == 2 && scratch[1][14] == 20, "Into composition must preserve a root matrix aliased from its output");
    horizon_actor_world_transforms_into(scratch, model, actors);
    expect(
        scratch.size() == 2 && scratch[1][12] == 40 && scratch[1][13] == -3 && scratch[1][14] == 5, "Into HOR output must replace earlier car matrices with source actor locals");
    actors.transforms[0][12] = 41 * 65536;
    horizon_actor_world_transforms_into(scratch, model, actors);
    expect(scratch[1][12] == 41, "An unchanged actor-frame address must not retain earlier transform contents");
    actors.transforms[0][12] = 40 * 65536;
    car_world_transforms_into(scratch, wheels, {unit, 1024, {1024, 0, 1024, 0}}, 1);
    expect(scratch == medium, "Into wheel output must replace high-detail animation with medium-detail source matrices");
    model_world_transforms_into(scratch, {}, unit);
    expect(scratch.empty(), "Empty model output must clear earlier wheel descriptors");
    horizon_actor_world_transforms_into(scratch, {}, {});
    expect(scratch.empty(), "Empty actor output must clear earlier model descriptors");
    car_world_transforms_into(scratch, {}, {unit}, 3);
    expect(scratch.empty() && scratch.capacity() == capacity, "Empty and smaller into outputs must retain warmed matrix storage");
    const auto rejects = [&](auto operation) {
        bool rejected = false;
        try {
            operation();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected, "Into preparation must preserve hierarchy/actor-size validation");
    };
    auto invalid = model;
    invalid.nodes[1].parent = 1;
    rejects([&] {
        model_world_transforms_into(scratch, invalid, body);
    });
    model_world_transforms_into(scratch, model, body);
    expect(scratch.size() == 2 && scratch[1][12] == 95 && scratch[1][13] == 2, "Valid preparation after a partial hierarchy error must replace all partial output");
    auto missing = actors;
    missing.visible.pop_back();
    rejects([&] {
        horizon_actor_world_transforms_into(scratch, model, missing);
    });
    horizon_actor_world_transforms_into(scratch, model, actors);
    expect(scratch.size() == 2 && scratch[1][12] == 40 && scratch[1][13] == -3, "Valid actor preparation must recover after mismatched presentation input");
    rejects([&] {
        car_world_transforms_into(scratch, invalid, {unit}, 3);
    });
    car_world_transforms_into(scratch, wheels, {unit, 1024, {1024, 0, 1024, 0}}, 3);
    expect(scratch == turned && scratch.data() == storage && scratch.capacity() == capacity, "Into matrix storage and wheel state must survive partial-error recovery");

    auto visual_model = model;
    visual_model.nodes.push_back({1, unit, 110, {}});
    visual_model.nodes.push_back({0, unit, 1, {}});
    CarPose visual_pose{unit};
    visual_pose.visual_angles = {1024, 0};
    const auto visual_high = car_world_transforms(visual_model, visual_pose, 3);
    expect(car_world_transforms(visual_model, visual_pose, 4) == visual_high, "Mega must retain Very High body tilt and damaged-panel transforms");
    const auto visual_medium = car_world_transforms(visual_model, visual_pose, 1);
    expect(visual_high[1][6] == 1 && visual_high[1][9] == -1 && visual_high[1][13] == 2 && visual_high[2] == visual_high[1],
        "High-detail node300 tilt must preserve its pivot and reach damaged-panel descendants");
    expect(visual_high[0] == root && visual_high[3] == root, "Cosmetic body feedback must not tilt the physical root or independent wheel pivots");
    expect(visual_medium == model_world_transforms(visual_model, unit), "Medium-detail source path must retain the authored body basis");
    expect(local_car_model_index(0) == 1 && local_car_model_index(1) == 1 && local_car_model_index(2) == 0 && local_car_model_index(3) == 0 && local_car_model_index(4) == 0,
        "Mega must share Very High's base model family while retaining the original lower-detail policy");
    bool invalid_detail = false;
    try {
        (void)local_car_model_index(5);
    } catch (const std::runtime_error&) {
        invalid_detail = true;
    }
    expect(invalid_detail, "Local-car LOD selection must reject detail above Mega");
    expect(rival_model_id(5, 2) == 10 && rival_model_id(4, 2) == 2 && rival_model_id(5, 8) == 8 && rival_model_id(3, 1) == 1,
        "Only the ordinary actor5 Vectra uses model10; physical and bonus car IDs must remain distinct");
    // Independent 44d7f9 offsets, including model10's physical car2 branch.
    constexpr std::array<std::array<int, 2>, 11> wheel_offsets{{{0x7ae, 0x7ae}, {0x28e, 0x10e4}, {0x170a, -0x106}, {0x7ae, 0x7ae}, {0x7ae, 0x7ae}, {0x7ae, 0x7ae}, {0x7ae, 0x7ae},
        {0xdd2, 0xdd2}, {0x7ae, 0x7ae}, {0x7ae, 0x7ae}, {0x170a, -0x106}}};
    for (std::uint32_t car_id = 0; car_id < wheel_offsets.size(); ++car_id) {
        for (std::uint32_t detail = 0; detail < 5; ++detail) {
            for (std::uint32_t lod = 0; lod < 3; ++lod) {
                const auto prepared = prepare_car_model(wheels, car_id, detail, lod);
                for (std::size_t node = 0; node < wheels.nodes.size(); ++node) {
                    auto expected = wheels.nodes[node].transform;
                    if (detail < 2 && lod == 1 && node >= 1 && node <= 4) {
                        const auto offset = wheel_offsets[car_id][node >= 3 ? 1 : 0];
                        expected[14] += float(node % 2 ? offset : -offset) / 65536.f;
                    }
                    expect(prepared.nodes[node].transform == expected, "Car preparation must apply signed source offsets only to medium wheel pivots at detail0/1");
                }
            }
        }
    }
    expect(wheels.nodes[1].transform == child && wheels.nodes[4].transform == child, "Preparing car instances must preserve the raw shared model");
    const Lighting light{{0, 0, 1}, 1, {10, 20, 30}};
    expect(model_vertex_color({0, 0, -1}, {255, 128, 64, 77}, light) == 0x4d0a0a08, "Unlit generic material must retain per-channel ambient and alpha");
    expect(model_vertex_color({0, 0, 1}, {255, 255, 255, 255}, light) == 0xffffffff, "Generic positive normal must saturate original diffuse light");
    auto camera_matrix = unit;
    camera_matrix[0] = 2;
    camera_matrix[5] = 3;
    camera_matrix[10] = 4;
    camera_matrix[12] = -20;
    camera_matrix[13] = -60;
    camera_matrix[14] = -120;
    const auto sky = horizon_world_transform({camera_matrix, default_projection(3, 640, 480)}, 0);
    expect(sky[12] == 10 && sky[13] == 18.5f && sky[14] == 30 && sky[0] == 1 && sky[5] == 1 && sky[10] == 1,
        "Horizon must follow full affine camera inverse without inheriting orientation");
    // Independent 431440 switch values: Knockhill uses 2.0, not Donington's
    // 1.5. Keep the fractional Q16 values instead of rounded decimal heights.
    constexpr std::array<std::int32_t, 10> sky_heights{0x18000, 0x14ccc, 0x14ccc, 0x14ccc, 0x1b333, 0x18000, 0x16666, 0x20000, 0x16666, 0x14ccc};
    for (std::uint32_t track = 0; track < sky_heights.size(); ++track) {
        const auto track_sky = horizon_world_transform({camera_matrix, default_projection(3, 640, 480)}, track);
        expect(track_sky[12] == 10 && track_sky[13] == 20.f - float(sky_heights[track]) / 65536.f && track_sky[14] == 30,
            "431440/431670 must retain each original track's camera-relative sky height");
    }
    // Actual Donington draw-boundary camera: its non-unit source basis loses
    //float32 translation bits when the published inverse is inverted again.
    CameraInputs source_camera{{0.3541601598262787f, -0.15705527365207672f, -0.9219112992286682f, 0, 1.9336925394242144e-09f, 0.9858007431030273f, -0.16793936491012573f, 0,
                                   0.9351902008056641f, 0.05947742983698845f, 0.34913134574890137f, 0, 168.60708618164062f, -18.948318481445312f, -52.245506286621094f, 1},
        default_projection(3, 640, 480)};
    const std::array source_position{-110.85408020019531f, 9.905120849609375f, -138.31088256835938f};
    const auto reconstructed_sky = horizon_world_transform(source_camera, 0);
    source_camera.published_world_position = source_position;
    const auto source_sky = horizon_world_transform(source_camera, 0);
    expect(source_sky[12] == source_position[0] && source_sky[13] == source_position[1] - 1.5f && source_sky[14] == source_position[2],
        "431670 must retain the published camera translation exactly before subtracting the track sky height");
    expect(reconstructed_sky != source_sky, "Measured non-unit source camera must expose the second affine inverse's float32 position loss");
    expect(source_sky[0] == 1 && source_sky[5] == 1 && source_sky[10] == 1, "Published sky position must retain an unrotated horizon basis");
    constexpr std::array<std::int32_t, 5> gains{52428, 47184, 41941, 36699, 26214};
    constexpr std::array<std::uint8_t, 5> ambients{152, 127, 127, 127, 101};
    for (std::uint32_t track = 0; track < tracks.size(); ++track) {
        const auto& fixture = tracks[track];
        std::array<float, 3> direction;
        for (std::size_t axis = 0; axis < direction.size(); ++axis) {
            direction[axis] = float(fixture.light_direction_q16[axis]) / 65536.f;
        }
        for (std::uint32_t choice = 0; choice <= 5; ++choice) {
            const auto setup = resolve_setup({.track_id = track, .weather_choice = choice}, 1);
            const auto weather = track == 9 ? 1 : choice == 0 ? fixture.default_weather : choice;
            const auto lighting = scene_lighting(setup);
            expect(setup.weather == weather && lighting.direction == direction && lighting.diffuse == float(gains[weather - 1]) / 65536.f
                       && lighting.ambient == std::array<std::uint8_t, 3>{ambients[weather - 1], ambients[weather - 1], ambients[weather - 1]},
                "Track default/weather choices must retain the source fixed direction and diffuse/ambient tables");
        }
    }
    auto bytes = static_fixture();
    put32(bytes, 32 + 72, 0x21000000); // Root chooses 00494a40's static branch.
    const auto parsed = toca::parse_static_model(bytes);
    const auto& baked = parsed.baked;
    expect(baked.chunks.size() == 1 && baked.chunks[0].mesh.vertices[0].position[0] > 998 && baked.chunks[0].mesh.vertices[0].position[1] == 0,
        "Static child must bake its root matrix rather than its child transform");
    expect(baked.chunks[0].mesh.vertices[0].u == .99f && baked.chunks[0].mesh.vertices[0].v == .02f && baked.chunks[0].face_flags[0] == 0,
        "Static raw UV clamp or separate terrain face-flag behavior changed");
    put32(bytes, 32 + 72, 0);
    expect(toca::parse_static_model(bytes).baked.chunks.empty(), "Dynamic hierarchy must not also appear in the static bake pass");

    // Reuse one raw mesh twice, with an absent variant between references.
    // The fixed normal mutates each time, but positions/UVs and the returned
    // generic model must continue to come from the original source fields.
    auto shared = static_fixture();
    shared.insert(shared.begin() + 196, 4, 0);
    put32(shared, 24, 200);
    put32(shared, 28, std::uint32_t(shared.size()));
    put32(shared, 112, 88);
    shared[112 + 76] = 3;
    put16(shared, 112 + 82, 0xffff);
    put32(shared, 32 + 72, 0x20000000);
    put32(shared, 40, 32768);
    const auto texture = shared.size();
    shared.resize(texture + 16 + 54);
    put16(shared, 16, 1);
    shared[texture] = 'T';
    put32(shared, texture + 12, 54);
    shared[texture + 16] = 'B';
    shared[texture + 17] = 'M';
    const auto repeated = toca::parse_static_model(shared);
    const auto& chunks = repeated.baked.chunks;
    expect(chunks.size() == 2 && chunks[0].node_index == 1 && chunks[0].variant_index == 0 && chunks[1].node_index == 1 && chunks[1].variant_index == 2
               && chunks[0].origin_xz == std::array<std::int32_t, 2>{1000 * 65536, 0} && chunks[1].origin_xz == chunks[0].origin_xz && repeated.baked.sector_centers.empty(),
        "Static mesh references must retain source variant order, absent slots and root placement without terrain sectors");
    expect(chunks[0].mesh.vertices[0].normal[0] == -.5f && chunks[1].mesh.vertices[0].normal[0] == -.25f
               && chunks[0].mesh.vertices[0].position == std::array<float, 3>{999.5f, 0, 0} && chunks[1].mesh.vertices[0].position == chunks[0].mesh.vertices[0].position
               && chunks[1].mesh.vertices[1].u == .5f && chunks[1].mesh.vertices[1].v == .5f && chunks[1].mesh.vertices[2].u == .02f && chunks[1].mesh.vertices[2].v == .99f
               && chunks[0].face_flags == Bytes{0} && chunks[1].face_flags == Bytes{0},
        "Static repeated references must accumulate fixed normals, reread original positions/UVs and ignore terrain lighting flags");
    const auto& original_vertex = repeated.model.meshes[0].vertices[0];
    expect(original_vertex.position[0] == -65535.f / 65536.f && original_vertex.normal[0] == original_vertex.position[0] && original_vertex.u == 2.5f && original_vertex.v == -.5f
               && repeated.model.nodes[0].transform[0] == .5f && repeated.model.nodes[1].transform[12] == 2
               && repeated.model.nodes[1].variants == std::vector<std::uint16_t>{0, 0xffff, 0},
        "Combined static decoding must retain the original hierarchy and unbaked mesh independently of accumulating bake state");
    expect(repeated.model.textures.size() == 1 && repeated.baked.textures.size() == 1 && repeated.model.textures[0].name == "T" && repeated.baked.textures[0].name == "T"
               && repeated.model.textures[0].bmp == Bytes(shared.begin() + std::ptrdiff_t(texture + 16), shared.end())
               && repeated.baked.textures[0].bmp == repeated.model.textures[0].bmp && repeated.model.textures[0].encoding == toca::ImageEncoding::bmp
               && repeated.baked.textures[0].encoding == toca::ImageEncoding::bmp,
        "Combined static decoding must retain original textures for both draw consumers");
    std::cout << "PASS: original local-car LOD, row-vector hierarchy, generic material lighting, weather and raw static bake\n";
}
} // namespace

int main() {
    try {
        synthetic();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
