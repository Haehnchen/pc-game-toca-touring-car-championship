#include "assets/store.hpp"
#include "frontend/selection_resources.hpp"
#include "frontend/track_lighting.hpp"
#include "frontend/track_resources.hpp"
#include "image/decode.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void synthetic() {
    using toca::frontend::garage_variant;
    expect(garage_variant(6, true, true) == 1 && garage_variant(7, false, false) == 1 && garage_variant(8, true, false) == 2 && garage_variant(9, false, true) == 3
               && garage_variant(7, true, true) == 4,
        "Original replacement garage selection must follow independent bonus-car availability and include the adjacent Renault view");
    for (std::uint32_t unlocks = 0; unlocks < 4; ++unlocks) {
        for (std::uint32_t car = 0; car < 10; ++car) {
            constexpr std::array variants{1u, 2u, 3u, 4u};
            expect(garage_variant(car, (unlocks & 1) != 0, (unlocks & 2) != 0) == (car < 7 ? 1u : variants[unlocks]),
                "Every unlock combination must select its authored garage for Renault and both bonus cars, retaining the other seven garages");
        }
    }
    bool invalid_car = false;
    try {
        (void)garage_variant(10, false, false);
    } catch (const std::runtime_error&) {
        invalid_car = true;
    }
    expect(invalid_car, "Out-of-range garage car ID was accepted");
    // 480ab0's world-translation input passes through the light frame itself;
    // constructor positions alone omit that source draw-time affine consumer.
    const auto& lights = toca::frontend::track_preview_light_positions();
    constexpr std::array<toca::frontend::PreviewVector, 2> expected_lights{{{-14.23678f, 44.25479f, 37.76804f}, {.623144f, -2.429487f, -.998802f}}};
    for (std::size_t light = 0; light < lights.size(); ++light) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(std::abs(lights[light][axis] - expected_lights[light][axis]) < .003f,
                "Preview light update lost original local Y/Z/X rotation, Q16 units or separate affine outputs");
        }
    }
    constexpr float component = .577350269f;
    const toca::frontend::PreviewVector position{0, 12, 0}, back{component, component, -component}, front{-component, -component, component};
    const auto available_back = toca::frontend::track_preview_illumination(position, back, true);
    const auto locked_back = toca::frontend::track_preview_illumination(position, back, false);
    expect(std::abs(available_back - 128.f / 255.f) < .000001f && std::abs(locked_back - available_back * (float(0x3333) / float(0x20000))) < .000001f,
        "Back-facing preview surfaces must retain the declared ambient floor and original locked-light ratio");
    const auto available_front = toca::frontend::track_preview_illumination(position, front, true);
    const auto locked_front = toca::frontend::track_preview_illumination(position, front, false);
    expect(available_front == 1.f && locked_front > locked_back && locked_front < available_back,
        "Preview point lighting must saturate safely while keeping locked miniatures visibly dimmer");
    expect(std::abs(toca::frontend::track_preview_illumination({20000, 20000, 20000}, front, true) - available_back) < .000001f,
        "Out-of-range point lights must leave only the declared preview ambient");
    const auto root = std::filesystem::current_path() / "selection-resource-fixture";
    expect(std::filesystem::create_directory(root), "Selection fixture directory already exists");

    struct Cleanup {
        std::filesystem::path root;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    const toca::AssetStore store(root);
    toca::frontend::TrackInformation distance;
    distance.metres = 4020;
    expect(toca::frontend::track_length(distance, 0) == "2.51" && toca::frontend::track_length(distance, 2) == "4.02",
        "Track distance must use the original language-dependent integer conversion");
    bool invalid_track = false;
    try {
        (void)toca::frontend::load_track_preview(store, 10);
    } catch (const std::runtime_error&) {
        invalid_track = true;
    }
    expect(invalid_track, "Out-of-range track preview ID was accepted");
    std::cout << "PASS: bonus garage selection, bounded preview IDs and original track distance conversion\n";
}

void reference(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    const auto resources = toca::frontend::load_selection_resources();
    const auto cursor = toca::decode_bmp(store.open_archive("data/frontend.bfp").member(28).bytes());
    expect(cursor->w == 11 && cursor->h == 25 && cursor->format == SDL_PIXELFORMAT_INDEX8, "Original name cursor dimensions or format changed");
    expect(resources.cars[0].drive == 0 && resources.cars[0].valves == 16 && resources.cars[1].drive == 2, "Compiled Honda/Audi technical data changed");
    for (const auto& labels : resources.labels) {
        expect(!labels.title.empty() && !labels.name_prompt.empty() && !labels.manual.empty() && !labels.automatic.empty() && !labels.unknown.empty(),
            "Compiled selection localization is incomplete");
    }
    const auto tracks = toca::frontend::load_track_resources();
    expect(tracks.tracks[0].metres == 4020 && tracks.tracks[1].metres == 3624 && tracks.tracks[2].metres == 3790 && tracks.tracks[8].metres == 3140,
        "Original track ordering or distance records changed");
    expect(tracks.tracks[0].lap_time == "9:59.99" && tracks.tracks[0].lap_car == 0 && !tracks.tracks[0].lap_driver.empty(), "Original compiled lap defaults changed");
    for (const auto track : {0u, 1u, 2u, 8u}) {
        const auto model = toca::frontend::load_track_preview(store, track);
        expect(model.nodes.size() == 3 && model.meshes.size() == 2 && model.textures.size() == 2, "Original miniature resource structure changed");
        for (const auto& texture : model.textures) {
            const auto surface = toca::decode_bmp(texture.bmp);
            expect(surface->w > 0 && surface->h > 0, "Original miniature embedded texture is empty");
        }
    }
    for (const auto& labels : tracks.labels) {
        expect(!labels.title.empty() && !labels.help.empty() && !labels.locked_help.empty() && !labels.unit.empty(), "Compiled track localization is incomplete");
    }
    std::cout << "PASS: compiled selection data, name cursor and original track miniatures\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Expected optional canonical data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
