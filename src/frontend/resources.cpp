#include "frontend/resources.hpp"

#include "assets/store.hpp"
#include "formats/cm3d.hpp"
#include "formats/glb.hpp"
#include "formats/model_validation.hpp"
#include "image/artwork.hpp"
#include "image/decode.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace toca::frontend {
namespace {
void validate_mega_model(const Model& original, const Model& replacement) {
    const auto require = [](bool valid) {
        if (!valid) {
            throw std::runtime_error("Mega model must retain original structure and transforms except bounded symmetric wheel outsets");
        }
    };
    require(model_structure_matches(original, replacement));
    std::array<float, 4> wheel_outsets{}, wheel_sides{};
    std::array<unsigned, 4> wheel_counts{};
    bool moved_wheels = false;
    for (std::size_t index = 0; index < original.nodes.size(); ++index) {
        const auto& source = original.nodes[index];
        const auto& target = replacement.nodes[index];
        auto transform = target.transform;
        const auto id = source.id_flags & 0xffffffu;
        if (id >= 1 && id <= 4) {
            const auto wheel = id - 1;
            ++wheel_counts[wheel];
            const float side = source.transform[14] > 0 ? 1.f : -1.f;
            const float outset = side * (target.transform[14] - source.transform[14]);
            wheel_outsets[wheel] = outset;
            wheel_sides[wheel] = source.transform[14];
            if (outset != 0) {
                // Only the Mega visual pivot moves. The source wheel basis,
                // body/physics data and all other node components stay exact.
                constexpr std::array<float, 12> basis{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                require(std::ranges::equal(std::span(source.transform).first<12>(), basis) && source.transform[15] == 1 && source.transform[14] != 0);
                // CM3D authors offsets on a 16.16 grid; include its nearest
                // representable point at the 100mm upper bound.
                require(std::isfinite(outset) && outset > 0 && outset <= .1f + 1.f / 65536.f);
                moved_wheels = true;
            }
            transform[14] = source.transform[14];
        }
        require(source.transform == transform);
    }
    if (moved_wheels) {
        require(std::ranges::all_of(wheel_counts, [](unsigned count) {
            return count == 1;
        }));
        for (const auto axle : {0u, 2u}) {
            require(wheel_sides[axle] * wheel_sides[axle + 1] < 0 && std::abs(wheel_outsets[axle] - wheel_outsets[axle + 1]) <= 1e-6f);
        }
    }
}
} // namespace

std::array<std::size_t, 3> car_member_indices(std::uint32_t car_id) {
    // TOCA 1.1a FUN_0044dc6d: skip this many members, then consume three.
    constexpr std::array<std::size_t, 11> first{1, 4, 7, 13, 16, 19, 22, 25, 28, 31, 10};
    if (car_id >= first.size()) {
        throw std::runtime_error("Original car ID must be within 0..10");
    }
    return {first[car_id], first[car_id] + 1, first[car_id] + 2};
}

std::int16_t CarLiveryMaterials::texture_slot(std::uint32_t node_id, std::int16_t source_slot, bool second_driver, bool paired) const {
    // 4437f0 (TT, split and Linkup) loads canonical materials without the
    // 44df6e/49a780 team-pair remap. Personal names are applied separately.
    if (!paired) {
        return source_slot;
    }
    node_id &= 0xffffffu;
    // Source first actor: side node15 is forced to A. Source second actor:
    // side node16, roof node9 and rear name node21 are changed to B.
    const bool first_side = !second_driver && node_id == 15;
    const bool second_side = second_driver && node_id == 16;
    if (number_a >= 0 && number_b >= 0) {
        if (first_side && source_slot == number_b) {
            return number_a;
        }
        if ((second_side || (second_driver && node_id == 9)) && source_slot == number_a) {
            return number_b;
        }
    }
    if (list_a >= 0 && list_b >= 0) {
        if (first_side && source_slot == list_b) {
            return list_a;
        }
        if ((second_side || (second_driver && node_id == 21)) && source_slot == list_a) {
            return list_b;
        }
    }
    return source_slot;
}

CarLiveryMaterials car_livery_materials(const Model& model, std::uint32_t render_id) {
    // 44df6e supplies each team's original race numbers. Vectra/Vectra2
    // already have distinct source models; Tank/Flexmobile have no pair swap.
    constexpr std::array<std::array<unsigned, 2>, 11> numbers{{{10, 20}, {1, 7}, {0, 0}, {3, 11}, {9, 15}, {4, 23}, {12, 14}, {2, 22}, {0, 0}, {0, 0}, {0, 0}}};
    if (render_id >= numbers.size()) {
        throw std::runtime_error("Car livery requires an original render ID within 0..10");
    }
    if (numbers[render_id][0] == 0) {
        return {};
    }
    const auto slot = [&](const std::string& name) -> std::int16_t {
        const auto found = std::ranges::find_if(model.textures, [&](const auto& texture) {
            return std::ranges::equal(texture.name, name, [](unsigned char a, unsigned char b) {
                const auto lower = [](unsigned char c) {
                    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                };
                return lower(a) == lower(b);
            });
        });
        return found == model.textures.end() ? -1 : std::int16_t(found - model.textures.begin());
    };
    return {slot("Num" + std::to_string(numbers[render_id][0])), slot("Num" + std::to_string(numbers[render_id][1])), slot("ListA"), slot("ListB")};
}

CarModels load_car_models(const AssetStore& store, std::uint32_t car_id, const std::filesystem::path& mega_models) {
    CarModels result;
    result.car_id = car_id;
    result.member_indices = car_member_indices(car_id);
    const auto archive = store.open_archive("data/allcars.bfp");
    auto cursor = archive.cursor();
    for (std::size_t index = 0; index < result.member_indices.front(); ++index) {
        if (!cursor.skip()) {
            throw std::runtime_error("Car archive ended before the selected model triple");
        }
    }
    for (std::size_t index = 0; index < result.models.size(); ++index) {
        const auto member = cursor.read();
        if (!member) {
            throw std::runtime_error("Car archive ended inside the selected model triple");
        }
        result.models[index] = parse_cm3d(member->bytes());
    }
    if (!mega_models.empty()) {
        const auto path = mega_models / ("car-" + std::to_string(car_id) + ".glb");
        try {
            if (std::filesystem::exists(path)) {
                auto replacement = load_glb(path);
                validate_mega_model(result.models[0], replacement);
                for (const auto& texture : replacement.textures) {
                    (void)decode_model_texture(texture);
                }
                result.models[0] = std::move(replacement);
                std::size_t vertices = 0, triangles = 0;
                for (const auto& mesh : result.models[0].meshes) {
                    vertices += mesh.vertices.size();
                    triangles += mesh.faces.size();
                }
                std::clog << "Mega model: " << path.string() << " (" << vertices << " vertices, " << triangles << " triangles)\n";
            }
        } catch (const std::runtime_error& error) {
            std::clog << "Mega model unavailable: " << path.string() << ": " << error.what() << "; using original highest detail\n";
        }
    }
    return result;
}

namespace {
Surface load_frontend_image(const std::filesystem::path& artwork_directory, std::string_view name) {
    auto surface = load_artwork(artwork_directory, std::string(name) + ".webp", false);
    if (surface->w != 640 || surface->h != 480) {
        throw std::runtime_error("Frontend image must have its authored 640x480 dimensions: " + std::string(name));
    }
    return surface;
}
} // namespace

Surface load_title(const std::filesystem::path& artwork_directory) {
    return load_frontend_image(artwork_directory, "title");
}

Surface load_copyright(const std::filesystem::path& artwork_directory, bool mega) {
    if (!mega) {
        return load_frontend_image(artwork_directory, "cpyright");
    }
    auto surface = load_artwork(artwork_directory, "cpyright.webp", true);
    if (surface->w < 640 || surface->h < 480 || surface->w * 3 != surface->h * 4) {
        throw std::runtime_error("Copyright artwork must preserve its authored 4:3 aspect at 640x480 or greater");
    }
    return surface;
}
} // namespace toca::frontend
