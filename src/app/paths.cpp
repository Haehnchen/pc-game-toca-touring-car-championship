#include "app/paths.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace toca {
namespace {
std::filesystem::path user_storage_root() {
#ifdef _WIN32
    // Match SDL's roaming AppData choice, without its CSIDL_FLAG_CREATE side effect.
    std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> shell(SDL_LoadObject("shell32.dll"), SDL_UnloadObject);
    if (!shell) {
        throw std::runtime_error("Cannot query Windows roaming AppData");
    }
    using QueryPath = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);
    const auto query = reinterpret_cast<QueryPath>(SDL_LoadFunction(shell.get(), "SHGetFolderPathW"));
    wchar_t directory[MAX_PATH]{};
    constexpr int roaming_app_data = 0x001a;
    if (!query || FAILED(query(nullptr, roaming_app_data, nullptr, 0, directory))) {
        throw std::runtime_error("Cannot locate Windows roaming AppData");
    }
    return std::filesystem::path(directory) / "toca" / "toca";
#else
#ifndef __APPLE__
    if (const auto* xdg_data = SDL_getenv("XDG_DATA_HOME")) {
        if (*xdg_data == '\0') {
            throw std::runtime_error("XDG_DATA_HOME must name a user-data directory");
        }
        return std::filesystem::path(xdg_data) / "toca" / "toca";
    }
#endif
    const auto* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!home || *home == '\0') {
        throw std::runtime_error(std::string("Cannot locate portable user storage: ") + SDL_GetError());
    }
#ifdef __APPLE__
    return std::filesystem::path(home) / "Library" / "Application Support" / "toca" / "toca";
#else
    return std::filesystem::path(home) / ".local" / "share" / "toca" / "toca";
#endif
#endif
}

std::filesystem::path default_data_root(const std::filesystem::path& bundled_data_root) {
    const auto* base = SDL_GetBasePath();
    if (base && *base != '\0') {
        const auto bundled = std::filesystem::path(base) / "game-data";
        if (std::filesystem::is_directory(bundled)) {
            return bundled;
        }
    }
    if (bundled_data_root.empty()) {
        throw std::runtime_error("Bundled game data is missing; rebuild the game or select --data-root DIR");
    }
    return bundled_data_root;
}
} // namespace

void require_writable_outside_data(const std::filesystem::path& path, const std::filesystem::path& data_root, const std::filesystem::path& bundled_data_root) {
    if (path.empty()) {
        return;
    }
    const auto resolved_path = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    // Filesystem equivalence also handles case-insensitive Windows/macOS volumes.
    for (const auto& root : {data_root, bundled_data_root}) {
        if (root.empty()) {
            continue;
        }
        const auto resolved_data = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
        for (auto ancestor = resolved_path;; ancestor = ancestor.parent_path()) {
            std::error_code error;
            if (std::filesystem::equivalent(ancestor, resolved_data, error)) {
                throw std::runtime_error("Writable paths must be outside the read-only game-data root: " + path.string());
            }
            if (ancestor == ancestor.parent_path()) {
                break;
            }
        }
    }
}

ApplicationPaths application_paths(const Options& options, const std::filesystem::path& bundled_data_root) {
    ApplicationPaths paths;
    paths.data_root = std::filesystem::weakly_canonical(std::filesystem::absolute(options.data_root.empty() ? default_data_root(bundled_data_root) : options.data_root));
    if (!std::filesystem::is_directory(paths.data_root)) {
        throw std::runtime_error("Game-data root is not a directory: " + paths.data_root.string());
    }
    if (!bundled_data_root.empty()) {
        paths.bundled_data_root = std::filesystem::canonical(bundled_data_root);
        if (!std::filesystem::is_directory(paths.bundled_data_root)) {
            throw std::runtime_error("Bundled game-data root is not a directory: " + paths.bundled_data_root.string());
        }
    }
    require_writable_outside_data(options.config_path, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(options.state_root, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(options.screenshot, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(options.trace, paths.data_root, paths.bundled_data_root);
    if (options.config_path.empty() || options.state_root.empty()) {
        // Path discovery is read-only; SDL_GetPrefPath creates directories before validation.
        const auto user_root = user_storage_root();
        paths.config = options.config_path.empty() ? user_root / "preferences.cfg" : options.config_path;
        paths.state_root = options.state_root.empty() ? user_root / "saves" : options.state_root;
    } else {
        paths.config = options.config_path;
        paths.state_root = options.state_root;
    }
    require_writable_outside_data(paths.config, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(paths.state_root, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(options.screenshot, paths.data_root, paths.bundled_data_root);
    require_writable_outside_data(options.trace, paths.data_root, paths.bundled_data_root);
    return paths;
}
} // namespace toca
