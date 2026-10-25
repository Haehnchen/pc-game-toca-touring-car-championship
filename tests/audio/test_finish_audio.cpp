#include "assets/store.hpp"
#include "audio/finish.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto sample : pcm) {
        expect(std::isfinite(sample), "Finish PCM must be finite");
        result += double(sample) * sample;
    }
    return result;
}

void synthetic() {
    using namespace toca::audio;
    Service service({}, true);
    std::array<float, 441> pcm;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .2f * float(std::sin(double(index) * .1));
    }
    service.load_pcm(1, pcm, 1, 44100);
    Finish finish(service, {1, 127});
    expect(!finish.playing() && service.active_voices() == 0, "Finish loading must stay silent");
    finish.start();
    finish.start();
    expect(finish.played_count() == 1 && service.active_voices() == 1, "Repeated finish activation must retain one owned loop");
    service.offline_mix(4410);
    expect(energy(service.offline_mix(1024)) > 0 && finish.playing(), "Finish sample must loop beyond its source duration");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Mute must silence actual finish-loop PCM");
    service.apply({});
    finish.stop();
    service.offline_mix(1024);
    expect(!finish.playing() && energy(service.offline_mix(1024)) == 0, "Focus/exit must stop the owned finish loop");
    finish.start();
    expect(finish.played_count() == 2 && energy(service.offline_mix(1024)) > 0, "Active finish-state resume must restore the loop");
    // A cleared/reused voice slot belongs to its new playback, not Finish.
    service.clear_samples();
    service.load_pcm(1, pcm, 1, 44100);
    const auto foreign = service.play(1, 0);
    finish.stop();
    expect(service.playing(foreign), "Finish cleanup must preserve foreign slot reuse");
    service.stop(foreign);
    finish.start();
    finish.fade();
    for (int tick = 0; tick < 15; ++tick) {
        finish.update();
    }
    expect(finish.volume() == 67 && finish.playing(), "Race-entry fade must follow original four-unit decrements");
    finish.set_active(false);
    finish.update();
    expect(finish.volume() == 67 && !finish.playing(), "Focus pause must stop PCM and preserve fade progress");
    finish.set_active(true);
    expect(finish.volume() == 67 && finish.playing(), "Focus resume must retain the current fade volume");
    for (int tick = 0; tick < 17; ++tick) {
        finish.update();
    }
    expect(finish.volume() == 0 && !finish.playing(), "Original fade completion must release its owned loop");
    {
        Finish ending(service, {1, 127});
        ending.start();
    }
    expect(service.active_voices() == 0, "Finish destruction must release its loop");
}

void original_volume_fade() {
    using namespace toca::audio;
    Service service({}, true);
    service.load_pcm(1, std::vector<float>(256, .25f), 1, 44100);
    Finish finish(service, {1, 127});
    finish.start();
    const auto expected = [](int hundredth_db) {
        return 128 * .25 * .25 * std::pow(10., double(hundredth_db) / 1000);
    };
    expect(std::abs(energy(service.offline_mix(64)) - expected(-207)) < 1e-6, "Finish initial volume127/master100 must use the original -207 dB hundredths");
    service.apply({.effects_volume = 50});
    finish.fade();
    finish.update();
    expect(finish.volume() == 123 && std::abs(energy(service.offline_mix(64)) - expected(-845)) < 1e-6, "Finish fade update must retain Miles integer master scaling");
    for (int tick = 0; tick < 14; ++tick) {
        finish.update();
    }
    expect(finish.volume() == 67 && std::abs(energy(service.offline_mix(64)) - expected(-1377)) < 1e-6, "Finish fade must use the recovered curve at lower volumes");
    finish.set_active(false);
    finish.set_active(true);
    expect(std::abs(energy(service.offline_mix(64)) - expected(-1377)) < 1e-6, "Finish resume must restore the same integer volume curve");
}

void reference(const char* root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    for (const auto weather : {2u, 3u}) {
        Service service({}, true);
        const auto data = load_finish_audio(service, store, {.weather = weather});
        expect(data.volume == 127, "Canonical finish-loop gain changed");
        Finish finish(service, data);
        finish.start();
        expect(finish.playing() && energy(service.offline_mix(8192)) > 0, "Canonical sample83 did not reach actual mixer PCM");
        finish.stop();
        expect(service.active_voices() == 0, "Canonical finish-loop exit retained a voice");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            original_volume_fade();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-finish-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original finish-loop PCM, focus lifecycle and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
