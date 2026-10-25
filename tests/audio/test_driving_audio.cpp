#include "assets/store.hpp"
#include "audio/driving.hpp"
#include "core/random.hpp"

#include <algorithm>
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
    for (const auto value : pcm) {
        expect(std::isfinite(value), "Driving PCM is not finite");
        result += double(value) * value;
    }
    return result;
}

void queued_skid_dispatch() {
    using namespace toca::audio;
    toca::SourceRandom random(0x12345678), expected(0x12345678);
    std::array<SourceSkidActor, 3> actors;
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        actors[actor].identity = std::uint8_t(actor);
    }
    actors[0].human = true;
    actors[0].events = {7, 7, 8, 9};
    actors[0].state.reset = 1;
    actors[1].events = {7, 8, 9, -1};
    actors[1].state.surface = 2;
    actors[2].events = {7, 8, 9, 9};
    actors[2].state.surface = 3;
    actors[2].state.reset = 1;
    const auto first = std::int32_t(39 + expected.next() % 6);
    const auto second = std::int32_t(39 + expected.next() % 6);
    const auto grass_start = std::int32_t(47 + (expected.next() & 1));
    const auto grass_continue = std::int32_t(47 + (expected.next() & 1));
    const auto events = dispatch_source_skids(actors, true, random);
    expect(events.size() == 11 && events[0].actor == 0 && events[0].wheel == 0 && events[3].wheel == 3 && events[4].actor == 1 && events[7].actor == 2,
        "44c540 must consume every queued wheel event in ascending fleet and wheel order");
    expect(events[0].decision.sample == first && events[0].decision.restart && events[0].decision.fade && events[1].decision.sample == -1
               && events[1].decision.action == SourceSkidAction::ignore && events[1].state.mask == 3 && events[2].decision.sample == second,
        "424d50 starts the channel once, preserves added wheel bits and selects a fresh continuation sample");
    expect(events[4].decision.sample == grass_start && events[5].decision.sample == grass_continue && events[4].decision.action == SourceSkidAction::ignore
               && events[5].decision.action == SourceSkidAction::ignore && random.state() == expected.state(),
        "Remote grass must consume and select source variants before ownership rejection");
    expect(events[7].decision.sample == 49 && events[7].decision.action == SourceSkidAction::ignore && events[7].state.reset == 0,
        "Remote gravel selects49 without a draw and clears an accepted restart before ownership rejection");
    SourceSkidState stopping{0, 3, 1};
    const auto seed = random.state();
    const auto partial = source_skid_event(stopping, 9, 0, true, true, random);
    const auto last = source_skid_event(stopping, 9, 1, true, true, random);
    expect(partial.action == SourceSkidAction::ignore && last.action == SourceSkidAction::stop && stopping.mask == 0 && stopping.reset == 1 && random.state() == seed,
        "Skid stop events clear their own wheel bits and retain the pending surface restart without drawing");
    const auto restarted = source_skid_event(stopping, 8, 2, true, true, random);
    expect(restarted.restart && restarted.fade && restarted.action == SourceSkidAction::update && stopping.mask == 4 && stopping.reset == 0,
        "A pending surface restart survives stopped wheels until the next accepted skid event");
    SourceSkidState curb{1, 0, 1};
    const auto curb_seed = random.state();
    const auto ignored = source_skid_event(curb, 7, 0, true, true, random);
    expect(ignored.sample == -1 && ignored.restart && curb.mask == 1 && curb.reset == 0 && random.state() == curb_seed,
        "Curb skid events update channel state before returning without a sample");
    SourceSkidState disabled{0, 2, 1};
    const auto muted = source_skid_event(disabled, 8, 0, true, false, random);
    expect(muted.action == SourceSkidAction::ignore && disabled.mask == 2 && disabled.reset == 1 && random.state() == curb_seed,
        "The original audio initialization gate preserves skid state and RNG; output muting is a separate service setting");
}

