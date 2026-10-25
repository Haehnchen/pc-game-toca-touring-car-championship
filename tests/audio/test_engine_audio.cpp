#include "assets/store.hpp"
#include "audio/driving.hpp"
#include "audio/engine.hpp"
#include "audio/impact.hpp"
#include "audio/scrape.hpp"
#include "audio/weather.hpp"
#include "core/random.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto value : pcm) {
        expect(std::isfinite(value), "Engine PCM must be finite");
        result += double(value) * value;
    }
    return result;
}

std::size_t crossings(const std::vector<float>& pcm) {
    std::size_t result = 0;
    for (std::size_t index = 2; index < pcm.size(); index += 2) {
        if (pcm[index - 2] < 0 && pcm[index] >= 0) {
            ++result;
        }
    }
    return result;
}

toca::audio::EngineData fixture(toca::audio::Service& service, bool motor_only = false, std::uint32_t sample_base = 256) {
    toca::audio::EngineData result;
    std::array<float, 100> pcm{};
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .25f * float(std::sin(double(index) * 6.283185307179586 / 100));
    }
    for (std::size_t index = 0; index < 3; ++index) {
        const auto sample = std::uint32_t(sample_base + index);
        service.load_pcm(sample, pcm, 1, 22050);
        const auto volume = std::uint8_t(motor_only && index != 0 ? 0 : 100);
        result.layers[index] = {sample, 22050, {2000, 8500, {11025, 22050, 33075}, {volume, volume, volume}}};
    }
    service.load_pcm(sample_base + 3, pcm, 1, 22050);
    result.whine = {sample_base + 3, 22050, {0, 193, {}, {}}};
    for (std::uint32_t index = 0; index <= 193; ++index) {
        result.whine.curve.rates.push_back(std::uint16_t(11025 + index * 100));
        result.whine.curve.gains.push_back(80);
    }
    result.shift_offsets = {30, 20, 10, 20, 50, 90, 100, 50, 10, 30, 60, 30, 10, 40, 70, 20, 5, 20, 50, 30, 20, 10, 5, 0};
    return result;
}

void synthetic() {
    using namespace toca::audio;
    expect(engine_sample_indices({}) == std::array<std::uint32_t, 3>{13, 14, 17}, "Camera2 Honda must select outside low/high/Honda");
    expect(engine_sample_indices({.car_id = 4, .camera_view = 0}) == std::array<std::uint32_t, 3>{1, 2, 4}, "Helicopter view must select Ford's original inside motor samples");
    constexpr std::array<std::uint32_t, 8> specific{17, 18, 22, 23, 16, 19, 20, 21};
    for (std::uint32_t car = 0; car < specific.size(); ++car) {
        expect(engine_sample_indices({.car_id = car})[2] == specific[car], "Normal car engine sample mapping changed");
    }
    for (const auto car : {8u, 9u}) {
        expect(engine_sample_indices({.car_id = car}) == std::array<std::uint32_t, 3>{13, 14, 16}
                   && engine_sample_indices({.car_id = car, .camera_view = 0}) == std::array<std::uint32_t, 3>{1, 2, 4},
            "Single Race bonus cars must retain source actor8's original Ford engine mapping");
    }
    const auto idle = engine_inputs(0, 65536, 0, 0);
    const auto running = engine_inputs(65536, 65536, 1, 2);
    expect(idle.rpm == 2000 && idle.throttle_percent == 0 && running.rpm == 8500 && running.throttle_percent == 100 && running.gear == 2,
        "Audio caller must preserve raw fixed RPM/throttle conversion and clamps");
    Service service({}, true);
    toca::SourceRandom random(7);
    Engine engine(service, {}, fixture(service));
    expect(service.active_voices() == 0, "Loading engine samples must not reserve voices");
    engine.update({2000, 0, 100}, random);
    expect(service.active_voices() == 0, "Stopped engine must not advance/play through an update");
    engine.start();
    engine.start();
    expect(engine.active() && engine.whine_active() && service.active_voices() == 4, "Three motor layers and WHINE must start once, before the separate WHIRR layer");
    expect(energy(service.offline_mix(2048)) == 0, "Engine loops must begin silent before source control update");
    const std::array<std::uint32_t, 5> specific_volumes{5, 0, 15, 20, 25};
    for (const auto volume : specific_volumes) {
        engine.update({2000, 0, 100}, random);
        const auto& controls = engine.last_controls();
        expect(controls[0].rate_hz == 11025 && controls[0].volume == 34 && controls[2].volume == volume,
            "Original fixed camera gains, throttle ramp or post-update shift counter changed");
    }
    expect(energy(service.offline_mix(44100)) > 0 && service.active_voices() == 4, "True SDL_mixer engine and WHINE loops must survive source EOF");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(2048)) == 0 && engine.active(), "Mute must silence PCM without losing engine voices");
    service.apply({});
    engine.update({9000, 0, 100}, random);
    expect(engine.last_controls()[0].volume == 0 && engine.last_controls()[0].rate_hz == 11025, "RPM outside a source curve must mute it at original fallback rate");
    service.offline_mix(4096); // Drain already prepared resampler samples after muting.
    expect(energy(service.offline_mix(4096)) == 0, "Out-of-range engine curves must produce silent PCM");
    engine.stop();
    expect(!engine.active() && service.active_voices() == 0, "Engine stop must release all motor and WHINE voice slots");
    engine.start();
    engine.update({2000, 0, 0}, random);
    expect(engine.last_controls()[2].volume == 22, "Restart must preserve source throttle state, then ramp down by10");
    engine.stop();
    service.clear_samples();
    Engine pitch(service, {}, fixture(service, true));
    pitch.start();
    pitch.update({2000, 0, 0}, random);
    service.offline_mix(2048);
    const auto low = service.offline_mix(8192);
    pitch.update({5250, 0, 0}, random);
    service.offline_mix(2048);
    const auto high = service.offline_mix(8192);
    const auto low_cross = crossings(low), high_cross = crossings(high);
    expect(energy(low) > 0 && energy(high) > 0 && low_cross > 10 && high_cross * 10 > low_cross * 18 && high_cross * 10 < low_cross * 22,
        "Absolute AIL rates must become originalHz/sourceWavHz in real PCM");
    pitch.stop();
    std::cout << "PASS: engine source mapping, throttle/gear/gain controls, silent start, loop/rate PCM, mute, stop and restart\n";
}

