#include "scene/ground_scrape.hpp"
#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace toca::scene;
using namespace toca::simulation::fixed;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

GroundScrapeInput fixture(bool inverted = false) {
    GroundScrapeInput input;
    input.tyre_contact = false;
    input.body.position = {10 * one, 2 * one, 20 * one};
    input.body.basis = {{{one, 0, 0}, {0, inverted ? -one : one, 0}, {0, 0, inverted ? -one : one}}};
    input.velocity = {one, 0, 0};
    const auto half = car_half_dimensions(0);
    for (unsigned i = 0; i < 8; ++i) {
        const Vector corner{i % 4 < 2 ? half[0] : -half[0], i < 4 ? -half[1] : half[1], i % 2 ? -half[2] : half[2]};
        input.body.contacts[i] = add(input.body.position, toca::simulation::source_transform::world(corner, input.body.basis));
    }
    input.active.fill(true);
    return input;
}

void gates() {
    auto input = fixture();
    expect(ground_scrape(input).count == 8, "All eight active dry asphalt body contacts may produce original ground requests");
    input.tyre_contact = true;
    expect(ground_scrape(input).count == 0, "Ordinary tyre-supported driving must not invent body sparks");
    input.tyre_contact = false;
    input.actor_state = 1;
    expect(ground_scrape(input).count == 0, "The nonzero source actor-state path must not emit ground sparks");
    input.actor_state = 0;
    input.rain = true;
    expect(ground_scrape(input).count == 0, "The original rain branch must suppress dry body sparks");
    input.rain = false;
    input.snow = true;
    expect(ground_scrape(input).count == 0, "The original snow branch must suppress dry body sparks");
    input.snow = false;
    input.visible = false;
    expect(ground_scrape(input).count == 0, "An invisible source root must not reserve spark requests");
    input.visible = true;
    input.active.fill(false);
    expect(ground_scrape(input).count == 0, "Airborne bounds must not fabricate touching contacts");
    input.active.fill(true);
    input.surface_ids.fill(1);
    expect(ground_scrape(input).count == 0, "Non-asphalt contact classes must not emit metal scrape sparks");
    input.surface_ids.fill(0);
    input.velocity = {0x6666, 0, 0};
    expect(ground_scrape(input).count == 0, "Original half-excess intensity must exceed0x1999 before emission");
    input.velocity = {0x666a, 0, 0};
    expect(ground_scrape(input).count != 0, "Source sqrt quantization must open the ground gate immediately above the intensity boundary");
}

void records() {
    auto input = fixture();
    input.active.fill(false);
    input.active[1] = true;
    input.active[3] = true;
    input.active[6] = true;
    input.surface_ids[3] = 2;
    const auto result = ground_scrape(input);
    expect(result.count == 2, "Only active asphalt bounds enter the source request queue, in ascending contact order");
    const auto& first = result.requests[0];
    expect(first.position == Vector{input.body.contacts[1][0], input.body.surfaces[1].height, input.body.contacts[1][2]},
        "Bottom spark origin must retain touching contactXZ and its sampled ground height");
    expect(first.ground && first.velocity == input.velocity && first.intensity_q16 == 26221 && first.direction == Vector{-65522, 0, 0},
        "Ground request must preserve original raw velocity, normalized reverse direction, half-excess strength and mode1");
    expect(result.requests[1].position == Vector{input.body.position[0] - 0x21999, 0, input.body.position[2] + 0xcccc},
        "Upper bounds must use authored side-body coordinates projected onto the support plane");
    input.queued_sparks = 9;
    expect(ground_scrape(input).count == 1, "Existing wall requests share the original ten-request queue capacity");
    input.queued_sparks = 10;
    expect(ground_scrape(input).count == 0, "Full source request queue must not consume another ground contact");
    input.queued_sparks = 0;
    input.velocity = {one, one / 2, 0};
    expect(ground_scrape(input).requests[0].velocity == Vector{one, 0, 0}, "Separating positive normal velocity must be removed before ground emission");
    input.velocity = {one, -one / 2, 0};
    expect(ground_scrape(input).requests[0].velocity == input.velocity, "Descending normal velocity must remain in the original ground request");
    input.velocity = {4 * one, 0, 0};
    expect(ground_scrape(input).requests[0].intensity_q16 == one, "Original ground intensity must saturate at one");

    input = fixture(true);
    input.active.fill(false);
    input.active[4] = true;
    input.active[7] = true;
    const auto roof = ground_scrape(input);
    expect(roof.count == 2 && roof.requests[0].position == Vector{input.body.position[0] + 0x1999, 0, input.body.position[2] - 0x9999}
               && roof.requests[1].position == Vector{input.body.position[0] - 0x9999, 0, input.body.position[2] + 0x9999},
        "Inverted body-up angle must select original roof offsets and preserve mirrored contact sides");
}
} // namespace

int main() {
    try {
        gates();
        records();
        std::cout << "PASS: original ground-contact gates and spark request records\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