void wheel_queue_producer() {
    using namespace toca::audio;
    std::array<SourceWheelQueueActor, 3> actors;
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        actors[actor].identity = std::uint8_t(actor);
    }
    actors[0].human = true; //The hidden local actor remains eligible.
    actors[0].input.longitudinal_slip = {0x4ccc, 0x4ccd, 65536, -65536};
    actors[0].input.airborne = {0, 0, 1, 2};
    actors[1].visibility = 2; //Any view bit admits the full-body rival.
    actors[1].input.lateral_slip[0] = 65536;
    actors[2].state.current = {1, 0, 1, 0};
    actors[2].state.previous = {0, 1, 1, 0};
    actors[2].queue.strengths.fill(123);
    const auto order = prepare_source_wheel_queues(actors);
    expect(order == std::vector<std::uint32_t>{0, 1} && actors[0].queue.events == std::array<std::int16_t, 4>{9, 7, 9, 7}
               && actors[0].queue.strengths == std::array<std::int32_t, 4>{0x8000, 6, 0x8000, 0x10000},
        "446b50 admits the human and visible rival in order;44b690 preserves threshold, strength and exact airborne-byte semantics");
    expect(actors[2].queue.events == std::array<std::int16_t, 4>{-1, -1, -1, -1} && actors[2].queue.strengths[0] == 123 && actors[2].pending_mute == 1
               && actors[2].state.current == std::array<std::int32_t, 4>{0, 0, 0, 0} && actors[2].state.previous == std::array<std::int32_t, 4>{0, 1, 0, 0},
        "Skipped rivals retain strength and untouched previous flags while active wheels clear and request deferred channel mute");
    prepare_source_wheel_queues(actors);
    expect(actors[0].queue.events[1] == 8 && actors[0].state.previous[1] == 1 && actors[1].queue.events[0] == 8,
        "Repeated source view callbacks derive continuation queues from retained producer flags without advancing physics");
    const auto previous = actors[0].state.current;
    prepare_source_wheel_queues(actors, {.disabled = true});
    expect(actors[0].queue.events[1] == -1 && actors[0].state.current == previous && actors[0].queue.strengths[1] == 6,
        "The disabled producer still clears view-zero events while preserving current flags and strength storage");
    const auto first_view = prepare_source_wheel_queues(actors, {.local_players = 2});
    const auto first_queue = actors[0].queue.events;
    actors[1].visibility = 0;
    const auto second_view = prepare_source_wheel_queues(actors, {.view = 1, .local_players = 2});
    expect(first_view == std::vector<std::uint32_t>{0} && second_view == std::vector<std::uint32_t>{1} && actors[0].queue.events == first_queue && actors[1].queue.events[0] == 8,
        "Local split views prepare only their own actor and later views preserve the earlier queued actor despite hidden AI ownership");
    SourceSkidState channel{2, 3, 1};
    expect(settle_source_skid_mute(actors[2].pending_mute, channel) && actors[2].pending_mute == 0 && channel.mask == 0 && channel.surface == 2 && channel.reset == 1,
        "25e90 settles the deferred mask stop without clearing the separate material or force-restart state");
}

void material_state() {
    using namespace toca::audio;
    SourceSkidState external;
    SourceCameraMix camera;
    std::array<std::uint8_t, 4> wheels{};
    advance_source_material(external, wheels, 3, 2, true, true, true, camera, 2, false);
    advance_source_material(external, wheels, 2, 2, true, true, true, camera, 2, false);
    expect(external.surface == 0 && external.reset == 0 && camera.primary == 0 && camera.secondary == 1,
        "Wet external wheels must agree before replacing asphalt as the dominant material");
    toca::SourceRandom random(17), expected(17);
    const auto asphalt = source_skid_event(external, 7, 0, true, true, random);
    expect(asphalt.sample == std::int32_t(39 + expected.next() % 6), "The retained dominant asphalt must select its original tire variant before all wet wheels agree");
    advance_source_material(external, wheels, 1, 2, true, true, true, camera, 2, false);
    const auto last = advance_source_material(external, wheels, 0, 2, true, true, true, camera, 2, false);
    const auto grass = source_skid_event(external, 8, 0, true, true, random);
    expect(last.changed && last.fade && external.surface == 2 && grass.restart && grass.sample == std::int32_t(47 + (expected.next() & 1)) && random.state() == expected.state(),
        "The final agreeing wet wheel must set a retained restart and change the next real tire RNG selection");
    SourceSkidState internal;
    wheels = {};
    advance_source_material(internal, wheels, 3, 2, true, true, true, camera, 1, false);
    advance_source_material(internal, wheels, 2, 2, true, true, true, camera, 1, false);
    expect(internal.surface == 2 && internal.reset == 1 && camera.primary == 1,
        "Wet internal views retain the original rising-material policy rather than the external all-wheel requirement");
    SourceSkidState showcase;
    wheels = {};
    advance_source_material(showcase, wheels, 3, 2, true, true, true, camera, 1, true);
    advance_source_material(showcase, wheels, 2, 2, true, true, true, camera, 1, true);
    expect(showcase.surface == 0 && camera.primary == 0 && camera.secondary == 2, "Showcase must select the external mix before evaluating the wet material transition");
    const auto before = wheels;
    const auto disabled = advance_source_material(showcase, wheels, 1, 3, false, true, true, camera, 1, false);
    const auto remote = advance_source_material(showcase, wheels, 1, 3, true, false, true, camera, 1, false);
    expect(!disabled.admitted && !remote.admitted && wheels == before && showcase.reset == 0 && camera.primary == 0,
        "Original audio initialization and ownership gates must suppress material and effective-camera mutation");
    advance_source_material(showcase, wheels, 3, 1, true, true, true, camera, 3, false);
    advance_source_material(showcase, wheels, 2, 16, true, true, true, camera, 3, false);
    expect(wheels == before, "Curb and airborne events must retain each wheel's previous material");
}

void pan_pcm(const std::vector<float>& pcm, std::uint32_t pan, const char* message) {
    std::array<double, 2> channels{};
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        expect(std::isfinite(pcm[index]), "Driving stereo PCM must stay finite");
        channels[index % 2] += double(pcm[index]) * pcm[index];
    }
    const auto gains = toca::audio::original_pan_gains(pan);
    const auto first = gains.left >= gains.right ? 0u : 1u, second = 1u - first;
    const auto near = std::max(gains.left, gains.right), far = std::min(gains.left, gains.right);
    expect(channels[first] > 0 && std::abs(channels[second] / channels[first] - double(far / near) * (far / near)) < .00001, message);
}

