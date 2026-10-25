#pragma once

#include "core/contracts.hpp"
#include "core/game_mode.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace toca {
class AssetStore;
}

namespace toca::scene {
enum class PausePage {
    main,
    sound,
    race_again,
    quit_time_trial,
    quit_championship
};
enum class PauseAction {
    none,
    resume,
    end_qualifying,
    restart_race,
    end_race,
    end_time_trial,
    end_championship
};
enum class PauseCue : std::uint32_t {
    navigation = 78,
    confirm = 79,
    option = 80
};

struct PauseUpdate {
    PauseAction action = PauseAction::none;
    bool audio_changed = false;
    std::vector<PauseCue> cues;
};

class PauseController {
    bool active_ = false, qualifying_ = false;
    GameMode mode_ = GameMode::single_race;
    PausePage page_ = PausePage::main;
    std::size_t selection_ = 0;
    AudioSettings audio_;

public:
    void open(bool qualifying, const AudioSettings& audio, GameMode mode = GameMode::single_race);
    void open_race_again(const AudioSettings& audio);
    void close();
    PauseUpdate update(const InputSnapshot&);
    bool active() const;
    bool qualifying() const;
    GameMode mode() const;
    PausePage page() const;
    std::size_t selection() const;
    std::size_t row_count() const;
    const AudioSettings& audio() const;
};

struct PauseLabels {
    // Original GSTRINGS24..38, plus82 (IN GAME MUSIC ON).
    std::array<std::string, 15> lines;
    std::string music_on;
    std::string quit_time_trial, quit_championship;
};

struct PauseMenu {
    std::string title;
    std::vector<std::string> rows;
};

PauseLabels load_pause_labels(const AssetStore&, std::uint32_t language);
PauseMenu pause_menu(const PauseLabels&, const PauseController&);
} // namespace toca::scene
