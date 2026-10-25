#include "audio/driving.hpp"

#include "assets/store.hpp"
#include "audio/audio_tables.hpp"
#include "core/random.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace toca::audio {
namespace {
std::int32_t multiply(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::uint32_t(std::uint64_t(std::int64_t(first) * second) >> 16));
}

bool wind_enabled(const EngineConfig& config) {
    return config.weather != 3 && config.weather != 5;
}

std::int32_t motor_gain(const EngineConfig& config) {
    return config.camera_view == 2 ? 0x4000 : config.camera_view == 3 ? 0x3333 : 0x5999;
}

std::int32_t surface_gain(const SourceCameraMix& mix, std::size_t index) {
    // 4244f0/4257d0/424410: outside, View2 and View0/3 mixes.
    constexpr std::array<std::array<std::int32_t, 14>, 3> mixes{{
        {0x5999, 0x5999, 0x4ccc, 0x8000, 0x10000, 0, 0, 0xf333, 0, 0xf333, 0, 0xe666, 0, 0x9999},
        {0x5999, 0x4000, 0x6666, 0x7333, 0xa666, 0x4ccc, 0x7333, 0, 0xcccc, 0, 0xb333, 0x9999, 0xa666, 0x6666},
        {0x4ccc, 0x3333, 0x5999, 0x6666, 0x9999, 0x4000, 0x6666, 0, 0xc000, 0, 0xa666, 0x8000, 0x8ccc, 0x4ccc},
    }};
    return mix.primary != 0 ? mixes[0][index] : mix.secondary == 1 ? mixes[1][index] : mix.secondary == 2 ? mixes[2][index] : 0;
}

void pan_controls(Service& service, Voice voice, std::uint32_t volume, std::uint32_t pan) {
    service.controls_original_volume(voice, volume, 1, pan);
}
} // namespace

DrivingData load_driving_audio(Service& service, const AssetStore& store, const EngineConfig& config) {
    (void)engine_sample_indices(config); // Same bounded normal-car/weather/camera contract.
    const auto archive = store.open_archive(wind_enabled(config) ? "data/sounddry.bfp" : "data/soundwet.bfp");
    DrivingData result;
    if (wind_enabled(config)) {
        auto& wind = result.wind;
        wind.sample = 0x200 + 24;
        wind.source_rate = service.load_sample(wind.sample, archive.member(24).bytes());
        const auto& source = builtin::wind_curve;
        wind.curve.minimum_rpm = source.minimum_speed_integer;
        wind.curve.maximum_rpm = source.maximum_speed_integer;
        wind.curve.rates.assign(source.rates_hz.begin(), source.rates_hz.end());
        wind.curve.gains.assign(source.gains_miles.begin(), source.gains_miles.end());
    }
    for (std::uint32_t index = 0; index < result.rumble_samples.size(); ++index) {
        const auto member = 56 + index;
        result.rumble_samples[index] = 0x200 + member;
        service.load_sample(result.rumble_samples[index], archive.member(member).bytes());
    }
    for (std::uint32_t index = 0; index < result.skid_samples.size(); ++index) {
        const auto member = index < 6 ? 39 + index : 41 + index;
        result.skid_samples[index] = 0x200 + member;
        service.load_sample(result.skid_samples[index], archive.member(member).bytes());
    }
    constexpr std::array<std::uint32_t, 3> rolling_members{67, 68, 81};
    for (std::size_t index = 0; index < rolling_members.size(); ++index) {
        const auto member = rolling_members[index];
        result.rolling_samples[index] = 0x200 + member;
        service.load_sample(result.rolling_samples[index], archive.member(member).bytes());
    }
    return result;
}

std::uint32_t driving_distance_volume(const std::array<std::int32_t, 3>& source, const std::array<std::int32_t, 3>& listener) {
    return std::uint32_t(source_spatial(source, {listener}).fade_q16 >> 16);
}

Driving::Driving(Service& service, const AssetStore& store, const EngineConfig& config) : Driving(service, config, load_driving_audio(service, store, config)) {}

Driving::Driving(Service& service, const EngineConfig& config, DrivingData data) : service_(service), config_(config), data_(std::move(data)) {
    (void)engine_sample_indices(config_);
    if (wind_enabled(config_)) {
        const auto& wind = data_.wind;
        if (wind.source_rate == 0 || wind.curve.minimum_rpm >= wind.curve.maximum_rpm || wind.curve.rates.size() < 2 || wind.curve.rates.size() > 1025
            || wind.curve.rates.size() != wind.curve.gains.size() || std::ranges::any_of(wind.curve.rates, [](auto rate_value) {
                   return rate_value == 0;
               })) {
            throw std::runtime_error("Invalid driving wind metadata");
        }
    }
}