std::size_t crossings(const std::vector<float>& pcm) {
    std::size_t result = 0;
    for (std::size_t index = 2; index < pcm.size(); index += 2) {
        if (pcm[index - 2] < 0 && pcm[index] >= 0) {
            ++result;
        }
    }
    return result;
}

toca::audio::DrivingData fixture(toca::audio::Service& service) {
    std::array<float, 100> pcm;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = .25f * float(std::sin(double(index) * 6.283185307179586 / 100));
    }
    toca::audio::DrivingData result;
    result.wind = {1024, 22050, {80, 260, {22050, 44100}, {100, 100}}};
    service.load_pcm(1024, pcm, 1, 22050);
    for (std::uint32_t index = 0; index < result.rumble_samples.size(); ++index) {
        result.rumble_samples[index] = 1025 + index;
        service.load_pcm(1025 + index, pcm, 1, 22050);
    }
    for (std::uint32_t index = 0; index < result.skid_samples.size(); ++index) {
        result.skid_samples[index] = 1040 + index;
        service.load_pcm(1040 + index, pcm, 1, 22050);
    }
    for (std::uint32_t index = 0; index < result.rolling_samples.size(); ++index) {
        result.rolling_samples[index] = 1060 + index;
        service.load_pcm(1060 + index, pcm, 1, 22050);
    }
    return result;
}

//Audio component cases prepare an explicit source queue before each sampled
//playback transaction; application scheduling owns its retained previous queue.
class QueuedDriving : public toca::audio::Driving {
    toca::audio::SourceWheelQueueActor producer_;

public:
    using toca::audio::Driving::Driving;

    void update(toca::audio::DrivingInputs input) {
        for (std::size_t wheel = 0; wheel < 4; ++wheel) {
            producer_.input.longitudinal_slip[wheel] = input.wheels[wheel].longitudinal_slip_q16;
            producer_.input.lateral_slip[wheel] = input.wheels[wheel].lateral_slip_q16;
            producer_.input.airborne[wheel] = std::uint8_t(input.wheels[wheel].airborne);
        }
        toca::audio::produce_source_wheel_queue(producer_);
        input.skid_queue = producer_.queue;
        toca::audio::Driving::update(input);
    }

    void stop() {
        toca::audio::Driving::stop();
        producer_ = {};
    }
};

void retained_queue_playback() {
    using namespace toca::audio;
    Service service({}, true);
    Driving driving(service, {}, fixture(service));
    driving.start(false);
    toca::SourceRandom random(31), expected(31);
    DrivingInputs input;
    input.random = &random;
    input.wheels[0].lateral_slip_q16 = 65536;
    driving.update(input);
    expect(!driving.skid_playing() && random.state() == expected.state(),
        "A cold native queue must remain silent despite current physical slip until a view producer prepares its events");
    SourceWheelQueueActor producer;
    producer.input.lateral_slip[0] = input.wheels[0].lateral_slip_q16;
    produce_source_wheel_queue(producer);
    input.skid_queue = producer.queue;
    input.wheels[0].lateral_slip_q16 = 0;
    input.wheels[0].position = {0, 0, 4 * 65536};
    driving.update(input);
    expect(driving.skid_playing() && driving.skid_sample() == 39 + expected.next() % 6 && driving.skid_volume() > 0
               && driving.skid_pan() == source_spatial(input.wheels[0].position, input.listener).pan && energy(service.offline_mix(512)) > 0,
        "Retained skid events use their prepared strength and current wheel position even after the physical slip ends");
    std::int32_t pending = 1;
    driving.settle_skid_mute(pending);
    expect(pending == 0 && !driving.skid_playing() && random.state() == expected.state(),
        "Deferred frame mute must stop the actual wheel voice without consuming an additional variation draw");
    driving.stop();
    Driving rival(service, {}, fixture(service));
    rival.start(false);
    input.local_player = false;
    for (auto& wheel : input.wheels) {
        wheel.surface = 2;
        wheel.rumbling = true;
        wheel.strength_q16 = 65536;
    }
    rival.update_queued_skids(input);
    expect(rival.skid_playing() && rival.skid_sample() == 39 + expected.next() % 6 && rival.rolling_sample() == 0 && rival.rumble_sample() == 0 && service.active_voices() == 1
               && random.state() == expected.state() && energy(service.offline_mix(512)) > 0,
        "Remote queued skids retain initialized asphalt material while ownership suppresses physical grass rolling and rumble callers");
    rival.stop();
}

