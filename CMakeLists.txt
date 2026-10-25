cmake_minimum_required(VERSION 3.25)
project(toca LANGUAGES C CXX)
include(cmake/CompilerCache.cmake)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TOCA_RUN_ARGS "" CACHE STRING "Arguments for the native application run target")
option(TOCA_DISTRIBUTION "Build a relocatable Release distribution" OFF)
separate_arguments(TOCA_RUN_ARGUMENTS NATIVE_COMMAND "${TOCA_RUN_ARGS}")

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
toca_setup_dependencies()
include(cmake/AudioDependencies.cmake)
toca_setup_audio_dependency()
include(cmake/ModelDependencies.cmake)
toca_setup_model_dependencies()
include(cmake/NetworkDependencies.cmake)
toca_setup_network_dependency()
include(cmake/Formatting.cmake)
toca_setup_formatting()

add_library(toca-formats STATIC src/formats/cm3d.cpp src/formats/glb.cpp src/formats/bfp.cpp src/formats/terrain.cpp src/formats/hpc.cpp src/formats/static_model.cpp)
target_link_libraries(toca-formats PUBLIC toca-project-options PRIVATE toca-cgltf meshoptimizer)
target_include_directories(toca-formats PUBLIC src)

set(TOCA_HOST_MESHOPT_ENCODER "" CACHE FILEPATH "Host meshopt encoder for cross-compiling Mega assets")
if(CMAKE_CROSSCOMPILING AND TOCA_HOST_MESHOPT_ENCODER)
  if(NOT EXISTS "${TOCA_HOST_MESHOPT_ENCODER}" OR IS_DIRECTORY "${TOCA_HOST_MESHOPT_ENCODER}")
    message(FATAL_ERROR "TOCA_HOST_MESHOPT_ENCODER must name an existing host executable")
  endif()
  add_executable(toca-meshopt-encode IMPORTED GLOBAL)
  set_target_properties(toca-meshopt-encode PROPERTIES IMPORTED_LOCATION "${TOCA_HOST_MESHOPT_ENCODER}")
else()
  add_executable(toca-meshopt-encode EXCLUDE_FROM_ALL tools/meshopt_encode.cpp)
  target_link_libraries(toca-meshopt-encode PRIVATE toca-project-options meshoptimizer)
endif()

add_library(toca-assets STATIC src/assets/store.cpp)
target_link_libraries(toca-assets PUBLIC toca-formats)
target_include_directories(toca-assets PUBLIC src)

add_library(toca-state STATIC src/settings/storage.cpp)
target_link_libraries(toca-state PUBLIC toca-project-options)
target_include_directories(toca-state PUBLIC src)

add_library(toca-network STATIC src/network/transport.cpp src/network/protocol.cpp src/network/session.cpp src/network/race_state.cpp src/network/content.cpp)
target_include_directories(toca-network PUBLIC src)
target_link_libraries(toca-network PUBLIC toca-project-options PRIVATE ENet::ENet toca-assets)
include(cmake/NativeData.cmake)
if(WIN32)
  target_link_libraries(toca-network PRIVATE iphlpapi)
endif()

add_library(toca-images STATIC src/image/decode.cpp src/image/artwork.cpp)
target_link_libraries(toca-images PUBLIC toca-project-options SDL3::SDL3 PRIVATE webpdecoder toca-assets)
target_include_directories(toca-images PUBLIC src)

add_library(toca-frontend-resources STATIC src/frontend/resources.cpp)
target_link_libraries(toca-frontend-resources PUBLIC toca-assets toca-images)
target_include_directories(toca-frontend-resources PUBLIC src)

add_library(toca-scene STATIC src/scene/setup.cpp src/scene/resources.cpp src/scene/pit_route.cpp src/scene/props.cpp src/scene/flag_animation.cpp src/scene/horizon_actors.cpp src/scene/prop_fragments.cpp src/scene/contact_effects.cpp src/scene/ground_scrape.cpp src/scene/weather.cpp src/scene/sun.cpp src/scene/surface_effect_resources.cpp src/scene/surface_effects.cpp src/scene/skid_marks.cpp src/scene/engine_effects.cpp src/scene/visibility.cpp src/scene/fleet_scene.cpp src/scene/frame_journal.cpp src/scene/camera.cpp src/scene/cockpit.cpp src/scene/placement.cpp src/scene/body.cpp src/scene/car_visual.cpp src/scene/car_lights.cpp src/scene/car_light_glow.cpp src/scene/car_shadow.cpp src/scene/car_damage.cpp src/scene/shadow_support.cpp src/scene/follow_camera.cpp src/scene/barriers.cpp src/scene/qualifying.cpp src/scene/finish_camera.cpp src/scene/pause.cpp)
target_link_libraries(toca-scene PUBLIC toca-frontend-resources PRIVATE glm::glm-header-only)
target_include_directories(toca-scene PUBLIC src)

