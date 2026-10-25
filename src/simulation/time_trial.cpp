#include "simulation/time_trial.hpp"

#include "settings/storage.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace toca::simulation {
namespace {
void put(std::vector<std::uint8_t>& bytes, std::uint32_t value, unsigned width = 4) {
    for (unsigned index = 0; index < width; ++index) {
        bytes.push_back(std::uint8_t(value >> (index * 8)));
    }
}

std::uint32_t get(std::span<const std::uint8_t> bytes, std::size_t& cursor, unsigned width = 4) {
    if (cursor > bytes.size() || bytes.size() - cursor < width) {
        throw std::runtime_error("Time Trial: truncated ghost save");
    }
    std::uint32_t result = 0;
    for (unsigned index = 0; index < width; ++index) {
        result |= std::uint32_t(bytes[cursor++]) << (index * 8);
    }
    return result;
}

std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t((std::int64_t(a) * b) >> 16));
}

std::int32_t difference(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}

std::optional<std::vector<std::uint8_t>> read_ghost_file(const std::filesystem::path& path, std::uintmax_t maximum) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Time Trial: cannot read " + path.string());
    }
    std::array<std::uint8_t, 8> header;
    input.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
    if (input.gcount() != std::streamsize(header.size())) {
        throw std::runtime_error("Time Trial: truncated ghost save");
    }
    std::size_t cursor = 0;
    if (get(header, cursor) != 0x54544354 || get(header, cursor) != 3) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path);
    if (size > maximum) {
        throw std::runtime_error("Time Trial: invalid save size: " + path.string());
    }
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (input.bad() || input.gcount() != std::streamsize(bytes.size()) || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Time Trial: incomplete save read: " + path.string());
    }
    return bytes;
}

} // namespace

TimeTrial::TimeTrial(std::uint32_t track, std::uint32_t car, std::uint32_t weather, std::string driver) : track_(track), car_(car), weather_(weather), driver_(std::move(driver)) {
    if (track >= 10 || car >= 10 || weather >= 6 || driver_.size() > 255 || driver_.find('\0') != std::string::npos) {
        throw std::runtime_error("Time Trial: invalid track/car/weather/name");
    }
    current_.reserve(maximum_samples);
    best_.reserve(maximum_samples);
}

void TimeTrial::record(const GhostPose& pose, std::uint32_t elapsed_ms, bool finish) {
    if (elapsed_ms > maximum_lap_milliseconds) {
        return;
    }
    // Repeated/extra host updates cannot exhaust the25Hz-derived file bound.
    // Keep the exact finish even when it falls between ordinary sample times.
    if (!current_.empty() && elapsed_ms != current_.back().milliseconds && elapsed_ms - current_.back().milliseconds < sample_interval_ms && !finish) {
        return;
    }
    Sample sample;
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            sample.basis_q14[row * 3 + axis] = std::bit_cast<std::int16_t>(std::uint16_t(pose.basis_q16[row][axis] >> 2));
        }
    }
    sample.position_q16 = pose.position_q16;
    sample.milliseconds = elapsed_ms;
    if (!current_.empty() && current_.back().milliseconds == elapsed_ms) {
        current_.back() = sample;
    } else if (current_.size() < maximum_samples) {
        current_.push_back(sample);
    }
}

void TimeTrial::update(std::uint64_t active_ms, const GhostPose& pose, std::span<const SessionEvent> events, std::uint32_t last_lap_ms) {
    if (active_ms < now_ms_ || active_ms > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Time Trial time must be monotonic active milliseconds within the source32-bit range");
    }
    now_ms_ = active_ms;
    const bool released = std::ranges::find(events, SessionEvent::released) != events.end();
    const bool complete = std::ranges::find(events, SessionEvent::lap_complete) != events.end();
    const bool restarted = std::ranges::find(events, SessionEvent::attempt_restarted) != events.end();
    if (recording_) {
        record(pose, std::uint32_t(active_ms - lap_start_ms_), complete);
    }
    if (complete && recording_) {
        if (last_lap_ms > 0 && last_lap_ms < best_ms_ && current_.size() >= 2) {
            best_.swap(current_);
            best_ms_ = last_lap_ms;
            best_car_ = car_;
            best_weather_ = weather_;
            best_driver_ = driver_;
            dirty_ = best_ms_ < saved_ms_;
        }
    }
    if (released || ((complete || restarted) && recording_)) {
        current_.clear();
        recording_ = true;
        lap_start_ms_ = active_ms;
        record(pose, 0);
    }
}

