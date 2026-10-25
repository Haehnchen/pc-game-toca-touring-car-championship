#include "assets/store.hpp"
#include "scene/finish_camera.hpp"
#include "scene/qualifying.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::array<float, 3> transform(const std::array<float, 16>& matrix, const std::array<std::int32_t, 3>& point) {
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = matrix[12 + axis];
        for (std::size_t component = 0; component < 3; ++component) {
            result[axis] += float(point[component]) / 65536 * matrix[component * 4 + axis];
        }
    }
    return result;
}

void camera() {
    using namespace toca::scene;
    std::array<SceneCamera, 2> cameras{};
    cameras[0].transform[13] = cameras[1].transform[13] = 10 * 65536;
    cameras[1].transform[12] = 100 * 65536;
    FinishCamera view(cameras);
    const std::array<std::int32_t, 3> target{2 * 65536, 65536, 8 * 65536};
    view.update(target);
    expect(view.selected() == 0 && view.position() == std::array<std::int32_t, 3>{0, 10 * 65536, 0}, "Finish camera must choose the closest authored horizontal actor");
    const auto matrix = view.world_to_view(target);
    const auto origin = transform(matrix, view.position()), center = transform(matrix, target);
    expect(
        std::abs(origin[0]) < .001f && std::abs(origin[1]) < .001f && std::abs(origin[2]) < .001f && std::abs(center[0]) < .001f && std::abs(center[1]) < .001f && center[2] > 10,
        "Finish look-at affine inverse must put the authored camera at origin and actual body ahead on center");
    for (int tick = 0; tick < 30; ++tick) {
        view.update({100 * 65536, 65536, 8 * 65536});
    }
    expect(view.selected() == 0, "Source camera selection must retain its thirty-call hold");
    view.update({100 * 65536, 65536, 8 * 65536});
    expect(view.selected() == 1, "Source camera selection must refresh after its hold expires");
    for (int tick = 0; tick < 91; ++tick) {
        view.update(target, true);
    }
    expect(view.selected() == 1, "Results first reconcile the retained previous-results actor, then hold sixty calls plus the thirty-call cache");
    view.update(target, true);
    expect(view.selected() == 0, "Race results must refresh the authored camera only after both source holds expire");
    FinishCamera far_view{std::span(cameras).first(1)};
    const std::array<std::int32_t, 3> far_target{1000 * 65536, 0, 0};
    const auto far_center = transform(far_view.world_to_view(far_target), far_target);
    expect(std::abs(far_center[0]) < .1f && std::abs(far_center[1]) < .1f && far_center[2] > 990 && far_center[2] < 1010,
        "Original large-vector normalization must retain a distant authored camera's look-at depth without fixed-square wrap");
}

void synthetic() {
    using namespace toca::scene;
    QualifyingData data;
    data.local_actor = 6;
    data.minimum_centiseconds = data.maximum_centiseconds = 1000;
    data.variation_q16[1] = 66; // Source bound floor(66*1000/65536)=1.
    for (std::uint32_t actor = 0; actor < data.drivers.size(); ++actor) {
        auto& driver = data.drivers[actor];
        driver.name = "DRIVER" + std::to_string(actor);
        driver.car_id = actor / 2;
        driver.ability = 1;
    }
    toca::SourceRandom random(1), expected_random(1);
    auto result = classify_qualifying(data, 10019, random);
    expect(result.local_rank == 6 && result.rows[6].time_ms == 10010, "Centisecond-equal qualifying records must retain original actor order");
    for (std::uint32_t index = 0; index < result.rows.size(); ++index) {
        expect(result.rows[index].actor_id == index && result.rows[index].time_ms == 10010, "Classification must generate original bounded times and stable equal-time ordering");
        if (index != data.local_actor) {
            (void)expected_random.next();
        }
    }
    expect(random.next() == expected_random.next(), "Classification must consume one shared source random value per AI actor, never for the measured player");
    toca::SourceRandom fast_random(1), slow_random(1);
    expect(classify_qualifying(data, 9990, fast_random).local_rank == 0 && classify_qualifying(data, 11000, slow_random).local_rank == 15,
        "Actual local best must determine pole or last grid rank against generated original opponents");
    toca::SourceRandom skipped_random(1);
    const auto skipped = classify_qualifying(data, 3'600'000, skipped_random);
    expect(
        skipped.local_rank == 15 && skipped.rows.back().time_ms == 3'600'000, "Original unmeasured qualifying skip sentinel must place the player last without fabricating a lap");
    bool rejected = false;
    try {
        (void)classify_qualifying(data, 0, random);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "Missing measured local time must not generate a fake classified result");
    camera();
}