void wet_material_playback() {
    using namespace toca::audio;
    Service service({}, true);
    for (const auto showcase : {false, true}) {
        Driving driving(service, {.weather = 3, .camera_view = showcase ? 1u : 2u}, fixture(service));
        toca::SourceRandom random(17), expected(17);
        DrivingInputs input;
        input.random = &random;
        input.showcase = showcase;
        input.wheels[0].surface = input.wheels[1].surface = 2;
        input.skid_queue.events[0] = 7;
        input.skid_queue.strengths[0] = 65536;
        driving.start();
        driving.update_contacts(input);
        driving.update_queued_skids(input);
        expect(driving.skid_sample() == 39 + expected.next() % 6 && driving.skid_playing(),
            "Real wet external/showcase playback must keep the asphalt variant while its wheels have mixed materials");
        input.wheels[2].surface = input.wheels[3].surface = 2;
        driving.update_contacts(input);
        driving.update_queued_skids(input);
        expect(driving.skid_sample() == 47 + (expected.next() & 1) && random.state() == expected.state() && driving.skid_playing() && energy(service.offline_mix(512)) > 0,
            "The completed wet material transition must restart actual grass playback using the next shared random value");
        driving.stop();
    }
}

void wheel_only_actor() {
    using namespace toca::audio;
    Service service({}, true);
    QueuedDriving driving(service, {}, fixture(service));
    DrivingInputs input;
    input.speed_integer = 180;
    driving.start(false);
    driving.update(input);
    expect(service.active_voices() == 0 && energy(service.offline_mix(512)) == 0, "An additional wheel-only actor must not reserve a silent or audible local WHIRR voice");
    auto& wheel = input.wheels[0];
    wheel.position = {0, 0, 4 * 65536};
    wheel.rumbling = true;
    wheel.strength_q16 = 32768;
    driving.update(input);
    expect(service.active_voices() == 1 && driving.rumble_volume() > 0 && energy(service.offline_mix(512)) > 0,
        "Wheel-only actors must retain real wheel rumble and its offline PCM output");
    driving.set_camera(1, true);
    driving.update(input);
    expect(service.active_voices() == 1 && driving.wind_control().volume == 0, "A motor-bank camera change must preserve the actor's wheel-only allocation policy");
    driving.stop();
    expect(service.active_voices() == 0, "Stopping a wheel-only actor must release its wheel voices");
    driving.start(false);
    driving.set_camera(3, true, true);
    expect(service.active_voices() == 0, "Focus resume and authored-camera restart must not allocate a second local WHIRR channel");
    driving.stop();
    QueuedDriving local_wheels(service, {}, fixture(service));
    toca::SourceRandom random(17), expected(17);
    input = {};
    input.random = &random;
    for (auto& contact : input.wheels) {
        contact.surface = 2;
    }
    input.wheels[0].lateral_slip_q16 = 65536;
    local_wheels.start(false);
    local_wheels.update(input);
    expect(local_wheels.skid_sample() == 47 + (expected.next() & 1) && local_wheels.skid_volume() > 0 && energy(service.offline_mix(512)) > 0,
        "A local actor without WHIRR must retain human grass skid playback and its original random sample");
    local_wheels.stop();
    QueuedDriving remote_wheels(service, {}, fixture(service));
    input.local_player = false;
    remote_wheels.start(false);
    remote_wheels.update(input);
    const auto remote_sample = 39 + expected.next() % 6;
    expect(remote_wheels.skid_sample() == remote_sample && remote_wheels.skid_volume() > 0 && energy(service.offline_mix(512)) > 0 && random.state() == expected.state(),
        "An explicit remote caller must retain initialized asphalt because425400 rejects its material writes before playback");
    remote_wheels.stop();
    driving.start();
    expect(service.active_voices() == 1, "The ordinary default start must continue to allocate its original local WHIRR channel");
    driving.stop();
}

void spatial_rumble() {
    using namespace toca::audio;
    Service service({}, true);
    QueuedDriving driving(service, {}, fixture(service));
    DrivingInputs input;
    auto& wheel = input.wheels[0];
    wheel.position = {0, 0, 4 * 65536};
    wheel.rumbling = true;
    wheel.strength_q16 = 32768;
    driving.start();
    driving.update(input);
    const auto pan = source_spatial(wheel.position, input.listener).pan;
    expect(driving.rumble_pan() == pan && pan != 64 && service.active_voices() == 2, "Original rumble start must use its actual wheel position and one shared handle");
    pan_pcm(service.offline_mix(512), pan, "The first rumble block must already apply original source pan");
    input.listener.inverse_heading = 2048;
    driving.update(input);
    const auto reversed = source_spatial(wheel.position, input.listener).pan;
    expect(driving.rumble_pan() == reversed && reversed != pan && service.active_voices() == 2, "A successful rumble update must rotate pan without restarting its shared voice");
    pan_pcm(service.offline_mix(512), reversed, "Rumble camera rotation must reach actual stereo PCM");
    const auto volume = driving.rumble_volume();
    wheel.position = {40 * 65536, 0, 0};
    driving.update(input);
    expect(driving.rumble_volume() == volume && driving.rumble_pan() == reversed, "424b50 must reject the zero integer spatial result at40units and leave rumble controls intact");
    wheel.position = {41 * 65536, 0, 0};
    driving.update(input);
    expect(driving.rumble_pan() == reversed && driving.rumble_volume() == volume, "A distant rumble query must retain the preceding voice controls");
    pan_pcm(service.offline_mix(512), reversed, "Rejected rumble spatial queries must not center ongoing PCM");
    service.apply({.stereo_effects = false});
    pan_pcm(service.offline_mix(512), 64, "Live effects mono must center an existing rumble without stopping it");
    service.apply({});
    pan_pcm(service.offline_mix(512), reversed, "Restoring effects stereo must recover the stored rumble pan");
    wheel.position = {0, 0, 4 * 65536};
    input.listener.stereo = false;
    driving.update(input);
    expect(driving.rumble_pan() == 64, "Original mono listener mode must write rumble pan64 on a successful update");
    pan_pcm(service.offline_mix(512), 64, "Source mono rumble controls must produce equal channel energy");
    input.listener.stereo = true;
    driving.update(input);
    expect(driving.rumble_pan() == reversed, "A fresh stereo rumble update must restore camera-relative pan");
    driving.stop();
    expect(service.active_voices() == 0, "Stopping spatial rumble must release its voice and wind handle");
}

