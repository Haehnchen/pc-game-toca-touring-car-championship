#pragma once

#include "assets/store.hpp"
#include "audio/service.hpp"
#include "race/championship.hpp"

#include <string>
#include <vector>

namespace toca::audio {
enum class ChampionshipCue {
    race_finish,
    results,
    standings,
    eliminated,
    complete,
    showdown_final
};

// 4af348/44cad0: EN, FR, DE, IT, ES. Canonical 1.1a supplies only
// EN/FR/DE speech; missing IT/ES folders use English as a native fallback.
std::uint32_t championship_speech_language(const AssetStore& store, std::uint32_t language);

// 44cb90/479cfb: localized unsigned8-bit mono22050 speech. Pass the
// pre-race-result State for race_finish so its first-win branch remains exact.
// Standings queues CTIN followed by points commentary, or ES/GOC afterRace24.
// showdown_final reconnects1.1a's existing FINAL clip at its English-only
// final Showdown award call; original44d1f5 mistakenly returned before play.
std::vector<std::string> championship_speech(
    ChampionshipCue cue, const race::ChampionshipState& state, SourceRandom& random, std::uint32_t language, const race::RaceResults* results = nullptr);

class Championship {
    Service& service_;
    const AssetStore& store_;
    std::uint32_t language_, requested_language_;
    Voice voice_ = 0;
    std::uint64_t serial_ = 0, played_ = 0;
    std::vector<std::string> pending_;
    std::size_t next_ = 0;
    bool active_ = true;
    void start_next();

public:
    Championship(Service& service, const AssetStore& store, std::uint32_t language);
    ~Championship();
    Championship(const Championship&) = delete;
    Championship& operator=(const Championship&) = delete;
    void play(ChampionshipCue cue, const race::ChampionshipState& state, SourceRandom& random, const race::RaceResults* results = nullptr);
    void update();
    // Focus/pause cuts commentary without replaying a consumed one-shot.
    void set_active(bool active);
    void stop();
    bool playing() const;
    std::uint64_t played_count() const;
};
} // namespace toca::audio
