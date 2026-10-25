#include "formats/glb.hpp"

#define CGLTF_IMPLEMENTATION
#include <algorithm>
#include <cgltf.h>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <meshoptimizer.h>
#include <stdexcept>
#include <string_view>

namespace toca {
namespace {
constexpr std::size_t max_file = 64 * 1024 * 1024, max_allocations = 128 * 1024 * 1024;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error("GLB: " + std::string(message));
    }
}

struct Budget {
    std::size_t used = 0;
};

struct alignas(std::max_align_t) Allocation {
    std::size_t size;
};

void* allocate(void* user, cgltf_size size) {
    auto& budget = *static_cast<Budget*>(user);
    if (size > max_allocations - budget.used) {
        return nullptr;
    }
    auto* header = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!header) {
        return nullptr;
    }
    header->size = size;
    budget.used += size;
    return header + 1;
}

void release(void* user, void* pointer) {
    if (pointer) {
        auto* header = static_cast<Allocation*>(pointer) - 1;
        static_cast<Budget*>(user)->used -= header->size;
        std::free(header);
    }
}

std::string name(const char* text) {
    require(text && std::strlen(text) > 0 && std::strlen(text) <= 64, "missing or oversized resource name");
    return text;
}

std::uint32_t integer(std::string_view text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    require(result.ec == std::errc{} && result.ptr == text.data() + text.size(), "expected an unsigned original node field");
    return value;
}

void node_extras(const cgltf_extras& extras, ModelNode& output, std::size_t mesh_count) {
    require(extras.data && std::strlen(extras.data) <= 8192, "missing or oversized original node metadata");
    const std::string_view json(extras.data);
    std::array<jsmntok_t, 1024> tokens{};
    jsmn_parser parser;
    jsmn_init(&parser);
    const auto count = jsmn_parse(&parser, json.data(), json.size(), tokens.data(), tokens.size());
    require(count > 0 && tokens[0].type == JSMN_OBJECT, "invalid original node metadata");
    const auto text = [&](int index) {
        return json.substr(std::size_t(tokens[index].start), std::size_t(tokens[index].end - tokens[index].start));
    };
    bool has_id = false, has_variants = false;
    int index = 1;
    while (index < count) {
        require(tokens[index].type == JSMN_STRING && index + 1 < count, "invalid original node metadata field");
        const auto key = text(index++);
        const auto& token = tokens[index];
        if (key == "toca_id_flags") {
            require(!has_id && token.type == JSMN_PRIMITIVE, "invalid or repeated node ID");
            output.id_flags = integer(text(index));
            has_id = true;
        } else if (key == "toca_variants") {
            require(!has_variants && token.type == JSMN_ARRAY && token.size <= 256, "invalid or repeated damage bindings");
            for (int variant = 0; variant < token.size; ++variant) {
                require(index + 1 + variant < count && tokens[index + 1 + variant].type == JSMN_PRIMITIVE, "invalid damage mesh index");
                const auto value = integer(text(index + 1 + variant));
                require(value == 65535 || value < mesh_count, "damage mesh index out of range");
                output.variants.push_back(std::uint16_t(value));
            }
            has_variants = true;
        }
        const auto end = token.end;
        do {
            ++index;
        } while (index < count && tokens[index].start < end);
    }
    require(has_id && has_variants, "node metadata requires original ID and damage bindings");
}

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type, cgltf_type shape) {
    const cgltf_accessor* result = nullptr;
    for (std::size_t index = 0; index < primitive.attributes_count; ++index) {
        const auto& source = primitive.attributes[index];
        if (source.type == type) {
            require(!result && source.index == 0, "duplicate vertex attribute");
            result = source.data;
        }
    }
    require(result && result->type == shape && result->component_type == cgltf_component_type_r_32f && !result->is_sparse && !result->normalized,
        "vertices require nonsparse float32 position/normal/UV attributes");
    return result;
}

const cgltf_accessor* ghost_position_attribute(const cgltf_primitive& primitive) {
    const cgltf_accessor* result = nullptr;
    for (std::size_t index = 0; index < primitive.attributes_count; ++index) {
        const auto& source = primitive.attributes[index];
        if (source.type == cgltf_attribute_type_custom && source.name && std::string_view(source.name) == "_TOCA_GHOST_POSITION") {
            require(!result, "duplicate Ghost position attribute");
            result = source.data;
        }
    }
    require(!result || (result->type == cgltf_type_vec3 && result->component_type == cgltf_component_type_r_32f && !result->is_sparse && !result->normalized),
        "Ghost positions require nonsparse float32 vectors");
    return result;
}

