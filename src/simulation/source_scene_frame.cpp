#include "simulation/source_scene_frame.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace toca::simulation {
namespace {
std::array<float, 3> position(const std::array<std::int32_t, 3>& fixed) {
    return {float(fixed[0]) / 65536.f, float(fixed[1]) / 65536.f, float(fixed[2]) / 65536.f};
}

scene::Visibility source_visibility(const scene::SceneResources& resources) {
    scene::Visibility result(resources.ground);
    for (const auto* baked : {&resources.trees_baked, &resources.fence_baked, &resources.objects_baked, &resources.cones.baked, &resources.posts.baked, &resources.signs.baked,
             &resources.flags.baked}) {
        (void)result.register_static(*baked);
    }
    return result;
}
} // namespace

std::array<scene::SceneJournalRoot, 18> source_scene_roots(const std::array<scene::GridSlot, 32>& grid) {
    std::array<scene::SceneJournalRoot, 18> result;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        //44e7bb seeds each allocated car parent from the first SGRID edge.
        //The later460170 body placement adds its midpoint/side/HPC offsets.
        const auto& start = grid[actor].first_xz;
        result[actor] = {scene::SceneJournalRootRole::car_parent, actor, 2000, {float(start[0]) / 65536.f, 0, float(start[1]) / 65536.f}};
    }
    result[16].role = scene::SceneJournalRootRole::follow_anchor;
    result[17].role = scene::SceneJournalRootRole::cockpit_anchor;
    return result;
}

std::vector<scene::SceneJournalRoot> source_lifecycle_scene_roots(const std::array<scene::GridSlot, 32>& grid, std::span<const scene::SceneCamera> cameras) {
    std::vector<scene::SceneJournalRoot> result;
    result.reserve(cameras.size() + 18);
    for (std::uint32_t actor = 0; actor < cameras.size(); ++actor) {
        const auto& transform = cameras[actor].transform;
        result.push_back({scene::SceneJournalRootRole::authored_camera, actor, 0, position({transform[12], transform[13], transform[14]})});
    }
    const auto ordinary = source_scene_roots(grid);
    result.insert(result.end(), ordinary.begin(), ordinary.end());
    return result;
}

SourceSceneFrame::SourceSceneFrame(scene::Visibility visibility, std::span<const scene::SceneJournalRoot> roots, std::int16_t heading, std::uint32_t local, std::uint32_t car,
    std::uint32_t detail, std::optional<std::int32_t> divisor, TimeStep initial, std::span<const scene::SceneCamera> cameras, std::span<const std::uint8_t> hpc)
    : clock_(divisor ? std::optional(SourceClock::from_stored_divisor(*divisor, initial.extra_step)) : std::nullopt), step_(initial), camera_(heading, false, car, false, detail),
      scene_(std::move(visibility)), projection_(scene::default_projection(detail, 640, 480)), published_projection_(projection_), root_count_(std::uint32_t(roots.size())),
      local_(local), detail_(detail) {
    if (local >= 16 || car >= 8 || detail > 3 || (roots.size() != 17 + cameras.size() && roots.size() != 18 + cameras.size()) || initial.delta_q16 != 65536
        || initial.reciprocal_q16 != 65536 || initial.extra_step) {
        throw std::runtime_error("Scene frames require a cold ordinary camera, initial TimeStep and sixteen actors");
    }
    if (!cameras.empty()) {
        finish_camera_.emplace(cameras);
        authored_roots_.assign(cameras.size(), scene::no_visibility_entry);
    }
    if (!hpc.empty()) {
        surface_.emplace(hpc);
    }
    scene_.initialize(order_++, roots);
    for (std::uint32_t root = 0; root < roots.size(); ++root) {
        if (roots[root].role == scene::SceneJournalRootRole::car_parent) {
            car_roots_.at(roots[root].actor) = root;
        } else if (roots[root].role == scene::SceneJournalRootRole::follow_anchor) {
            follow_root_ = root;
        } else if (roots[root].role == scene::SceneJournalRootRole::cockpit_anchor) {
            cockpit_root_ = root;
        } else if (roots[root].role == scene::SceneJournalRootRole::authored_camera && roots[root].actor < authored_roots_.size()
                   && authored_roots_[roots[root].actor] == scene::no_visibility_entry) {
            authored_roots_[roots[root].actor] = root;
        } else {
            throw std::runtime_error("Scene frames exclude externally controlled roots");
        }
    }
    for (const auto root : authored_roots_) {
        if (root == scene::no_visibility_entry) {
            throw std::runtime_error("Scene frames require one authored root per camera actor");
        }
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        auto& context = initial_context_.actors[actor];
        context.physical = scene_.root_registration(car_roots_[actor]);
        context.sector_visible = true;
        context.render_mask = context.wheel_effect_mask = context.engine_effect_mask = 1;
        context.contact_effect_mask = 0; //44dc6d initializes the HIGH node+140 to0.
        initial_masks_[actor] = {1, 1, 1}; //4469a0 before the first4271d0/446e50.
    }
}

