#pragma once

#include "simulation/session.hpp"
#include "simulation/time.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toca::simulation {
// Object-to-world rows match scene::BodyPose::basis; translation is original Q16.
// This presentation-only actor never enters Motion, contact, damage or audio.
struct GhostPose {
    std::array<std::int32_t, 3> position_q16{};
    std::array<std::array<std::int32_t, 3>, 3> basis_q16{};
};

class TimeTrial {
    struct Sample {
        std::array<std::int16_t, 6> basis_q14{};
        std::array<std::int32_t, 3> position_q16{};
        std::uint32_t milliseconds = 0;
    };

    std::uint32_t track_, car_, weather_, best_car_ = 0, best_weather_ = 0;
    std::string driver_, best_driver_;
    std::uint32_t best_ms_ = 999999;
    std::uint32_t saved_ms_ = 999999;
    std::uint64_t now_ms_ = 0, lap_start_ms_ = 0;
    bool recording_ = false, dirty_ = false;
    std::vector<Sample> current_, best_;
    void record(const GhostPose& pose, std::uint32_t elapsed_ms, bool finish = false);
    std::filesystem::path path(const std::filesystem::path& state_root) const;

public:
    // A full valid lap, including its origin and an off-grid finish sample.
    // 999999ms is the no-record sentinel; native recording follows25Hz ticks.
    static constexpr std::uint32_t maximum_lap_milliseconds = 999998;
    static constexpr std::uint32_t sample_interval_ms = LiveSchedule::interval_ns / 1'000'000;
    static constexpr std::size_t maximum_samples = (maximum_lap_milliseconds + sample_interval_ms - 1) / sample_interval_ms + 1;
    TimeTrial(std::uint32_t track, std::uint32_t car, std::uint32_t weather, std::string driver);
    // Call once after each Session::update using that call's events and time.
    // Source45dcb0 discards the approach, promotes only strictly faster full laps,
    // then records/replays the next attempt from the same start-line timestamp.
    void update(std::uint64_t active_ms, const GhostPose& pose, std::span<const SessionEvent> events, std::uint32_t last_lap_ms);
    std::optional<GhostPose> playback() const;
    // Native version3 LE format:32-byte header and millisecond samples.
    // One file per track; car/weather are saved metadata.
    // Missing file or unsupported format returns false; malformed supported
    // files and read/write failures throw.
    // Even LOAD BEST LAP=NO reads the disk baseline to protect a faster save;
    // use_saved_best only controls the prior ghost/timing supplied to this run.
    bool load(const std::filesystem::path& state_root, bool use_saved_best = true);
    bool save(const std::filesystem::path& state_root);
    std::uint32_t best_lap_ms() const;
    std::uint32_t best_car_id() const;
    std::uint32_t best_weather() const;
    const std::string& best_driver() const;
    std::size_t sample_count() const;
    std::size_t recording_samples() const;
    bool dirty() const;
};
} // namespace toca::simulation
