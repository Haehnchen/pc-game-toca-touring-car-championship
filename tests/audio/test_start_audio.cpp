#include "assets/store.hpp"
#include "audio/start.hpp"

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
    double value = 0;
    for (const auto sample : pcm) {
        expect(std::isfinite(sample), "Start PCM must be finite");
        value += double(sample) * sample;
    }
    return value;
}

void synthetic() {
    using namespace toca::audio;
    const EngineInputs raw{8500, 0, 73};
    const auto grid = session_audio_inputs(raw, 0, 9);
    expect(grid.engine.rpm == 6800 && grid.engine.gear == 1 && grid.engine.throttle_percent == 73 && grid.speed_integer == 168 && grid.engine.speed_integer == 168,
        "State9 must apply the original audio-only RPM/gear/speed overrides");
    const auto idle_grid = session_audio_inputs({2000, 7, 0}, 0, 9);
    expect(idle_grid.engine.rpm == 2000 && idle_grid.engine.gear == 1 && idle_grid.speed_integer == 120 && idle_grid.engine.speed_integer == 120,
        "State9 idle must retain source2000RPM floor and synthetic audio speed120");
    const auto driving = session_audio_inputs(raw, 82, 6);
    const auto qualifying = session_audio_inputs(raw, 83, 4);
    expect(driving.engine.rpm == 8500 && driving.engine.gear == 0 && driving.speed_integer == 82 && qualifying.engine.rpm == 8500 && qualifying.speed_integer == 83
               && driving.engine.speed_integer == 82 && qualifying.engine.speed_integer == 83 && raw.rpm == 8500 && raw.gear == 0,
        "Session audio mapping must preserve raw HUD inputs and non-grid states");
    Service service({}, true);
    std::array<float, 4410> pcm;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .2f * float(std::sin(double(index) * .1));
    }
    service.load_pcm(1, pcm, 1, 44100);
    service.load_pcm(2, pcm, 1, 44100);
    Start start(service, {{1, 2}});
    start.update(StartCue::red);
    start.start();
    expect(start.played_count() == 0 && service.active_voices() == 0, "Loading/arming must not invent a start signal");
    start.update(StartCue::red);
    const auto first = energy(service.offline_mix(1024));
    double raw_energy = 0;
    for (std::size_t index = 0; index < 1024; ++index) {
        raw_energy += 2 * double(pcm[index]) * pcm[index];
    }
    expect(start.played_count() == 1 && start.last_sample() == 69 && std::abs(first - raw_energy * std::pow(10., -863. / 1000)) < 1e-6,
        "Original red sample69 must start at Miles master100/sample60, integer47 and -863 hundredths of a dB");
    start.update(StartCue::go);
    expect(start.played_count() == 2 && start.last_sample() == 70 && service.active_voices() == 1, "Original go sample must replace the same start handle");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Mute must silence the real start cue");
    service.apply({});
    service.offline_mix(10000);
    expect(service.active_voices() == 0, "Start signal must finish rather than loop");
    const auto foreign = service.play(1, 0);
    start.stop();
    expect(service.playing(foreign), "Start cleanup must not stop foreign slot reuse");
    service.stop(foreign);
    start.start();
    expect(start.played_count() == 2 && service.active_voices() == 0, "Focus resume must not replay an old start event");
    start.update(StartCue::go);
    start.stop();
    service.offline_mix(1024);
    expect(energy(service.offline_mix(1024)) == 0, "Scene exit must stop its own active signal");
}

void reference(const char* root) {
    using namespace toca::audio;
    Service service({}, true);
    const toca::AssetStore store(root);
    Start start(service, store, {});
    expect(service.active_voices() == 0, "Original signal loading must stay silent");
    start.start();
    for (const auto cue : {StartCue::red, StartCue::go}) {
        start.update(cue);
        expect(energy(service.offline_mix(8192)) > 0, "Canonical original start WAV did not reach mixer PCM");
    }
    expect(start.played_count() == 2 && start.last_sample() == 70, "Canonical signal ordering changed");
    start.stop();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-start-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original red/go sample order, finite PCM and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
