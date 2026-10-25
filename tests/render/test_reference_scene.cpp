#include "assets/store.hpp"
#include "frontend/car_name.hpp"
#include "scene/engine_effects.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/hud.hpp"
#include "scene/qualifying_view.hpp"
#include "scene/skid_marks.hpp"
#include "scene/surface_effects.hpp"
#include "scene/view.hpp"
#include "scene/weather.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class T> void read(std::istream& input, T& value) {
    expect(bool(input >> value), "Truncated original render-state input");
}

template <class T, std::size_t Size> void read(std::istream& input, std::array<T, Size>& values) {
    for (auto& value : values) {
        read(input, value);
    }
}

struct AnimatedNode {
    std::int32_t parent;
    std::uint32_t id, visible;
    toca::scene::HorizonTransform transform;
};

struct CapturedCarNode {
    std::int32_t parent = -1, variant = -1;
    std::uint32_t sibling = 0, id = 0, visible = 0;
    std::array<float, 16> local{}, world{};
};

struct CapturedCar {
    std::uint32_t slot = 0, role = 0, model = 0, detail = 0, root_mask = 0, lod_mask = 0, name_kind = 0;
    toca::scene::CarPose pose{};
    std::string name;
    std::vector<CapturedCarNode> nodes;
};

void read_shadow(std::istream& input, toca::scene::CarPose& pose) {
    toca::scene::CarShadowFootprint shadow;
    for (auto& point : shadow.points) {
        read(input, point);
    }
    std::uint32_t type;
    read(input, type);
    expect(type < 3, "Unknown original shadow type");
    shadow.type = toca::scene::CarShadowType(type);
    pose.shadow = shadow;
}

std::vector<CapturedCar> read_cars(std::istream& input, toca::scene::CameraInputs& camera) {
    std::uint32_t present;
    read(input, present);
    expect(present <= 1, "Captured actor presence must be boolean");
    if (!present) {
        return {};
    }
    std::uint32_t count, view, ghost_visible, viewport;
    std::int32_t ghost_owner;
    read(input, count);
    read(input, view);
    read(input, ghost_owner);
    read(input, ghost_visible);
    read(input, viewport);
    expect(count > 0 && count <= 16 && view <= 5 && ghost_owner >= -1 && ghost_owner <= 2 && ghost_visible <= 1 && viewport == 0,
        "Captured actors require bounded slots, a supported view and one original viewport");
    camera.view = std::uint16_t(view);
    std::vector<CapturedCar> cars(count);
    std::array<bool, 16> slots{};
    std::uint32_t locals = 0, visible_ghosts = 0, total_nodes = 0;
    const auto short_angle = [&](auto& destination) {
        std::int32_t angle;
        read(input, angle);
        expect(angle >= INT16_MIN && angle <= INT16_MAX, "Captured car angle must retain its signed16 width");
        destination = std::int16_t(angle);
    };
    for (auto& car : cars) {
        std::uint32_t driver, remap_livery, nodes, shadows;
        read(input, car.slot);
        read(input, car.role);
        read(input, car.model);
        read(input, car.detail);
        read(input, car.root_mask);
        read(input, car.lod_mask);
        read(input, driver);
        read(input, remap_livery);
        read(input, car.name_kind);
        read(input, nodes);
        read(input, shadows);
        expect(car.slot < 16 && !slots[car.slot] && car.role <= 3 && car.model <= 10 && car.detail <= 2 && car.root_mask <= 15 && car.lod_mask <= 15 && driver <= 1
                   && remap_livery <= 1 && car.name_kind <= 2 && nodes > 0 && nodes <= 256 && total_nodes + nodes <= 4096 && shadows <= 1,
            "Captured car has invalid identity, family, visibility or hierarchy bounds");
        slots[car.slot] = true;
        total_nodes += nodes;
        locals += car.role == 0;
        visible_ghosts += car.role >= 2 && (car.root_mask & car.lod_mask & 1) != 0;
        car.pose.second_driver = driver != 0;
        car.pose.remap_livery = remap_livery != 0;
        car.pose.actor_id = car.slot;
        read(input, car.pose.object_to_world);
        short_angle(car.pose.steering_angle);
        for (auto& wheel : car.pose.wheel_angles) {
            short_angle(wheel);
        }
        short_angle(car.pose.visual_angles.local_x);
        short_angle(car.pose.visual_angles.absolute_z);
        read(input, car.pose.lights.headlights);
        read(input, car.pose.lights.tail);
        read(input, car.pose.lights.brake);
        for (auto& level : car.pose.damage_levels) {
            std::uint32_t value;
            read(input, value);
            expect(value <= 255, "Captured damage level must retain its byte width");
            level = std::uint8_t(value);
        }
        for (auto& broken : car.pose.lights.broken) {
            read(input, broken);
        }
        expect(bool(input >> std::quoted(car.name)) && car.name.size() <= (car.name_kind == 1 ? 9u : 15u)
                   && std::ranges::all_of(car.name,
                       [](unsigned char c) {
                           return c >= 32 && c <= 126;
                       }),
            "Captured personal name requires bounded original printable bytes");
        car.nodes.resize(nodes);
        for (std::size_t ordinal = 0; auto& node : car.nodes) {
            read(input, node.parent);
            read(input, node.sibling);
            read(input, node.id);
            read(input, node.visible);
            read(input, node.variant);
            read(input, node.local);
            read(input, node.world);
            expect(node.parent >= -1 && node.parent < std::int32_t(ordinal) && node.sibling < nodes && node.id <= 0xffffff && node.visible <= 15 && node.variant >= -1
                       && node.variant <= 255
                       && std::ranges::all_of(
                           node.local,
                           [](float f) {
                               return std::isfinite(f);
                           })
                       && std::ranges::all_of(
                           node.world,
                           [](float f) {
                               return std::isfinite(f);
                           }),
                "Captured car node has invalid path, variant, visibility or matrix values");
            ++ordinal;
        }
        if (shadows) {
            read_shadow(input, car.pose);
        }
    }
    expect(locals == 1 && visible_ghosts <= 1, "Captured actor set requires one local player and at most one visible ghost");
    return cars;
}

