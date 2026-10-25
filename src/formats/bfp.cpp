#include "formats/bfp.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace toca {
struct BfpStorage {
    std::vector<std::uint8_t> bytes;
    std::vector<BfpEntry> entries;
    std::size_t payload_base = 0;
    std::uint32_t largest = 0;
};

namespace {
std::uint32_t little_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::shared_ptr<const BfpStorage> parse(std::vector<std::uint8_t> bytes) {
    if (bytes.size() < 12) {
        throw std::runtime_error("Truncated BFP header");
    }
    const auto count = little_u32(bytes, 0);
    const auto directory_size = little_u32(bytes, 4);
    const auto largest = little_u32(bytes, 8);
    if (std::uint64_t(count) * 24 != directory_size || directory_size > bytes.size() - 12) {
        throw std::runtime_error("Invalid BFP directory size");
    }
    auto storage = std::make_shared<BfpStorage>();
    storage->bytes = std::move(bytes);
    storage->payload_base = 12 + std::size_t(directory_size);
    storage->largest = largest;
    storage->entries.reserve(count);
    std::uint64_t expected_offset = 0;
    std::uint32_t actual_largest = 0;
    const auto payload_size = storage->bytes.size() - storage->payload_base;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto position = 12 + std::size_t(index) * 24;
        BfpEntry entry;
        entry.index = index;
        std::copy_n(storage->bytes.begin() + static_cast<std::ptrdiff_t>(position), entry.raw_name.size(), entry.raw_name.begin());
        entry.payload_offset = little_u32(storage->bytes, position + 16);
        entry.size = little_u32(storage->bytes, position + 20);
        if (entry.payload_offset != expected_offset || std::uint64_t(entry.payload_offset) + entry.size > payload_size) {
            throw std::runtime_error("Invalid BFP member bounds/order at index " + std::to_string(index));
        }
        expected_offset += entry.size;
        actual_largest = std::max(actual_largest, entry.size);
        storage->entries.push_back(entry);
    }
    if (expected_offset != payload_size) {
        throw std::runtime_error("Unaccounted bytes after BFP payload");
    }
    if (largest != actual_largest) {
        throw std::runtime_error("BFP maximum member size disagrees with directory");
    }
    return storage;
}
} // namespace

BfpMember::BfpMember(std::shared_ptr<const BfpStorage> storage, std::size_t index) : storage_(std::move(storage)), index_(index) {}

const BfpEntry& BfpMember::entry() const {
    if (!storage_) {
        throw std::logic_error("BFP member has no backing storage");
    }
    return storage_->entries[index_];
}

std::span<const std::uint8_t> BfpMember::bytes() const {
    const auto& description = entry();
    return std::span(storage_->bytes).subspan(storage_->payload_base + description.payload_offset, description.size);
}

BfpCursor::BfpCursor(std::shared_ptr<const BfpStorage> storage) : storage_(std::move(storage)) {}

std::optional<BfpMember> BfpCursor::read() {
    if (eof()) {
        return std::nullopt;
    }
    return BfpMember(storage_, position_++);
}

bool BfpCursor::skip() {
    if (eof()) {
        return false;
    }
    ++position_;
    return true;
}

std::size_t BfpCursor::position() const {
    return position_;
}

std::size_t BfpCursor::remaining() const {
    return storage_ ? storage_->entries.size() - position_ : 0;
}

bool BfpCursor::eof() const {
    return remaining() == 0;
}

void BfpCursor::close() {
    storage_.reset();
}

BfpArchive::BfpArchive(std::vector<std::uint8_t> bytes) : storage_(parse(std::move(bytes))) {}

std::size_t BfpArchive::size() const {
    return storage_ ? storage_->entries.size() : 0;
}

std::uint32_t BfpArchive::largest_member_size() const {
    return storage_ ? storage_->largest : 0;
}

std::span<const BfpEntry> BfpArchive::entries() const {
    return storage_ ? std::span<const BfpEntry>(storage_->entries) : std::span<const BfpEntry>{};
}

BfpMember BfpArchive::member(std::size_t index) const {
    if (index >= size()) {
        throw std::out_of_range("BFP member index is outside the archive");
    }
    return BfpMember(storage_, index);
}

BfpCursor BfpArchive::cursor() const {
    return BfpCursor(storage_);
}
} // namespace toca
