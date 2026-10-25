#include "assets/store.hpp"
#include "scene/championship_finale.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
using namespace toca::simulation;
constexpr std::int32_t one = 65536;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void put(std::span<std::uint8_t> bytes, std::size_t offset, std::int32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = std::uint8_t(std::uint32_t(value) >> (8 * index));
    }
}

std::int32_t get(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return fixed::bits(bytes[offset] | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24);
}

std::span<const std::uint8_t> bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

void sequence() {
    constexpr std::string_view text = "PAGE\r\nCREDITS\r\nFIRST NAME\r\n.HEADING\r\nPAGE\r\nSECOND\r\nPAGE\r\nEND\r\n";
    ChampionshipFinaleSequence flow(bytes(text));
    flow.advance(3'999'999'999);
    expect(!flow.can_exit() && flow.camera_phase() == 0 && flow.credit_page() == 0 && flow.credits().empty(), "Finale must reject exit before four active seconds");
    flow.advance(1);
    expect(flow.can_exit() && flow.camera_phase() == 0, "Exit opens at exactly four seconds, before the strictly-greater camera transition");
    flow.advance(40'000'000);
    expect(flow.camera_phase() == 0, "Source timer must retain its1/16-second quantization at4040ms");
    flow.advance(40'000'000);
    expect(flow.camera_phase() == 1 && flow.credit_page() == 1 && flow.credits().empty(), "The leading literal PAGE must retain its empty credit interval");
    flow.advance(4'000'000'000);
    const std::vector<ChampionshipFinaleCredit> first_page{{"CREDITS", true}, {"FIRST NAME", false}, {".HEADING", true}};
    expect(
        flow.camera_phase() == 2 && flow.credit_page() == 2 && flow.credits() == first_page, "Second transition must expose the actual first page, preserving dotted heading text");
    flow.advance(4'000'000'000);
    expect(flow.camera_phase() == 0 && !flow.can_exit() && flow.credit_page() == 3 && flow.credits()[0].text == "SECOND",
        "Strictly-after12s resets timer0, orbit camera and exit gate while advancing credits");
    flow.advance(4'080'000'000);
    expect(flow.camera_phase() == 1 && flow.credit_page() == 4 && flow.credits().empty(), "END must publish an empty page and rewind the stream");
    flow.advance(4'000'000'000);
    expect(flow.credit_page() == 5 && flow.credits().empty(), "Rewound credits must keep the leading PAGE empty interval");
    flow.advance(4'000'000'000);
    expect(flow.credit_page() == 6 && flow.credits().front().text == "CREDITS", "Credits must repeat indefinitely without exiting the scene");
    const auto before = flow.active_time_ms();
    flow.advance(0);
    expect(flow.active_time_ms() == before, "A paused host must not advance finale time");
    ChampionshipFinaleSequence whole(bytes(text)), split(bytes(text));
    whole.advance(25'000'000'000);
    for (int frame = 0; frame < 1500; ++frame) {
        split.advance(frame % 3 == 0 ? 16'666'666 : 16'666'667);
    }
    expect(whole.active_time_ms() == split.active_time_ms() && whole.camera_phase() == split.camera_phase() && whole.credit_page() == split.credit_page()
               && whole.credits() == split.credits(),
        "Presentation cadence must not change source timer or credit chronology");
}

void controls() {
    std::vector<std::uint8_t> route(4 + 18 * 64);
    put(route, 0, 18);
    for (std::size_t point = 0; point < 18; ++point) {
        const auto base = 4 + point * 64;
        put(route, base, 20 * one);
        put(route, base + 8, -10 * one - 1);
        put(route, base + 36, 20 * one);
        put(route, base + 44, 10 * one);
        put(route, base + 52, 130);
        put(route, base + 56, 140);
    }
    const auto centered = championship_finale_route(route);
    expect(get(centered, 4 + 20) == 0 && get(centered, 4 + 32) == 0 && get(route, 4 + 8) == -10 * one - 1,
        "Finale midpoint must divide signed edge deltas toward zero without modifying source bytes");
    expect(get(centered, 4 + 17 * 64 + 12) == 20 * one && get(centered, 4 + 17 * 64 + 52) == 130 && get(centered, 4 + 17 * 64 + 56) == 140,
        "Finale must center every route record while preserving original speed fields");
    std::array<std::uint8_t, 40> driver{};
    put(driver, 28, 0x8000);
    SourceRandom random(1);
    FinaleTraffic traffic{30 * one, 1};
    AiDriver truck(centered, driver, {}, 0, 0, random), pace(centered, driver, {}, 0, 0, random);
    truck.set_finale(0, traffic);
    pace.set_finale(1, traffic);
    BodyPose body;
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    truck.observe(body, 0, 0);
    pace.observe(body, 0, 0);
    expect(truck.axes(body, 0, 1).throttle_q16 == one && pace.axes(body, 0, 1).throttle_q16 == one, "Convoy must accelerate both physical actors from rest");
    expect(truck.axes(body, 70 * one, 1).throttle_q16 == 0xccc && truck.axes(body, 90 * one, 1).brake_q16 == 0,
        "Finale must retain source minimum coasting throttle through inclusive speed90");
    expect(truck.axes(body, 90 * one + 1, 1).brake_q16 == one, "Finale must apply the source speed90 emergency-brake threshold");
    traffic.truck_forward_gap_q16 = 0xeffff;
    expect(truck.axes(body, 40 * one, 1).brake_q16 == one && pace.axes(body, 40 * one, 1).throttle_q16 > 0, "Truck must brake at the exact source short-gap boundary");
    traffic.truck_forward_gap_q16 = 0xf0000;
    expect(truck.axes(body, 40 * one, 1).brake_q16 == 0, "Truck must release its gap brake at exactly15units");
    traffic.truck_forward_gap_q16 = 45 * one;
    expect(pace.axes(body, 40 * one, 1).brake_q16 == 0, "Pace car must keep moving at exactly45units");
    ++traffic.truck_forward_gap_q16;
    expect(pace.axes(body, 40 * one, 1).brake_q16 == one, "Pace car must wait when its lead exceeds45units");
    traffic.truck_forward_gap_q16 = 0;
    traffic.truck_ahead_actor = 0;
    expect(truck.axes(body, 40 * one, 1).brake_q16 == 0, "Truck short-gap brake must inspect the actual source ahead actor");
}

void reference(const char* root) {
    const AssetStore store(root);
    const auto archive = store.open_archive("data/champfin.bfp");
    const auto credits = archive.member(10);
    ChampionshipFinaleSequence titles(credits.bytes());
    titles.advance(8'080'000'000);
    expect(titles.credits() == std::vector<ChampionshipFinaleCredit>{{"CREDITS", true}, {"IN ALPHABETICAL ORDER", false}},
        "Canonical CHAMPFIN first visible page must retain the original names and heading");
    bool commentary = false, music = false, qa = false, loop = false;
    for (int page = 0; page < 30; ++page) {
        titles.advance(4'080'000'000);
        for (const auto& line : titles.credits()) {
            commentary |= line.text == "ENGLISH - TIFF NEEDELL";
            music |= line.text == "JONATHAN COLLING AT OCTAGON MUSIC";
            qa |= line.text == "NICK THOMAS";
            loop |= page > 15 && line.text == "IN ALPHABETICAL ORDER";
        }
    }
    expect(commentary && music && qa && loop, "All original credit pages must remain accessible and loop without truncation");
    auto setup = resolve_setup({.track_id = 1, .car_id = 0, .detail = 3, .championship_meeting = 11});
    const auto resources = load_scene_resources(store, setup);
    const auto data = load_qualifying_data(store, setup, 1, "PLAYER", false, false, 23);
    const auto starts = championship_finale_starts(resources);
    expect(starts[0].source_index == 4 && starts[1].source_index == 1 && starts[0].xz == resources.grid[4].first_xz && starts[1].xz == resources.grid[1].first_xz
               && starts[0].xz != race_grid_start(resources.grid, 4).xz,
        "Finale must use source GRID first endpoints rather than racing-grid midpoint offsets");
    SourceRandom random(1234);
    SurfaceRoughness roughness;
    ChampionshipFinale finale(resources, data, credits.bytes(), {0, 3, 7}, random, roughness, 73);
    const auto truck = finale.actor(0).body.position, pace = finale.actor(1).body.position;
    const auto initial = finale.frame();
    expect(initial.teams == std::array<std::uint32_t, 3>{0, 3, 7} && initial.camera_phase == 0 && initial.camera_cut == 0 && initial.credit_page == 0,
        "Finale must preserve literal standings-team indices and initialize its orbit camera without an authored-camera cut");
    finale.advance(8'080'000'000);
    const auto frame = finale.frame();
    expect(finale.actor(0).body.position != truck && finale.actor(1).body.position != pace && finale.actor(0).speed_q16 > 0 && finale.actor(1).speed_q16 > 0,
        "Canonical Silverstone convoy must move both source physical actors");
    expect(frame.camera_phase == 2 && frame.credit_page == 2 && !frame.credits.empty() && finale.can_exit(), "Physical finale must publish the same source camera/credit timeline");
    expect(frame.camera_cut >> 32 == 2 && std::uint32_t(frame.camera_cut) < resources.cameras.size(),
        "Finale render history must identify both its source phase and actual authored camera selection");
    expect(frame.pace.wheel_angles != std::array<std::int16_t, 4>{} && frame.transporter.wheel_angles != std::array<std::int16_t, 4>{},
        "Moving convoy wheels must use actual integrated physical wheel speeds");
    for (const auto value : frame.camera_world_to_view) {
        expect(std::isfinite(value), "Source close camera must produce a finite native view matrix");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            sequence();
            controls();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("usage: test-championship-finale [reference-root]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
