#pragma once

#include "audio/engine_state.hpp"
#include "audio/service.hpp"

#include <array>
#include <optional>

namespace toca {
class AssetStore;
class SourceRandom;
} // namespace toca

namespace toca::audio {
struct EngineConfig {
    std::uint32_t car_id = 0;
    std::uint32_t weather = 2;
    std::uint32_t camera_view = 2;
};

struct EngineInputs {
    std::int32_t rpm = 2000;
    std::int32_t gear = 0;
    std::int32_t throttle_percent = 0;
    std::int32_t speed_integer = 0;
};

struct EngineCurve {
    std::int32_t minimum_rpm = 0, maximum_rpm = 0;
    std::vector<std::uint16_t> rates;
    std::vector<std::uint8_t> gains;
};

struct EngineLayer {
    std::uint32_t sample = 0, source_rate = 0;
    EngineCurve curve;
};

struct EngineData {
    std::array<EngineLayer, 3> layers;
    EngineLayer whine;
    std::array<std::int32_t, 24> shift_offsets{};
};

struct EngineControl {
    std::uint32_t rate_hz = 11025;
    std::uint32_t volume = 0;
};

// 0044a6f0's raw fixed-state conversion, without substituting a rev model.
EngineInputs engine_inputs(std::int32_t engine_speed_q16, std::int32_t rpm_scale_q16, std::int32_t throttle_q16, std::int32_t gear);

// Native sample index in the original 84-member SOUND archive.
std::array<std::uint32_t, 3> engine_sample_indices(const EngineConfig& config);
EngineData load_engine_audio(Service& service, const AssetStore& store, const EngineConfig& config);
// 414d10/4141f0: one external car-specific layer per opponent.
std::array<EngineLayer, 8> load_rival_engine_audio(Service& service, const AssetStore& store, std::uint32_t weather);

// One local player's three motor layers and original WHINE/shift state.
// Service must outlive Engine; stop before clearing samples.
class Engine {
    Service& service_;
    EngineConfig config_;
    EngineData data_;
    std::optional<EngineData> alternate_;
    std::array<Voice, 3> voices_{};
    std::array<EngineControl, 3> controls_{};
    Voice whine_ = 0;
    EngineControl whine_control_{};
    SourceEngineState source_state_;
    bool started_ = false;

public:
    Engine(Service& service, const AssetStore& store, const EngineConfig& config);
    Engine(Service& service, const EngineConfig& config, EngineData data, std::optional<EngineData> alternate = std::nullopt);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void start();
    // Switch original inside/outside banks while retaining throttle/shift state.
    bool set_camera(std::uint32_t view);
    std::uint32_t camera_view() const;
    void update(const EngineInputs& input, SourceRandom& random);
    void stop();
    bool active() const;
    std::uint32_t shift_ticks() const;
    const SourceEngineState& source_state() const;
    const std::array<EngineControl, 3>& last_controls() const;
    const EngineControl& whine_control() const;
    bool whine_active() const;
};
} // namespace toca::audio
