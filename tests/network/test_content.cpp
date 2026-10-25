#include "assets/store.hpp"
#include "network/content.hpp"
#include "scene/catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct PrivateDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("toca-content-" + std::to_string(std::random_device{}()));

    PrivateDirectory() {
        std::filesystem::create_directory(path);
    }

    ~PrivateDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    void write(const std::string& name, const std::string& bytes) const {
        const auto destination = path / name;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary);
        output << bytes;
        require(bool(output), "Cannot write private content fixture");
    }
};
} // namespace

int main() {
    try {
        PrivateDirectory directory;
        directory.write("data/config.bfp", "configuration");
        directory.write("data/allcars.bfp", "car physics");
        for (const auto& track : toca::scene::track_catalog) {
            for (const auto suffix : {"keep.bfp", "free.bfp"}) {
                const auto name = std::string(track.archive_prefix) + suffix;
                directory.write(name, name);
            }
        }
        const toca::AssetStore store(directory.path);
        const auto initial = toca::network::simulation_content_crc32(store);
        require(initial == toca::network::simulation_content_crc32(store), "Identical game data must produce the same content identity without an original executable");
        directory.write("assets/ui/menu.webp", "local artwork");
        require(initial == toca::network::simulation_content_crc32(store), "Presentation overrides must not change the simulation content identity");
        directory.write("data/config.bfp", "modified configuration");
        require(initial != toca::network::simulation_content_crc32(store), "Changed canonical simulation data must change the network content identity");
        std::filesystem::remove(directory.path / "data/allcars.bfp");
        bool rejected = false;
        try {
            (void)toca::network::simulation_content_crc32(store);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Missing canonical simulation inputs must not produce an accepted fingerprint");
        std::cout << "PASS: network content identity uses embedded tables and canonical data without an original executable\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
