#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toca {
// Write a complete native state file through an exclusive temporary and rename.
void write_state_file(const std::filesystem::path& path, std::string_view content);

namespace state_file {
// Native version-1 files have an eight-byte magic/version header and a CRC32 trailer.
struct Format {
    std::size_t size;
    std::uint32_t magic;
    std::string_view context;
};

std::vector<std::uint8_t> read(const std::filesystem::path& path, Format format);
void write(const std::filesystem::path& path, std::span<std::uint8_t> bytes, Format format);

class Reader {
    std::span<const std::uint8_t> remaining_;
    std::string_view context_;
    std::span<const std::uint8_t> take(std::size_t count, std::string_view field);

public:
    Reader(std::span<const std::uint8_t> bytes, std::string_view context) : remaining_(bytes), context_(context) {}

    std::uint32_t number();
    std::string text(std::size_t capacity, std::string_view field);
};

class Writer {
    std::span<std::uint8_t> remaining_;
    std::string_view context_;
    std::span<std::uint8_t> take(std::size_t count);

public:
    Writer(std::span<std::uint8_t> bytes, std::string_view context) : remaining_(bytes), context_(context) {}

    void number(std::uint32_t value);
    void text(std::string_view value, std::size_t capacity);
    void finish() const;
};
} // namespace state_file
} // namespace toca
