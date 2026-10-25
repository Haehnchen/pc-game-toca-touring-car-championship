#include "assets/store.hpp"
#include "audio/pause.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
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
    for (const auto value : pcm) {
        expect(std::isfinite(value), "Pause PCM must remain finite");
        result += double(value) * value;
    }
    return result;
}

void synthetic() {
    using namespace toca::audio;
    Service service({}, true);
    const std::vector<float> pcm(441, .25f);
    for (const auto sample : {1u, 2u, 3u}) {
        service.load_pcm(sample, pcm, 1, 44100);
    }
    Pause pause(service, {1, 2, 3});
    expect(pause.played_count() == 0 && service.active_voices() == 0, "Loading pause audio must remain silent");
    pause.play(78);
    const auto expected = 88 * 2 * std::pow(.25 * std::pow(10., -423. / 2000), 2);
    expect(std::abs(energy(service.offline_mix(88)) - expected) < .00001, "Race menu cue must use original volume100");
    service.apply({.effects_volume = 50});
    const auto half = 88 * 2 * std::pow(.25 * std::pow(10., -1025. / 2000), 2);
    expect(std::abs(energy(service.offline_mix(88)) - half) < .00001, "Live pause volume50 must recompute original integer master and dB gain");
    pause.play(79);
    pause.play(80);
    expect(service.active_voices() == 3 && pause.played_count() == 3, "Original menu cues must retain separate finite playback");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(88)) == 0, "Master mute must silence pause samples");
    service.apply({.effects_volume = 0});
    expect(energy(service.offline_mix(88)) == 0, "Effects volume must silence pause samples");
    service.offline_mix(1000);
    expect(service.active_voices() == 0, "Pause cues must finish once rather than loop");
    const auto foreign = service.play(1, 0);
    pause.stop();
    expect(service.playing(foreign), "Pause cleanup must preserve a reused foreign voice slot");
    pause.play(78);
    pause.play(80);
    pause.stop();
    expect(service.active_voices() == 1 && service.playing(foreign), "Scene reset must stop every owned cue without stopping other audio");
    service.stop(foreign);
    service.apply({.effects_volume = 50});
    const auto normalized = service.play(1, 0, .5f);
    expect(
        std::abs(energy(service.offline_mix(88)) - 88 * 2 * std::pow(.25 * .5 * .5, 2)) < .00001, "Reusing an original-volume slot must restore the generic linear gain contract");
    service.stop(normalized);
}

void reference(const char* root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    for (const auto weather : {2u, 3u, 5u}) {
        Service service({}, true);
        Pause pause(service, store, weather);
        for (const auto sample : {78u, 79u, 80u}) {
            pause.play(sample);
            expect(energy(service.offline_mix(44100)) > 0 && service.active_voices() == 0, "Original wet/dry pause cue must decode nonzero finite PCM");
        }
        expect(pause.played_count() == 3, "Original three pause samples must play");
    }
}

void dump(const char* root, const char* output) {
    const toca::AssetStore store(root);
    const auto destination = std::filesystem::canonical(output);
    const auto reference = std::filesystem::canonical(root);
    const auto relative = destination.lexically_relative(reference);
    expect(!relative.empty() && *relative.begin() == "..", "PCM dumps must remain outside canonical game data");
    for (const auto volume : {0u, 50u, 100u}) {
        for (const auto sample : {78u, 79u, 80u}) {
            toca::audio::Service service({.effects_volume = volume}, true);
            toca::audio::Pause pause(service, store, 2);
            pause.play(sample);
            const auto pcm = service.offline_mix(88200);
            expect(service.active_voices() == 0, "Captured pause sample must reach finite EOF within two seconds");
            const auto path = destination / ("native-" + std::to_string(sample) + "-" + std::to_string(volume) + ".f32");
            expect(!std::filesystem::exists(path), "Refusing to overwrite a PCM capture");
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size() * sizeof(float)));
            expect(bool(file), "Could not write native service PCM capture");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else if (argc == 4 && std::string_view(argv[1]) == "--dump") {
            dump(argv[2], argv[3]);
        } else {
            throw std::runtime_error("Usage: toca-pause-audio-tests [canonical-root | --dump canonical-root output-directory]");
        }
        std::cout << "PASS: original race pause samples, finite muted PCM, independent effects gain and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
