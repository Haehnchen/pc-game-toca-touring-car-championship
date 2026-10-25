#include "assets/store.hpp"
#include "audio/scrape.hpp"
#include "core/random.hpp"

#include <cmath>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto value : pcm) {
        expect(std::isfinite(value), "Scrape mixer output must stay finite");
        result += double(value) * value;
    }
    return result;
}

std::array<double, 2> channel_energy(const std::vector<float>& pcm) {
    std::array<double, 2> result{};
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        expect(std::isfinite(pcm[index]), "Panned scrape mixer output must stay finite");
        result[index % 2] += double(pcm[index]) * pcm[index];
    }
    return result;
}

void lifecycle(toca::audio::Service& service, toca::audio::Scrapes& scrape) {
    toca::SourceRandom random(4250), expected(4250);
    scrape.update({}, 65536, {}, random);
    expect(!scrape.playing() && random.next() == expected.next(), "Inactive scrape must neither play nor consume random values");
    scrape.start();
    scrape.update({}, 65536, {}, random);
    expected.next();
    expect(scrape.playing() && scrape.played_count() == 1 && scrape.volume() == 32 && energy(service.offline_mix(1024)) > 0,
        "Actual source scrape must loop at half the local View2 gain");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Master mute must silence the active scrape");
    service.apply({});
    scrape.update({}, 65536, {}, random);
    expected.next();
    expect(scrape.played_count() == 2 && service.active_voices() == 1, "409c30 must release and restart its local handle without accumulating voices");
    scrape.update({}, 0, {}, random);
    service.offline_mix(1024);
    expect(!scrape.playing() && service.active_voices() == 0 && energy(service.offline_mix(1024)) == 0, "Leaving the wall must stop its loop immediately");
    scrape.update({41 * 65536, 0, 0}, 65536, {}, random);
    expected.next();
    expect(!scrape.playing() && random.next() == expected.next(), "A distant scrape still consumes the original discarded event10 draw");
    scrape.update({}, 65536, {}, random);
    scrape.stop();
    expect(service.active_voices() == 0, "Pause and exit must release the scrape voice");
    scrape.start();
    expect(!scrape.playing(), "Resume must await a fresh contact");
    scrape.stop();
}

void synthetic() {
    toca::audio::Service service({}, true);
    std::array<float, 128> pcm{};
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = .2f * float(std::sin(double(i) * .2));
    }
    service.load_pcm(50, pcm, 1, 44100);
    toca::audio::Scrapes scrape(service, {50, 0xa666});
    lifecycle(service, scrape);
    for (int voice = 0; voice < 12; ++voice) {
        service.play(50, 0);
    }
    toca::SourceRandom random(4), expected(4);
    scrape.start();
    scrape.update({}, 65536, {}, random);
    expected.next();
    expect(!scrape.playing() && service.active_voices() == 12 && random.next() == expected.next(), "Voice exhaustion must preserve foreign playback and source random order");
    scrape.stop();
    expect(service.active_voices() == 12, "Stopping scrape must not release foreign voices");
}

void spatial() {
    using namespace toca::audio;
    Service service({}, true);
    std::array<float, 4096> pcm;
    pcm.fill(.25f);
    service.load_pcm(50, pcm, 1, 44100);
    Scrapes scrape(service, {50, 0xa666});
    toca::SourceRandom random(50), expected(50);
    scrape.start();
    SpatialListener listener;
    constexpr std::array<std::int32_t, 3> position{0, 0, 4 * 65536};
    scrape.update(position, 65536, listener, random);
    expected.next();
    const auto first = channel_energy(service.offline_mix(256));
    expect(scrape.pan() == source_spatial(position, listener).pan && first[0] > first[1], "The first wall-scrape block must already use the original source-relative pan");
    listener.inverse_heading = 2048;
    scrape.update(position, 65536, listener, random);
    expected.next();
    const auto reversed = channel_energy(service.offline_mix(256));
    expect(reversed[1] > reversed[0] && scrape.played_count() == 2 && service.active_voices() == 1,
        "Turning the listener must reverse scrape pan through the unchanged stop/restart handle lifecycle");
    listener.stereo = false;
    scrape.update(position, 65536, listener, random);
    expected.next();
    const auto mono = channel_energy(service.offline_mix(256));
    expect(scrape.pan() == 64 && mono[0] > 0 && mono[0] == mono[1], "Original mono must start the wall loop at centered pan64");
    scrape.update({41 * 65536, 0, 0}, 65536, listener, random);
    expected.next();
    scrape.update({}, 0, listener, random);
    expect(!scrape.playing() && random.next() == expected.next(), "Spatial scrape must retain exact event10 RNG and zero-strength stop semantics");
    scrape.stop();
}

