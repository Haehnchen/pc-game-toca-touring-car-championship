#include "settings/storage.hpp"

#include "core/crc32.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace toca {
namespace {
void sync_file(std::FILE* file) {
    if (std::fflush(file) != 0) {
        throw std::runtime_error("Flushing state file failed");
    }
#ifdef _WIN32
    const int result = _commit(_fileno(file));
#else
    const int result = fsync(fileno(file));
#endif
    if (result != 0) {
        throw std::runtime_error("Synchronizing state file failed");
    }
}

struct FileCloser {
    void operator()(std::FILE* file) const {
        std::fclose(file);
    }
};
} // namespace

void write_state_file(const std::filesystem::path& path, std::string_view content) {
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(parent);
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(std::random_device{}());
    // Exclusive creation keeps concurrent native instances from sharing a partial file.
#ifdef _WIN32
    // Legacy MSVCRT does not support fopen's C11 'x' mode.
    const int descriptor = _wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
    std::FILE* raw_file = descriptor < 0 ? nullptr : _wfdopen(descriptor, L"wb");
#else
    std::FILE* raw_file = std::fopen(temporary.c_str(), "wbx");
#endif
    if (!raw_file) {
        const int error = errno;
#ifdef _WIN32
        if (descriptor >= 0) {
            _close(descriptor);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
#endif
        throw std::system_error(error, std::generic_category(), "Cannot create state file temporary file: " + temporary.string());
    }
    std::unique_ptr<std::FILE, FileCloser> file(raw_file);
    try {
        if (std::fwrite(content.data(), 1, content.size(), file.get()) != content.size()) {
            throw std::runtime_error("Writing state file failed");
        }
        sync_file(file.get());
        if (std::fclose(file.release()) != 0) {
            throw std::runtime_error("Closing state file failed");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Replacing state file failed: " + std::to_string(GetLastError()));
        }
#else
        std::filesystem::rename(temporary, path);
        const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0) {
            throw std::runtime_error("Opening state file directory for synchronization failed");
        }
        const int sync_result = fsync(directory);
        close(directory);
        if (sync_result != 0) {
            throw std::runtime_error("Synchronizing state file directory failed");
        }
#endif
    } catch (...) {
        file.reset();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

namespace state_file {
std::span<const std::uint8_t> Reader::take(std::size_t count, std::string_view field) {
    if (count > remaining_.size()) {
        throw std::runtime_error(std::string(context_) + ": incomplete " + std::string(field));
    }
    const auto value = remaining_.first(count);
    remaining_ = remaining_.subspan(count);
    return value;
}

std::uint32_t Reader::number() {
    const auto bytes = take(4, "number");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= std::uint32_t(bytes[index]) << (index * 8);
    }
    return value;
}

std::string Reader::text(std::size_t capacity, std::string_view field) {
    const auto bytes = take(capacity, field);
    const auto end = std::ranges::find(bytes, 0);
    if (end == bytes.end()) {
        throw std::runtime_error(std::string(context_) + ": unterminated " + std::string(field));
    }
    return {bytes.begin(), end};
}

std::span<std::uint8_t> Writer::take(std::size_t count) {
    if (count > remaining_.size()) {
        throw std::runtime_error(std::string(context_) + ": native layout size mismatch");
    }
    const auto value = remaining_.first(count);
    remaining_ = remaining_.subspan(count);
    return value;
}

void Writer::number(std::uint32_t value) {
    auto bytes = take(4);
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = std::uint8_t(value >> (index * 8));
    }
}

void Writer::text(std::string_view value, std::size_t capacity) {
    if (value.size() >= capacity || value.find('\0') != std::string_view::npos) {
        throw std::runtime_error(std::string(context_) + ": invalid text");
    }
    auto bytes = take(capacity);
    std::ranges::fill(bytes, 0);
    std::ranges::copy(value, bytes.begin());
}

void Writer::finish() const {
    if (!remaining_.empty()) {
        throw std::runtime_error(std::string(context_) + ": native layout size mismatch");
    }
}

std::vector<std::uint8_t> read(const std::filesystem::path& path, Format format) {
    const auto context = std::string(format.context);
    if (format.size < 12 || std::filesystem::file_size(path) != format.size) {
        throw std::runtime_error(context + ": invalid file size: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(context + ": cannot read " + path.string());
    }
    std::vector<std::uint8_t> bytes(format.size);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) || input.peek() != std::char_traits<char>::eof() || input.bad()) {
        throw std::runtime_error(context + ": incomplete read");
    }
    Reader checksum{std::span(bytes).last(4), format.context};
    if (crc32(std::span(bytes).first(bytes.size() - 4)) != checksum.number()) {
        throw std::runtime_error(context + ": CRC32 mismatch");
    }
    Reader header{std::span(bytes).first(8), format.context};
    if (header.number() != format.magic || header.number() != 1) {
        throw std::runtime_error(context + ": invalid native header");
    }
    return bytes;
}

void write(const std::filesystem::path& path, std::span<std::uint8_t> bytes, Format format) {
    if (format.size < 12 || bytes.size() != format.size) {
        throw std::runtime_error(std::string(format.context) + ": native layout size mismatch");
    }
    Writer header{bytes.first(8), format.context};
    header.number(format.magic);
    header.number(1);
    Writer checksum{bytes.last(4), format.context};
    checksum.number(crc32(bytes.first(bytes.size() - 4)));
    write_state_file(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
} // namespace state_file
} // namespace toca
