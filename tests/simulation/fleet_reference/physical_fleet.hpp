#pragma once

#include "binary_io.hpp"
#include "clock_transactions.hpp"
#include "diagnostics.hpp"
#include "record.hpp"
#include "simulation/car_contacts.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/race_world.hpp"
#include "simulation/source_scene_frame.hpp"

#include <memory>
#include <string_view>

namespace toca::test::fleet {
using namespace toca::simulation;

class Fleet {
    const HpcSurface& surface_;
    const scene::BarrierData& barriers_;
    RaceWorldProcessContext process_ = fresh_race_world_process();
    DamageContext damage_;
    SurfaceRoughness& roughness_ = process_.roughness;
    PropContacts props_;
    CarContacts contacts_;
    std::array<std::unique_ptr<Motion>, 16> actors_;
    std::array<std::uint32_t, 16> car_ids_{};
    std::array<bool, 16> reverse_{};
    std::array<std::array<std::int32_t, 15>, 16> partners_{};
    std::array<std::uint32_t, 16> partner_counts_{};
    std::array<BodyMode, 16> modes_{}, integration_modes_{}, ground_modes_{};
    std::array<bool, 2048> sector_visible_{};
    bool visibility_all_ = false;
    std::uint32_t local_, weather_;
    bool wrote_header_ = false;
    std::array<SourceSnapshot, 16> integration_view_, ground_view_;

    void capture_physical(std::array<SourceSnapshot, 16>& view) const;

    void write(std::ostream& output, std::uint32_t id, std::string_view phase, const TimeStep* time = nullptr, const ClockTransactions* lifecycle = nullptr);

    void write_detached(std::ostream& output, std::uint32_t id, std::string_view phase) const;

public:
    Fleet(const HpcSurface& surface, const scene::BarrierData& barriers, const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
        const Terrain& terrain, std::uint32_t local, std::uint32_t weather);

    template <class Actors> void copy_drivetrain(Actors& destination, bool rpm = false) const {
        for (std::size_t actor = 0; actor < 16; ++actor) {
            const auto& state = actors_[actor]->controls();
            auto& raw = destination[actor].dynamics;
            raw[0x330] = std::uint8_t(state.gear);
            raw[0x334] = std::uint8_t(state.pending_gear);
            source_store(raw, 0x30c, state.gear_pending);
            source_store(raw, 0x304, state.reverse_pedal);
            if (rpm) {
                source_store(raw, 0x124, state.engine_speed_q16);
                source_store(raw, 0x128, actors_[actor]->state().previous_engine_speed_q16);
            }
        }
    }

    template <class Actors> void copy_pedals(Actors& destination) const {
        for (std::size_t actor = 0; actor < 16; ++actor) {
            const auto state = actors_[actor]->pedal_filter_state();
            auto& raw = destination[actor].dynamics;
            const std::array<std::int32_t, 6> values{
                state.throttle_step_q16, state.brake_step_q16, state.throttle_ramp_q16, state.brake_ramp_q16, state.throttle_q16, state.brake_q16};
            constexpr std::array<std::size_t, 6> offsets{0x14, 0x38, 0x120, 0x134, 0x11c, 0x130};
            for (std::size_t field = 0; field < offsets.size(); ++field) {
                source_store(raw, offsets[field], std::uint32_t(values[field]));
            }
        }
    }

    template <class Actors> void copy_steering(Actors& destination) const {
        for (std::size_t actor = 0; actor < 16; ++actor) {
            const auto state = actors_[actor]->steering_filter_state();
            auto& raw = destination[actor].dynamics;
            source_store(raw, 0x44, std::uint32_t(state.return_step_q16));
            source_store(raw, 0x150, std::uint32_t(state.accumulator_q16));
            source_store(raw, 0x274, std::uint16_t(state.angle), 2);
            if (source_u32(destination[actor].gameplay, 8) != 0) {
                //40b360 clears the human's mapped axes below0xccc. The
                //following403dd0 PRE sees those physical results, before
                //422f60 maps the next external human input.
                const auto& controls = actors_[actor]->controls();
                source_store(raw, 0x138, std::uint32_t(controls.raw_left_q16));
                source_store(raw, 0x13c, std::uint32_t(controls.raw_right_q16));
            }
        }
    }

    void copy_limiter(std::array<bool, 16>& active, std::array<std::int32_t, 16>& load) const;

    void publish_physical(std::array<SourceSnapshot, 16>& destination, bool ground, bool dispatch = false) const;

    void publish_dispatch(std::array<SourceSnapshot, 16>& destination) const;

    void publish_initial_body(std::array<SourceSnapshot, 16>& destination) const;
    scene::BodyPose local_pose() const;
    fixed::Vector local_velocity() const;
    void scene_boundary(ScenePhysicalBoundary& boundary) const;

    void publish_initial_control_calibration(std::array<SourceSnapshot, 16>& destination) const;

    BodySelectionResult select_dispatch(BodySelectionContext context, std::array<SourceSnapshot, 16>& drivers, std::array<BodySelectionActor, 16>* selected_inputs = nullptr);

    void copy_pair_history(std::array<std::uint8_t, 0x800>& destination) const;

    void local_boundary(const SourceLocalInputBoundary& boundary);

    void local_handoff_to_ai(std::int32_t tire_base);

    void publish_warm_context(std::array<SourceSnapshot, 16>& actors) const;

    void warm_grid(const ColdGridInitResult& grid);

    std::vector<InitialWorldValue> initial_world_values(const ColdGridInitResult* startup = nullptr) const;

    void restore(const Record& record, bool fresh_world = false);

    void step(const Record& record, std::ostream& output, std::ostream& detached, std::ostream& launch, std::ostream& selection_output, bool imported, bool derive_selection,
        bool preselected_dispatch = false, const ClockTransactions* clock = nullptr, const scene::FleetSceneContext* generated_scene = nullptr);
};

} // namespace toca::test::fleet
