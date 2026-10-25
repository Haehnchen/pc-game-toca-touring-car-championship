#include "assets/store.hpp"
#include "audio/weather.hpp"
#include "core/random.hpp"

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
    for (const auto value : pcm) {
        expect(std::isfinite(value), "Thunder PCM must remain finite");
        result += double(value) * value;
    }
    return result;
}

void synthetic() {
    using namespace toca::audio;
    Service service({}, true);
    std::array<float, 4410> pcm;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .25f * float(std::sin(double(index) * .1));
    }
    service.load_pcm(1, pcm, 1, 44100);
    service.load_pcm(2, pcm, 1, 44100);
    Weather weather(service, {1, 2});
    toca::SourceRandom random(1), expected(1);
    weather.thunder(0xf5b, random);
    expect(weather.played_count() == 0 && service.active_voices() == 0, "Subthreshold thunder must remain silent");
    weather.thunder(0xf5c, random);
    expect(weather.last_sample() == 76 + expected.next() % 2 && random.next() == expected.next(), "Thunder must consume one shared source random draw only above threshold");
    const auto quiet_strength = energy(service.offline_mix(1024));
    expect(quiet_strength > 0, "Thunder must reach the real offline mixer");
    weather.stop();
    service.offline_mix(1024);
    weather.thunder(65536, random);
    expect(std::abs(energy(service.offline_mix(1024)) - quiet_strength) < 1e-5, "Original unshifted thunder strength must saturate both audible events to the same gain");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Mute must silence thunder through the service");
    service.apply({});
    service.offline_mix(10000);
    expect(service.active_voices() == 0, "Thunder must play once and finish");
    const auto foreign = service.play(1, 0);
    weather.stop();
    expect(service.playing(foreign), "Thunder cleanup must preserve a reused foreign voice slot");
    service.stop(foreign);
    weather.thunder(65536, random);
    weather.stop();
    service.offline_mix(1024);
    expect(energy(service.offline_mix(1024)) == 0 && weather.played_count() == 3, "Pause cleanup must stop without replaying thunder");
}

void reference(const char* root) {
    const toca::AssetStore store(root);
    for (const auto selected : {3u, 5u}) {
        toca::audio::Service service({}, true);
        toca::audio::Weather weather(service, store, {.weather = selected});
        toca::SourceRandom random(1);
        bool heard_first = false, heard_second = false;
        for (int event = 0; event < 12; ++event) {
            weather.thunder(65536, random);
            expect(energy(service.offline_mix(8192)) > 0, "Canonical thunder members must produce PCM");
            heard_first |= weather.last_sample() == 76;
            heard_second |= weather.last_sample() == 77;
            weather.stop();
        }
        expect(heard_first && heard_second, "Both original thunder variants must be heard");
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
            throw std::runtime_error("Usage: toca-weather-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original thunder threshold, RNG, saturated gain, finite muted PCM and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
