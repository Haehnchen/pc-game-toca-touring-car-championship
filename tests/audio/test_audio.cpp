#include "assets/store.hpp"
#include "audio/impact.hpp"
#include "audio/pause.hpp"
#include "audio/sample_file.hpp"
#include "audio/service.hpp"
#include "audio/voice_pool.hpp"
#include "audio/weather.hpp"
#include "core/random.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

double energy(const std::vector<float>& pcm, std::size_t channel) {
    double sum = 0;
    for (std::size_t i = channel; i < pcm.size(); i += 2) {
        sum += double(pcm[i]) * pcm[i];
    }
    return sum;
}

std::size_t nonzero_frames(const std::vector<float>& pcm) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        count += std::abs(pcm[i]) > .0001f || std::abs(pcm[i + 1]) > .0001f;
    }
    return count;
}

void pool_failure_recovery() {
    using namespace toca::audio::detail;
    // Fail before track creation and after ownership, as during callback setup.
    // Exercise both initial construction and the single-to-split expansion.
    for (const auto initial : {0u, 12u}) {
        for (const auto failed_attempt : {1u, 6u, 12u}) {
            for (const bool after_creation : {false, true}) {
                std::array<unsigned, 48> destroyed{};
                unsigned created = 0, attempts = 0, fail_at = 0;

                struct Release {
                    std::array<unsigned, 48>* destroyed;

                    void operator()(unsigned* track) const noexcept {
                        ++(*destroyed)[*track];
                        delete track;
                    }
                };

                using Track = std::unique_ptr<unsigned, Release>;
                std::vector<VoiceSlot<Track>> pool;
                const auto create = [&]() -> Track {
                    const bool fail = ++attempts == fail_at;
                    if (fail && !after_creation) {
                        throw std::runtime_error("Injected track creation failure");
                    }
                    Track track(new unsigned(created++), Release{&destroyed});
                    if (fail) {
                        throw std::runtime_error("Injected track initialization failure");
                    }
                    return track;
                };
                resize_voice_pool(pool, initial, create);
                std::array<unsigned*, 12> retained{};
                for (std::size_t index = 0; index < pool.size(); ++index) {
                    auto& slot = pool[index];
                    retained[index] = slot.track.get();
                    slot.serial = index + 101;
                    slot.gain = .5f;
                    slot.stereo = {.25f, .75f};
                    slot.original_volume = index % 2 ? std::optional<std::uint32_t>(63) : std::nullopt;
                }
                const auto check_retained = [&] {
                    for (std::size_t index = 0; index < initial; ++index) {
                        const auto& slot = pool[index];
                        expect(slot.track.get() == retained[index] && slot.serial == index + 101 && slot.gain == .5f && slot.stereo.left == .25f && slot.stereo.right == .75f
                                   && slot.original_volume == (index % 2 ? std::optional<std::uint32_t>(63) : std::nullopt),
                            "Pool resize changed retained track ownership or playback controls");
                    }
                };
                attempts = 0;
                fail_at = failed_attempt;
                bool failed = false;
                try {
                    resize_voice_pool(pool, initial + 12, create);
                } catch (const std::runtime_error&) {
                    failed = true;
                }
                expect(failed && attempts == failed_attempt && pool.size() == initial, "Failed audio expansion exposed a partially initialized pool");
                check_retained();
                for (unsigned index = 0; index < created; ++index) {
                    expect(destroyed[index] == unsigned(index >= initial), "Failed audio expansion must release only staged tracks exactly once");
                }
                fail_at = 0;
                resize_voice_pool(pool, initial + 12, create);
                expect(pool.size() == initial + 12, "Audio expansion must succeed after a failed attempt");
                check_retained();
                for (std::size_t index = initial; index < pool.size(); ++index) {
                    const auto& slot = pool[index];
                    expect(slot.track && slot.serial == 0 && slot.gain == 0 && slot.stereo.left == 1 && slot.stereo.right == 1 && !slot.original_volume,
                        "New voice slots must begin with independent default playback state");
                }
                attempts = 0;
                resize_voice_pool(pool, initial, create);
                resize_voice_pool(pool, initial, create);
                expect(pool.size() == initial && attempts == 0, "Shrinking or retaining a pool must never allocate a track");
                check_retained();
                pool.clear();
                for (unsigned index = 0; index < created; ++index) {
                    expect(destroyed[index] == 1, "Voice tracks must be released exactly once after retry and shrink");
                }
            }
        }
    }
    std::cout << "PASS: atomic voice pool construction/growth, failure cleanup, retained state, retry and shrink\n";
}

