#pragma once

#include "formats/hpc.hpp"
#include "scene/placement.hpp"
#include "simulation/motion.hpp"
#include "simulation/source_snapshot.hpp"

#include <filesystem>
#include <vector>

namespace toca::test::motion {
using namespace toca::simulation;

using Bytes = std::vector<std::uint8_t>;
constexpr std::int32_t unit = 65536;

void expect(bool valid, const char* message);
void put16(Bytes& bytes, std::size_t offset, std::uint16_t value);
void put32(Bytes& bytes, std::size_t offset, std::int32_t value);
Bytes plane(std::uint16_t flags = 0);
Bytes driving_plane(std::uint16_t flags, bool split = false);
void finite(const MotionState& state);
void ticks(Motion& motion, std::uint32_t count, const ControlsInput& input);

template <class Buffer> void word(Buffer& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::uint8_t(std::uint32_t(value) >> (i * 8));
    }
}

void cold_distant_effect_gate();
void brake_lights();
void engine_effect_adapter();
void automatic_reverse();
void handling();
void sine_values();
void fixed_arithmetic();
void qualifying_restart();
void canonical(const std::filesystem::path& root);

} // namespace toca::test::motion
