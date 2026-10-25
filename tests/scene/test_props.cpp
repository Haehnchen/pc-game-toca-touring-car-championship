#include "assets/store.hpp"
#include "scene/props.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace toca::scene;
constexpr std::int32_t unit = 65536;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value);
    bytes.at(offset + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, std::uint16_t(value));
    put16(bytes, offset + 2, std::uint16_t(value >> 16));
}

struct Node {
    std::int16_t parent;
    std::uint32_t id;
    std::vector<std::uint16_t> variants;
    std::int32_t x = 0;
};

Bytes model(const std::vector<Node>& nodes, std::uint16_t mesh_count = 4) {
    Bytes bytes(32);
    std::memcpy(bytes.data(), "CM3D", 4);
    put32(bytes, 4, 100);
    put32(bytes, 8, 32);
    put16(bytes, 12, std::uint16_t(nodes.size()));
    put16(bytes, 14, mesh_count);
    put32(bytes, 20, 32);
    for (const auto& node : nodes) {
        const auto offset = bytes.size();
        const auto size = (80 + node.variants.size() * 2 + 3) & ~std::size_t(3);
        bytes.resize(offset + size);
        put32(bytes, offset, std::uint32_t(size));
        put16(bytes, offset + 4, std::bit_cast<std::uint16_t>(node.parent));
        for (std::size_t axis = 0; axis < 4; ++axis) {
            put32(bytes, offset + 8 + axis * 20, unit);
        }
        put32(bytes, offset + 56, std::bit_cast<std::uint32_t>(node.x));
        put32(bytes, offset + 72, node.id);
        bytes[offset + 76] = std::uint8_t(node.variants.size());
        for (std::size_t variant = 0; variant < node.variants.size(); ++variant) {
            put16(bytes, offset + 80 + variant * 2, node.variants[variant]);
        }
    }
    put32(bytes, 24, std::uint32_t(bytes.size()));
    for (std::uint16_t mesh = 0; mesh < mesh_count; ++mesh) {
        const auto offset = bytes.size();
        bytes.resize(offset + 168);
        bytes[offset] = std::uint8_t('A' + mesh);
        put32(bytes, offset + 12, 168);
        put16(bytes, offset + 16, 3);
        put16(bytes, offset + 18, 1);
        put32(bytes, offset + 48 + 32, unit);
        put32(bytes, offset + 48 + 64 + 4, unit);
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put32(bytes, offset + 48 + vertex * 32 + 16, std::bit_cast<std::uint32_t>(-unit));
            put32(bytes, offset + 48 + vertex * 32 + 28, unit);
        }
        const auto face = offset + 48 + 96;
        put16(bytes, face + 4, 1);
        put16(bytes, face + 6, 2);
        put16(bytes, face + 8, 0xffff);
        for (std::size_t channel = 0; channel < 4; ++channel) {
            bytes[face + 10 + channel] = 255;
        }
    }
    put32(bytes, 28, std::uint32_t(bytes.size()));
    return bytes;
}

