#pragma once

#include "frontend/records.hpp"

#include <filesystem>
#include <string_view>

namespace toca {
frontend::LapRecords load_lap_records(const std::filesystem::path& path, const frontend::LapRecords& defaults);
void save_lap_records(const std::filesystem::path& path, const frontend::LapRecords& records);
// 426f20/446240: only a strictly faster completed Time Trial lap replaces its
// track record. XBOOSTME suppresses this table, independently of ghost saving.
bool improve_lap_record(frontend::LapRecords& records, std::uint32_t track, std::string_view driver, std::uint32_t car, std::uint32_t lap_ms, bool boost);
// Original visible result code, not the native file's CRC32 checksum.
std::string lap_record_code(std::uint32_t track, std::uint32_t car, std::uint32_t lap_ms);
} // namespace toca
