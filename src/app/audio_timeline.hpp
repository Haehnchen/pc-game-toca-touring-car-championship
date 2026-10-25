#pragma once

#include <cstdint>

namespace toca {
namespace audio {
class Service;
}

class AudioTimeline {
    audio::Service& audio_;
    std::uint64_t previous_ = 0, remainder_ = 0;

public:
    explicit AudioTimeline(audio::Service& audio);

    void reset(std::uint64_t now);

    void advance(std::uint64_t now);
};
} // namespace toca