void synthetic() {
    const auto flags = decode_props(model({{-1, 0x4000042, {}, 10 * unit}, {0, 1, {1, 2, 3}}, {1, 5, {0, 0, 0}}}), PropKind::flags);
    expect(flags.model.nodes.size() == 3 && flags.model.nodes[0].transform[12] == 10.f && flags.model.nodes[2].parent == 1 && flags.actors.size() == 1
               && flags.actors[0].root_node == 0 && flags.actors[0].animation_node == 1 && flags.model.nodes[2].variants == std::vector<std::uint16_t>{0, 0, 0},
        "Props must own original placements, hierarchy and every variant while selecting only the source ID1 flag node");
    const auto sibling = decode_props(model({{-1, 8, {}}, {0, 2, {0, 1, 2}}, {-1, 9, {}}, {2, 1, {1, 2, 3}}}), PropKind::flags);
    expect(sibling.actors.size() == 2 && sibling.actors[0].animation_node == 3 && sibling.actors[1].animation_node == 3,
        "480ce0 flag lookup must descend children and then following sibling roots in original order");
    const auto missing = decode_props(model({{-1, 8, {}}, {0, 2, {0, 1, 2}}}), PropKind::flags);
    expect(missing.actors.size() == 1 && missing.actors[0].animation_node == UINT32_MAX, "An original flag with children but no ID1 must retain its missing animation target");
    const auto stopped = decode_props(model({{-1, 8, {}}, {-1, 9, {}}, {1, 1, {1, 2, 3}}}), PropKind::flags);
    expect(stopped.model.nodes.size() == 3 && stopped.actors.empty(), "44d72f must stop registration on the first childless root without discarding loaded scene geometry");
    const auto overwritten = decode_props(model({{-1, 0x4000042, {}, 10 * unit}, {1, 2, {0, 1, 2}}, {1, 0, {3, 3, 3}}}), PropKind::flags);
    expect(overwritten.model.nodes.size() == 1 && overwritten.model.nodes[0].parent == -1 && overwritten.model.nodes[0].transform[12] == 0
               && (overwritten.model.nodes[0].id_flags & 0xffffff) == 0 && overwritten.model.nodes[0].variants == std::vector<std::uint16_t>{3, 3, 3} && overwritten.actors.empty(),
        "494c6d unresolved parents must overwrite the previous actor's transform/ID/geometry instead of inventing repaired child placements");
    std::vector<Node> many;
    for (int index = 0; index < 53; ++index) {
        many.push_back({-1, std::uint32_t(index == 0 ? 999 : index == 1 ? 998 : index), {0}});
    }
    const auto cones = decode_props(model(many), PropKind::cones), signs = decode_props(model(many), PropKind::signs);
    expect(cones.model.nodes.size() == 53 && cones.actors.size() == 30 && cones.actors.front().root_node == 2 && cones.actors.back().root_node == 31 && signs.actors.size() == 50
               && signs.actors.back().root_node == 51,
        "Original registration must exclude IDs998/999 and cap controllers30/50 without dropping loaded geometry");
    const auto baked = decode_props(model({{-1, 0x20000000, {}, 3 * unit}, {0, 1, {0}, 100 * unit}}), PropKind::cones);
    expect(baked.model.nodes.empty() && baked.actors.empty() && baked.baked.chunks.size() == 1 && baked.baked.chunks[0].mesh.vertices[0].position[0] == 3.f,
        "Static prop roots must use the original baked root transform rather than a dynamic child hierarchy");
    const auto static_forward = decode_props(model({{-1, 0x20000000, {}, 3 * unit}, {1, 1, {0}, 100 * unit}}), PropKind::cones);
    expect(static_forward.model.nodes.empty() && static_forward.actors.empty() && static_forward.baked.chunks.size() == 1
               && static_forward.baked.chunks[0].mesh.vertices[0].position[0] == 3.f,
        "Unresolved static child parents must preserve the source inherited baked branch and last root matrix");
    auto empty = model({}, 0);
    empty.resize(36);
    put32(empty, 32, 0x01130000);
    expect(decode_props(empty, PropKind::flags).model.nodes.empty(), "Counted-empty exporter FLAGS must ignore its unused four-byte suffix");
    auto broken = model({{-1, 0, {}}, {0, 1, {0}}});
    broken.pop_back();
    rejects(
        [&] {
            (void)decode_props(broken, PropKind::cones);
        },
        "Truncated prop mesh must not survive shared container validation");
    rejects(
        [&] {
            (void)decode_props(model({{0, 0, {0}}}), PropKind::cones);
        },
        "An unresolved initial parent must not reference an actor outside the owned resource");
    rejects(
        [&] {
            (void)decode_props(model({{-1, 0, {}}, {0, 1, {4}}}), PropKind::cones);
        },
        "Invalid prop variant must not index outside the owned mesh table");
    rejects(
        [&] {
            (void)decode_props(empty, PropKind(99));
        },
        "Invalid prop controller kinds must be bounded before registration");
}

