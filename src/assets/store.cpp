#include "assets/store.hpp"

#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace toca {
namespace {
char ascii_lower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool ascii_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

bool within(const std::filesystem::path& path, const std::filesystem::path& root) {
    for (auto ancestor = path;; ancestor = ancestor.parent_path()) {
        std::error_code error;
        if (std::filesystem::equivalent(ancestor, root, error)) {
            return true;
        }
        if (ancestor == ancestor.parent_path()) {
            return false;
        }
    }
}

std::optional<std::filesystem::path> find_in_root(const std::filesystem::path& root, const std::vector<std::string_view>& components, std::string_view relative_path) {
    auto current = root;
    for (const auto component : components) {
        if (!std::filesystem::is_directory(current)) {
            return std::nullopt;
        }
        std::optional<std::filesystem::path> matched;
        for (const auto& entry : std::filesystem::directory_iterator(current)) {
            if (ascii_equal(entry.path().filename().string(), component)) {
                if (matched) {
                    throw std::runtime_error("Ambiguous ASCII-insensitive asset component: " + std::string(component));
                }
                matched = entry.path();
            }
        }
        if (!matched) {
            return std::nullopt;
        }
        current = std::filesystem::canonical(*matched);
        if (!within(current, root)) {
            throw std::runtime_error("Asset path escapes the game-data root: " + std::string(relative_path));
        }
    }
    return current;
}

std::vector<std::uint8_t> read_asset(const std::filesystem::path& path, std::string_view relative_path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Asset is not a regular file: " + std::string(relative_path));
    }
    const auto size = std::filesystem::file_size(path);
    if (size > std::numeric_limits<std::size_t>::max() || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("Asset is too large to read: " + std::string(relative_path));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open read-only asset: " + std::string(relative_path));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read complete asset: " + std::string(relative_path));
    }
    if (file.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Asset changed size while reading: " + std::string(relative_path));
    }
    return bytes;
}
} // namespace

AssetStore::AssetStore(const std::filesystem::path& root, const std::filesystem::path& bundled_root)
    : root_(std::filesystem::canonical(root)), bundled_root_(bundled_root.empty() ? std::filesystem::path{} : std::filesystem::canonical(bundled_root)) {
    if (!std::filesystem::is_directory(root_)) {
        throw std::runtime_error("Asset root is not a directory: " + root_.string());
    }
    if (!bundled_root_.empty() && !std::filesystem::is_directory(bundled_root_)) {
        throw std::runtime_error("Bundled asset root is not a directory: " + bundled_root_.string());
    }
}

AssetStore AssetStore::for_loading(std::function<void()> progress) const {
    AssetStore loading(root_, bundled_root_);
    loading.archives_.emplace();
    loading.progress_ = std::move(progress);
    return loading;
}

const std::filesystem::path& AssetStore::root() const {
    return root_;
}

const std::filesystem::path& AssetStore::bundled_root() const {
    return bundled_root_;
}

std::optional<std::filesystem::path> AssetStore::find(std::string_view relative_path) const {
    if (progress_) {
        progress_();
    }
    if (relative_path.empty() || relative_path.front() == '/' || relative_path.front() == '\\' || relative_path.find(':') != std::string_view::npos
        || relative_path.find('\0') != std::string_view::npos) {
        throw std::runtime_error("Asset paths must be relative to the game-data root");
    }
    std::vector<std::string_view> components;
    std::size_t position = 0;
    while (position < relative_path.size()) {
        const auto separator = relative_path.find_first_of("/\\", position);
        const auto component = relative_path.substr(position, separator == std::string_view::npos ? relative_path.size() - position : separator - position);
        if (component.empty() || component == "." || component == "..") {
            throw std::runtime_error("Asset paths cannot contain empty, dot or parent components");
        }
        components.push_back(component);
        if (separator == std::string_view::npos) {
            break;
        }
        position = separator + 1;
        if (position == relative_path.size()) {
            throw std::runtime_error("Asset paths cannot end with a separator");
        }
    }
    if (!bundled_root_.empty()) {
        if (const auto bundled = find_in_root(bundled_root_, components, relative_path)) {
            return bundled;
        }
    }
    return find_in_root(root_, components, relative_path);
}

std::filesystem::path AssetStore::resolve(std::string_view relative_path) const {
    if (const auto path = find(relative_path)) {
        return *path;
    }
    throw std::runtime_error("Asset not found: " + std::string(relative_path));
}

std::vector<std::uint8_t> AssetStore::read_file(std::string_view relative_path) const {
    auto bytes = read_asset(resolve(relative_path), relative_path);
    if (progress_) {
        progress_();
    }
    return bytes;
}

BfpArchive AssetStore::open_archive(std::string_view relative_path) const {
    if (!archives_) {
        return BfpArchive(read_file(relative_path));
    }
    const auto path = resolve(relative_path);
    if (const auto found = archives_->find(path); found != archives_->end()) {
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Asset is not a regular file: " + std::string(relative_path));
        }
        return found->second;
    }
    const auto archive = archives_->emplace(path, BfpArchive(read_asset(path, relative_path))).first->second;
    if (progress_) {
        progress_();
    }
    return archive;
}
} // namespace toca