Driving::~Driving() {
    try {
        stop();
    } catch (...) {
    }
}

void Driving::start(bool local_wind) {
    if (started_) {
        return;
    }
    local_wind_ = local_wind;
    if (local_wind_ && wind_enabled(config_)) {
        wind_ = service_.play_original_volume(data_.wind.sample, 0, 0);
    }
    started_ = true;
}

void Driving::set_camera(std::uint32_t view, bool restart_wind, bool trackside) {
    auto next = config_;
    next.camera_view = trackside ? 3u : view;
    (void)engine_sample_indices(next);
    config_ = next;
    // 4145c0 restarts WHIRR with the motor bank; contact voices keep their state.
    if (restart_wind && started_ && local_wind_ && wind_enabled(config_)) {
        if (wind_) {
            service_.stop(wind_);
        }
        wind_ = service_.play_original_volume(data_.wind.sample, 0, 0);
    }
}

void Driving::update(const DrivingInputs& input) {
    update_wind(input);
    update_contacts(input);
    update_queued_skids(input);
}

void Driving::update_wind(const DrivingInputs& input) {
    if (!started_) {
        return;
    }
    if (input.shift_ticks > 2) {
        throw std::runtime_error("Driving audio requires the current engine shift counter0..2");
    }
    wind_control_ = {};
    if (wind_ != 0 && service_.playing(wind_)) {
        const auto& curve = data_.wind.curve;
        if (input.speed_integer >= curve.minimum_rpm) {
            const auto speed = std::min(input.speed_integer, curve.maximum_rpm);
            const auto index =
                std::size_t(((std::int64_t(speed) - curve.minimum_rpm) * std::int64_t(curve.rates.size() - 1)) / (std::int64_t(curve.maximum_rpm) - curve.minimum_rpm));
            const auto shift_percent = input.shift_ticks == 2 ? 80 : input.shift_ticks == 1 ? 90 : 100;
            wind_control_.rate_hz = curve.rates[index];
            wind_control_.volume = std::uint32_t((std::int32_t(curve.gains[index]) * ((shift_percent * motor_gain(config_)) / 100)) >> 16);
        }
        service_.controls_original_volume(wind_, std::min(wind_control_.volume, 127u), float(wind_control_.rate_hz) / float(data_.wind.source_rate));
    }
}

void Driving::update_contacts(const DrivingInputs& input) {
    if (!started_ || !input.local_player) {
        return;
    }
    select_source_camera_mix(camera_mix_, std::int32_t(config_.camera_view), input.showcase);
    for (const auto& wheel : input.wheels) {
        if (wheel.surface > 3) {
            throw std::runtime_error("Driving wheel requires original surface0..3");
        }
    }
    // 0040fa..fb sends wheel events3→0; 004251f0 shares one local handle/mask.
    for (std::size_t index = input.wheels.size(); index-- > 0;) {
        const auto& wheel = input.wheels[index];
        const auto bit = std::uint8_t(1u << index);
        if (wheel.strength_q16 < 0 || wheel.strength_q16 > 65536) {
            throw std::runtime_error("Driving wheel strength must be original clamped Q16");
        }
        const auto previous = (rumble_mask_ & bit) != 0;
        if (!wheel.rumbling) {
            rumble_mask_ &= std::uint8_t(~bit);
            if (previous && rumble_mask_ == 0 && rumble_ != 0) {
                service_.stop(rumble_);
                rumble_ = 0;
                rumble_volume_ = 0;
            }
            continue;
        }
        const auto starting = rumble_mask_ == 0;
        rumble_mask_ |= bit;
        if (!previous && !starting) {
            continue;
        }
        const auto spatial = source_spatial(wheel.position, input.listener);
        const auto distance = std::uint32_t(spatial.fade_q16 >> 16);
        if (distance == 0) {
            continue; // Original failed spatial query leaves prior controls.
        }
        auto volume = std::int32_t((std::int64_t(distance) * surface_gain(camera_mix_, 11)) >> 16);
        volume = std::min(std::int32_t((std::int64_t(volume) * wheel.strength_q16 * 2) >> 16), 100);
        const auto strength_mask = (std::uint32_t(wheel.strength_q16) * 25u & 0x3fffc000u) * 4u;
        if (std::bit_cast<std::int32_t>(strength_mask) < 65537) {
            volume = 0;
        }
        if (starting) {
            const auto adjusted = wheel.strength_q16 < 0x199a ? 0x2666 : std::min(multiply(wheel.strength_q16, 0x19999), 65536);
            rumble_sample_ = 64u - std::uint32_t((adjusted * 8) >> 16);
            rumble_ = service_.play_original_volume(data_.rumble_samples.at(rumble_sample_ - 56), 0, std::uint32_t(volume), spatial.pan);
        } else if (rumble_ != 0 && service_.playing(rumble_)) {
            pan_controls(service_, rumble_, std::uint32_t(volume), spatial.pan);
        }
        if (rumble_ != 0) {
            rumble_volume_ = std::uint32_t(volume);
            rumble_pan_ = spatial.pan;
        }
    }
    update_surfaces(input);
}

