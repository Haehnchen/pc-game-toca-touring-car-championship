#include "audio/championship.hpp"

#include "audio/sample_file.hpp"

#include <array>
#include <stdexcept>

namespace toca::audio {
namespace {
constexpr std::array<std::string_view, 8> teams{"HO", "AU", "VA", "VO", "FO", "NI", "PE", "RE"}; // 4af268.
constexpr std::array<std::string_view, 16> drivers{"JT", "GT", "FB", "JB", "JC", "DW", "RR", "KB", "WH", "PR", "DL", "AR", "TH", "PW", "AM", "JP"}; // 4af378.
constexpr std::array<std::string_view, 5> languages{"english", "french", "german", "italian", "spanish"}; // 4af348/44cad0.
constexpr std::uint32_t speech_sample = 0x7000;

std::uint32_t draw(SourceRandom& random, std::uint32_t bound) {
    return random.next() % bound + 1;
}

std::string variant(std::string_view prefix, std::uint32_t number) {
    return std::string(prefix) + std::to_string(number);
}

std::string named(std::string_view prefix, std::string_view name, std::uint32_t number) {
    return std::string(prefix) + std::string(name) + std::to_string(number);
}

std::string path(std::uint32_t language, std::string_view directory, std::string_view name) {
    return "speech/pc/" + std::string(languages[language]) + "/" + std::string(directory) + "/" + std::string(name) + ".raw";
}

std::string standings_summary(const race::ChampionshipState& state, SourceRandom& random) {
    const auto order = race::championship_standings(state);
    const auto team = teams[state.local_actor / 2];
    const auto winner = order[0];
    const auto gap = state.points[winner] - state.points[order[1]];
    // 422980 ->44cb90 case12, including source thresholds8/20/26.
    if (winner == state.local_actor) {
        const auto number = draw(random, 3);
        if (gap > 25) {
            return named("CTPB", team, number);
        }
        return number == 3 ? "CTPR1" : named("CTPR", team, number);
    }
    if (order[15] == state.local_actor) {
        return named("CTPL", team, draw(random, 3));
    }
    const auto number = draw(random, 4);
    if (gap < 8) {
        return variant("CTIC", number);
    }
    const auto prefix = gap < 20 ? "CTIS" : "CTIB";
    return number == 4 ? named(prefix, drivers[winner], 1) : variant(prefix, number);
}

std::string ending_speech(const race::ChampionshipState& state, SourceRandom& random, std::uint32_t language) {
    const auto team = teams[state.local_actor / 2];
    if (race::championship_standings(state)[0] == state.local_actor) {
        return path(language, "send", named("ES", team, draw(random, 3)));
    }
    return path(language, "gameover", named("GOC", team, 1));
}
} // namespace

std::uint32_t championship_speech_language(const AssetStore& store, std::uint32_t language) {
    if (language >= languages.size()) {
        throw std::runtime_error("Championship commentary: unsupported language");
    }
    if (language < 3) {
        return language;
    }
    // The executable knows all five languages, but the canonical 1.1a data
    // contains no Italian/Spanish speech folders. Keep those menus usable
    // with English commentary; use their correct source language if present.
    for (const auto& entry : std::filesystem::directory_iterator(store.resolve("speech/pc"))) {
        auto name = entry.path().filename().string();
        for (auto& letter : name) {
            if (letter >= 'A' && letter <= 'Z') {
                letter = char(letter + ('a' - 'A'));
            }
        }
        if (name == languages[language] && entry.is_directory()) {
            return language;
        }
    }
    return 0;
}

std::vector<std::string> championship_speech(
    ChampionshipCue cue, const race::ChampionshipState& state, SourceRandom& random, std::uint32_t language, const race::RaceResults* results) {
    race::validate_championship(state);
    if (language >= languages.size()) {
        throw std::runtime_error("Championship commentary: unsupported language");
    }
    const auto team = teams[state.local_actor / 2];
    switch (cue) {
        case ChampionshipCue::race_finish: {
            if (results == nullptr || results->local_actor != state.local_actor || results->local_rank >= 16 || results->rows[0].actor_id >= 16) {
                throw std::runtime_error("Championship commentary: race-finish classification missing");
            }
            std::string name;
            if (results->local_rank == 0) {
                name = named(state.has_won ? "ERN" : "ERF", team, draw(random, state.has_won ? 5 : 3));
            } else {
                const auto& first = results->rows[0];
                const auto& second = results->rows[1];
                const bool close = first.finished && second.finished && second.time_ms >= first.time_ms && second.time_ms - first.time_ms < 1000;
                if (close) {
                    name = named("ERE", drivers[first.actor_id], 1);
                } else {
                    auto number = draw(random, 5);
                    while (number == 4) {
                        number = draw(random, 5);
                    }
                    name = number == 5 ? named("ERC", drivers[first.actor_id], 1) : variant("ERC", number);
                }
            }
            return {path(language, "rend", name)};
        }
        case ChampionshipCue::results:
            return {path(language, "fpintro", variant("FPI", draw(random, 7)))};
        case ChampionshipCue::standings: {
            // Draw the introductory variation before points commentary.
            const auto intro = path(language, "champ", variant("CTIN", draw(random, 5)));
            // 448f00 sees counter25 after the final race and follows CTIN
            // with ES/GOC before the final award page, never a CTP summary.
            if (state.meeting == 11 && state.race == 1 && (state.phase == race::ChampionshipPhase::results || state.phase == race::ChampionshipPhase::complete)) {
                return {intro, ending_speech(state, random, language)};
            }
            return {intro, path(language, "champ", standings_summary(state, random))};
        }
        case ChampionshipCue::eliminated:
            return {path(language, "gameover", named("GO", team, draw(random, 4)))};
        case ChampionshipCue::showdown_final:
            if (state.showdown && state.phase == race::ChampionshipPhase::complete && language == 0) {
                return {path(0, "extras", "FINAL")};
            }
            return {};
        case ChampionshipCue::complete:
            return {ending_speech(state, random, language)};
    }
    throw std::runtime_error("Championship commentary: invalid stage");
}

Championship::Championship(Service& service, const AssetStore& store, std::uint32_t language)
    : service_(service), store_(store), language_(championship_speech_language(store, language)), requested_language_(language) {
    if (language_ >= languages.size()) {
        throw std::runtime_error("Championship commentary: unsupported language");
    }
}

Championship::~Championship() {
    stop();
}

bool Championship::playing() const {
    return voice_ != 0 && serial_ == service_.playback_serial(voice_) && service_.playing(voice_);
}

void Championship::start_next() {
    if (!active_ || playing() || next_ >= pending_.size()) {
        return;
    }
    load_sample_file(service_, speech_sample, store_, pending_[next_++]);
    voice_ = service_.play_original_volume(speech_sample, 1, 127, 64);
    serial_ = voice_ == 0 ? 0 : service_.playback_serial(voice_);
    if (voice_ != 0) {
        ++played_;
    }
}

void Championship::play(ChampionshipCue cue, const race::ChampionshipState& state, SourceRandom& random, const race::RaceResults* results) {
    stop();
    // An English fallback must not turn Italian/Spanish selection into the
    // explicitly English-only ending requested by44cb90 case16.
    pending_ = championship_speech(cue, state, random, cue == ChampionshipCue::showdown_final ? requested_language_ : language_, results);
    start_next();
}

void Championship::update() {
    start_next();
}

void Championship::set_active(bool active) {
    active_ = active;
    if (!active_) {
        stop();
    }
}

void Championship::stop() {
    if (playing()) {
        service_.stop(voice_);
    }
    voice_ = 0;
    serial_ = 0;
    pending_.clear();
    next_ = 0;
}

std::uint64_t Championship::played_count() const {
    return played_;
}
} // namespace toca::audio
