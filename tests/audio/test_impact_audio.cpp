#include "assets/store.hpp"
#include "audio/contact_state.hpp"
#include "audio/impact.hpp"
#include "core/random.hpp"

#include <cmath>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double energy(const std::vector<float>& pcm) {
    double result = 0;
    for (const auto sample : pcm) {
        expect(std::isfinite(sample), "Impact PCM must remain finite");
        result += double(sample) * sample;
    }
    return result;
}

std::array<double, 2> channel_energy(const std::vector<float>& pcm) {
    std::array<double, 2> result{};
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        expect(std::isfinite(pcm[index]), "Panned impact PCM must remain finite");
        result[index % 2] += double(pcm[index]) * pcm[index];
    }
    return result;
}

void miles_pan() {
    using namespace toca::audio;
    // Actual canonical2000def0/2000de70/200166b8 machine-code results for
    // pans0..63. Pans64..127 mirror these with the opposite channel muted.
    constexpr std::array<std::int32_t, 64> decibels{-10000, -3598, -2996, -2644, -2394, -2200, -2042, -1908, -1792, -1690, -1598, -1515, -1440, -1370, -1306, -1246, -1190, -1137,
        -1088, -1041, -996, -954, -913, -875, -838, -802, -768, -735, -704, -673, -644, -615, -588, -561, -535, -510, -486, -462, -439, -416, -394, -373, -352, -331, -311, -292,
        -273, -254, -236, -218, -200, -183, -166, -150, -133, -117, -102, -86, -71, -56, -42, -28, -13, 0};
    Service service({}, true);
    std::array<float, 4096> pcm;
    pcm.fill(.25f);
    service.load_pcm(123, pcm, 1, 44100);
    for (std::uint32_t pan = 0; pan <= 127; ++pan) {
        const auto index = pan < 64 ? pan : 127 - pan;
        const auto far_gain = index == 0 ? 0.f : float(std::pow(10.0, double(decibels[index]) / 2000));
        const auto gains = original_pan_gains(pan);
        expect(gains.left == (pan < 64 ? 1.f : far_gain) && gains.right == (pan < 64 ? far_gain : 1.f),
            "Miles pan must match all128 original hundredth-dB results, including centers63/64 and silent endpoints");
        const auto voice = service.play_pan(123, 1, .5f, pan);
        const auto output = channel_energy(service.offline_mix(256));
        const auto near = output[pan < 64 ? 0 : 1], far = output[pan < 64 ? 1 : 0];
        expect(near > 0 && std::abs(far / near - double(far_gain) * far_gain) < .00001,
            "The first offline playback block must already apply original pan; no initially centered block may leak");
        service.stop(voice);
    }
    const auto reused = service.play(123);
    const auto centered = channel_energy(service.offline_mix(256));
    expect(centered[0] > 0 && centered[0] == centered[1], "An ordinary playback must reset pan when it reuses a spatial voice");
    service.stop(reused);
}

toca::audio::ImpactData fixture(toca::audio::Service& service) {
    toca::audio::ImpactData data;
    constexpr std::array<std::uint32_t, 24> members{25, 26, 27, 28, 29, 30, 33, 34, 35, 36, 45, 65, 66, 31, 32, 37, 38, 46, 51, 52, 53, 54, 55, 64};
    std::array<float, 4410> pcm;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .2f * float(std::sin(double(index) * .1));
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        data.samples[index] = 0x300 + members[index];
        service.load_pcm(data.samples[index], pcm, 1, 44100);
    }
    return data;
}

