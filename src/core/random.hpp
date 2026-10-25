#pragma once

#include <cstdint>

namespace toca {
// 0049ff50/0049ff60: explicit source CRT state, including unsigned wrap.
class SourceRandom {
    std::uint32_t state_;

public:
    explicit SourceRandom(std::uint32_t seed) : state_(seed) {}

    std::uint32_t state() const {
        return state_;
    }

    std::uint32_t next() {
        state_ = state_ * 0x343fdu + 0x269ec3u;
        return (state_ >> 16) & 0x7fffu;
    }
};
} // namespace toca
