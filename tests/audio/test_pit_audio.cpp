#include "assets/store.hpp"
#include "audio/pit.hpp"

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
        expect(std::isfinite(value), "Pit PCM must remain finite");
        result += double(value) * value;
    }
    return result;
}

void synthetic() {
    using namespace toca::audio;
    Service service({}, true);
    std::array<float, 4410> pcm;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = .25f * float(std::sin(double(i) * .1));
    }
    service.load_pcm(1, pcm, 1, 44100);
    Pit pit(service, 1);
    expect(pit.played_count() == 0 && service.active_voices() == 0, "Loading the pit cue must remain silent");
    pit.enter();
    const auto actual = service.offline_mix(1024);
    expect(pit.played_count() == 1 && energy(actual) > 0, "Pit entry must reach the actual mixer");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Mute must silence the pit announcement");
    service.apply({});
    service.offline_mix(10000);
    expect(service.active_voices() == 0, "The pit announcement must finish once rather than loop");
    const auto foreign = service.play(1, 0);
    pit.stop();
    expect(service.playing(foreign), "Pit cleanup must preserve a reused foreign voice slot");
    service.stop(foreign);
    pit.enter();
    pit.stop();
    service.offline_mix(1024);
    expect(energy(service.offline_mix(1024)) == 0 && pit.played_count() == 2, "Focus loss must stop the current cue without replay");
}

void reference(const char* root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    for (const auto weather : {2u, 3u, 5u}) {
        Service service({}, true);
        Pit pit(service, store, {.weather = weather});
        pit.enter();
        expect(pit.played_count() == 1 && energy(service.offline_mix(8192)) > 0, "Original wet/dry member75 must produce the pit-entry PCM");
        pit.stop();
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
            throw std::runtime_error("Usage: toca-pit-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original pit-entry announcement, finite muted PCM and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
