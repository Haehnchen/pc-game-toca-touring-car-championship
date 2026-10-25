#include "assets/store.hpp"
#include "audio/horn.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (auto value : pcm) {
        expect(std::isfinite(value), "Horn PCM must remain finite");
        result += double(value) * value;
    }
    return result;
}

void load_tone(toca::audio::Service& service) {
    std::array<float, 4410> pcm;
    pcm.fill(.25f);
    service.load_pcm(1, pcm, 1, 44100);
}

double reference_energy(std::uint32_t volume) {
    toca::audio::Service service({}, true);
    load_tone(service);
    service.play_original_volume(1, 0, volume, 64);
    return energy(service.offline_mix(12800));
}

void synthetic() {
    using namespace toca::audio;
    for (const auto [view, volume] : std::array<std::array<std::uint32_t, 2>, 4>{{{0, 29}, {1, 59}, {2, 39}, {3, 29}}}) {
        const auto expected = reference_energy(volume);
        Service service({}, true);
        load_tone(service);
        Horn horn(service, {.camera_view = view}, 1);
        expect(!horn.playing() && horn.played_count() == 0 && service.active_voices() == 0, "Loading a horn must remain silent");
        horn.update(false);
        horn.update(true);
        horn.update(true);
        const auto actual = energy(service.offline_mix(12800));
        expect(horn.playing() && horn.played_count() == 1 && actual > 0 && std::abs(actual - expected) < expected * 1e-5,
            "Held horn must loop once at the original camera-specific integer volume and center pan");
        horn.set_camera(view == 1 ? 2 : 1);
        horn.update(true);
        expect(horn.played_count() == 1 && std::abs(energy(service.offline_mix(12800)) - expected) < expected * 1e-5,
            "Changing camera during a held horn must not restart it or change its original start gain");
        service.apply({.muted = true});
        expect(energy(service.offline_mix(1024)) == 0 && horn.playing(), "Mute must silence the actual looping voice");
        service.apply({});
        horn.update(false);
        expect(!horn.playing() && energy(service.offline_mix(1024)) == 0, "Releasing the action must stop the horn immediately");
        horn.update(true);
        expect(horn.played_count() == 2, "A fresh press must restart the horn");
        horn.stop();
        horn.update(true);
        expect(!horn.playing() && horn.played_count() == 2, "Focus/scene stop must not manufacture another held-key press");
        horn.update(false);
        horn.update(true);
        expect(horn.playing() && horn.played_count() == 3, "Release then press must restore the horn after stopping");
        // Another owner can recycle a voice after service-wide sample cleanup.
        service.clear_samples();
        load_tone(service);
        const auto foreign = service.play(1, 0);
        horn.stop();
        expect(service.playing(foreign), "Horn cleanup must preserve an unrelated reused voice");
        service.stop(foreign);
    }
    Service service({}, true);
    load_tone(service);
    Horn bonus(service, {.car_id = 8}, 1);
    bonus.update(true);
    expect(!bonus.playing() && bonus.played_count() == 0 && energy(service.offline_mix(1024)) == 0, "Native bonus-car8 uses406a40's special action, not the normal car horn");
}

void reference(const char* root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    for (const auto weather : {2u, 3u, 5u}) {
        const auto archive = store.open_archive(weather == 2 ? "data/sounddry.bfp" : "data/soundwet.bfp");
        const auto& name = archive.member(71).entry().raw_name;
        constexpr std::string_view expected = "CARHORN.WAV";
        expect(std::equal(expected.begin(), expected.end(), name.begin()), "Local player0 must use canonical SOUND member71 CARHORN.WAV");
        Service service({}, true);
        Horn horn(service, store, {.weather = weather});
        horn.update(true);
        expect(horn.playing() && horn.played_count() == 1 && energy(service.offline_mix(8192)) > 0, "Original wet/dry horn must reach the real offline mixer");
        service.offline_mix(100000);
        expect(horn.playing() && horn.played_count() == 1, "Original horn must continue beyond one sample while held");
        horn.update(false);
        expect(!horn.playing() && energy(service.offline_mix(1024)) == 0, "Canonical horn must stop on release");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-horn-tests [canonical-root]");
        }
        std::cout << "PASS: held original horn, camera gain, looping PCM and owned stop\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