std::array<std::uint8_t, 4> material_color(const cgltf_material* material) {
    std::array<std::uint8_t, 4> result{255, 255, 255, 255};
    if (material) {
        require(material->has_pbr_metallic_roughness, "material requires a base color");
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const auto value = material->pbr_metallic_roughness.base_color_factor[channel];
            require(std::isfinite(value) && value >= 0 && value <= 1, "invalid material color");
            result[channel] = std::uint8_t(std::lround(value * 255));
        }
    }
    return result;
}

std::int16_t material_texture(const cgltf_material* material, const cgltf_data& data) {
    if (!material || !material->pbr_metallic_roughness.base_color_texture.texture) {
        return -1;
    }
    const auto& view = material->pbr_metallic_roughness.base_color_texture;
    require(view.texcoord == 0 && !view.has_transform, "texture UV transforms are unsupported");
    const auto* texture = view.texture;
    require(texture->has_webp && texture->webp_image && !texture->image && !texture->has_basisu, "native textures require embedded EXT_texture_webp images");
    return std::int16_t(texture->webp_image - data.images);
}

ModelMesh mesh(const cgltf_mesh& source, const cgltf_data& data) {
    ModelMesh output;
    output.name = name(source.name);
    require(source.primitives_count > 0 && source.primitives_count <= 4096 && !source.weights_count, "invalid mesh primitive count or morph weights");
    const auto& first = source.primitives[0];
    const auto* positions = attribute(first, cgltf_attribute_type_position, cgltf_type_vec3);
    const auto* normals = attribute(first, cgltf_attribute_type_normal, cgltf_type_vec3);
    const auto* uvs = attribute(first, cgltf_attribute_type_texcoord, cgltf_type_vec2);
    const auto* ghost_positions = ghost_position_attribute(first);
    require(positions->count > 0 && positions->count <= 65535 && normals->count == positions->count && uvs->count == positions->count, "invalid original16-bit mesh vertex count");
    if (ghost_positions) {
        require(ghost_positions->count == positions->count, "Ghost positions must match the original vertex count");
        output.ghost_positions.resize(positions->count);
    }
    output.vertices.resize(positions->count);
    for (std::size_t index = 0; index < output.vertices.size(); ++index) {
        auto& vertex = output.vertices[index];
        std::array<float, 2> uv;
        require(cgltf_accessor_read_float(positions, index, vertex.position.data(), 3) && cgltf_accessor_read_float(normals, index, vertex.normal.data(), 3)
                    && cgltf_accessor_read_float(uvs, index, uv.data(), 2),
            "cannot read vertex attributes");
        for (const auto value : vertex.position) {
            require(std::isfinite(value) && std::abs(value) <= 32768, "invalid vertex position");
        }
        for (const auto value : vertex.normal) {
            // Canonical scenery includes nonunit normals (Croft's bus uses4).
            // Float32 rounds the largest signed16.16 value to32768 exactly.
            require(std::isfinite(value) && std::abs(value) <= 32768, "invalid vertex normal");
        }
        require(std::isfinite(uv[0]) && std::isfinite(uv[1]) && std::abs(uv[0]) <= 64 && std::abs(uv[1]) <= 64, "invalid vertex UV");
        vertex.u = uv[0];
        vertex.v = uv[1];
        if (ghost_positions) {
            auto& ghost = output.ghost_positions[index];
            require(cgltf_accessor_read_float(ghost_positions, index, ghost.data(), 3), "cannot read Ghost positions");
            for (const auto value : ghost) {
                require(std::isfinite(value) && std::abs(value) <= 32768, "invalid Ghost position");
            }
        }
    }
    for (std::size_t primitive_index = 0; primitive_index < source.primitives_count; ++primitive_index) {
        const auto& primitive = source.primitives[primitive_index];
        require(primitive.type == cgltf_primitive_type_triangles && !primitive.targets_count && !primitive.has_draco_mesh_compression
                    && primitive.attributes_count == (ghost_positions ? 4u : 3u),
            "native meshes require position/normal/UV triangles with optional Ghost positions");
        require(attribute(primitive, cgltf_attribute_type_position, cgltf_type_vec3) == positions && attribute(primitive, cgltf_attribute_type_normal, cgltf_type_vec3) == normals
                    && attribute(primitive, cgltf_attribute_type_texcoord, cgltf_type_vec2) == uvs && ghost_position_attribute(primitive) == ghost_positions,
            "mesh primitives must share the original vertex table");
        const auto* indices = primitive.indices;
        require(indices && indices->type == cgltf_type_scalar && indices->component_type == cgltf_component_type_r_16u && !indices->is_sparse && indices->count % 3 == 0
                    && indices->count / 3 <= 65535 - output.faces.size(),
            "invalid original16-bit triangle index table");
        const auto color = material_color(primitive.material);
        const auto texture = material_texture(primitive.material, data);
        const std::string_view extras = primitive.extras.data ? primitive.extras.data : "";
        require(extras.empty() || extras == R"({"toca_ghost_hidden":true})", "unsupported mesh primitive metadata");
        require(extras.empty() || ghost_positions, "Ghost interior faces require body-attached Ghost positions");
        for (std::size_t index = 0; index < indices->count; index += 3) {
            ModelFace face{{}, texture, color, !extras.empty()};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto value = cgltf_accessor_read_index(indices, index + corner);
                require(value < output.vertices.size(), "triangle vertex index out of range");
                face.indices[corner] = std::uint16_t(value);
            }
            output.faces.push_back(face);
        }
    }
    return output;
}