void synthetic() {
    using namespace toca::audio;
    Service service({}, true);
    const auto data = fixture(service);
    Impact impact(service, data);
    ImpactEvent event{{}, 1, 0xe666, true};
    const SpatialListener listener{};
    expect(service.active_voices() == 0, "Loading impact samples must reserve no voices");
    impact.update(event, listener);
    impact.start();
    expect(impact.played_count() == 0 && service.active_voices() == 0, "Start/resume must not replay prior impact events");
    event.strength_q16 = 4000;
    impact.update(event, listener);
    expect(impact.played_count() == 0, "Subthreshold source impact must stay silent");
    event.strength_q16 = 0xe666;
    event.position_q16[0] = 41 * 65536;
    impact.update(event, listener);
    expect(impact.played_count() == 0, "Impact outside source40-unit cutoff must stay silent");
    event.position_q16 = {};
    impact.update(event, listener);
    expect(impact.played_count() == 1 && impact.last_sample() == 29 && impact.last_volume() == 64, "Original full-strength severity or local camera gain changed");
    expect(energy(service.offline_mix(1024)) > 0, "Impact event must reach real SDL_mixer PCM");
    service.apply({.muted = true});
    expect(energy(service.offline_mix(1024)) == 0, "Master mute must silence impact PCM");
    service.apply({});
    service.offline_mix(10000);
    expect(service.active_voices() == 0, "Impact must play once rather than loop");
    const auto foreign = service.play(data.samples[0], 0);
    impact.stop();
    expect(service.playing(foreign), "Impact stop must preserve a foreign playback reusing its finished slot");
    service.stop(foreign);
    impact.start();
    for (int index = 0; index < 13; ++index) {
        impact.update(event, listener);
    }
    expect(service.active_voices() == 12 && impact.played_count() == 13, "Impact must honor shared12-voice exhaustion without stealing");
    impact.stop();
    expect(service.active_voices() == 0, "Focus/exit stop must release owned impact voices");
    impact.start();
    expect(service.active_voices() == 0, "Resume must await a new contact event");
    event.strength_q16 = 20000;
    const auto soft = impact_control(event, listener, data);
    expect(soft.sample == 0x300 + 35, "Original soft-impact severity band changed");
    event.local_player = false;
    expect(impact_control(event, listener, data).volume == 44, "Original remote camera gain changed");
    event.local_player = true;
    event.kind = 6;
    event.strength_q16 = -65536;
    expect(impact_control(event, listener, data).volume == 0, "A separating cone contact must retain the source's silent signed strength");
    event.strength_q16 = 32768;
    for (const auto kind : {6u, 0x12u, 0x13u, 0x14u, 0x15u}) {
        event.kind = kind;
        const auto control = impact_control(event, listener, data);
        expect(control.sample
                       == 0x300
                              + (kind == 6       ? 45u
                                  : kind == 0x15 ? 66u
                                                 : 65u)
                   && control.volume == 32,
            "Prop impacts must retain original samples and unamplified strength");
        impact.update(event, listener);
        expect(energy(service.offline_mix(4410)) > 0, "Prop contact must reach the actual offline mix");
    }
    impact.stop();
    expect(service.active_voices() == 0, "Prop impact voices must stop with the scene");
    impact.start();
    toca::SourceRandom random(419), expected(419);
    for (const auto kind : {2u, 3u, 4u, 3u, 4u}) {
        const auto member = kind == 2 ? 46 : (kind == 3 ? 31 : 37) + (expected.next() & 1);
        impact.update({{}, kind, 32768, true}, listener, &random);
        expect(impact.last_sample() == member && impact.last_volume() == 32 && energy(service.offline_mix(8192)) > 0,
            "Damage sounds must use original samples, unamplified strength and exactly one shared RNG draw for kinds3/4");
    }
    for (unsigned contact = 0; contact < 12; ++contact) {
        const auto member = 51 + expected.next() % 5;
        impact.update({{}, 5, 0x3333, true}, listener, &random);
        expect(impact.last_sample() == member && impact.last_volume() == 12 && energy(service.offline_mix(8192)) > 0,
            "Landing must play a source ROLL sample at unamplified contact gain with one shared random draw");
    }
    impact.update({{41 * 65536, 0, 0}, 5, 65536, true}, listener, &random);
    impact.update({{}, 5, 0xf5b, true}, listener, &random);
    impact.update({{41 * 65536, 0, 0}, 3, 65536, true}, listener, &random);
    impact.update({{}, 4, 0xf5b, true}, listener, &random);
    expect(random.next() == expected.next(), "Silent or distant damage must not consume sound-variation RNG");
    for (int i = 0; i < 12; ++i) {
        service.play(data.samples[0], 0);
    }
    const auto played = impact.played_count();
    impact.update({{}, 3, 65536, true}, listener, &random);
    expected.next();
    impact.update({{}, 5, 0x3333, true}, listener, &random);
    expected.next();
    expect(impact.played_count() == played && random.next() == expected.next(), "A full source voice pool must still consume the selected damage variation exactly once");
    impact.stop();
}