void import_car_selection(CapturedCar& car, const toca::Model& model, std::uint32_t graphics_detail, std::ostream& geometry) {
    using namespace toca::scene;
    expect(car.nodes.size() == model.nodes.size(), "Captured car hierarchy does not cover its canonical family");
    const auto worlds = car_world_transforms(model, car.pose, car.detail == 0 ? 3u : 1u);
    CarRenderSelection selection{std::uint8_t(car.detail), std::uint8_t(car.root_mask & car.lod_mask)};
    selection.node_masks.resize(model.nodes.size());
    selection.node_variants.resize(model.nodes.size());
    std::vector<std::vector<std::size_t>> children(model.nodes.size() + 1);
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const auto parent = model.nodes[index].parent;
        expect(parent >= -1 && parent < std::int32_t(index), "Canonical car parent must precede its child");
        children[std::size_t(parent + 1)].push_back(index);
    }

    struct Visit {
        std::size_t node;
        std::int32_t parent;
        std::size_t sibling;
    };

    std::vector<Visit> pending;
    const auto push = [&](std::size_t parent_index, std::int32_t parent_ordinal) {
        const auto& siblings = children[parent_index];
        for (std::size_t sibling = siblings.size(); sibling-- > 0;) {
            pending.push_back({siblings[sibling], parent_ordinal, sibling});
        }
    };
    push(0, -1);
    std::size_t ordinal = 0;
    std::vector<bool> rendered;
    while (!pending.empty()) {
        const auto visit = pending.back();
        pending.pop_back();
        const auto& source = car.nodes.at(ordinal);
        const auto& canonical = model.nodes[visit.node];
        const bool matched = source.parent == visit.parent && source.sibling == visit.sibling && source.id == (canonical.id_flags & 0xffffff);
        expect(matched, "Captured car path/ID differs from its canonical loaded family");
        expect(source.variant < 0 || std::size_t(source.variant) < canonical.variants.size(), "Captured car variant is outside its canonical mesh choices");
        selection.node_masks[visit.node] = std::uint8_t(source.visible);
        selection.node_variants[visit.node] = std::int16_t(source.variant);
        const bool parent_visible = source.parent < 0 ? (car.root_mask & car.lod_mask & 1) != 0 : rendered.at(std::size_t(source.parent));
        rendered.push_back(parent_visible && (source.visible & 1) != 0);
        float maximum = 0;
        for (std::size_t element = 0; element < 16; ++element) {
            maximum = std::max(maximum, std::abs(worlds[visit.node][element] - source.world[element]));
        }
        geometry << car.slot << ',' << car.role << ',' << car.model << ',' << car.detail << ',' << ordinal << ',' << source.parent << ',' << source.sibling << ',' << source.id
                 << ',' << matched << ',' << maximum << ',' << source.visible << ',' << source.variant << ','
                 << unsigned(car_damage_variant(canonical.id_flags, car.pose.damage_levels)) << ',' << car.root_mask << ',' << car.lod_mask << ',' << rendered.back() << '\n';
        push(visit.node + 1, std::int32_t(ordinal));
        ++ordinal;
    }
    expect(ordinal == car.nodes.size(), "Captured car hierarchy has unreachable nodes");
    if (car.role == 0 && (car.root_mask & car.lod_mask & 1) != 0) {
        expect(car.detail == local_car_model_index(graphics_detail), "Captured player uses an unsupported local model family");
    }
    car.pose.render_selection = std::move(selection);
}