void Driving::release(Voice& voice, std::uint32_t volume, std::uint32_t pan, bool fade) {
    if (voice == 0) {
        return;
    }
    if (!fade) {
        service_.stop(voice);
    } else {
        // 425ef0: one displaced tail per car; replacing it ends the old tail.
        if (tail_ != 0) {
            service_.stop(tail_);
        }
        tail_ = voice;
        tail_volume_ = volume;
        tail_pan_ = pan;
    }
    voice = 0;
}

void Driving::update_tail() {
    if (tail_ == 0) {
        return;
    }
    // 426110: exact integer decay at the audio dispatch, no wall-clock timer.
    tail_volume_ = (tail_volume_ * 0xe666u) >> 16;
    if (tail_volume_ < 2) {
        service_.stop(tail_);
        tail_ = 0;
        tail_volume_ = 0;
    } else {
        // 426110 changes only volume: preserve the displaced voice's pan.
        pan_controls(service_, tail_, tail_volume_, tail_pan_);
    }
}

void Driving::update_surfaces(const DrivingInputs& input) {
    // 425400 is called in order3..0. Curb1 and airborne16 retain each
    // wheel's prior rolling surface; the discrete curb loop is separate.
    const auto wet = !wind_enabled(config_);
    for (std::size_t index = input.wheels.size(); index-- > 0;) {
        const auto& wheel = input.wheels[index];
        const auto decision = advance_source_material(skid_state_, wheel_surfaces_, std::uint32_t(index), wheel.airborne ? 16u : wheel.surface, true, input.local_player, wet,
            camera_mix_, std::int32_t(config_.camera_view), input.showcase);
        if (!decision.admitted) {
            continue;
        }
        const auto current = decision.current;
        if (decision.changed) {
            release(rolling_, rolling_volume_, rolling_pan_, decision.fade);
            rolling_volume_ = 0;
        }
        if (!wet && (current == 0 || skid_state_.surface == 0)) {
            continue;
        }
        const auto gain = surface_gain(camera_mix_, skid_state_.surface == 2 ? 7u : skid_state_.surface == 3 ? 9u : 12u);
        if (gain == 0) {
            if (rolling_ != 0) {
                service_.stop(rolling_);
                rolling_ = 0;
            }
            rolling_volume_ = 0;
            continue;
        }
        const auto base = (gain * 100) >> 16;
        rolling_volume_ = std::uint32_t(std::min((std::int64_t(base) * wheel.strength_q16 * (skid_state_.surface == 0 ? 1 : 2)) >> 16, INT64_C(100)));
        rolling_sample_ = skid_state_.surface == 2 ? 67 : skid_state_.surface == 3 ? 68 : 81;
        // 425400 never queries source/camera position. After the current wheel
        // material write, the dominant material's left/right occupancy sets pan.
        const bool left = wheel_surfaces_[0] == skid_state_.surface || wheel_surfaces_[2] == skid_state_.surface;
        const bool right = wheel_surfaces_[1] == skid_state_.surface || wheel_surfaces_[3] == skid_state_.surface;
        const auto pan = !input.listener.stereo ? 64u : left ? (right ? 64u : 32u) : 96u;
        if (rolling_ == 0 || !service_.playing(rolling_)) {
            rolling_ = service_.play_original_volume(data_.rolling_samples[skid_state_.surface == 2 ? 0 : skid_state_.surface == 3 ? 1 : 2], 0, rolling_volume_, pan);
        } else {
            pan_controls(service_, rolling_, rolling_volume_, pan);
        }
        if (rolling_ != 0) {
            rolling_pan_ = pan;
        }
    }
}

