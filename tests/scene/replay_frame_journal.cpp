#include "assets/store.hpp"
#include "frame_replay_output.hpp"
#include "scene/frame_journal.hpp"
#include "scene/placement.hpp"
#include "scene/resources.hpp"
#include "simulation/source_scene_frame.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace toca;
using namespace toca::scene;

class Reader {
    std::ifstream input_;

public:
    explicit Reader(const char* path) : input_(path, std::ios::binary) {
        if (!input_) {
            throw std::runtime_error("Cannot open scene journal inputs");
        }
    }

    std::uint32_t word() {
        std::array<unsigned char, 4> bytes;
        if (!input_.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
            throw std::runtime_error("Truncated scene journal inputs");
        }
        return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 | std::uint32_t(bytes[3]) << 24;
    }

    std::uint64_t order() {
        const auto low = word();
        return low | std::uint64_t(word()) << 32;
    }

    std::int32_t integer() {
        return std::bit_cast<std::int32_t>(word());
    }

    float real() {
        const auto result = std::bit_cast<float>(word());
        if (!std::isfinite(result)) {
            throw std::runtime_error("Nonfinite scene journal input");
        }
        return result;
    }

    std::uint32_t magic(std::string_view expected = "TOCASCN1", std::uint32_t maximum_version = 1) {
        std::array<char, 8> value;
        if (!input_.read(value.data(), value.size()) || std::string_view(value.data(), value.size()) != expected) {
            throw std::runtime_error("Unsupported scene journal inputs");
        }
        const auto version = word();
        if (version < 1 || version > maximum_version) {
            throw std::runtime_error("Unsupported scene journal version");
        }
        return version;
    }

    void finish() {
        if (input_.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Trailing scene journal inputs");
        }
    }
};

Visibility canonical_visibility(const SceneResources& resources) {
    Visibility result(resources.ground);
    for (const auto* baked : {&resources.trees_baked, &resources.fence_baked, &resources.objects_baked, &resources.cones.baked, &resources.posts.baked, &resources.signs.baked,
             &resources.flags.baked}) {
        (void)result.register_static(*baked);
    }
    return result;
}

using toca::scene_test::Output;

void read_pose(Reader& input, BodyPose& body) {
    for (auto& position : body.position) {
        position = input.integer();
    }
    for (auto& row : body.basis) {
        for (auto& value : row) {
            value = input.integer();
        }
    }
}