void whine_controls() {
    using namespace toca::audio;
    Service service({}, true);
    auto data = fixture(service);
    for (auto& layer : data.layers) {
        std::ranges::fill(layer.curve.gains, 0);
    }
    Engine engine(service, {.camera_view = 1}, data);
    toca::SourceRandom random(7519), expected(7519);
    engine.start();
    constexpr std::array<std::uint32_t, 10> rates{19025, 20025, 19025, 16025, 12025, 11025, 16025, 20025, 18025, 15025};
    constexpr std::array<std::uint32_t, 10> volumes{0, 0, 6, 4, 1, 2, 7, 19, 15, 6};
    expect(expected.next() == 24592, "WHINE fixture must select the original enabled shift-modulation branch");
    for (std::size_t index = 0; index < rates.size(); ++index) {
        engine.update({2000, 1, 100, 100}, random);
        expect(engine.whine_control().rate_hz == rates[index] && engine.whine_control().volume == volumes[index],
            "Original WHINE shift table, index rounding, throttle ramp or negative-gain fallback changed");
        expect(engine.shift_ticks() == (index == 0 ? 2u : index == 1 ? 1u : 0u), "Shift counter must update after motor controls and before WHIRR");
    }
    expect(expected.next() == 19099, "WHINE fixture must select the disabled modulation branch on its second upshift");
    engine.update({2000, 2, 100, 100}, random);
    expect(engine.whine_control().rate_hz == 21025 && engine.whine_control().volume == 7, "A low RNG draw must disable WHINE modulation while preserving shift attenuation");
    engine.update({2000, 1, 100, 100}, random);
    expect(engine.whine_control().rate_hz == 21025 && engine.whine_control().volume == 7, "Downshift must attenuate without consuming a modulation draw");
    expect(expected.next() == 25911, "WHINE fixture third upshift draw changed");
    engine.update({2000, 2, 100, 100}, random);
    expect(engine.whine_control().rate_hz == 19025 && engine.whine_control().volume == 0, "A later enabled upshift must restart at offset index1");
    expect(random.next() == expected.next(), "Only upward gear transitions may consume the shared source RNG");
    engine.update({2000, 5, 100, 193}, random);
    expect(engine.whine_control().rate_hz == 11025 && engine.whine_control().volume == 0, "WHINE maximum speed is excluded and must use the silent fallback rate");
    engine.update({2000, 5, 99, 100}, random);
    expect(engine.whine_control().volume == 0, "WHINE throttle must reset immediately unless the caller is exactly100");
    engine.update({2000, 5, 100, 100}, random);
    expect(engine.whine_control().rate_hz == 21025 && engine.whine_control().volume == 2, "Full throttle must resume WHINE in exact ten-point steps");
    engine.stop();
    engine.start();
    engine.update({2000, 5, 100, 100}, random);
    expect(engine.whine_control().volume == 5, "Voice restart must preserve WHINE throttle and gear state");
    engine.stop();
    for (const auto car : {8u, 9u}) {
        toca::SourceRandom bonus_random(7519), untouched(7519);
        Engine bonus(service, {.car_id = car}, data);
        bonus.start();
        for (const auto gear : {0, 1, 2, 1, 6, 7}) {
            bonus.update({2000, gear, 100, 100}, bonus_random);
        }
        expect(!bonus.whine_active() && service.active_voices() == 3 && bonus.shift_ticks() == 0 && bonus_random.next() == untouched.next(),
            "Both source bonus cars must bypass WHINE voices, shift state and shared RNG entirely");
        bonus.stop();
    }
}

