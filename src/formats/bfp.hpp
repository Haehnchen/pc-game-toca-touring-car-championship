#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace toca {
struct BfpEntry {
    std::uint32_t index = 0;
    std::array<std::uint8_t, 16> raw_name{};
    std::uint32_t payload_offset = 0;
    std::uint32_t size = 0;
};

struct BfpStorage;
class BfpArchive;
class BfpCursor;

// A member retains its immutable archive bytes, including after cursor close.
class BfpMember {
    std::shared_ptr<const BfpStorage> storage_;
    std::size_t index_;

    BfpMember(std::shared_ptr<const BfpStorage> storage, std::size_t index);
    friend class BfpArchive;
    friend class BfpCursor;

public:
    const BfpEntry& entry() const;
    std::span<const std::uint8_t> bytes() const;
};

class BfpCursor {
    std::shared_ptr<const BfpStorage> storage_;
    std::size_t position_ = 0;

    explicit BfpCursor(std::shared_ptr<const BfpStorage> storage);
    friend class BfpArchive;

public:
    std::optional<BfpMember> read();
    bool skip();
    std::size_t position() const;
    std::size_t remaining() const;
    bool eof() const;
    void close();
};

class BfpArchive {
    std::shared_ptr<const BfpStorage> storage_;

public:
    explicit BfpArchive(std::vector<std::uint8_t> bytes);
    std::size_t size() const;
    std::uint32_t largest_member_size() const;
    std::span<const BfpEntry> entries() const;
    BfpMember member(std::size_t index) const;
    BfpCursor cursor() const;
};
} // namespace toca