void events_loops_and_pool() {
    toca::audio::Service service({}, true);
    service.consume({1, 0, toca::GameEventKind::startup, 0});
    expect(energy(service.offline_mix(441), 0) == 0, "Startup invented an audio cue");
    const std::vector<float> tone(441, .25f);
    service.load_pcm(7, tone, 1, 44100);
    service.consume({2, 1, toca::GameEventKind::play_sound, 7});
    const auto single = service.offline_mix(882);
    expect(nonzero_frames(single) == 441 && service.active_voices() == 0, "Single play duration or EOF changed");
    const auto voice = service.play(7, 3);
    const auto triple = service.offline_mix(1764);
    expect(nonzero_frames(triple) == 1323 && !service.playing(voice), "Total-play count was not translated to repeats");
    for (std::uint32_t i = 1; i <= 12; ++i) {
        expect(service.play(7, 0) == i, "Shared voice pool did not select first free slot");
    }
    expect(service.active_voices() == 12 && service.play(7) == 0, "Voice exhaustion did not preserve existing tracks");
    service.stop(4);
    expect(service.play(7, 0) == 4, "Released voice was not reused");
    service.offline_mix(2000);
    expect(service.active_voices() == 12, "Infinite loop completed unexpectedly");
    service.consume({3, 2, toca::GameEventKind::focus_changed, 0});
    service.consume({4, 3, toca::GameEventKind::shutdown, 0});
    expect(!service.active() && service.active_voices() == 0 && service.consumed_events() == 4, "Ordered shutdown did not stop the shared pool");
    std::cout << "PASS: silent startup, ordered sample event, exact finite loops, infinite loops, 12-voice exhaustion/reuse\n";
}

