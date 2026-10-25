#include "assets/store.hpp"
#include "scene/engine_effects.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
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

PropFragmentTables table() {
    PropFragmentTables result;
    result.positive.fill(one / 2);
    return result;
}

EngineEffectVehicle car() {
    EngineEffectVehicle result;
    result.engine_speed_q16 = one;
    result.gear = 1;
    return result;
}

void ready(EngineEffects& effects) {
    for (int tick = 0; tick < 902; ++tick) {
        effects.advance();
    }
    expect(effects.frame().cooldown == 0 && effects.frame().phase == 0, "420bb0 must reach its first jointly eligible cooldown/phase at tick902");
}

void timing_and_burst() {
    auto shared = table();
    EngineEffects effects(shared);
    std::array cars{car()};
    effects.emit(cars);
    expect(effects.frame().sprites.empty() && effects.frame().cooldown == 900 && effects.frame().phase == 2, "4209d0 starts a cold scene at cooldown900/phase2");
    for (int tick = 0; tick < 900; ++tick) {
        effects.advance();
        effects.emit(cars);
        expect(effects.frame().sprites.empty(), "A startup burst cannot bypass the global900-tick cooldown and phase gate");
    }
    expect(effects.frame().cooldown == 0 && effects.frame().phase == 2, "Cooldown reaches zero on phase2, requiring two further source ticks");
    effects.advance();
    effects.emit(cars);
    expect(effects.frame().sprites.empty(), "Phase1 cannot trigger a generic exhaust burst");
    effects.advance();
    effects.emit(cars);
    const auto initial = effects.frame().sprites.at(0);
    expect(initial.center == SurfaceEffectVector{-148307, -27656, 23789} && initial.minimum == SurfaceEffectVector{-6553, -6553, 0}
               && initial.maximum == SurfaceEffectVector{6553, 6553, 0} && initial.shade0 == 200 && effects.frame().flags == 6,
        "420be0 first burst must use the original Honda exhaust offset,0x1999 radius and shade200");
    expect(effects.frame().sounds.size() == 1 && effects.frame().sounds[0].position == initial.center && effects.frame().sounds[0].kind == 17
               && effects.frame().sounds[0].strength_q16 == one && effects.frame().sounds[0].local_player && effects.frame().cooldown == 900 && shared.cursor == 0,
        "The first sprite emits event17 strength1 exactly once and resets global cooldown without consuming RNG");
    effects.advance();
    cars[0].position = {one, 2 * one, 3 * one};
    cars[0].engine_speed_q16 = 0;
    cars[0].gear = 0;
    effects.emit(cars);
    expect(effects.frame().sprites[0].center == SurfaceEffectVector{-95878, 103416, 220397} && effects.frame().sprites[0].maximum[0] == 9829
               && effects.frame().sprites[0].shade0 == 160 && effects.frame().sounds.empty(),
        "Second visible tick follows the current car, moves backward0x3333, expands by0xccc and ignores new gear/RPM gates");
    effects.advance();
    effects.emit(cars);
    expect(effects.frame().sprites[0].center[0] == -108985 && effects.frame().sprites[0].maximum[0] == 13105 && effects.frame().sprites[0].shade0 == 120
               && effects.frame().sounds.empty(),
        "Third burst tick retains source separately multiplied backward displacement and shade120");
    effects.advance();
    effects.emit(cars);
    expect(effects.frame().sprites.empty() && effects.frame().emitted == 3, "The fourth visible call clears the completed three-sprite burst");
}