void spatial_skid() {
    using namespace toca::audio;
    Service service({}, true);
    QueuedDriving driving(service, {}, fixture(service));
    toca::SourceRandom random(424), expected(424);
    DrivingInputs input;
    input.random = &random;
    auto& wheel = input.wheels[0];
    wheel.position = {0, 0, 4 * 65536};
    wheel.lateral_slip_q16 = 65536;
    driving.start();
    driving.update(input);
    expected.next();
    const auto pan = source_spatial(wheel.position, input.listener).pan;
    expect(driving.skid_pan() == pan && service.active_voices() == 2, "Original skid start must apply its source position before playback");
    pan_pcm(service.offline_mix(512), pan, "The first skid block must already use source stereo gains");
    input.listener.inverse_heading = 2048;
    driving.update(input);
    expected.next();
    const auto reversed = source_spatial(wheel.position, input.listener).pan;
    expect(driving.skid_pan() == reversed && service.active_voices() == 2, "A held skid must update its source pan without replacing the voice");
    pan_pcm(service.offline_mix(512), reversed, "Skid camera rotation must alter actual PCM channels");
    const auto volume = driving.skid_volume();
    wheel.position = {40 * 65536, 0, 0};
    driving.update(input);
    expected.next();
    expect(driving.skid_pan() == reversed && driving.skid_volume() == volume,
        "A zero integer skid query at40units must retain volume/pan after consuming its original variation draw");
    wheel.position = {41 * 65536, 0, 0};
    driving.update(input);
    expected.next();
    expect(driving.skid_pan() == reversed && driving.skid_volume() == volume, "Distant skid queries must preserve controls after source RNG dispatch");
    pan_pcm(service.offline_mix(512), reversed, "Rejected skid queries must not center the existing voice");
    wheel.position = {0, 0, 4 * 65536};
    input.listener.stereo = false;
    driving.update(input);
    expected.next();
    expect(driving.skid_pan() == 64, "Original mono listener must write skid pan64");
    pan_pcm(service.offline_mix(512), 64, "Mono skid updates must mix equal stereo channels");
    input.listener.stereo = true;
    driving.update(input);
    expected.next();
    expect(driving.skid_pan() == reversed && random.next() == expected.next(), "Pan changes must preserve the original skid variation draw order");
    driving.stop();
    expect(service.active_voices() == 0, "Stopping spatial skids must release every owned voice");
}

