#include "app/session_models.hpp"
#include "assets/store.hpp"
#include "formats/model_validation.hpp"
#include "scene/resources.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t triangles(const toca::Model& model) {
    std::size_t result = 0;
    for (const auto& mesh : model.meshes) {
        result += mesh.faces.size();
    }
    return result;
}

void check(const char* data_root, const std::filesystem::path& mega) {
    using namespace toca::scene;
    const toca::AssetStore store(data_root);
    std::set<std::vector<std::uint8_t>> sleeve_styles;
    for (const auto car : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 9u}) {
        const auto original = load_cockpit_resources(store, car);
        const auto updated = load_cockpit_resources(store, car, mega);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            expect(updated.models[0].textures[slot].bmp == updated.models[1].textures[slot].bmp, "Both hands must use the same car's authored material set");
        }
        sleeve_styles.insert(updated.models[0].textures[0].bmp);
        for (const auto role : {0u, 1u, 3u, 4u}) {
            expect(triangles(updated.models[role]) > triangles(original.models[role]), "Both Mega roles must have actual added geometry for every cockpit");
            expect(updated.models[role].textures.size() == (role < 2 ? 4u : 1u), "Mega role must preserve its original material count");
            for (const auto& texture : updated.models[role].textures) {
                expect(texture.encoding == toca::ImageEncoding::webp, "Every Mega hand/shared material must contain authored WebP artwork");
            }
            expect(original.models[role].nodes.size() == updated.models[role].nodes.size(), "Mega cockpit hierarchy changed");
            for (std::size_t node = 0; node < original.models[role].nodes.size(); ++node) {
                expect(original.models[role].nodes[node].transform == updated.models[role].nodes[node].transform, "Mega cockpit changed an original pivot");
            }
        }
        if (car == 0) {
            expect(triangles(updated.models[2]) > triangles(original.models[2]) && updated.models[2].textures.size() == 4,
                "Honda must load its actual four-slot solid Mega dashboard");
            expect(toca::model_structure_matches(original.models[2], updated.models[2]) && toca::model_node_transforms_match(original.models[2], updated.models[2]),
                "Mega dashboard must preserve every original node, pivot, variant and material slot");
            for (const auto& texture : updated.models[2].textures) {
                expect(texture.encoding == toca::ImageEncoding::webp, "All four Honda dashboard materials must load as authored WebP");
            }
        } else {
            const auto role = 2u;
            expect(triangles(original.models[role]) == triangles(updated.models[role]) && original.models[role].textures[0].bmp == updated.models[role].textures[0].bmp,
                "Other cars must retain their own original dashboard until their individual package is accepted");
        }
    }
    expect(sleeve_styles.size() == 9, "Nine distinct vehicle sleeve styles must remain bound to their own interiors");
    for (const auto absent : {8u, 10u}) {
        const auto resources = load_cockpit_resources(store, absent, mega);
        expect(resources.models[2].nodes.empty() && resources.models[3].nodes.empty() && resources.models[4].nodes.empty(), "Absent interiors must not acquire Mega parts");
    }
    const auto original = load_cockpit_resources(store, 0);
    const auto very_high = load_scene_resources(store, resolve_setup({.detail = 3}), mega);
    const auto updated = load_scene_resources(store, resolve_setup({.detail = 4}), mega);
    expect(triangles(very_high.cockpit.models[4]) == triangles(original.models[4]), "Original profile must not activate Mega cockpit geometry");
    expect(triangles(very_high.cockpit.models[0]) == triangles(original.models[0]), "Original profile must retain its original hand geometry");
    expect(very_high.cockpit.models[2].textures[0].bmp == original.models[2].textures[0].bmp,
        "Original detail must retain the full source dashboard independently of the supplied Mega directory");
    expect(updated.cockpit.models[2].textures[0].encoding == toca::ImageEncoding::webp, "Normal Mega loading must activate the Honda dashboard");
    expect(triangles(updated.cockpit.models[4]) > triangles(original.models[4]), "Normal scene loading must activate the shared Mega wheel");

    toca::SessionModels models(updated.car, updated.cockpit, mega);
    expect(&models.car(store, 0) == &updated.car && &models.cockpit(store, 0) == &updated.cockpit,
        "Both local players must reuse the primary car and cockpit without copying or loading them again");
    const toca::frontend::CarModels* vectra;
    const toca::frontend::CarModels* vectra2;
    const CockpitResources* audi;
    {
        const auto loading = store.for_loading();
        vectra = &models.car(loading, 2);
        vectra2 = &models.car(loading, 10);
        audi = &models.cockpit(loading, 1);
    }
    expect(vectra != vectra2 && vectra->car_id == 2 && vectra2->car_id == 10 && vectra->member_indices != vectra2->member_indices,
        "Session reuse must preserve Vectra2's separate render ID and archive triple");
    expect(&models.car(store, 2) == vectra && &models.car(store, 10) == vectra2 && &std::as_const(models).car(2) == vectra && &models.cockpit(store, 1) == audi,
        "Cached model references must remain stable across other IDs and outlive the temporary loading store");
    expect(audi->shared_roles == updated.cockpit.shared_roles, "SessionModels must propagate the primary cockpit's shared-role lifetime to another car");
    toca::SessionModels plain(very_high.car, very_high.cockpit, {});
    const auto& plain_audi = plain.cockpit(store, 1);
    expect(&plain_audi != audi && plain_audi.models[4].textures[0].encoding == toca::ImageEncoding::bmp && audi->models[4].textures[0].encoding == toca::ImageEncoding::webp,
        "Separate scene/detail selections must retain their own original or Mega cockpit resources");

    const auto invalid = std::filesystem::current_path() / "invalid-cockpit";
    const auto shared = invalid / "cockpit" / "shared";
    std::filesystem::create_directories(shared);
    const toca::AssetStore missing_source(invalid);
    toca::SessionModels other_source(updated.car, updated.cockpit, mega);
    bool missing_rejected = false;
    try {
        (void)other_source.car(missing_source, 2);
    } catch (const std::runtime_error&) {
        missing_rejected = true;
    }
    expect(missing_rejected, "A fresh scene must load from its supplied source instead of borrowing another session's cached car");
    std::filesystem::copy_file(mega / "cockpit/shared/wheel.glb", shared / "wheel.glb", std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream bad(shared / "wiper.glb", std::ios::binary);
        bad << "invalid GLB";
    }
    auto fallback = load_cockpit_resources(store, 0, invalid);
    expect(triangles(fallback.models[3]) == triangles(original.models[3]) && triangles(fallback.models[4]) > triangles(original.models[4]),
        "One invalid role must fall back without discarding the other valid Mega role");

    std::filesystem::remove(shared / "wheel.glb");
    auto retained = load_cockpit_resources(store, 1, invalid, fallback.shared_roles);
    expect(retained.shared_roles == fallback.shared_roles && triangles(retained.models[4]) > triangles(original.models[4])
               && retained.models[3].textures[0].bmp == original.models[3].textures[0].bmp && retained.models[0].textures[0].bmp == plain_audi.models[0].textures[0].bmp,
        "Another car must reuse the session's selected shared roles after file removal while retaining its own hands");
    const auto refreshed = load_cockpit_resources(store, 1, invalid);
    expect(refreshed.models[4].textures[0].bmp == original.models[4].textures[0].bmp, "A fresh role cache must observe the missing replacement and select the original wheel");
    const auto original_profile = load_cockpit_resources(store, 1, {}, fallback.shared_roles);
    expect(original_profile.models[4].textures[0].encoding == toca::ImageEncoding::bmp, "An original profile must not inherit a cached Mega shared role");
    const auto other_directory = load_cockpit_resources(store, 1, mega, fallback.shared_roles);
    expect(triangles(other_directory.models[3]) > triangles(original.models[3]), "A different replacement directory must resolve its own valid shared wiper");
    retained.models[4].nodes[0].transform[12] += 1;
    const auto independent = load_cockpit_resources(store, 2, invalid, fallback.shared_roles);
    expect(independent.models[4].nodes[0].transform == fallback.models[4].nodes[0].transform, "Changing a cockpit's model copy must not change another car's cached role template");
    for (const auto absent : {8u, 10u}) {
        const auto empty = load_cockpit_resources(store, absent, invalid, fallback.shared_roles);
        expect(empty.models[3].nodes.empty() && empty.models[4].nodes.empty(), "A warmed shared-role cache must not create absent interiors");
    }

    // Alter only this fixture's archive: source pivots and material payloads
    // remain part of role identity even when the replacement path is shared.
    const auto source_path = cockpit_archive(1);
    auto changed_bytes = store.read_file(source_path);
    const auto source_archive = store.open_archive(source_path);
    const auto role_offset = [&](unsigned role) {
        return 12 + source_archive.size() * 24 + source_archive.member(role).entry().payload_offset;
    };
    const auto u32 = [&](std::size_t offset) {
        return std::uint32_t(changed_bytes.at(offset)) | (std::uint32_t(changed_bytes.at(offset + 1)) << 8) | (std::uint32_t(changed_bytes.at(offset + 2)) << 16)
               | (std::uint32_t(changed_bytes.at(offset + 3)) << 24);
    };
    changed_bytes.at(role_offset(4) + 32 + 8 + 12 * 4) ^= 1;
    const auto texture = role_offset(3) + u32(role_offset(3) + 28);
    changed_bytes.at(texture + 16 + u32(texture + 12) - 1) ^= 1;
    std::filesystem::create_directories(invalid / "data");
    {
        std::ofstream changed(invalid / source_path, std::ios::binary);
        changed.write(reinterpret_cast<const char*>(changed_bytes.data()), std::streamsize(changed_bytes.size()));
        expect(bool(changed), "Cannot write altered cockpit source fixture");
    }
    const toca::AssetStore altered_source(invalid);
    const auto altered = load_cockpit_resources(altered_source, 1, invalid, fallback.shared_roles);
    expect(altered.models[4].nodes[0].transform[12] != original.models[4].nodes[0].transform[12] && triangles(altered.models[4]) == triangles(original.models[4])
               && altered.models[3].textures[0].bmp != original.models[3].textures[0].bmp,
        "Different source pivots and image bytes must retain their own fallback instead of inheriting a cached role");
    std::filesystem::copy_file(mega / "cockpit/shared/wheel.glb", shared / "wheel.glb", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(shared / "wheel.glb", shared / "wiper.glb", std::filesystem::copy_options::overwrite_existing);
    fallback = load_cockpit_resources(store, 0, invalid);
    expect(triangles(fallback.models[3]) == triangles(original.models[3]), "A valid GLB with another role's mesh binding must fall back");
    std::filesystem::remove(shared / "wiper.glb");
    fallback = load_cockpit_resources(store, 0, invalid);
    expect(fallback.models[3].textures[0].bmp == original.models[3].textures[0].bmp, "Missing role must retain the complete original model/material");
    const auto hands = invalid / "cockpit/honda";
    std::filesystem::create_directories(hands);
    std::filesystem::copy_file(mega / "cockpit/honda/right-hand.glb", hands / "right-hand.glb", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(hands / "right-hand.glb", hands / "left-hand.glb", std::filesystem::copy_options::overwrite_existing);
    fallback = load_cockpit_resources(store, 0, invalid);
    expect(triangles(fallback.models[0]) == triangles(original.models[0]) && triangles(fallback.models[1]) > triangles(original.models[1]),
        "A hand with wrong side bindings must fall back independently of the other valid hand");
    std::filesystem::remove(hands / "left-hand.glb");
    fallback = load_cockpit_resources(store, 0, invalid);
    expect(fallback.models[0].textures[0].bmp == original.models[0].textures[0].bmp, "Missing left hand must retain all original geometry and artwork");
    const auto dashboard = hands / "dashboard.glb";
    const auto expect_dashboard_fallback = [&] {
        const auto selected = load_cockpit_resources(store, 0, invalid);
        expect(triangles(selected.models[2]) == triangles(original.models[2]), "Bad dashboard must preserve all original mesh slots");
        for (std::size_t slot = 0; slot < original.models[2].textures.size(); ++slot) {
            expect(selected.models[2].textures[slot].bmp == original.models[2].textures[slot].bmp, "Dashboard fallback must preserve every source material");
        }
        expect(selected.models[1].textures[0].encoding == toca::ImageEncoding::webp, "Dashboard fallback must retain the other valid Mega roles");
    };
    expect_dashboard_fallback();
    {
        std::ofstream broken(dashboard, std::ios::binary);
        broken << "broken dashboard";
    }
    expect_dashboard_fallback();
    std::filesystem::copy_file(hands / "right-hand.glb", dashboard, std::filesystem::copy_options::overwrite_existing);
    expect_dashboard_fallback();
    std::filesystem::copy_file(mega / "cockpit/honda/dashboard.glb", dashboard, std::filesystem::copy_options::overwrite_existing);
    const auto recovered = load_cockpit_resources(store, 0, invalid);
    expect(recovered.models[2].textures[0].encoding == toca::ImageEncoding::webp, "A valid dashboard must recover independently after role fallback");
    std::cout << "PASS: Mega hands/shared roles, session model reuse, profile isolation, absent interiors and independent fallback\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Expected canonical data root and bundled Mega directory");
        }
        check(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