void gates_and_visibility() {
    auto shared = table();
    EngineEffects effects(shared);
    ready(effects);
    std::array cars{car()};
    cars[0].engine_speed_q16 = 0x9999;
    effects.emit(cars);
    expect(effects.frame().sprites.empty(), "Engine-speed increase exactly0x9999 is below the strict trigger");
    cars[0].engine_speed_q16 = 0x999a;
    cars[0].gear = 0;
    effects.emit(cars);
    expect(effects.frame().sprites.empty(), "Neutral suppresses a generic burst despite an eligible speed increase");
    cars[0].gear = 7;
    shared.cursor = 1;
    effects.emit(cars);
    expect(effects.frame().sprites.empty() && shared.cursor == 1, "A nonzero shared cursor rejects the burst without advancing that cursor");
    shared.cursor = 0;
    cars[0].actor = 11;
    cars[0].visibility = 0;
    effects.emit(cars);
    expect(effects.frame().sprites.empty(), "4608b0 excludes invisible cars before evaluating420be0");
    cars[0].visibility = 2;
    cars[0].local_player = false;
    effects.emit(cars);
    expect(effects.frame().sprites.size() == 1 && effects.frame().visibility == std::vector<std::uint8_t>{2} && !effects.frame().sounds[0].local_player
               && effects.frame().sounds[0].actor == 11,
        "A reverse-gear burst retains the actual view mask and remote-audio identity");
    cars[0].visibility = 0;
    for (int tick = 0; tick < 10; ++tick) {
        effects.advance();
        effects.emit(cars);
    }
    cars[0].visibility = 2;
    effects.emit(cars);
    expect(effects.frame().sprites[0].shade0 == 160, "A hidden car freezes burst age while global timers continue");
    const auto cooldown = effects.frame().cooldown;
    const auto phase = effects.frame().phase;
    effects.reset();
    expect(effects.frame().sprites.empty() && effects.frame().sounds.empty() && effects.frame().cooldown == cooldown && effects.frame().phase == phase,
        "420b80 grid reset cancels bursts and output while preserving cooldown/phase");
    effects.emit(cars);
    expect(effects.frame().sprites.empty(), "A canceled burst cannot resume after grid reset or bypass its retained cooldown");
}

void hidden_actor_and_order() {
    auto shared = table();
    EngineEffects effects(shared);
    ready(effects);
    auto remote = car(), human = car();
    remote.actor = 11;
    remote.local_player = false;
    human.actor = 3;
    human.visibility = 0;
    const std::array first{human, remote};
    effects.emit(first);
    expect(effects.frame().sounds.size() == 1 && effects.frame().sounds[0].actor == 11 && !effects.frame().sounds[0].local_player,
        "Generic type17 must carry its permanent remote actor, not its position in the visible emission list");
    remote.visibility = 0;
    for (unsigned tick = 0; tick < 900; ++tick) {
        effects.advance();
        effects.emit(std::array{remote, human});
    }
    expect(effects.frame().cooldown == 0 && effects.frame().phase == 0 && effects.frame().sprites.empty() && shared.cursor == 0,
        "Invisible actors freeze their own bursts while the shared cooldown and phase continue without consuming table samples");
    human.visibility = 1;
    effects.emit(std::array{human, remote});
    expect(effects.frame().sounds.size() == 1 && effects.frame().sounds[0].actor == 3 && effects.frame().sounds[0].local_player,
        "A newly visible human may trigger during a remote actor's suspended burst without inheriting its audio identity");
    remote.visibility = 2;
    effects.emit(std::array{remote, human});
    expect(effects.frame().sprites.size() == 2 && effects.frame().sprites[0].shade0 == 160 && effects.frame().sprites[1].shade0 == 160
               && effects.frame().visibility == std::vector<std::uint8_t>{2, 1} && effects.frame().sounds.empty(),
        "Reordered actors resume their own second emission despite the new global cooldown, with no duplicate type17 calls");
    const auto cooldown = effects.frame().cooldown;
    const auto phase = effects.frame().phase;
    effects.reset();
    effects.emit(std::array{human, remote});
    expect(effects.frame().sprites.empty() && effects.frame().sounds.empty() && effects.frame().cooldown == cooldown && effects.frame().phase == phase,
        "Warm reset must cancel both visible and suspended bursts while retaining the shared timing gates");
}

