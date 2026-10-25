#include "audio/engine.hpp"

#include "assets/store.hpp"
#include "audio/audio_tables.hpp"
#include "core/random.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace toca::audio {
namespace {
void validate(const EngineConfig& config) {
    if (config.car_id > 9 || config.weather < 1 || config.weather > 5 || config.camera_view > 5) {
        throw std::runtime_error("Engine requires original car0..9, resolved weather and camera0..5");
    }
}

EngineCurve curve(const builtin::RpmPitchCurve& source) {
    return {source.minimum_rpm, source.maximum_rpm, {source.rates_hz.begin(), source.rates_hz.end()}, {source.gains_miles.begin(), source.gains_miles.end()}};
}

EngineCurve curve(const builtin::SpeedPitchCurve& source) {
    return {source.minimum_speed_integer, source.maximum_speed_integer, {source.rates_hz.begin(), source.rates_hz.end()}, {source.gains_miles.begin(), source.gains_miles.end()}};
}

bool outside(const EngineConfig& config) {
    return config.camera_view == 2 || config.camera_view == 3;
}

bool whine_enabled(const EngineConfig& config) {
    return config.car_id < 8; // 14120 excludes original bonus actor pairs8/9.
}

std::int32_t curve_index(const EngineCurve& source, std::int32_t rpm) {
    if (rpm < source.minimum_rpm || rpm > source.maximum_rpm) {
        return -1;
    }
    return std::int32_t(((std::int64_t(rpm) - source.minimum_rpm) * std::int64_t(source.rates.size() - 1)) / (std::int64_t(source.maximum_rpm) - source.minimum_rpm));
}
} // namespace

EngineInputs engine_inputs(std::int32_t engine_speed_q16, std::int32_t rpm_scale_q16, std::int32_t throttle_q16, std::int32_t gear) {
    const auto values = source_engine_caller(engine_speed_q16, rpm_scale_q16, throttle_q16, gear, 0, 6);
    return {values.rpm, values.gear, values.throttle};
}

std::array<std::uint32_t, 3> engine_sample_indices(const EngineConfig& config) {
    validate(config);
    // 414b00 maps actor pairs. 44df6e puts either bonus car in pair8/9,
    // so the local Single Race caller retains Ford's sound4/16 for both.
    constexpr std::array<std::uint32_t, 10> specific{5, 6, 10, 11, 4, 7, 8, 9, 4, 4};
    const auto offset = outside(config) ? 12u : 0u;
    return {1 + offset, 2 + offset, specific[config.car_id] + offset};
}

EngineData load_engine_audio(Service& service, const AssetStore& store, const EngineConfig& config) {
    const auto indices = engine_sample_indices(config);
    const auto archive_name = (config.weather == 3 || config.weather == 5) ? "data/soundwet.bfp" : "data/sounddry.bfp";
    // 00424790 full-memory branch. The low-memory archives are not substituted.
    const auto archive = store.open_archive(archive_name);
    EngineData result;
    constexpr std::array tables{builtin::low_engine_curve, builtin::high_engine_curve, builtin::car_engine_curve};
    for (std::size_t layer = 0; layer < 3; ++layer) {
        auto& target = result.layers[layer];
        const auto member = archive.member(indices[layer]);
        target.sample = 0x100u + indices[layer];
        target.source_rate = service.load_sample(target.sample, member.bytes());
        target.curve = curve(tables[layer]);
    }
    if (whine_enabled(config)) {
        const auto member_index = outside(config) ? 15u : 3u;
        const auto member = archive.member(member_index);
        result.whine = {0x100 + member_index, 0, curve(builtin::transmission_whine_curve)};
        result.whine.source_rate = service.load_sample(result.whine.sample, member.bytes());
        result.shift_offsets = builtin::transmission_shift_offsets;
    }
    return result;
}

