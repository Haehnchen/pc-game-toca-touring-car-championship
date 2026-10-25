#include "assets/store.hpp"
#include "scene/body.hpp"
#include "scene/pit_route.hpp"
#include "simulation/fixed.hpp"
#include "simulation/time_trial.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

toca::simulation::GhostPose pose(std::int32_t x) {
    return {{x, -65536, 17}, {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}}};
}

template <typename Callable> void expect_error(Callable&& callable, const char* message) {
    bool failed = false;
    try {
        callable();
    } catch (const std::exception&) {
        failed = true;
    }
    expect(failed, message);
}

void test(const std::filesystem::path& root) {
    using namespace toca::simulation;
    constexpr std::array release{SessionEvent::released}, complete{SessionEvent::lap_complete}, restart{SessionEvent::attempt_restarted};
    TimeTrial trial(0, 3, 5, "DRIVER");
    expect(!trial.load(root) && !trial.save(root) && !trial.playback(), "No saved record must leave Time Trial usable without a ghost");
    trial.update(500, pose(-100), {}, 0);
    expect(trial.recording_samples() == 0 && trial.best_lap_ms() == 0, "The automatic approach must never become a saved lap");
    trial.update(1000, pose(0), release, 0);
    trial.update(1500, pose(65536), {}, 0);
    trial.update(2000, pose(131072), complete, 1000);
    expect(trial.best_lap_ms() == 1000 && trial.sample_count() == 3 && trial.dirty() && trial.recording_samples() == 1,
        "A completed first lap must promote its recording and begin the next attempt at zero");
    expect(trial.best_car_id() == 3 && trial.best_weather() == 5 && trial.best_driver() == "DRIVER", "Promoted ghost must retain its actual car, weather and driver");
    expect(trial.playback() && trial.playback()->position_q16[0] == 0, "The newly completed best must replay from the next start-line crossing");
    trial.update(2250, pose(7), {}, 0);
    const auto halfway = trial.playback();
    expect(halfway && halfway->position_q16 == std::array<std::int32_t, 3>{32768, -65536, 17} && halfway->basis_q16 == pose(0).basis_q16,
        "Ghost playback must interpolate recorded translation and restore the third fixed-point basis row");
    trial.update(2250, pose(8), {}, 0);
    expect(
        trial.recording_samples() == 2 && trial.playback()->position_q16 == halfway->position_q16, "Repeated paused active time must neither append samples nor advance playback");
    expect(trial.save(root) && !trial.dirty() && !trial.save(root), "Only a newly improved completed best must write a durable save");

    const auto unsupported_root = root / "unsupported";
    const auto unsupported_file = unsupported_root / "time-trial" / "track-0.ghost";
    std::filesystem::create_directories(unsupported_file.parent_path());
    std::filesystem::copy_file(root / "time-trial" / "track-0.ghost", unsupported_file);
    std::fstream unsupported(unsupported_file, std::ios::binary | std::ios::in | std::ios::out);
    unsupported.seekp(4);
    unsupported.put('\xff');
    unsupported.close();
    TimeTrial fresh(0, 1, 2, "FRESH");
    expect(!fresh.load(unsupported_root) && fresh.best_lap_ms() == 0 && !fresh.playback() && !fresh.dirty(),
        "An unsupported format must behave like an absent ghost without importing timing or samples");
    TimeTrial ignored(0, 1, 2, "NO LOAD");
    expect(!ignored.load(unsupported_root, false), "An unsupported format must also be absent when loading the saved ghost is disabled");
    fresh.update(0, pose(0), release, 0);
    fresh.update(1200, pose(10), complete, 1200);
    expect(fresh.dirty() && fresh.save(unsupported_root), "A completed lap must save normally without using an unsupported file as a comparison baseline");
    TimeTrial replaced(0, 0, 0, "NEXT");
    expect(replaced.load(unsupported_root) && replaced.best_lap_ms() == 1200 && replaced.best_car_id() == 1,
        "An ordinary saved lap must replace the unsupported file with the current format");

    TimeTrial without_saved_ghost(0, 1, 2, "FRESH");
    expect(without_saved_ghost.load(root, false) && without_saved_ghost.best_lap_ms() == 0 && !without_saved_ghost.playback(),
        "LOAD BEST LAP=NO must read the persistent baseline while starting without the previous ghost");
    without_saved_ghost.update(0, pose(0), release, 0);
    without_saved_ghost.update(1200, pose(10), complete, 1200);
    expect(without_saved_ghost.best_lap_ms() == 1200 && without_saved_ghost.playback() && !without_saved_ghost.dirty() && !without_saved_ghost.save(root),
        "LOAD BEST LAP=NO must replay a new session best without overwriting a faster persistent record");

    TimeTrial loaded(0, 9, 1, "OTHER");
    expect(loaded.load(root) && loaded.best_lap_ms() == 1000 && loaded.best_car_id() == 3 && loaded.best_weather() == 5 && loaded.best_driver() == "DRIVER",
        "A per-track record must reload independently of the selected player car/weather");
    loaded.update(10, pose(0), release, 0);
    loaded.update(510, pose(9), {}, 0);
    expect(loaded.playback() && loaded.playback()->position_q16[0] == 65536, "Reloaded ghost must replay its saved positions");
    loaded.update(1010, pose(999), complete, 1000);
    expect(!loaded.dirty() && loaded.best_car_id() == 3 && !loaded.save(root), "An equal lap must preserve the previous best actor and save");
    loaded.update(2110, pose(999), complete, 1100);
    expect(!loaded.dirty() && loaded.best_car_id() == 3, "A slower lap must preserve the previous best");
    loaded.update(2510, pose(777), complete, 400);
    expect(loaded.dirty() && loaded.best_lap_ms() == 400 && loaded.best_car_id() == 9 && loaded.best_weather() == 1,
        "A faster full lap must replace loaded timing, pose recording and actor metadata together");
    expect(loaded.save(root), "An improved loaded record must atomically replace the saved best");
    TimeTrial reloaded(0, 0, 0, "NEXT");
    expect(reloaded.load(root) && reloaded.best_lap_ms() == 400 && reloaded.best_car_id() == 9, "Replacement must survive a fresh controller reload");
    TimeTrial other_track(1, 3, 5, "DRIVER");
    expect(!other_track.load(root), "Track namespaces must remain separate");

    // Advance synthetic active time without sleeping. Cover the longest valid
    // lap and a finish between40ms ticks, far beyond the former3600-pose limit.
    TimeTrial full_lap(2, 0, 1, "FULL LAP");
    constexpr auto lap_end = TimeTrial::maximum_lap_milliseconds;
    full_lap.update(0, pose(0), release, 0);
    for (std::uint64_t time = TimeTrial::sample_interval_ms; time < lap_end; time += TimeTrial::sample_interval_ms) {
        full_lap.update(time, pose(std::int32_t(time)), {}, 0);
    }
    full_lap.update(lap_end, pose(lap_end), complete, lap_end);
    expect(full_lap.sample_count() == TimeTrial::maximum_samples && full_lap.sample_count() > 3600 && full_lap.best_lap_ms() == lap_end && full_lap.playback(),
        "Every pose through the exact longest valid lap finish must survive promotion");

    const auto blocked = root / "blocked";
    std::ofstream(blocked) << "file";
    expect_error(
        [&] {
            full_lap.save(blocked);
        },
        "Save failure must be reported");
    expect(full_lap.dirty() && full_lap.best_lap_ms() == lap_end, "Save failure must preserve the unsaved best for a later retry");
    expect(full_lap.save(root), "Retry must persist the same completed best");
    TimeTrial whole_reload(2, 1, 3, "NEXT");
    expect(whole_reload.load(root) && whole_reload.sample_count() == TimeTrial::maximum_samples && whole_reload.best_lap_ms() == lap_end,
        "A long complete ghost must reload through the same bounded native schema");
    whole_reload.update(0, pose(0), release, 0);
    whole_reload.update(180000, pose(0), {}, 0);
    expect(whole_reload.playback() && whole_reload.playback()->position_q16[0] == 180000, "Playback must continue past the old144-second prefix");
    whole_reload.update(lap_end - 1, pose(0), {}, 0);
    expect(whole_reload.playback() && whole_reload.playback()->position_q16[0] >= std::int32_t(lap_end) - 2, "Playback must interpolate through the final off-grid interval");
    whole_reload.update(lap_end, pose(0), {}, 0);
    expect(whole_reload.playback() && whole_reload.playback()->position_q16[0] == std::int32_t(lap_end), "The exact finish pose must remain visible");
    whole_reload.update(lap_end + 1, pose(0), {}, 0);
    expect(!whole_reload.playback(), "Ghost must end only after its complete recorded lap, without extrapolation");

    TimeTrial dense(3, 0, 1, "DENSE");
    dense.update(0, pose(0), release, 0);
    for (std::uint32_t time = 1; time <= 40; ++time) {
        dense.update(time, pose(time), {}, 0);
    }
    expect(dense.recording_samples() == 2, "Extra host updates must not fill a file bound derived from40ms sampling");
    dense.update(41, pose(41), complete, 41);
    expect(dense.sample_count() == 3, "An off-grid finish must be retained even less than40ms after the previous pose");
    TimeTrial overlong(4, 0, 1, "OVERLONG");
    overlong.update(0, pose(0), release, 0);
    overlong.update(lap_end + 1, pose(0), {}, 0);
    expect(overlong.recording_samples() == 1, "A duration outside the valid best-lap range must not keep growing the recording");
    overlong.update(lap_end + 2, pose(0), complete, lap_end + 2);
    expect(overlong.best_lap_ms() == 0 && !overlong.dirty(), "A lap beyond the no-record sentinel cannot become a saved best");

    loaded.update(2610, pose(123), restart, 0);
    expect(loaded.recording_samples() == 1 && loaded.best_lap_ms() == 400 && !loaded.dirty() && loaded.playback()->position_q16[0] == 999,
        "A start-line attempt restart must reset recording/playback without promoting an incomplete lap");

    const auto ghost_file = root / "time-trial" / "track-0.ghost";
    std::filesystem::resize_file(ghost_file, std::filesystem::file_size(ghost_file) - 1);
    TimeTrial damaged(0, 0, 1, "CHECK");
    expect_error(
        [&] {
            damaged.load(root);
        },
        "A truncated supported recording must fail its exact-size check");
    expect(damaged.best_lap_ms() == 0 && !damaged.dirty(), "A failed load must not partially import an invalid ghost");
}