void decode_views(cgltf_data& data, Budget& budget) {
    require(data.buffers_count >= 1 && data.buffers_count <= 2 && !data.buffers[0].uri && data.bin && data.buffers[0].size <= data.bin_size
                && data.bin_size - data.buffers[0].size <= 3,
        "model requires one embedded binary buffer and optional meshopt fallback");
    data.buffers[0].data = const_cast<void*>(data.bin);
    if (data.buffers_count == 2) {
        require(!data.buffers[1].uri && data.buffers[1].size <= max_allocations, "invalid meshopt fallback buffer");
    }
    for (std::size_t index = 0; index < data.buffer_views_count; ++index) {
        auto& view = data.buffer_views[index];
        require(view.buffer && view.offset <= view.buffer->size && view.size <= view.buffer->size - view.offset, "invalid binary buffer view");
        if (!view.has_meshopt_compression) {
            require(view.buffer == &data.buffers[0], "uncompressed view requires embedded bytes");
            continue;
        }
        const auto& compressed = view.meshopt_compression;
        require(compressed.buffer == &data.buffers[0] && compressed.offset <= data.buffers[0].size && compressed.size > 0
                    && compressed.size <= data.buffers[0].size - compressed.offset && compressed.count > 0 && compressed.stride > 0
                    && compressed.count <= max_allocations / compressed.stride && compressed.count * compressed.stride == view.size
                    && (!view.stride || view.stride == compressed.stride) && compressed.filter == cgltf_meshopt_compression_filter_none,
            "invalid bounded meshopt buffer");
        const bool attributes = compressed.mode == cgltf_meshopt_compression_mode_attributes;
        const bool triangles = compressed.mode == cgltf_meshopt_compression_mode_triangles;
        const bool indices = compressed.mode == cgltf_meshopt_compression_mode_indices;
        require((attributes && compressed.stride <= 256 && compressed.stride % 4 == 0)
                    || ((triangles || indices) && (compressed.stride == 2 || compressed.stride == 4) && (!triangles || compressed.count % 3 == 0)),
            "unsupported meshopt mode/stride");
        view.data = allocate(&budget, view.size);
        require(view.data != nullptr, "meshopt decoded allocation budget exceeded");
        const auto* source = static_cast<const unsigned char*>(data.bin) + compressed.offset;
        const auto result = attributes  ? meshopt_decodeVertexBuffer(view.data, compressed.count, compressed.stride, source, compressed.size)
                            : triangles ? meshopt_decodeIndexBuffer(view.data, compressed.count, compressed.stride, source, compressed.size)
                                        : meshopt_decodeIndexSequence(view.data, compressed.count, compressed.stride, source, compressed.size);
        require(result == 0, "invalid meshopt bitstream");
    }
}
} // namespace