void exhaust() {
    using namespace toca::audio;
    Service service({}, true);
    const auto data = fixture(service);
    Impact impact(service, data);
    impact.start();
    toca::SourceRandom random(71), expected(71);
    impact.update({{}, 17, 65536, true}, {}, &random);
    expect(impact.last_sample() == 64 && impact.last_volume() == 64 && energy(service.offline_mix(8192)) > 0,
        "420be0 event17 must play BANG64 with amplified strength and local camera gain");
    expect(random.next() == expected.next(), "Exhaust sample selection must not consume source RNG");
    const auto remote = impact_control({{0, 0, 4 * 65536}, 17, 65536, false}, {}, data);
    expect(remote.sample == 0x340 && remote.volume == 44 && remote.pan != 64, "Exhaust must retain original spatial pan and remote camera gain");
    impact.update({{41 * 65536, 0, 0}, 17, 65536, true}, {});
    expect(impact.played_count() == 1, "Out-of-range exhaust must stay silent");
    impact.stop();
}

void exhaust_views() {
    using namespace toca::audio;
    Service service({}, true);
    const auto data = fixture(service);
    Impact impact(service, data);
    impact.start();
    const ImpactEvent event{{0, 0, 4 * 65536}, 17, 65536, true};
    std::array<SourceContactListener, 2> listeners{{{{{1000 * 65536, 0, 0}, 0, true}, 2}, {{{}, 0, true}, 2}}};
    toca::SourceRandom random(419);
    const auto initial_rng = random.state();
    impact.update_views(event, listeners, &random);
    expect(impact.played_count() == 1 && service.active_voices() == 1 && impact.last_sample() == 64 && impact.last_volume() == 64
               && impact.last_pan() == source_spatial(event.position_q16, listeners[1].spatial).pan && energy(service.offline_mix(1024)) > 0,
        "Exhaust audible only to the second view must play one spatial sample64 voice, not disappear or play once per view");
    impact.stop();
    impact.start();
    listeners[0].spatial = {};
    listeners[1].spatial.inverse_heading = 2048;
    impact.update_views(event, listeners, &random);
    expect(impact.played_count() == 2 && service.active_voices() == 1 && impact.last_pan() == source_spatial(event.position_q16, listeners[1].spatial).pan,
        "Equal-volume exhaust listeners must retain the source's later-listener pan tie break");
    impact.stop();
    impact.start();
    auto remote = event;
    remote.local_player = false;
    impact.update_views(remote, listeners, &random);
    expect(impact.played_count() == 3 && impact.last_volume() == 44, "Remote exhaust must use the original external-view gain after listener selection");
    impact.stop();
    impact.start();
    listeners[1].view = 1;
    impact.update_views(remote, listeners, &random);
    expect(impact.played_count() == 3 && service.active_voices() == 0, "Remote exhaust must respect the final source camera-mix transaction and its silent interior gain");
    listeners[0].spatial.position_q16 = listeners[1].spatial.position_q16 = {1000 * 65536, 0, 0};
    impact.update_views(event, listeners, &random);
    expect(impact.played_count() == 3 && service.active_voices() == 0 && random.state() == initial_rng,
        "Rejected or admitted exhaust events across both views must consume no random variation draws");
}