SourceSceneFrame::SourceSceneFrame(const scene::SceneResources& resources, std::uint32_t local, std::optional<std::int32_t> divisor, TimeStep initial, bool lifecycle)
    : SourceSceneFrame(source_visibility(resources), source_lifecycle_scene_roots(resources.grid, lifecycle ? std::span(resources.cameras) : std::span<const scene::SceneCamera>{}),
          scene::single_race_start(resources.grid).heading, local, resources.setup.selection.car_id, resources.setup.selection.detail, divisor, initial,
          lifecycle ? std::span(resources.cameras) : std::span<const scene::SceneCamera>{}, lifecycle ? std::span(resources.collision_hpc) : std::span<const std::uint8_t>{}) {
    route_points_ = resources.route_points;
}

void SourceSceneFrame::set_lifecycle(SceneLifecycleBoundary boundary) {
    if (physical_begun_ || pending_primary_ || physical_
        || (boundary.state != 4 && boundary.state != 6 && boundary.state != 7 && boundary.state != 8 && boundary.state != 9 && boundary.state != 10 && boundary.state != 13)
        || boundary.timer0_q16 < 0) {
        throw std::runtime_error("Scene lifecycle requires a generated local Single Race primary boundary");
    }
    if ((boundary.state == 7 || boundary.state == 8 || boundary.state == 10 || boundary.state == 13) && !finish_camera_) {
        throw std::runtime_error("Finish/results require the authored camera resource owner");
    }
    if (boundary.state != lifecycle_.state && finish_camera_) {
        finish_camera_->reset_selection_cache();
    }
    if (boundary.state == 4 && !qualifying_track_) {
        throw std::runtime_error("State4 requires the authored qualifying entry writer");
    }
    if (boundary.state == 7 && lifecycle_.state != 7) {
        qualifying_finish_seed_ = true; //448450 selects once before its first421aa0.
    }
    lifecycle_ = boundary;
}

void SourceSceneFrame::warm_reset() {
    if (physical_begun_ || pending_primary_ || (physical_ && physical_ != step_.update_count()) || !finish_camera_) {
        throw std::runtime_error("Warm scene entry requires the completed local lifecycle wrapper");
    }
    //1ec50/26b00 do not reconstruct the head/view, authored nodes or result hold.
    authored_ = false;
    finish_camera_->reset_session();
    warm_entry_ = true;
}

void SourceSceneFrame::reset_clock(std::optional<std::int32_t> divisor) {
    if (clock_.has_value() != divisor.has_value()) {
        throw std::runtime_error("Scene clock resets must preserve owned-QPC or explicit observed-frame ownership");
    }
    if (clock_) {
        clock_->reset(*divisor);
    }
}

void SourceSceneFrame::restore_grid(std::span<const scene::BodyPose, 16> bodies, std::int16_t heading, std::optional<std::int32_t> divisor) {
    reset_clock(divisor); //412a80 retains the current delta and extra-pass latch.
    if (!grid_gates_ && has_publication_) {
        initial_context_ = scene_.context();
    }
    camera_.initialize_heading(heading);
    scene_.reset_parent_masks(order_++); //460170 restores all parents before PRE.
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        scene_.migrate(order_++, car_roots_[actor], position(bodies[actor].position));
        auto& context = initial_context_.actors[actor];
        context.physical = scene_.root_registration(car_roots_[actor]);
        context.sector_visible = initial_context_.visibility.sector_visible(context.physical.sector);
        //4469a0 resets the derived producer arrays, not the retained HIGH
        //node+140 used by BODY+8 contact/effect readers.
        context.render_mask = context.wheel_effect_mask = context.engine_effect_mask = 1;
        initial_masks_[actor] = {1, 1, 1}; //44ffe1→4469a0.
    }
    scene_.migrate(order_++, follow_root_, position(bodies[local_].position));
    if (cockpit_root_ != scene::no_visibility_entry) {
        scene_.migrate(order_++, cockpit_root_, position(bodies[local_].position));
    }
    grid_gates_ = true;
}