void split_pool() {
    toca::AudioSettings settings;
    settings.stereo_effects = false;
    toca::audio::Service service(settings, true);
    service.set_local_players(2);
    std::vector<float> stereo(4410);
    for (std::size_t sample = 0; sample < stereo.size(); sample += 2) {
        stereo[sample] = .01f;
        stereo[sample + 1] = -.005f;
    }
    service.load_pcm(1, stereo, 2, 44100);
    for (unsigned voice = 1; voice <= 24; ++voice) {
        expect(service.play(1, 0) == voice, "Two players must have room for both engine and driving banks");
    }
    const auto mixed = service.offline_mix(128);
    expect(service.active_voices() == 24 && energy(mixed, 0) > 0, "Both local voice banks must reach the real offline mixer");
    bool resize_rejected = false;
    try {
        service.set_local_players(1);
    } catch (const std::runtime_error&) {
        resize_rejected = true;
    }
    expect(resize_rejected && service.active_voices() == 24, "Pool resize must reject active sample voices without stopping them");
    for (std::size_t sample = 0; sample < mixed.size(); sample += 2) {
        expect(std::abs(mixed[sample] - mixed[sample + 1]) < .000001f, "New local voices must retain the saved mono setting");
    }
    for (unsigned voice = 1; voice <= 24; ++voice) {
        service.stop(voice);
    }
    expect(energy(service.offline_mix(128), 0) == 0, "Stopping a scene must silence both local players");
    // Reserve the first player's bank; finite effects must track and release
    // higher voice IDs without stopping unrelated engine loops.
    for (unsigned voice = 1; voice <= 12; ++voice) {
        expect(service.play(1, 0) == voice, "Reserve the first local audio bank");
    }
    toca::SourceRandom random(1);
    toca::audio::ImpactData impacts;
    impacts.samples.fill(1);
    toca::audio::Impact impact(service, impacts);
    toca::audio::Weather weather(service, {1, 1});
    toca::audio::Pause pause(service, {1, 1, 1});
    impact.start();
    impact.update({{}, 1, 0xe666, true}, {});
    weather.thunder(65536, random);
    pause.play(78);
    expect(service.active_voices() == 15 && service.playing(15), "All finite consumers must support the second player's voice IDs");
    impact.stop();
    weather.stop();
    pause.stop();
    expect(service.active_voices() == 12, "Stopping finite cues must preserve both players' unrelated engine voices");
    std::array<std::uint64_t, 24> serials{};
    for (unsigned voice = 1; voice <= serials.size(); ++voice) {
        serials[voice - 1] = service.playback_serial(voice);
    }
    service.clear_samples();
    service.set_local_players(1);
    expect(service.active_voices() == 0, "Leaving split screen must release every local voice");
    for (unsigned voice = 1; voice <= 12; ++voice) {
        expect(service.playback_serial(voice) == serials[voice - 1], "Shrinking must preserve retained playback serials");
    }
    bool removed = false;
    try {
        (void)service.playback_serial(13);
    } catch (const std::runtime_error&) {
        removed = true;
    }
    expect(removed, "Shrinking must invalidate the removed voice bank");
    settings.stereo_effects = true;
    settings.effects_volume = 40;
    service.apply(settings);
    service.set_local_players(2);
    service.load_pcm(1, stereo, 2, 44100);
    const auto previous_serial = *std::ranges::max_element(serials);
    for (unsigned voice = 1; voice <= 24; ++voice) {
        expect(service.playback_serial(voice) == (voice <= 12 ? serials[voice - 1] : 0), "Regrowth must retain old serials and initialize only new slots");
        // Isolate the newly created bank in the mixed PCM.
        expect(service.play_pan(1, 0, voice <= 12 ? 0.f : .5f, 127) == voice, "Regrown pool must preserve first-free voice IDs");
        expect(service.playback_serial(voice) == previous_serial + voice, "Playback serials must remain monotonic across pool replacement");
    }
    const auto pcm_is = [&](float left, float right) {
        const auto pcm = service.offline_mix(128);
        for (std::size_t sample = 0; sample < pcm.size(); sample += 2) {
            expect(std::abs(pcm[sample] - left) < .000001f && std::abs(pcm[sample + 1] - right) < .000001f, "Recreated voices must retain live gain and mono/stereo controls");
        }
    };
    pcm_is(0, -.012f);
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(.006f, .006f);
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(0, -.012f);
}

void live_controls() {
    toca::audio::Service service({}, true);
    std::vector<float> tone(44100);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        tone[i] = .25f * std::sin(float(i) * 2.f * 3.14159265f * 441.f / 44100.f);
    }
    service.load_pcm(1, tone, 1, 44100);
    const auto voice = service.play(1, 0);
    service.controls(voice, 1, 1, 1, 0);
    const auto left = service.offline_mix(4410);
    service.controls(voice, .5f, 2, 0, 1);
    const auto right = service.offline_mix(4410);
    expect(energy(left, 0) > 1 && energy(left, 1) < .000001, "Left pan endpoint failed");
    expect(energy(right, 1) > .1 && energy(right, 0) < .000001, "Live right pan endpoint failed");
    const auto ratio = energy(right, 1) / energy(left, 0);
    expect(ratio > .23 && ratio < .27, "Live gain did not halve PCM amplitude");
    auto crossings = [](const std::vector<float>& pcm, std::size_t channel) {
        int count = 0;
        for (std::size_t i = channel + 2; i < pcm.size(); i += 2) {
            count += pcm[i - 2] <= 0 && pcm[i] > 0;
        }
        return count;
    };
    expect(crossings(right, 1) >= 2 * crossings(left, 0) - 2 && crossings(right, 1) <= 2 * crossings(left, 0) + 2, "Live frequency ratio did not double tone frequency");
    service.apply({1, true});
    expect(energy(service.offline_mix(441), 1) == 0, "Mute did not pass through mixer gain");
    service.apply({.5f, false});
    expect(energy(service.offline_mix(441), 1) > 0, "Unmute did not restore the active mixer path");
    std::cout << "PASS: live frequency, stereo endpoints, gain, mute/unmute through SDL_mixer PCM\n";
}