void spatial_contacts() {
    using namespace toca::audio;
    Service service({}, true);
    Impact impact(service, fixture(service));
    impact.start();
    SpatialListener listener;
    ImpactEvent event{{0, 0, 4 * 65536}, 1, 65536, true};
    impact.update(event, listener);
    const auto first = channel_energy(service.offline_mix(1024));
    expect(impact.last_pan() == source_spatial(event.position_q16, listener).pan && first[0] != first[1],
        "Actual impact playback must use424070/424ac0 source direction rather than centered stereo");
    impact.stop();
    impact.start();
    listener.inverse_heading = 2048;
    impact.update(event, listener);
    const auto reversed = channel_energy(service.offline_mix(1024));
    expect((first[0] > first[1]) != (reversed[0] > reversed[1]), "Turning the listener180degrees must reverse the contact's dominant channel");
    impact.stop();
    impact.start();
    listener.stereo = false;
    impact.update(event, listener);
    const auto mono = channel_energy(service.offline_mix(1024));
    expect(impact.last_pan() == 64 && mono[0] > 0 && mono[0] == mono[1], "Original mono mode must force source pan64 before playback");
    impact.stop();
}

void source_contact_gates() {
    using namespace toca::audio;
    toca::SourceRandom random(421), expected(421);
    SourceCameraMix camera{1, 0};
    const auto raw = source_contact_listener({std::bit_cast<std::uint32_t>(.5f), 0, std::bit_cast<std::uint32_t>(-.5f)}, std::bit_cast<std::uint32_t>(1.f), 1024, true, 2, false);
    expect(raw.spatial.position_q16 == SpatialVector{} && raw.spatial.inverse_heading == 1024,
        "424070 camera float conversion must retain source nearest-even rounding and raw inverse heading");
    std::array listeners{SourceContactListener{{}, 1, false}, SourceContactListener{{}, 2, true}};
    const auto impact = source_impact_event({{}, 3, 65536}, listeners, camera, {}, &random);
    expect(impact.sample == std::int32_t(31 + (expected.next() & 1)) && impact.volume == 59 && impact.spatial && camera.primary == 0 && camera.secondary == 2,
        "The strongest-listener source query must select a sample before the final listener's showcase gain");
    const auto retained = camera;
    const auto disabled = source_impact_event({{}, 4, 65536, true, false}, listeners, camera, {}, &random);
    expect(!disabled.drew && camera.primary == retained.primary && camera.secondary == retained.secondary && random.state() == expected.state(),
        "Disabled424860 must preserve camera context and the shared stream");
    listeners = {SourceContactListener{{}, 2, false}, SourceContactListener{{}, 1, false}};
    const auto zero_gain = source_impact_event({{}, 4, 65536, false}, listeners, camera, {}, &random);
    expect(zero_gain.sample == std::int32_t(37 + (expected.next() & 1)) && zero_gain.volume == 0 && zero_gain.drew,
        "Remote internal camera gain0 rejects playback only after the original damage variant draw");
    const auto distant = source_impact_event({{50 * 65536, 0, 0}, 5, 65536}, listeners, camera, {}, &random);
    expect(!distant.spatial && distant.sample == -1 && !distant.drew && camera.primary == 1 && random.state() == expected.state(),
        "Rejected spatial damage still refreshes source camera mix but does not select a variation");
    const auto distance_override = source_impact_event({{50 * 65536, 0, 0}, 27, 65536}, listeners, camera, {}, &random);
    expect(distance_override.spatial && distance_override.volume > 0 && !distance_override.drew,
        "Event27 halves quantized listener distance before the source cutoff and fade without drawing");
    const auto thunder = source_impact_event({{500 * 65536, 0, 0}, 25, 65536}, {}, camera, {}, &random);
    expect(thunder.sample == std::int32_t(76 + (expected.next() & 1)) && thunder.volume == 6553600 && thunder.pan == 64 && camera.primary == 1,
        "Thunder bypasses every spatial/camera query, selects76/77 and preserves the raw unshifted requested volume");
    const auto invalid = source_scrape_event({{}, -1, 65536}, listeners, camera, {}, random);
    expect(!invalid.drew && random.state() == expected.state(), "Invalid scrape handles must reject before the discarded draw");
    const auto scrape = source_scrape_event({{50 * 65536, 0, 0}, 0, 65536, 10, true, true}, listeners, camera, {}, random);
    expected.next();
    expect(scrape.drew && scrape.sample == 50 && !scrape.spatial && scrape.action == SourceScrapeAction::ignore && random.state() == expected.state(),
        "Every valid event10 draws even for distant already-playing scrapes");
    const auto update = source_scrape_event({{}, 0, 65536, 11, true, true}, listeners, camera, {}, random);
    const auto stop = source_scrape_event({{}, 0, 0, 12, true, true}, listeners, camera, {}, random);
    expect(update.action == SourceScrapeAction::update && update.volume == 50 && stop.action == SourceScrapeAction::stop && random.state() == expected.state(),
        "Mapped scrape continuation and stop must use their actual actions without extra random draws");
}

