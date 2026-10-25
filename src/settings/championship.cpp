#include "settings/championship.hpp"

#include "settings/storage.hpp"

#include <stdexcept>

namespace toca {
namespace {
constexpr std::uint32_t magic = 0x48434354; // TCCH, native championship checkpoint.
constexpr std::size_t file_size = 4 * (11 + 16 + 3 + 4 + 16 + 16 * 3) + 12 + 16 * 40;

constexpr state_file::Format format{file_size, magic, "Championship save"};
} // namespace

std::optional<race::ChampionshipState> load_championship(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    const auto bytes = state_file::read(path, format);
    state_file::Reader reader{std::span(bytes).subspan(8, file_size - 12), format.context};
    race::ChampionshipState state;
    const auto flags = reader.number();
    if (flags > 15) {
        throw std::runtime_error("Championship save: invalid flags");
    }
    state.full_length = (flags & 1) != 0;
    state.showdown = (flags & 2) != 0;
    state.has_won = (flags & 4) != 0;
    state.transmission_manual = (flags & 8) != 0;
    state.player_car = reader.number();
    state.local_actor = reader.number();
    state.meeting = reader.number();
    state.race = reader.number();
    state.phase = race::ChampionshipPhase(reader.number());
    // Reserved fields belong to this sole native format, not an original save.
    if (reader.number() != 0 || reader.number() != 0) {
        throw std::runtime_error("Championship save: invalid reserved fields");
    }
    state.driver = reader.text(12, "name");
    for (auto& points : state.points) {
        points = reader.number();
    }
    for (auto& count : state.podiums) {
        count = reader.number();
    }
    for (auto& award : state.awards) {
        award = reader.number();
    }
    for (auto& actor : state.grid) {
        actor = reader.number();
    }
    for (auto& driver : state.drivers) {
        driver.actor_id = reader.number();
        driver.car_id = reader.number();
        driver.time_ms = reader.number();
        driver.driver = reader.text(40, "name");
    }
    race::validate_championship(state);
    return state;
}

void save_championship(const std::filesystem::path& path, const race::ChampionshipState& state) {
    race::validate_championship(state);
    std::array<std::uint8_t, file_size> bytes{};
    state_file::Writer writer{std::span(bytes).subspan(8, file_size - 12), format.context};
    writer.number(std::uint32_t(state.full_length) | std::uint32_t(state.showdown) << 1 | std::uint32_t(state.has_won) << 2 | std::uint32_t(state.transmission_manual) << 3);
    writer.number(state.player_car);
    writer.number(state.local_actor);
    writer.number(state.meeting);
    writer.number(state.race);
    writer.number(std::uint32_t(state.phase));
    writer.number(0);
    writer.number(0);
    writer.text(state.driver, 12);
    for (const auto points : state.points) {
        writer.number(points);
    }
    for (const auto count : state.podiums) {
        writer.number(count);
    }
    for (const auto award : state.awards) {
        writer.number(award);
    }
    for (const auto actor : state.grid) {
        writer.number(actor);
    }
    for (const auto& driver : state.drivers) {
        writer.number(driver.actor_id);
        writer.number(driver.car_id);
        writer.number(driver.time_ms);
        writer.text(driver.driver, 40);
    }
    writer.finish();
    state_file::write(path, bytes, format);
}

std::filesystem::path championship_slot_path(const std::filesystem::path& state_root, bool showdown, std::uint32_t slot) {
    if (slot >= championship_slot_count) {
        throw std::runtime_error("Championship save slot must be0..4");
    }
    return state_root / "championship" / (showdown ? "showdown" : "normal") / ("slot-" + std::to_string(slot + 1) + ".dat");
}

ChampionshipSlots load_championship_slots(const std::filesystem::path& state_root, bool showdown) {
    ChampionshipSlots slots;
    for (std::uint32_t slot = 0; slot < slots.size(); ++slot) {
        try {
            slots[slot].state = load_championship(championship_slot_path(state_root, showdown, slot));
            if (slots[slot].state && slots[slot].state->showdown != showdown) {
                throw std::runtime_error("Championship save: slot belongs to the other championship variant");
            }
        } catch (const std::exception& error) {
            slots[slot].state.reset();
            slots[slot].error = error.what();
        }
    }
    return slots;
}

void save_championship_slot(const std::filesystem::path& state_root, std::uint32_t slot, const race::ChampionshipState& state) {
    save_championship(championship_slot_path(state_root, state.showdown, slot), state);
}

} // namespace toca