Model parse_glb(std::span<const std::uint8_t> bytes) {
    require(bytes.size() >= 20 && bytes.size() <= max_file, "invalid bounded GLB file size");
    const auto u32 = [&](std::size_t offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    require(u32(0) == 0x46546c67 && u32(4) == 2 && u32(8) == bytes.size(), "expected complete GLB version2");
    Budget budget;
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    options.json_token_count = 262144;
    options.memory = {allocate, release, &budget};
    cgltf_data* parsed = nullptr;
    const auto result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
    require(result == cgltf_result_success, "cannot parse GLB2 structure");
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> document(parsed, cgltf_free);
    auto& data = *document;
    require(data.nodes_count > 0 && data.nodes_count <= 4096 && data.meshes_count > 0 && data.meshes_count <= 4096 && data.images_count <= 1024 && data.textures_count <= 1024
                && data.materials_count <= 16384 && data.accessors_count <= 16384 && data.buffer_views_count <= 16384 && !data.animations_count && !data.skins_count,
        "model exceeds native resource bounds or includes animation/skins");
    for (std::size_t index = 0; index < data.extensions_required_count; ++index) {
        const std::string_view extension(data.extensions_required[index]);
        require(extension == "EXT_texture_webp" || extension == "KHR_materials_unlit" || extension == "EXT_meshopt_compression", "unsupported required GLB extension");
    }
    decode_views(data, budget);
    for (std::size_t index = 0; index < data.accessors_count; ++index) {
        const auto& accessor = data.accessors[index];
        require(accessor.buffer_view && !accessor.is_sparse && accessor.count > 0 && accessor.count <= 196605 && accessor.component_type != cgltf_component_type_invalid
                    && accessor.type != cgltf_type_invalid,
            "invalid bounded nonsparse accessor");
        const auto size = cgltf_calc_size(accessor.type, accessor.component_type);
        const auto component_size = cgltf_component_size(accessor.component_type);
        const auto& view = *accessor.buffer_view;
        require(size > 0 && accessor.stride >= size && accessor.stride <= 256 && accessor.offset <= view.size && size <= view.size - accessor.offset
                    && accessor.count - 1 <= (view.size - accessor.offset - size) / accessor.stride,
            "accessor exceeds its embedded buffer view");
        const auto address = reinterpret_cast<std::uintptr_t>(cgltf_buffer_view_data(&view)) + accessor.offset;
        require(component_size > 0 && address % component_size == 0 && accessor.stride % component_size == 0, "misaligned vertex/index accessor");
    }
    std::size_t vertices = 0, faces = 0, primitives = 0;
    for (std::size_t index = 0; index < data.meshes_count; ++index) {
        const auto& source = data.meshes[index];
        require(source.primitives_count > 0 && source.primitives_count <= 4096, "invalid mesh primitive count");
        vertices += attribute(source.primitives[0], cgltf_attribute_type_position, cgltf_type_vec3)->count;
        primitives += source.primitives_count;
        for (std::size_t primitive_index = 0; primitive_index < source.primitives_count; ++primitive_index) {
            const auto& primitive = source.primitives[primitive_index];
            require(primitive.indices && primitive.attributes_count == (ghost_position_attribute(primitive) ? 4u : 3u) && !primitive.targets_count,
                "missing triangle indices or unsupported attributes");
            for (std::size_t attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
                require(primitive.attributes[attribute_index].data != nullptr, "missing vertex accessor");
            }
            faces += primitive.indices->count / 3;
        }
    }
    require(vertices <= 2 * 1024 * 1024 && faces <= 2 * 1024 * 1024 && primitives <= 8192, "decoded geometry exceeds native allocation bounds");
    require(cgltf_validate(&data) == cgltf_result_success, "invalid GLB accessor/resource bounds");
    Model output;
    for (std::size_t index = 0; index < data.images_count; ++index) {
        const auto& image = data.images[index];
        require(!image.uri && image.buffer_view && image.mime_type && std::string_view(image.mime_type) == "image/webp", "images must be embedded WebP");
        const auto& view = *image.buffer_view;
        require(view.size > 12 && view.size <= 16 * 1024 * 1024 && !view.has_meshopt_compression && view.buffer == &data.buffers[0], "invalid embedded image bounds");
        const auto* start = cgltf_buffer_view_data(&view);
        require(std::memcmp(start, "RIFF", 4) == 0 && std::memcmp(start + 8, "WEBP", 4) == 0, "invalid embedded WebP image");
        output.textures.push_back({name(image.name), {start, start + view.size}, ImageEncoding::webp});
    }
    for (std::size_t index = 0; index < data.meshes_count; ++index) {
        output.meshes.push_back(mesh(data.meshes[index], data));
    }
    for (std::size_t index = 0; index < data.nodes_count; ++index) {
        const auto& source = data.nodes[index];
        require(
            source.has_matrix && !source.has_translation && !source.has_rotation && !source.has_scale && !source.skin && !source.weights_count && !source.has_mesh_gpu_instancing,
            "native nodes require original matrices without animation/instancing");
        ModelNode node;
        node.parent = source.parent ? std::int16_t(source.parent - data.nodes) : -1;
        require(node.parent < std::int32_t(index), "node parent must precede child");
        for (std::size_t component = 0; component < 16; ++component) {
            require(std::isfinite(source.matrix[component]) && std::abs(source.matrix[component]) <= 32768, "invalid original node matrix");
            node.transform[component] = source.matrix[component];
        }
        node_extras(source.extras, node, output.meshes.size());
        if (source.mesh) {
            require(!node.variants.empty() && node.variants[0] == std::size_t(source.mesh - data.meshes), "node mesh does not match its first damage binding");
        }
        output.nodes.push_back(std::move(node));
    }
    return output;
}

Model load_glb(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(bool(file), "cannot open model " + path.string());
    const auto size = file.tellg();
    require(size >= 0 && size <= std::streamoff(max_file), "invalid bounded GLB file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    require(bool(file.read(reinterpret_cast<char*>(bytes.data()), size)), "cannot read GLB model");
    return parse_glb(bytes);
}
} // namespace toca
