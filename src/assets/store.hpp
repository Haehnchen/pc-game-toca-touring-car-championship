#pragma once

#include "formats/bfp.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string_view>

namespace toca {
// Read-only loose-file/archive access. BFP member identity remains archive + index.
class AssetStore {
    std::filesystem::path root_;
    std::filesystem::path bundled_root_;
    mutable std::optional<std::map<std::filesystem::path, BfpArchive>> archives_;
    std::function<void()> progress_;

public:
    // Bundled files take precedence; other files remain in the main root.
    explicit AssetStore(const std::filesystem::path& root, const std::filesystem::path& bundled_root = {});
    // Owns shared immutable archives only for this temporary loading store's lifetime.
    AssetStore for_loading(std::function<void()> progress = {}) const;
    const std::filesystem::path& root() const;
    const std::filesystem::path& bundled_root() const;
    // Missing paths return nullopt; invalid, ambiguous and escaping paths still fail.
    std::optional<std::filesystem::path> find(std::string_view relative_path) const;
    std::filesystem::path resolve(std::string_view relative_path) const;
    std::vector<std::uint8_t> read_file(std::string_view relative_path) const;
    BfpArchive open_archive(std::string_view relative_path) const;
};
} // namespace toca
