#include "assets/store.hpp"
#include "audio/rivals.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto sample : pcm) {
        expect(std::isfinite(sample), "Rival PCM must be finite");
        result += double(sample) * sample;
    }
    return result;
}

void exhausted_pool() {
    using namespace toca::audio;
    Service service({}, true);
    service.load_pcm(1, std::vector<float>(128, .2f), 1, 22050);
    std::array<EngineLayer, 8> layers;
    layers.fill({1, 22050, {2000, 8500, {11025, 22050, 33075}, {100, 100, 100}}});
    std::array<Voice, 12> foreign;
    for (auto& voice : foreign) {
        voice = service.play(1, 0);
        expect(voice != 0, "Pool fixture must allocate all twelve foreign loops");
    }
    Rivals motors(service, layers);
    const std::array<RivalEngineInput, 2> inputs{{{1, 0, {}, 2000}, {2, 1, {}, 5000}}};
    motors.update(inputs, {});
    expect(motors.playing_count() == 0 && service.active_voices() == 12, "A full shared pool must skip rival allocation without accessing Voice0 or stopping foreign loops");
    service.stop(foreign[0]);
    motors.update(inputs, {});
    expect(motors.playing_count() == 1 && service.active_voices() == 12, "One available slot must retain the first rival even when the second allocation fails");
    service.stop(foreign[1]);
    motors.update(inputs, {});
    expect(motors.playing_count() == 2 && service.active_voices() == 12, "A later available slot must recover the second motor while preserving the first");
    expect(energy(service.offline_mix(512)) > 0, "Recovered motors and retained loops must reach the real offline mixer");
    motors.stop();
    expect(service.active_voices() == 10, "Rival cleanup must preserve all ten remaining foreign loops");
    for (std::size_t index = 2; index < foreign.size(); ++index) {
        expect(service.playing(foreign[index]), "Pool pressure must not steal another owner's voice");
        service.stop(foreign[index]);
    }
}

void stable_selection() {
    using namespace toca::audio;
    Service actual({}, true), expected({}, true);
    std::array<EngineLayer, 8> layers;
    for (std::uint32_t car = 0; car < layers.size(); ++car) {
        std::array<float, 128> pcm;
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            pcm[index] = .2f * float(std::sin(double(index * (car + 1)) * 6.283185307179586 / pcm.size()));
        }
        actual.load_pcm(car, pcm, 1, 22050);
        expected.load_pcm(car, pcm, 1, 22050);
        layers[car] = {car, 22050, {2000, 8500, {11025, 22050, 33075}, {100, 100, 100}}};
    }
    Rivals full(actual, layers), pair(expected, layers);
    std::array<RivalEngineInput, 4> inputs{
        {{7, 1, {10 * 65536, 0, 0}, 2000}, {3, 2, {10 * 65536, 0, 0}, 4000}, {5, 3, {10 * 65536, 0, 0}, 6000}, {1, 4, {10 * 65536, 0, 0}, 8000}}};
    const auto compare = [&](std::size_t first, std::size_t second) {
        const std::array selected{inputs[first], inputs[second]};
        full.update(inputs, {});
        pair.update(selected, {});
        expect(actual.offline_mix(512) == expected.offline_mix(512), "Rival selection must retain input-order ties and the exact two loudest PCM voices");
    };
    compare(0, 1); // Equal volume retains input order, not numeric actor ID.
    inputs[2].position = {};
    compare(2, 0); // A louder late candidate displaces only the second tie.
    inputs[3].position = {};
    compare(2, 3);
    inputs[2].position[0] = inputs[3].position[0] = 41 * 65536;
    compare(0, 1); // Previously selected voices stop when out of range.
    full.update({}, {});
    expect(actual.active_voices() == 0, "Empty rival selection must release every previous voice");
}
} // namespace

int main(int argc, char** argv) {
    try {
        using namespace toca::audio;
        stable_selection();
        exhausted_pool();
        Service service({}, true);
        std::array<EngineLayer, 8> layers;
        std::array<float, 100> pcm;
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = .4f * float(std::sin(double(i) * 6.283185307179586 / 100));
        }
        for (std::uint32_t car = 0; car < 8; ++car) {
            service.load_pcm(car, pcm, 1, 22050);
            layers[car] = {car, 22050, {2000, 8500, {11025, 22050, 33075}, {100, 100, 100}}};
        }
        if (argc == 2) {
            const toca::AssetStore assets(argv[1]);
            layers = load_rival_engine_audio(service, assets, 2);
        }
        Rivals motors(service, layers);
        std::array<RivalEngineInput, 3> inputs{{{1, 0, {0, 0, 0}, 2000}, {2, 1, {65536, 0, 0}, 5000}, {3, 2, {2 * 65536, 0, 0}, 8500}}};
        motors.update(inputs, {});
        expect(motors.playing_count() == 2 && service.active_voices() == 2 && motors.maximum_volume() == 31,
            "Two loudest rivals must retain original distance0.8 and View2 mix1 truncations");
        expect(energy(service.offline_mix(4096)) > 0, "Rival engine voices must produce actual PCM");
        for (const auto view : {0u, 1u, 3u, 4u, 5u, 2u}) {
            motors.update(inputs, {}, view);
            expect(motors.maximum_volume()
                           == (view == 2                ? 31u
                               : view == 0 || view == 3 ? 27u
                                                        : 23u)
                       && service.active_voices() == 2,
                "Camera selection must update rival mix gains while retaining its two real voices");
        }
        service.apply({.muted = true});
        expect(energy(service.offline_mix(2048)) == 0, "Mute must silence rival voices");
        service.apply({});
        inputs[0].position[0] = 41 * 65536;
        inputs[1].position[0] = 42 * 65536;
        motors.update(inputs, {});
        expect(motors.playing_count() == 1 && service.active_voices() == 1, "Distant actors must release both old voices and admit the remaining candidate");
        motors.stop();
        expect(service.active_voices() == 0 && motors.playing_count() == 0, "Pause/finish must release opponent loops");
        motors.update(inputs, {});
        expect(motors.playing_count() == 1, "Resume must restart selected real opponent audio");
        motors.stop();
        std::cout << "PASS: source two-voice rival selection, distance/pitch/pan, PCM, mute, stop/resume\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