void surface_pan_and_tails() {
    using namespace toca::audio;
    Service service({}, true);
    QueuedDriving driving(service, {.camera_view = 1}, fixture(service));
    DrivingInputs input;
    for (auto& wheel : input.wheels) {
        wheel.strength_q16 = 65536;
        wheel.position = {100 * 65536, 0, 100 * 65536};
    }
    input.wheels[0].surface = input.wheels[2].surface = 2;
    driving.start();
    driving.update(input);
    expect(driving.rolling_pan() == 32 && driving.rolling_sample() == 67, "425400 must pan left-only dominant grass at32 using contacts0/2");
    pan_pcm(service.offline_mix(512), 32, "Left-side original grass occupancy must reach the first rolling PCM block");
    input.listener.position_q16 = {-200 * 65536, 0, -200 * 65536};
    input.listener.inverse_heading = 2048;
    driving.update(input);
    expect(driving.rolling_pan() == 32, "Surface rolling pan must ignore camera heading and all world-space distances");
    pan_pcm(service.offline_mix(512), 32, "Rolling spatial independence must retain its material-side PCM gains");
    input.listener.stereo = false;
    driving.update(input);
    expect(driving.rolling_pan() == 64, "425400's mono mode must center its occupancy-based pan");
    pan_pcm(service.offline_mix(512), 64, "Mono rolling must mix equal channels");
    input.listener.stereo = true;
    driving.update(input);
    expect(driving.rolling_pan() == 32, "Restoring source stereo must recover the actual material occupancy");
    input.wheels[1].surface = input.wheels[3].surface = 3;
    driving.update(input);
    expect(driving.rolling_pan() == 96 && driving.rolling_sample() == 68, "Right-only dominant gravel must use contacts1/3 and original pan96");
    pan_pcm(service.offline_mix(512), 96, "Surface replacement must start its new gravel voice at the original side pan");
    input.wheels[0].surface = input.wheels[2].surface = 3;
    driving.update(input);
    expect(driving.rolling_pan() == 64, "The dominant material on both sides must center rolling pan");
    pan_pcm(service.offline_mix(512), 64, "Both-side rolling occupancy must produce equal channel energy");
    for (std::size_t index = 1; index < input.wheels.size(); ++index) {
        input.wheels[index].surface = 0;
    }
    driving.update(input);
    expect(driving.rolling_pan() == 32 && driving.tail_volume() == 0, "A remaining wheel0 gravel contact must update rolling pan32 before that last contact leaves");
    pan_pcm(service.offline_mix(512), 32, "The final left gravel contact must set its material-side PCM gains");
    input.wheels[0].surface = 0;
    driving.update(input);
    // Dry asphalt callbacks return before their pan update. The preceding
    // tick's last gravel callback, rather than an intermediate asphalt write,
    // set32 before425ef0 transfers that exact handle to the fading tail.
    expect(driving.tail_volume() > 0 && driving.tail_pan() == 32 && service.active_voices() == 2,
        "The displaced rolling tail must preserve its last per-wheel pan, including3-to0 material-update order");
    pan_pcm(service.offline_mix(512), 32, "Rolling fade volume updates must retain the inherited voice pan");
    input.listener.inverse_heading = 0;
    driving.update(input);
    expect(driving.tail_pan() == 32, "426110 must not recompute the displaced rolling tail's pan from the moving listener");
    pan_pcm(service.offline_mix(512), 32, "A moving listener must not center or rotate an existing rolling tail");
    driving.stop();
    expect(service.active_voices() == 0, "Stopping driving must release occupancy-panned rolling tails");

    driving.start();
    for (auto& wheel : input.wheels) {
        wheel.surface = 3;
    }
    driving.update(input);
    expect(driving.rolling_pan() == 64, "Direct dry-return gate check requires a centered four-wheel gravel voice");
    for (auto& wheel : input.wheels) {
        wheel.surface = 0;
    }
    driving.update(input);
    expect(driving.tail_volume() > 0 && driving.tail_pan() == 64, "425400 dry-asphalt return must preserve pan64 when all four contacts leave gravel within one update");
    pan_pcm(service.offline_mix(512), 64, "The direct all-wheel asphalt transition must leave its displaced rolling voice centered");
    driving.stop();

    QueuedDriving skid(service, {}, fixture(service));
    input = {};
    for (auto& wheel : input.wheels) {
        wheel.surface = 3;
    }
    input.wheels[0].position = {0, 0, 4 * 65536};
    input.wheels[0].lateral_slip_q16 = 65536;
    skid.start();
    skid.update(input);
    const auto pan = skid.skid_pan();
    input.wheels[0].lateral_slip_q16 = 0;
    input.listener.inverse_heading = 2048;
    skid.update(input);
    expect(skid.tail_volume() > 0 && skid.tail_pan() == pan, "Releasing a gravel skid must retain its last source pan in the shared tail");
    pan_pcm(service.offline_mix(512), pan, "The first displaced skid tail block must retain its spatial gains");
    service.apply({.stereo_effects = false});
    pan_pcm(service.offline_mix(512), 64, "Live effects mono must center a displaced skid tail");
    skid.update(input);
    service.apply({});
    pan_pcm(service.offline_mix(512), pan, "Stereo restore after tail decay must recover the frozen source pan");
    skid.stop();
    expect(service.active_voices() == 0, "Stopping driving must release its source-panned skid tail");
}