void retained_whine_state() {
    using namespace toca::audio;
    Service service({}, true);
    const auto data = fixture(service);
    const SourceWhineCurve curve{data.whine.curve.minimum_rpm, data.whine.curve.maximum_rpm, data.whine.curve.rates, data.whine.curve.gains, data.shift_offsets};
    SourceEngineState state;
    toca::SourceRandom random(7519), expected(7519);
    auto values = source_engine_caller(65536, 65536, 1, 5, 82 * 65536, 9);
    expect(values.rpm == 6800 && values.speed == 168 && values.gear == 1 && values.throttle == 100,
        "44a6f0 must derive the grid's audio gear and speed independently of the physical fifth gear");
    const auto idle = source_engine_caller(65536, 65536, 1, 5, 82 * 65536, 18);
    expect(idle.rpm == 2000 && idle.speed == 0 && idle.gear == 0 && idle.throttle == 0, "44a6f0's non-driving caller states must provide idle audio inputs");
    SourceEngineCall call;
    call.values = {2000, 100, 1, 100};
    expect(prepare_source_engine_update(state, call) && state.throttle == 20 && state.throttle_mirror == 20,
        "414240 must ramp both retained car-throttle fields before reaching WHINE");
    expected.next();
    const auto first = advance_source_whine(state, call, curve, random);
    expect(first.reached && first.offset == 20 && first.index == 80 && first.rate == 19025 && first.rate_written && first.volume_written && state.previous_gear == 1
               && state.phase == 1 && state.shift == 2 && state.whine_throttle == 10 && random.state() == expected.state(),
        "A retained cold WHINE state must derive its real offset, curve command and shared RNG transition");
    call.enabled = false;
    expect(!prepare_source_engine_update(state, call) && state.shift == 2 && state.throttle == 20 && random.state() == expected.state(),
        "The original update gate must suppress the entire prefix without importing another state");
    call.enabled = true;
    call.local_players = 2;
    expect(!prepare_source_engine_update(state, call) && state.shift == 1 && state.throttle == 20,
        "The split-player return follows the shared shift decrement and precedes car-throttle and WHINE updates");
    call.local_players = 1;
    call.bonus = true;
    prepare_source_engine_update(state, call);
    const auto bonus = advance_source_whine(state, call, curve, random);
    expect(!bonus.reached && state.shift == 0 && state.throttle == 40 && state.phase == 1 && state.previous_gear == 1 && random.state() == expected.state(),
        "The bonus gate bypasses WHINE while preserving the ordinary caller's independent throttle prefix");
    call.bonus = false;
    call.playing = false;
    call.values.gear = 2;
    state.whine_volume = 23;
    prepare_source_engine_update(state, call);
    expected.next();
    const auto missing_voice = advance_source_whine(state, call, curve, random);
    expect(missing_voice.reached && !missing_voice.rate_written && !missing_voice.volume_written && state.whine_volume == 13 && state.phase == 26
               && random.state() == expected.state(),
        "An inactive WHINE handle still advances gear/RNG and decays the retained remembered volume without a playback command");
    call.playing = true;
    call.values.flag0 = 1;
    prepare_source_engine_update(state, call);
    const auto flags = advance_source_whine(state, call, curve, random);
    expect(flags.rate_written && !flags.volume_written && state.whine_volume == 13, "The source caller flags retain remembered volume while permitting the frequency command");
    const auto ramp = state.whine_throttle;
    call.values.gear = 0;
    prepare_source_engine_update(state, call);
    const auto neutral = advance_source_whine(state, call, curve, random);
    expect(neutral.rate == 11025 && neutral.volume == 0 && neutral.volume_written && state.whine_volume == 13 && state.whine_throttle == ramp && random.state() == expected.state(),
        "Neutral silences the handle while retaining the separate WHINE ramp and remembered volume without an upshift draw");
}

