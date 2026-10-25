#include "audio/service.hpp"

#include "audio/voice_pool.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace toca::audio {
namespace {
void check(bool result, const char* operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}

struct Runtime {
    Runtime() {
        check(SDL_InitSubSystem(SDL_INIT_AUDIO), "Initialize audio service");
        if (!MIX_Init()) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            check(false, "Initialize SDL_mixer");
        }
    }

    ~Runtime() {
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

template <class T, auto Destroy> using Owned = std::unique_ptr<T, decltype(Destroy)>;

struct MixerLock {
    MIX_Mixer* mixer;

    explicit MixerLock(MIX_Mixer* value) : mixer(value) {
        MIX_LockMixer(mixer);
    }

    ~MixerLock() {
        MIX_UnlockMixer(mixer);
    }
};

void SDLCALL mono_effect(void*, MIX_Track*, const SDL_AudioSpec* spec, float* pcm, int samples) {
    // Each effect track forces stereo before this cooked callback. Average the
    // source channels before SDL_mixer applies the centered per-channel gains.
    SDL_assert(spec->channels == 2);
    for (int index = 0; index + 1 < samples; index += 2) {
        const auto mono = (pcm[index] + pcm[index + 1]) * .5f;
        pcm[index] = pcm[index + 1] = mono;
    }
}

float original_volume_gain(std::uint32_t volume) {
    static const auto gains = [] {
        std::array<float, 101> result{};
        // Combined sample0..127 and effects0..100 volumes span only0..100.
        // DS2000de70's -10000 endpoint stays exact silence, not residual1e-5.
        // Other entries retain the original truncated hundredth-dB curve.
        for (std::size_t value = 1; value < result.size(); ++value) {
            const auto decibels = std::int32_t(-2000 * std::log10(127.0 / double(value)));
            result[value] = float(std::pow(10.0, double(decibels) / 2000));
        }
        return result;
    }();
    return gains[volume];
}
} // namespace

MilesPanGains original_pan_gains(std::uint32_t pan) {
    if (pan > 127) {
        throw std::runtime_error("Original sample pan must be within0..127");
    }
    if (pan == 0) {
        return {1, 0};
    }
    if (pan == 127) {
        return {0, 1};
    }
    if (pan == 63 || pan == 64) {
        return {};
    }
    // 2000def0 passes either pan or127-pan to2000de70 with range0..63.
    // DirectSound keeps the near channel at full gain and attenuates the far
    // channel by the returned, truncated hundredth-dB value.
    const auto value = pan < 64 ? pan : 127 - pan;
    static const auto far_gains = [] {
        std::array<float, 64> result{};
        for (std::size_t value = 1; value < result.size(); ++value) {
            const auto decibels = std::int32_t(-2000 * std::log10(63.0 / double(value)));
            result[value] = float(std::pow(10.0, double(decibels) / 2000));
        }
        return result;
    }();
    const auto gain = far_gains[value];
    return pan < 64 ? MilesPanGains{1, gain} : MilesPanGains{gain, 1};
}

struct Service::Impl {
    Runtime runtime;
    Owned<MIX_Mixer, MIX_DestroyMixer> mixer{nullptr, MIX_DestroyMixer};
    std::unordered_map<std::uint32_t, Owned<MIX_Audio, MIX_DestroyAudio>> samples;
    std::unordered_map<std::uint32_t, Owned<MIX_Audio, MIX_DestroyAudio>> music;
    Owned<MIX_Track, MIX_DestroyTrack> music_voice{nullptr, MIX_DestroyTrack};
    using VoiceSlot = detail::VoiceSlot<Owned<MIX_Track, MIX_DestroyTrack>>;
    std::vector<VoiceSlot> voices;
    bool offline = false, running = true;
    std::uint64_t count = 0, sequence = 0;
    bool stereo_effects = true;
    std::uint32_t effects_volume = 100;
    float effects_gain = 1, music_bus_gain = 1;
    std::uint64_t next_playback_serial = 0;
    std::uint32_t current_music = 0;
    std::uint64_t music_started_ns = 0, music_start_count = 0;
    float music_gain = 1;
    bool music_armed = false;

