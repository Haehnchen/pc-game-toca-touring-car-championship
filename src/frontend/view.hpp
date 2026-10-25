#pragma once

#include "frontend/controller.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace toca {
class AssetStore;
}

namespace toca::gpu {
class Renderer;
struct Frame;
} // namespace toca::gpu

namespace toca::frontend {
class View {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    View(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t background_index = 0);
    ~View();
    void load_frontend();
    const std::array<std::string, 5>& name_defaults() const;
    gpu::Frame frame(const Controller& controller);
};
} // namespace toca::frontend