void tire_whine_phase_order() {
    using namespace toca::audio;
    Service service({}, true);
    Engine engine(service, {}, fixture(service));
    DrivingData driving_data;
    const std::array<float, 16> pcm{.25f, -.25f, .25f, -.25f, .25f, -.25f, .25f, -.25f, .25f, -.25f, .25f, -.25f, .25f, -.25f, .25f, -.25f};
    service.load_pcm(400, pcm, 1, 22050);
    driving_data.wind = {400, 22050, {0, 260, {11025, 22050}, {100, 100}}};
    for (std::uint32_t index = 0; index < driving_data.skid_samples.size(); ++index) {
        service.load_pcm(401 + index, pcm, 1, 22050);
        driving_data.skid_samples[index] = 401 + index;
    }
    ImpactData impact_data;
    for (const auto sample : {0x300u + 31, 0x300u + 32, 502u, 503u, 504u}) {
        service.load_pcm(sample, pcm, 1, 22050);
    }
    impact_data.samples[13] = 0x300 + 31;
    impact_data.samples[14] = 0x300 + 32;
    Impact impact(service, impact_data);
    Scrapes scrape(service, {502, 0xa666});
    Weather weather(service, {503, 504});
    Driving driving(service, {}, driving_data);
    engine.start();
    driving.start();
    toca::SourceRandom random(7519), expected(7519);
    DrivingInputs input;
    input.random = &random;
    input.speed_integer = 100;
    input.skid_queue.events[0] = 7;
    input.skid_queue.strengths[0] = 65536;
    const auto impact_sample = 31 + (expected.next() & 1);
    impact.start();
    impact.update({{}, 3, 65536}, {}, &random);
    expect(impact.last_sample() == impact_sample && energy(service.offline_mix(16)) > 0, "The source contact phase must play its selected damage variant through real PCM");
    scrape.start();
    scrape.update({}, 65536, {}, random);
    expected.next();
    expect(scrape.playing() && energy(service.offline_mix(16)) > 0, "Valid wall scrape must consume its discarded draw before the queued tires and produce real PCM");
    const auto skid_sample = 39 + expected.next() % 6;
    const auto thunder_sample = 76 + (expected.next() & 1);
    const auto modulation = expected.next();
    driving.update_contacts(input);
    driving.update_queued_skids(input);
    weather.thunder(65536, random);
    engine.update({2000, 1, 100, 100}, random);
    input.shift_ticks = engine.shift_ticks();
    driving.update_wind(input);
    const auto wind_gain = (80 * 0x4000) / 100;
    expect(driving.skid_sample() == skid_sample && weather.last_sample() == thunder_sample && weather.played_count() == 1
               && engine.source_state().phase == (modulation <= 0x5ffe ? 26u : 1u) && engine.shift_ticks() == 2
               && driving.wind_control().volume == std::uint32_t((100 * wind_gain) >> 16) && random.state() == expected.state() && energy(service.offline_mix(512)) > 0,
        "Real contact/scrape, queued tire, thunder and rising-gear WHINE must consume one stream in source order, followed by WHIRR's updated shift counter");
    driving.stop();
    engine.stop();
    impact.stop();
    scrape.stop();
    weather.stop();
}

void miles_pcm() {
    using namespace toca::audio;
    Service service({}, true);
    auto data = fixture(service);
    std::array<float, 256> constant;
    constant.fill(.25f);
    for (auto& layer : data.layers) {
        std::ranges::fill(layer.curve.gains, 0);
    }
    service.load_pcm(data.whine.sample, constant, 1, 22050);
    std::ranges::fill(data.whine.curve.rates, 22050);
    Engine engine(service, {.camera_view = 1}, data);
    toca::SourceRandom random(7);
    engine.start();
    expect(energy(service.offline_mix(512)) == 0, "WHINE first playback block must begin at original volume0");
    for (int tick = 0; tick < 10; ++tick) {
        engine.update({2000, 5, 100, 100}, random);
    }
    expect(engine.whine_control().volume == 27, "WHINE DC fixture source volume changed");
    const auto pcm_level = [&](int decibels) {
        service.offline_mix(2048);
        const auto observed = energy(service.offline_mix(512));
        // DLL observations: master100/sample27 ->21 ->-1563; master50 ->10 ->-2207.
        const auto expected_energy = 1024 * .25 * .25 * std::pow(10., double(decibels) / 1000);
        expect(std::abs(observed / expected_energy - 1) < .0001, "WHINE PCM must use integer master/sample combination and original DirectSound dB");
    };
    pcm_level(-1563);
    service.apply({.effects_volume = 50});
    pcm_level(-2207);
    service.apply({.effects_volume = 0});
    expect(energy(service.offline_mix(512)) == 0, "Original master zero must immediately silence WHINE");
    service.apply({});
    pcm_level(-1563);
    engine.stop();
}

