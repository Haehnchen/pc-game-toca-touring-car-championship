#include "core/crc32.hpp"
#include "settings/championship.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        using namespace toca;
        const auto root = std::filesystem::current_path() / "championship-slot-state";
        require(!std::filesystem::exists(root), "Slot checks require isolated storage");
        const auto empty = load_championship_slots(root, false);
        for (const auto& slot : empty) {
            require(!slot.state && slot.error.empty(), "Missing slots must remain independent empty entries");
        }
        require(!std::filesystem::exists(root), "Listing missing slots must not create saves");
        for (bool showdown : {false, true}) {
            for (std::uint32_t slot = 0; slot < championship_slot_count; ++slot) {
                auto state = race::new_championship(slot % 2 != 0, slot, "DRIVER " + std::to_string(slot + 1), showdown);
                state.transmission_manual = slot % 2 != 0;
                state.meeting = slot;
                state.race = slot % 2;
                state.points[state.local_actor] = 21 * slot;
                save_championship_slot(root, slot, state);
                if (showdown && slot == 4) {
                    std::ifstream input(championship_slot_path(root, showdown, slot), std::ios::binary);
                    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
                    require(bytes.size() == 1044 && crc32(std::span(bytes).first(1040)) == 0xd66de45b,
                        "Championship checkpoint version1 bytes must remain compatible with existing saves");
                }
                const auto loaded = load_championship_slots(root, showdown);
                require(loaded[slot].state == state && loaded[slot].error.empty(), "Each of the five variant slots must preserve its complete named championship");
            }
        }
        auto normal = load_championship_slots(root, false), showdown = load_championship_slots(root, true);
        auto replacement = *normal[2].state;
        replacement.driver = "REPLACED";
        replacement.points[replacement.local_actor] += 12;
        save_championship_slot(root, 2, replacement);
        const auto changed = load_championship_slots(root, false);
        for (std::size_t slot = 0; slot < championship_slot_count; ++slot) {
            require(changed[slot].state == (slot == 2 ? replacement : *normal[slot].state), "Replacing a selected slot must preserve all neighboring slots");
            require(load_championship_slots(root, true)[slot].state == showdown[slot].state, "Normal-slot replacement must preserve every Showdown slot");
        }
        const auto damaged = championship_slot_path(root, false, 1);
        {
            std::fstream file(damaged, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(28);
            file.put(char(0xff));
        }
        const auto partial = load_championship_slots(root, false);
        require(!partial[1].state && !partial[1].error.empty() && partial[0].state == normal[0].state && partial[2].state == replacement,
            "A CRC32-damaged slot must report its own error while leaving other named saves loadable");
        save_championship(championship_slot_path(root, false, 4), *showdown[4].state);
        require(!load_championship_slots(root, false)[4].state && !load_championship_slots(root, false)[4].error.empty(),
            "A checkpoint for the other championship variant must not enter this variant's slot list");
        rejects(
            [&] {
                (void)championship_slot_path(root, false, 5);
            },
            "Slot indexing must reject an absent sixth slot");
        const auto blocked = root / "blocked";
        {
            std::ofstream file(blocked);
            file << "occupied";
        }
        rejects(
            [&] {
                save_championship_slot(blocked, 0, replacement);
            },
            "A failed slot write must report an error");
        require(load_championship_slots(root, false)[2].state == replacement, "A failed write elsewhere must retain the previous good save");
        std::cout << "PASS: five named slots per variant, independent replacement, CRC32 errors and atomic save failures\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
