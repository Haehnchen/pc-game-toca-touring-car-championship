#include "audio/championship.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto sample : pcm) {
        expect(std::isfinite(sample), "Championship speech PCM must remain finite");
        result += double(sample) * sample;
    }
    return result;
}

bool speech_file_exists(const toca::AssetStore& store, std::string_view canonical_path) {
    auto encoded = std::filesystem::path(canonical_path);
    encoded.replace_extension(".ogg");
    auto found = store.find(encoded.generic_string());
    if (!found) {
        found = store.find(canonical_path);
    }
    return found && std::filesystem::is_regular_file(*found) && std::filesystem::file_size(*found) > 0;
}

toca::race::RaceResults result() {
    toca::race::RaceResults value;
    value.local_actor = 0;
    value.local_rank = 0;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        value.rows[actor] = {actor, actor / 2, 600000 + actor * 2000, 6, "DRIVER", true, false};
    }
    return value;
}

void source_paths(const toca::AssetStore* store) {
    using namespace toca;
    using namespace audio;
    auto state = race::new_championship(false, 0, "PLAYER");
    auto finished = result();
    std::set<std::string> checked;
    for (std::uint32_t seed = 0; seed < 100; ++seed) {
        for (std::uint32_t language = 0; language < 5; ++language) {
            SourceRandom random(seed);
            const auto speech_language = store ? championship_speech_language(*store, language) : language;
            const auto paths = [&](ChampionshipCue cue, const race::RaceResults* rows = nullptr) {
                const auto files = championship_speech(cue, state, random, speech_language, rows);
                expect(!files.empty(), "Every implemented source stage must select commentary");
                if (store != nullptr) {
                    for (const auto& path : files) {
                        if (checked.insert(path).second) {
                            expect(speech_file_exists(*store, path), "Selected commentary must exist in canonical language assets");
                        }
                    }
                }
                return files;
            };
            state.has_won = false;
            expect(paths(ChampionshipCue::race_finish, &finished)[0].find("/rend/ERFHO") != std::string::npos, "First win must use the original first-victory family");
            state.has_won = true;
            expect(paths(ChampionshipCue::race_finish, &finished)[0].find("/rend/ERNHO") != std::string::npos, "Later wins must use ordinary winner commentary");
            finished.local_rank = 4;
            std::swap(finished.rows[0], finished.rows[4]);
            paths(ChampionshipCue::race_finish, &finished);
            finished.rows[1].time_ms = finished.rows[0].time_ms + 500;
            const auto close = paths(ChampionshipCue::race_finish, &finished);
            expect(close[0].find("/rend/EREJC1.raw") != std::string::npos, "Sub-second winning gap must name the source winning actor");
            finished = result();
            paths(ChampionshipCue::results);
            for (const auto gap : {0u, 8u, 20u, 26u}) {
                state.points.fill(100);
                state.points[0] = 1; // Local last -> CTPL.
                paths(ChampionshipCue::standings);
                state.points[0] = 99; // Local middle, leader1 -> CTIC/CTIS/CTIB.
                state.points[15] = 1;
                state.points[1] += gap;
                paths(ChampionshipCue::standings);
                state.points.fill(100);
                state.points[0] = 100 + gap + 1; // Local leader -> CTPR/CTPB.
                paths(ChampionshipCue::standings);
            }
            paths(ChampionshipCue::eliminated);
            paths(ChampionshipCue::complete);
            state.points[1] = state.points[0] + 1;
            paths(ChampionshipCue::complete);
        }
    }
}

void final_standings(const toca::AssetStore* store = nullptr) {
    using namespace toca;
    using namespace audio;
    auto state = race::new_championship(false, 0, "PLAYER", true);
    state.meeting = 11;
    state.race = 1;
    state.phase = race::ChampionshipPhase::results;
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto spoken = store ? championship_speech_language(*store, language) : language;
        for (const bool champion : {true, false}) {
            state.points.fill(100);
            state.points[champion ? 0 : 1] = 200;
            SourceRandom random(1), expected(1);
            const auto files = championship_speech(ChampionshipCue::standings, state, random, spoken);
            expect(files.size() == 2 && files[0].find("/champ/CTIN") != std::string::npos, "Final standings must retain the source CTIN introduction");
            expect(files[1].find(champion ? "/send/ESHO" : "/gameover/GOCHO1.raw") != std::string::npos,
                "Final standings must select champion ES or completed-season GOC before honors");
            expected.next(); // CTIN variation.
            if (champion) {
                expected.next(); // ES variation; GOC has no random draw.
            }
            expect(random.next() == expected.next(), "Final standings must not consume an intermediate CTP variation draw");
            if (store) {
                for (const auto& file : files) {
                    expect(speech_file_exists(*store, file), "Final standings speech must exist in the canonical language assets");
                }
            }
        }
    }
}