add_library(toca-simulation STATIC src/scene/championship_finale.cpp src/simulation/time.cpp src/simulation/source_scene_frame.cpp src/simulation/controls.cpp src/simulation/motion.cpp src/simulation/surface_roughness.cpp src/simulation/rollover.cpp src/simulation/barrier_contact.cpp src/simulation/damage.cpp src/simulation/damage_parts.cpp src/simulation/prop_contacts.cpp src/simulation/race_world.cpp src/simulation/session.cpp src/race/race_progress.cpp src/race/championship.cpp src/settings/championship.cpp src/settings/championship_records.cpp src/simulation/rivals.cpp src/simulation/cold_grid.cpp src/simulation/linkup_fleet.cpp src/simulation/car_boost.cpp src/simulation/car_contacts.cpp src/simulation/contact_candidates.cpp src/simulation/time_trial.cpp src/simulation/ai_driver.cpp src/simulation/pit_lane.cpp)
target_link_libraries(toca-simulation PUBLIC toca-project-options toca-scene toca-state)
target_include_directories(toca-simulation PUBLIC src)

# Diagnostic reader for private original recordings; uses the live motion consumer.
add_executable(toca-reference-replay EXCLUDE_FROM_ALL tests/simulation/replay_reference.cpp)
target_link_libraries(toca-reference-replay PRIVATE toca-simulation)
add_executable(toca-fleet-reference-replay EXCLUDE_FROM_ALL tests/simulation/replay_fleet_reference.cpp src/audio/spatial.cpp
  tests/simulation/fleet_reference/audio_replay.cpp tests/simulation/fleet_reference/binary_io.cpp
  tests/simulation/fleet_reference/clock_transactions.cpp tests/simulation/fleet_reference/record.cpp
  tests/simulation/fleet_reference/startup.cpp tests/simulation/fleet_reference/options.cpp
  tests/simulation/fleet_reference/combined_replay.cpp tests/simulation/fleet_reference/diagnostics.cpp
  tests/simulation/fleet_reference/driver_comparison.cpp tests/simulation/fleet_reference/physical_fleet.cpp
  tests/simulation/fleet_reference/retained_state.cpp tests/simulation/fleet_reference/session_comparison.cpp)
target_link_libraries(toca-fleet-reference-replay PRIVATE toca-simulation)
add_executable(toca-scene-journal-replay EXCLUDE_FROM_ALL tests/scene/replay_frame_journal.cpp)
target_link_libraries(toca-scene-journal-replay PRIVATE toca-simulation)

# One shader source for all SDL GPU backends; no runtime compiler dependency.
set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(shader IN ITEMS legacy.vert legacy.frag world.vert world-reference.vert)
  set(shader_source "${shader}")
  set(shader_defines)
  if(shader STREQUAL "world-reference.vert")
    set(shader_source world.vert)
    list(APPEND shader_defines -DTOCA_REFERENCE_RASTER=1)
  endif()
  if(shader MATCHES "\\.vert$")
    set(shader_stage vertex)
  else()
    set(shader_stage fragment)
  endif()
  foreach(format IN ITEMS SPIRV DXIL MSL)
    if(format STREQUAL "SPIRV")
      set(extension spv)
    else()
      string(TOLOWER "${format}" extension)
    endif()
    set(output "${shader_dir}/${shader}.${extension}")
    add_custom_command(OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_dir}"
      COMMAND "${TOCA_SHADERCROSS}" "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${shader_source}.hlsl"
        -s HLSL -d "${format}" -t "${shader_stage}" -e main -o "${output}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders" ${shader_defines}
      DEPENDS "src/shaders/${shader_source}.hlsl" "src/shaders/source_precision.hlsl" ${TOCA_SHADERCROSS_DEPENDENCY}
      VERBATIM)
    list(APPEND compiled_shaders "${output}")
  endforeach()