void canonical(const std::filesystem::path& root) {
    const toca::AssetStore store(root);

    struct Fixture {
        const char* prefix;
        std::array<std::size_t, 4> actors;
        std::array<std::int32_t, 3> cone_position;
    };

    for (const auto& fixture : {
             Fixture{"donl", {29, 8, 29, 14}, {-6337336, 478440, -9922144}},
             Fixture{"silv", {15, 10, 26, 0}, {27394048, 3978072, -18219008}},
             Fixture{"thru", {14, 10, 17, 24}, {-27721728, 62260, -30375944}},
             Fixture{"bran", {20, 5, 16, 0}, {-11141120, 81916, -5505024}},
             Fixture{"oult", {19, 6, 20, 18}, {-13762560, 485008, 10289152}},
             Fixture{"dons", {26, 8, 21, 14}, {-6835432, 484976, -8303448}},
             Fixture{"crof", {24, 7, 34, 24}, {2424832, 485024, -22544384}},
             Fixture{"knoc", {20, 5, 20, 27}, {-18713876, -357164, 5796684}},
             Fixture{"snet", {26, 10, 21, 21}, {-36306944, 1094504, -12255232}},
             Fixture{"land", {5, 0, 0, 24}, {-13762560, -1032180, -4587520}},
         }) {
        const auto archive = store.open_archive(std::string("data/") + fixture.prefix + "free.bfp");
        const std::array kinds{PropKind::cones, PropKind::posts, PropKind::signs, PropKind::flags};
        for (std::size_t kind = 0; kind < kinds.size(); ++kind) {
            const auto props = decode_props(archive.member(8 + kind).bytes(), kinds[kind]);
            expect(props.actors.size() == fixture.actors[kind], "Canonical prop controller count changed");
            if (kind == 0) {
                const auto& matrix = props.model.nodes[props.actors.front().root_node].transform;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    expect(matrix[12 + axis] == float(fixture.cone_position[axis]) / unit, "Canonical cone must retain its exact original placement conversion");
                }
                const bool lavaland = std::string_view(fixture.prefix) == "land";
                const auto visual = prepare_cone_model(props.model, lavaland ? 9 : 0);
                for (std::size_t node = 0; node < props.model.nodes.size(); ++node) {
                    expect(visual.nodes[node].transform == props.model.nodes[node].transform && visual.nodes[node].variants == props.model.nodes[node].variants,
                        "Cone visual-origin correction must retain original physical pivots and variant bindings");
                }
                for (std::size_t mesh = 0; mesh < props.model.meshes.size(); ++mesh) {
                    const auto& original = props.model.meshes[mesh];
                    const auto& prepared = visual.meshes[mesh];
                    expect(prepared.vertices.size() == original.vertices.size() && prepared.faces.size() == original.faces.size(),
                        "Cone origin correction must preserve the source shape and material faces");
                    for (std::size_t vertex = 0; vertex < original.vertices.size(); ++vertex) {
                        const auto& a = original.vertices[vertex];
                        const auto& b = prepared.vertices[vertex];
                        expect(a.position[0] == b.position[0] && a.position[2] == b.position[2] && a.normal == b.normal && a.u == b.u && a.v == b.v
                                   && (lavaland || a.position[1] == b.position[1]),
                            "Only Lavaland's visual height may change; other cone geometry, normals and UVs stay source-exact");
                    }
                    if (lavaland) {
                        const auto lowest = std::ranges::min(prepared.vertices, {}, [](const auto& vertex) {
                            return vertex.position[1];
                        });
                        const auto clearance = float(0x75c2) / unit + lowest.position[1];
                        expect(clearance >= 0 && clearance < .006f, "Lavaland's upright source support must leave ordinary millimeter clearance rather than a floating gargoyle");
                        const auto untouched = prepare_cone_model(props.model, 0);
                        expect(untouched.meshes[mesh].vertices.front().position == original.vertices.front().position, "The gargoyle correction must remain confined to Lavaland");
                        expect(std::abs(original.vertices.front().position[1] - prepared.vertices.front().position[1] - 41023.f / 131072.f) < .00001f,
                            "The render-only shift must match the independently audited GARCON pivot offset");
                    }
                }
            }
            if (kind == 3) {
                for (const auto& actor : props.actors) {
                    expect(actor.animation_node < props.model.nodes.size() && (props.model.nodes[actor.animation_node].id_flags & 0xffffff) == 1
                               && props.model.nodes[actor.animation_node].variants.size() == 3,
                        "Canonical animated flag must select its original three-variant ID1 node");
                }
                if (std::string_view(fixture.prefix) == "bran") {
                    expect(props.model.nodes.size() == 21 && props.model.nodes[0].transform[12] == 0 && props.actors.empty(),
                        "Original Brands self-parent fallback must materialize21 childless actors and zero flag controllers");
                }
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-props-tests [canonical-root]");
        }
        std::cout << "PASS: owning original prop scenegraph, registration, variants and source parent fallback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