void Driving::update_skid(const DrivingInputs& input) {
    //44c540 consumes the retained view queue with current wheel positions.
    for (std::size_t index = 0; index < input.wheels.size(); ++index) {
        const auto& wheel = input.wheels[index];
        const auto kind = input.skid_queue.events[index];
        if (kind == -1) {
            continue;
        }
        const auto decision = source_skid_event(skid_state_, kind, std::uint32_t(index), input.local_player, true, input.random);
        if (decision.action == SourceSkidAction::stop && skid_ != 0) {
            release(skid_, skid_volume_, skid_pan_, skid_state_.surface == 3);
            skid_volume_ = 0;
        }
        if (decision.action != SourceSkidAction::update) {
            continue;
        }
        const auto sample = std::uint32_t(decision.sample);
        const auto strength = input.skid_queue.strengths[index];
        const auto source = source_spatial(wheel.position, input.listener);
        if (source.fade_q16 == 0) {
            continue;
        }
        // 424c10 applies strength*1.2 to the fixed distance fade BEFORE
        // truncation to the integer volume used by424d50.
        const auto spatial = std::min(multiply(source.fade_q16, multiply(strength, 0x13333)) >> 16, 100);
        if (spatial == 0) {
            continue; // 424b50 rejects a zero INTEGER result before gain/pan.
        }
        const auto gain = surface_gain(camera_mix_, skid_state_.surface == 0 ? (input.local_player ? 3u : 5u) : skid_state_.surface == 2 ? 8u : 10u);
        const auto previous_volume = skid_volume_;
        skid_volume_ = std::uint32_t((spatial * gain) >> 16);
        if (gain == 0) {
            if (skid_ != 0) {
                service_.stop(skid_);
                skid_ = 0;
            }
            continue;
        }
        if (decision.restart && skid_ != 0) {
            release(skid_, previous_volume, skid_pan_, true);
        }
        if (decision.restart && skid_ == 0 && gain != 0) {
            skid_sample_ = sample;
            skid_ = service_.play_original_volume(data_.skid_samples[sample < 45 ? sample - 39 : sample - 41], 0, skid_volume_, source.pan);
        } else if (skid_ != 0 && service_.playing(skid_)) {
            pan_controls(service_, skid_, skid_volume_, source.pan);
        }
        if (skid_ != 0) {
            skid_pan_ = source.pan;
        }
    }
}

void Driving::settle_skid_mute(std::int32_t& pending) {
    if (settle_source_skid_mute(pending, skid_state_)) {
        if (skid_ != 0) {
            service_.stop(skid_);
            skid_ = 0;
        }
        skid_volume_ = 0;
    }
}

void Driving::update_queued_skids(const DrivingInputs& input) {
    if (started_) {
        select_source_camera_mix(camera_mix_, std::int32_t(config_.camera_view), input.showcase);
        update_skid(input);
        update_tail();
    }
}

void Driving::stop() {
    if (wind_ != 0) {
        service_.stop(wind_);
        wind_ = 0;
    }
    if (rumble_ != 0) {
        service_.stop(rumble_);
        rumble_ = 0;
    }
    if (skid_ != 0) {
        service_.stop(skid_);
        skid_ = 0;
    }
    if (rolling_ != 0) {
        service_.stop(rolling_);
        rolling_ = 0;
    }
    if (tail_ != 0) {
        service_.stop(tail_);
        tail_ = 0;
    }
    tail_volume_ = 0;
    skid_state_.mask = 0;
    wheel_surfaces_ = {};
    skid_state_.surface = 0;
    skid_state_.reset = 0;
    camera_mix_ = {};
    skid_volume_ = rolling_volume_ = 0;
    rumble_mask_ = 0;
    rumble_volume_ = 0;
    wind_control_ = {};
    rumble_pan_ = skid_pan_ = rolling_pan_ = tail_pan_ = 64;
    started_ = false;
}

const EngineControl& Driving::wind_control() const {
    return wind_control_;
}

std::uint32_t Driving::rumble_sample() const {
    return rumble_sample_;
}

std::uint32_t Driving::rumble_volume() const {
    return rumble_volume_;
}

std::uint32_t Driving::skid_sample() const {
    return skid_sample_;
}

std::uint32_t Driving::skid_volume() const {
    return skid_volume_;
}

bool Driving::skid_playing() const {
    return skid_ != 0 && service_.playing(skid_);
}

std::uint32_t Driving::rolling_sample() const {
    return rolling_sample_;
}

std::uint32_t Driving::rolling_volume() const {
    return rolling_volume_;
}

std::uint32_t Driving::tail_volume() const {
    return tail_volume_;
}

std::uint32_t Driving::rumble_pan() const {
    return rumble_pan_;
}

std::uint32_t Driving::skid_pan() const {
    return skid_pan_;
}

std::uint32_t Driving::rolling_pan() const {
    return rolling_pan_;
}

std::uint32_t Driving::tail_pan() const {
    return tail_pan_;
}
} // namespace toca::audio
