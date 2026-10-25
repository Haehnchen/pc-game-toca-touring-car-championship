#include "audio/scrape.hpp"

#include "assets/store.hpp"
#include "audio/contact_state.hpp"
#include "core/random.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::audio {
namespace {
using namespace simulation::fixed;

ScrapeData load(Service& service, const AssetStore& store, const EngineConfig& config) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    ScrapeData data{0x300 + 50, config.camera_view == 2 ? 0xa666 : config.camera_view == 0 || config.camera_view == 3 ? 0x9999 : one};
    service.load_sample(data.sample, archive.member(50).bytes());
    return data;
}

} // namespace

Scrapes::Scrapes(Service& service, const AssetStore& store, const EngineConfig& config) : Scrapes(service, load(service, store, config)) {}

Scrapes::Scrapes(Service& service, ScrapeData data) : service_(service), data_(data) {
    if (data.gain_q16 < 0 || data.gain_q16 > one) {
        throw std::runtime_error("Invalid source wall scrape camera gain");
    }
}

Scrapes::~Scrapes() {
    stop();
}

bool Scrapes::playing(std::int8_t index) const {
    if (index < 0) {
        return false;
    }
    const auto& handle = handles_.at(std::size_t(index));
    return handle.voice && service_.playback_serial(handle.voice) == handle.serial && service_.playing(handle.voice);
}

std::int8_t Scrapes::allocate() const {
    for (std::int8_t index = 0; index < 4; ++index) {
        if (!playing(index)) {
            return index;
        }
    }
    return -1;
}

void Scrapes::release() {
    if (playing(wall_)) {
        service_.stop(handles_[std::size_t(wall_)].voice);
    }
    wall_ = -1;
    volume_ = 0;
    pan_ = 64;
}

void Scrapes::event(std::int8_t index, std::uint32_t kind, const Vector& position, std::int32_t strength, const SpatialListener& listener, SourceRandom& random) {
    if (!started_ || index < 0) {
        return;
    }
    auto& handle = handles_.at(std::size_t(index));
    SourceCameraMix camera;
    SourceContactGains gains;
    gains.local.fill(data_.gain_q16);
    const std::array listeners{SourceContactListener{listener}};
    const auto decision = source_scrape_event({position, index, strength, kind, true, playing(index)}, listeners, camera, gains, random);
    if (decision.action == SourceScrapeAction::stop) {
        service_.stop(handle.voice);
    } else if (decision.action == SourceScrapeAction::play) {
        handle.voice = service_.play_original_volume(data_.sample, 0, std::uint32_t(decision.volume), decision.pan);
        if (handle.voice) {
            handle.serial = service_.playback_serial(handle.voice);
            ++starts_;
        }
    } else if (decision.action == SourceScrapeAction::update) {
        service_.controls_original_volume(handle.voice, std::uint32_t(decision.volume), 1, decision.pan);
    }
    if ((decision.action == SourceScrapeAction::play || decision.action == SourceScrapeAction::update) && index == wall_ && playing(index)) {
        volume_ = std::uint32_t(decision.volume);
        pan_ = decision.pan;
    }
}

bool Scrapes::pair_handle(std::uint32_t first, std::uint32_t second) const {
    return pairs_.at(first * 16 + second) >= 0;
}

void Scrapes::reset_pair(std::uint32_t first, std::uint32_t second) {
    pairs_.at(first * 16 + second) = pairs_.at(second * 16 + first) = -1;
}

void Scrapes::pair_event(
    std::uint32_t first, std::uint32_t second, std::uint32_t kind, const Vector& position, std::int32_t strength, const SpatialListener& listener, SourceRandom& random) {
    if (!started_) {
        return;
    }
    auto& index = pairs_.at(first * 16 + second);
    if (kind == 10 && index < 0) {
        index = allocate();
        pairs_.at(second * 16 + first) = index;
    }
    event(index, kind, position, strength, listener, random);
}

void Scrapes::start() {
    started_ = true;
}

void Scrapes::set_camera(std::uint32_t view) {
    (void)engine_sample_indices({.camera_view = view});
    data_.gain_q16 = view == 2 ? 0xa666 : view == 0 || view == 3 ? 0x9999 : one;
}

void Scrapes::update(const Vector& position, std::int32_t strength, const SpatialListener& listener, SourceRandom& random) {
    if (strength < 0 || strength > one) {
        throw std::runtime_error("Wall scrape requires source-bounded tangential speed");
    }
    if (!started_) {
        return;
    }
    release();
    if (strength == 0) {
        return;
    }
    wall_ = allocate();
    event(wall_, 10, position, strength, listener, random);
}

void Scrapes::reset_pairs() {
    pairs_.fill(-1);
}

void Scrapes::stop() {
    for (std::int8_t index = 0; index < 4; ++index) {
        if (playing(index)) {
            service_.stop(handles_[std::size_t(index)].voice);
        }
    }
    release();
    started_ = false;
}

bool Scrapes::playing() const {
    return playing(wall_);
}

std::uint64_t Scrapes::played_count() const {
    return starts_;
}

std::uint32_t Scrapes::volume() const {
    return volume_;
}

std::uint32_t Scrapes::pan() const {
    return pan_;
}
} // namespace toca::audio
