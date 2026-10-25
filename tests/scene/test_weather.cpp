#include "assets/store.hpp"
#include "core/random.hpp"
#include "scene/weather.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::int32_t one = 65536;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

PropFragmentTables constant() {
    PropFragmentTables result;
    result.positive.fill(one / 2);
    return result;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    auto crc = UINT32_C(0xffffffff);
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

void presentation() {
    for (const auto weather : {3u, 4u}) {
        for (const auto fps : {60u, 144u}) {
            SourceRandom random(7);
            auto tables = make_prop_fragment_tables(random);
            Weather state(tables, resolve_setup({.weather_choice = weather}), random, {});
            const auto initial = state.frame().particles.front();
            expect(state.presentation_frame(0).particles == state.frame().particles, "Weather creation must initialize presentation history at its actual spawn");
            const auto fall = weather == 3 ? -0x9999 : -0x6666;
            WeatherFrame rendered;
            for (std::uint32_t sample = 0; sample <= fps / 5; ++sample) {
                const auto tick = sample * 25 / fps + 1;
                while (state.frame().updates < tick) {
                    (void)state.advance();
                    state.update_view({});
                }
                const auto alpha = float(sample * 25 % fps) / float(fps);
                const auto cursor = tables.cursor;
                const auto current = state.frame();
                state.presentation_frame_into(rendered, alpha);
                const auto expected_y = std::int64_t(initial[1]) + std::llround(double(fall) * (double(tick - 1) + alpha));
                expect(std::abs(std::int64_t(rendered.particles.front()[1]) - expected_y) <= 1,
                    "Rain and snow must move continuously at60/144Hz between unchanged25Hz simulation ticks");
                expect(weather == 3 ? rendered.lines.front().to == rendered.particles.front() : rendered.sprites.front().center == rendered.particles.front(),
                    "Weather draw geometry must use the actual interpolated particle position");
                expect(state.frame().particles == current.particles && state.frame().updates == current.updates && tables.cursor == cursor
                           && rendered.lightning_alpha == current.lightning_alpha,
                    "Weather presentation must preserve source positions, update count, lightning and shared random cursor");
                expect(state.presentation_frame(1).particles == current.particles, "Paused/exact weather presentation must retain authoritative coordinates");
            }
            WeatherCamera cut;
            cut.position = {25 * one, 0, 0};
            state.update_view(cut);
            expect(state.presentation_frame(0).particles == state.frame().particles && state.presentation_frame(.5f).particles == state.frame().particles,
                "Weather recentering must cut rather than sweeping particles across the wrap cube");
            state.update_view(cut);
            const auto before_wrap = state.frame();
            bool wrapped = false;
            for (unsigned tick = 0; tick < 40 && !wrapped; ++tick) {
                const auto previous = state.frame();
                state.update_view(cut);
                const auto rendered = state.presentation_frame(.5f);
                for (std::size_t particle = 0; particle < previous.particles.size(); ++particle) {
                    if (state.frame().particles[particle][1] > previous.particles[particle][1]) {
                        expect(rendered.particles[particle] == state.frame().particles[particle], "An individually wrapped particle must cut to its new cube edge");
                        wrapped = true;
                    }
                }
            }
            expect(wrapped && state.frame().updates > before_wrap.updates, "Weather presentation regression must exercise a real per-particle wrap");
        }
    }
}

void presentation_reuse() {
    SourceRandom random(7);
    auto tables = make_prop_fragment_tables(random);
    Weather rain(tables, resolve_setup({.weather_choice = 3}), random, {});
    Weather snow(tables, resolve_setup({.weather_choice = 4}), random, {});
    Weather storm(tables, resolve_setup({.weather_choice = 5}), random, {});
    Weather dry(tables, resolve_setup({.weather_choice = 1}), random, {});
    WeatherFrame rendered;
    rain.presentation_frame_into(rendered, 1);
    snow.presentation_frame_into(rendered, 1);
    const auto particles = rendered.particles.data();
    const auto lines = rendered.lines.data();
    const auto sprites = rendered.sprites.data();
    const auto cursor = tables.cursor;
    const auto source_random = random.state();
    for (const auto* state : {&rain, &snow, &storm, &dry, &rain}) {
        state->presentation_frame_into(rendered, .5f);
        const auto& expected = state->frame();
        expect(rendered.particles == expected.particles && rendered.lines.size() == expected.lines.size() && rendered.sprites.size() == expected.sprites.size()
                   && rendered.wind == expected.wind && rendered.streak == expected.streak && rendered.updates == expected.updates && rendered.wind_updates == expected.wind_updates
                   && rendered.lightning_alpha == expected.lightning_alpha,
            "Reusing weather output across rain, snow, storm and dry must replace every stream and its metadata");
        for (std::size_t index = 0; index < rendered.lines.size(); ++index) {
            expect(rendered.lines[index].from == expected.lines[index].from && rendered.lines[index].to == expected.lines[index].to
                       && rendered.lines[index].color == expected.lines[index].color,
                "Reused rain output must retain exact endpoint and colour bytes");
        }
        for (std::size_t index = 0; index < rendered.sprites.size(); ++index) {
            const auto& a = rendered.sprites[index];
            const auto& b = expected.sprites[index];
            expect(a.center == b.center && a.minimum == b.minimum && a.maximum == b.maximum && a.texture == b.texture && a.brightness == b.brightness && a.alpha == b.alpha,
                "Reused snow output must retain exact geometry and texture fields");
        }
        expect(rendered.particles.data() == particles && rendered.lines.data() == lines && rendered.sprites.data() == sprites,
            "Weather output must retain warmed storage across changing particle counts and empty streams");
    }
    expect(tables.cursor == cursor && random.state() == source_random, "Reusing weather output must leave both random streams untouched");
}

void synthetic() {
    presentation();
    presentation_reuse();
    constexpr WeatherCamera camera;
    for (std::uint32_t track = 0; track < 10; ++track) {
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            SourceRandom random(7), expected_random(7);
            auto tables = make_prop_fragment_tables(random);
            (void)make_prop_fragment_tables(expected_random);
            const auto setup = resolve_setup({.track_id = track, .weather_choice = weather}, 1);
            Weather state(tables, setup, random, camera);
            const auto count = track == 9 ? 0u : weather == 3 ? 90u : weather == 4 ? 25u : weather == 5 ? 10u : 0u;
            for (std::uint32_t draw = 0; draw < count * 3; ++draw) {
                (void)expected_random.next();
            }
            expect(random.next() == expected_random.next(), "Weather placement must consume exactly three source RNG draws per particle");
            expect(state.frame().particles.size() == count && state.frame().sprites.size() == (weather == 4 && track != 9 ? 25u : 0u),
                "Resolved weather must select original rain/snow/storm counts, with Lavaland dry");
            expect(tables.cursor == (count == 0 ? 0u : weather == 4 ? 26u : 1u), "Weather setup must preserve sprite selection and the shared wind-table peek");
        }
    }

    SourceRandom random(7);
    auto tables = make_prop_fragment_tables(random);
    Weather rain(tables, resolve_setup({.weather_choice = 3}), random, camera);
    expect(rain.frame().particles[0] == WeatherVector{266724, 351276, 993532}, "Source placement must use X,Z,Y draw order and the exact fixed random fraction");
    expect(!rain.advance(), "Rain must not invent a thunder event");
    rain.update_view(camera);
    expect(rain.frame().particles[0] == WeatherVector{266724, 311955, 993532} && rain.frame().streak == WeatherVector{0, -0x9999, 0},
        "Rain must advance by the original vertical increment in the camera-centered cube");
    expect(
        rain.frame().lines[0].from == WeatherVector{266724, 272634, 993532} && rain.frame().lines[0].to == rain.frame().particles[0] && rain.frame().lines[0].color == 0x72969696,
        "GPU rain must retain endpoint order, source grey and separately shifted per-particle alpha");
    auto moved = camera;
    moved.position[0] = one;
    rain.update_view(moved);
    expect(rain.frame().streak == WeatherVector{-one, -0x9999, 0}, "Rain streaks must include small camera displacement without moving the entire particle field");
    moved.position[0] = 25 * one;
    rain.update_view(moved);
    expect(rain.frame().streak == WeatherVector{0, -0x9999, 0}, "Camera jumps greater than five source units must suppress displacement streaks");
    for (const auto& particle : rain.frame().particles) {
        expect(std::abs(std::int64_t(particle[0]) - moved.position[0]) <= 6 * one && std::abs(std::int64_t(particle[1])) <= 6 * one
                   && std::abs(std::int64_t(particle[2]) - 10 * one) <= 6 * one,
            "Camera re-centering must wrap particles inside every inclusive cube axis");
    }
    const auto paused = rain.frame().particles;
    const auto paused_cursor = tables.cursor;
    for (unsigned frame = 0; frame < 200; ++frame) {
        (void)rain.frame();
    }
    expect(rain.frame().particles == paused && rain.frame().updates == 3 && rain.frame().wind_updates == 1 && tables.cursor == paused_cursor,
        "Presentation reads without active simulation/view ticks must freeze particles, wind and the shared cursor");

    for (const auto low_gravity : {false, true}) {
        for (const auto up : {false, true}) {
            auto sprite_tables = constant();
            SourceRandom source(7);
            const auto setup = resolve_setup({.weather_choice = 3, .rain_up = up, .cat_dog = true});
            Weather animals(sprite_tables, setup, source, camera, low_gravity);
            const auto first = animals.frame().particles[0];
            animals.update_view(camera);
            const auto fall = (low_gravity ? 0xa3d : 0x6666) * (up ? 1 : -1);
            expect(animals.frame().streak[1] == fall && animals.frame().particles.size() == 25 && animals.frame().sprites.size() == 25
                       && animals.frame().sprites[0].minimum == WeatherVector{-0x4000, -0x4000, 0} && animals.frame().sprites[0].maximum == WeatherVector{0x4000, 0x4000, 0}
                       && animals.frame().sprites[0].alpha == 254 && animals.frame().sprites[0].brightness == 255,
                "Cats/Dogs must retain sprite size, source alpha/brightness and vertical extras");
            const auto delta = std::int64_t(animals.frame().particles[0][1]) - first[1];
            expect(delta == fall || delta == fall + 12 * one || delta == fall - 12 * one, "Vertical extra must only change source fall, preserving cube wrapping");
            expect(sprite_tables.cursor == 14 && animals.frame().sprites[0].texture == WeatherTexture::cats,
                "Sprite update advances all25 shared table slots even without a texture-changing wrap; equality8000 selects Cats");
        }
        auto drop_tables = constant();
        SourceRandom source(7);
        Weather upward(drop_tables, resolve_setup({.rain_up = true}), source, camera, low_gravity);
        upward.update_view(camera);
        expect(upward.frame().streak[1] == (low_gravity ? 0xf5c : 0x9999) && upward.frame().lines.size() == 90,
            "CMRAINUP must reverse ordinary rain with its own low-gravity constants");
    }

    auto wind_tables = constant();
    SourceRandom wind_random(7);
    Weather wind(wind_tables, resolve_setup({.weather_choice = 3}), wind_random, camera);
    for (unsigned tick = 0; tick < 80; ++tick) {
        (void)wind.advance();
    }
    expect(wind.frame().wind == WeatherVector{} && wind_tables.cursor == 1, "Initial weather wind hold must last80 source updates without table draws");
    (void)wind.advance();
    expect(wind.frame().wind == WeatherVector{326, 0, 5} && wind_tables.cursor == 5, "First wind gust must use four shared samples and original327/11 slew limits");
    (void)wind.advance();
    expect(wind.frame().wind == WeatherVector{653, 0, 22}, "Wind must retain source lookup and per-product fixed rounding");
    const auto saved = wind.persistent_state();
    auto retained_tables = wind_tables;
    SourceRandom retained_random(7);
    Weather retained(retained_tables, resolve_setup({.weather_choice = 1}), retained_random, camera, false, saved);
    expect(retained.frame().particles.empty() && retained.frame().wind == wind.frame().wind && retained_tables.cursor == wind_tables.cursor,
        "Wet-to-dry scene ownership must preserve source wind globals without precipitation or new setup draws");
    (void)wind.advance();
    (void)retained.advance();
    expect(retained.frame().wind == wind.frame().wind && retained.wind_state().elapsed == wind.wind_state().elapsed && retained_tables.cursor == wind_tables.cursor,
        "Retained dry wind must continue the original gust instead of restarting from cold BSS state");
    auto replaced_tables = wind_tables;
    Weather replaced(replaced_tables, resolve_setup({.weather_choice = 3}), retained_random, camera, false, saved);
    expect(replaced.frame().wind == WeatherVector{} && replaced.wind_state().strength == 0 && replaced.wind_state().hold == 80,
        "A new precipitation setup must replace inherited wind using the original reset fields");
    auto dry_tables = constant();
    SourceRandom dry_random(7);
    Weather dry(dry_tables, resolve_setup({.weather_choice = 1}), dry_random, camera);
    (void)dry.advance();
    expect(dry.frame().particles.empty() && dry_tables.cursor == 2 && dry.frame().wind == WeatherVector{},
        "Dry weather still consumes the original wind cycle's shared table values, without precipitation setup draws");

    auto flash_tables = constant();
    SourceRandom flash_random(7);
    Weather storm(flash_tables, resolve_setup({.weather_choice = 5}), flash_random, camera);
    constexpr std::array<std::uint8_t, 6> flash{0, 0, 180, 140, 100, 60};
    for (unsigned tick = 1; tick <= 83; ++tick) {
        const auto thunder = storm.advance();
        expect(tick == 83 ? thunder == one / 2 : !thunder, "Thunder must arrive once at the next446a80 step after its exact source delay");
        storm.update_view(camera);
        if (tick <= flash.size()) {
            expect(storm.frame().lightning_alpha == flash[tick - 1], "Storm flash must preserve the original variant and reversed six-frame lookup");
        } else {
            expect(storm.frame().lightning_alpha == 0, "Storm flash must stay absent throughout its source rest interval");
        }
    }
    expect(!storm.advance(), "Reading/draining the previous thunder event must not replay it");

    auto lifecycle_tables = constant();
    SourceRandom lifecycle_random(7);
    Weather first_storm(lifecycle_tables, resolve_setup({.weather_choice = 5}), lifecycle_random, camera);
    for (unsigned tick = 0; tick < 79; ++tick) {
        expect(!first_storm.advance(), "Storm lifecycle fixture must not reach its first thunder before scene exit");
        first_storm.update_view(camera);
    }
    expect(first_storm.persistent_state().thunder_remaining == 3, "Storm lifecycle fixture must retain the original three remaining view steps");
    Weather clear(lifecycle_tables, resolve_setup({.weather_choice = 1}), lifecycle_random, camera, false, first_storm.persistent_state());
    for (unsigned tick = 0; tick < 20; ++tick) {
        expect(!clear.advance(), "Dry scene must not manufacture a delayed storm event");
        clear.update_view(camera);
    }
    expect(clear.persistent_state().thunder_remaining == 3 && clear.frame().lightning_alpha == 0, "Dry views must preserve, without advancing, the original storm delay globals");
    Weather second_storm(lifecycle_tables, resolve_setup({.weather_choice = 5}), lifecycle_random, camera, false, clear.persistent_state());
    for (unsigned tick = 1; tick <= 3; ++tick) {
        expect(!second_storm.advance(), "Inherited thunder must retain the next446a80 delivery boundary");
        second_storm.update_view(camera);
        expect(second_storm.frame().lightning_alpha == flash[tick - 1], "A fresh storm setup must restart its six-frame flash independently of inherited thunder");
    }
    expect(second_storm.persistent_state().pending == one / 2, "Inherited thunder timer must reach its ready event during the restarted flash");
    auto pending_tables = lifecycle_tables;
    Weather pending_clear(pending_tables, resolve_setup({.weather_choice = 1}), lifecycle_random, camera, false, second_storm.persistent_state());
    expect(pending_clear.advance() == one / 2 && !pending_clear.advance(), "A ready event must survive scene ownership and be consumed once, without render-dependent replay");
    expect(second_storm.advance() == one / 2 && !second_storm.advance(), "The restored storm must deliver its inherited delayed thunder exactly once");

    std::array<float, 16> view{.5f, 0, 0, 0, 0, .25f, 0, 0, 0, 0, .125f, 0, -2, -3, -4, 1};
    const auto affine = weather_camera(view);
    expect(affine.position == WeatherVector{4 * one, 12 * one, 32 * one} && affine.forward == WeatherVector{0, 0, 8 * one} && affine.right == WeatherVector{2 * one, 0, 0}
               && affine.up == WeatherVector{0, 4 * one, 0},
        "Weather camera conversion must invert the full affine matrix instead of transposing a non-unit basis");
}

void canonical(const std::filesystem::path& root) {
    const AssetStore store(root);
    const auto resources = load_weather_resources(store);
    constexpr std::array<std::size_t, 3> sizes{1142, 608, 608};
    constexpr std::array<std::uint32_t, 3> checksums{0x01768568, 0x37f2860e, 0xb653adb6};
    for (std::size_t texture = 0; texture < resources.textures.size(); ++texture) {
        expect(resources.textures[texture].size() == sizes[texture] && crc32(resources.textures[texture]) == checksums[texture],
            "Canonical Snow/Dogs/Cats IMAGE640 member identity changed");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-weather-tests [canonical-root]");
        }
        std::cout << "PASS: original precipitation, shared wind, extras, lightning/thunder and camera/resource data\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