endforeach()
add_custom_target(toca-shaders DEPENDS ${compiled_shaders})
add_library(toca-render INTERFACE)
target_include_directories(toca-render INTERFACE src)
target_link_libraries(toca-render INTERFACE toca-project-options SDL3::SDL3 toca-platform)
if(NOT TOCA_DISTRIBUTION)
  target_compile_definitions(toca-render INTERFACE TOCA_SHADER_DIR="${shader_dir}")
endif()
add_dependencies(toca-render toca-shaders)

# Pose/lighting preparation is independent of the GPU and its generated shaders.
add_library(toca-scene-preparation STATIC src/scene/model_pose.cpp src/scene/lighting.cpp)
target_link_libraries(toca-scene-preparation PUBLIC toca-scene)

add_library(toca-scene-render STATIC src/scene/view.cpp src/scene/terrain_view.cpp src/scene/render_geometry.cpp src/scene/mega_ground_uv.cpp src/scene/mega_ground_transitions.cpp src/scene/mega_ground_edges.cpp src/scene/presentation.cpp src/scene/championship_finale_view.cpp src/scene/weather_render.cpp src/scene/sun_render.cpp src/scene/surface_effects_render.cpp src/scene/skid_marks_render.cpp src/scene/engine_effects_render.cpp src/scene/loading.cpp src/scene/hud.cpp src/scene/hud_artwork.cpp src/scene/qualifying_view.cpp src/scene/pause_view.cpp)
target_link_libraries(toca-scene-render PUBLIC toca-scene-preparation toca-render toca-frontend)
# Source TL preparation and its shader oracle round multiply/add separately.
# Preserve that contract in optimized builds, including header-only consumers.
if(MSVC)
  target_compile_options(toca-scene PUBLIC /fp:strict)
  target_compile_options(toca-scene-preparation PUBLIC /fp:strict)
  target_compile_options(toca-scene-render PUBLIC /fp:strict)