void import_animation(const toca::Model& model, const std::vector<AnimatedNode>& nodes, toca::scene::HorizonModelFrame& frame, std::uint32_t root_id) {
    expect(nodes.size() == model.nodes.size(), "Captured animation does not cover the canonical family");
    std::vector<std::vector<std::uint32_t>> children(model.nodes.size() + 1);
    for (std::size_t node = 0; node < model.nodes.size(); ++node) {
        const auto parent = model.nodes[node].parent;
        expect(parent >= -1 && (parent < 0 || std::size_t(parent) < node), "Canonical animation parent is invalid");
        children[std::size_t(parent + 1)].push_back(std::uint32_t(node));
    }
    frame.transforms.resize(model.nodes.size());
    frame.visible.resize(model.nodes.size());
    std::vector<std::pair<std::uint32_t, std::int32_t>> pending;
    for (auto root = children[0].rbegin(); root != children[0].rend(); ++root) {
        pending.emplace_back(*root, -1);
    }
    std::size_t ordinal = 0;
    while (!pending.empty()) {
        const auto [node, parent] = pending.back();
        pending.pop_back();
        const auto& source = nodes.at(ordinal);
        const auto id = model.nodes[node].id_flags & 0xffffffu;
        expect(source.parent == parent && (source.id == id || (parent == -1 && source.id == root_id && id != 998 && id != 999)),
            "Captured animation changed canonical hierarchy or identity");
        frame.transforms[node] = source.transform;
        frame.visible[node] = std::uint8_t(source.visible & 1u);
        for (auto child = children[node + 1].rbegin(); child != children[node + 1].rend(); ++child) {
            pending.emplace_back(*child, std::int32_t(ordinal));
        }
        ++ordinal;
    }
    expect(ordinal == nodes.size(), "Captured animation has unreachable nodes");
}