std::optional<GhostPose> TimeTrial::playback() const {
    if (!recording_ || best_.size() < 2) {
        return std::nullopt;
    }
    const auto elapsed = now_ms_ - lap_start_ms_;
    if (elapsed > best_.back().milliseconds) {
        return std::nullopt;
    }
    auto upper = std::upper_bound(best_.begin(), best_.end(), elapsed, [](std::uint64_t time, const Sample& sample) {
        return time < sample.milliseconds;
    });
    if (upper == best_.end()) {
        upper = best_.end() - 1;
    }
    const auto& second = *upper;
    const auto& first = upper == best_.begin() ? second : *(upper - 1);
    const auto duration = second.milliseconds - first.milliseconds;
    const auto fraction = duration == 0 ? 0 : std::uint32_t(((elapsed - first.milliseconds) * 65536) / duration);
    const auto interpolate = [fraction](std::int32_t a, std::int32_t b) {
        return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t((std::int64_t(difference(b, a)) * fraction) >> 16));
    };
    GhostPose result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.position_q16[axis] = interpolate(first.position_q16[axis], second.position_q16[axis]);
        for (std::size_t row = 0; row < 2; ++row) {
            result.basis_q16[row][axis] = interpolate(std::int32_t(first.basis_q14[row * 3 + axis]) * 4, std::int32_t(second.basis_q14[row * 3 + axis]) * 4);
        }
    }
    // 45df60/45e120 reconstruct row2 from row0 cross row1, with separately
    // shifted products; do not normalize the source's interpolated axes.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        result.basis_q16[2][axis] = difference(multiply(result.basis_q16[0][a], result.basis_q16[1][b]), multiply(result.basis_q16[0][b], result.basis_q16[1][a]));
    }
    return result;
}

std::filesystem::path TimeTrial::path(const std::filesystem::path& state_root) const {
    return state_root / "time-trial" / ("track-" + std::to_string(track_) + ".ghost");
}

bool TimeTrial::load(const std::filesystem::path& state_root, bool use_saved_best) {
    const auto file = path(state_root);
    if (!std::filesystem::exists(file)) {
        return false;
    }
    // A bounded allocation/read also detects truncation or growth during I/O.
    const auto contents = read_ghost_file(file, 32 + 255 + maximum_samples * 28);
    if (!contents) {
        return false;
    }
    const auto& bytes = *contents;
    if (bytes.size() < 32) {
        throw std::runtime_error("Time Trial: truncated ghost save");
    }
    std::size_t cursor = 8;
    if (get(bytes, cursor) != track_) {
        throw std::runtime_error("Time Trial: invalid ghost save header");
    }
    const auto car = get(bytes, cursor), weather = get(bytes, cursor), milliseconds = get(bytes, cursor);
    const auto count = get(bytes, cursor), name_size = get(bytes, cursor);
    if (car >= 10 || weather >= 6 || milliseconds == 0 || milliseconds > maximum_lap_milliseconds || count < 2 || count > maximum_samples || name_size > 255
        || bytes.size() != 32 + name_size + count * 28) {
        throw std::runtime_error("Time Trial: invalid ghost save metadata");
    }
    std::string driver(bytes.begin() + std::ptrdiff_t(cursor), bytes.begin() + std::ptrdiff_t(cursor + name_size));
    cursor += name_size;
    if (driver.find('\0') != std::string::npos) {
        throw std::runtime_error("Time Trial: invalid saved driver name");
    }
    std::vector<Sample> samples(count);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        auto& sample = samples[index];
        for (auto& value : sample.basis_q14) {
            value = std::bit_cast<std::int16_t>(std::uint16_t(get(bytes, cursor, 2)));
        }
        for (auto& value : sample.position_q16) {
            value = std::bit_cast<std::int32_t>(get(bytes, cursor));
        }
        sample.milliseconds = get(bytes, cursor);
        if ((index == 0 && sample.milliseconds != 0) || (index > 0 && sample.milliseconds <= samples[index - 1].milliseconds) || sample.milliseconds > milliseconds) {
            throw std::runtime_error("Time Trial: invalid ghost save sample times");
        }
    }
    saved_ms_ = milliseconds;
    if (use_saved_best) {
        best_ = std::move(samples);
        best_car_ = car;
        best_weather_ = weather;
        best_driver_ = std::move(driver);
        best_ms_ = milliseconds;
    }
    dirty_ = false;
    return true;
}

bool TimeTrial::save(const std::filesystem::path& state_root) {
    if (!dirty_) {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    put(bytes, 0x54544354); // "TCTT", native ghost format.
    put(bytes, 3);
    put(bytes, track_);
    put(bytes, best_car_);
    put(bytes, best_weather_);
    put(bytes, best_ms_);
    put(bytes, std::uint32_t(best_.size()));
    put(bytes, std::uint32_t(best_driver_.size()));
    bytes.insert(bytes.end(), best_driver_.begin(), best_driver_.end());
    for (const auto& sample : best_) {
        for (const auto value : sample.basis_q14) {
            put(bytes, std::uint16_t(value), 2);
        }
        for (const auto value : sample.position_q16) {
            put(bytes, std::uint32_t(value));
        }
        put(bytes, sample.milliseconds);
    }
    const auto file = path(state_root);
    write_state_file(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    saved_ms_ = best_ms_;
    dirty_ = false;
    return true;
}

std::uint32_t TimeTrial::best_lap_ms() const {
    return best_.empty() ? 0 : best_ms_;
}

std::uint32_t TimeTrial::best_car_id() const {
    return best_car_;
}

std::uint32_t TimeTrial::best_weather() const {
    return best_weather_;
}

const std::string& TimeTrial::best_driver() const {
    return best_driver_;
}

std::size_t TimeTrial::sample_count() const {
    return best_.size();
}

std::size_t TimeTrial::recording_samples() const {
    return current_.size();
}

bool TimeTrial::dirty() const {
    return dirty_;
}
} // namespace toca::simulation
