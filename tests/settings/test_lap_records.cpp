#include "core/crc32.hpp"
#include "settings/lap_records.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void fails(Function function, const char* message) {
    bool failed = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, message);
}

std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}
} // namespace

int main() {
    try {
        using namespace toca;
        constexpr std::array<std::uint8_t, 9> crc_input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        require(crc32(crc_input) == 0xcbf43926, "Native file checksum must use IEEE/zlib CRC32");
        const auto path = std::filesystem::current_path() / "records.dat";
        frontend::LapRecords defaults;
        for (std::uint32_t track = 0; track < defaults.size(); ++track) {
            defaults[track] = {599990, "DEFAULT", track, ""};
        }
        auto records = load_lap_records(path, defaults);
        require(records == defaults, "Fresh records must retain compiled defaults");
        require(!improve_lap_record(records, 0, "PLAYER", 0, 0, false) && !improve_lap_record(records, 0, "PLAYER", 0, 101600, true) && records == defaults,
            "Unfinished and boosted attempts must not change track records");
        require(lap_record_code(0, 0, 60000) == "CWAWIVENFM" && lap_record_code(0, 1, 101600) == "DQBQGMDKIY", "Original result-code byte/nibble encoding changed");
        require(improve_lap_record(records, 0, "PLAYER", 1, 101609, false), "First completed faster lap must replace its record");
        require(records[0] == frontend::LapRecord{101600, "PLAYER", 1, "DQBQGMDKIY"}, "Table must use hundredths, actual player/car and original result code");
        require(!improve_lap_record(records, 0, "OTHER", 2, 101600, false) && !improve_lap_record(records, 0, "OTHER", 2, 110000, false),
            "Equal and slower laps must preserve the existing record holder");
        require(records[1] == defaults[1] && improve_lap_record(records, 9, "LAST", 9, 200000, false), "Records must remain per-track, including Lavaland");
        save_lap_records(path, records);
        const auto saved = read(path);
        require(saved.size() == 452 && load_lap_records(path, defaults) == records, "All records, names, cars and codes must survive process-independent reload");
        require(crc32(std::span(saved).first(448)) == 0xa9648968, "Track-record version1 bytes must remain compatible with existing saves");
        const auto malformed = path.parent_path() / "malformed.dat";
        const auto reject_bytes = [&](const auto& bytes, const char* message) {
            {
                std::ofstream output(malformed, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            }
            fails(
                [&] {
                    (void)load_lap_records(malformed, defaults);
                },
                message);
        };
        auto invalid_bytes = saved;
        invalid_bytes.pop_back();
        reject_bytes(invalid_bytes, "Truncated native files must be rejected");
        invalid_bytes = saved;
        invalid_bytes.push_back(0);
        reject_bytes(invalid_bytes, "Trailing native file bytes must be rejected");
        const auto update_checksum = [](auto& bytes) {
            const auto checksum = crc32(std::span(bytes).first(bytes.size() - 4));
            for (unsigned byte = 0; byte < 4; ++byte) {
                bytes[bytes.size() - 4 + byte] = std::uint8_t(checksum >> (byte * 8));
            }
        };
        for (const auto offset : {0, 4}) {
            invalid_bytes = saved;
            invalid_bytes[offset] ^= 1;
            update_checksum(invalid_bytes);
            reject_bytes(invalid_bytes, "An invalid magic or version must fail even with a valid CRC32");
        }
        invalid_bytes = saved;
        std::fill_n(invalid_bytes.begin() + 20, 12, 'A');
        update_checksum(invalid_bytes);
        reject_bytes(invalid_bytes, "An unterminated name must not consume the following native field");
        auto faster = records;
        require(improve_lap_record(faster, 0, "FASTER", 2, 99000, false), "A later faster lap must be eligible");
        const auto directory = path.parent_path() / "blocked.dat";
        std::filesystem::create_directory(directory);
        fails(
            [&] {
                save_lap_records(directory, faster);
            },
            "Failed record writes must be reported");
        require(load_lap_records(path, defaults) == records, "Failed replacement must preserve already persisted records");
        save_lap_records(path, faster);
        require(load_lap_records(path, defaults) == faster, "Failed save must be retryable from retained results");
        auto corrupt = read(path);
        corrupt[20] ^= 1;
        const auto bad = path.parent_path() / "corrupt.dat";
        {
            std::ofstream output(bad, std::ios::binary);
            output.write(reinterpret_cast<const char*>(corrupt.data()), std::streamsize(corrupt.size()));
        }
        fails(
            [&] {
                (void)load_lap_records(bad, defaults);
            },
            "Corrupt native records must fail CRC32 validation");
        auto invalid = faster;
        invalid[0].car_id = 10;
        fails(
            [&] {
                save_lap_records(path, invalid);
            },
            "Invalid record metadata must not replace a valid save");
        require(load_lap_records(path, defaults) == faster, "Rejected metadata changed the valid record file");
        for (const auto& item : std::filesystem::directory_iterator(path.parent_path())) {
            require(item.path().filename().string().find(".tmp-") == std::string::npos, "Atomic save left a temporary file");
        }
        std::cout << "PASS: source record gates/codes, all-track persistence, CRC32, atomic failure and retry\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
