#include "binary_io.hpp"

#include "simulation/source_snapshot.hpp"

#include <array>
#include <istream>

namespace toca::test::fleet {
using namespace toca::simulation;

void read(std::istream& input, std::span<std::uint8_t> bytes) {
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!input) {
        throw std::runtime_error("Truncated original fleet replay stream");
    }
}

std::uint32_t read_u32(std::istream& input) {
    std::array<std::uint8_t, 4> bytes;
    read(input, bytes);
    return source_u32(bytes, 0);
}

void source_store(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value, std::size_t width) {
    if (offset + width > bytes.size()) {
        throw std::runtime_error("Joined source field exceeds retained actor allocation");
    }
    for (std::size_t byte = 0; byte < width; ++byte) {
        bytes[offset + byte] = std::uint8_t(value >> (byte * 8));
    }
}

} // namespace toca::test::fleet