void pair_contacts() {
    using namespace toca::audio;
    Service service({}, true);
    std::array<float, 4096> pcm;
    pcm.fill(.25f);
    service.load_pcm(50, pcm, 1, 44100);
    Scrapes scrape(service, {50, 0xa666});
    toca::SourceRandom random(428), expected(428);
    scrape.start();
    const std::array<std::int32_t, 3> position{0, 0, 4 * 65536};
    scrape.pair_event(0, 1, 10, position, 65536, {}, random);
    expected.next();
    const auto first = channel_energy(service.offline_mix(256));
    expect(scrape.pair_handle(1, 0) && service.active_voices() == 1 && first[0] > first[1], "Car scrape must allocate a symmetric pair mapping and pan its first PCM block");
    SpatialListener reverse;
    reverse.inverse_heading = 2048;
    scrape.pair_event(0, 1, 11, position, 65536, reverse, random);
    const auto second = channel_energy(service.offline_mix(256));
    expect(scrape.played_count() == 1 && second[1] > second[0], "Continued contact must update pan without restarting or consuming random");
    scrape.pair_event(0, 1, 11, {41 * 65536, 0, 0}, 65536, reverse, random);
    expect(service.active_voices() == 1, "An inaudible update retains its already-playing source handle");
    scrape.pair_event(0, 1, 12, {}, 0, {}, random);
    expect(scrape.pair_handle(0, 1) && service.active_voices() == 0, "Separation stops playback while retaining the pair-slot mapping");
    scrape.pair_event(0, 1, 11, position, 65536, {}, random);
    expect(service.active_voices() == 0, "Event11 cannot restart a stopped mapped handle");
    scrape.reset_pair(0, 1);
    scrape.pair_event(0, 1, 10, position, 65536, {}, random);
    expected.next();
    scrape.stop();
    scrape.start();
    expect(scrape.pair_handle(0, 1), "Pause must preserve the source pair handle matrix");
    scrape.pair_event(0, 1, 11, position, 65536, {}, random);
    expect(service.active_voices() == 0, "Resume continuation keeps a stopped pair silent until recontact");
    scrape.reset_pairs();
    for (std::uint32_t other = 1; other <= 3; ++other) {
        scrape.pair_event(0, other, 10, position, 65536, {}, random);
        expected.next();
    }
    scrape.update(position, 65536, {}, random);
    expected.next();
    expect(service.active_voices() == 4 && scrape.playing(), "Three car scrapes and the wall occupy the four shared source handles");
    scrape.pair_event(0, 4, 10, position, 65536, {}, random);
    expect(!scrape.pair_handle(0, 4) && service.active_voices() == 4, "Exhausting the four source handles must skip allocation and the event10 random draw");
    scrape.update({}, 0, {}, random);
    scrape.pair_event(0, 4, 10, position, 65536, {}, random);
    expected.next();
    expect(scrape.pair_handle(0, 4) && service.active_voices() == 4, "A released wall slot must be reusable by the next car contact");
    scrape.reset_pair(0, 4);
    expect(service.active_voices() == 4, "First-impact matrix reset must not implicitly stop a shared handle");
    expect(random.next() == expected.next(), "Car/wall scrape allocation, continuation, pause and stop preserve source RNG chronology");
    scrape.stop();
    expect(service.active_voices() == 0, "Scene stop releases every shared scrape voice");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            spatial();
            pair_contacts();
        } else if (argc == 2) {
            toca::audio::Service service({}, true);
            const toca::AssetStore assets(argv[1]);
            toca::audio::Scrapes scrape(service, assets, {});
            lifecycle(service, scrape);
        } else {
            throw std::runtime_error("Usage: toca-scrape-audio-tests [canonical-root]");
        }
        std::cout << "PASS: original shared car/wall scrape handles, pause, spatial controls, random stream and muted PCM\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