void original_volume_curve() {
    // Canonical mss32 machine-code2000de00/de70/166b8 output at master127.
    // The standalone audit also executes the actual sample/master setters and
    // verifies every pair against these effective-volume entries.
    constexpr std::array<std::int32_t, 128> decibels{-10000, -4207, -3605, -3253, -3003, -2809, -2651, -2517, -2401, -2299, -2207, -2124, -2049, -1979, -1915, -1855, -1799, -1746,
        -1697, -1650, -1605, -1563, -1522, -1484, -1447, -1411, -1377, -1344, -1313, -1282, -1253, -1224, -1197, -1170, -1144, -1119, -1095, -1071, -1048, -1025, -1003, -982, -961,
        -940, -920, -901, -882, -863, -845, -827, -809, -792, -775, -759, -742, -726, -711, -695, -680, -665, -651, -636, -622, -608, -595, -581, -568, -555, -542, -529, -517,
        -505, -492, -480, -469, -457, -445, -434, -423, -412, -401, -390, -379, -369, -359, -348, -338, -328, -318, -308, -299, -289, -280, -270, -261, -252, -243, -234, -225,
        -216, -207, -198, -190, -181, -173, -165, -156, -148, -140, -132, -124, -116, -109, -101, -93, -86, -78, -71, -63, -56, -49, -42, -34, -27, -20, -13, -6, 0};
    const auto expected_gain = [&](std::uint32_t sample, std::uint32_t master) {
        const auto effective = sample * master / 127;
        return effective == 0 ? 0.f : float(std::pow(10., double(decibels[effective]) / 2000));
    };
    toca::audio::Service service({}, true);
    toca::AudioSettings settings;
    service.load_pcm(1, std::vector<float>(1024, .25f), 1, 44100);
    const auto pcm_is = [&](float left, float right, const char* message, bool exact = false) {
        const auto pcm = service.offline_mix(16);
        for (std::size_t i = 0; i < pcm.size(); i += 2) {
            if (exact) {
                expect(
                    std::bit_cast<std::uint32_t>(pcm[i]) == std::bit_cast<std::uint32_t>(left) && std::bit_cast<std::uint32_t>(pcm[i + 1]) == std::bit_cast<std::uint32_t>(right),
                    message);
            } else {
                expect(std::abs(pcm[i] - left) < .0000002f && std::abs(pcm[i + 1] - right) < .0000002f, message);
            }
        }
    };
    auto voice = service.play_original_volume(1, 0, 127, 0);
    pcm_is(.25f * expected_gain(127, 100), 0, "Original master/sample volume and pan must reach the first PCM block");
    // A UI maximum of100 stays100, rather than scaling up to Miles127.
    for (std::uint32_t master = 0; master <= 100; ++master) {
        settings.effects_volume = master;
        service.apply(settings);
        for (std::uint32_t sample = 0; sample <= 127; ++sample) {
            service.controls_original_volume(voice, sample, 1);
            const auto amplitude = .25f * expected_gain(sample, master);
            pcm_is(amplitude, amplitude, "Original integer sample/master PCM bits disagree with canonical Miles output", true);
        }
    }
    service.controls_original_volume(voice, 101, 1, 32);
    const auto far_gain = float(std::pow(10., -588. / 2000));
    auto amplitude = .25f * expected_gain(101, 100);
    pcm_is(amplitude, amplitude * far_gain, "Original sample attenuation must compose with relative DirectSound pan");
    settings.effects_volume = 37;
    service.apply(settings);
    amplitude = .25f * expected_gain(101, 37);
    pcm_is(amplitude, amplitude * far_gain, "Live effects settings must recalculate the retained integer sample volume");
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(amplitude, amplitude, "Mono must center original-volume voices without changing their loudness");
    service.controls_original_volume(voice, 53, 1, 127);
    amplitude = .25f * expected_gain(53, 37);
    pcm_is(amplitude, amplitude, "Original controls in Mono must retain integer volume and center the current pan");
    settings.stereo_effects = true;
    settings.master_gain = .5f;
    service.apply(settings);
    pcm_is(0, amplitude * .5f, "Stereo and application master must compose with the latest original-volume controls");
    settings.muted = true;
    service.apply(settings);
    pcm_is(0, 0, "Application mute must silence original-volume voices");
    settings.muted = false;
    settings.master_gain = 1;
    service.apply(settings);
    pcm_is(0, amplitude, "Unmute must retain original sample volume and pan");
    service.stop(voice);
    const auto reused = service.play(1, 0, .5f);
    expect(reused == voice, "Volume probe must reuse the original-volume voice");
    pcm_is(.125f * .37f, .125f * .37f, "Raw voice reuse must clear retained original volume and pan");
    service.controls_original_volume(reused, 100, 1, 0);
    service.controls(reused, .5f, 1, 0, 1);
    settings.effects_volume = 60;
    service.apply(settings);
    pcm_is(0, .075f, "Raw controls must replace the previous integer volume on subsequent settings changes");
    service.stop(reused);
    voice = service.play_original_volume(1, 0, 17, 127);
    pcm_is(0, .25f * expected_gain(17, 60), "Original-volume reuse must reset raw gain and apply the new pan before playback");
    service.stop(voice);
    // A nonconstant tone makes frequency controls observable as well as gain.
    std::vector<float> tone(44100);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        tone[i] = .25f * std::sin(float(i) * 2.f * 3.14159265f * 441.f / 44100.f);
    }
    service.load_pcm(2, tone, 1, 44100);
    voice = service.play_original_volume(2, 0, 127);
    const auto slow = service.offline_mix(4410);
    service.controls_original_volume(voice, 127, 2);
    const auto fast = service.offline_mix(4410);
    const auto crossings = [](const std::vector<float>& pcm) {
        int count = 0;
        for (std::size_t i = 2; i < pcm.size(); i += 2) {
            count += pcm[i - 2] <= 0 && pcm[i] > 0;
        }
        return count;
    };
    expect(std::abs(crossings(fast) - 2 * crossings(slow)) <= 2, "Original-volume controls must update playback frequency");
    std::cout << "PASS: all12928 integer Miles volumes, first PCM, live controls/settings, relative pan, mono/stereo, mute, reuse and frequency\n";
}