void SourceSceneFrame::warm_grid(std::span<const scene::BodyPose, 16> bodies, std::int16_t heading, std::optional<std::int32_t> divisor) {
    if (!warm_entry_ || physical_begun_ || pending_primary_ || physical_) {
        throw std::runtime_error("Warm scene grid requires449f50 before the next physical pass");
    }
    restore_grid(bodies, heading, divisor);
    warm_entry_ = false;
}

void SourceSceneFrame::initialize_qualifying(std::span<const scene::BodyPose, 16> bodies, std::uint32_t track, std::int16_t heading, std::optional<std::int32_t> divisor) {
    if (physical_begun_ || pending_primary_ || physical_ || observed_frame_ || track >= 10) {
        throw std::runtime_error("Qualifying entry requires the completed443f60 body boundary and authored track");
    }
    reset_clock(divisor);
    if (!grid_gates_ && has_publication_) {
        initial_context_ = scene_.context();
    }
    camera_.initialize_heading(heading);
    qualifying_track_ = track;
    first_qualifying_render_ = true;
    qualifying_finish_seed_ = false;
    authored_ = warm_entry_ = false;
    set_lifecycle({4, 0});
    scene_.reset_parent_masks(order_++, local_);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const bool active = actor % 4 == local_ % 4;
        auto initial_position = bodies[actor].position;
        if (!active && !route_points_.empty()) {
            const auto authored = scene::qualifying_start(route_points_, local_, actor);
            initial_position[0] = authored.xz[0];
            initial_position[2] = authored.xz[1];
        }
        //44ffe1 calls47ef80 immediately before47f410(mode0), so its forced
        //world refresh bypasses the manual-mask skip even for removed actors.
        //Later ordinary publications retain the Mode4 registration again.
        scene_.migrate(order_++, car_roots_[actor], position(initial_position));
        auto& context = initial_context_.actors[actor];
        context.physical = scene_.root_registration(car_roots_[actor]);
        context.sector_visible = initial_context_.visibility.sector_visible(context.physical.sector);
        context.render_mask = active ? 1 : 0;
        if (!active) {
            context.contact_effect_mask = 0; //443f60→427170 clears all inactive meshes.
        }
        context.wheel_effect_mask = context.engine_effect_mask = 1;
        initial_masks_[actor] = {1, 1, 1}; //44ffe1→4469a0.
    }
    scene_.migrate(order_++, follow_root_, position(bodies[local_].position));
    if (cockpit_root_ != scene::no_visibility_entry) {
        scene_.migrate(order_++, cockpit_root_, position(bodies[local_].position));
    }
    grid_gates_ = true;
}

void SourceSceneFrame::race_grid(std::span<const scene::BodyPose, 16> bodies, std::int16_t heading, std::optional<std::int32_t> divisor) {
    if (!qualifying_track_ || lifecycle_.state != 9 || physical_begun_ || pending_primary_ || physical_ || observed_frame_ || warm_entry_) {
        throw std::runtime_error("Earned race grid requires the qualifying-to-State9 entry, without449f50");
    }
    restore_grid(bodies, heading, divisor);
    authored_ = false; //447980, preserving the selected authored actor itself.
}

void SourceSceneFrame::begin_observed_frame(TimeStep step) {
    if (clock_ || observed_frame_ || physical_begun_ || pending_primary_ || physical_ || step.delta_q16 <= 0 || step.reciprocal_q16 <= 0) {
        throw std::runtime_error("Observed scene frames require explicit caller timing and an inactive clockless owner");
    }
    step_ = step;
    observed_frame_ = true;
}

void SourceSceneFrame::finish_observed_frame() {
    if (clock_ || !observed_frame_ || physical_begun_ || pending_primary_ || physical_ != step_.update_count()) {
        throw std::runtime_error("Observed scene frame is missing its generated primary/extra publication sequence");
    }
    observed_frame_ = false;
    physical_ = 0;
}

void SourceSceneFrame::initialize_grid(std::span<const scene::BodyPose, 16> bodies) {
    if (physical_ || physical_begun_ || has_publication_) {
        throw std::runtime_error("Initial scene-grid publication cannot replace an active scene");
    }
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        scene_.migrate(order_++, car_roots_[actor], position(bodies[actor].position));
        initial_context_.actors[actor].physical = scene_.root_registration(car_roots_[actor]);
    }
    scene_.migrate(order_++, follow_root_, position(bodies[local_].position));
    if (cockpit_root_ != scene::no_visibility_entry) {
        scene_.migrate(order_++, cockpit_root_, position(bodies[local_].position));
    }
}

