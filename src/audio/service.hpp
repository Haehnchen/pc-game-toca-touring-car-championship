#pragma once

#include "core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace toca::audio {
using Voice = std::uint32_t;
inline constexpr std::size_t max_effect_voices = 24;

struct MilesPanGains {
    float left = 1, right = 1;
};

// Canonical mss32's DirectSound2000def0/2000de70 pan0..127 curve.
MilesPanGains original_pan_gains(std::uint32_t pan);

// SDL_mixer owns decoding/resampling/mixing. Native voices retain game-side allocation.
class Service {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Voice play_sample(std::uint32_t sample, std::uint32_t total_plays, float gain, std::optional<std::uint32_t> original_volume, std::uint32_t pan);

public:
    explicit Service(const AudioSettings& settings = {}, bool offline = false);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    void apply(const AudioSettings& settings);
    void consume(const GameEvent& event);
    bool active() const;
    bool offline() const;
    std::uint64_t consumed_events() const;
    std::size_t active_voices() const;
    // Return the decoded source frequency for original pitch-rate controls.
    std::uint32_t load_sample(std::uint32_t sample, std::span<const std::uint8_t> encoded);
    void clear_samples();
    // Twelve voices per local listener; resize only between scenes.
    void set_local_players(std::uint32_t players);
    void load_pcm(std::uint32_t sample, std::span<const float> pcm, int channels, int sample_rate);
    Voice play(std::uint32_t sample, std::uint32_t total_plays = 1, float gain = 1);
    // Set original sample pan before playback can reach the audio device.
    Voice play_pan(std::uint32_t sample, std::uint32_t total_plays, float gain, std::uint32_t pan);
    // Original Miles integer master/sample volume and DirectSound hundredth-dB conversion.
    Voice play_original_volume(std::uint32_t sample, std::uint32_t total_plays, std::uint32_t volume, std::uint32_t pan = 64);
    void stop(Voice voice);
    bool playing(Voice voice) const;
    std::uint64_t playback_serial(Voice voice) const;
    void controls(Voice voice, float gain, float frequency_ratio, float left_gain, float right_gain);
    void controls_original_volume(Voice voice, std::uint32_t volume, float frequency_ratio, std::uint32_t pan = 64);
    void load_music(std::uint32_t cd_track, const std::filesystem::path& path);
    void start_music(std::uint32_t cd_track, float gain, std::uint64_t timestamp_ns);
    void update_music(std::uint64_t timestamp_ns, bool restart_enabled);
    void stop_music();
    bool music_playing() const;
    std::uint32_t music_track() const;
    std::uint64_t music_starts() const;
    std::uint64_t music_duration_ns(std::uint32_t cd_track) const;
    std::vector<float> offline_mix(std::size_t frames);
};
} // namespace toca::audio