void stereo_and_mono_effects() {
    toca::audio::Service service({}, true);
    toca::AudioSettings settings;
    const auto pcm_is = [&](float left, float right, const char* message) {
        const auto pcm = service.offline_mix(128);
        for (std::size_t index = 0; index < pcm.size(); index += 2) {
            expect(std::abs(pcm[index] - left) < .00001f && std::abs(pcm[index + 1] - right) < .00001f, message);
        }
    };
    service.load_pcm(1, std::vector<float>(1024, .25f), 1, 44100);
    auto voice = service.play(1, 0);
    service.controls(voice, 1, 1, 1, 0);
    pcm_is(.25f, 0, "Stereo must preserve directional effect panning");
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(.25f, .25f, "Live mono must center an active directional effect");
    service.controls(voice, .5f, 1, 0, 1);
    pcm_is(.125f, .125f, "Controls in mono must update gain while retaining centered output");
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(0, .125f, "Returning to stereo must restore the latest requested pan");
    service.stop(voice);
    settings.stereo_effects = false;
    service.apply(settings);
    const auto reused = service.play(1, 0);
    expect(reused == voice, "Mono probe must reuse the released voice");
    pcm_is(.25f, .25f, "Reused mono voices must reset gain and pan");
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(.25f, .25f, "A reused voice must not restore an earlier playback's pan");
    service.stop(reused);

    voice = service.play_pan(1, 0, 1, 0);
    pcm_is(.25f, 0, "Initial source pan must apply to the first PCM block");
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(.25f, .25f, "Mono must center a voice started with original pan");
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(.25f, 0, "Stereo must restore a voice's initial original pan");
    service.stop(voice);
    settings.stereo_effects = false;
    service.apply(settings);
    voice = service.play_pan(1, 0, 1, 127);
    pcm_is(.25f, .25f, "A panned sound started under Mono must begin centered");
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(0, .25f, "Stereo must restore the requested pan of a sound started under Mono");
    service.stop(voice);

    std::vector<float> stereo(2048);
    for (std::size_t index = 0; index < stereo.size(); index += 2) {
        stereo[index] = .25f;
        stereo[index + 1] = -.125f;
    }
    service.load_pcm(2, stereo, 2, 44100);
    voice = service.play(2, 0);
    pcm_is(.25f, -.125f, "Stereo source channels must remain distinct in stereo mode");
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(.0625f, .0625f, "Mono output must downmix both source channels before mixing");
    settings.stereo_effects = true;
    service.apply(settings);
    pcm_is(.25f, -.125f, "Live stereo restore must preserve the original source channels");
    service.stop(voice);

    // A synthetic stereo WAV exercises the separate deferred music decoder.
    constexpr std::uint32_t frames = 1024;
    std::vector<std::uint8_t> wave(44 + frames * 4);
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        wave[offset] = std::uint8_t(value);
        wave[offset + 1] = std::uint8_t(value >> 8);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        put16(offset, std::uint16_t(value));
        put16(offset + 2, std::uint16_t(value >> 16));
    };
    std::memcpy(wave.data(), "RIFF", 4);
    put32(4, std::uint32_t(wave.size() - 8));
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, 44100);
    put32(28, 176400);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(wave.data() + 36, "data", 4);
    put32(40, frames * 4);
    for (std::size_t index = 0; index < frames; ++index) {
        put16(44 + index * 4, 8192);
        put16(46 + index * 4, std::uint16_t(-4096));
    }
    const auto path = std::filesystem::current_path() / "stereo-music-fixture.wav";
    expect(!std::filesystem::exists(path), "Stereo music fixture already exists");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};

    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(wave.data()), std::streamsize(wave.size()));
        expect(bool(output), "Write stereo music fixture");
    }
    service.load_music(2, path);
    service.start_music(2, 1, 0);
    pcm_is(.25f, -.125f, "Music must initially retain distinct stereo channels");
    settings.stereo_effects = false;
    service.apply(settings);
    pcm_is(.25f, -.125f, "Mono effects must leave stereo CD music unchanged");
    service.stop_music();
    std::cout << "PASS: live mono/stereo, retained pan controls, voice reuse, stereo source downmix, independent stereo music\n";
}

