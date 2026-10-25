#include "audio/sample_file.hpp"

#include "assets/store.hpp"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace toca::audio {
std::uint32_t load_sample_file(Service& service, std::uint32_t sample, const AssetStore& store, std::string_view canonical_path) {
    auto encoded_path = std::filesystem::path(canonical_path);
    encoded_path.replace_extension(".ogg");
    if (store.find(encoded_path.generic_string())) {
        return service.load_sample(sample, store.read_file(encoded_path.generic_string()));
    }
    const auto bytes = store.read_file(canonical_path);
    if (!canonical_path.ends_with(".raw")) {
        return service.load_sample(sample, bytes);
    }
    if (bytes.empty()) {
        throw std::runtime_error("Championship commentary: empty original RAW sample");
    }
    std::vector<float> pcm;
    pcm.reserve(bytes.size());
    // Original Miles type0 is unsigned 8-bit mono at exactly 22050 Hz.
    for (const auto byte : bytes) {
        pcm.push_back(float(int(byte) - 128) / 128.f);
    }
    service.load_pcm(sample, pcm, 1, 22050);
    return 22050;
}
} // namespace toca::audio
