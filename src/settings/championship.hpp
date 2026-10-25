#pragma once

#include "race/championship.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace toca {
// One native checkpoint file. A missing file returns nullopt; a damaged file
// reports an error so the frontend can retain the previous good state.
std::optional<race::ChampionshipState> load_championship(const std::filesystem::path& path);
void save_championship(const std::filesystem::path& path, const race::ChampionshipState& state);

inline constexpr std::uint32_t championship_slot_count = 5;

struct ChampionshipSlot {
    std::optional<race::ChampionshipState> state;
    std::string error;
};

using ChampionshipSlots = std::array<ChampionshipSlot, championship_slot_count>;

// 465898 selects five slots independently for normal/Showdown. Native files
// retain the existing CRC32 checkpoint format; names are the saved driver.
std::filesystem::path championship_slot_path(const std::filesystem::path& state_root, bool showdown, std::uint32_t slot);
ChampionshipSlots load_championship_slots(const std::filesystem::path& state_root, bool showdown);
void save_championship_slot(const std::filesystem::path& state_root, std::uint32_t slot, const race::ChampionshipState& state);
} // namespace toca
