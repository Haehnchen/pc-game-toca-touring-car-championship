#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <stdexcept>

namespace toca::test::fleet {
struct UnsupportedSourceState : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void read(std::istream& input, std::span<std::uint8_t> bytes);
std::uint32_t read_u32(std::istream& input);
void source_store(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value, std::size_t width = 4);

} // namespace toca::test::fleet
