#include "frontend/cheats.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca::frontend::cheats;
        State state;
        expect(catalog().size() == 22 && code_text(Code::micro) == "CMMICRO" && code_text(Code::patscreem) == "PATSCREEM", "Original code catalog order changed");
        expect(track_max_id(state) == 8 && track_available(state, 0) && track_available(state, 1) && !track_available(state, 2), "Fresh progress must open only tracks0/1");
        expect(car_available(state, 7) && !car_available(state, 8) && !car_available(state, 9), "Bonus cars must start locked");
        expect(!redeem(state, "jhammo").recognized && !redeem(state, "OTHER").recognized, "Lookup must preserve original strcmp behavior");
        expect(find_code("CMDISCO") == Code::disco && !redeem(state, "CMDISCO").accepted, "The original disabled code must not report an activation");
        for (const auto& entry : catalog()) {
            if (entry.code == Code::disco) {
                continue;
            }
            State code_state;
            const auto first = redeem(code_state, entry.text);
            expect(first.accepted && first.activated && active(code_state, entry.code), "Every original allowed code must have its individual saved flag");
            const auto second = redeem(code_state, entry.text);
            expect(second.accepted && !second.activated && !active(code_state, entry.code), "Every original code must disable without activation speech");
        }
        const auto tracks = redeem(state, "JHAMMO");
        expect(tracks.recognized && tracks.accepted && tracks.activated && tracks.changed && track_available(state, 8) && track_max_id(state) == 8,
            "Typed JHAMMO must bypass availability without changing the separate bonus browse rule");
        State direct;
        (void)apply(direct, Code::all_tracks);
        expect(state == direct, "Typed and direct original overrides must share one transition");
        expect(!redeem(state, "JHAMMO").activated && !state.overrides.all_tracks, "Second override redemption must disable silently");
        expect(set_menu_active(state, Code::all_tracks, true).changed && menu_active(state, Code::all_tracks) && track_max_id(state) == 9 && track_available(state, 9),
            "The direct tracks switch must include Lavaland");
        expect(!set_menu_active(state, Code::all_tracks, true).changed, "Repeated YES must preserve the track override");
        expect(set_menu_active(state, Code::bonus_cars, true).changed && car_available(state, 8) && car_available(state, 9), "The bonus-car switch must expose both cars");
        expect(redeem(state, "CMGARAGE").activated && state.progress.bonus_car_8 && state.progress.bonus_car_9, "Garage name code must earn both bonus cars");
        expect(!redeem(state, "CMGARAGE").activated && state.progress.bonus_car_8, "Second garage redemption must repeat its action without activation speech");
        state.progress.track_threshold = 9;
        state.progress.special_championship = true;
        const auto progress = state.progress;
        expect(set_menu_active(state, Code::all_tracks, false).changed && set_menu_active(state, Code::bonus_cars, false).changed && state.progress == progress
                   && car_available(state, 8) && track_available(state, 9),
            "Disabling menu overrides must preserve earned cars, tracks and Showdown");
        const auto overrides = state.overrides;
        expect(redeem(state, "CMLOCK").activated && state.progress == Progress{} && state.overrides == overrides, "CMLOCK must reset only progress to threshold1");
        state.progress.bonus_car_8 = true;
        expect(!redeem(state, "CMLOCK").activated && !state.progress.bonus_car_8, "Second CMLOCK redemption must still execute its progress reset silently");
        std::array<bool, 22> listed{};
        for (const auto category : {Category::unlocks, Category::camera, Category::effects, Category::racing}) {
            for (const auto code : category_codes(category)) {
                expect(!listed[static_cast<std::uint32_t>(code)], "Each effect must have exactly one category");
                listed[static_cast<std::uint32_t>(code)] = true;
                State direct;
                const auto enable = set_menu_active(direct, code, true);
                expect(effect_ready(code), "Every listed effect must have its original consumer");
                expect(enable.accepted && enable.changed && menu_active(direct, code), "Ready menu effects must enable directly");
                expect(!set_menu_active(direct, code, true).changed, "Setting YES twice must be idempotent");
                expect(set_menu_active(direct, code, false).changed && !menu_active(direct, code), "Ready menu effects must disable directly");
                expect(!set_menu_active(direct, code, false).changed, "Setting NO twice must be idempotent");
            }
        }
        State partial;
        (void)redeem(partial, "JHAMMO");
        expect(!menu_active(partial, Code::all_tracks) && set_menu_active(partial, Code::all_tracks, true).changed && track_max_id(partial) == 9,
            "The complete track switch must reflect and complete the typed code's partial override");
        expect(!allowed(Code::disco) && allowed(Code::micro, 1) && allowed(Code::impossible, 1) && !allowed(Code::all_tracks, 1) && allowed(Code::all_tracks, 3)
                   && !allowed(Code::garage, 3),
            "Original mode gates changed");
        const auto before = state;
        expect(!apply(state, Code::garage, 1).accepted && state == before, "Restricted original modes must not mutate progress");
        State enabled;
        for (const auto& entry : catalog()) {
            (void)apply(enabled, entry.code);
        }
        enabled.overrides.bonus_track = true;
        for (const auto mode : {0u, 1u, 2u, 3u}) {
            const auto session = for_mode(enabled, mode);
            for (const auto& entry : catalog()) {
                expect(active(session, entry.code) == (allowed(entry.code, mode) && active(enabled, entry.code)), "Session effects must respect the original per-mode gate");
            }
            expect(session.progress == enabled.progress && session.overrides.bonus_track == allowed(Code::all_tracks, mode),
                "Mode filtering must retain earned progress and filter bonus-track browsing with the track override");
        }
        State showdown;
        expect(set_menu_active(showdown, Code::patscreem, true).changed && showdown_available(showdown) && showdown.progress == Progress{},
            "Showdown's menu switch must grant playable access without writing earned progress");
        expect(set_menu_active(showdown, Code::patscreem, false).changed && !showdown_available(showdown), "Unearned Showdown access must be reversible");
        (void)redeem(showdown, "PATSCREEM");
        expect(set_menu_active(showdown, Code::patscreem, false).changed && showdown_available(showdown) && showdown.progress.special_championship,
            "Disabling the switch must retain Showdown earned by the original name code");
        expect(!track_available(state, 10) && !car_available(state, 10), "Availability must respect original content bounds");
        State copter;
        (void)set_menu_active(copter, Code::all_tracks, true);
        (void)apply(copter, Code::copter);
        expect(!track_available(copter, 0) && track_available(copter, 1) && track_available(copter, 3) && track_available(copter, 4) && track_available(copter, 7)
                   && track_available(copter, 8) && !track_available(copter, 9),
            "Original helicopter track filtering must remain independent of unlock overrides");
        (void)apply(copter, Code::patscreem);
        (void)apply(copter, Code::patscreem);
        expect(copter.progress.special_championship && !active(copter, Code::patscreem), "PATSCREEM must execute its permanent side effect on both transitions");
        const auto copter_progress = copter.progress;
        expect(set_menu_active(copter, Code::copter, false).changed && copter.progress == copter_progress, "Disabling an active camera must retain the permanent Showdown unlock");
        std::cout << "cheats ok\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
