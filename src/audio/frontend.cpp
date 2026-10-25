#include "audio/frontend.hpp"

#include "assets/store.hpp"
#include "audio/sample_file.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace toca::audio {
void load_frontend_samples(Service& service, const AssetStore& store) {
    // Pointer table 0x004b8298, consumed by FUN_0046dc17/FUN_004796eb.
    constexpr std::array<const char*, 11> files{
        "updown.wav", "option.wav", "forwback.wav", "type.wav", "cannot.wav", "cme_e.wav", "cme_f.wav", "cme_g.wav", "cme_i.wav", "cme_s.wav", "bad.wav"};
    for (std::uint32_t sample = 0; sample < files.size(); ++sample) {
        load_sample_file(service, sample, store, std::string("fe/sounds/") + files[sample]);
    }
}

std::uint32_t frontend_sample_id(FrontendCue cue, std::uint32_t language) {
    switch (cue) {
        case FrontendCue::navigation:
            return 0;
        case FrontendCue::option:
            return 1;
        case FrontendCue::character:
            return 3;
        case FrontendCue::confirm:
        case FrontendCue::back:
            return 2;
        case FrontendCue::unavailable:
            return 4;
        case FrontendCue::cheat_activated:
            // 004774da: CME_E/F/G/I/S, selected by the frontend language.
            if (language >= 5) {
                throw std::runtime_error("Unknown cheat announcement language");
            }
            return 5 + language;
    }
    throw std::runtime_error("Unknown frontend audio cue");
}

Voice play_frontend_cue(Service& service, FrontendCue cue, std::uint32_t language, Voice previous_announcement) {
    // FUN_00475220 supplies Miles volume70 and pan64 before playback.
    if (cue == FrontendCue::cheat_activated && previous_announcement != 0) {
        service.stop(previous_announcement);
    }
    return service.play_original_volume(frontend_sample_id(cue, language), 1, 70);
}
} // namespace toca::audio