void replay_qualifying_frames(Reader& input, Output& output, const char* data_root) {
    const auto track = input.word(), car = input.word(), weather = input.word(), detail = input.word(), local = input.word();
    const auto initial_state = input.word(), count = input.word();
    if (track >= 10 || car >= 8 || weather < 1 || weather > 5 || detail > 3 || local >= 16 || initial_state != 7 || !count || count > 1'000'000) {
        throw std::runtime_error("Qualifying scene component requires a bounded first-State7 entry");
    }
    std::array<BodyPose, 16> bodies;
    for (auto& body : bodies) {
        read_pose(input, body);
    }
    const AssetStore assets(data_root);
    const auto resources = load_scene_resources(assets, resolve_setup({.track_id = track, .car_id = car, .weather_choice = weather, .detail = detail}));
    simulation::SourceSceneFrame frames(resources, local, std::nullopt, {}, true);
    frames.initialize_qualifying(bodies, track, single_qualifying_start(resources.route_points).heading);
    bool open = false, comparing = false;
    std::uint32_t previous_id = 0, expected_pass = 0, compared = 0;
    simulation::TimeStep used;
    for (std::uint32_t record = 0; record < count; ++record) {
        const auto id = input.word();
        simulation::TimeStep step;
        step.delta_q16 = input.integer();
        step.reciprocal_q16 = input.integer();
        const auto extra = input.word(), state = input.word();
        const auto timer = input.integer();
        const auto pass = input.word(), compare = input.word();
        if (extra > 1 || pass > 1 || compare > 1 || (record && id != previous_id + 1) || (!record && (state != initial_state || pass != 0)) || (state != 7 && state != 8)
            || timer < 0 || (comparing && !compare)) {
            throw std::runtime_error("Qualifying scene records changed the declared source phase/pass/suffix");
        }
        previous_id = id;
        step.extra_step = extra != 0;
        if (pass == 0) {
            if (open) {
                frames.finish_observed_frame();
            }
            frames.begin_observed_frame(step);
            frames.set_lifecycle({state, timer});
            open = true;
            expected_pass = 0;
            used = step;
        } else if (!open || expected_pass != 1 || !used.extra_step || step.delta_q16 != used.delta_q16 || step.reciprocal_q16 != used.reciprocal_q16
                   || step.extra_step != used.extra_step) {
            throw std::runtime_error("Qualifying source extra pass changed its recorded primary timestep");
        }
        simulation::ScenePhysicalBoundary boundary;
        boundary.session_state = state;
        read_pose(input, boundary.before);
        read_pose(input, boundary.local_body);
        for (auto& value : boundary.previous_velocity) {
            value = input.integer();
        }
        for (auto& value : boundary.velocity) {
            value = input.integer();
        }
        boundary.angular_velocity_y = input.integer();
        for (std::size_t actor = 0; actor < 16; ++actor) {
            for (auto& value : boundary.positions[actor]) {
                value = input.integer();
            }
            const auto mode = input.word();
            if (mode > 4) {
                throw std::runtime_error("Unsupported qualifying physical body mode");
            }
            boundary.modes[actor] = std::uint8_t(mode);
        }
        if (boundary.local_body.position != boundary.positions[local]) {
            throw std::runtime_error("Qualifying local pose and physical body positions disagree");
        }
        if (compare) {
            const auto& camera = frames.published_state();
            if (!camera.authored) {
                throw std::runtime_error("Qualifying consumer suffix precedes its generated authored-camera publication");
            }
            scene_test::write_lifecycle_pre(output, id, frames);
            output.row(id, "pre_camera", 0, "authored_index", camera.authored_actor);
            output.row(id, "pre_camera", 0, "ordinary_view", frames.selected_view());
            comparing = true;
            ++compared;
        }
        frames.begin_physical(boundary.before, state);
        frames.camera_controls({});
        frames.complete_physical(boundary);
        if (pass == 0) {
            frames.publish_primary();
        }
        expected_pass = pass + 1;
    }
    if (open) {
        frames.finish_observed_frame();
    }
    if (!compared) {
        throw std::runtime_error("Qualifying component omitted its declared consumer suffix");
    }
    input.finish();
    std::cout << "Compared " << count << " supplied qualifying physical boundaries; " << compared << " authored-scene consumer checkpoints\n";
}