    explicit Impl(bool offline_output) : offline(offline_output) {
        const SDL_AudioSpec format{SDL_AUDIO_F32, 2, 44100};
        mixer.reset(offline ? MIX_CreateMixer(&format) : MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format));
        check(bool(mixer), "Create SDL_mixer output");
        music_voice.reset(MIX_CreateTrack(mixer.get()));
        check(bool(music_voice), "Allocate separate music track");
        resize_voices(12);
    }

    void resize_voices(std::size_t count) {
        detail::resize_voice_pool(voices, count, [&] {
            Owned<MIX_Track, MIX_DestroyTrack> track(MIX_CreateTrack(mixer.get()), MIX_DestroyTrack);
            check(bool(track), "Allocate shared audio voice");
            check(MIX_SetTrackCookedCallback(track.get(), stereo_effects ? nullptr : mono_effect, nullptr), "Initialize local voice mono output");
            return track;
        });
    }

    MIX_Track* track(Voice voice) const {
        if (voice == 0 || voice > voices.size()) {
            throw std::runtime_error("Audio voice is outside the local-player pool");
        }
        return voices.at(voice - 1).track.get();
    }

    float voice_gain(std::size_t index) const {
        // Keep voice curves separate from the live effects bus gain.
        const auto& slot = voices[index];
        if (!slot.original_volume) {
            return slot.gain * effects_gain;
        }
        // Canonical mss32 2000de00/de70: combine integers first, then truncate
        // -2000*log10(127/volume) to DirectSound hundredths of a decibel.
        const auto volume = effects_volume * *slot.original_volume / 127;
        return original_volume_gain(volume);
    }
};

Service::Service(const AudioSettings& settings, bool offline) : impl_(std::make_unique<Impl>(offline)) {
    apply(settings);
}

Service::~Service() = default;

void Service::apply(const AudioSettings& settings) {
    if (!std::isfinite(settings.master_gain) || settings.master_gain < 0 || settings.master_gain > 1) {
        throw std::runtime_error("Audio master gain must be finite and within 0..1");
    }
    if (settings.effects_volume > 100 || settings.music_volume > 100) {
        throw std::runtime_error("Effects and music volumes must be within 0..100");
    }
    impl_->effects_gain = float(settings.effects_volume) / 100;
    impl_->effects_volume = settings.effects_volume;
    impl_->music_bus_gain = float(settings.music_volume) / 100;
    const MixerLock lock(impl_->mixer.get());
    impl_->stereo_effects = settings.stereo_effects;
    constexpr MIX_StereoGains centered{1, 1};
    for (std::size_t index = 0; index < impl_->voices.size(); ++index) {
        check(MIX_SetTrackGain(impl_->voices[index].track.get(), impl_->voice_gain(index)), "Apply effects volume");
        check(MIX_SetTrackStereo(impl_->voices[index].track.get(), settings.stereo_effects ? &impl_->voices[index].stereo : &centered), "Apply effects stereo mode");
        check(MIX_SetTrackCookedCallback(impl_->voices[index].track.get(), settings.stereo_effects ? nullptr : mono_effect, nullptr), "Apply effects mono output");
    }
    check(MIX_SetTrackGain(impl_->music_voice.get(), impl_->music_gain * impl_->music_bus_gain), "Apply music volume");
    check(MIX_SetMixerGain(impl_->mixer.get(), settings.muted ? 0.f : settings.master_gain), "Set mixer master gain");
}