void reference(const char* root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    Service service({}, true);
    Impact impact(service, store, {});
    impact.start();
    impact.update({{}, 1, 0xe666, true}, {});
    expect(impact.last_sample() == 29 && impact.played_count() == 1 && energy(service.offline_mix(8192)) > 0, "Canonical original impact WAV did not reach SDL_mixer PCM");
    impact.stop();
    service.offline_mix(1024);
    expect(energy(service.offline_mix(1024)) == 0, "Stopped canonical impact must stay silent");
    impact.start();
    impact.update({{}, 17, 65536, true}, {});
    expect(impact.last_sample() == 64 && energy(service.offline_mix(8192)) > 0, "Original exhaust BANG64 must decode and reach SDL_mixer");
    impact.stop();
    impact.start();
    for (const auto kind : {6u, 0x12u, 0x15u}) {
        impact.update({{}, kind, 65536, true}, {});
        expect(impact.last_sample()
                       == (kind == 6      ? 45u
                           : kind == 0x15 ? 66u
                                          : 65u)
                   && energy(service.offline_mix(8192)) > 0,
            "Canonical cone/post/sign WAV did not reach SDL_mixer PCM");
    }
    impact.stop();
    service.offline_mix(8192);
    impact.start();
    toca::SourceRandom random(19), expected(19);
    for (const auto kind : {2u, 3u, 4u}) {
        const auto member = kind == 2 ? 46 : (kind == 3 ? 31 : 37) + (expected.next() & 1);
        impact.update({{}, kind, 65536, true}, {}, &random);
        expect(impact.last_sample() == member && energy(service.offline_mix(8192)) > 0, "Canonical damage WAV must reach the real muted offline audio path");
    }
    impact.stop();
    service.offline_mix(8192);
    impact.start();
    std::array<bool, 5> landings{};
    for (unsigned contact = 0; contact < 24; ++contact) {
        const auto variation = expected.next() % 5;
        impact.update({{}, 5, 0x3333, true}, {}, &random);
        expect(impact.last_sample() == 51 + variation && energy(service.offline_mix(8192)) > 0, "Original ROLL landing samples must decode and reach the actual offline mixer");
        landings[variation] = true;
    }
    for (const auto played : landings) {
        expect(played, "Canonical landing check must exercise all five original ROLL samples");
    }
    impact.stop();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            miles_pan();
            synthetic();
            exhaust();
            exhaust_views();
            spatial_contacts();
            source_contact_gates();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-impact-audio-tests [canonical-root]");
        }
        std::cout << "PASS: source impact selection, finite PCM and owned cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