void special_and_order() {
    auto shared = table();
    EngineEffects effects(shared, GameMode::time_trial);
    std::array cars{car()};
    cars[0].car_type = 8;
    cars[0].engine_speed_q16 = 0;
    cars[0].gear = 0;
    effects.emit(cars);
    expect(effects.frame().sprites.size() == 2 && shared.cursor == 2 && effects.frame().sounds.empty() && effects.frame().cooldown == 900,
        "TimeTrial car8 emits two sprites even cold and in neutral, without generic sound or cooldown trigger");
    expect(effects.frame().sprites[0].center == SurfaceEffectVector{-149356, 6553, 0} && effects.frame().sprites[1].center == SurfaceEffectVector{-155909, 6553, 0}
               && effects.frame().sprites[0].maximum[0] == 11469 && effects.frame().sprites[1].maximum[0] == 14090 && effects.frame().sprites[0].shade0 == 182
               && effects.frame().sprites[1].shade0 == 182,
        "Type8 special sprites retain source sizes, backward0x1999 offset and ASM integer200/Q16 brightness quirk");
    const auto cursor = shared.cursor;
    for (int draw = 0; draw < 144; ++draw) {
        (void)effects.frame();
    }
    expect(shared.cursor == cursor && effects.frame().updates == 0, "Render-only access cannot consume special-case RNG or tick timers");

    auto ordered_table = table();
    ordered_table.cursor = 35;
    EngineEffects ordered(ordered_table, GameMode::time_trial);
    ready(ordered);
    std::array pair{cars[0], car()};
    pair[1].actor = 1;
    ordered.emit(pair);
    expect(ordered_table.cursor == 0 && ordered.frame().sprites.size() == 3 && ordered.frame().sounds.size() == 1,
        "Car order matters: type8 consumes35/36, wraps to zero, then the following generic car can trigger without RNG");
    auto reversed_table = table();
    reversed_table.cursor = 35;
    EngineEffects reversed(reversed_table, GameMode::time_trial);
    ready(reversed);
    const std::array reverse_pair{pair[1], pair[0]};
    reversed.emit(reverse_pair);
    expect(reversed_table.cursor == 0 && reversed.frame().sprites.size() == 2 && reversed.frame().sounds.empty(),
        "Reversed car order must not retroactively give the rejected generic car a zero cursor");
}

void car_offsets_and_cadence() {
    constexpr std::array<SurfaceEffectVector, 10> offsets{{{-148307, -27656, 23789}, {-145424, -27131, -18022}, {-144113, -22020, -18022}, {-140771, -31588, 38076},
        {-149815, -25624, -15400}, {-142934, -28049, -15859}, {-142213, -24903, -40173}, {-144965, -30015, -16121}, {-149356, 6553, 0}, {-176947, -28573, -19660}}};
    for (std::uint8_t type = 0; type < 10; ++type) {
        auto shared = table();
        EngineEffects effects(shared);
        ready(effects);
        std::array cars{car()};
        cars[0].car_type = type;
        effects.emit(cars);
        expect(effects.frame().sprites.size() == 1 && effects.frame().sprites[0].center == offsets[type],
            "Every original car type uses4209d0's own exhaust anchor, including car8's ordinary non-TimeTrial branch");
    }
    auto first_table = table(), second_table = table();
    EngineEffects first(first_table), second(second_table);
    const std::array cars{car()};
    std::size_t sounds = 0;
    for (int tick = 0; tick < 1808; ++tick) {
        first.advance();
        second.advance();
        first.emit(cars);
        second.emit(cars);
        sounds += first.frame().sounds.size();
        for (int draw = 0; draw < tick % 7; ++draw) {
            (void)second.frame();
        }
        expect(first.frame().sprites.size() == second.frame().sprites.size() && first.frame().phase == second.frame().phase && first.frame().cooldown == second.frame().cooldown
                   && first_table.cursor == second_table.cursor,
            "Active25Hz updates must preserve timers, output and RNG across different render counts and zero-step pauses");
    }
    expect(sounds == 2 && first.frame().emitted == 6 && first_table.cursor == 0,
        "Generic bursts recur at ticks902/1802 with three sprites and one sound each, without shared RNG consumption");
}