void render_frame(const std::filesystem::path& root, std::istream& input, const std::filesystem::path& output, toca::gpu::Renderer& renderer,
    toca::gpu::RasterProfile raster_profile, bool final_frame) {
    using namespace toca;
    using namespace toca::scene;
    std::string magic;
    read(input, magic);
    expect(magic == "TOCA_RENDER_12", "Unknown original render-state schema");
    std::uint32_t track, car_id, weather, detail, horizon, width, height;
    for (auto* value : {&track, &car_id, &weather, &detail, &horizon, &width, &height}) {
        read(input, *value);
    }
    expect(track < 10 && car_id < 10 && weather >= 1 && weather <= 5 && detail < 4 && width == 640 && height == 480, "Original scene selection is outside known source bounds");
    CameraInputs camera;
    read(input, camera.world_to_view);
    std::array<std::int32_t, 3> anchor_position;
    read(input, anchor_position);
    camera.registered_camera_position = anchor_position;
    camera.projection = default_projection(detail, width, height);
    std::array<std::int32_t, 16> projection;
    read(input, projection);
    for (std::size_t index = 0; index < projection.size(); ++index) {
        if (index != 0 && index != 5 && index != 10 && index != 14) {
            expect(projection[index] == (index == 11 ? 65536 : 0), "Original projection has unsupported off-axis or homogeneous coefficients");
        }
    }
    camera.projection.x = float(projection[0]) / 65536;
    camera.projection.y = float(projection[5]) / 65536;
    camera.projection.depth_scale = float(projection[10]) / 65536;
    camera.projection.depth_offset = float(projection[14]) / 65536;
    CarPose car;
    read(input, car.object_to_world);
    std::int32_t angle;
    read(input, angle);
    car.steering_angle = std::int16_t(angle);
    for (auto& wheel : car.wheel_angles) {
        read(input, angle);
        wheel = std::int16_t(angle);
    }
    read(input, angle);
    car.visual_angles.local_x = std::int16_t(angle);
    read(input, angle);
    car.visual_angles.absolute_z = std::int16_t(angle);
    std::uint32_t shadows;
    read(input, shadows);
    expect(shadows <= 1, "Render fixture needs one local original shadow; rival shadow state is outside its scope");
    if (shadows) {
        CarShadowFootprint shadow;
        for (auto& point : shadow.points) {
            read(input, point);
        }
        std::uint32_t type;
        read(input, type);
        expect(type < 3, "Unknown original shadow type");
        shadow.type = CarShadowType(type);
        car.shadow = shadow;
    }
    WeatherFrame precipitation;
    std::uint32_t particle_count;
    read(input, particle_count);
    expect(particle_count <= 90, "Original weather particle bound exceeded");
    for (std::uint32_t particle = 0; particle < particle_count; ++particle) {
        WeatherVector position, streak;
        std::uint32_t alpha;
        read(input, position);
        read(input, streak);
        read(input, alpha);
        expect(alpha < 256, "Original precipitation alpha is not a byte");
        if (weather == 4) {
            precipitation.sprites.push_back({position, {-0xccc, -0xccc, 0}, {0xccc, 0xccc, 0}, WeatherTexture::snow});
        } else {
            auto from = position;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                from[axis] = std::bit_cast<std::int32_t>(std::uint32_t(position[axis]) + std::uint32_t(streak[axis]));
            }
            precipitation.lines.push_back({from, position, (alpha << 24) | 0x969696u});
        }
    }
    SurfaceEffectFrame surface_effects;
    EngineEffectFrame engine_effects;
    constexpr std::array<SurfaceEffectTexture, 6> submission_order{
        SurfaceEffectTexture::spray, SurfaceEffectTexture::mud, SurfaceEffectTexture::gravel, SurfaceEffectTexture::grass, SurfaceEffectTexture::smoke, SurfaceEffectTexture::dust};
    for (std::size_t index = 0; index < submission_order.size(); ++index) {
        surface_effects.batches[index].texture = submission_order[index];
        surface_effects.batches[index].flags = submission_order[index] == SurfaceEffectTexture::spray ? 6 : 2;
    }
    std::uint32_t effect_batch_count, effect_sprite_count = 0, surface_sprite_count = 0;
    read(input, effect_batch_count);
    expect(effect_batch_count <= 7, "Original effect batch count exceeds its seven textures");
    std::array<bool, 7> captured_textures{};
    // Protocol IDs follow the source texture enum; submission follows446f20.
    constexpr std::array<std::size_t, 6> submission_slots{4, 5, 0, 1, 2, 3};
    constexpr std::array<std::size_t, 7> submission_ranks{5, 6, 0, 2, 3, 4, 1};
    std::size_t previous_rank = 0;
    for (std::uint32_t index = 0; index < effect_batch_count; ++index) {
        std::uint32_t texture, flags, sprite_count;
        read(input, texture);
        read(input, flags);
        read(input, sprite_count);
        expect(texture < captured_textures.size() && !captured_textures[texture], "Original effect texture is unknown or duplicated");
        expect(index == 0 || submission_ranks[texture] > previous_rank, "Original effect batches reorder source submissions");
        previous_rank = submission_ranks[texture];
        expect(flags == (texture == 6 || texture == std::uint32_t(SurfaceEffectTexture::spray) ? 6u : 2u), "Original effect batch has incorrect source flags");
        expect(sprite_count < 128 - effect_sprite_count, "Original effect packets exceed the strict sprite queue bound");
        captured_textures[texture] = true;
        auto& sprites = texture == 6 ? engine_effects.sprites : surface_effects.batches[submission_slots[texture]].sprites;
        sprites.resize(sprite_count);
        for (auto& sprite : sprites) {
            read(input, sprite.center);
            read(input, sprite.minimum);
            read(input, sprite.maximum);
            std::uint32_t shade0, shade1;
            read(input, shade0);
            read(input, shade1);
            expect(shade0 < 256 && shade1 < 256, "Original surface sprite shades must be bytes");
            sprite.shade0 = std::uint8_t(shade0);
            sprite.shade1 = std::uint8_t(shade1);
        }
        effect_sprite_count += sprite_count;
        if (texture != 6) {
            surface_sprite_count += sprite_count;
        }
    }
    engine_effects.visibility.assign(engine_effects.sprites.size(), 1);
    bool has_hud;
    read(input, has_hud);
    HudSnapshot hud_snapshot;
    if (has_hud) {
        read(input, hud_snapshot.speed_integer);
        read(input, hud_snapshot.rpm);
        read(input, hud_snapshot.gear);
        read(input, hud_snapshot.start_phase);
        read(input, hud_snapshot.kilometres);
        read(input, hud_snapshot.visible);
        auto& session = hud_snapshot.session;
        read(input, session.qualifying);
        read(input, session.lap);
        read(input, session.lap_limit);
        read(input, session.lap_time_ms);
        read(input, session.best_lap_ms);
        std::uint32_t mode;
        read(input, mode);
        expect(mode <= std::uint32_t(GameMode::linkup), "Unknown original HUD mode");
        session.mode = GameMode(mode);
        read(input, session.position);
        read(input, session.split_active);
        read(input, session.split_ticks);
        expect(bool(input >> std::quoted(session.split_text)), "Truncated original HUD split text");
        HudValues validation;
        validation.import_snapshot(hud_snapshot);
    }
    // Source node-world matrices from the completed original render. They are
    // observations for geometry diagnostics, never injected native mesh data.
    std::uint32_t node_count;
    read(input, node_count);
    expect(node_count > 0 && node_count <= 256, "Original car node count is invalid");
    std::vector<std::pair<std::uint32_t, std::array<float, 16>>> original_nodes(node_count);
    for (auto& [id, matrix] : original_nodes) {
        read(input, id);
        read(input, matrix);
    }
    std::array<float, 3> published_camera_position;
    read(input, published_camera_position);
    camera.published_world_position = published_camera_position;
    bool has_animation;
    read(input, has_animation);
    std::vector<std::int32_t> captured_flags;
    std::array<std::vector<AnimatedNode>, 5> animated_nodes;
    if (has_animation) {
        std::uint32_t count, total = 0;
        read(input, count);
        expect(count <= 40, "Captured flag controllers exceed the original bound");
        captured_flags.resize(count);
        for (auto& variant : captured_flags) {
            read(input, variant);
            expect(variant >= -1 && variant < 3, "Captured flag requires a null target or one of three variants");
        }
        for (auto& family : animated_nodes) {
            read(input, count);
            expect(count <= 4096 - total, "Captured animation exceeds the shared actor registry");
            total += count;
            family.resize(count);
            for (std::size_t ordinal = 0; auto& node : family) {
                read(input, node.parent);
                read(input, node.id);
                read(input, node.visible);
                read(input, node.transform);
                expect(node.parent >= -1 && node.parent < std::int32_t(ordinal) && node.id <= 0xffffffu && node.visible <= 255,
                    "Captured animation has invalid hierarchy, identity or visibility");
                ++ordinal;
            }
        }
    }
    std::uint32_t has_caption, caption_language = 0, caption_mode = 0, caption_visible = 0;
    read(input, has_caption);
    expect(has_caption <= 1, "Original caption presence must be a boolean");
    if (has_caption) {
        read(input, caption_language);
        read(input, caption_mode);
        read(input, caption_visible);
        expect(caption_language <= 4 && caption_mode <= std::uint32_t(GameMode::linkup) && caption_visible <= 1,
            "Original rollout caption requires a known language, mode and visibility");
    }
    auto captured_cars = read_cars(input, camera);
    std::uint32_t has_cockpit;
    read(input, has_cockpit);
    expect(has_cockpit <= 1, "Captured cockpit presence must be boolean");
    std::optional<CockpitSnapshot> captured_cockpit;
    if (has_cockpit) {
        auto& cockpit = captured_cockpit.emplace();
        read(input, cockpit.car_id);
        std::int32_t wheel, wiper;
        read(input, wheel);
        read(input, wiper);
        read(input, cockpit.duplicate_wiper);
        expect(cockpit.car_id == car_id && camera.view == 1 && wheel >= INT16_MIN && wheel <= INT16_MAX && wiper >= INT16_MIN && wiper <= INT16_MAX && !cockpit.duplicate_wiper,
            "Captured cockpit requires its actual view, car, signed angles and single-wiper template");
        cockpit.wheel_angle = std::int16_t(wheel);
        cockpit.wiper_angle = std::int16_t(wiper);
        for (auto& role : cockpit.roles) {
            std::uint32_t actual, manual;
            read(input, actual);
            read(input, manual);
            expect(actual <= 15 && manual <= 15, "Captured cockpit role masks must contain four viewport bits");
            role.actual_mask = std::uint8_t(actual);
            role.manual_mask = std::uint8_t(manual);
            read(input, role.local);
            read(input, role.parent_local);
        }
    }
    std::uint32_t has_skids;
    read(input, has_skids);
    expect(has_skids <= 1, "Captured skid presence must be boolean");
    SkidMarkFrame skid_marks;
    if (has_skids) {
        std::uint32_t persistent, active;
        read(input, persistent);
        read(input, active);
        expect(persistent <= 100 && active < 100, "Captured skid queues exceed their original physical capacity");
        skid_marks.persistent = persistent;
        skid_marks.active = active;
        skid_marks.quads.resize(persistent + active);
        for (auto& quad : skid_marks.quads) {
            std::uint32_t sector, mask;
            read(input, sector);
            read(input, mask);
            read(input, quad.color);
            expect(mask <= 15 && (sector < 2048 || (sector == 0xffff && mask == 0)), "Captured skid sector/mask is outside the supported source draw domain");
            quad.sector = std::uint16_t(sector);
            quad.visibility = std::uint8_t(mask);
            for (auto& vertex : quad.vertices) {
                read(input, vertex);
            }
        }
    }
    std::uint32_t has_sparks, spark_count = 0;
    read(input, has_sparks);
    expect(has_sparks <= 1, "Captured contact spark presence must be boolean");
    std::vector<ContactSparkLine> spark_lines;
    if (has_sparks) {
        read(input, spark_count);
        expect(spark_count <= 64, "Captured spark export exceeds its original pool capacity");
        for (std::uint32_t ordinal = 0; ordinal < spark_count; ++ordinal) {
            std::uint32_t mask;
            ContactSparkLine line;
            read(input, mask);
            read(input, line.color);
            // Wire preserves47ca40's endpoint/origin argument order;
            // ContactSparkLine stores origin/from and endpoint/to.
            read(input, line.to);
            read(input, line.from);
            expect(mask <= 15, "Captured spark mask exceeds its four source viewports");
            if ((mask & 1) != 0) {
                spark_lines.push_back(line);
            }
        }
    }
    std::uint32_t has_queued_lines;
    read(input, has_queued_lines);
    expect(has_queued_lines <= 1, "Captured queued world line presence must be boolean");
    if (has_queued_lines) {
        std::uint32_t count;
        read(input, count);
        expect(count <= 17, "Captured world lines exceed442480's queue capacity");
        auto& lines = camera.queued_grid_lines.emplace(count);
        for (auto& line : lines) {
            read(input, line.color);
            read(input, line.from);
            read(input, line.to);
        }
    }
    std::uint32_t history;
    read(input, history);
    expect(history <= 64 && (final_frame || history == 0), "Original presentation history must be bounded and nonnested");
    for (std::uint32_t ordinal = 0; ordinal < history; ++ordinal) {
        render_frame(root, input, output / ("history-" + std::to_string(ordinal)), renderer, raster_profile, false);
    }
    if (final_frame) {
        std::string extra;
        expect(!(input >> extra), "Unaccounted original render-state data");
    }
    const AssetStore store(root);
    const auto resources = load_scene_resources(store, resolve_setup({track, car_id, weather, detail}, horizon));
    expect(resources.setup.horizon_index == horizon, "Native horizon selection differs from captured original state");
    const auto weather_resources = load_weather_resources(store);
    const auto surface_resources = load_surface_effect_resources(store, resources.setup);
    const auto engine_resources = load_engine_effect_resources(store);
    std::array<std::optional<frontend::CarModels>, 11> car_models;
    std::vector<RivalModel> rival_models;
    std::vector<RivalPose> rival_poses;
    CapturedCar* captured_player = nullptr;
    CapturedCar* captured_ghost = nullptr;
    std::filesystem::create_directories(output);
    std::ofstream car_geometry(output / "actors.csv");
    car_geometry << std::setprecision(std::numeric_limits<float>::max_digits10);
    car_geometry << "actor,role,model,detail,ordinal,parent,sibling,node_id,matched,max_matrix_difference,visible,variant,damage_variant,root_mask,lod_mask,rendered\n";
    for (auto& captured : captured_cars) {
        auto& loaded = car_models.at(captured.model);
        if (!loaded) {
            loaded = frontend::load_car_models(store, captured.model);
        }
        const auto model = prepare_car_model(loaded->models[captured.detail], captured.model, detail, captured.detail);
        import_car_selection(captured, model, detail, car_geometry);
        if (captured.role == 0) {
            captured_player = &captured;
            expect(captured.model == car_id && captured.pose.object_to_world == car.object_to_world, "Captured local render actor differs from the base scene identity");
            car = captured.pose;
        } else if (captured.role == 1) {
            rival_models.push_back({captured.slot, &*loaded, captured.pose.second_driver});
            rival_poses.push_back({captured.slot, captured.pose});
        } else if ((captured.root_mask & captured.lod_mask & 1) != 0) {
            captured_ghost = &captured;
        }
    }
    car_geometry.close();
    expect(captured_cars.empty() || captured_player, "Captured actor set did not publish its local player");
    car.cockpit = std::move(captured_cockpit);
    SceneView view(renderer, resources, &weather_resources, &surface_resources, &engine_resources, rival_models);
    std::unique_ptr<frontend::CarNameFont> name_font;
    for (const auto& captured : captured_cars) {
        if (captured.name_kind == 0 || (captured.role >= 2 && &captured != captured_ghost)) {
            continue;
        }
        if (!name_font) {
            name_font = std::make_unique<frontend::CarNameFont>(store);
        }
        const auto bitmap = name_font->render(captured.name);
        if (captured.role == 0) {
            view.set_player_name(renderer, bitmap.get());
        } else if (captured.role == 1) {
            view.set_rival_name(renderer, captured.slot, bitmap.get());
        } else {
            view.set_ghost_name(renderer, bitmap.get());
        }
    }
    if (captured_ghost) {
        view.set_ghost_model(renderer, *car_models.at(captured_ghost->model));
    }
    std::unique_ptr<Hud> hud;
    if (has_hud) {
        hud = std::make_unique<Hud>(store, renderer, hud_snapshot.kilometres ? 1u : 0u);
        hud->import_snapshot(hud_snapshot);
    }
    std::unique_ptr<QualifyingView> captions;
    if (has_caption) {
        captions = std::make_unique<QualifyingView>(store, renderer, caption_language);
    }
    HorizonActors actors(resources.moving_horizon);
    auto actor_frame = actors.frame();
    PropFrame prop_frame;
    std::vector<std::uint8_t> flag_variants(resources.flags.model.nodes.size());
    if (has_animation) {
        expect(captured_flags.size() == resources.flags.actors.size(), "Captured flag controllers do not match canonical registration");
        for (std::size_t ordinal = 0; ordinal < captured_flags.size(); ++ordinal) {
            const auto target = resources.flags.actors[ordinal].animation_node;
            const auto variant = captured_flags[ordinal];
            expect((target == UINT32_MAX) == (variant == -1), "Captured flag animation target differs from canonical registration");
            if (target != UINT32_MAX) {
                flag_variants.at(target) = std::uint8_t(variant);
            }
        }
        import_animation(resources.moving_horizon.moving, animated_nodes[0], actor_frame.moving, 756);
        import_animation(resources.moving_horizon.rotorcraft, animated_nodes[1], actor_frame.rotorcraft, UINT32_MAX);
        import_animation(resources.cones.model, animated_nodes[2], prop_frame.cones, 996);
        import_animation(resources.posts.model, animated_nodes[3], prop_frame.posts, 995);
        import_animation(resources.signs.model, animated_nodes[4], prop_frame.signs, 995);
    }
    const auto model_index = local_car_model_index(detail);
    const auto model = prepare_car_model(resources.car.models[model_index], car_id, detail, model_index);
    const auto matrices = car_world_transforms(model, car, detail);
    std::filesystem::create_directories(output);
    std::ofstream geometry(output / "geometry.csv");
    geometry << std::setprecision(std::numeric_limits<float>::max_digits10);
    geometry << "node_id,matched,max_matrix_difference,original_x,original_y,original_z,native_x,native_y,native_z\n";
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const auto id = model.nodes[index].id_flags & 0xffffffu;
        const auto original = std::ranges::find_if(original_nodes, [id](const auto& node) {
            return node.first == id;
        });
        float difference = 0;
        if (original != original_nodes.end()) {
            for (std::size_t element = 0; element < matrices[index].size(); ++element) {
                difference = std::max(difference, std::abs(original->second[element] - matrices[index][element]));
            }
        }
        geometry << id << ',' << (original != original_nodes.end()) << ',' << difference;
        for (const auto axis : {12u, 13u, 14u}) {
            geometry << ',' << (original != original_nodes.end() ? original->second[axis] : 0);
        }
        for (const auto axis : {12u, 13u, 14u}) {
            geometry << ',' << matrices[index][axis];
        }
        geometry << '\n';
    }
    auto frame = view.frame(camera, scene_lighting(resources.setup), car, flag_variants, actor_frame, prop_frame, {}, &precipitation, {}, spark_lines, &surface_effects,
        has_skids ? &skid_marks : nullptr, captured_ghost ? &captured_ghost->pose : nullptr, false, &engine_effects, rival_poses);
    if (hud) {
        hud->append(frame, width, height);
    }
    if (captions && caption_visible) {
        captions->append_rollout_caption(frame, GameMode(caption_mode), width, height);
    }
    expect(renderer.draw(frame, false), "Matched original scene drawable unavailable");
    if (final_frame) {
        renderer.save_screenshot((output / "native.png").string());
    }
    const auto projected = test::materialize_world(frame);
    std::ofstream audit(output / "native-state.txt");
    audit << "track " << track << " car " << car_id << " weather " << weather << " detail " << detail << " horizon " << horizon << "\nvertices " << projected.vertices.size()
          << " indices " << projected.indices.size() << " draws " << projected.draws.size() << " shadows " << shadows << " precipitation " << particle_count << " effect_batches "
          << effect_batch_count << " surface_sprites " << surface_sprite_count << " engine_sprites " << engine_effects.sprites.size() << " hud " << has_hud << " animation "
          << has_animation << " caption " << has_caption << " caption_visible " << caption_visible << " actors " << captured_cars.size() << " rivals " << rival_poses.size()
          << " ghost " << bool(captured_ghost) << " skid_quads " << skid_marks.quads.size() << " spark_lines " << spark_lines.size() << " raster_profile "
          << (raster_profile == gpu::RasterProfile::native ? "native" : "reference_dgvoodoo263") << '\n';
    audit << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const auto value : camera.world_to_view) {
        audit << value << ' ';
    }
    audit << '\n';
    audit.close();
    if (!final_frame) {
        return;
    }
    std::cout << "TOCA_MATCHED_FRAME_READY " << output.string() << std::endl;
    // The Python runner captures only this SDL game window on its owned Xvfb.
    // Presentation remains fixed; waiting does not simulate or consume RNG.
    const auto deadline = SDL_GetTicks() + 10000;
    while (!std::filesystem::exists(output / "capture.done") && SDL_GetTicks() < deadline) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
}

void run(const std::filesystem::path& root, const std::filesystem::path& state_file, const std::filesystem::path& output, toca::gpu::RasterProfile raster_profile) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize reference render fixture");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA matched original scene", 640, 480, 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private matched-scene window");
    gpu::Renderer renderer(window.get(), raster_profile);
    std::ifstream input(state_file);
    render_frame(root, input, output, renderer, raster_profile, true);
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--reference-raster"),
            "Usage: toca-reference-scene CANONICAL_ROOT FRAME_TXT OUTPUT_DIRECTORY [--reference-raster]");
        run(argv[1], argv[2], argv[3], argc == 5 ? toca::gpu::RasterProfile::reference_dgvoodoo263 : toca::gpu::RasterProfile::native);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