void synthetic() {
    using namespace toca;
    using namespace audio;
    auto final_state = race::new_championship(false, 0, "PLAYER", true);
    final_state.meeting = 11;
    final_state.race = 1;
    final_state.phase = race::ChampionshipPhase::complete;
    for (std::uint32_t language = 0; language < 5; ++language) {
        SourceRandom random(1), untouched(1);
        const auto final = championship_speech(ChampionshipCue::showdown_final, final_state, random, language);
        expect(final.size() == (language == 0 ? 1u : 0u), "Restored Showdown finale must retain the original English-only gate");
        expect(random.next() == untouched.next(), "Fixed final speech must not consume a variation draw");
    }
    const auto root = std::filesystem::current_path() / "championship-audio-data";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    auto state = race::new_championship(false, 0, "PLAYER");
    SourceRandom sample_random(1);
    const auto files = championship_speech(ChampionshipCue::standings, state, sample_random, 0);
    for (const auto& file : files) {
        const auto path = root / file;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        for (unsigned index = 0; index < 4096; ++index) {
            output.put(char(index % 64 < 32 ? 160 : 96));
        }
    }
    std::filesystem::create_directories(root / "speech/pc/english/extras");
    {
        std::ofstream final(root / "speech/pc/english/extras/FINAL.raw", std::ios::binary);
        for (unsigned index = 0; index < 4096; ++index) {
            final.put(char(index % 64 < 32 ? 160 : 96));
        }
    }
    constexpr std::array<std::string_view, 5> languages{"english", "french", "german", "italian", "spanish"};
    for (std::size_t language = 1; language < languages.size(); ++language) {
        for (const auto& file : files) {
            auto translated = file;
            translated.replace(translated.find("english"), 7, languages[language]);
            std::filesystem::create_directories((root / translated).parent_path());
            std::filesystem::copy_file(root / file, root / translated);
        }
    }
    Service service({}, true);
    AssetStore store(root);
    Championship speech(service, store, 0);
    SourceRandom random(1);
    speech.play(ChampionshipCue::standings, state, random);
    expect(speech.playing() && speech.played_count() == 1, "Standings introduction must start exactly one actual voice");
    expect(energy(service.offline_mix(1024)) > 0, "Unsigned RAW speech must reach the real offline mixer");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Mute must silence the actual commentary voice");
    service.apply({});
    for (int index = 0; index < 20 && speech.played_count() == 1; ++index) {
        service.offline_mix(1024);
        speech.update();
    }
    expect(speech.played_count() == 2 && speech.playing(), "Standings summary must wait for the introductory one-shot");
    speech.set_active(false);
    expect(!speech.playing(), "Focus/pause must cut current commentary");
    service.offline_mix(1024);
    speech.set_active(true);
    speech.update();
    expect(speech.played_count() == 2 && !speech.playing() && energy(service.offline_mix(1024)) == 0, "Focus resume must not repeat consumed speech");
    // Foreign playback may reuse the stopped voice slot. Owner cleanup must
    // compare serials and preserve that later playback.
    service.load_pcm(1, std::vector<float>(4096, .2f), 1, 22050);
    const auto other = service.play(1, 0);
    speech.stop();
    expect(service.playing(other), "Commentary cleanup must preserve another owner's reused voice");
    service.stop(other);
    for (std::uint32_t language = 0; language < languages.size(); ++language) {
        expect(championship_speech_language(store, language) == language, "Available speech must retain all five original language IDs");
        SourceRandom mapping_random(1);
        const auto localized = championship_speech(ChampionshipCue::standings, state, mapping_random, language);
        expect(localized[0].find("/" + std::string(languages[language]) + "/") != std::string::npos, "Speech path must use original EN/FR/DE/IT/ES mapping");
        Championship localized_voice(service, store, language);
        SourceRandom voice_random(1);
        localized_voice.play(ChampionshipCue::standings, state, voice_random);
        expect(localized_voice.playing() && energy(service.offline_mix(512)) > 0, "All five source languages must reach actual PCM playback");
    }
    for (const auto language : {3u, 4u}) {
        std::filesystem::remove_all(root / "speech/pc" / languages[language]);
        expect(championship_speech_language(store, language) == 0, "Missing Italian/Spanish commentary must select native English fallback");
        Championship fallback(service, store, language);
        SourceRandom voice_random(1);
        fallback.play(ChampionshipCue::standings, state, voice_random);
        expect(fallback.playing() && energy(service.offline_mix(512)) > 0, "Missing localized assets must retain playable English speech");
    }
    for (std::uint32_t language = 0; language < 5; ++language) {
        Championship ending(service, store, language);
        SourceRandom random(1);
        ending.play(ChampionshipCue::showdown_final, final_state, random);
        expect(ending.playing() == (language == 0), "Resolved English speech fallback must not enable FINAL for another requested language");
        if (language == 0) {
            expect(energy(service.offline_mix(512)) > 0, "Restored final speech must reach actual PCM");
        }
    }
    std::filesystem::remove_all(root);
    source_paths(nullptr);
    final_standings();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            const toca::AssetStore store(argv[1]);
            source_paths(&store);
            final_standings(&store);
            auto finale = toca::race::new_championship(false, 0, "PLAYER", true);
            finale.meeting = 11;
            finale.race = 1;
            finale.phase = toca::race::ChampionshipPhase::complete;
            toca::audio::Service service({}, true);
            toca::audio::Championship speech(service, store, 0);
            toca::SourceRandom random(1);
            speech.play(toca::audio::ChampionshipCue::showdown_final, finale, random);
            expect(speech.playing() && energy(service.offline_mix(8192)) > 0, "Canonical English FINAL.raw must reach actual offline PCM");
        } else {
            throw std::runtime_error("Usage: toca-championship-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original championship commentary families, RAW PCM and lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
