#include "assets/store.hpp"
#include "audio/frontend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        expect(std::isfinite(sample), "Audio output is not finite");
        result += double(sample) * sample;
    }
    return result;
}

std::vector<std::uint8_t> wave() {
    constexpr std::uint32_t frames = 882;
    std::vector<std::uint8_t> result(44 + frames * 2);
    auto put16 = [&](std::size_t offset, std::uint16_t value) {
        result.at(offset) = std::uint8_t(value);
        result.at(offset + 1) = std::uint8_t(value >> 8);
    };
    auto put32 = [&](std::size_t offset, std::uint32_t value) {
        put16(offset, std::uint16_t(value));
        put16(offset + 2, std::uint16_t(value >> 16));
    };
    std::memcpy(result.data(), "RIFF", 4);
    put32(4, std::uint32_t(result.size() - 8));
    std::memcpy(result.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, 44100);
    put32(28, 88200);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(result.data() + 36, "data", 4);
    put32(40, frames * 2);
    for (std::size_t index = 0; index < frames; ++index) {
        put16(44 + index * 2, 8192);
    }
    return result;
}

void synthetic() {
    auto encoded = wave();
    const auto music_path = std::filesystem::current_path() / "frontend-music-fixture.wav";
    expect(!std::filesystem::exists(music_path), "Music fixture already exists");
    {
        std::ofstream output(music_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()), std::streamsize(encoded.size()));
        expect(bool(output), "Cannot write music fixture");
    }

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{music_path};

    {
        toca::audio::Service buses({}, true);
        buses.load_sample(0, encoded);
        buses.load_music(2, music_path);
        const auto voice = buses.play(0, 0);
        const auto full_effects = energy(buses.offline_mix(88));
        toca::AudioSettings settings;
        settings.effects_volume = 50;
        buses.apply(settings);
        expect(std::abs(energy(buses.offline_mix(88)) / full_effects - .25) < .001, "Live effects volume must scale active sample PCM");
        buses.controls(voice, .5f, 1, 1, 1);
        expect(std::abs(energy(buses.offline_mix(88)) / full_effects - .0625) < .001, "Engine controls must preserve the separate effects volume");
        settings.music_volume = 0;
        buses.apply(settings);
        expect(std::abs(energy(buses.offline_mix(88)) / full_effects - .0625) < .001, "Music volume must not alter sample controls");
        buses.stop(voice);
        const auto reused = buses.play(0, 0);
        expect(std::abs(energy(buses.offline_mix(88)) / full_effects - .25) < .001, "New sample playback must retain effects volume");
        buses.stop(reused);
        settings.music_volume = 100;
        buses.apply(settings);
        buses.start_music(2, 1, 0);
        const auto full_music = energy(buses.offline_mix(88));
        settings.effects_volume = 0;
        settings.game_music = false; // This flag gates scene callers; frontend CD02 keeps playing.
        buses.apply(settings);
        expect(std::abs(energy(buses.offline_mix(88)) / full_music - 1) < .001, "Effects volume and in-game policy must not silence frontend music");
        settings.music_volume = 50;
        buses.apply(settings);
        expect(std::abs(energy(buses.offline_mix(88)) / full_music - .25) < .001, "Live music volume must independently scale decoded PCM");
        settings.muted = true;
        buses.apply(settings);
        expect(energy(buses.offline_mix(88)) == 0, "Master mute must cover both independent buses");
    }

    toca::audio::Service service({}, true);

    for (const auto sample : {0u, 1u, 2u, 3u, 4u}) {
        service.load_sample(sample, encoded);
    }
    encoded.assign(encoded.size(), 0); // Predecoded samples do not borrow the supplied span.
    for (const auto cue : {toca::audio::FrontendCue::navigation, toca::audio::FrontendCue::option, toca::audio::FrontendCue::character, toca::audio::FrontendCue::confirm,
             toca::audio::FrontendCue::back, toca::audio::FrontendCue::unavailable}) {
        expect(toca::audio::play_frontend_cue(service, cue) != 0, "Frontend cue did not allocate a voice");
        // Canonical master100/sample70 -> integer55 -> DirectSound -726.
        expect(std::abs(energy(service.offline_mix(88)) - 176 * .25 * .25 * std::pow(10., -726. / 1000)) < 1e-6,
            "Frontend first PCM block must use the original integer master/sample curve");
        service.apply({.effects_volume = 50});
        // master50/sample70 -> integer27 -> -1344; no replay is needed.
        expect(std::abs(energy(service.offline_mix(88)) - 176 * .25 * .25 * std::pow(10., -1344. / 1000)) < 1e-6,
            "Live frontend volume must recompute the Miles curve for its active cue");
        service.apply({});
        expect(energy(service.offline_mix(1323)) > 0 && service.active_voices() == 0, "Finite encoded frontend cue did not mix and finish");
    }
    service.load_music(2, music_path);
    expect(service.music_duration_ns(2) == 20'000'000, "Music metadata duration changed");
    service.start_music(2, .5f, 1'000'000'000);
    expect(service.music_playing() && service.music_track() == 2 && service.music_starts() == 1 && service.active_voices() == 0, "Music used a sample-pool voice");
    service.load_pcm(99, std::vector<float>(441, .001f), 1, 44100);
    for (toca::audio::Voice voice = 1; voice <= 12; ++voice) {
        expect(service.play(99, 0) == voice, "Music reduced the 12 available sample voices");
    }
    service.clear_samples();
    expect(service.active_voices() == 0 && service.music_playing(), "Scene handoff must release all sample voices independently of the CD track");
    service.load_sample(0, wave());
    expect(service.play(0) == 1, "Frontend return must reload and reuse released voices");
    expect(energy(service.offline_mix(1323)) > 0 && !service.music_playing(), "Music did not reach finite EOF");
    service.update_music(3'020'000'000, true);
    expect(service.music_starts() == 1, "Music restarted at duration+2000ms instead of strictly after");
    service.update_music(3'021'000'000, true);
    expect(service.music_starts() == 2 && service.music_playing(), "Completed music did not restart after its deadline");
    service.update_music(6'000'000'000, true);
    expect(service.music_starts() == 2, "Elapsed time restarted music before playback reached EOF");
    service.stop_music();
    service.update_music(10'000'000'000, true);
    expect(service.music_track() == 0 && !service.music_playing() && service.music_starts() == 2, "Explicit stop did not disarm restart");
    service.start_music(2, 1, 11'000'000'000);
    service.apply({1, true});
    expect(energy(service.offline_mix(441)) == 0, "Music mute bypassed the mixer");
    service.apply({1, false});
    expect(energy(service.offline_mix(882)) > 0 && !service.music_playing(), "Music unmute/EOF path failed");
    service.update_music(14'000'000'000, false);
    service.update_music(15'000'000'000, true);
    expect(service.music_starts() == 3 && !service.music_playing(), "Disabled restart was rearmed without a new play request");
    service.start_music(2, 1, 16'000'000'000);
    service.consume({1, 16'000'000'001, toca::GameEventKind::shutdown, 0});
    expect(!service.music_playing() && service.music_track() == 0, "Shutdown left the music track running");
    std::cout << "PASS: encoded WAV cue PCM, separate music/12 voices, finite EOF/deadline/status/restartflag/stop/mute/shutdown\n";
}

void bundled(const std::filesystem::path& data_root, const std::filesystem::path& resource_root) {
    toca::audio::Service service({}, true);
    toca::audio::load_frontend_samples(service, toca::AssetStore(data_root));
    for (const auto cue : {toca::audio::FrontendCue::navigation, toca::audio::FrontendCue::option, toca::audio::FrontendCue::character, toca::audio::FrontendCue::confirm,
             toca::audio::FrontendCue::unavailable}) {
        expect(toca::audio::play_frontend_cue(service, cue) != 0, "Original cue did not allocate a voice");
        expect(energy(service.offline_mix(44100)) > 0, "Original cue produced no PCM");
    }
    for (std::uint32_t language = 0; language < 5; ++language) {
        expect(
            toca::audio::frontend_sample_id(toca::audio::FrontendCue::cheat_activated, language) == 5 + language, "Cheat activation must select the original localized CME sample");
        expect(toca::audio::play_frontend_cue(service, toca::audio::FrontendCue::cheat_activated, language) != 0, "Original cheat announcement did not allocate a voice");
        expect(energy(service.offline_mix(44100 * 5)) > 0 && service.active_voices() == 0, "Original localized cheat announcement must decode audible PCM and finish");
    }
    toca::audio::Voice announcement = 0;
    for (int repeat = 0; repeat < 30; ++repeat) {
        announcement = toca::audio::play_frontend_cue(service, toca::audio::FrontendCue::cheat_activated, 2, announcement);
        expect(announcement != 0 && service.active_voices() == 1, "Rapid code activation must replace the original announcement handle, not exhaust the voice pool");
        expect(energy(service.offline_mix(2205)) > 0, "Replacement announcement lost its decoded speech");
    }
    service.stop(announcement);
    service.load_music(2, resource_root / "music/track02.ogg");
    expect(service.music_duration_ns(2) == 194'720'000'000, "Original track02 metadata duration changed");
    service.start_music(2, 1, 0);
    double decoded_energy = 0;
    for (int chunk = 0; chunk < 10; ++chunk) {
        decoded_energy += energy(service.offline_mix(4410));
    }
    expect(service.music_playing() && decoded_energy > 0, "Deferred Vorbis track02 did not produce finite PCM");
    service.stop_music();
    expect(energy(service.offline_mix(441)) == 0, "Music stop left audio in output");
    service.clear_samples();
    service.load_music(3, resource_root / "music/track03.ogg");
    service.start_music(3, 1, 2'000'000'000);
    decoded_energy = 0;
    for (int chunk = 0; chunk < 10; ++chunk) {
        decoded_energy += energy(service.offline_mix(4410));
    }
    expect(service.music_track() == 3 && service.music_playing() && decoded_energy > 0 && service.active_voices() == 0,
        "Scene CD03 must decode real PCM after frontend sample teardown");
    service.stop_music();
    service.start_music(2, 1, 4'000'000'000);
    expect(service.music_track() == 2 && energy(service.offline_mix(4410)) > 0, "Frontend return must resume the actual CD02 decoder");
    std::cout << "PASS: bundled Vorbis cues, CD02/03 PCM and stop/scene handoff/frontend return\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 3) {
            bundled(argv[1], argv[2]);
        } else {
            throw std::runtime_error("Expected optional bundled data and application resource roots");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