void synthetic() {
    using namespace toca::audio;
    expect(driving_distance_volume({0, 0, 0}, {0, 100 * 65536, 0}) == 100, "Original driving attenuation must ignore height");
    expect(driving_distance_volume({10 * 65536, 0, 0}, {0, 0, 0}) == 75 && driving_distance_volume({41 * 65536, 0, 0}, {0, 0, 0}) == 0,
        "Original fixed fade or40-unit cutoff changed");
    Service service({}, true);
    QueuedDriving driving(service, {}, fixture(service));
    expect(service.active_voices() == 0, "Driving sample loading must reserve no voices");
    DrivingInputs input;
    driving.update(input);
    expect(service.active_voices() == 0, "Stopped driving consumer must not start samples");
    driving.start();
    driving.start();
    expect(service.active_voices() == 1, "Dry wind loop must start once");
    driving.update(input);
    expect(energy(service.offline_mix(2048)) == 0, "Stationary wind must stay silent");
    input.speed_integer = 100;
    driving.update(input);
    service.offline_mix(2048);
    const auto low = service.offline_mix(8192);
    expect(energy(low) > 0 && driving.wind_control().rate_hz == 22050 && driving.wind_control().volume == 25, "Original speed curve/base gain did not control wind PCM");
    input.shift_ticks = 2;
    driving.update(input);
    expect(driving.wind_control().volume == 19, "Wind must use existing engine shift counter");
    input.speed_integer = 400;
    input.shift_ticks = 0;
    driving.update(input);
    service.offline_mix(2048);
    const auto high = service.offline_mix(8192);
    const auto lower = crossings(low), higher = crossings(high);
    expect(energy(high) > 0 && higher * 10 > lower * 18 && higher * 10 < lower * 22, "Wind speed saturation/rate change did not reach real PCM");
    input.speed_integer = 0;
    input.wheels[3].rumbling = true;
    input.wheels[3].strength_q16 = 32768;
    driving.update(input);
    expect(service.active_voices() == 2 && driving.rumble_sample() == 58 && driving.rumble_volume() == 59, "First source wheel event must select one correctly scaled tire loop");
    expect(energy(service.offline_mix(4410)) > 0, "Tire event did not reach actual mixer PCM");
    input.wheels[0] = input.wheels[3];
    driving.update(input);
    expect(service.active_voices() == 2 && driving.rumble_sample() == 58, "Additional wheels must share the original local tire handle");
    input.wheels[3].rumbling = false;
    driving.update(input);
    expect(service.active_voices() == 2, "One stopped wheel must not stop other active rumble bits");
    input.wheels[0].strength_q16 = 1000;
    driving.update(input);
    expect(driving.rumble_volume() == 0, "Original tiny-strength bit threshold must mute tire volume");
    input.wheels[0].strength_q16 = 32768;
    driving.update(input);
    service.apply({.muted = true});
    expect(energy(service.offline_mix(2048)) == 0 && service.active_voices() == 2, "Mute must preserve active wind/tire loops while silencing PCM");
    service.apply({});
    input.wheels[0].rumbling = false;
    driving.update(input);
    expect(service.active_voices() == 1 && driving.rumble_volume() == 0, "Last source wheel stop must release the shared tire loop");
    driving.stop();
    expect(service.active_voices() == 0, "Driving stop must release all owned voices");
    driving.start();
    driving.update(input);
    expect(service.active_voices() == 1, "Driving restart must reestablish only the dry wind loop");
    driving.stop();
    QueuedDriving wet(service, {.weather = 3}, fixture(service));
    wet.start();
    wet.update({});
    expect(service.active_voices() == 1 && wet.wind_control().volume == 0 && wet.rolling_sample() == 81 && wet.rolling_volume() == 0,
        "Original weather3 must use the wet rolling layer, silent at rest, without dry WHIRR");
    wet.stop();

    toca::SourceRandom random(9), expected_random(9);
    QueuedDriving skid(service, {}, fixture(service));
    skid.start();
    input = {};
    input.random = &random;
    input.wheels[0].lateral_slip_q16 = 0x4ccc;
    skid.update(input);
    expect(service.active_voices() == 1 && skid.skid_volume() == 0, "Slip threshold itself must remain silent");
    input.wheels[0].lateral_slip_q16 = -65536;
    skid.update(input);
    expect(skid.skid_sample() == 39 + expected_random.next() % 6 && skid.skid_volume() == 44 && service.active_voices() == 2,
        "Asphalt skid must use the original threshold, shared RNG sample and View2 gain");
    expect(energy(service.offline_mix(4096)) > 0, "Actual skid event did not mix PCM");
    skid.update(input);
    expected_random.next();
    input.wheels[1] = input.wheels[0];
    skid.update(input);
    expected_random.next();
    expect(service.active_voices() == 2, "Additional slipping wheels must share one skid handle");
    input.wheels[0].airborne = true;
    skid.update(input);
    expected_random.next();
    expect(service.active_voices() == 2, "Airborne wheel must not stop another wheel's skid");
    input.wheels[1].airborne = true;
    skid.update(input);
    expect(service.active_voices() == 1 && skid.skid_volume() == 0, "Last airborne wheel must release the skid loop");
    for (auto& wheel : input.wheels) {
        wheel = {};
        wheel.surface = 2;
        wheel.strength_q16 = 65536;
    }
    input.wheels[0].longitudinal_slip_q16 = 65536;
    skid.update(input);
    expect(skid.skid_sample() == 47 + expected_random.next() % 2 && skid.skid_volume() == 79 && skid.rolling_volume() == 0,
        "Grass must select its skid samples/gain and preserve View2's zero rolling mix");
    for (auto& wheel : input.wheels) {
        wheel.surface = 3;
    }
    skid.update(input);
    expect(skid.skid_sample() == 49 && skid.skid_volume() == 69 && skid.rolling_volume() == 0, "Gravel must select the original distinct skid sample/gain");
    expect(skid.tail_volume() == (79 * 0xe666u) >> 16 && service.active_voices() == 3, "Replacing the grass skid must preserve one original integer-decay tail");
    input.wheels[0].longitudinal_slip_q16 = 0;
    skid.update(input);
    expect(
        skid.skid_volume() == 0 && skid.tail_volume() == (69 * 0xe666u) >> 16 && service.active_voices() == 2, "Last gravel skid must replace the prior tail with its own decay");
    auto tail = skid.tail_volume();
    while (tail != 0) {
        skid.update(input);
        const auto next = (tail * 0xe666u) >> 16;
        tail = next < 2 ? 0 : next;
        expect(skid.tail_volume() == tail, "Skid tail must follow source integer decay and terminate below2");
    }
    expect(service.active_voices() == 1, "Completed skid tail must release its voice");
    expect(random.next() == expected_random.next(), "Skid RNG draw order must match original start/update/shared-wheel dispatch");
    skid.stop();
    expect(service.active_voices() == 0, "Stopping driving must release skid and surface voices");
    QueuedDriving rain(service, {.weather = 5}, fixture(service));
    rain.start();
    input = {};
    for (auto& wheel : input.wheels) {
        wheel.strength_q16 = 65536;
    }
    rain.update(input);
    expect(rain.rolling_sample() == 81 && rain.rolling_volume() == 64 && energy(service.offline_mix(4096)) > 0, "Wet asphalt must consume the source wet rolling sample/gain");
    rain.stop();
    QueuedDriving outside(service, {.camera_view = 1}, fixture(service));
    outside.start();
    for (auto& wheel : input.wheels) {
        wheel.surface = 2;
    }
    outside.update(input);
    expect(outside.rolling_sample() == 67 && outside.rolling_volume() == 100, "Original outside camera must expose the grass rolling layer");
    for (auto& wheel : input.wheels) {
        wheel.surface = 3;
    }
    outside.update(input);
    expect(outside.rolling_sample() == 68 && outside.rolling_volume() == 100, "Original outside camera must expose the distinct gravel rolling layer");
    outside.stop();
    std::cout << "PASS: wind/rumble, slip thresholds and surface skid samples, shared masks/RNG, wet/rough rolling, source fade tails and lifecycle through offline PCM\n";
}

