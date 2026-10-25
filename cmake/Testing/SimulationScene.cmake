  # Render an explicitly supplied local original capture; never launch Wine
  # or depend on untracked screenshots in the ordinary test suite.
  add_executable(toca-reference-scene tests/render/test_reference_scene.cpp)
  target_link_libraries(toca-reference-scene PRIVATE toca-project-options toca-scene-render)
  toca_add_test(TARGET toca-engine-effect-tests NAME engine-effects SOURCE tests/scene/test_engine_effects.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-engine-effect-render-tests NAME engine-effect-render SOURCE tests/render/test_engine_effects.cpp
    DISPLAY LIBRARIES toca-scene-render LABELS gpu TIMEOUT 45)
  toca_add_test(TARGET toca-source-scene-frame-tests NAME simulation-scene-frames SOURCE tests/simulation/test_scene_frames.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-scene-visibility-tests NAME scene-visibility SOURCE tests/scene/test_visibility.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-scene-visibility-render-tests NAME scene-visibility-render SOURCE tests/render/test_visibility.cpp
    DISPLAY LIBRARIES toca-scene-render LABELS gpu TIMEOUT 60)
  toca_add_test(TARGET toca-engine-effect-tests NAME engine-reference-effects
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-engine-effect-render-tests NAME engine-reference-render DISPLAY
    ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
  toca_add_test(TARGET toca-scene-visibility-tests NAME scene-reference-visibility
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-scene-visibility-render-tests NAME scene-reference-visibility-render DISPLAY
    ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 60)
  if(UNIX AND NOT APPLE)
    toca_add_test(TARGET toca-native-engine-effect-tests NAME native-engine-effects SOURCE tests/integration/test_engine_effects.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" 15 TIMEOUT 75)
    add_dependencies(toca-native-engine-effect-tests toca-game)
  endif()
  toca_add_test(TARGET toca-cheat-tests NAME frontend-cheats SOURCE tests/frontend/test_cheats.cpp
    LIBRARIES toca-frontend LABELS unit)
  toca_add_test(TARGET toca-extras-state-tests NAME extras-state SOURCE tests/settings/test_extras.cpp
    LIBRARIES toca-app-support toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-settings-tests NAME native-preferences SOURCE tests/settings/test_preferences.cpp
    LIBRARIES toca-app-support LABELS unit parser)
  toca_add_test(TARGET toca-gamepad-feedback-tests NAME gamepad-feedback SOURCE tests/app/test_gamepad_feedback.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-wheel-feedback-tests NAME wheel-feedback SOURCE tests/app/test_wheel_feedback.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-wheel-haptic-tests NAME wheel-haptics SOURCE tests/app/test_wheel_haptics.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-horn-tests NAME horn-audio SOURCE tests/audio/test_horn.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-input-tests NAME native-input SOURCE tests/app/test_input.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-input-device-tests NAME native-input-devices SOURCE tests/app/test_input_devices.cpp
    LIBRARIES toca-app-support toca-simulation LABELS unit)
  target_sources(toca-input-device-tests PRIVATE tests/app/input_devices/fixtures.cpp
    tests/app/input_devices/keyboard_pad.cpp tests/app/input_devices/local_players.cpp
    tests/app/input_devices/wheel.cpp tests/app/input_devices/feedback.cpp)
  toca_add_test(TARGET toca-input-lifetime-tests NAME input-device-lifetime SOURCE tests/app/test_input_lifetime.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-analog-control-tests NAME vehicle-analog-controls SOURCE tests/simulation/test_analog_controls.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-driving-input-tests NAME driving-input SOURCE tests/app/test_driving_input.cpp
    LIBRARIES toca-app-support LABELS unit)
  toca_add_test(TARGET toca-step-input-tests NAME diagnostic-step-input SOURCE tests/app/test_step_input.cpp
    LIBRARIES toca-app-support toca-simulation LABELS unit tooling)
  toca_add_test(TARGET toca-time-tests NAME simulation-time SOURCE tests/simulation/test_time.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-controls-tests NAME vehicle-controls SOURCE tests/simulation/test_controls.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-shared-surface-tests NAME shared-surface SOURCE tests/simulation/test_shared_surface.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-motion-tests NAME vehicle-motion SOURCE tests/simulation/test_motion.cpp
    LIBRARIES toca-simulation LABELS unit)
  target_sources(toca-motion-tests PRIVATE tests/simulation/motion_tests/fixtures.cpp
    tests/simulation/motion_tests/body.cpp tests/simulation/motion_tests/canonical.cpp
    tests/simulation/motion_tests/numeric.cpp tests/simulation/motion_tests/effects.cpp
    tests/simulation/motion_tests/handling.cpp tests/simulation/motion_tests/lifecycle.cpp
    tests/simulation/motion_tests/snapshot.cpp tests/simulation/motion_tests/snapshot_dispatch.cpp)
  toca_add_test(TARGET toca-rollover-tests NAME vehicle-rollover SOURCE tests/simulation/test_rollover.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-damage-tests NAME vehicle-damage SOURCE tests/simulation/test_damage.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-car-damage-tests NAME car-damage-render SOURCE tests/render/test_car_damage.cpp
    LIBRARIES toca-scene-render LABELS unit parser)
  toca_add_test(TARGET toca-barrier-tests NAME vehicle-barriers SOURCE tests/simulation/test_barrier_contact.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-prop-contact-tests NAME prop-contacts SOURCE tests/simulation/test_prop_contacts.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-race-world-tests NAME race-world SOURCE tests/simulation/test_race_world.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-contact-effect-tests NAME contact-effects SOURCE tests/scene/test_contact_effects.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-ground-scrape-tests NAME ground-scrape SOURCE tests/scene/test_ground_scrape.cpp
    LIBRARIES toca-scene LABELS unit)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-ground-scrape-flow-tests NAME ground-scrape-flow SOURCE tests/integration/test_ground_scrape.cpp
      LIBRARIES toca-simulation toca-scene-render DISPLAY LABELS gpu integration TIMEOUT 30)
    toca_add_test(TARGET toca-contact-effect-render-tests NAME contact-effects-render SOURCE tests/render/test_contact_effects.cpp
      LIBRARIES toca-scene-render DISPLAY LABELS gpu unit TIMEOUT 30)
  endif()
  toca_add_test(TARGET toca-scrape-audio-tests NAME scrape-audio SOURCE tests/audio/test_scrape_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-spatial-audio-tests NAME spatial-audio SOURCE tests/audio/test_spatial.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-prop-fragment-tests NAME prop-fragments SOURCE tests/scene/test_prop_fragments.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-pause-tests NAME pause-menu SOURCE tests/scene/test_pause.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-rivals-audio-tests NAME rivals-audio SOURCE tests/audio/test_rivals_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-pause-audio-tests NAME pause-audio SOURCE tests/audio/test_pause_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-session-tests NAME session-progress SOURCE tests/simulation/test_session.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-tests NAME championship SOURCE tests/simulation/test_championship.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-finale-tests NAME championship-finale SOURCE tests/scene/test_championship_finale.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-finale-view-tests NAME championship-finale-models SOURCE tests/render/test_championship_finale.cpp
    LIBRARIES toca-scene-render toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-slot-tests NAME championship-slots SOURCE tests/settings/test_championship_slots.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-presentation-tests NAME championship-presentation SOURCE tests/frontend/test_championship_presentation.cpp
    LIBRARIES toca-frontend toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-championship-record-tests NAME championship-records SOURCE tests/settings/test_championship_records.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  add_executable(toca-championship-fixtures EXCLUDE_FROM_ALL tests/integration/championship_fixtures.cpp)
  target_link_libraries(toca-championship-fixtures PRIVATE toca-simulation)
  toca_add_test(TARGET toca-championship-audio-tests NAME championship-audio SOURCE tests/audio/test_championship_audio.cpp
    LIBRARIES toca-audio toca-simulation LABELS unit audio)
  toca_add_test(TARGET toca-race-progress-tests NAME race-progress SOURCE tests/simulation/test_race_progress.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-contact-candidate-tests NAME contact-candidates SOURCE tests/simulation/test_contact_candidates.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-car-contact-tests NAME car-contacts SOURCE tests/simulation/test_car_contacts.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-time-trial-tests NAME time-trial SOURCE tests/simulation/test_time_trial.cpp
    LIBRARIES toca-simulation LABELS unit parser
    ARGS "${PROJECT_BINARY_DIR}/time-trial-test-state")
  toca_add_test(TARGET toca-lap-record-tests NAME lap-records SOURCE tests/settings/test_lap_records.cpp
    LIBRARIES toca-app-support LABELS unit parser)
  toca_add_test(TARGET toca-ai-driver-tests NAME ai-driver SOURCE tests/simulation/test_ai_driver.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  toca_add_test(TARGET toca-pit-route-tests NAME pit-route SOURCE tests/scene/test_pit_route.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-pit-lane-tests NAME pit-lane SOURCE tests/simulation/test_pit_lane.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-qualifying-tests NAME qualifying SOURCE tests/scene/test_qualifying.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-qualifying-tests NAME qualifying-reference
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-engine-audio-tests NAME engine-audio SOURCE tests/audio/test_engine_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-driving-audio-tests NAME driving-audio SOURCE tests/audio/test_driving_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-impact-audio-tests NAME impact-audio SOURCE tests/audio/test_impact_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-start-audio-tests NAME start-audio SOURCE tests/audio/test_start_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-weather-audio-tests NAME weather-audio SOURCE tests/audio/test_weather_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-skid-mark-tests NAME skid-marks SOURCE tests/scene/test_skid_marks.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-skid-mark-render-tests NAME skid-mark-render SOURCE tests/render/test_skid_marks.cpp
    DISPLAY LIBRARIES toca-scene-render LABELS gpu TIMEOUT 45)
  toca_add_test(TARGET toca-surface-effect-tests NAME surface-effects SOURCE tests/scene/test_surface_effects.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-sun-tests NAME scene-sun SOURCE tests/scene/test_sun.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-weather-tests NAME scene-weather SOURCE tests/scene/test_weather.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-pit-audio-tests NAME pit-audio SOURCE tests/audio/test_pit_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-finish-audio-tests NAME finish-audio SOURCE tests/audio/test_finish_audio.cpp
    LIBRARIES toca-audio LABELS unit audio)
  toca_add_test(TARGET toca-audio-tests NAME audio-service SOURCE tests/audio/test_audio.cpp
    LIBRARIES toca-audio LABELS unit audio TIMEOUT 30)
  toca_add_test(TARGET toca-audio-tests NAME bundled-audio-decoding
    ARGS --converted "${bundled_runtime_data}" LABELS audio bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-frontend-audio-tests NAME frontend-audio SOURCE tests/audio/test_frontend_audio.cpp
    LIBRARIES toca-audio LABELS unit audio TIMEOUT 30)
  toca_add_test(TARGET toca-format-tests NAME cm3d-parser SOURCE tests/formats/test_cm3d.cpp
    LIBRARIES toca-formats toca-assets LABELS unit parser)
  toca_add_test(TARGET toca-asset-tests NAME bfp-parser SOURCE tests/formats/test_assets.py
    LABELS unit parser ARGS BfpTests -v)
  toca_add_test(TARGET toca-native-asset-tests NAME native-assets SOURCE tests/assets/test_assets.cpp
    LIBRARIES toca-assets LABELS unit parser)
  toca_add_test(TARGET toca-compressed-data-tests NAME compressed-game-data SOURCE tests/tools/test_game_data.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-frontend-resource-tests NAME frontend-resources SOURCE tests/frontend/test_resources.cpp
    LIBRARIES toca-frontend-resources LABELS unit parser)
  toca_add_test(TARGET toca-menu-tests NAME frontend-menu SOURCE tests/frontend/test_menu.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-menu-settings-tests NAME frontend-menu-settings SOURCE tests/frontend/test_menu_settings.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-menu-binding-tests NAME frontend-menu-bindings SOURCE tests/frontend/test_menu_bindings.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-menu-mode-tests NAME frontend-menu-modes SOURCE tests/frontend/test_menu_modes.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  target_sources(toca-menu-mode-tests PRIVATE tests/frontend/menu_mode_checks.cpp)
  toca_add_test(TARGET toca-menu-font-layout-tests NAME frontend-menu-fonts SOURCE tests/frontend/test_menu_fonts.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-menu-icon-asset-tests NAME menu-icon-assets SOURCE tests/tools/test_menu_icon_assets.py
    LABELS tooling unit)
  toca_add_test(TARGET toca-garage-asset-tests NAME garage-assets SOURCE tests/tools/test_garage_assets.py
    LABELS tooling unit TIMEOUT 30)
  toca_add_test(TARGET toca-selection-resource-tests NAME frontend-selection SOURCE tests/frontend/test_selection_resources.cpp
    LIBRARIES toca-frontend LABELS unit parser)
  toca_add_test(TARGET toca-scene-setup-tests NAME scene-setup SOURCE tests/scene/test_setup.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-random-tests NAME source-random SOURCE tests/core/test_random.cpp
    LIBRARIES toca-project-options LABELS unit)
  target_include_directories(toca-random-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
  toca_add_test(TARGET toca-scene-resource-tests NAME scene-resources SOURCE tests/scene/test_resources.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-scene-prop-tests NAME scene-props SOURCE tests/scene/test_props.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-flag-animation-tests NAME flag-animation SOURCE tests/scene/test_flag_animation.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-horizon-actor-tests NAME horizon-actors SOURCE tests/scene/test_horizon_actors.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-skid-mark-tests NAME skid-reference-marks
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-weather-tests NAME scene-reference-weather
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-surface-effect-render-tests NAME surface-reference-effects SOURCE tests/render/test_surface_effects.cpp
    DISPLAY LIBRARIES toca-scene-render LABELS gpu parser bundled-data
    ARGS "${original_data_directory}" TIMEOUT 45)
  toca_add_test(TARGET toca-weather-render-tests NAME scene-reference-weather-render SOURCE tests/render/test_weather.cpp
    DISPLAY LIBRARIES toca-scene-render LABELS gpu bundled-data
    ARGS "${original_data_directory}" TIMEOUT 45)
  toca_add_test(TARGET toca-scene-camera-tests NAME scene-camera SOURCE tests/scene/test_camera.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-scene-presentation-tests NAME scene-presentation SOURCE tests/scene/test_presentation.cpp
    LIBRARIES toca-scene-render LABELS unit)
  toca_add_test(TARGET toca-cockpit-tests NAME scene-cockpit SOURCE tests/scene/test_cockpit.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-cockpit-model-tests NAME cockpit-model-generator SOURCE tests/tools/test_cockpit_models.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-cockpit-texture-tests NAME cockpit-textures SOURCE tests/tools/test_cockpit_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-cockpit-build-tests NAME cockpit-model-build SOURCE tests/tools/test_cockpit_build.py
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-cockpit-inventory-tests NAME cockpit-inventory SOURCE tests/tools/test_cockpit_inventory.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-cockpit-dashboard-tests NAME cockpit-dashboards SOURCE tests/tools/test_cockpit_dashboards.py
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-cockpit-hand-tests NAME cockpit-hands SOURCE tests/tools/test_cockpit_hands.py
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-cockpit-hand-texture-tests NAME cockpit-hand-textures SOURCE tests/tools/test_cockpit_hand_textures.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  if(cockpit_models)
    toca_add_test(TARGET toca-mega-cockpit-tests NAME scene-reference-mega-cockpit SOURCE tests/scene/test_mega_cockpit.cpp
      LIBRARIES toca-scene LABELS parser bundled-data
      ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TIMEOUT 30)
    add_dependencies(toca-mega-cockpit-tests toca-cockpit-resources)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      toca_add_test(TARGET toca-mega-cockpit-render-tests NAME scene-reference-mega-cockpit-render SOURCE tests/render/test_mega_cockpit.cpp
        LIBRARIES toca-scene-render DISPLAY LABELS gpu bundled-data
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TIMEOUT 60)
      add_dependencies(toca-mega-cockpit-render-tests toca-cockpit-resources)
      toca_add_test(TARGET toca-mega-cockpit-render-tests NAME scene-reference-mega-dashboard-render
        DISPLAY LABELS gpu bundled-data
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --dashboard-review TIMEOUT 60)
    endif()
  endif()
  toca_add_test(TARGET toca-cockpit-tests NAME scene-reference-cockpit
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-cockpit-render-tests NAME scene-reference-cockpit-render SOURCE tests/render/test_cockpit.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 60)
  endif()
  toca_add_test(TARGET toca-scene-placement-tests NAME scene-placement SOURCE tests/scene/test_placement.cpp
    LIBRARIES toca-scene LABELS unit parser)
  toca_add_test(TARGET toca-scene-body-tests NAME scene-body SOURCE tests/scene/test_body.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-car-lights-tests NAME car-lights SOURCE tests/scene/test_car_lights.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-car-visual-tests NAME car-visual SOURCE tests/scene/test_car_visual.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-reference-render-reader-tests NAME reference-render-reader SOURCE tests/integration/test_reference_render.py
    LABELS unit)
  toca_add_test(TARGET toca-car-shadow-tests NAME car-shadow SOURCE tests/scene/test_car_shadow.cpp
    LIBRARIES toca-scene LABELS unit)
  toca_add_test(TARGET toca-follow-camera-tests NAME follow-camera SOURCE tests/scene/test_follow_camera.cpp
    LIBRARIES toca-scene toca-audio LABELS unit)
  toca_add_test(TARGET toca-scene-render-tests NAME scene-terrain SOURCE tests/render/test_scene.cpp
    LIBRARIES toca-scene-render glm::glm-header-only LABELS unit)
  toca_add_test(TARGET toca-mega-ground-uv-tests NAME mega-ground-uv SOURCE tests/scene/test_mega_ground_uv.cpp
    LIBRARIES toca-scene-render LABELS unit)
  toca_add_test(TARGET toca-mega-ground-transition-tests NAME mega-ground-transition SOURCE tests/scene/test_mega_ground_transitions.cpp
    LIBRARIES toca-scene-render LABELS unit)
  toca_add_test(TARGET toca-mega-ground-edge-tests NAME mega-ground-edges SOURCE tests/scene/test_mega_ground_edges.cpp
    LIBRARIES toca-scene-render LABELS unit)
  toca_add_test(TARGET toca-scene-model-tests NAME scene-models SOURCE tests/render/test_scene_models.cpp
    LIBRARIES toca-scene-preparation LABELS unit parser)
  toca_add_test(TARGET toca-hud-tests NAME scene-hud SOURCE tests/scene/test_hud.cpp
    LIBRARIES toca-scene-render LABELS unit)
  toca_add_test(TARGET toca-hud-artwork-tests NAME scene-hud-artwork SOURCE tests/scene/test_hud_artwork.cpp
    LIBRARIES toca-scene-render ARGS "${hud_artwork_dir}" LABELS unit)
  toca_add_test(TARGET toca-hud-tests NAME scene-reference-hud
    ARGS "${original_data_directory}" LABELS parser bundled-data)
