  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-hud-layout-tests NAME scene-reference-hud-layout SOURCE tests/render/test_hud_layout.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" "${hud_artwork_dir}" LABELS gpu bundled-data TIMEOUT 45)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-loading-view-tests NAME scene-reference-loading-view SOURCE tests/render/test_loading_view.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
  endif()
  toca_add_test(TARGET toca-scene-resource-tests NAME scene-reference-resources
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-scene-prop-tests NAME scene-reference-props
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-prop-contact-tests NAME prop-reference-contacts
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-horizon-actor-tests NAME scene-reference-horizon-actors
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-scene-placement-tests NAME scene-reference-placement
    ARGS "${original_data_directory}" LABELS parser bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-scene-body-tests NAME scene-reference-body
    ARGS "${original_data_directory}" LABELS bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-motion-tests NAME vehicle-reference-motion
    ARGS "${original_data_directory}" LABELS bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-barrier-tests NAME vehicle-reference-barriers
    ARGS "${original_data_directory}" LABELS bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-session-tests NAME session-reference-progress
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-ai-driver-tests NAME ai-reference-driver
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-rivals-audio-tests NAME rivals-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio bundled-data)
  toca_add_test(TARGET toca-rivals-tests NAME rivals-reference SOURCE tests/simulation/test_rivals.cpp
    LIBRARIES toca-simulation ARGS "${original_data_directory}" LABELS bundled-data extended TIMEOUT 180)
  foreach(track RANGE 0 9)
    toca_add_test(TARGET toca-rivals-tests NAME rivals-track-${track}
      ARGS "${original_data_directory}" ${track} LABELS bundled-data extended TIMEOUT 60)
  endforeach()
  toca_add_test(TARGET toca-rivals-tests NAME rivals-donington-snow
    ARGS "${original_data_directory}" 0 5 LABELS bundled-data extended TIMEOUT 60)
  toca_add_test(TARGET toca-rivals-tests NAME rivals-silverstone-rain
    ARGS "${original_data_directory}" 1 3 LABELS bundled-data extended TIMEOUT 60)
  toca_add_test(TARGET toca-rivals-tests NAME rivals-lavaland-weather
    ARGS "${original_data_directory}" 9 5 LABELS bundled-data extended TIMEOUT 60)
  toca_add_test(TARGET toca-full-lap-tests NAME vehicle-full-lap SOURCE tests/simulation/test_full_lap.cpp
    LIBRARIES toca-simulation toca-assets ARGS "${original_data_directory}" LABELS bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-track-lap-tests NAME vehicle-track-laps SOURCE tests/simulation/test_track_laps.cpp
    LIBRARIES toca-simulation toca-assets ARGS "${original_data_directory}" track-laps LABELS bundled-data TIMEOUT 120)
  foreach(weather IN ITEMS rain snow)
    toca_add_test(TARGET toca-track-lap-tests NAME vehicle-track-laps-${weather}
      ARGS "${original_data_directory}" track-laps --weather ${weather}
      LABELS bundled-data TIMEOUT 120)
  endforeach()
  toca_add_test(TARGET toca-pit-route-tests NAME pit-reference-route
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-pit-lane-tests NAME pit-reference-lane
    ARGS "${original_data_directory}" LABELS bundled-data)
  toca_add_test(TARGET toca-pit-motion-tests NAME pit-reference-motion SOURCE tests/simulation/test_pit_motion.cpp
    LIBRARIES toca-simulation ARGS "${original_data_directory}" LABELS bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-pause-tests NAME pause-reference-menu
    ARGS "${original_data_directory}" LABELS parser bundled-data)
  toca_add_test(TARGET toca-pause-audio-tests NAME pause-reference-audio
    ARGS "${bundled_runtime_data}" LABELS audio bundled-data)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-prop-scene-tests NAME prop-scene SOURCE tests/render/test_prop_scene.cpp
      LIBRARIES toca-scene-render toca-simulation DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-pit-view-tests NAME pit-view SOURCE tests/render/test_pit_view.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-pause-view-tests NAME pause-view SOURCE tests/render/test_pause_view.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" "${hud_artwork_dir}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-time-trial-view-tests NAME time-trial-view SOURCE tests/render/test_time_trial_view.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-records-view-tests NAME frontend-records-view SOURCE tests/render/test_records_view.cpp
      LIBRARIES toca-frontend toca-simulation DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-menu-transition-tests NAME frontend-menu-transition SOURCE tests/render/test_menu_transition.cpp
      LIBRARIES toca-frontend DISPLAY ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-qualifying-view-tests NAME qualifying-view SOURCE tests/render/test_qualifying_view.cpp
      LIBRARIES toca-scene-render DISPLAY ARGS "${original_data_directory}" "${hud_artwork_dir}" LABELS gpu bundled-data TIMEOUT 45)
    toca_add_test(TARGET toca-scene-render-tests NAME scene-reference-terrain DISPLAY
      ARGS --reference "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 60)
    toca_add_test(TARGET toca-scene-model-original-tests NAME scene-reference-models SOURCE tests/render/test_scene_model_original.cpp
      LIBRARIES toca-scene-render DISPLAY
      ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 60)
    toca_add_test(TARGET toca-sun-tests NAME scene-reference-sun
      ARGS "${original_data_directory}" LABELS bundled-data)
    toca_add_test(TARGET toca-sun-render-tests NAME scene-reference-sun-render SOURCE tests/render/test_sun.cpp DISPLAY
      LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" LABELS gpu bundled-data TIMEOUT 45)
    if(TARGET toca-mega-scene-resources)
      toca_add_test(TARGET toca-ground-material-tests NAME scene-reference-ground-materials SOURCE tests/render/test_ground_materials.cpp DISPLAY
        LIBRARIES toca-scene-render glm::glm-header-only
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" "${PROJECT_SOURCE_DIR}/assets/models/mega"
          "${PROJECT_SOURCE_DIR}/docs/mega-ground-textures.tsv" 0
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-ground-material-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-refinement SOURCE tests/render/test_mega_ground_refinement.cpp DISPLAY
        LIBRARIES toca-scene-render glm::glm-header-only
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-mega-ground-refinement-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-edges DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --edges
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-edges-silverstone DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --edges 1
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-direct DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --direct
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-edges-brands DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --edges 3
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-edges-knockhill DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --edges 7
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-edges-snetterton DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --edges 8
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-boundaries-croft DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --boundaries 6
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-ground-refinement-tests NAME scene-reference-mega-ground-boundaries-lavaland DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --boundaries 9
        LABELS gpu bundled-data TIMEOUT 90)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/mega-ground-textures.tsv")
      file(STRINGS "${PROJECT_SOURCE_DIR}/docs/mega-ground-textures.tsv" ground_inventory)
      foreach(ground_track IN ITEMS 1 2 3 4 5 6 7 8 9)
        set(ground_complete TRUE)
        foreach(ground_row IN LISTS ground_inventory)
          if(ground_row MATCHES "^${ground_track}\t.*\tgeplant$")
            set(ground_complete FALSE)
          endif()
        endforeach()
        if(NOT ground_complete)
          continue()
        endif()
        toca_add_test(TARGET toca-ground-material-tests NAME scene-reference-ground-materials-track${ground_track} DISPLAY
          ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" "${PROJECT_SOURCE_DIR}/assets/models/mega"
            "${PROJECT_SOURCE_DIR}/docs/mega-ground-textures.tsv" ${ground_track}
          LABELS gpu bundled-data extended TIMEOUT 90)
      endforeach()
      toca_add_test(TARGET toca-ground-filtering-tests NAME gpu-ground-filtering SOURCE tests/render/test_ground_filtering.cpp DISPLAY
        LIBRARIES toca-scene-render glm::glm-header-only LABELS gpu TIMEOUT 60)
      toca_add_test(TARGET toca-ground-filtering-tests NAME scene-reference-ground-filtering DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 90)
      add_dependencies(toca-ground-filtering-tests toca-mega-scene-resources)
      add_dependencies(toca-sun-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-background-tests NAME scene-reference-backgrounds SOURCE tests/render/test_backgrounds.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-background-tests toca-mega-scene-resources)
      foreach(background_track RANGE 0 9)
        toca_add_test(TARGET toca-background-tests NAME scene-reference-backgrounds-track-${background_track} DISPLAY
          ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --track ${background_track}
          LABELS gpu bundled-data extended TIMEOUT 120)
      endforeach()
      toca_add_test(TARGET toca-background-tests NAME scene-reference-backgrounds-all DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --all --woodland
        LABELS gpu bundled-data extended TIMEOUT 600)
      toca_add_test(TARGET toca-mega-woodland-render-tests NAME scene-reference-woodland-croft SOURCE tests/render/test_mega_woodland.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-mega-woodland-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-woodland-render-tests NAME scene-reference-woodland-croft-start DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" start-24
        LABELS gpu bundled-data extended TIMEOUT 180)
      toca_add_test(TARGET toca-mega-woodland-render-tests NAME scene-reference-woodland-snet-llrts2 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" snet-llrts2
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-woodland-render-tests NAME scene-reference-woodland-snet-llrtsx DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" snet-llrtsx
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-woodland-overview-tests NAME scene-reference-woodland-overview SOURCE tests/render/test_woodland_overview.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 600)
      add_dependencies(toca-woodland-overview-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-woodland-oulton-tests NAME scene-reference-woodland-oulton SOURCE tests/render/test_woodland_oulton.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 180)
      add_dependencies(toca-woodland-oulton-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-woodland-oulton-tests NAME scene-reference-woodland-oulton-wedge DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" wedge-14-15
        LABELS gpu bundled-data extended TIMEOUT 180)
      toca_add_test(TARGET toca-woodland-density-tests NAME scene-reference-woodland-density SOURCE tests/render/test_woodland_density.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 180)
      add_dependencies(toca-woodland-density-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation SOURCE tests/render/test_mega_vegetation.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "${CMAKE_CURRENT_BINARY_DIR}/generated/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-mega-vegetation-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-tris07 DISPLAY
        ARGS "${original_data_directory}" "${CMAKE_CURRENT_BINARY_DIR}/generated/mega" TRIS07_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-trge17 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRGE17_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-tris04b DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRIS04B_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-tris02b DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRIS02B_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-trie01 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRIE01_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-trgs18 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRGS18_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-trgs22 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRGS22_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-mega-vegetation-render-tests NAME scene-reference-vegetation-trgs20 DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" TRGS20_1
        LABELS gpu bundled-data extended TIMEOUT 90)
      toca_add_test(TARGET toca-scene-model-mega-tests NAME scene-reference-mega SOURCE tests/render/test_scene_model_mega.cpp
        LIBRARIES toca-scene-render DISPLAY
        ARGS "${original_data_directory}" "${CMAKE_CURRENT_BINARY_DIR}/generated/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-scene-model-prop-tests NAME scene-reference-banner-backing SOURCE tests/render/test_scene_model_props.cpp
        LIBRARIES toca-scene-render DISPLAY
        ARGS --banner-reference "${original_data_directory}" "${CMAKE_CURRENT_BINARY_DIR}/generated/mega"
        LABELS gpu bundled-data TIMEOUT 60)
      toca_add_test(TARGET toca-mega-flags-render-tests NAME scene-reference-mega-flags SOURCE tests/render/test_mega_flags.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      add_dependencies(toca-mega-flags-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-scene-model-prop-tests NAME scene-reference-mega-scenery DISPLAY
        ARGS --scenery-reference "${original_data_directory}" "${CMAKE_CURRENT_BINARY_DIR}/generated/mega" LPEOP5_1 LPEOP2_1 LPEOP3_1 cones posts signs
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-barn-render-tests NAME scene-reference-donington-barn SOURCE tests/render/test_mega_barn.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 90)
      add_dependencies(toca-mega-barn-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-grandstand-render-tests NAME scene-reference-donington-grandstand SOURCE tests/render/test_mega_grandstand.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 90)
      add_dependencies(toca-mega-grandstand-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-cbo-render-tests NAME scene-reference-donington-cbo SOURCE tests/render/test_mega_cbo.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" --model 4
        LABELS gpu bundled-data TIMEOUT 90)
      toca_add_test(TARGET toca-mega-cbo-render-tests NAME scene-reference-donington-cbo-package DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 300)
      add_dependencies(toca-mega-cbo-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-mega-stl-render-tests NAME scene-reference-donington-stl SOURCE tests/render/test_mega_stl.cpp DISPLAY
        LIBRARIES toca-scene-render ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data extended TIMEOUT 90)
      add_dependencies(toca-mega-stl-render-tests toca-mega-scene-resources)
      toca_add_test(TARGET toca-scene-model-bridge-tests NAME scene-reference-dunlop-bridge SOURCE tests/render/test_scene_model_bridge.cpp
        LIBRARIES toca-scene-render DISPLAY
        ARGS "${original_data_directory}" "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
        LABELS gpu bundled-data TIMEOUT 90)
      foreach(model_target IN ITEMS toca-scene-model-mega-tests toca-scene-model-prop-tests)
        target_sources(${model_target} PRIVATE tests/render/scene_models/props.cpp)
      endforeach()
      foreach(model_target IN ITEMS toca-scene-model-mega-tests toca-scene-model-prop-tests toca-scene-model-bridge-tests)
        add_dependencies(${model_target} toca-mega-scene-resources)
      endforeach()
    endif()
    toca_add_test(TARGET toca-car-damage-tests NAME car-damage-reference-render DISPLAY
      ARGS --reference "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
    set(car_lights_mega_args)
    if(vehicle_honda_mode STREQUAL "mega")
      list(APPEND car_lights_mega_args "${CMAKE_CURRENT_BINARY_DIR}/generated/mega")
    endif()
    toca_add_test(TARGET toca-car-lights-render-tests NAME car-lights-reference-render SOURCE tests/render/test_car_lights.cpp DISPLAY
      LIBRARIES toca-scene-render ARGS "${original_data_directory}" ${car_lights_mega_args}
      LABELS gpu bundled-data TIMEOUT 90)
    if(car_lights_mega_args)
      target_sources(toca-car-lights-render-tests PRIVATE ${vehicle_models})
    endif()
    toca_add_test(TARGET toca-scene-model-shadow-tests NAME scene-reference-car-shadow SOURCE tests/render/test_scene_model_shadows.cpp
      LIBRARIES toca-scene-render DISPLAY
      ARGS "${original_data_directory}" LABELS gpu bundled-data TIMEOUT 45)
  endif()
