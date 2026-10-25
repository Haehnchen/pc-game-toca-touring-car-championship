#include "scene/pause.hpp"

#include "assets/store.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::scene {
void PauseController::open(bool qualifying, const AudioSettings& audio, GameMode mode) {
    if (audio.effects_volume > 100 || audio.music_volume > 100) {
        throw std::runtime_error("Pause audio volume must be within0..100");
    }
    active_ = true;
    mode_ = mode;
    qualifying_ = qualifying && mode != GameMode::time_trial && mode != GameMode::linkup;
    audio_ = audio;
    page_ = PausePage::main;
    selection_ = 0;
}

void PauseController::open_race_again(const AudioSettings& audio) {
    open(false, audio);
    page_ = PausePage::race_again;
}

void PauseController::close() {
    active_ = false;
}

PauseUpdate PauseController::update(const InputSnapshot& input) {
    PauseUpdate result;
    if (!active_ || !input.focused) {
        return result;
    }
    const auto pressed = [&](Action action) {
        return (input.pressed & action_mask(action)) != 0;
    };
    // 441720 dispatch order: Down, Up, Left, Right, Enter, Escape.
    if (pressed(Action::down) || pressed(Action::up)) {
        selection_ = pressed(Action::down) ? (selection_ + 1) % row_count() : (selection_ + row_count() - 1) % row_count();
        result.cues.push_back(PauseCue::navigation);
    } else if (pressed(Action::left) || pressed(Action::right)) {
        if (page_ == PausePage::sound && selection_ < 3) {
            const auto before = audio_;
            if (selection_ == 2) {
                audio_.game_music = !audio_.game_music;
            } else {
                auto& volume = selection_ == 0 ? audio_.effects_volume : audio_.music_volume;
                volume = pressed(Action::left) ? (volume > 10 ? volume - 10 : 0) : std::min(100u, volume + 10);
            }
            result.audio_changed = before != audio_;
            if (result.audio_changed) {
                result.cues.push_back(PauseCue::option);
            }
        }
    } else if (pressed(Action::confirm)) {
        result.cues.push_back(PauseCue::confirm);
        if (page_ == PausePage::main) {
            if (selection_ == 0) {
                result.action = PauseAction::resume;
            } else if (selection_ == 1) {
                page_ = PausePage::sound;
                selection_ = 0;
            } else if (qualifying_ && selection_ == 2) {
                result.action = PauseAction::end_qualifying;
            } else if (mode_ == GameMode::linkup) {
                //441720 leaves Linkup directly; RACE AGAIN is single-race only.
                result.action = PauseAction::end_race;
            } else {
                page_ = mode_ == GameMode::championship ? PausePage::quit_championship : mode_ == GameMode::time_trial ? PausePage::quit_time_trial : PausePage::race_again;
                selection_ = mode_ == GameMode::single_race ? 0 : 1;
            }
        } else if (page_ == PausePage::sound && selection_ == 3) {
            page_ = PausePage::main;
            selection_ = 0;
        } else if (page_ == PausePage::race_again) {
            result.action = selection_ == 0 ? PauseAction::restart_race : PauseAction::end_race;
        } else if (page_ == PausePage::quit_time_trial || page_ == PausePage::quit_championship) {
            if (selection_ == 0) {
                result.action = page_ == PausePage::quit_championship ? PauseAction::end_championship : PauseAction::end_time_trial;
            } else {
                page_ = PausePage::main;
                selection_ = 0;
            }
        }
    } else if (pressed(Action::back) && page_ != PausePage::race_again && page_ != PausePage::quit_time_trial && page_ != PausePage::quit_championship) {
        // 440510 case3 clears dcdbc: RACE AGAIN requires an explicit Yes/No.
        result.action = PauseAction::resume;
    }
    if (result.action != PauseAction::none) {
        close();
    }
    return result;
}

bool PauseController::active() const {
    return active_;
}

bool PauseController::qualifying() const {
    return qualifying_;
}

GameMode PauseController::mode() const {
    return mode_;
}

PausePage PauseController::page() const {
    return page_;
}

std::size_t PauseController::selection() const {
    return selection_;
}

std::size_t PauseController::row_count() const {
    return page_ == PausePage::main ? (qualifying_ ? 4 : 3) : page_ == PausePage::sound ? 4 : 2;
}

const AudioSettings& PauseController::audio() const {
    return audio_;
}

PauseLabels load_pause_labels(const AssetStore& store, std::uint32_t language) {
    if (language > 4) {
        throw std::runtime_error("Pause view requires original language0..4");
    }
    const auto archive = store.open_archive("data/config.bfp");
    const auto member = archive.member(2 + language);
    const auto bytes = member.bytes();
    PauseLabels result;
    std::size_t begin = 0;
    // 42bee0 advances over entire runs of CR/LF, one pointer per nonempty line.
    for (std::size_t line = 0; line <= 125; ++line) {
        auto end = begin;
        while (end < bytes.size() && bytes[end] != '\r' && bytes[end] != '\n') {
            ++end;
        }
        if (end == begin || end == bytes.size()) {
            throw std::runtime_error("Pause labels are missing from original GSTRINGS");
        }
        if (line >= 24 && line <= 38) {
            result.lines[line - 24].assign(bytes.begin() + std::ptrdiff_t(begin), bytes.begin() + std::ptrdiff_t(end));
        } else if (line == 82) {
            result.music_on.assign(bytes.begin() + std::ptrdiff_t(begin), bytes.begin() + std::ptrdiff_t(end));
        } else if (line == 123 || line == 125) {
            auto& title = line == 123 ? result.quit_time_trial : result.quit_championship;
            title.assign(bytes.begin() + std::ptrdiff_t(begin), bytes.begin() + std::ptrdiff_t(end));
        }
        begin = end;
        while (begin < bytes.size() && (bytes[begin] == '\r' || bytes[begin] == '\n')) {
            ++begin;
        }
    }
    return result;
}

PauseMenu pause_menu(const PauseLabels& labels, const PauseController& controller) {
    const auto& line = labels.lines;
    PauseMenu result;
    if (controller.page() == PausePage::main) {
        result.title = line[0];
        result.rows = {line[1], line[2]};
        if (controller.qualifying()) {
            result.rows.push_back(line[3]);
        }
        result.rows.push_back(line[controller.mode() == GameMode::championship ? 5 : controller.mode() == GameMode::time_trial ? 4 : 6]);
    } else if (controller.page() == PausePage::sound) {
        const auto volume_label = [](const std::string& format, std::uint32_t volume) {
            const auto conversion = format.find("%d");
            if (conversion == std::string::npos || format.find('%', conversion + 2) != std::string::npos) {
                throw std::runtime_error("Invalid original pause volume format");
            }
            auto text = format;
            text.replace(conversion, 2, std::to_string(volume));
            return text;
        };
        const auto& audio = controller.audio();
        result.title = line[2];
        result.rows = {audio.effects_volume == 0 ? line[8] : volume_label(line[7], audio.effects_volume), volume_label(line[9], audio.music_volume),
            audio.game_music ? labels.music_on : line[10], line[11]};
    } else {
        result.title = controller.page() == PausePage::quit_championship ? labels.quit_championship
                       : controller.page() == PausePage::quit_time_trial ? labels.quit_time_trial
                                                                         : line[14];
        result.rows = {line[12], line[13]};
    }
    return result;
}
} // namespace toca::scene