void raw_refill_and_eof() {
    toca::audio::Service initialized({}, true);
    const SDL_AudioSpec output{SDL_AUDIO_F32, 2, 44100}, source{SDL_AUDIO_F32, 1, 44100};
    std::unique_ptr<MIX_Mixer, decltype(&MIX_DestroyMixer)> mixer(MIX_CreateMixer(&output), MIX_DestroyMixer);
    expect(bool(mixer), "Create raw probe mixer");
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream(nullptr, SDL_DestroyAudioStream);
    std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> track(MIX_CreateTrack(mixer.get()), MIX_DestroyTrack);
    expect(bool(track), "Create raw probe track");
    const std::vector<float> raw(882, .125f);
    auto* io = SDL_IOFromConstMem(raw.data(), raw.size() * sizeof(float));
    expect(io && MIX_SetTrackRawIOStream(track.get(), io, &source, true), "Assign raw PCM IO");
    expect(MIX_PlayTrack(track.get(), 0), "Play raw PCM IO");
    std::vector<float> rendered(2000 * 2);
    expect(MIX_Generate(mixer.get(), rendered.data(), int(rendered.size() * sizeof(float))) >= 0, "Mix raw PCM IO");
    expect(nonzero_frames(rendered) == 882 && !MIX_TrackPlaying(track.get()), "Raw PCM IO did not finish at EOF");
    stream.reset(SDL_CreateAudioStream(&source, &source));
    expect(bool(stream), "Create refill stream");
    expect(MIX_SetTrackAudioStream(track.get(), stream.get()), "Assign refill stream");
    expect(SDL_PutAudioStreamData(stream.get(), raw.data(), 441 * sizeof(float)), "Push first raw block");
    expect(MIX_PlayTrack(track.get(), 0), "Start refill stream");
    std::vector<float> first(220 * 2), tail(1000 * 2);
    expect(MIX_Generate(mixer.get(), first.data(), int(first.size() * sizeof(float))) >= 0, "Mix first raw block");
    expect(MIX_TrackPlaying(track.get()), "Stream stopped before refill");
    expect(SDL_PutAudioStreamData(stream.get(), raw.data() + 441, 441 * sizeof(float)), "Refill raw block");
    expect(SDL_FlushAudioStream(stream.get()), "Flush EOF raw block");
    expect(MIX_Generate(mixer.get(), tail.data(), int(tail.size() * sizeof(float))) >= 0, "Mix refill tail");
    expect(nonzero_frames(first) + nonzero_frames(tail) == 882 && !MIX_TrackPlaying(track.get()), "Raw refill count/EOF changed");
    expect(std::all_of(first.begin(), first.end(),
               [](float sample) {
                   return std::abs(sample - .125f) < .00001f;
               }),
        "Raw refill continuity changed");
    expect(MIX_SetTrackAudioStream(track.get(), nullptr), "Detach refill stream");
    std::cout << "PASS: raw IO EOF and mid-play AudioStream refill continuity/sample count/EOF\n";
}

