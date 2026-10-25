  set(bundled_runtime_data "$<TARGET_FILE_DIR:toca-game>/assets/original-data")
  toca_add_test(TARGET toca-hpc-surface-tests NAME hpc-surface SOURCE tests/formats/test_hpc.cpp
    LIBRARIES toca-formats LABELS unit parser)
  toca_add_test(TARGET toca-network-protocol-tests NAME network-protocol SOURCE tests/network/test_protocol.cpp
    LIBRARIES toca-network LABELS unit parser)
  toca_add_test(TARGET toca-network-transport-tests NAME network-transport SOURCE tests/network/test_transport.cpp
    LIBRARIES toca-network ENet::ENet LABELS unit TIMEOUT 30)
  toca_add_test(TARGET toca-network-transport-tests NAME network-transport-probe
    ARGS --probe LABELS extended TIMEOUT 180)
  toca_add_test(TARGET toca-network-session-tests NAME network-session SOURCE tests/network/test_session.cpp
    LIBRARIES toca-network ENet::ENet LABELS unit TIMEOUT 30)
  target_sources(toca-network-session-tests PRIVATE tests/network/session/support.cpp
    tests/network/session/lobby.cpp tests/network/session/protocol.cpp
    tests/network/session/loading.cpp tests/network/session/link.cpp)
  toca_add_test(TARGET toca-network-race-state-tests NAME network-race-state SOURCE tests/network/test_race_state.cpp
    LIBRARIES toca-network LABELS unit)
  toca_add_test(TARGET toca-car-boost-tests NAME car-boost SOURCE tests/simulation/test_car_boost.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-network-motion-tests NAME network-motion SOURCE tests/simulation/test_network_motion.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-network-motion-tests NAME fleet-scene-consumers ARGS --scene-context LABELS unit)
  toca_add_test(TARGET toca-network-progress-tests NAME network-progress SOURCE tests/simulation/test_network_progress.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-network-results-tests NAME network-results SOURCE tests/simulation/test_network_results.cpp
    LIBRARIES toca-simulation LABELS unit)
  toca_add_test(TARGET toca-network-lobby-tests NAME frontend-network-lobby SOURCE tests/frontend/test_network_lobby.cpp
    LIBRARIES toca-frontend LABELS unit)
  toca_add_test(TARGET toca-network-content-tests NAME network-content SOURCE tests/network/test_content.cpp
    LIBRARIES toca-network LABELS unit)
  toca_add_test(TARGET toca-builtin-data-tests NAME native-builtin-data SOURCE tests/frontend/test_builtin_data.cpp
    LIBRARIES toca-frontend toca-simulation LABELS unit)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    toca_add_test(TARGET toca-native-network-connect-tests NAME native-network-connect SOURCE tests/integration/test_network_connect.py
      LABELS gpu audio bundled-data integration TIMEOUT 60
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-connect-tests toca-game)
    toca_add_test(TARGET toca-native-network-lobby-tests NAME native-network-lobby SOURCE tests/integration/test_network_lobby.py
      LABELS gpu audio bundled-data integration TIMEOUT 90
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-lobby-tests toca-game)
    add_executable(toca-network-loading-peer tests/network/loading_peer.cpp)
    target_link_libraries(toca-network-loading-peer PRIVATE toca-network toca-assets)
    toca_add_test(TARGET toca-native-network-recovery-tests NAME native-network-recovery SOURCE tests/integration/test_network_recovery.py
      LABELS gpu audio bundled-data integration TIMEOUT 120
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" $<TARGET_FILE:toca-network-loading-peer>)
    add_dependencies(toca-native-network-recovery-tests toca-game toca-network-loading-peer)
    set_tests_properties(native-network-recovery PROPERTIES PROCESSORS 2)
    toca_add_test(TARGET toca-native-network-smoke-tests NAME native-network-smoke SOURCE tests/integration/test_network_smoke.py
      LABELS gpu audio bundled-data integration TIMEOUT 90
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-smoke-tests toca-game)
    set_tests_properties(native-network-smoke PROPERTIES PROCESSORS 2)
    toca_add_test(TARGET toca-native-network-race-tests NAME native-network-race SOURCE tests/integration/test_network_race.py
      LABELS gpu audio bundled-data integration extended TIMEOUT 480
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-race-tests toca-game)
    set_tests_properties(native-network-connect native-network-lobby native-network-race PROPERTIES PROCESSORS 2)
    toca_add_test(TARGET toca-native-network-ai-tests NAME native-network-ai SOURCE tests/integration/test_network_ai.py
      LABELS gpu audio bundled-data integration TIMEOUT 60
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-ai-tests toca-game)
    toca_add_test(TARGET toca-native-network-capacity-tests NAME native-network-capacity SOURCE tests/integration/test_network_capacity.py
      LABELS gpu audio bundled-data integration TIMEOUT 180
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}")
    add_dependencies(toca-native-network-capacity-tests toca-game)
    toca_add_test(TARGET toca-native-network-capacity-tests NAME native-network-soak
      LABELS gpu audio bundled-data integration extended TIMEOUT 900
      ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" --soak)
    set_tests_properties(native-network-capacity native-network-soak PROPERTIES PROCESSORS 8)
  endif()
  toca_add_test(TARGET toca-scenery-inventory-tests NAME scenery-inventory SOURCE tests/tools/test_scenery_inventory.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-background-inventory-tests NAME background-inventory SOURCE tests/tools/test_background_inventory.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-mega-vegetation-tests NAME mega-vegetation SOURCE tests/tools/test_mega_vegetation.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-mega-aircraft-tests NAME mega-aircraft SOURCE tests/tools/test_mega_aircraft.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-mega-geometry-tests NAME mega-geometry SOURCE tests/tools/test_mega_geometry.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-finale-model-tests NAME finale-models SOURCE tests/tools/test_finale_models.py
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-mega-dunlop-tests NAME mega-dunlop SOURCE tests/tools/test_mega_dunlop.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-mega-barn-tests NAME mega-barn SOURCE tests/tools/test_mega_barn.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-mega-grandstand-tests NAME mega-grandstand SOURCE tests/tools/test_mega_grandstand.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-mega-cbo-tests NAME mega-cbo SOURCE tests/tools/test_mega_cbo.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-mega-woodland-tests NAME mega-woodland SOURCE tests/tools/test_mega_woodland.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-woodland-audit-tests NAME woodland-audit SOURCE tests/tools/test_woodland_audit.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-mega-asset-tests NAME mega-assets SOURCE tests/tools/test_mega_assets.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-renault-glb-generator-tests NAME renault-glb-generator SOURCE tests/tools/test_renault_glb.py
    ARGS --meshopt-encoder $<TARGET_FILE:toca-meshopt-encode> LABELS unit tooling TIMEOUT 30)
  add_dependencies(toca-renault-glb-generator-tests toca-meshopt-encode)
  toca_add_test(TARGET toca-glb-tests NAME formats-glb SOURCE tests/formats/test_glb.cpp
    LIBRARIES toca-formats LABELS unit parser)
  toca_add_test(TARGET toca-renault-texture-tests NAME renault-textures SOURCE tests/tools/test_renault_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-renault-generator-tests NAME renault-model-generator SOURCE tests/tools/test_renault_model.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-honda-generator-tests NAME honda-model-generator SOURCE tests/tools/test_honda_model.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-honda-texture-tests NAME honda-textures SOURCE tests/tools/test_honda_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-audi-generator-tests NAME audi-model-generator SOURCE tests/tools/test_audi_model.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-audi-texture-tests NAME audi-textures SOURCE tests/tools/test_audi_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-vectra-generator-tests NAME vectra-model-generator SOURCE tests/tools/test_vectra_model.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 30)
  toca_add_test(TARGET toca-vectra-texture-tests NAME vectra-textures SOURCE tests/tools/test_vectra_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-vectra2-generator-tests NAME vectra2-model-generator SOURCE tests/tools/test_vectra2_geometry.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 60)
  toca_add_test(TARGET toca-vectra2-texture-tests NAME vectra2-textures SOURCE tests/tools/test_vectra2_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-volvo-generator-tests NAME volvo-model-generator SOURCE tests/tools/test_volvo_geometry.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-volvo-texture-tests NAME volvo-textures SOURCE tests/tools/test_volvo_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-ford-generator-tests NAME ford-model-generator SOURCE tests/tools/test_ford_geometry.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-ford-texture-tests NAME ford-textures SOURCE tests/tools/test_ford_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-nissan-generator-tests NAME nissan-model-generator SOURCE tests/tools/test_nissan_geometry.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-nissan-texture-tests NAME nissan-textures SOURCE tests/tools/test_nissan_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-peugeot-generator-tests NAME peugeot-model-generator SOURCE tests/tools/test_peugeot_geometry.py
    ENVIRONMENT "TOCA_DATA_ROOT=${original_data_directory}"
    LABELS unit tooling bundled-data TIMEOUT 90)
  toca_add_test(TARGET toca-peugeot-texture-tests NAME peugeot-textures SOURCE tests/tools/test_peugeot_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-tank-generator-tests NAME tank-model-generator SOURCE tests/tools/test_tank_geometry.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-tank-texture-tests NAME tank-textures SOURCE tests/tools/test_tank_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-flexmobile-generator-tests NAME flexmobile-model-generator SOURCE tests/tools/test_flexmobile_geometry.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-flexmobile-texture-tests NAME flexmobile-textures SOURCE tests/tools/test_flexmobile_textures.py
    LABELS unit tooling TIMEOUT 60)
  toca_add_test(TARGET toca-vehicle-generator-tests NAME vehicle-model-generator SOURCE tests/tools/test_vehicle_models.py
    LABELS unit tooling TIMEOUT 180)
  toca_add_test(TARGET toca-vehicle-texture-tests NAME vehicle-textures SOURCE tests/tools/test_vehicle_textures.py
    LABELS unit tooling TIMEOUT 30)
  toca_add_test(TARGET toca-vehicle-sync-tests NAME vehicle-model-sync SOURCE tests/tools/test_vehicle_sync.py
    LABELS unit tooling TIMEOUT 30)
  foreach(vehicle_key IN LISTS vehicle_test_keys)
    set(vehicle_model "${vehicle_${vehicle_key}_model}")
    set(vehicle_render_id "${vehicle_${vehicle_key}_render_id}")
    set(vehicle_mode "${vehicle_${vehicle_key}_mode}")
    set(vehicle_test_name "vehicle-model-${vehicle_key}")
    set(vehicle_test_options)
    set(vehicle_test_source)
    if(NOT TARGET toca-vehicle-model-tests)
      set(vehicle_test_source SOURCE tests/render/test_vehicle_model.cpp LIBRARIES toca-scene-render)
    endif()
    if(vehicle_mode STREQUAL "reference")
      string(APPEND vehicle_test_name "-reference")
      list(APPEND vehicle_test_options --reference)
    endif()
    toca_add_test(TARGET toca-vehicle-model-tests NAME "${vehicle_test_name}" ${vehicle_test_source}
      DISPLAY LABELS gpu bundled-data TIMEOUT 90
      ARGS "${original_data_directory}" "${vehicle_model}" --render-id "${vehicle_render_id}" ${vehicle_test_options})
    add_dependencies(toca-vehicle-model-tests toca-model-resources)
    if(TARGET toca-${vehicle_key}-reference-model)
      add_dependencies(toca-vehicle-model-tests toca-${vehicle_key}-reference-model)
    endif()
    if(UNIX AND NOT APPLE AND vehicle_mode STREQUAL "mega" AND vehicle_render_id LESS 11)
      set(vehicle_native_source)
      if(NOT TARGET toca-native-vehicle-tests)
        set(vehicle_native_source SOURCE tests/integration/test_vehicle_model.py)
      endif()
      toca_add_test(TARGET toca-native-vehicle-tests NAME native-${vehicle_key}-model ${vehicle_native_source}
        DISPLAY LABELS gpu audio bundled-data integration TIMEOUT 60
        ARGS $<TARGET_FILE:toca-game> "${bundled_runtime_data}" "${vehicle_render_id}")
      add_dependencies(toca-native-vehicle-tests toca-game)
    endif()
  endforeach()
