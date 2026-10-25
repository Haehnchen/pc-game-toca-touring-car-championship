#pragma once

#include "audio/service.hpp"

namespace toca {
class AssetStore;
}

namespace toca::audio {
enum class FrontendCue {
    navigation,
    option,
    character,
    confirm,
    back,
    unavailable,
    cheat_activated,
};

void load_frontend_samples(Service& service, const AssetStore& store);
std::uint32_t frontend_sample_id(FrontendCue cue, std::uint32_t language = 0);
// The frontend owns one announcement handle, matching original 0052ee04.
Voice play_frontend_cue(Service& service, FrontendCue cue, std::uint32_t language = 0, Voice previous_announcement = 0);
} // namespace toca::audio
