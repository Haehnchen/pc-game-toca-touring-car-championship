#include "app/runtime_resources.hpp"

#include "SDL3/SDL.h"
#include "frontend/controller.hpp"

#include <algorithm>

namespace toca {
void create_output_parent(const std::filesystem::path& path) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
}

std::filesystem::path resource_root() {
    const auto* base = SDL_GetBasePath();
    if (!base) {
        throw std::runtime_error(std::string("Cannot locate application resources: ") + SDL_GetError());
    }
    return std::filesystem::path(base) / "assets";
}

std::filesystem::path mega_model_directory(std::uint32_t detail) {
#ifdef TOCA_MEGA_MODELS
    if (detail == graphics_mega_detail) {
        return resource_root() / "models/mega";
    }
#else
    (void)detail;
#endif
    return {};
}

frontend::cheats::State scene_cheats(const frontend::Controller& menu) {
    return frontend::cheats::for_mode(menu.cheats_state(), static_cast<std::uint32_t>(menu.setup().mode));
}

} // namespace toca
