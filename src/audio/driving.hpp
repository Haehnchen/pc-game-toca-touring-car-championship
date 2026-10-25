#pragma once

#include "audio/engine.hpp"
#include "audio/material_state.hpp"
#include "audio/skid.hpp"
#include "audio/spatial.hpp"
#include "audio/wheel_queue.hpp"

namespace toca {
class SourceRandom;
}

namespace toca::audio {
struct DrivingWheel {
    std::array<std::int32_t, 3> position{};
    bool rumbling = false;
    std::int32_t strength_q16 = 0;
    bool airborne = false;
    std::uint8_t surface = 0;
    std::int32_t longitudinal_slip_q16 = 0, lateral_slip_q16 = 0;
};

struct DrivingInputs {
    std::int32_t speed_integer = 0;
    std::uint32_t shift_ticks = 0;
    bool local_player = true;
    bool showcase = false;
    SpatialListener listener{};
    std::array<DrivingWheel, 4> wheels;
    SourceWheelQueue skid_queue;
    SourceRandom* random = nullptr;
};

struct DrivingData {
    EngineLayer wind;
    std::array<std::uint32_t, 8> rumble_samples{}; // Original members56..63.
    std::array<std::uint32_t, 9> skid_samples{}; // Members39..44,47..49.
    std::array<std::uint32_t, 3> rolling_samples{}; // Grass67, gravel68, wet81.
};

DrivingData load_driving_audio(Service& service, const AssetStore& store, const EngineConfig& config);
// 00423fb0/00424c70: integer-XZ distance,40-unit cutoff and fixed fade.
std::uint32_t driving_distance_volume(const std::array<std::int32_t, 3>& source, const std::array<std::int32_t, 3>& listener);

class Driving {
    Service& service_;
    EngineConfig config_;
    DrivingData data_;
    Voice wind_ = 0, rumble_ = 0, skid_ = 0, rolling_ = 0, tail_ = 0;
    std::uint8_t rumble_mask_ = 0;
    SourceSkidState skid_state_;
    SourceCameraMix camera_mix_;
    std::array<std::uint8_t, 4> wheel_surfaces_{};
    bool started_ = false, local_wind_ = true;
    EngineControl wind_control_{};
    std::uint32_t rumble_sample_ = 0, rumble_volume_ = 0;
    std::uint32_t skid_sample_ = 0, skid_volume_ = 0, rolling_sample_ = 0, rolling_volume_ = 0;
    std::uint32_t tail_volume_ = 0;
    std::uint32_t rumble_pan_ = 64, skid_pan_ = 64, rolling_pan_ = 64, tail_pan_ = 64;

    void update_surfaces(const DrivingInputs& input);
    void update_skid(const DrivingInputs& input);
    void release(Voice& voice, std::uint32_t volume, std::uint32_t pan, bool fade);
    void update_tail();

public:
    Driving(Service& service, const AssetStore& store, const EngineConfig& config);
    Driving(Service& service, const EngineConfig& config, DrivingData data);
    ~Driving();
    Driving(const Driving&) = delete;
    Driving& operator=(const Driving&) = delete;
    //The original local player owns one WHIRR channel. Additional physical
    //actors dispatch wheel audio without allocating another local channel.
    void start(bool local_wind = true);
    void set_camera(std::uint32_t view, bool restart_wind = false, bool trackside = false);
    void update(const DrivingInputs& input);
    void update_contacts(const DrivingInputs& input);
    void update_wind(const DrivingInputs& input);
    //425400/4251f0 reject non-human actors before material/rumble updates.
    //Their44c540 skid channel keeps4244f0's initialized material state.
    void update_queued_skids(const DrivingInputs& input);
    void settle_skid_mute(std::int32_t& pending);
    void stop();
    const EngineControl& wind_control() const;
    std::uint32_t rumble_sample() const;
    std::uint32_t rumble_volume() const;
    std::uint32_t skid_sample() const;
    std::uint32_t skid_volume() const;
    bool skid_playing() const;
    std::uint32_t rolling_sample() const;
    std::uint32_t rolling_volume() const;
    std::uint32_t tail_volume() const;
    std::uint32_t rumble_pan() const;
    std::uint32_t skid_pan() const;
    std::uint32_t rolling_pan() const;
    std::uint32_t tail_pan() const;
};
} // namespace toca::audio