void SourceSceneFrame::begin_physical(const scene::BodyPose& before, std::uint32_t session_state) {
    if (session_state != lifecycle_.state && (session_state == 9 || session_state == 6) && physical_ == 0) {
        set_lifecycle({session_state, 0});
    }
    if (physical_begun_ || pending_primary_ || physical_ >= step_.update_count() || session_state != lifecycle_.state || (!clock_ && !observed_frame_)) {
        throw std::runtime_error("Scene frame PRE is outside its generated local lifecycle sequence");
    }
    if (qualifying_finish_seed_) {
        finish_camera_->update(before.position);
        qualifying_finish_seed_ = false;
    }
    if (session_state != 4) {
        camera_.update(before, false);
    }
    physical_begun_ = true;
    controls_consumed_ = false;
}

void SourceSceneFrame::camera_controls(const SceneCameraControls& input) {
    if (!physical_begun_ || controls_consumed_) {
        throw std::runtime_error("Scene camera controls require one active human mapper");
    }
    if (lifecycle_.state == 7 || lifecycle_.state == 8 || lifecycle_.state == 10 || lifecycle_.state == 13) {
        //422f60 skips C/Z processing outside states4/6/9, including13's raw
        //device wrapper. Local AI handoff does not replace the retained latches.
        controls_consumed_ = true;
        return;
    }
    //422f60 checks C against the old rear latch before processing held Z.
    if (input.cycle_held && !cycle_held_) {
        camera_.cycle_view();
    }
    cycle_held_ = input.cycle_held;
    camera_.set_rear_view(input.rear_held);
    if (camera_.selected_view() == 1 && cockpit_root_ == scene::no_visibility_entry) {
        throw std::runtime_error("Cockpit view requires its registered source head anchor");
    }
    projection_ = scene::selected_view_projection(detail_, 640, 480, camera_.projection_view());
    controls_consumed_ = true;
}

void SourceSceneFrame::complete_physical(const ScenePhysicalBoundary& input) {
    if (!physical_begun_ || (!controls_consumed_ && lifecycle_.state != 7 && lifecycle_.state != 8 && lifecycle_.state != 10 && lifecycle_.state != 13)) {
        throw std::runtime_error("Scene frame POST requires its PRE and external controls");
    }
    for (const auto mode : input.modes) {
        if (mode > 4) {
            throw std::runtime_error("Scene frames require a generated source physical body mode");
        }
    }
    auto body = input.local_body;
    body.position = input.positions[local_];
    camera_.update_cockpit(body, input.previous_velocity, input.velocity, input.angular_velocity_y);
    last_physical_ = input;
    physical_begun_ = false;
    if (physical_++ == 0) {
        pending_primary_ = input;
    }
}

void SourceSceneFrame::publish_primary() {
    if (physical_begun_ || !pending_primary_) {
        throw std::runtime_error("Scene publication requires a completed primary physical pass");
    }
    refresh(*pending_primary_);
    pending_primary_.reset();
}

void SourceSceneFrame::publish_retained() {
    if ((lifecycle_.state != 7 && lifecycle_.state != 8 && lifecycle_.state != 13) || physical_begun_ || pending_primary_ || physical_ || !last_physical_ || warm_entry_) {
        throw std::runtime_error("Retained scene publication requires a finish/results renderer without a physical pass");
    }
    refresh(*last_physical_);
}

void SourceSceneFrame::physical(const ScenePhysicalBoundary& input) {
    begin_physical(input.before, input.session_state);
    camera_controls({});
    complete_physical(input);
    if (pending_primary_) {
        publish_primary();
    }
}