void camera_switching() {
    using namespace toca::audio;
    Service service({}, true);
    const auto data = fixture(service);
    auto inside = fixture(service, false, 300);
    for (auto& layer : inside.layers) {
        std::ranges::fill(layer.curve.rates, 33075);
    }
    Engine engine(service, {}, data, inside);
    toca::SourceRandom random(7), baseline(7);
    Engine control(service, {}, data);
    engine.start();
    control.start();
    for (int tick = 0; tick < 6; ++tick) {
        engine.update({2000, 1, 100, 100}, random);
        control.update({2000, 1, 100, 100}, baseline);
    }
    control.stop();
    expect(!engine.set_camera(3), "Near/far chase must reuse the outside sample bank");
    engine.update({2000, 1, 100, 100}, random);
    expect(engine.last_controls()[0].volume == 29 && engine.last_controls()[2].volume == 19, "View3 must select original4d3ba8 mix2 rather than View2's mix1");
    for (const auto view : {1u, 4u, 5u, 0u, 2u, 1u, 3u}) {
        const bool changed = engine.set_camera(view);
        expect(changed == (view != 4 && view != 5 && view != 0), "Only inside/outside transitions may restart motor voices");
        expect(service.active_voices() == 4 && engine.shift_ticks() == 0, "Camera switch must retain gear state without leaking motor/WHINE voices");
        engine.update({2000, 1, 100, 100}, random);
        expect(engine.last_controls()[0].rate_hz == (view == 2 || view == 3 ? 11025u : 33075u), "Bank switching must replace the actual sample/curve data");
        const auto expected = view == 2 ? 25u : view == 3 ? 19u : 34u;
        expect(engine.last_controls()[2].volume == expected, "Camera switch must preserve smoothed throttle and select source mix gains");
        expect(energy(service.offline_mix(1024)) > 0, "Selected camera bank must reach the real offline mixer");
    }
    expect(random.next() == baseline.next(), "View changes must not consume additional shift randomness");
    engine.stop();
    engine.set_camera(4);
    expect(service.active_voices() == 0, "Changing a paused camera must not allocate playing voices");
}

void reference(const std::filesystem::path& root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    Service service({}, true);
    const auto data = load_engine_audio(service, store, {});
    expect(data.layers[0].source_rate == 22050 && data.layers[0].curve.rates.size() == 77 && data.layers[0].curve.rates.front() == 16141 && data.layers[1].curve.rates.size() == 81
               && data.layers[2].curve.rates.size() == 131,
        "Original sample rate or compiled engine curve dimensions changed");
    expect(data.whine.sample == 271 && data.whine.source_rate == 22050 && data.whine.curve.rates.size() == 194 && data.whine.curve.rates.front() == 11025
               && data.whine.curve.rates.back() == 25328 && data.whine.curve.gains.front() == 77 && data.whine.curve.gains.back() == 0,
        "Canonical outside WHINE member and curve changed");
    Engine engine(service, {}, data);
    toca::SourceRandom random(7);
    expect(service.active_voices() == 0, "Canonical loading must stay silent");
    engine.start();
    engine.update({2000, 0, 0}, random);
    expect(engine.last_controls()[0].rate_hz == 16141 && engine.last_controls()[1].volume == 0 && engine.last_controls()[2].volume == 0,
        "Canonical idle mapping must use low curve only");
    expect(energy(service.offline_mix(4410)) > 0, "Bundled Honda low engine Vorbis must decode and mix");
    engine.update({6500, 2, 100}, random);
    service.offline_mix(2048);
    expect(energy(service.offline_mix(4410)) > 0 && engine.last_controls()[0].volume == 0 && engine.last_controls()[1].volume > 0,
        "Original high RPM must cross over from low to high sample");
    engine.stop();
    expect(service.active_voices() == 0, "Canonical engine must release voices");
    std::cout << "PASS: bundled SOUND archive Vorbis and original compiled rate/gain tables through offline PCM\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            whine_controls();
            retained_whine_state();
            tire_whine_phase_order();
            miles_pcm();
            camera_switching();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-engine-audio-tests [CANONICAL_ROOT]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
