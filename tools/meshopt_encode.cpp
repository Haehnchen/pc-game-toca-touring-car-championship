#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <meshoptimizer.h>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
std::uint32_t read32() {
    std::array<unsigned char, 4> bytes{};
    if (!std::cin.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        throw std::runtime_error("incomplete encoder request");
    }
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

void write32(std::size_t value) {
    const std::array<unsigned char, 4> bytes{std::uint8_t(value), std::uint8_t(value >> 8), std::uint8_t(value >> 16), std::uint8_t(value >> 24)};
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
} // namespace

// One bounded batch per asset avoids a process launch for every material/mesh.
int main() {
    try {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        meshopt_encodeVertexVersion(0); // EXT_meshopt_compression bitstream.
        meshopt_encodeIndexVersion(0);
        if (read32() != 0x54504f4d) {
            throw std::runtime_error("invalid encoder request");
        }
        const auto records = read32();
        if (records > 16384) {
            throw std::runtime_error("too many geometry buffers");
        }
        std::size_t total = 0;
        for (std::uint32_t record = 0; record < records; ++record) {
            const auto mode = read32(), stride = read32(), count = read32(), length = read32();
            constexpr std::size_t limit = 128 * 1024 * 1024;
            if (mode > 1 || !count || (mode == 0 && (!stride || stride > 256 || stride % 4)) || (mode == 1 && stride != 2) || length > limit - total
                || std::uint64_t(count) * stride != length) {
                throw std::runtime_error("invalid bounded geometry buffer");
            }
            total += length;
            std::vector<unsigned char> source(length);
            if (!std::cin.read(reinterpret_cast<char*>(source.data()), source.size())) {
                throw std::runtime_error("incomplete geometry buffer");
            }
            std::vector<unsigned char> encoded;
            std::size_t size = 0;
            if (mode == 0) {
                encoded.resize(meshopt_encodeVertexBufferBound(count, stride));
                size = meshopt_encodeVertexBuffer(encoded.data(), encoded.size(), source.data(), count, stride);
            } else {
                std::vector<unsigned int> indices(count);
                for (std::size_t index = 0; index < count; ++index) {
                    indices[index] = unsigned(source[index * 2]) | (unsigned(source[index * 2 + 1]) << 8);
                }
                encoded.resize(meshopt_encodeIndexSequenceBound(count, 65536));
                size = meshopt_encodeIndexSequence(encoded.data(), encoded.size(), indices.data(), count);
            }
            if (!size) {
                throw std::runtime_error("meshoptimizer encoding failed");
            }
            write32(size);
            std::cout.write(reinterpret_cast<const char*>(encoded.data()), std::streamsize(size));
        }
        if (std::cin.peek() != std::char_traits<char>::eof() || !std::cout) {
            throw std::runtime_error("invalid encoder stream");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "meshopt: " << error.what() << '\n';
        return 1;
    }
}
