#include "audio/rivals.hpp"

#include <algorithm>
#include <utility>

namespace toca::audio {
Rivals::Rivals(Service& service, const AssetStore& assets, std::uint32_t weather) : Rivals(service, load_rival_engine_audio(service, assets, weather)) {}

Rivals::Rivals(Service& service, std::array<EngineLayer, 8> layers) : service_(service), layers_(std::move(layers)) {}

Rivals::~Rivals() {
    stop();
}

void Rivals::update(std::span<const RivalEngineInput> inputs, const SpatialListener& listener, std::uint16_t camera_view) {
    (void)engine_sample_indices({.camera_view = camera_view});
    // 424070/424410 category2: near chase mix1, far/trackside mix2, inside.
    const auto camera_gain = camera_view == 2 ? 0x6666 : camera_view == 0 || camera_view == 3 ? 0x5999 : 0x4ccc;

    struct Candidate {
        const RivalEngineInput* input;
        std::uint32_t volume, pan;
    };

    std::array<Candidate, 2> candidates{};
    std::size_t count = 0;
    for (const auto& input : inputs) {
        const auto spatial = source_spatial(input.position, listener);
        // 424c10 first truncates distance*0.8 to an integer, then category2.
        const auto distance_gain = (std::int64_t(spatial.fade_q16) * 0xcccc) >> 32;
        const auto volume = std::uint32_t((distance_gain * camera_gain) >> 16);
        if (spatial.in_range && volume) {
            // Only two motors can play. Strict comparisons retain input order
            // at equal volume, as in the original stable descending selection.
            if (count == 0 || volume > candidates[0].volume) {
                candidates[1] = candidates[0];
                candidates[0] = {&input, volume, spatial.pan};
            } else if (count < 2 || volume > candidates[1].volume) {
                candidates[1] = {&input, volume, spatial.pan};
            } else {
                continue;
            }
            count = std::min(count + 1, candidates.size());
        }
    }
    const auto selected = std::span(candidates).first(count);
    for (std::uint32_t actor = 0; actor < voices_.size(); ++actor) {
        if (std::ranges::none_of(selected, [actor](const auto& candidate) {
                return candidate.input->actor == actor;
            })) {
            if (voices_[actor] && service_.playback_serial(voices_[actor]) == serials_[actor]) {
                service_.stop(voices_[actor]);
            }
            voices_[actor] = 0;
        }
    }
    playing_ = volume_ = 0;
    for (const auto& candidate : selected) {
        const auto& input = *candidate.input;
        const auto& layer = layers_.at(input.car < 8 ? input.car : 4);
        auto& voice = voices_.at(input.actor);
        if (!voice || service_.playback_serial(voice) != serials_[input.actor] || !service_.playing(voice)) {
            voice = service_.play_original_volume(layer.sample, 0, candidate.volume, candidate.pan);
            serials_[input.actor] = voice ? service_.playback_serial(voice) : 0;
        }
        if (!voice) {
            break; // 4259f0 stops dispatching after allocation failure.
        }
        const auto& curve = layer.curve;
        const auto rpm = std::clamp(input.rpm, 2000, 8500);
        std::uint32_t rate = 11025;
        if (rpm >= curve.minimum_rpm && rpm <= curve.maximum_rpm) {
            const auto index = std::size_t(std::int64_t(rpm - curve.minimum_rpm) * std::int64_t(curve.rates.size() - 1) / (curve.maximum_rpm - curve.minimum_rpm));
            rate = curve.rates.at(index);
        }
        service_.controls_original_volume(voice, candidate.volume, float(rate) / float(layer.source_rate), candidate.pan);
        ++playing_;
        volume_ = std::max(volume_, candidate.volume);
    }
}

void Rivals::stop() {
    for (std::size_t actor = 0; actor < voices_.size(); ++actor) {
        if (voices_[actor] && service_.playback_serial(voices_[actor]) == serials_[actor]) {
            service_.stop(voices_[actor]);
        }
        voices_[actor] = 0;
    }
    playing_ = volume_ = 0;
}

std::uint32_t Rivals::playing_count() const {
    return playing_;
}

std::uint32_t Rivals::maximum_volume() const {
    return volume_;
}
} // namespace toca::audio
