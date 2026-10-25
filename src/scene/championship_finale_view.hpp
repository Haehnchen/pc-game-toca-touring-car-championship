#pragma once

#include "frontend/font.hpp"
#include "scene/championship_finale.hpp"
#include "scene/view.hpp"

#include <memory>

namespace toca::scene {
struct ChampionshipFinaleResources {
    Model pace, transporter;
    // 449b9e reads member2+standings_actor/2 directly, in archive order.
    std::array<Model, 8> teams;
    std::vector<std::uint8_t> credits;
    std::array<frontend::Font, 2> fonts; // BIG640, LIL640.
};

// Optional per-vehicle Mega replacements retain the original convoy actors.
ChampionshipFinaleResources load_championship_finale_resources(const AssetStore& store, const std::filesystem::path& mega_models = {});
// 449bce: rank0..2 translations are local to the transporter root.
std::array<float, 16> championship_finale_trophy_transform(const ChampionshipFinaleCarPose& transporter, std::uint32_t rank);

struct ChampionshipFinaleDrawCounts {
    std::size_t world_vertices = 0;
    // Submitted source triangles before shader clipping: pace, transporter,
    // then the three podium drivers in source order.
    std::array<std::uint32_t, 5> model_triangles{};
    std::uint32_t credit_glyphs = 0;
};

// Uses the ordinary Silverstone SceneView and shared resident model resources.
// Both resource owners must outlive this view. No race HUD is allocated.
class ChampionshipFinaleView {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    ChampionshipFinaleView(gpu::Renderer& renderer, const SceneResources& scene, const ChampionshipFinaleResources& resources, std::uint32_t language,
        const std::filesystem::path& artwork_directory = {});
    ~ChampionshipFinaleView();
    gpu::Frame frame(const ChampionshipFinaleFrame& state, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors, std::uint32_t width, std::uint32_t height,
        bool mirror = false, bool upside = false) const;
    void frame_into(gpu::Frame& output, const ChampionshipFinaleFrame& state, std::span<const std::uint8_t> flag_variants, const HorizonFrame& actors, std::uint32_t width,
        std::uint32_t height, bool mirror = false, bool upside = false) const;
    const ChampionshipFinaleDrawCounts& draw_counts() const;
};
} // namespace toca::scene
