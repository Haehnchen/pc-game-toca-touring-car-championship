#include "scene/engine_effects.hpp"
#include "simulation/linkup_fleet.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/rivals.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace toca;
using namespace toca::simulation;
constexpr std::int32_t unit = 65536;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void word(auto& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::uint8_t(std::uint32_t(value) >> (i * 8));
    }
}

HpcSurface floor() {
    std::vector<std::uint8_t> bytes(106);
    const auto half = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = std::uint8_t(value);
        bytes[offset + 1] = std::uint8_t(value >> 8);
    };
    word(bytes, 0, -128 * unit);
    word(bytes, 4, -128 * unit);
    half(8, 1);
    half(20, 2);
    half(24, 4);
    const std::array<std::array<std::int32_t, 3>, 4> vertices{{{-64 * unit, 0, -64 * unit}, {-64 * unit, 0, 64 * unit}, {64 * unit, 0, -64 * unit}, {64 * unit, 0, 64 * unit}}};
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            word(bytes, 28 + vertex * 12 + axis * 4, vertices[vertex][axis]);
        }
    }
    half(76, 2);
    for (std::size_t i = 0; i < 6; ++i) {
        half(80 + i / 3 * 8 + i % 3 * 2, std::array<std::uint16_t, 6>{0, 1, 2, 2, 1, 3}[i]);
    }
    half(96, 2);
    half(100, 1);
    half(102, 1);
    half(104, 1);
    return HpcSurface(bytes);
}

std::vector<std::uint8_t> route() {
    std::vector<std::uint8_t> bytes(4 + 3 * 64);
    word(bytes, 0, 3);
    for (std::size_t point = 0; point < 3; ++point) {
        const auto base = 4 + point * 64;
        word(bytes, base, -16 * unit);
        word(bytes, base + 8, std::int32_t(point) * 10 * unit);
        word(bytes, base + 12, 0);
        word(bytes, base + 20, std::int32_t(point) * 10 * unit);
        word(bytes, base + 24, 0);
        word(bytes, base + 32, std::int32_t(point) * 10 * unit);
        word(bytes, base + 36, 16 * unit);
        word(bytes, base + 44, std::int32_t(point) * 10 * unit);
        word(bytes, base + 52, 50);
        word(bytes, base + 56, 50);
    }
    return bytes;
}