std::array<EngineLayer, 8> load_rival_engine_audio(Service& service, const AssetStore& store, std::uint32_t weather) {
    const auto archive = store.open_archive(weather == 3 || weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    std::array<EngineLayer, 8> result;
    for (std::uint32_t car = 0; car < result.size(); ++car) {
        const auto member_id = engine_sample_indices({car, weather, 2})[2];
        const auto member = archive.member(member_id);
        result[car] = {0x100 + member_id, 0, curve(builtin::car_engine_curve)};
        result[car].source_rate = service.load_sample(result[car].sample, member.bytes());
    }
    return result;
}

Engine::Engine(Service& service, const AssetStore& store, const EngineConfig& config)
    : Engine(service, config, load_engine_audio(service, store, config), load_engine_audio(service, store, {config.car_id, config.weather, outside(config) ? 1u : 2u})) {}

Engine::Engine(Service& service, const EngineConfig& config, EngineData data, std::optional<EngineData> alternate)
    : service_(service), config_(config), data_(std::move(data)), alternate_(std::move(alternate)) {
    validate(config_);
    const auto validate_layer = [](const EngineLayer& layer) {
        if (layer.source_rate == 0 || layer.curve.maximum_rpm <= layer.curve.minimum_rpm || layer.curve.rates.size() < 2 || layer.curve.rates.size() > 1025
            || layer.curve.rates.size() != layer.curve.gains.size() || std::ranges::any_of(layer.curve.rates, [](auto rate) {
                   return rate == 0;
               })) {
            throw std::runtime_error("Invalid engine layer metadata");
        }
    };
    for (const auto& layer : data_.layers) {
        validate_layer(layer);
    }
    if (whine_enabled(config_)) {
        validate_layer(data_.whine);
    }
    if (alternate_) {
        for (const auto& layer : alternate_->layers) {
            validate_layer(layer);
        }
        if (whine_enabled(config_)) {
            validate_layer(alternate_->whine);
        }
    }
}

bool Engine::set_camera(std::uint32_t view) {
    auto next = config_;
    next.camera_view = view;
    validate(next);
    const bool changed_bank = outside(next) != outside(config_);
    if (changed_bank) {
        if (!alternate_) {
            throw std::logic_error("Camera audio bank was not loaded");
        }
        const bool playing = started_;
        stop();
        std::swap(data_, *alternate_);
        if (playing) {
            start();
        }
    }
    config_ = next;
    return changed_bank;
}

std::uint32_t Engine::camera_view() const {
    return config_.camera_view;
}

Engine::~Engine() {
    try {
        stop();
    } catch (...) {
    } // Destructors cannot report backend shutdown errors.
}

void Engine::start() {
    if (started_) {
        return;
    }
    try {
        for (std::size_t index = 0; index < voices_.size(); ++index) {
            voices_[index] = service_.play_original_volume(data_.layers[index].sample, 0, 0);
        }
        if (whine_enabled(config_)) {
            whine_ = service_.play_original_volume(data_.whine.sample, 0, 0);
        }
        started_ = true;
        controls_ = {};
        whine_control_ = {};
    } catch (...) {
        stop();
        throw;
    }
}

void Engine::update(const EngineInputs& input, SourceRandom& random) {
    if (!started_) {
        return;
    }
    if (input.throttle_percent < 0 || input.throttle_percent > 100) {
        throw std::runtime_error("Engine throttle percent must be within0..100");
    }
    if (input.speed_integer < 0 || input.speed_integer > 260) {
        throw std::runtime_error("Engine audio requires original clamped speed0..260");
    }
    SourceEngineCall call;
    call.values = {input.rpm, input.speed_integer, input.gear, input.throttle_percent};
    call.bonus = !whine_enabled(config_);
    call.playing = whine_ != 0 && service_.playing(whine_);
    call.mix = config_.camera_view == 2 ? 0x4000 : config_.camera_view == 3 ? 0x3333 : 0x5999;
    (void)prepare_source_engine_update(source_state_, call);
    // 004145c0/00424410: View2 selects mix1 (4d3be0), View3 mix2 (4d3ba8).
    const auto base = outside(config_) ? config_.camera_view == 2 ? 0x5999 : 0x4ccc : 0x5999;
    const auto specific_gain = outside(config_) ? config_.camera_view == 2 ? 0x4000 : 0x3333 : 0x5999;
    for (std::size_t index = 0; index < voices_.size(); ++index) {
        auto& control = controls_[index];
        control = {};
        const auto& layer = data_.layers[index];
        const auto selected = curve_index(layer.curve, input.rpm);
        if (selected >= 0) {
            control.rate_hz = layer.curve.rates[std::size_t(selected)];
            auto volume = std::int32_t(layer.curve.gains[std::size_t(selected)]);
            if (index == 2) {
                volume = (volume * source_state_.throttle) / 100;
            }
            volume = std::int32_t((std::int64_t(volume) * (index == 2 ? specific_gain : base)) >> 16);
            if (index == 2 && source_state_.shift != 0) {
                volume = std::max(volume - 20, 0);
            }
            control.volume = std::uint32_t(volume);
        }
        if (voices_[index] != 0 && service_.playing(voices_[index])) {
            service_.controls_original_volume(voices_[index], std::min(control.volume, 127u), float(control.rate_hz) / float(layer.source_rate));
        }
    }
    const auto decision = advance_source_whine(
        source_state_, call, {data_.whine.curve.minimum_rpm, data_.whine.curve.maximum_rpm, data_.whine.curve.rates, data_.whine.curve.gains, data_.shift_offsets}, random);
    if (decision.rate_written) {
        whine_control_.rate_hz = decision.rate;
    }
    if (decision.volume_written) {
        whine_control_.volume = std::uint32_t(decision.volume);
    }
    if (decision.rate_written || decision.volume_written) {
        service_.controls_original_volume(whine_, whine_control_.volume, float(whine_control_.rate_hz) / float(data_.whine.source_rate));
    }
}

void Engine::stop() {
    for (auto& voice : voices_) {
        if (voice != 0) {
            service_.stop(voice);
            voice = 0;
        }
    }
    if (whine_ != 0) {
        service_.stop(whine_);
        whine_ = 0;
    }
    started_ = false;
    controls_ = {};
    whine_control_ = {};
}

bool Engine::active() const {
    return started_ && (whine_active() || std::ranges::any_of(voices_, [&](Voice voice) {
        return voice != 0 && service_.playing(voice);
    }));
}

std::uint32_t Engine::shift_ticks() const {
    return std::uint32_t(source_state_.shift);
}

const SourceEngineState& Engine::source_state() const {
    return source_state_;
}

const std::array<EngineControl, 3>& Engine::last_controls() const {
    return controls_;
}

const EngineControl& Engine::whine_control() const {
    return whine_control_;
}

bool Engine::whine_active() const {
    return started_ && whine_ != 0 && service_.playing(whine_);
}
} // namespace toca::audio