void Service::consume(const GameEvent& event) {
    if (impl_->count != 0 && event.sequence <= impl_->sequence) {
        throw std::runtime_error("Audio events must have strictly increasing sequence numbers");
    }
    switch (event.kind) {
        case GameEventKind::startup:
            impl_->running = true;
            break;
        case GameEventKind::focus_changed:
            // Focus policy awaits recovered callers; it does not create an audio cue.
            break;
        case GameEventKind::play_sound:
            if (!impl_->running) {
                throw std::runtime_error("Cannot play an audio event after shutdown");
            }
            (void)play(event.value);
            break;
        case GameEventKind::shutdown:
            stop_music();
            for (auto& voice : impl_->voices) {
                check(MIX_StopTrack(voice.track.get(), 0), "Stop shutdown voice");
            }
            impl_->running = false;
            break;
    }
    impl_->sequence = event.sequence;
    ++impl_->count;
}

bool Service::active() const {
    return impl_->running;
}

bool Service::offline() const {
    return impl_->offline;
}

std::uint64_t Service::consumed_events() const {
    return impl_->count;
}

std::size_t Service::active_voices() const {
    return std::ranges::count_if(impl_->voices, [](const auto& voice) {
        return MIX_TrackPlaying(voice.track.get());
    });
}

std::uint32_t Service::load_sample(std::uint32_t sample, std::span<const std::uint8_t> encoded) {
    if (encoded.empty()) {
        throw std::runtime_error("Encoded sample cannot be empty");
    }
    auto* stream = SDL_IOFromConstMem(encoded.data(), encoded.size());
    check(stream != nullptr, "Open encoded sample bytes");
    Owned<MIX_Audio, MIX_DestroyAudio> audio(MIX_LoadAudio_IO(impl_->mixer.get(), stream, true, true), MIX_DestroyAudio);
    check(bool(audio), "Decode sample bytes");
    SDL_AudioSpec format{};
    check(MIX_GetAudioFormat(audio.get(), &format), "Query decoded sample format");
    if (format.freq <= 0 || format.freq > 384000) {
        throw std::runtime_error("Decoded sample frequency must be within 1..384000 Hz");
    }
    impl_->samples.insert_or_assign(sample, std::move(audio));
    return std::uint32_t(format.freq);
}

void Service::set_local_players(std::uint32_t players) {
    if (players < 1 || players > 2 || active_voices() != 0) {
        throw std::runtime_error("Audio pool changes require one or two players and no active sample voices");
    }
    impl_->resize_voices(players * 12);
}

void Service::clear_samples() {
    for (auto& voice : impl_->voices) {
        check(MIX_StopTrack(voice.track.get(), 0), "Stop outgoing sample voice");
        check(MIX_SetTrackAudio(voice.track.get(), nullptr), "Release outgoing voice sample");
    }
    impl_->samples.clear();
}

void Service::load_pcm(std::uint32_t sample, std::span<const float> pcm, int channels, int sample_rate) {
    if (pcm.empty() || (channels != 1 && channels != 2) || pcm.size() % std::size_t(channels) != 0 || sample_rate <= 0) {
        throw std::runtime_error("PCM requires complete mono/stereo frames and a positive source rate");
    }
    const SDL_AudioSpec format{SDL_AUDIO_F32, channels, sample_rate};
    Owned<MIX_Audio, MIX_DestroyAudio> audio(MIX_LoadRawAudio(impl_->mixer.get(), pcm.data(), pcm.size_bytes(), &format), MIX_DestroyAudio);
    check(bool(audio), "Load PCM sample");
    impl_->samples.insert_or_assign(sample, std::move(audio));
}

Voice Service::play(std::uint32_t sample, std::uint32_t total_plays, float gain) {
    return play_sample(sample, total_plays, gain, std::nullopt, 64);
}

Voice Service::play_pan(std::uint32_t sample, std::uint32_t total_plays, float gain, std::uint32_t pan) {
    return play_sample(sample, total_plays, gain, std::nullopt, pan);
}

Voice Service::play_original_volume(std::uint32_t sample, std::uint32_t total_plays, std::uint32_t volume, std::uint32_t pan) {
    if (volume > 127) {
        throw std::runtime_error("Original sample volume must be within 0..127");
    }
    return play_sample(sample, total_plays, 1, volume, pan);
}

