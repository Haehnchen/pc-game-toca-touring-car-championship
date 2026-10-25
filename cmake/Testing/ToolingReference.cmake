  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-display-tests NAME native-display SOURCE tests/platform/test_display.cpp
      LIBRARIES toca-render DISPLAY LABELS gpu unit TIMEOUT 30)
    toca_add_test(TARGET toca-runtime-tests NAME isolated-runtime SOURCE tests/tools/test_runtime.py
      LABELS unit tooling TIMEOUT 45)
  endif()
  toca_add_test(TARGET toca-cmake-tests NAME cmake-test-helper SOURCE tests/tools/test_cmake_tests.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-check-tests NAME selected-test-builds SOURCE tests/tools/test_check.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-configure-tests NAME incremental-configure SOURCE tests/tools/test_configure.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-formatting-tests NAME optional-formatting SOURCE tests/tools/test_formatting.py
    LABELS unit tooling TIMEOUT 45)
  toca_add_test(TARGET toca-package-tests NAME release-packaging SOURCE tests/tools/test_package_release.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-release-archive-tests NAME release-archives SOURCE tests/tools/test_verify_release.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-audio-data-tests NAME bundled-audio-data SOURCE tests/tools/test_audio_data.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-native-data-tests NAME native-data-identity SOURCE tests/tools/test_native_data_identity.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-native-support-tests NAME native-test-support SOURCE tests/integration/test_native_support.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-checksum-tests NAME crc32-checks SOURCE tests/tools/test_checksums.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-reference-trace-tests NAME reference-trace-reader SOURCE tests/integration/test_reference_trace.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-reference-replay-tests NAME reference-replay-reader SOURCE tests/tools/test_reference_replay.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-fleet-replay-options-tests NAME reference-fleet-replay-options SOURCE tests/simulation/test_fleet_replay_options.cpp
    LABELS unit tooling)
  target_sources(toca-fleet-replay-options-tests PRIVATE tests/simulation/fleet_reference/options.cpp)
  toca_add_test(TARGET toca-fleet-clock-tests NAME reference-fleet-clock-transactions SOURCE tests/simulation/test_fleet_clock_transactions.cpp
    LIBRARIES toca-simulation LABELS unit parser)
  target_sources(toca-fleet-clock-tests PRIVATE tests/simulation/fleet_reference/clock_transactions.cpp
    tests/simulation/fleet_reference/binary_io.cpp)
  toca_add_test(TARGET toca-reference-fleet-tests NAME reference-fleet-reader SOURCE tests/tools/test_reference_fleet.py
    LABELS unit tooling)
  toca_add_test(TARGET toca-format-tests NAME cm3d-reference-cars ARGS "${original_data_directory}"
    LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-asset-tests NAME bfp-reference-assets ARGS ReferenceTests -v
    LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-native-asset-tests NAME native-reference-assets
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-menu-font-tests NAME menu-font-artwork SOURCE tests/tools/test_menu_font.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-car-name-font-tests NAME car-name-font-artwork SOURCE tests/tools/test_car_name_font.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-loading-font-tests NAME loading-font-artwork SOURCE tests/tools/test_loading_font.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-car-name-tests NAME car-name-reference SOURCE tests/frontend/test_car_name.cpp
    LIBRARIES toca-frontend ARGS "${original_data_directory}" "${hud_artwork_dir}"
    LABELS unit parser bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-frontend-resource-tests NAME frontend-reference-resources
    ARGS "${original_data_directory}"
    LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-menu-localization-tests NAME frontend-reference-menu SOURCE tests/frontend/test_menu_localization.cpp
    LIBRARIES toca-frontend ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 30)
  target_sources(toca-menu-localization-tests PRIVATE tests/frontend/menu_mode_checks.cpp)
  toca_add_test(TARGET toca-frontend-audio-tests NAME frontend-reference-audio
    ARGS "${bundled_runtime_data}" "${PROJECT_SOURCE_DIR}/assets" LABELS audio bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-engine-audio-tests NAME engine-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-horn-tests NAME horn-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio bundled-data)
  toca_add_test(TARGET toca-driving-audio-tests NAME driving-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-scrape-audio-tests NAME scrape-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-impact-audio-tests NAME impact-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-start-audio-tests NAME start-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-weather-audio-tests NAME weather-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-pit-audio-tests NAME pit-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-finish-audio-tests NAME finish-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio parser bundled-data)
  toca_add_test(TARGET toca-selection-resource-tests NAME frontend-reference-selection
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 30)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-gpu-tests NAME gpu-states SOURCE tests/render/test_gpu.cpp
      LIBRARIES toca-render toca-frontend-resources glm::glm-header-only DISPLAY LABELS gpu
      ARGS --screenshot states.png TIMEOUT 30)
    toca_add_test(TARGET toca-gpu-tests NAME gpu-pipeline-preparation DISPLAY LABELS gpu
      ARGS --pipeline-preparation TIMEOUT 60)
    toca_add_test(TARGET toca-antialiasing-tests NAME gpu-antialiasing SOURCE tests/render/test_antialiasing.cpp
      LIBRARIES toca-render DISPLAY LABELS gpu TIMEOUT 60)
    toca_add_test(TARGET toca-mipmap-tests NAME gpu-mipmaps SOURCE tests/render/test_mipmaps.cpp
      LIBRARIES toca-render toca-images DISPLAY LABELS gpu TIMEOUT 45)
    toca_add_test(TARGET toca-world-geometry-tests NAME gpu-world-geometry SOURCE tests/render/test_world_geometry.cpp
      LIBRARIES toca-scene-render DISPLAY LABELS gpu TIMEOUT 60)
    toca_add_test(TARGET toca-scene-model-residency-tests NAME scene-resident-models SOURCE tests/render/test_scene_model_residency.cpp
      LIBRARIES toca-scene-render DISPLAY
      LABELS gpu TIMEOUT 45)
    toca_add_test(TARGET toca-split-screen-tests NAME scene-split-screen SOURCE tests/render/test_split_screen.cpp
      LIBRARIES toca-scene-render DISPLAY LABELS gpu TIMEOUT 45)
    toca_add_test(TARGET toca-gpu-tests NAME gpu-title DISPLAY LABELS gpu bundled-data
      ARGS --image "${PROJECT_SOURCE_DIR}/assets/ui/original/title.webp" --screenshot title.png TIMEOUT 30)
    toca_add_test(TARGET toca-native-flow-tests NAME native-aspects SOURCE tests/integration/test_native.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" aspects TIMEOUT 30)
    add_dependencies(toca-native-flow-tests toca-game)
    toca_add_test(TARGET toca-native-flow-tests NAME native-pacing DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" pacing TIMEOUT 30)
    toca_add_test(TARGET toca-native-flow-tests NAME native-interaction DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" interaction TIMEOUT 30)
    toca_add_test(TARGET toca-native-flow-tests NAME native-frontend DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" frontend TIMEOUT 45)
    toca_add_test(TARGET toca-native-cold-start-tests NAME native-cold-start SOURCE tests/integration/test_cold_start.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> TIMEOUT 90)
    add_dependencies(toca-native-cold-start-tests toca-game)
    toca_add_test(TARGET toca-native-settings-tests NAME native-settings SOURCE tests/integration/test_settings.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-settings-tests toca-game)
    toca_add_test(TARGET toca-native-menu-icon-tests NAME native-menu-icons SOURCE tests/integration/test_menu_icons.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 180)
    add_dependencies(toca-native-menu-icon-tests toca-game)
    toca_add_test(TARGET toca-native-menu-background-tests NAME native-menu-backgrounds SOURCE tests/integration/test_menu_backgrounds.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 240)
    add_dependencies(toca-native-menu-background-tests toca-game)
    toca_add_test(TARGET toca-native-garage-artwork-tests NAME native-garage-artwork SOURCE tests/integration/test_garage_artwork.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 180)
    add_dependencies(toca-native-garage-artwork-tests toca-game)
    toca_add_test(TARGET toca-native-controller-tests NAME native-controller-setup SOURCE tests/integration/test_controller_setup.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-controller-tests toca-game)
    toca_add_test(TARGET toca-native-extras-tests NAME native-extras SOURCE tests/integration/test_extras.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 90)
    add_dependencies(toca-native-extras-tests toca-game)
    toca_add_test(TARGET toca-native-flow-tests NAME native-scene DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" scene TIMEOUT 60)
    toca_add_test(TARGET toca-native-flow-tests NAME native-silverstone DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" silverstone TIMEOUT 60)
    toca_add_test(TARGET toca-native-flow-tests NAME native-thruxton DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" thruxton TIMEOUT 60)
    toca_add_test(TARGET toca-native-flow-tests NAME native-brands-hatch DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" brands-hatch TIMEOUT 60)
    foreach(track_name IN ITEMS oulton-park donington-short croft knockhill snetterton lavaland)
      toca_add_test(TARGET toca-native-flow-tests NAME native-${track_name} DISPLAY LABELS gpu audio bundled-data integration
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" ${track_name} TIMEOUT 60)
    endforeach()
    toca_add_test(TARGET toca-native-controls-tests NAME native-controls SOURCE tests/integration/test_controls.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" controls TIMEOUT 60)
    add_dependencies(toca-native-controls-tests toca-game)
    if(vehicle_honda_mode STREQUAL "mega")
      toca_add_test(TARGET toca-native-controls-tests NAME native-mega-brake-lights
        DISPLAY LABELS gpu audio bundled-data integration
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" brake-lights-mega TIMEOUT 60)
    endif()
    toca_add_test(TARGET toca-native-presentation-tests NAME native-presentation SOURCE tests/integration/test_presentation.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-presentation-tests toca-game)
    toca_add_test(TARGET toca-native-antialiasing-tests NAME native-antialiasing SOURCE tests/integration/test_antialiasing.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 90)
    toca_add_test(TARGET toca-native-antialiasing-tests NAME native-antialiasing-lavaland
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" 9 TIMEOUT 90)
    add_dependencies(toca-native-antialiasing-tests toca-game)
    toca_add_test(TARGET toca-native-camera-tests NAME native-cameras SOURCE tests/integration/test_cameras.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 90)
    add_dependencies(toca-native-camera-tests toca-game)
    if(cockpit_models)
      toca_add_test(TARGET toca-native-mega-cockpit-tests NAME native-mega-cockpit SOURCE tests/integration/test_mega_cockpit.py
        DISPLAY LABELS gpu audio bundled-data integration
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 120)
      add_dependencies(toca-native-mega-cockpit-tests toca-game)
    endif()
    toca_add_test(TARGET toca-native-full-lap-tests NAME native-full-lap SOURCE tests/integration/test_full_lap.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 90)
    add_dependencies(toca-native-full-lap-tests toca-game)
    toca_add_test(TARGET toca-native-qualifying-finish-tests NAME native-qualifying-finish SOURCE tests/integration/test_qualifying_finish.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 240)
    add_dependencies(toca-native-qualifying-finish-tests toca-game)
    foreach(track_id RANGE 0 9)
      toca_add_test(TARGET toca-native-full-lap-tests NAME native-track-lap-${track_id}
        DISPLAY LABELS gpu audio bundled-data integration extended
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" --track ${track_id} TIMEOUT 120)
    endforeach()
    foreach(weather IN ITEMS rain snow)
      foreach(track_id RANGE 0 9)
        toca_add_test(TARGET toca-native-full-lap-tests NAME native-track-lap-${track_id}-${weather}
          DISPLAY LABELS gpu audio bundled-data integration extended
          ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" --track ${track_id} --weather ${weather} TIMEOUT 120)
      endforeach()
    endforeach()
    toca_add_test(TARGET toca-native-contact-tests NAME native-contacts SOURCE tests/integration/test_contacts.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 45)
    add_dependencies(toca-native-contact-tests toca-game)
    toca_add_test(TARGET toca-native-prop-contact-tests NAME native-prop-contacts SOURCE tests/integration/test_prop_contacts.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-prop-contact-tests toca-game)
    toca_add_test(TARGET toca-native-surface-effect-tests NAME native-surface-dry SOURCE tests/integration/test_surface_effects.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" 1 TIMEOUT 60)
    add_dependencies(toca-native-surface-effect-tests toca-game)
    toca_add_test(TARGET toca-native-surface-effect-tests NAME native-surface-wet DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" 3 TIMEOUT 60)
    toca_add_test(TARGET toca-native-weather-tests NAME native-weather-rain SOURCE tests/integration/test_weather.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" 3 TIMEOUT 60)
    add_dependencies(toca-native-weather-tests toca-game)
    foreach(weather IN ITEMS 4 5)
      toca_add_test(TARGET toca-native-weather-tests NAME native-weather-${weather} DISPLAY LABELS gpu audio bundled-data integration
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" ${weather} TIMEOUT 60)
    endforeach()
    toca_add_test(TARGET toca-native-pit-tests NAME native-pit-lane SOURCE tests/integration/test_pit_lane.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 150)
    add_dependencies(toca-native-pit-tests toca-game)
    toca_add_test(TARGET toca-native-pause-tests NAME native-pause SOURCE tests/integration/test_pause.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 90)
    add_dependencies(toca-native-pause-tests toca-game)
    toca_add_test(TARGET toca-native-split-tests NAME native-split-screen SOURCE tests/integration/test_split_screen.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 150)
    add_dependencies(toca-native-split-tests toca-game)
    toca_add_test(TARGET toca-native-split-tests NAME native-split-mega-hud
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" --mega TIMEOUT 150)
    toca_add_test(TARGET toca-native-split-race-tests NAME native-split-race SOURCE tests/integration/test_split_race.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 300)
    add_dependencies(toca-native-split-race-tests toca-game)
    toca_add_test(TARGET toca-native-session-tests NAME native-session SOURCE tests/integration/test_session.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-session-tests toca-game)
    toca_add_test(TARGET toca-native-hud-tests NAME native-mega-hud SOURCE tests/integration/test_hud.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 60)
    add_dependencies(toca-native-hud-tests toca-game)
    toca_add_test(TARGET toca-championship-audio-tests NAME championship-reference-audio
      LABELS audio bundled-data ARGS "${package_audio_directory}")
    add_dependencies(toca-championship-audio-tests toca-audio-resources)
    toca_add_test(TARGET toca-championship-presentation-tests NAME championship-reference-presentation
      LABELS bundled-data ARGS "${original_data_directory}")
    toca_add_test(TARGET toca-championship-finale-tests NAME championship-reference-finale
      LABELS bundled-data ARGS "${original_data_directory}")
    set(finale_mega_test_args)
    if(TOCA_MEGA_MODELS)
      set(finale_mega_test_args "$<TARGET_FILE_DIR:toca-game>/assets/models/mega")
      add_dependencies(toca-championship-finale-view-tests toca-mega-finale-resources)
    endif()
    toca_add_test(TARGET toca-championship-finale-view-tests NAME championship-reference-finale-models
      DISPLAY LABELS gpu bundled-data ARGS "${original_data_directory}" "${hud_artwork_dir}" ${finale_mega_test_args} TIMEOUT 60)
    toca_add_test(TARGET toca-championship-record-tests NAME championship-reference-records
      LABELS bundled-data parser ARGS "${original_data_directory}")
    toca_add_test(TARGET toca-native-championship-tests NAME native-championship SOURCE tests/integration/test_championship.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 800)
    add_dependencies(toca-native-championship-tests toca-game)
    toca_add_test(TARGET toca-native-championship-presentation-tests NAME native-championship-presentation SOURCE tests/integration/test_championship_presentation.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" $<TARGET_FILE:toca-championship-fixtures> $<IF:$<BOOL:${TOCA_MEGA_MODELS}>,4,0> TIMEOUT 180)
    add_dependencies(toca-native-championship-presentation-tests toca-game toca-championship-fixtures)
    toca_add_test(TARGET toca-native-championship-record-tests NAME native-championship-records SOURCE tests/integration/test_championship_records.py
      DISPLAY LABELS integration gpu audio bundled-data
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" $<TARGET_FILE:toca-championship-fixtures> TIMEOUT 120)
    add_dependencies(toca-native-championship-record-tests toca-game toca-championship-fixtures)
    toca_add_test(TARGET toca-native-single-race-tests NAME native-single-race SOURCE tests/integration/test_single_race.py
      DISPLAY LABELS gpu audio bundled-data integration extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 180)
    add_dependencies(toca-native-single-race-tests toca-game)
    toca_add_test(TARGET toca-native-time-trial-tests NAME native-time-trial SOURCE tests/integration/test_time_trial.py
      DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" $<TARGET_FILE:toca-time-trial-tests> TIMEOUT 180)
    add_dependencies(toca-native-time-trial-tests toca-game toca-time-trial-tests)
    toca_add_test(TARGET toca-native-time-trial-driven-tests NAME native-time-trial-driven SOURCE tests/integration/test_time_trial_driven.py
      DISPLAY LABELS gpu integration bundled-data extended
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" TIMEOUT 120)
    add_dependencies(toca-native-time-trial-driven-tests toca-game)
    toca_add_test(TARGET toca-native-controls-tests NAME native-control-pacing DISPLAY LABELS gpu audio bundled-data integration
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" pacing TIMEOUT 60)
    toca_add_test(TARGET toca-gpu-tests NAME gpu-car DISPLAY LABELS gpu bundled-data
      ARGS --data-root "${original_data_directory}" --screenshot honda.png TIMEOUT 30)
  else()
    message(STATUS "GPU runtime checks unavailable: the private Xvfb runtime requires Linux")
  endif()