else()
  target_compile_options(toca-scene PUBLIC
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>")
  target_compile_options(toca-scene-preparation PUBLIC
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>")
  target_compile_options(toca-scene-render PUBLIC
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>")
endif()

add_library(toca-frontend STATIC src/frontend/builtin_data.cpp
  src/frontend/car_name.cpp src/frontend/cheats.cpp src/frontend/controller.cpp src/frontend/font.cpp src/frontend/menus.cpp src/frontend/network_lobby.cpp src/frontend/network_status.cpp src/frontend/championship_presentation.cpp src/frontend/records.cpp
  src/frontend/menu_transition.cpp src/frontend/canvas.cpp src/frontend/selection_resources.cpp src/frontend/track_resources.cpp src/frontend/view.cpp)
target_link_libraries(toca-frontend PUBLIC toca-frontend-resources toca-render glm::glm-header-only)
target_include_directories(toca-frontend PUBLIC src)

add_library(toca-app-support STATIC
  src/settings/preferences.cpp src/settings/extras.cpp src/settings/lap_records.cpp src/app/options.cpp src/app/gamepad_feedback.cpp src/app/wheel_feedback.cpp src/app/wheel_haptics.cpp src/app/input.cpp src/app/input_devices.cpp src/app/driving_input.cpp src/app/paths.cpp src/app/step_input.cpp)
target_include_directories(toca-app-support PUBLIC src)
target_link_libraries(toca-app-support PUBLIC toca-project-options toca-state SDL3::SDL3)

add_library(toca-platform STATIC src/platform/display.cpp)
target_include_directories(toca-platform PUBLIC src)
target_link_libraries(toca-platform PUBLIC toca-project-options SDL3::SDL3)

add_library(toca-audio STATIC src/audio/service.cpp src/audio/sample_file.cpp src/audio/championship.cpp src/audio/spatial.cpp src/audio/pause.cpp src/audio/frontend.cpp src/audio/engine.cpp src/audio/rivals.cpp src/audio/driving.cpp src/audio/impact.cpp src/audio/scrape.cpp src/audio/start.cpp src/audio/finish.cpp src/audio/pit.cpp src/audio/horn.cpp src/audio/weather.cpp)
target_include_directories(toca-audio PUBLIC src)
target_link_libraries(toca-audio PUBLIC toca-project-options SDL3_mixer::SDL3_mixer-static SDL3::SDL3 toca-assets)

add_executable(toca-game src/main.cpp src/app/application.cpp src/app/network.cpp
  src/app/runtime_resources.cpp src/app/audio_timeline.cpp src/app/scene_session.cpp
  src/app/scene_session_audio.cpp src/app/scene_session_presentation.cpp src/app/scene_session_frame.cpp src/app/scene_session_step.cpp
  src/app/finale_session.cpp src/app/trace.cpp)
target_link_libraries(toca-game PRIVATE toca-app-support toca-platform toca-render toca-audio toca-frontend toca-scene-render toca-simulation toca-network)
if(WIN32)
  set_target_properties(toca-game PROPERTIES WIN32_EXECUTABLE TRUE)
  enable_language(RC)
  target_sources(toca-game PRIVATE src/platform/windows.rc)
  target_include_directories(toca-game PRIVATE "${PROJECT_SOURCE_DIR}/assets/ui/original")
  set_property(SOURCE src/platform/windows.rc APPEND PROPERTY OBJECT_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/ui/original/icon.ico")
  add_custom_command(TARGET toca-game POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_RUNTIME_DLLS:toca-game>" "$<TARGET_FILE_DIR:toca-game>"
    COMMAND_EXPAND_LISTS VERBATIM)
endif()
include(cmake/GameData.cmake)
include(cmake/AudioData.cmake)
# Bundle only the approved converted soundtrack alongside each built executable.
foreach(track RANGE 2 6)
  list(APPEND music_assets "${PROJECT_SOURCE_DIR}/assets/music/track0${track}.ogg")
endforeach()
add_custom_target(toca-resources
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:toca-game>/assets/music"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${music_assets} "$<TARGET_FILE_DIR:toca-game>/assets/music"
  DEPENDS ${music_assets} VERBATIM)
add_dependencies(toca-game toca-resources)
# Mega models are generated locally; each missing override uses original Very High.
# Original inputs come from the versioned, CRC32-verified BFP bundle.
option(TOCA_MEGA_MODELS "Build optional Mega GLB models with embedded WebP textures" ON)
if(TOCA_MEGA_MODELS AND CMAKE_CROSSCOMPILING AND NOT TOCA_HOST_MESHOPT_ENCODER)
  message(FATAL_ERROR "Cross-compiling Mega assets requires TOCA_HOST_MESHOPT_ENCODER from a native build")
endif()
include(cmake/VehicleModels.cmake)
include(cmake/CockpitModels.cmake)
include(cmake/FinaleModels.cmake)
if(TOCA_MEGA_MODELS)
  file(GLOB mega_authored_textures CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/models/mega/textures/*.webp")
  foreach(prefix IN ITEMS donl silv thru bran oult dons crof knoc snet land)
    list(APPEND mega_scene_sources "${original_data_directory}/data/${prefix}free.bfp")
    list(APPEND mega_scene_sources "${original_data_directory}/data/${prefix}hor.bfp")
  endforeach()
  # The producer verifies its complete manifest inventory, including deleted outputs.
  # Keep one input list for freshness checks and build ordering.
  set(mega_scene_inputs
    ${mega_scene_sources}
    "${original_data_directory}/data/image640.bfp"
    "${PROJECT_SOURCE_DIR}/tools/build-mega-assets.py" "${PROJECT_SOURCE_DIR}/tools/mega_geometry.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_aircraft.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_static_aircraft.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_static_platform.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_static_vectra.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_static_fleet.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_gargoyle.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_vegetation.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_woodland.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_dunlop.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_barn.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_grandstand.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo4.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo5.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo6.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo1.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo3.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_cbo2.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_spectators.py"
    "${PROJECT_SOURCE_DIR}/tools/mega_sheep.py"
    "${PROJECT_SOURCE_DIR}/tools/vehicle_model.py" "${PROJECT_SOURCE_DIR}/tools/renault_geometry.py"
    "${PROJECT_SOURCE_DIR}/tools/vehicle_geometry.py"
    "${PROJECT_SOURCE_DIR}/tools/checksum.py"
    "${PROJECT_SOURCE_DIR}/tools/vehicle_glb.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_textures.py"
    "${PROJECT_SOURCE_DIR}/assets/models/mega/prop-materials.webp"
    "${PROJECT_SOURCE_DIR}/assets/models/mega/aircraft-skin.webp"
    "${PROJECT_SOURCE_DIR}/assets/models/mega/vegetation-generation.json"
    "${PROJECT_SOURCE_DIR}/assets/models/mega/texture-replacements.json" ${mega_authored_textures})
  add_custom_target(toca-mega-scene-resources
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-mega-assets.py"
      --source "${original_data_directory}"
      --output "${CMAKE_CURRENT_BINARY_DIR}/generated/mega"
      --meshopt-encoder "$<TARGET_FILE:toca-meshopt-encode>"
      --update --inputs ${mega_scene_inputs}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-mega-assets.py"
      --sync-scene "${CMAKE_CURRENT_BINARY_DIR}/generated/mega/scene"
      "$<TARGET_FILE_DIR:toca-game>/assets/models/mega/scene"
    DEPENDS ${mega_scene_inputs} toca-meshopt-encode VERBATIM)
  add_dependencies(toca-model-resources toca-mega-scene-resources)
  add_dependencies(toca-mega-scene-resources toca-original-data)
  target_compile_definitions(toca-game PRIVATE TOCA_MEGA_MODELS=1)
endif()
# Original images form the base; equal logical names in mega/ override them.
set(ui_asset_directory "${PROJECT_SOURCE_DIR}/assets/ui")
set(original_startup_assets
  "${ui_asset_directory}/original/title.webp"
  "${ui_asset_directory}/original/cpyright.webp")
set(mega_menu_assets
  "${ui_asset_directory}/mega/title-wide.webp"
  "${ui_asset_directory}/mega/cpyright.webp"
  "${ui_asset_directory}/mega/menu-background-wide.webp"
  "${ui_asset_directory}/mega/menu-background-renault-wide.webp"
  "${ui_asset_directory}/mega/menu-background-honda-wide.webp"
  "${ui_asset_directory}/mega/menu-background-volvo-wide.webp"
  "${ui_asset_directory}/mega/menu-background-ford-wide.webp"
  "${ui_asset_directory}/mega/menu-background-nissan-wide.webp"
  "${ui_asset_directory}/mega/menu-background-peugeot-wide.webp"
  "${ui_asset_directory}/mega/menu-background-vectra-wide.webp"
  "${ui_asset_directory}/mega/menu-frame.webp")
set(hud_artwork_dir "${ui_asset_directory}/mega/hud")
file(GLOB hud_artwork_assets CONFIGURE_DEPENDS "${hud_artwork_dir}/*.webp")
file(GLOB menu_icon_assets CONFIGURE_DEPENDS "${ui_asset_directory}/mega/menu-icons/*.webp")
set(mega_loading_assets)
foreach(name IN ITEMS donington silverstone thruxton brands-hatch oulton-park
    donington-short croft knockhill snetterton lavaland font sides)
  list(APPEND mega_loading_assets "${ui_asset_directory}/mega/loading/${name}.webp")
endforeach()
set(original_garage_assets)
set(mega_garage_assets)
foreach(variant RANGE 1 4)
  if(variant EQUAL 1)
    set(garage_first_car 0)
  else()
    set(garage_first_car 7)
  endif()
  foreach(car RANGE ${garage_first_car} 9)
    list(APPEND original_garage_assets "${ui_asset_directory}/original/garage/gar${variant}-car${car}.webp")
    list(APPEND mega_garage_assets "${ui_asset_directory}/mega/garage/gar${variant}-car${car}.webp")
  endforeach()
endforeach()
# Manual rendering/import/composition stays outside the build.
add_executable(toca-garage-car-export EXCLUDE_FROM_ALL tools/export-garage-car.cpp)
target_link_libraries(toca-garage-car-export PRIVATE toca-scene-render)
# One target owns the UI tree, so removed/moved files cannot survive an incremental build.
add_custom_target(toca-ui-resources
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:toca-game>/assets/ui"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:toca-game>/assets/ui/original/garage"
    "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/garage"
    "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/hud"
    "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/menu-icons"
    "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/loading"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${original_startup_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/original"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${original_garage_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/original/garage"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${mega_menu_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${mega_garage_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/garage"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${hud_artwork_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/hud"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${menu_icon_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/menu-icons"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${mega_loading_assets} "$<TARGET_FILE_DIR:toca-game>/assets/ui/mega/loading"
  DEPENDS ${original_startup_assets} ${original_garage_assets} ${mega_menu_assets} ${mega_garage_assets}
    ${hud_artwork_assets} ${menu_icon_assets} ${mega_loading_assets} VERBATIM)
add_dependencies(toca-frontend toca-ui-resources)
add_custom_target(run
  COMMAND $<TARGET_FILE:toca-game> ${TOCA_RUN_ARGUMENTS}
  DEPENDS toca-game WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  USES_TERMINAL VERBATIM)

include(cmake/Packaging.cmake)

if(BUILD_TESTING)
  include(cmake/Testing.cmake)
  toca_register_tests()
endif()