Voice Service::play_sample(std::uint32_t sample, std::uint32_t total_plays, float gain, std::optional<std::uint32_t> original_volume, std::uint32_t pan) {
    if (!impl_->running) {
        throw std::runtime_error("Cannot start a voice after audio shutdown");
    }
    if (!std::isfinite(gain) || gain < 0 || gain > 1) {
        throw std::runtime_error("Sample gain must be finite and within 0..1");
    }
    const auto pan_gains = original_pan_gains(pan);
    const auto audio = impl_->samples.find(sample);
    if (audio == impl_->samples.end()) {
        throw std::runtime_error("Audio sample is not loaded: " + std::to_string(sample));
    }
    const MixerLock lock(impl_->mixer.get());
    for (std::size_t index = 0; index < impl_->voices.size(); ++index) {
        auto* voice = impl_->voices[index].track.get();
        if (MIX_TrackPlaying(voice)) {
            continue;
        }
        check(MIX_SetTrackAudio(voice, audio->second.get()), "Assign sample to voice");
        impl_->voices[index].gain = gain;
        impl_->voices[index].original_volume = original_volume;
        check(MIX_SetTrackGain(voice, impl_->voice_gain(index)), "Set initial voice gain");
        check(MIX_SetTrackFrequencyRatio(voice, 1), "Reset voice rate");
        impl_->voices[index].stereo = {pan_gains.left, pan_gains.right};
        const MIX_StereoGains centered{1, 1};
        check(MIX_SetTrackStereo(voice, impl_->stereo_effects ? &impl_->voices[index].stereo : &centered), "Set initial voice stereo gains");
        const auto options = SDL_CreateProperties();
        check(options != 0, "Create loop options");
        // Miles uses total plays (zero=infinite); SDL_mixer uses additional repeats.
        const auto loops = total_plays == 0 ? -1 : Sint64(total_plays) - 1;
        const bool configured = SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
        const bool started = configured && MIX_PlayTrack(voice, options);
        SDL_DestroyProperties(options);
        check(started, "Start sample voice");
        impl_->voices[index].serial = ++impl_->next_playback_serial;
        return Voice(index + 1);
    }
    return 0;
}

void Service::stop(Voice voice) {
    check(MIX_StopTrack(impl_->track(voice), 0), "Stop sample voice");
}

bool Service::playing(Voice voice) const {
    return MIX_TrackPlaying(impl_->track(voice));
}

std::uint64_t Service::playback_serial(Voice voice) const {
    (void)impl_->track(voice);
    return impl_->voices.at(voice - 1).serial;
}

void Service::controls(Voice voice, float gain, float frequency_ratio, float left_gain, float right_gain) {
    auto* track = impl_->track(voice);
    const MIX_StereoGains stereo{left_gain, right_gain};
    constexpr MIX_StereoGains centered{1, 1};
    const MixerLock lock(impl_->mixer.get());
    check(MIX_SetTrackGain(track, gain * impl_->effects_gain), "Set voice gain");
    impl_->voices.at(voice - 1).gain = gain;
    impl_->voices.at(voice - 1).original_volume.reset();
    check(MIX_SetTrackFrequencyRatio(track, frequency_ratio), "Set voice frequency ratio");
    check(MIX_SetTrackStereo(track, impl_->stereo_effects ? &stereo : &centered), "Set voice stereo gains");
    impl_->voices.at(voice - 1).stereo = stereo;
}

void Service::controls_original_volume(Voice voice, std::uint32_t volume, float frequency_ratio, std::uint32_t pan) {
    if (volume > 127) {
        throw std::runtime_error("Original sample volume must be within 0..127");
    }
    const auto gains = original_pan_gains(pan);
    auto* track = impl_->track(voice);
    const auto index = voice - 1;
    constexpr MIX_StereoGains centered{1, 1};
    const MixerLock lock(impl_->mixer.get());
    impl_->voices[index].original_volume = volume;
    impl_->voices[index].stereo = {gains.left, gains.right};
    check(MIX_SetTrackGain(track, impl_->voice_gain(index)), "Set original voice volume");
    check(MIX_SetTrackFrequencyRatio(track, frequency_ratio), "Set voice frequency ratio");
    check(MIX_SetTrackStereo(track, impl_->stereo_effects ? &impl_->voices[index].stereo : &centered), "Set original voice pan");
}

