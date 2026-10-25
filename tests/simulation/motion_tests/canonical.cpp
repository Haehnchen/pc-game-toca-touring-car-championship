#include "assets/store.hpp"
#include "fixtures.hpp"
#include "scene/catalog.hpp"

#include <iostream>

namespace toca::test::motion {
using namespace toca::simulation;

void canonical(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    for (const auto track : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u}) {
        const auto prefix = std::string(toca::scene::track_content(track).archive_prefix);
        const auto keep = store.open_archive(prefix + "keep.bfp");
        const auto free = store.open_archive(prefix + "free.bfp");
        const auto ground = toca::parse_terrain(free.member(1).bytes());
        const toca::scene::BarrierData barriers(keep.member(1).bytes(), ground.sector_centers);
        const auto weather = toca::scene::resolve_setup({track, 0, 0, 3}).weather;
        const toca::HpcSurface surface(keep.member(0).bytes());
        DamageContext damage(surface, &barriers);
        const auto grid = toca::scene::decode_start_grid(keep.member(3).bytes());
        const auto qualifying = toca::scene::single_qualifying_start(keep.member(1).bytes());
        const auto race = toca::scene::single_race_start(grid);
        for (const auto start : {race, qualifying}) {
            const auto body = toca::scene::initial_body_pose(surface, start, 0);
            std::array<SurfaceRoughness, 3> independent_roughness;
            Motion straight(surface, body, 0, true, weather, start.source_index != 15, damage, independent_roughness[0], &barriers),
                braking(surface, body, 0, true, weather, start.source_index != 15, damage, independent_roughness[1], &barriers),
                turning(surface, body, 0, true, weather, start.source_index != 15, damage, independent_roughness[2], &barriers);
            for (auto* motion : {&straight, &braking, &turning}) {
                ticks(*motion, 80, {.throttle = true});
            }
            const auto displacement = fixed::sub(straight.state().body.position, body.position);
            expect(fixed::dot(displacement, body.basis[0]) > unit, "Canonical track start must move forward through the recovered contact force chain");
            // This unsteered DONS qualifying probe reaches the authored HPC
            // edge on its 96th tick with the proper local-player grip values.
            const auto followup = track == 5 && start.source_index != 15 ? 12u : 16u;
            ticks(straight, followup, {});
            ticks(braking, followup, {.brake = true});
            ticks(turning, followup, {.throttle = true, .right = true});
            expect(braking.state().speed_q16 < straight.state().speed_q16, "Canonical track braking must reduce actual body speed relative to coasting");
            expect(turning.state().body.basis != straight.state().body.basis, "Canonical track steering must alter the integrated body basis");
            std::cout << prefix << ' ' << (start.source_index == 15 ? "race" : "qualifying") << " position " << straight.state().body.position[0] << ' '
                      << straight.state().body.position[1] << ' ' << straight.state().body.position[2] << " speed " << straight.state().speed_q16 << " brake "
                      << braking.state().speed_q16 << " yaw_rate " << turning.state().angular_velocity_q16[1] << '\n';
        }
        for (std::uint32_t car = 1; car < 8; ++car) {
            SurfaceRoughness roughness;
            Motion motion(surface, toca::scene::initial_body_pose(surface, race, car), car, true, car % 5 + 1, false, damage, roughness, &barriers);
            ticks(motion, 8, {.throttle = true});
        }
    }
}

} // namespace toca::test::motion