void SourceSceneFrame::refresh(const ScenePhysicalBoundary& input) {
    auto body = input.local_body;
    body.position = input.positions[local_];
    if (lifecycle_.state == 4) {
        if (first_qualifying_render_) {
            camera_.initialize_heading(scene::qualifying_camera_heading(*qualifying_track_));
            first_qualifying_render_ = false;
        }
        camera_.update(body, true); //4480e0→45eb60, once after primary physics.
    }
    if (lifecycle_.state == 8 || lifecycle_.state == 13 || ((lifecycle_.state == 7 || lifecycle_.state == 10) && lifecycle_.timer0_q16 >= 0x20000)) {
        authored_ = true; //448e90/45ed30 case13, after primary physics.
        finish_camera_->update(body.position, lifecycle_.state == 13);
    }
    if (authored_) {
        published_.world_to_view = finish_camera_->publish(body.position, surface_ ? &*surface_ : nullptr, lifecycle_.state == 13, &published_.camera_to_world);
        std::copy_n(published_.camera_to_world.begin() + 12, 3, published_.world_position.begin());
    } else {
        published_ = camera_.frame(body);
    }
    //4871b2 calls47f410(mode1) for camera ancestry, then49a660 and PVS.
    //After models,47f410(mode0) updates every root before480a80/489069.
    const auto ancestor = ancestor_root();
    scene_.migrate(order_++, ancestor, authored_ ? published_.world_position : position(body.position));
    scene::VisibilityCamera view{published_.world_to_view, published_.world_position, projection_};
    const std::array ancestors{ancestor};
    scene_.select(order_++, view, ancestors);
    scene::SceneJournalModels models;
    models.local_actor = local_;
    models.graphics_detail = detail_;
    models.view = camera_.selected_view();
    models.authored_camera = authored_;
    models.view_factor_q16 = scene::source_view_factor(camera_.selected_view());
    models.local_forward = body.basis[0];
    models.body_positions = input.positions;
    models.body_modes = input.modes;
    scene_.models(order_++, models);
    for (std::uint32_t root = 0; root < root_count_; ++root) {
        if (root == follow_root_ || root == cockpit_root_) {
            scene_.migrate(order_++, root, position(body.position));
        } else {
            for (std::uint32_t actor = 0; actor < 16; ++actor) {
                if (car_roots_[actor] == root) {
                    //4271d0 clears this view's parent+140 for Mode4. The later
                    //47f440(mode0) leaves its world/sector unchanged; an actual
                    //camera ancestor was already handled in mode1 above.
                    if (input.modes[actor] != 4) {
                        scene_.migrate(order_++, root, position(input.positions[actor]));
                    }
                    break;
                }
            }
            for (std::uint32_t actor = 0; actor < authored_roots_.size(); ++actor) {
                if (authored_roots_[actor] == root) {
                    scene_.migrate(order_++, root, position(finish_camera_->actor_position(actor)));
                    break;
                }
            }
        }
    }
    scene_.publish(order_++);
    has_publication_ = true;
    grid_gates_ = false;
    published_projection_ = projection_;
    published_state_ = {
        camera_.selected_view(), camera_.projection_view(), camera_.rear_held(), ancestor, authored_, authored_ ? finish_camera_->selected() : scene::no_visibility_entry};
}

void SourceSceneFrame::sample_idle(std::uint32_t counter) {
    if (!clock_ || physical_begun_ || pending_primary_ || physical_) {
        throw std::runtime_error("Menu-only scene clock cannot discard an active physical frame");
    }
    step_ = clock_->sample(counter);
}

void SourceSceneFrame::finish(std::uint32_t counter) {
    if (!clock_ || physical_begun_ || pending_primary_ || physical_ != step_.update_count()) {
        throw std::runtime_error("Scene frame is missing a generated physical pass");
    }
    step_ = clock_->sample(counter);
    physical_ = 0;
}

const TimeStep& SourceSceneFrame::step() const {
    return step_;
}

const scene::FollowCameraFrame& SourceSceneFrame::camera() const {
    if (!has_publication_) {
        throw std::runtime_error("Scene frame has no primary publication");
    }
    return published_;
}

const scene::Projection& SourceSceneFrame::projection() const {
    return published_projection_;
}

const scene::SceneFrameJournal& SourceSceneFrame::scene() const {
    return scene_;
}

std::uint16_t SourceSceneFrame::selected_view() const {
    return camera_.selected_view();
}

std::uint16_t SourceSceneFrame::projection_view() const {
    return camera_.projection_view();
}

bool SourceSceneFrame::rear_held() const {
    return camera_.rear_held();
}

std::uint32_t SourceSceneFrame::ancestor_root() const {
    if (authored_) {
        return authored_roots_.at(finish_camera_->selected());
    }
    const auto view = camera_.selected_view();
    if (view == 1) {
        if (cockpit_root_ == scene::no_visibility_entry) {
            throw std::runtime_error("Cockpit view has no registered head anchor");
        }
        return cockpit_root_;
    }
    return view == 2 || view == 3 ? follow_root_ : car_roots_[local_];
}

const SceneCameraState& SourceSceneFrame::published_state() const {
    if (!has_publication_) {
        throw std::runtime_error("Scene frame has no published camera controller");
    }
    return published_state_;
}

std::uint32_t SourceSceneFrame::car_root(std::uint32_t actor) const {
    return car_roots_.at(actor);
}

std::uint32_t SourceSceneFrame::authored_root(std::uint32_t actor) const {
    return authored_roots_.at(actor);
}

const scene::FleetSceneContext& SourceSceneFrame::retained_context() const {
    return grid_gates_ ? initial_context_ : scene_.context();
}

const std::array<scene::SceneJournalCarMasks, 16>& SourceSceneFrame::car_masks() const {
    return grid_gates_ ? initial_masks_ : scene_.car_masks();
}
} // namespace toca::simulation
