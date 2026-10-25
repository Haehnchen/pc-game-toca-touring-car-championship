#pragma once

#include "formats/model.hpp"
#include "image/decode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace toca {
class AssetStore;
}

namespace toca::frontend {
struct CarModels {
    std::uint32_t car_id = 0;
    std::array<std::size_t, 3> member_indices;
    std::array<Model, 3> models;
};

// 49a780/49adc2 remap the existing driver names/numbers at draw time. The
// source slots and shared model/texture caches remain canonical for both actors.
struct CarLiveryMaterials {
    std::int16_t number_a = -1, number_b = -1, list_a = -1, list_b = -1;

    std::int16_t texture_slot(std::uint32_t node_id, std::int16_t source_slot, bool second_driver, bool paired = true) const;
};

CarLiveryMaterials car_livery_materials(const Model& model, std::uint32_t render_id);

// Original car IDs, including the separate Vectra2 ID 10, select archive indices.
// The three decoded members retain file order; runtime detail selection is separate.
std::array<std::size_t, 3> car_member_indices(std::uint32_t car_id);
// Mega overrides are optional car-<id>.glb files in this directory. Missing or
// invalid files retain the original high member; low/medium remain canonical.
CarModels load_car_models(const AssetStore& store, std::uint32_t car_id, const std::filesystem::path& mega_models = {});
// Copyright artwork keeps its authored 4:3 aspect, with an optional Mega override.
Surface load_title(const std::filesystem::path& artwork_directory);
Surface load_copyright(const std::filesystem::path& artwork_directory, bool mega = false);
} // namespace toca::frontend
