#pragma once

#include "formats/hpc.hpp"
#include "scene/resources.hpp"

#include <optional>

namespace toca::scene {
// 426b10 chooses the nearest authored camera, then 26830 points it at the actor.
// Call update once per active source tick, not once per presentation.
class FinishCamera {
    std::vector<SceneCamera> cameras_;
    std::uint32_t selected_ = 0, remaining_ = 0;
    std::uint32_t results_hold_ = 0, previous_results_ = 0;
    std::optional<std::array<std::int32_t, 3>> close_origin_;

public:
    explicit FinishCamera(std::span<const SceneCamera> cameras);
    //26b00 resets only the selector countdown;1ec50 additionally selects actor0.
    //The results hold, previous-results actor and close-origin scratch survive.
    void reset_selection_cache();
    void reset_session();
    void update(const std::array<std::int32_t, 3>& target_q16, bool race_results = false);
    std::uint32_t selected() const;
    std::array<std::int32_t, 3> position() const;
    std::array<std::int32_t, 3> actor_position(std::uint32_t actor) const;
    //Actual26830 publication mutates the authored node. Const presentation
    //queries above/below never advance this source lifetime.
    std::array<float, 16> publish(const std::array<std::int32_t, 3>& target_q16, const HpcSurface* surface, bool close, std::array<float, 16>* published_world = nullptr);
    //26830 arg4=1 keeps the authored look direction but moves15units from
    //the target and clamps above the actual42e9f0 ground by3units.
    std::array<std::int32_t, 3> position(const std::array<std::int32_t, 3>& target_q16, const HpcSurface* surface, bool close) const;
    std::array<float, 16> world_to_view(
        const std::array<std::int32_t, 3>& target_q16, const HpcSurface* surface = nullptr, bool close = false, std::array<float, 16>* published_world = nullptr) const;
};
} // namespace toca::scene