struct SourceFormat {
    std::uint32_t rate = 0, channels = 0;
    std::uint64_t frames = 0;
};

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    expect(bool(input), "Open converted audio probe");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void decoded_vorbis(std::span<const std::uint8_t> bytes, SourceFormat source) {
    expect(source.rate > 0 && (source.channels == 1 || source.channels == 2) && source.frames > 0, "Vorbis probe requires a complete original source format");
    expect(bytes.size() >= 4 && std::memcmp(bytes.data(), "OggS", 4) == 0, "Converted sample must contain real Ogg bytes");
    toca::audio::Service runtime({}, true);
    expect(runtime.load_sample(1, bytes) == source.rate, "Encoded sample API changed the original frequency");
    const auto voice = runtime.play(1);
    double signal = 0;
    const auto output_frames = (source.frames * 44100 + source.rate - 1) / source.rate + 4410;
    for (std::uint64_t offset = 0; offset < output_frames; offset += 1024) {
        const auto pcm = runtime.offline_mix(std::size_t(std::min<std::uint64_t>(1024, output_frames - offset)));
        expect(std::ranges::all_of(pcm,
                   [](float value) {
                       return std::isfinite(value);
                   }),
            "Decoded Vorbis contains nonfinite PCM");
        signal += energy(pcm, 0);
    }
    expect(signal > 0 && !runtime.playing(voice), "Converted finite sample must produce PCM and reach EOF");

    using Audio = std::unique_ptr<MIX_Audio, decltype(&MIX_DestroyAudio)>;
    Audio audio(MIX_LoadAudio_IO(nullptr, SDL_IOFromConstMem(bytes.data(), bytes.size()), true, true), MIX_DestroyAudio);
    expect(bool(audio), "Predecode converted Vorbis for exact frame check");
    SDL_AudioSpec decoded{};
    expect(MIX_GetAudioFormat(audio.get(), &decoded), "Query predecoded Vorbis source format");
    expect(decoded.freq == int(source.rate) && decoded.channels == int(source.channels), "Vorbis changed original frequency/channel count");
    const auto frames = MIX_GetAudioDuration(audio.get());
    if (frames != Sint64(source.frames)) {
        throw std::runtime_error("Vorbis decoded frame count changed: expected " + std::to_string(source.frames) + ", got " + std::to_string(frames));
    }

    // Mix at the source frequency to check two exact loop spans without resampling.
    const SDL_AudioSpec format{SDL_AUDIO_F32, int(source.channels), int(source.rate)};
    std::unique_ptr<MIX_Mixer, decltype(&MIX_DestroyMixer)> mixer(MIX_CreateMixer(&format), MIX_DestroyMixer);
    expect(bool(mixer), "Create source-frequency loop probe");
    std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> track(MIX_CreateTrack(mixer.get()), MIX_DestroyTrack);
    expect(bool(track) && MIX_SetTrackAudio(track.get(), audio.get()), "Assign loop probe sample");
    const auto options = SDL_CreateProperties();
    expect(options != 0, "Create loop probe options");
    const auto started = SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, 1) && MIX_PlayTrack(track.get(), options);
    SDL_DestroyProperties(options);
    expect(started, "Start exactly two Vorbis loop plays");
    const auto samples = source.frames * source.channels;
    expect(samples < std::uint64_t(std::numeric_limits<int>::max() / (2 * sizeof(float))) - 16, "Loop probe exceeds mixer bounds");
    std::vector<float> loops(std::size_t(samples * 2 + 16));
    expect(MIX_Generate(mixer.get(), loops.data(), int(loops.size() * sizeof(float))) >= 0, "Mix complete Vorbis loop probe");
    expect(!MIX_TrackPlaying(track.get()), "Two converted loop plays exceeded their original duration");
    expect(std::equal(loops.begin(), loops.begin() + std::ptrdiff_t(samples), loops.begin() + std::ptrdiff_t(samples)),
        "Converted loop changed its sample span or introduced padding");
    std::cout << "PASS: Vorbis source " << source.rate << "Hz/" << source.channels << "ch, " << source.frames << " decoded frames, finite EOF and two exact loops\n";
}