void reference(const char* root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    const auto data = load_qualifying_data(store, resolve_setup({0, 0, 0, 3}), 1, "PLAYER 1");
    expect(data.local_actor == 0 && data.drivers[0].name == "PLAYER 1" && data.drivers[1].name == "G.Tarquini" && data.drivers[2].name == "F.Biela"
               && data.drivers[15].name == "J.Plato",
        "Original AIBIN difficulty bank or local driver substitution changed");
    const auto renamed = load_qualifying_data(store, resolve_setup({0, 0, 0, 3}), 1, "HUMAN");
    toca::SourceRandom names_random(27), renamed_random(27);
    const auto names_result = classify_qualifying(data, 90000, names_random), renamed_result = classify_qualifying(renamed, 90000, renamed_random);
    for (std::size_t rank = 0; rank < names_result.rows.size(); ++rank) {
        const auto& before = names_result.rows[rank];
        const auto& after = renamed_result.rows[rank];
        expect(before.actor_id == after.actor_id && before.car_id == after.car_id && before.time_ms == after.time_ms
                   && (after.actor_id == data.local_actor ? after.driver == "HUMAN" : after.driver == before.driver),
            "420610 and later setup name copies identify human actors; CODIES must never replace unrelated AI opponent names or tuning");
    }
    expect(data.minimum_centiseconds == 8700 && data.maximum_centiseconds == 10500 && data.drivers[0].ability == 8 && data.ability_fraction_q16[8] == 3276
               && data.variation_q16[8] == 1114,
        "Original Donington qualifying time bounds or ability parameters changed");
    expect(data.distributions[0] == std::vector<std::int16_t>{1, 5, 15} && data.distributions[1].size() == 7 && data.distributions[2].size() == 26
               && data.distributions[2].back() == -200,
        "Original signed AI speed/target distributions changed");
    const auto silverstone = load_qualifying_data(store, resolve_setup({1, 1, 5, 3}), 1, "DRIVER");
    expect(silverstone.local_actor == 2 && silverstone.minimum_centiseconds != data.minimum_centiseconds && silverstone.weather_speed_penalty_q16 == 524,
        "Silverstone track/weather selection must consume its own QUALPC and AIBIN records");
    const auto thruxton = load_qualifying_data(store, resolve_setup({2, 0, 0, 3}), 1, "PLAYER 1");
    expect(thruxton.minimum_centiseconds == 6800 && thruxton.maximum_centiseconds == 8200,
        "Generic normal-track QUALPC selection must preserve Thruxton's original medium/default-weather time bounds");
    expect(load_qualifying_data(store, resolve_setup({}), 1, "PLAYER", true).tire_offset_q16 == 0
               && load_qualifying_data(store, resolve_setup({}), 1, "PLAYER", false, true).tire_offset_q16 == 0x3333
               && load_qualifying_data(store, resolve_setup({}), 1, "PLAYER", true, true).tire_offset_q16 == 0,
        "Original Mayhem/Impossible tire tuning must override AIBIN weather with Mayhem precedence");
    for (const auto car : {8u, 9u}) {
        const auto bonus = load_qualifying_data(store, resolve_setup({.car_id = car}), 1, "BONUS");
        expect(bonus.local_actor == 8 && bonus.drivers[8].name == "BONUS" && bonus.drivers[8].car_id == car && bonus.drivers[9].car_id == car
                   && bonus.drivers[8].raw == data.drivers[8].raw && bonus.drivers[10].car_id == data.drivers[10].car_id,
            "Each bonus car must replace Ford's actor pair while retaining the original AI bank");
        toca::SourceRandom bonus_random(1234);
        const auto classified = classify_qualifying(bonus, 100123, bonus_random);
        expect(classified.local_actor == 8 && classified.rows[classified.local_rank].car_id == car, "Qualifying classification must retain the selected bonus car and actor8");
    }

    struct Bounds {
        std::int32_t minimum, maximum;
    };

    // CONFIG member1, u16 index48+60*difficulty+6*track+2*weather_group.
    // 444670/444730: snow is group1, rain group2; Lavaland always uses dry.
    constexpr std::array<std::array<Bounds, 3>, 10> defaults{{
        {{{88, 123}, {87, 105}, {86, 95}}},
        {{{77, 108}, {75, 90}, {73, 81}}},
        {{{70, 98}, {68, 82}, {66, 72}}},
        {{{43, 60}, {41, 49}, {39, 43}}},
        {{{54, 76}, {52, 62}, {51, 56}}},
        {{{73, 102}, {68, 82}, {64, 71}}},
        {{{81, 113}, {79, 95}, {77, 85}}},
        {{{48, 67}, {47, 56}, {46, 51}}},
        {{{66, 92}, {64, 77}, {63, 69}}},
        {{{38, 62}, {36, 60}, {34, 54}}},
    }};
    for (std::uint32_t track = 0; track < defaults.size(); ++track) {
        for (std::uint32_t difficulty = 0; difficulty < defaults[track].size(); ++difficulty) {
            const auto selected = load_qualifying_data(store, resolve_setup({.track_id = track}), difficulty, "PLAYER 1");
            const auto& expected = defaults[track][difficulty];
            expect(selected.minimum_centiseconds == expected.minimum * 100 && selected.maximum_centiseconds == expected.maximum * 100,
                "Every track's default weather/difficulty must select its exact source QUALPC bounds");
            toca::SourceRandom classify_random(1234);
            const auto classified = classify_qualifying(selected, std::uint32_t(expected.minimum * 1000 + 123), classify_random);
            expect(classified.rows[classified.local_rank].actor_id == selected.local_actor && classified.rows[classified.local_rank].time_ms == expected.minimum * 1000u + 120,
                "Every track/difficulty's original driver bank and variation bounds must classify the measured local lap");
        }
    }
    // The original first48 QUALPC shorts are the24 championship pairs;
    // their AIBIN banks are one-based1..24, independent of Single difficulty.
    constexpr std::array<Bounds, 12> championship_bounds{
        {{86, 120}, {76, 104}, {68, 91}, {40, 51}, {51, 64}, {64, 77}, {77, 90}, {46, 54}, {63, 71}, {68, 75}, {39, 44}, {73, 79}}};
    constexpr std::array<std::uint32_t, 12> championship_tracks{0, 1, 2, 3, 4, 5, 6, 7, 8, 2, 3, 1};
    constexpr std::array<std::uint32_t, 24> biela_ability{9, 10, 10, 10, 10, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 9, 9, 9};
    for (std::uint32_t round = 0; round < 24; ++round) {
        const auto meeting = round / 2;
        const auto selected = resolve_setup({.track_id = championship_tracks[meeting], .car_id = 1, .championship_meeting = meeting});
        const auto championship = load_qualifying_data(store, selected, 2, "CHAMPION", false, false, round);
        expect(championship.minimum_centiseconds == championship_bounds[meeting].minimum * 100 && championship.maximum_centiseconds == championship_bounds[meeting].maximum * 100,
            "Every championship race must read its own QUALPC pair, including return visits with different weather");
        expect(championship.local_actor == 2 && championship.drivers[2].name == "CHAMPION" && championship.drivers[2].car_id == 1
                   && championship.drivers[2].ability == biela_ability[round] && championship.drivers[15].ability == (round == 17 || round == 18 ? 8u : 9u),
            "Championship must select the one-based original driver bank and replace only the local driver's identity");
        const auto other_difficulty = load_qualifying_data(store, selected, 0, "CHAMPION", false, false, round);
        expect(other_difficulty.drivers[2].raw == championship.drivers[2].raw && other_difficulty.minimum_centiseconds == championship.minimum_centiseconds,
            "Single Race difficulty must not alter the championship's driver bank or qualifying bounds");
    }
    const auto thruxton_rain = load_qualifying_data(store, resolve_setup({.track_id = 2, .weather_choice = 3}), 1, "PLAYER 1");
    const auto thruxton_snow = load_qualifying_data(store, resolve_setup({.track_id = 2, .weather_choice = 5}), 1, "PLAYER 1");
    expect(thruxton_rain.minimum_centiseconds == 7500 && thruxton_rain.maximum_centiseconds == 9000 && thruxton_snow.minimum_centiseconds == 6900
               && thruxton_snow.maximum_centiseconds == 8300,
        "Original QUALPC rain and snow groups must retain their separate source order");
    const auto lava = load_qualifying_data(store, resolve_setup({.track_id = 9, .weather_choice = 5}), 1, "PLAYER 1");
    expect(lava.minimum_centiseconds == 3600 && lava.maximum_centiseconds == 6000 && lava.weather_speed_penalty_q16 == 0,
        "Lavaland must use its original dry qualifying bounds and weather parameters even when snow was selected");
    toca::SourceRandom random(1234);
    const auto result = classify_qualifying(data, 100123, random);
    expect(std::is_sorted(result.rows.begin(), result.rows.end(),
               [](const auto& a, const auto& b) {
                   return a.time_ms < b.time_ms;
               })
               && result.rows[result.local_rank].actor_id == 0 && result.rows[result.local_rank].time_ms == 100120,
        "Canonical classification must sort generated rivals together with actual local centiseconds");
    const auto archive = store.open_archive("data/donlfree.bfp");
    const auto cameras = decode_scene_cameras(archive.member(4).bytes());
    FinishCamera finish(cameras);
    finish.update({-131 * 65536, 7 * 65536, -90 * 65536});
    expect(finish.selected() < cameras.size(), "Canonical authored camera index escaped its resource list");
    for (const auto value : finish.world_to_view({-131 * 65536, 7 * 65536, -90 * 65536})) {
        expect(std::isfinite(value), "Canonical qualifying camera must have a finite affine inverse");
    }
    const auto keep = store.open_archive("data/donlkeep.bfp");
    const toca::HpcSurface ground(keep.member(0).bytes());
    const std::array<std::int32_t, 3> target{-131 * 65536, 7 * 65536, -90 * 65536};
    const auto close_position = finish.position(target, &ground, true);
    const auto support = ground.prop_sample(close_position[0], close_position[2], 0);
    expect(support && close_position[1] >= support->height + 3 * 65536, "Original race-result close camera must remain at least three units above actual HPC ground");
    const auto authored_matrix = finish.world_to_view(target), close_matrix = finish.world_to_view(target, &ground, true);
    const auto close_origin = transform(close_matrix, close_position);
    expect(std::abs(close_origin[0]) < .001f && std::abs(close_origin[1]) < .001f && std::abs(close_origin[2]) < .001f,
        "Race-result view matrix must use its actual close camera origin");
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            expect(authored_matrix[row * 4 + column] == close_matrix[row * 4 + column], "Original close branch must retain the authored look direction after moving its origin");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-qualifying-tests [canonical-root]");
        }
        std::cout << "PASS: original qualifying records, shared RNG, grid ranks and finish camera\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