void Service::load_music(std::uint32_t cd_track, const std::filesystem::path& path) {
    if (cd_track < 2 || cd_track > 6) {
        throw std::runtime_error("Bundled CD audio track must be within 2..6");
    }
    const auto utf8 = path.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    Owned<MIX_Audio, MIX_DestroyAudio> audio(MIX_LoadAudio(impl_->mixer.get(), filename.c_str(), false), MIX_DestroyAudio);
    check(bool(audio), "Open deferred music decoder");
    check(MIX_GetAudioDuration(audio.get()) > 0, "Read finite music duration");
    impl_->music.insert_or_assign(cd_track, std::move(audio));
}

std::uint64_t Service::music_duration_ns(std::uint32_t cd_track) const {
    const auto found = impl_->music.find(cd_track);
    if (found == impl_->music.end()) {
        throw std::runtime_error("CD music is not loaded: " + std::to_string(cd_track));
    }
    const auto milliseconds = MIX_AudioFramesToMS(found->second.get(), MIX_GetAudioDuration(found->second.get()));
    check(milliseconds >= 0, "Read music duration in milliseconds");
    return std::uint64_t(milliseconds) * 1'000'000;
}

void Service::start_music(std::uint32_t cd_track, float gain, std::uint64_t timestamp_ns) {
    if (!impl_->running || !std::isfinite(gain) || gain < 0 || gain > 1) {
        throw std::runtime_error("Music requires an active service and finite gain within 0..1");
    }
    const auto found = impl_->music.find(cd_track);
    if (found == impl_->music.end()) {
        throw std::runtime_error("CD music is not loaded: " + std::to_string(cd_track));
    }
    stop_music();
    check(MIX_SetTrackAudio(impl_->music_voice.get(), found->second.get()), "Assign deferred music");
    check(MIX_SetTrackGain(impl_->music_voice.get(), gain * impl_->music_bus_gain), "Set music gain");
    check(MIX_PlayTrack(impl_->music_voice.get(), 0), "Start finite music track");
    impl_->current_music = cd_track;
    impl_->music_gain = gain;
    impl_->music_started_ns = timestamp_ns;
    impl_->music_armed = true;
    ++impl_->music_start_count;
}

void Service::update_music(std::uint64_t timestamp_ns, bool restart_enabled) {
    if (!impl_->music_armed || timestamp_ns < impl_->music_started_ns || music_playing()) {
        return;
    }
    const auto elapsed_ms = (timestamp_ns - impl_->music_started_ns) / 1'000'000;
    const auto deadline_ms = music_duration_ns(impl_->current_music) / 1'000'000 + 2000;
    if (elapsed_ms > deadline_ms) {
        impl_->music_armed = false;
        if (restart_enabled) {
            start_music(impl_->current_music, impl_->music_gain, timestamp_ns);
        }
    }
}

void Service::stop_music() {
    check(MIX_StopTrack(impl_->music_voice.get(), 0), "Stop music track");
    impl_->music_armed = false;
    impl_->current_music = 0;
}

bool Service::music_playing() const {
    return MIX_TrackPlaying(impl_->music_voice.get());
}

std::uint32_t Service::music_track() const {
    return impl_->current_music;
}

std::uint64_t Service::music_starts() const {
    return impl_->music_start_count;
}

std::vector<float> Service::offline_mix(std::size_t frames) {
    if (!impl_->offline || frames > std::size_t(std::numeric_limits<int>::max()) / (sizeof(float) * 2)) {
        throw std::runtime_error("Offline mixing requires an offline service and a bounded frame count");
    }
    std::vector<float> result(frames * 2);
    check(MIX_Generate(impl_->mixer.get(), result.data(), int(result.size() * sizeof(float))) >= 0, "Generate offline PCM");
    return result;
}
} // namespace toca::audio
