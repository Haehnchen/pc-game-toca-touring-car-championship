#pragma once

#include "audio/service.hpp"

#include <string_view>

namespace toca {
class AssetStore;
}

namespace toca::audio {
// Native data supplies .ogg beside the canonical .wav/.raw logical path.
// The canonical source tree remains directly usable without conversion.
std::uint32_t load_sample_file(Service& service, std::uint32_t sample, const AssetStore& store, std::string_view canonical_path);
} // namespace toca::audio
