#include "settings/lap_records.hpp"

#include "settings/storage.hpp"

#include <stdexcept>

namespace toca {
namespace {
constexpr std::size_t record_size = 44, file_size = 8 + 10 * record_size + 4;
constexpr std::uint32_t magic = 0x524c4354; // "TCLR", native track records.

constexpr state_file::Format format{file_size, magic, "Track records"};

void validate(const frontend::LapRecord& record) {
    if (record.lap_ms == 0 || record.lap_ms >= 999999 || record.car_id >= 10 || record.driver.size() >= 12 || record.code.size() >= 16
        || record.driver.find('\0') != std::string::npos || record.code.find('\0') != std::string::npos) {
        throw std::runtime_error("Track records: invalid time, car or text");
    }
}
} // namespace

frontend::LapRecords load_lap_records(const std::filesystem::path& path, const frontend::LapRecords& defaults) {
    if (!std::filesystem::exists(path)) {
        return defaults;
    }
    const auto bytes = state_file::read(path, format);
    state_file::Reader reader{std::span(bytes).subspan(8, file_size - 12), format.context};
    frontend::LapRecords records;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto minutes = reader.number(), seconds = reader.number(), milliseconds = reader.number();
        if (minutes > 16 || seconds >= 60 || milliseconds >= 1000) {
            throw std::runtime_error("Track records: invalid split time");
        }
        records[index] = {minutes * 60000 + seconds * 1000 + milliseconds, reader.text(12, "text"), reader.number(), reader.text(16, "text")};
        validate(records[index]);
    }
    return records;
}

void save_lap_records(const std::filesystem::path& path, const frontend::LapRecords& records) {
    std::array<std::uint8_t, file_size> bytes{};
    state_file::Writer writer{std::span(bytes).subspan(8, file_size - 12), format.context};
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        validate(record);
        writer.number(record.lap_ms / 60000);
        writer.number(record.lap_ms / 1000 % 60);
        writer.number(record.lap_ms % 1000);
        writer.text(record.driver, 12);
        writer.number(record.car_id);
        writer.text(record.code, 16);
    }
    writer.finish();
    state_file::write(path, bytes, format);
}

std::string lap_record_code(std::uint32_t track, std::uint32_t car, std::uint32_t lap_ms) {
    if (track >= 10 || car >= 10 || lap_ms == 0 || lap_ms >= 999999) {
        throw std::runtime_error("Track records: invalid result-code input");
    }
    // 426f20 packs track+car*10, minutes+60, seconds, hundredths.
    // 420670(key3)/4206b0 subtract the next byte then add "INCREDIBLY".
    std::array<std::uint8_t, 4> encoded{std::uint8_t(track + car * 10), std::uint8_t(lap_ms / 60000 + 60), std::uint8_t(lap_ms / 1000 % 60), std::uint8_t(lap_ms % 1000 / 10)};
    constexpr std::string_view key = "INCREDIBLY";
    std::uint8_t checksum = 0;
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const auto next = index + 1 < encoded.size() ? encoded[index + 1] : 0;
        encoded[index] = std::uint8_t(encoded[index] - next + key[index]);
        checksum = std::uint8_t(checksum + encoded[index]);
    }
    // 420870 prefixes the low-byte sum; high nibbles use A..P, low K..Z.
    std::string code;
    const auto append = [&](std::uint8_t value) {
        code += char('A' + (value >> 4));
        code += char('K' + (value & 15));
    };
    append(checksum);
    for (const auto value : encoded) {
        append(value);
    }
    return code;
}

bool improve_lap_record(frontend::LapRecords& records, std::uint32_t track, std::string_view driver, std::uint32_t car, std::uint32_t lap_ms, bool boost) {
    if (track >= records.size() || car >= 10 || driver.size() >= 12 || driver.find('\0') != std::string_view::npos) {
        throw std::runtime_error("Track records: invalid completed lap metadata");
    }
    // Source 426f20 stores its displayed hundredths times ten, not sub-centiseconds.
    lap_ms = lap_ms / 10 * 10;
    if (boost || lap_ms == 0 || lap_ms >= records[track].lap_ms) {
        return false;
    }
    records[track] = {lap_ms, std::string(driver), car, lap_record_code(track, car, lap_ms)};
    return true;
}
} // namespace toca