void small_worklists(const HpcSurface& surface, std::span<const std::uint8_t> points, const std::array<scene::GridSlot, 32>& slots, const scene::QualifyingData& data) {
    for (const auto count : {1u, 2u}) {
        DamageContext damage(surface);
        SurfaceRoughness roughness;
        SourceRandom random(1);
        LinkupRoster roster;
        roster.humans = count;
        roster.local_actor = count - 1;
        roster.hosting = true;
        scene::PropResources posts, empty;
        for (std::uint32_t actor = 0; actor < count; ++actor) {
            roster.drivers.push_back({0, 2, false});
            roster.grid.push_back(std::uint8_t(actor));
            const auto body = scene::initial_body_pose(surface, scene::race_grid_start(slots, actor), 0);
            ModelNode node{-1, {}, 0, {}};
            node.transform[0] = node.transform[5] = node.transform[10] = node.transform[15] = 1;
            node.transform[12] = float(body.position[0] + unit) / unit;
            node.transform[13] = float(body.position[1] - 0x8666) / unit;
            node.transform[14] = float(body.position[2]) / unit;
            posts.actors.push_back({std::uint32_t(posts.model.nodes.size())});
            posts.model.nodes.push_back(node);
        }
        // An unused zero-initialized vehicle tail would also touch this post.
        ModelNode origin{-1, {}, 0, {}};
        origin.transform[0] = origin.transform[5] = origin.transform[10] = origin.transform[15] = 1;
        posts.actors.push_back({std::uint32_t(posts.model.nodes.size())});
        posts.model.nodes.push_back(origin);
        constexpr std::array<std::array<std::int32_t, 2>, 4> sectors{{{0, 0}, {64 * unit, 0}, {0, -64 * unit}, {64 * unit, -64 * unit}}};
        PropContacts props(empty, posts, empty, sectors, surface);
        const auto body = scene::initial_body_pose(surface, scene::race_grid_start(slots, roster.local_actor), 0);
        Motion local(surface, body, 0, false, 1, false, damage, roughness);
        local.prepare_ai(AiDriver(points, data.drivers[roster.local_actor].raw, data.distributions, 0, 0, random));
        LinkupFleet fleet(surface, slots, points, data, 1, random, roster, damage, roughness);
        const auto random_before = random.state();
        const std::array<std::uint32_t, 2> expected{count - 1, 0};
        for (auto tick = 0u; tick < 3; ++tick) {
            fleet.update({}, local, {}, {}, {}, true, &props);
            expect(std::ranges::equal(fleet.integration_order(), std::span(expected).first(count)),
                "Small human rosters must integrate exactly their descending active prefix every tick");
            expect(fleet.contacts().empty() && random.state() == random_before, "Unused actor slots must not create vehicle contacts or AI decisions");
            expect(props.frame().updates == tick + 1 && props.frame().post_states.back() == 0, "Small rosters advance props once without passing phantom vehicles at the origin");
            const auto positions = fleet.positions(local.state().body);
            expect(std::ranges::all_of(std::span(positions).subspan(count),
                       [](const auto& position) {
                           return position == std::array<std::int32_t, 3>{};
                       }),
                "Inactive roster slots must remain absent from position output");
            if (tick == 0) {
                expect(props.frame().contacts == count && props.frame().sounds.size() == count, "Each real small-roster body must reach the shared prop dispatcher exactly once");
                for (std::uint32_t actor = 0; actor < count; ++actor) {
                    expect(props.frame().sounds[actor].local_player == (actor == roster.local_actor), "Prop dispatch retains actor order and the nonzero local-player identity");
                }
            }
        }
    }
}