void replay_frames(Reader& input, Output& output, const char* data_root) {
    if (input.magic("TOCASFR1", 2) == 2) {
        replay_qualifying_frames(input, output, data_root);
        return;
    }
    const auto track = input.word(), car = input.word(), weather = input.word(), detail = input.word(), local = input.word();
    const auto divisor = input.integer();
    toca::simulation::TimeStep initial;
    initial.delta_q16 = input.integer();
    initial.reciprocal_q16 = input.integer();
    const auto extra = input.word(), size = input.word();
    if (track >= 10 || car >= 8 || weather < 1 || weather > 5 || detail > 3 || local >= 16 || extra > 1 || size != 17) {
        throw std::runtime_error("Unsupported generated scene frame setup");
    }
    initial.extra_step = extra != 0;
    std::vector<SceneJournalRoot> roots(size);
    for (std::uint32_t root = 0; root < size; ++root) {
        auto& value = roots[root];
        value.role = SceneJournalRootRole(input.word());
        value.actor = input.word();
        value.flags = input.word();
        for (auto& position : value.local_position) {
            position = input.real();
        }
    }
    const auto count = input.word();
    if (!count || count > 1'000'000) {
        throw std::runtime_error("Generated scene frame count exceeds the bounded profile");
    }
    const AssetStore assets(data_root);
    const auto resources = load_scene_resources(assets, resolve_setup({.track_id = track, .car_id = car, .weather_choice = weather, .detail = detail}));
    toca::simulation::SourceSceneFrame frames(canonical_visibility(resources), roots, single_race_start(resources.grid).heading, local, car, detail, divisor, initial);
    for (std::uint32_t frame = 0; frame < count; ++frame) {
        const auto physical = input.word(), next_counter = input.word();
        if (physical != frames.step().update_count()) {
            throw std::runtime_error("Scene frame body payload count disagrees with the generated source clock");
        }
        const auto used = frames.step();
        for (std::uint32_t pass = 0; pass < physical; ++pass) {
            toca::simulation::ScenePhysicalBoundary boundary;
            boundary.session_state = input.word();
            for (auto& position : boundary.before.position) {
                position = input.integer();
            }
            for (auto& axis : boundary.before.basis) {
                for (auto& component : axis) {
                    component = input.integer();
                }
            }
            for (std::uint32_t actor = 0; actor < 16; ++actor) {
                for (auto& position : boundary.positions[actor]) {
                    position = input.integer();
                }
                const auto mode = input.word();
                if (mode > 1) {
                    throw std::runtime_error("Generated scene frames exclude retirement and recovery bodies");
                }
                boundary.modes[actor] = std::uint8_t(mode);
            }
            frames.physical(boundary);
        }
        frames.finish(next_counter);
        scene_test::write_frame(output, frame, frames, used, frames.step());
    }
    input.finish();
    std::cout << "Generated " << count << " source scene frames\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool frames = argc == 5 && std::string_view(argv[1]) == "--frames";
        if (argc != 4 && !frames) {
            throw std::runtime_error("Usage: scene-journal-replay [--frames] DATA_ROOT INPUT_BIN OUTPUT_CSV");
        }
        const auto offset = frames ? 1 : 0;
        Reader input(argv[2 + offset]);
        if (frames) {
            Output output(argv[4]);
            replay_frames(input, output, argv[2]);
            return 0;
        }
        input.magic();
        const auto track = input.word(), car = input.word(), weather = input.word(), detail = input.word(), local = input.word(), count = input.word();
        if (track >= 10 || car >= 8 || weather < 1 || weather > 5 || detail > 3 || local >= 16 || count == 0 || count > 1'000'000) {
            throw std::runtime_error("Scene journal requires a bounded ordinary single-player scene");
        }
        const AssetStore assets(argv[1]);
        const auto resources = load_scene_resources(assets, resolve_setup({.track_id = track, .car_id = car, .weather_choice = weather, .detail = detail}));
        SceneFrameJournal journal(canonical_visibility(resources));
        Output output(argv[3]);
        std::vector<SceneJournalRoot> roots;
        std::vector<bool> migrated;
        std::array<std::uint32_t, 16> car_roots;
        car_roots.fill(no_visibility_entry);
        for (std::uint32_t event = 0; event < count; ++event) {
            const auto kind = input.word();
            const auto order = input.order();
            if (order != event) {
                throw std::runtime_error("Scene journal operation sequence is not contiguous");
            }
            switch (kind) {
                case 1: {
                    const auto size = input.word();
                    if (size != 17 || !roots.empty()) {
                        throw std::runtime_error("Scene journal requires sixteen car parents and one follow anchor");
                    }
                    roots.resize(size);
                    migrated.resize(size);
                    std::uint32_t follows = 0;
                    for (std::uint32_t root = 0; root < size; ++root) {
                        auto& value = roots[root];
                        value.role = SceneJournalRootRole(input.word());
                        value.actor = input.word();
                        value.flags = input.word();
                        for (auto& coordinate : value.local_position) {
                            coordinate = input.real();
                        }
                        if (value.role == SceneJournalRootRole::car_parent && value.actor < car_roots.size()) {
                            car_roots[value.actor] = root;
                        } else if (value.role == SceneJournalRootRole::follow_anchor) {
                            ++follows;
                        } else {
                            throw std::runtime_error("Unsupported root role in bounded scene journal");
                        }
                    }
                    if (follows != 1) {
                        throw std::runtime_error("Scene journal has an ambiguous follow role");
                    }
                    journal.initialize(order, roots);
                    for (std::uint32_t root = 0; root < size; ++root) {
                        output.registration(order, root, journal, false);
                    }
                    break;
                }
                case 2: {
                    const auto root = input.word();
                    std::array<float, 3> position;
                    for (auto& coordinate : position) {
                        coordinate = input.real();
                    }
                    journal.migrate(order, root, position);
                    migrated.at(root) = true;
                    output.registration(order, root, journal, true);
                    break;
                }
                case 3: {
                    VisibilityCamera camera;
                    for (auto& value : camera.world_to_view) {
                        value = input.real();
                    }
                    for (auto& value : camera.world_position) {
                        value = input.real();
                    }
                    std::array<std::int32_t, 16> projection;
                    for (auto& value : projection) {
                        value = input.integer();
                    }
                    const auto near_q16 = input.integer(), far_q16 = input.integer();
                    if (near_q16 != 0x4000 || far_q16 <= near_q16 || far_q16 > 1000 * 65536) {
                        throw std::runtime_error("Unsupported source scene frustum");
                    }
                    camera.projection = default_projection(detail, 640, 480);
                    camera.projection.near_plane = float(near_q16) / 65536.f;
                    camera.projection.far_plane = float(far_q16) / 65536.f;
                    camera.projection.x = float(projection[0]) / 65536.f;
                    camera.projection.y = float(projection[5]) / 65536.f;
                    camera.projection.depth_scale = float(projection[10]) / 65536.f;
                    camera.projection.depth_offset = float(projection[14]) / 65536.f;
                    camera.view_mask = 1;
                    const auto size = input.word();
                    if (size > roots.size()) {
                        throw std::runtime_error("Scene journal camera ancestry exceeds initialized roots");
                    }
                    std::vector<std::uint32_t> ancestors(size);
                    for (auto& root : ancestors) {
                        root = input.word();
                    }
                    const auto all_sectors = input.word();
                    if (all_sectors > 1) {
                        throw std::runtime_error("Invalid scene visibility override");
                    }
                    journal.select(order, camera, ancestors, all_sectors != 0);
                    output.pvs(order, journal.visibility());
                    break;
                }
                case 4: {
                    SceneJournalModels models;
                    models.local_actor = input.word();
                    models.graphics_detail = input.word();
                    models.view = input.word();
                    const auto branch = input.word();
                    if (branch > 1 || models.local_actor != local || models.graphics_detail != detail) {
                        throw std::runtime_error("Scene model call changed the bounded setup");
                    }
                    models.authored_camera = branch != 0;
                    models.view_factor_q16 = input.integer();
                    for (auto& position : models.body_positions) {
                        for (auto& coordinate : position) {
                            coordinate = input.integer();
                        }
                    }
                    for (auto& mode : models.body_modes) {
                        const auto value = input.word();
                        if (value > 4) {
                            throw std::runtime_error("Unsupported scene physical mode");
                        }
                        mode = std::uint8_t(value);
                    }
                    journal.models(order, models);
                    output.models(order, journal);
                    break;
                }
                case 5:
                    journal.publish(order);
                    for (std::uint32_t actor = 0; actor < 16; ++actor) {
                        const auto root = car_roots[actor];
                        output.registration(order, root, journal, migrated.at(root));
                        output.row(order, "root", root, "render_mask", journal.visibility().root_mask(root) & 1);
                        output.row(order, "root", root, "manual_mask", journal.root_manual_mask(root) & 1);
                    }
                    output.models(order, journal);
                    break;
                default:
                    throw std::runtime_error("Unknown scene journal operation");
            }
        }
        input.finish();
        std::cout << "Replayed " << count << " actual scene operations\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene journal: " << error.what() << '\n';
        return 1;
    }
}