void converted_samples(const std::filesystem::path& converted_root) {
    const toca::AssetStore converted(converted_root);
    expect(!converted.find("fe/sounds/updown.wav") && !converted.find("speech/pc/english/extras/FINAL.raw"), "Converted root must exercise Ogg without PCM fallbacks");
    // This checks approved Vorbis output, not sample equality with the original PCM.
    decoded_vorbis(converted.read_file("fe/sounds/updown.ogg"), {22050, 1, 5615});
    decoded_vorbis(converted.read_file("speech/pc/english/extras/FINAL.ogg"), {22050, 1, 405504});
    const auto archive = converted.open_archive("data/sounddry.bfp");
    expect(archive.size() == 84, "Converted sound archive changed member indices");
    for (const auto [index, frames] : std::array<std::pair<std::size_t, std::uint64_t>, 3>{{{1, 13468}, {24, 22207}, {83, 169061}}}) {
        decoded_vorbis(archive.member(index).bytes(), {22050, 1, frames});
    }
    toca::audio::Service runtime({}, true);
    expect(toca::audio::load_sample_file(runtime, 1, converted, "fe/sounds/updown.wav") == 22050, "Frontend did not resolve the converted Ogg cue");
    expect(toca::audio::load_sample_file(runtime, 2, converted, "speech/pc/english/extras/FINAL.raw") == 22050, "Commentary did not resolve converted Vorbis");
}

void dummy_device() {
    expect(std::string(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "") == "dummy"
               || std::string(SDL_getenv("SDL_AUDIODRIVER") ? SDL_getenv("SDL_AUDIODRIVER") : "") == "dummy",
        "Device probe requires SDL dummy output");
    toca::audio::Service service({}, false);
    expect(std::string(SDL_GetCurrentAudioDriver()) == "dummy", "Device probe opened a non-dummy driver");
    service.load_pcm(9, std::vector<float>(4410, .125f), 1, 44100);
    service.consume({1, 0, toca::GameEventKind::startup, 0});
    service.consume({2, 0, toca::GameEventKind::play_sound, 9});
    const auto deadline = SDL_GetTicks() + 2000;
    while (service.active_voices() && SDL_GetTicks() < deadline) {
        SDL_Delay(5);
    }
    expect(service.active() && service.active_voices() == 0, "Dummy device did not consume event PCM");
    service.consume({3, 0, toca::GameEventKind::shutdown, 0});
    std::cout << "PASS: real SDL dummy-device event/playback/completion path\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string_view(argv[1]) == "--vorbis") {
            decoded_vorbis(file_bytes(argv[2]), {std::uint32_t(std::stoul(argv[3])), std::uint32_t(std::stoul(argv[4])), std::stoull(argv[5])});
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--converted") {
            converted_samples(argv[2]);
            return 0;
        }
        expect(argc == 1, "Usage: toca-audio-tests [--vorbis FILE RATE CHANNELS FRAMES | --converted BUNDLED_ROOT]");
        pool_failure_recovery();
        events_loops_and_pool();
        split_pool();
        live_controls();
        original_volume_curve();
        stereo_and_mono_effects();
        raw_refill_and_eof();
        dummy_device();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
