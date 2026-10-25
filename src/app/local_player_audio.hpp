#pragma once

#include "audio/driving.hpp"
#include "audio/engine.hpp"
#include "audio/impact.hpp"
#include "audio/scrape.hpp"

#include <memory>
#include <optional>

namespace toca {
// A view of either local player's channels. Owners retain their declaration
// order because other scene/audio resources are destroyed between channels.
struct LocalPlayerAudio {
    std::unique_ptr<audio::Engine>& engine;
    std::unique_ptr<audio::Driving>& driving;
    std::unique_ptr<audio::Impact>& impact;
    std::unique_ptr<audio::Scrapes>& scrape;

    void load(audio::Service& service, const AssetStore& assets, const audio::EngineConfig& config, std::optional<audio::DrivingData>* wheel_data = nullptr) {
        engine = std::make_unique<audio::Engine>(service, assets, config);
        if (wheel_data) {
            *wheel_data = audio::load_driving_audio(service, assets, config);
            driving = std::make_unique<audio::Driving>(service, config, **wheel_data);
        } else {
            driving = std::make_unique<audio::Driving>(service, assets, config);
        }
        impact = std::make_unique<audio::Impact>(service, assets, config);
        scrape = std::make_unique<audio::Scrapes>(service, assets, config);
    }

    void start_engine_and_driving() {
        engine->start();
        driving->start();
    }

    void start_contacts() {
        impact->start();
        scrape->start();
    }

    void start() {
        start_engine_and_driving();
        start_contacts();
    }

    template <class WheelInputs> void resume(const audio::EngineInputs& engine_input, SourceRandom& random, WheelInputs wheel_inputs) {
        engine->start();
        engine->update(engine_input, random);
        driving->start();
        // Read shift_ticks only after the engine update; it owns that latch.
        auto input = wheel_inputs();
        input.skid_queue = {};
        driving->update(input);
    }
};
} // namespace toca
