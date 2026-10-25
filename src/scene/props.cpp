#include "scene/props.hpp"

#include "formats/static_model.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace toca::scene {
namespace {
std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("Props: truncated original field");
    }
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return u16(bytes, offset) | (std::uint32_t(u16(bytes, offset + 2)) << 16);
}

std::size_t actor_limit(PropKind kind) {
    switch (kind) {
        case PropKind::cones:
        case PropKind::posts:
            return 30;
        case PropKind::signs:
            return 50;
        case PropKind::flags:
            return 40;
    }
    throw std::runtime_error("Props: unknown original controller kind");
}

std::uint32_t find_animation_node(const Model& model, const std::vector<std::vector<std::uint32_t>>& children, std::uint32_t node) {
    std::vector<std::uint32_t> pending{node};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if ((model.nodes[current].id_flags & 0xffffffu) == 1) {
            return current;
        }
        for (auto child = children[current].rbegin(); child != children[current].rend(); ++child) {
            pending.push_back(*child);
        }
    }
    return UINT32_MAX;
}
} // namespace

PropResources decode_props(std::span<const std::uint8_t> bytes, PropKind kind) {
    const auto limit = actor_limit(kind);
    if (u32(bytes, 0) != 0x44334d43 || u32(bytes, 4) != 100 || u32(bytes, 8) != 32 || u32(bytes, 20) != 32) {
        throw std::runtime_error("Props: expected original version100 CM3D container");
    }
    // Empty FLAGS has zero counted sections and four unused exporter bytes.
    // 494a40/495ee0 consume the counts; the ordinary CM3D reader remains strict.
    if (u16(bytes, 12) == 0 && u16(bytes, 14) == 0 && u16(bytes, 16) == 0 && u32(bytes, 24) == 32 && u32(bytes, 28) == 32 && bytes.size() == 36) {
        return {};
    }

    const auto count = u16(bytes, 12);
    const auto mesh_start = std::size_t(u32(bytes, 24));
    std::vector<std::uint8_t> validation_bytes(bytes.begin(), bytes.end());
    std::vector<std::int16_t> parents;
    std::size_t position = 32, preceding_root = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (position > mesh_start || mesh_start - position < 80 || position > bytes.size() || bytes.size() - position < 80) {
            throw std::runtime_error("Props: truncated original node section");
        }
        const auto size = u32(bytes, position);
        const auto variants = bytes[position + 76];
        if (size != ((80u + 2u * variants + 3u) & ~3u) || size > mesh_start - position || size > bytes.size() - position) {
            throw std::runtime_error("Props: invalid original node size");
        }
        const auto parent = std::bit_cast<std::int16_t>(u16(bytes, position + 4));
        if (parent < -1) {
            throw std::runtime_error("Props: parent precedes owned archive actor table");
        }
        parents.push_back(parent);
        if (parent == -1) {
            preceding_root = index;
        } else if (index == 0) {
            throw std::runtime_error("Props: unresolved parent has no preceding owned root");
        } else if (std::size_t(parent) >= index) {
            // Give the shared strict validator an earlier parent without adding
            // a root: the source's inherited static/dynamic branch must survive.
            // Actual parent resolution still uses the unchanged original below.
            validation_bytes[position + 4] = std::uint8_t(preceding_root);
            validation_bytes[position + 5] = std::uint8_t(preceding_root >> 8);
        }
        position += size;
    }
    if (position != mesh_start) {
        throw std::runtime_error("Props: unaccounted original node bytes");
    }
    auto parsed = parse_static_model(validation_bytes);
    auto& decoded = parsed.model;
    PropResources result;
    result.baked = std::move(parsed.baked);
    result.model.meshes = std::move(decoded.meshes);
    result.model.textures = std::move(decoded.textures);
    result.model.trailing_null_byte = decoded.trailing_null_byte;
    bool dynamic = false, have_root = false;
    for (std::size_t index = 0; index < decoded.nodes.size(); ++index) {
        const auto parent = parents[index];
        auto node = std::move(decoded.nodes[index]);
        if (parent == -1) {
            have_root = true;
            dynamic = (node.id_flags & 0x20000000u) == 0;
        }
        if (!have_root) {
            throw std::runtime_error("Props: unresolved parent has no preceding owned root");
        }
        if (!dynamic) {
            continue;
        }
        if (parent == -1 || std::size_t(parent) < result.model.nodes.size()) {
            node.parent = parent;
            result.model.nodes.push_back(std::move(node));
        } else {
            // 494c6d..96: an unresolved parent skips47f260 and keeps EBP,
            // then overwrites that previous actor's matrix, ID and mesh set.
            // Brands FLAGS exercises this source behavior for every child.
            if (result.model.nodes.empty()) {
                throw std::runtime_error("Props: unresolved parent has no preceding owned actor");
            }
            auto& previous = result.model.nodes.back();
            node.parent = previous.parent;
            node.id_flags = (previous.id_flags & 0xff000000u) | (node.id_flags & 0xffffffu);
            previous = std::move(node);
        }
    }
    std::vector<std::vector<std::uint32_t>> children(result.model.nodes.size());
    std::vector<std::uint32_t> roots;
    for (std::uint32_t index = 0; index < result.model.nodes.size(); ++index) {
        const auto parent = result.model.nodes[index].parent;
        if (parent < 0) {
            roots.push_back(index);
        } else {
            children[std::size_t(parent)].push_back(index);
        }
    }
    for (std::size_t index = 0; index < roots.size() && result.actors.size() < limit; ++index) {
        const auto root = roots[index];
        const auto id = result.model.nodes[root].id_flags & 0xffffffu;
        if (id == 998 || id == 999) {
            continue;
        }
        PropActor actor{root};
        if (kind == PropKind::flags) {
            if (children[root].empty()) {
                break; // 44d72f stops on a root with no child, after loading it.
            }
            for (auto sibling = index; sibling < roots.size() && actor.animation_node == UINT32_MAX; ++sibling) {
                actor.animation_node = find_animation_node(result.model, children, roots[sibling]);
            }
        }
        result.actors.push_back(actor);
    }
    return result;
}

Model prepare_cone_model(Model model, std::uint32_t track_id) {
    if (track_id != 9 || model.textures.size() != 2 || model.textures[0].name != "GARG01" || model.textures[1].name != "GARG02") {
        return model;
    }
    for (auto& mesh : model.meshes) {
        if (mesh.name != "GARCON_1" || mesh.vertices.empty()) {
            continue;
        }
        // GARCON has the ordinary cone's height but its authored center sits
        // .313 units above the pivot expected by 458ac0's support/tilt model.
        // Center only the render mesh, in every detail profile, before rotation.
        const auto [low, high] = std::ranges::minmax_element(mesh.vertices, {}, [](const auto& vertex) {
            return vertex.position[1];
        });
        const auto center = (low->position[1] + high->position[1]) * .5f;
        for (auto& vertex : mesh.vertices) {
            vertex.position[1] -= center;
        }
    }
    return model;
}
} // namespace toca::scene