void miles_wind_pcm() {
    using namespace toca::audio;
    Service service({}, true);
    auto data = fixture(service);
    std::array<float, 256> constant;
    constant.fill(.25f);
    service.load_pcm(data.wind.sample, constant, 1, 22050);
    std::ranges::fill(data.wind.curve.rates, 22050);
    QueuedDriving driving(service, {}, data);
    driving.start();
    DrivingInputs input;
    input.speed_integer = 100;
    driving.update(input);
    expect(driving.wind_control().volume == 25, "WHIRR fixture source volume changed");
    const auto check = [&](int decibels) {
        service.offline_mix(2048);
        const auto expected_energy = 1024 * .25 * .25 * std::pow(10., double(decibels) / 1000);
        expect(std::abs(energy(service.offline_mix(512)) / expected_energy - 1) < .0001,
            "WHIRR must combine source volume25 with integer Miles master before DirectSound dB conversion");
    };
    check(-1650); // master100: floor(100*25/127)=19.
    service.apply({.effects_volume = 50});
    check(-2299); // master50: floor(50*25/127)=9.
    service.apply({.effects_volume = 0});
    expect(energy(service.offline_mix(512)) == 0, "Live Miles master zero must silence WHIRR without stopping its loop");
    service.apply({});
    check(-1650);
    driving.stop();
}

void reference(const std::filesystem::path& root) {
    using namespace toca::audio;
    const toca::AssetStore store(root);
    Service service({}, true);
    const auto data = load_driving_audio(service, store, {});
    expect(data.wind.curve.minimum_rpm == 80 && data.wind.curve.maximum_rpm == 260 && data.wind.curve.rates.size() == 91 && data.wind.source_rate == 22050,
        "Original WHIRR curve or bundled source-rate metadata changed");
    QueuedDriving driving(service, {}, data);
    driving.start();
    DrivingInputs input;
    input.speed_integer = 180;
    driving.update(input);
    expect(energy(service.offline_mix(4410)) > 0 && driving.wind_control().volume > 0, "Bundled WHIRR Vorbis must mix through its compiled source curve");
    input.speed_integer = 0;
    input.wheels[2].rumbling = true;
    input.wheels[2].strength_q16 = 65536;
    driving.update(input);
    expect(driving.rumble_sample() == 56 && energy(service.offline_mix(4410)) > 0, "Actual canonical tire sample must mix from a source event");
    driving.stop();
    expect(service.active_voices() == 0, "Canonical driving cleanup leaked voices");
    for (const auto weather : {1u, 3u, 4u, 5u}) {
        QueuedDriving surfaces(service, store, {.weather = weather});
        toca::SourceRandom random(27);
        surfaces.start();
        DrivingInputs skid_input;
        skid_input.random = &random;
        for (const auto surface : {0u, 2u, 3u}) {
            for (auto& wheel : skid_input.wheels) {
                wheel.surface = std::uint8_t(surface);
                wheel.strength_q16 = 65536;
            }
            skid_input.wheels[0].lateral_slip_q16 = 65536;
            surfaces.update(skid_input);
            const auto sample = surfaces.skid_sample();
            expect((surface == 0      ? sample >= 39 && sample <= 44
                       : surface == 2 ? sample == 47 || sample == 48
                                      : sample == 49)
                       && surfaces.skid_volume() > 0 && energy(service.offline_mix(4410)) > 0,
                "Canonical asphalt/grass/gravel skid sample must mix across the original weather archives");
        }
        surfaces.stop();
        expect(service.active_voices() == 0, "Canonical surface voice cleanup leaked voices");
    }
    std::cout << "PASS: canonical WHIRR, rumble and asphalt/grass/gravel skid PCM across dry/wet weather archives\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            queued_skid_dispatch();
            wheel_queue_producer();
            material_state();
            retained_queue_playback();
            wet_material_playback();
            wheel_only_actor();
            synthetic();
            miles_wind_pcm();
            spatial_rumble();
            spatial_skid();
            surface_pan_and_tails();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-driving-audio-tests [CANONICAL_ROOT]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