void fixture(const std::filesystem::path& data_root, const std::filesystem::path& state_root) {
    using namespace toca;
    using namespace toca::simulation;
    const AssetStore store(data_root);
    const auto keep = store.open_archive("data/donlkeep.bfp");
    const auto bytes = keep.member(1).bytes();
    const auto count = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8);
    const auto points = scene::decode_pit_route(bytes.first(4 + std::size_t(count) * 64));
    const HpcSurface surface(keep.member(0).bytes());
    Session session(bytes, false, GameMode::time_trial);
    TimeTrial trial(0, 1, 1, "AUDI GHOST");
    std::vector<GhostPose> poses;
    for (const auto& point : points.points) {
        const auto normal = fixed::normalized({fixed::sub(point.left[2], point.right[2]), 0, fixed::sub(point.right[0], point.left[0])});
        scene::HorizontalPlacement placement;
        for (std::size_t axis = 0; axis < 2; ++axis) {
            const auto component = axis * 2;
            placement.xz[axis] = std::int32_t((std::int64_t(point.left[component]) + point.right[component]) / 2) + normal[component];
        }
        // The fixture follows authored gates, with HPC support and source body
        // assembly. It is explicitly a synthetic traversal, not a physics lap.
        placement.heading = std::int16_t(std::atan2(double(normal[0]), double(normal[2])) * (4096.0 / 6.283185307179586));
        const auto body = scene::initial_body_pose(surface, placement, 1);
        poses.push_back({body.position, body.basis});
    }
    std::uint64_t time = 0;
    std::uint32_t releases = 0, completions = 0;
    const auto steps = poses.size() * 3;
    for (std::size_t step = 0; step < steps && completions == 0; ++step) {
        const auto point = session.next_point();
        const auto events = session.update(time += 400, poses[point].position_q16);
        releases += std::ranges::count(events, SessionEvent::released);
        completions += std::ranges::count(events, SessionEvent::lap_complete);
        trial.update(time, poses[point], events, session.last_lap_ms());
    }
    expect(releases == 1 && completions == 1 && session.completed_laps() == 1 && session.state() == 6 && trial.best_lap_ms() == session.last_lap_ms()
               && trial.sample_count() == poses.size() + 1,
        "Canonical ordered POINTS traversal must discard the approach and promote exactly one full measured recording");
    expect(trial.save(state_root), "Canonical complete-lap fixture must persist its Audi ghost");
    TimeTrial reloaded(0, 0, 1, "HONDA PLAYER");
    expect(reloaded.load(state_root) && reloaded.best_car_id() == 1 && reloaded.best_lap_ms() == trial.best_lap_ms() && reloaded.sample_count() == trial.sample_count(),
        "Canonical fixture must reload its complete Audi recording independently of a Honda player");
    constexpr std::array release{SessionEvent::released};
    reloaded.update(0, poses[0], release, 0);
    expect(reloaded.playback() && reloaded.playback()->position_q16 == poses[0].position_q16, "Canonical reloaded ghost must start at its recorded HPC-aligned start-line pose");
    std::cout << "Canonical synthetic gate traversal: " << poses.size() << " ordered POINTS, " << trial.sample_count() << " ghost samples, best " << trial.best_lap_ms()
              << "ms, Audi1 ghost for Honda0 player\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--fixture") {
            fixture(argv[2], argv[3]);
        } else if (argc == 2) {
            const auto root = std::filesystem::path(argv[1]) / ("run-" + std::to_string(std::random_device{}()));
            std::filesystem::create_directories(root);
            test(root);
        } else {
            throw std::runtime_error("Usage: toca-time-trial-tests PRIVATE-STATE-ROOT | --fixture CANONICAL-ROOT PRIVATE-STATE-ROOT");
        }
        std::cout << "PASS: Time Trial recording, best promotion, interpolated ghost playback and durable per-track records\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
