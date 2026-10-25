#include "audio/impact.hpp"

#include "assets/store.hpp"
#include "audio/contact_state.hpp"
#include "core/random.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace toca::audio {
namespace {
constexpr std::array<std::uint32_t, 24> members{25, 26, 27, 28, 29, 30, 33, 34, 35, 36, 45, 65, 66, 31, 32, 37, 38, 46, 51, 52, 53, 54, 55, 64};

ImpactControl contact_control(
    const ImpactEvent& event, std::span<const SourceContactListener> listeners, const ImpactData& data, const SourceContactGains& gains, SourceRandom* random) {
    if (((event.kind < 1 || event.kind > 6) && (event.kind < 0x11 || event.kind > 0x15)) || event.strength_q16 > 65536) {
        throw std::runtime_error("Impact audio requires an original collision/prop event and source-bounded strength");
    }
    SourceCameraMix camera;
    const auto decision = source_impact_event({event.position_q16, event.kind, event.strength_q16, event.local_player}, listeners, camera, gains, random);
    if (decision.volume <= 0) {
        return {};
    }
    const auto found = std::find(members.begin(), members.end(), std::uint32_t(decision.sample));
    return {data.samples[std::size_t(found - members.begin())], std::uint32_t(decision.volume), decision.pan};
}

} // namespace

ImpactData load_impact_audio(Service& service, const AssetStore& store, const EngineConfig& config) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    ImpactData result;
    // 004244f0 initializes these runtime mixes; they are not PE data tables.
    if (config.camera_view == 0 || config.camera_view == 3) {
        result.local_gain_q16 = 0x9999;
        result.remote_gain_q16 = 0x6666;
    } else if (config.camera_view != 2) {
        result.local_gain_q16 = 0x10000;
        result.remote_gain_q16 = 0;
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        result.samples[index] = 0x300 + members[index];
        service.load_sample(result.samples[index], archive.member(members[index]).bytes());
    }
    return result;
}

void Impact::set_camera(std::uint32_t view) {
    (void)engine_sample_indices({.camera_view = view});
    data_.local_gain_q16 = view == 2 ? 0xa666 : view == 0 || view == 3 ? 0x9999 : 0x10000;
    data_.remote_gain_q16 = view == 2 ? 0x7333 : view == 0 || view == 3 ? 0x6666 : 0;
}

ImpactControl impact_control(const ImpactEvent& event, const SpatialListener& listener, const ImpactData& data, SourceRandom* random) {
    SourceContactGains gains;
    gains.local.fill(data.local_gain_q16);
    gains.remote.fill(data.remote_gain_q16);
    const std::array listeners{SourceContactListener{listener}};
    return contact_control(event, listeners, data, gains, random);
}

ImpactControl impact_control_views(const ImpactEvent& event, std::span<const SourceContactListener> listeners, const ImpactData& data, SourceRandom* random) {
    return contact_control(event, listeners, data, {}, random);
}

Impact::Impact(Service& service, const AssetStore& store, const EngineConfig& config) : Impact(service, load_impact_audio(service, store, config)) {}

Impact::Impact(Service& service, ImpactData data) : service_(service), data_(std::move(data)) {
    if (data_.local_gain_q16 < 0 || data_.local_gain_q16 > 65536 || data_.remote_gain_q16 < 0 || data_.remote_gain_q16 > 65536) {
        throw std::runtime_error("Invalid original impact camera gain");
    }
}

Impact::~Impact() {
    stop();
}

void Impact::start() {
    started_ = true;
}

void Impact::update(const ImpactEvent& event, const SpatialListener& listener, SourceRandom* random) {
    if (!started_) {
        return;
    }
    play(impact_control(event, listener, data_, random));
}

void Impact::update_views(const ImpactEvent& event, std::span<const SourceContactListener> listeners, SourceRandom* random) {
    if (!started_) {
        return;
    }
    play(impact_control_views(event, listeners, data_, random));
}

void Impact::play(const ImpactControl& control) {
    if (control.volume == 0) {
        return;
    }
    // 00424cd0 scans first-free handles; native split screen extends the pool.
    const auto voice = service_.play_original_volume(control.sample, 1, control.volume, control.pan);
    if (voice != 0) {
        serials_[voice - 1] = service_.playback_serial(voice);
        ++played_count_;
        last_ = control;
    }
}

void Impact::stop() {
    for (std::size_t index = 0; index < serials_.size(); ++index) {
        const auto voice = Voice(index + 1);
        if (serials_[index] != 0 && service_.playback_serial(voice) == serials_[index] && service_.playing(voice)) {
            service_.stop(voice);
        }
        serials_[index] = 0;
    }
    started_ = false;
}

std::uint64_t Impact::played_count() const {
    return played_count_;
}

std::uint32_t Impact::last_sample() const {
    return last_.sample == 0 ? 0u : last_.sample - 0x300;
}

std::uint32_t Impact::last_volume() const {
    return last_.volume;
}

std::uint32_t Impact::last_pan() const {
    return last_.pan;
}
} // namespace toca::audio