void scene_context_consumers(const HpcSurface& surface, std::span<const std::uint8_t> points, const scene::QualifyingData& initial_data) {
    std::array<scene::GridSlot, 32> slots{};
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto x = actor == 0 || actor == 14 ? 0 : (std::int32_t(actor) * 4 - 32) * unit;
        const auto z = actor == 0 || actor == 14 ? 0 : 20 * unit;
        slots[actor] = {{x, z - 4 * unit}, {x, z + 4 * unit}, 0, 0};
    }
    const auto fast = [](Motion& motion, std::int32_t velocity = 2 * unit) {
        auto state = motion.network_state();
        state.velocity = {velocity, 0, 0};
        motion.apply_network_state(state);
    };
    const auto rejected = [](auto& fleet, const scene::FleetSceneContext& context) {
        auto invalid = context;
        invalid.actors[0].physical.neighbors.fill(scene::no_visibility_sector);
        invalid.actors[15].model_detail = 3;
        bool threw = false;
        try { fleet.set_scene_context(invalid); }
        catch (const std::runtime_error&) { threw = true; }
        expect(threw, "An invalid scene packet must be rejected before replacing any contact registration");
    };
    for (const bool connected : {false, true}) {
        DamageContext damage(surface);
        SurfaceRoughness roughness;
        SourceRandom random(1);
        auto data = initial_data;
        data.local_actor = 14;
        RaceGrid grid;
        for (std::uint32_t actor = 0; actor < 16; ++actor) grid.rank_to_actor[actor] = actor;
        Motion local(surface, scene::initial_body_pose(surface, scene::race_grid_start(slots, 14), 0), 0, false, 1, false, damage, roughness);
        fast(local);
        Rivals rivals(surface, slots, points, data, 1, random, grid, damage, roughness);
        scene::FleetSceneContext context;
        context.visibility.selected.resize(80);
        context.visibility.ordered_sectors = {1};
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            context.actors[actor].physical.sector = std::uint16_t(50 + actor);
            context.actors[actor].sector_visible = false;
            context.actors[actor].contact_effect_mask = 1;
        }
        context.actors[14].physical.sector = 7;
        context.actors[0].physical.sector = 8;
        if (connected) context.actors[0].physical.neighbors[0] = 7;
        rivals.set_scene_context(context);
        rejected(rivals, context);
        context.actors[0].physical.neighbors.fill(scene::no_visibility_sector);
        rivals.update({}, local, {}, {}, false);
        const auto hit = std::ranges::any_of(rivals.contacts(), [](const auto& contact) {
            return (contact.first == 0 && contact.second == 14) || (contact.first == 14 && contact.second == 0);
        });
        expect(hit == connected, "Rivals must consume the copied physical-neighbor packet even for PVS-hidden cars");
        if (connected) expect(!rivals.contact_effects().audio.empty(), "The shared registration must reach the actual nonzero contact consumer");
    }
    slots[1] = slots[0];
    slots[1].first_xz[0] += 4 * unit;
    slots[1].second_xz[0] += 4 * unit;
    for (const bool effects : {false, true}) {
        DamageContext damage(surface);
        SurfaceRoughness roughness;
        SourceRandom random(1);
        LinkupRoster roster{{{0, 2, false}, {0, 2, false}}, {0, 1}, 2, 1, true};
        Motion local(surface, scene::initial_body_pose(surface, scene::race_grid_start(slots, 1), 0), 0, false, 1, false, damage, roughness);
        fast(local, -4 * unit);
        LinkupFleet fleet(surface, slots, points, initial_data, 1, random, roster, damage, roughness, nullptr, false);
        fast(fleet.motion(0, local), 4 * unit);
        scene::FleetSceneContext context;
        context.visibility.selected.resize(16);
        context.visibility.ordered_sectors = {1};
        context.actors[0].physical = {8, {7, scene::no_visibility_sector, scene::no_visibility_sector}};
        context.actors[1].physical.sector = 7;
        for (auto& actor : context.actors) {
            actor.sector_visible = !effects;
            actor.model_detail = effects ? 0 : 1;
            actor.render_mask = effects ? 0 : 1;
            actor.contact_effect_mask = effects ? 1 : 0;
        }
        if (!effects) context.visibility.selected[7] = context.visibility.selected[8] = 1;
        fleet.set_scene_context(context);
        rejected(fleet, context);
        context.actors[0].physical.neighbors.fill(scene::no_visibility_sector);
        context.actors[0].contact_effect_mask = context.actors[1].contact_effect_mask = 0;
        auto clock = SourceClock::from_stored_divisor(25);
        (void)clock.sample(0);
        fleet.update(clock.sample(1), local, {}, {}, {}, false);
        expect(!fleet.contacts().empty(), "Linkup must retain the complete copied registration despite later caller edits");
        expect(fleet.contact_effects().fragments.empty() != effects, "Contact fragments must use the separate manual effect mask, independently of PVS/render selection");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto surface = floor();
        const auto points = route();
        DamageContext damage(surface);
        SurfaceRoughness roughness;
        SourceRandom random(1);
        std::array<scene::GridSlot, 32> slots{};
        for (std::size_t rank = 0; rank < 8; ++rank) {
            slots[rank] = {{(std::int32_t(rank) * 6 - 21) * unit, -4 * unit}, {(std::int32_t(rank) * 6 - 21) * unit, 4 * unit}, 0, 0};
        }
        scene::QualifyingData data;
        for (auto& driver : data.drivers) {
            word(driver.raw, 28, 0xfd70);
            driver.raw[19] = 1;
            driver.raw[20] = 31;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--scene-context") {
            scene_context_consumers(surface, points, data);
            std::cout << "PASS: atomic scene packets drive Rivals/Linkup contacts and independent effect gates\n";
            return 0;
        }
        if (argc != 1) throw std::runtime_error("Usage: network-motion-tests [--scene-context]");
        small_worklists(surface, points, slots, data);
        LinkupRoster roster;
        roster.humans = 8;
        roster.local_actor = 3;
        roster.hosting = true;
        for (std::uint32_t actor = 0; actor < 8; ++actor) {
            roster.drivers.push_back({actor, 2, false});
            roster.grid.push_back(std::uint8_t(actor));
        }
        const auto local_body = scene::initial_body_pose(surface, scene::race_grid_start(slots, 3), 3);
        Motion local(surface, local_body, 3, false, 1, false, damage, roughness);
        local.prepare_ai(AiDriver(points, data.drivers[3].raw, data.distributions, 0, 0, random));
        LinkupFleet fleet(surface, slots, points, data, 1, random, roster, damage, roughness);
        expect(fleet.count() == 8 && fleet.local_actor() == 3 && fleet.owns(3) && !fleet.owns(0), "Host still owns only its human within an eight-human shared actor order");
        for (std::uint32_t actor = 0; actor < 8; ++actor) {
            expect(!fleet.motion(actor, local).ai_controlled() && fleet.body_mode(actor) == BodyMode::full, "Every active human must retain human controls and full physics");
        }
        const auto old = fleet.motion(5, local).state();
        auto state = fleet.motion(5, local).network_state();
        state.position[0] += 0x40000;
        state.position[1] -= 7;
        state.position[2] -= 0x40000;
        state.velocity = {unit, 2 * unit, -unit};
        state.angular_velocity = {1, -2, 3};
        state.visual_angles = {-32, 31};
        state.steering_angle = 4095;
        state.engine_speed_q16 = 0xabcde;
        state.throttle_q16 = 1234;
        state.controls.throttle_q16 = unit;
        fleet.apply_state(5, state, local);
        const auto& corrected = fleet.motion(5, local);
        expect(
            corrected.state().body.position == correct_remote_position(old.body.position, state.position) && corrected.state().previous_position_q16 == old.previous_position_q16,
            "43b7d0 must smooth all XYZ without replacing retained integration history");
        expect(corrected.state().velocity_q16 == state.velocity && corrected.state().angular_velocity_q16 == state.angular_velocity
                   && corrected.controls().engine_speed_q16 == 0xabcde && corrected.controls().throttle_q16 == 1234 && corrected.controls().steering_angle == 4095,
            "Original correction imports velocity, orientation and recovered control fields together");
        const auto before_invalid = corrected.network_state();
        auto invalid = state;
        invalid.engine_speed_q16 = 0x100000;
        bool rejected = false;
        try {
            fleet.apply_state(5, invalid, local);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected && corrected.network_state() == before_invalid, "Invalid correction must reject before any physical mutation");
        rejected = false;
        try {
            fleet.apply_state(3, state, local);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected && local.state().body.position == local_body.position, "A remote state cannot replace this process's authoritative local car");
        Motion ai_state_probe(surface, local_body, 2, false, 1, false, damage, roughness);
        ai_state_probe.set_actor_identity(0, false);
        ai_state_probe.apply_network_controls({0, 0, 0, unit, 0});
        ai_state_probe.apply_network_state(state);
        expect(ai_state_probe.controls().raw_brake_q16 == unit && ai_state_probe.controls().raw_throttle_q16 == 0 && ai_state_probe.controls().engine_speed_q16 == 0xabcde,
            "43b7d0 imports AI body and engine state while retaining the locally generated AI axes");
        state.position = old.body.position;
        state.velocity = {};
        state.angular_velocity = {};
        state.steering_angle = 0;
        state.engine_speed_q16 = 0;
        state.throttle_q16.reset();
        fleet.apply_state(5, state, local);
        fleet.apply_gear(5, 7, true, local);
        expect(fleet.motion(5, local).controls().gear == 7 && fleet.motion(5, local).controls().reverse_pedal, "43be20 gear event changes only committed gear/reverse fields");
        fleet.apply_gear(5, 1, false, local);
        const auto random_before = random.state();
        fleet.set_race_leader(5); // Deliberately differs from grid/spatial leader0.
        fleet.update({}, local, {.throttle = true}, {}, {}, false);
        expect(random.state() == random_before && std::ranges::equal(fleet.integration_order(), std::array<std::uint32_t, 8>{7, 6, 5, 4, 3, 2, 1, 0}),
            "Eight humans run descending full-body integration without any AI RNG decisions");
        expect(fleet.motion(5, local).controls().maximum_throttle_q16 == fleet.motion(5, local).controls().base_throttle_q16
                   && local.controls().maximum_throttle_q16 > local.controls().base_throttle_q16,
            "Catch-up must use the explicit source race leader and physically adjust the local trailing human's throttle");
        for (std::uint32_t actor = 0; actor < 8; ++actor) {
            const auto& control = fleet.motion(actor, local).controls();
            expect(control.tire_offsets_q16[4] == control.tire_offsets_q16[0] + 0xccc, "Every human physical consumer must retain catch-up's front-longitudinal tire exception");
        }
        expect(local.controls().raw_throttle_q16 == unit && fleet.motion(5, local).controls().raw_throttle_q16 == unit && fleet.motion(4, local).controls().raw_throttle_q16 == 0,
            "Local input stays on shared actor3; remote owner controls drive actor5 only");
        for (const auto& event : fleet.events()) {
            expect(event.actor == 3, "Only the local source owner publishes gear and visual-damage events");
        }
        std::array<std::uint8_t, 17> variants{};
        variants[0] = 3;
        fleet.apply_damage(5, variants, local);
        expect(fleet.motion(5, local).damage().levels[0] == 3, "43bb60 visual variants update the remote actor");
        fleet.set_active_human(5, false, local);
        expect(fleet.owns(5) && !fleet.human(5) && fleet.motion(5, local).ai_controlled(), "Host takes over a departed human through the original AI handoff");
        fleet.update({}, local, {}, {}, {}, false);
        expect(fleet.body_mode(5) == BodyMode::full, "Departed humans retain source player identity and full body selection after AI handoff");
        SourceRandom grid_random(1), replay_random(1);
        const auto grid = make_linkup_grid(3, 8, grid_random);
        (void)replay_random.next();
        std::array<bool, 3> seen{};
        for (std::uint8_t actor = 0; actor < 3; ++actor) {
            std::uint32_t rank;
            do {
                rank = replay_random.next() % 3;
            } while (seen[rank]);
            seen[rank] = true;
            expect(grid[rank] == actor, "463c52 grid uses source rejection sampling over human ranks");
        }
        expect(grid_random.state() == replay_random.state() && grid[3] == 3 && grid[7] == 7, "Source human grid retains exact CRT consumption and trailing AI order");
        auto ai_roster = roster;
        ai_roster.humans = 4;
        LinkupFleet ai_fleet(surface, slots, points, data, 1, random, ai_roster, damage, roughness);
        expect(ai_fleet.count() == 8 && ai_fleet.car_id(7) == 7 && ai_fleet.physics_car_id(7) == 0 && ai_fleet.motion(7, local).engine_effect_vehicle().car_type == 0
                   && ai_fleet.motion(7, local).ai_controlled(),
            "AI fills Linkup to eight using source actor-order visual models and deterministic fresh-process physical car0");
        std::array<std::uint32_t, 16> order{6, 2, 4, 0, 7, 3, 5, 1};
        ai_fleet.set_order(order);
        Motion mixed_local(surface, local_body, 3, false, 1, false, damage, roughness);
        mixed_local.prepare_ai(AiDriver(points, data.drivers[3].raw, data.distributions, 0, 0, random));
        for (auto tick = 0; tick < 3; ++tick) {
            ai_fleet.update({}, mixed_local, {}, {}, {}, true);
            expect(std::ranges::equal(ai_fleet.integration_order(), std::array<std::uint32_t, 8>{4, 5, 6, 7, 3, 2, 1, 0}),
                "Mixed ranked traffic integrates distant AI in actor order before the descending full-human prefix");
            for (std::uint32_t actor = 0; actor < 8; ++actor) {
                expect(ai_fleet.body_mode(actor) == (actor < 4 ? BodyMode::full : BodyMode::distant),
                    "Ranked traffic must preserve the exact active full/distant partition across repeated updates");
            }
            std::reverse(order.begin(), order.begin() + 8);
            ai_fleet.set_order(order);
        }
        std::cout << "Network source motion and eight-human fleet passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
