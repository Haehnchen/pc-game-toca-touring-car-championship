#pragma once

#include "app/options.hpp"

namespace toca {
struct ApplicationPaths {
    std::filesystem::path data_root;
    std::filesystem::path bundled_data_root;
    std::filesystem::path config;
    std::filesystem::path state_root;
};

ApplicationPaths application_paths(const Options& options, const std::filesystem::path& bundled_data_root = {});
void require_writable_outside_data(const std::filesystem::path& path, const std::filesystem::path& data_root, const std::filesystem::path& bundled_data_root = {});
} // namespace toca