void presentation() {
    auto shared = table();
    EngineEffects effects(shared, GameMode::time_trial);
    std::array cars{car()};
    cars[0].car_type = 8;
    cars[0].actor = 3;
    cars[0].position = {one, 2 * one, 3 * one};
    effects.emit(cars);
    const auto authoritative = effects.frame();
    const auto cursor = shared.cursor;
    std::array<float, 16> source{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1};
    for (const auto fps : {60u, 144u}) {
        for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
            const auto alpha = float(sample * 25) / float(fps);
            auto rendered_pose = source;
            rendered_pose[12] = alpha;
            const std::array poses{effect_pose(3, source, rendered_pose)};
            const auto rendered = effects.presentation_frame(poses);
            for (std::size_t sprite = 0; sprite < rendered.sprites.size(); ++sprite) {
                const auto& a = authoritative.sprites[sprite];
                const auto& b = rendered.sprites[sprite];
                expect(std::abs(double(b.center[0]) - (double(a.center[0]) + double(alpha - 1) * one)) <= 1 && b.center[1] == a.center[1] && b.center[2] == a.center[2],
                    "Permanent exhaust sprites must follow the displayed car continuously at60/144Hz");
                expect(b.minimum == a.minimum && b.maximum == a.maximum && b.shade0 == a.shade0 && b.shade1 == a.shade1,
                    "Exhaust presentation must preserve the current discrete burst size and shade");
            }
            expect(rendered.phase == authoritative.phase && rendered.cooldown == authoritative.cooldown && rendered.emitted == authoritative.emitted
                       && rendered.visibility == authoritative.visibility && rendered.sounds.empty() && shared.cursor == cursor,
                "Attached exhaust rendering must preserve source timers, emissions, view masks and special-case random cursor");
        }
    }
    // Exercise the full affine inverse with both poses scaled and the display
    // axes rotated90 degrees. These expected coordinates are derived directly.
    source[0] = 2;
    source[5] = 3;
    source[10] = 4;
    const std::array<float, 16> rotated{0, 0, -5, 0, 0, 6, 0, 0, 7, 0, 0, 0, 10, 20, 30, 1};
    const std::array poses{effect_pose(3, source, rotated)};
    const auto transformed = effects.presentation_frame(poses);
    for (std::size_t sprite = 0; sprite < transformed.sprites.size(); ++sprite) {
        const auto& point = authoritative.sprites[sprite].center;
        const SurfaceEffectVector expected{std::int32_t(std::llround(10.0 * one + 7.0 * (point[2] - 3.0 * one) / 4)),
            std::int32_t(std::llround(20.0 * one + 6.0 * (point[1] - 2.0 * one) / 3)), std::int32_t(std::llround(30.0 * one - 5.0 * (point[0] - double(one)) / 2))};
        expect(transformed.sprites[sprite].center == expected, "Exhaust attachment must preserve nonuniform source scale through a rotated, scaled display pose");
    }
    expect(effects.frame().sprites.front().center == authoritative.sprites.front().center && effects.frame().updates == authoritative.updates && shared.cursor == cursor,
        "Exhaust presentation must not replace source sprite coordinates or advance simulation");

    auto generic_table = table();
    EngineEffects generic(generic_table);
    ready(generic);
    generic.emit(std::array{car()});
    const auto sound = generic.frame().sounds.front();
    const std::array generic_pose{effect_pose(0, source, rotated)};
    const auto generic_frame = generic.presentation_frame(generic_pose);
    expect(generic_frame.sounds.size() == 1 && generic_frame.sounds.front().position == sound.position && generic_frame.sounds.front().kind == sound.kind
               && generic.frame().sounds.front().position == sound.position,
        "Reanchoring a generic burst must leave its original audio event at the authoritative emission position");
    effects.reset();
    expect(effects.presentation_frame(poses).sprites.empty(), "Exhaust grid reset must discard the previous sprite-to-actor bindings");
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    auto crc = UINT32_C(0xffffffff);
    for (const auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

void canonical(const std::filesystem::path& root) {
    const AssetStore store(root);
    const auto resources = load_engine_effect_resources(store);
    const auto images = store.open_archive("data/image640.bfp");
    const auto source = images.member(16).bytes();
    expect(resources.bang_bmp.size() == 1334 && crc32(resources.bang_bmp) == UINT32_C(0x293546a6)
               && std::equal(resources.bang_bmp.begin(), resources.bang_bmp.end(), source.begin(), source.end()),
        "4463e0 must retain the exact canonical16x16/8bpp BANG2 member16, CRC32293546a6");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            timing_and_burst();
            gates_and_visibility();
            hidden_actor_and_order();
            special_and_order();
            car_offsets_and_cadence();
            presentation();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-engine-effect-tests [canonical-root]");
        }
        std::cout << "PASS original exhaust flame trigger, timing, geometry, RNG, reset and BANG2 resource\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
