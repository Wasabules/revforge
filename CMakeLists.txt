cmake_minimum_required(VERSION 3.20)
project(revforge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PRESETS_JSON ${CMAKE_CURRENT_SOURCE_DIR}/presets/presets.json)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
add_custom_command(
  OUTPUT ${GENERATED_DIR}/presets_data.inc
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_presets.py ${PRESETS_JSON} ${GENERATED_DIR}/presets_data.inc
  DEPENDS ${PRESETS_JSON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_presets.py
)

add_library(revforge_core STATIC
  core/src/engine_config.cpp
  core/src/engine_sound.cpp
  core/src/exhaust_line.cpp
  core/src/acoustics.cpp
  core/src/sound_sheet.cpp
  core/src/starter_sound.cpp
  core/src/gearbox.cpp
  core/src/gearbox_sound.cpp
  core/src/driving_model.cpp
  core/src/shift_logic.cpp
  core/src/driver_scenarios.cpp
  core/src/powertrain.cpp
  core/src/turbo.cpp
  core/src/turbo_pipe.cpp
  core/src/consumption.cpp
  core/src/presets.cpp
  ${GENERATED_DIR}/presets_data.inc
)
target_include_directories(revforge_core PUBLIC core/include PRIVATE core/src ${GENERATED_DIR})
# Résultat identique d'une plateforme à l'autre : pas de contraction ni de calcul approché.
target_compile_options(revforge_core PUBLIC -ffp-contract=off -fno-fast-math)
if(NOT EMSCRIPTEN)
  target_compile_options(revforge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EMSCRIPTEN)
  add_executable(revforge_wasm wasm/exports.cpp)
  target_link_libraries(revforge_wasm PRIVATE revforge_core)
  set_target_properties(revforge_wasm PROPERTIES OUTPUT_NAME revforge SUFFIX .wasm)
  target_compile_options(revforge_core PUBLIC -fno-exceptions -fno-rtti)
  target_compile_options(revforge_wasm PRIVATE -fno-exceptions -fno-rtti)
  target_link_options(revforge_wasm PRIVATE
    --no-entry -O3 -sSTANDALONE_WASM -sFILESYSTEM=0 -sMALLOC=emmalloc
    -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=4MB -sSTACK_SIZE=128KB
    -sERROR_ON_UNDEFINED_SYMBOLS=1
    "-sEXPORTED_FUNCTIONS=_rf_init,_rf_buffer,_rf_render,_rf_set_config,_rf_set_electric,_rf_set_car,_rf_set_mode,_rf_set_shift_mode,_rf_set_scenario,_rf_leg_kind,_rf_leg_target,_rf_grade_pct,_rf_underpowered,_rf_scenario_seconds,_rf_set_controls,_rf_set_transmission,_rf_shift,_rf_crank,_rf_set_listener,_rf_set_manual_rpm,_rf_set_volume,_rf_rpm,_rf_speed,_rf_gear,_rf_throttle,_rf_load,_rf_boost,_rf_turbine,_rf_turbo_release,_rf_turbo_rpm,_rf_turbo_charge,_rf_turbo_plenum,_rf_turbo_outlet,_rf_turbo_inlet,_rf_turbo_exhaust,_rf_turbo_wastegate,_rf_turbo_blowoff,_rf_turbo_flow,_rf_turbo_out_c,_rf_turbo_cool_c,_rf_turbo_gas_c,_rf_turbo_wheel_mm,_rf_turbo_inertia,_rf_torque,_rf_power,_rf_clutch,_rf_wheel_rpm,_rf_slip,_rf_pops,_rf_cut,_rf_shifting,_rf_cranking,_rf_stalled,_rf_lugging,_rf_starter_direct,_rf_starter_kw,_rf_starter_ring,_rf_starter_pinion,_rf_starter_helix,_rf_torque_at,_rf_power_at,_rf_peak_torque,_rf_peak_torque_rpm,_rf_peak_power,_rf_peak_power_rpm,_rf_idle_rpm,_rf_redline,_rf_gears,_rf_overall_ratio,_rf_zero_to_100,_rf_top_speed,_rf_clutch_input_rpm,_rf_clutch_slip_kw,_rf_clutch_torque,_rf_line_impulse,_rf_line_impulse_length,_rf_set_gearbox,_rf_set_gear_ratio,_rf_set_clutch_assist,_rf_repair_gearbox,_rf_input_rpm,_rf_output_rpm,_rf_meshed,_rf_shift_step,_rf_grinding,_rf_overload,_rf_box_wear,_rf_box_broken,_rf_reverse_ratio,_rf_box_ratio,_rf_box_driver,_rf_box_driven,_rf_box_contact,_rf_box_mesh_hz,_rf_box_final,_rf_box_final_pinion,_rf_box_final_ring,_rf_box_input_driver,_rf_box_input_driven,_rf_box_direct,_rf_box_kind,_rf_box_limit,_rf_box_module,_rf_box_helix,_rf_box_center,_rf_box_inertia,_rf_box_efficiency,_rf_box_pending,_rf_box_cone_slip,_rf_box_rattle,_rf_box_loose,_rf_box_three_shafts,_rf_box_hypoid,_rf_box_face,_rf_box_dog_teeth,_rf_box_cone_radius,_rf_box_reverse_teeth,_rf_wheel_radius,_rf_buffer_right,_rf_render_stereo,_rf_set_listener_position,_rf_set_windows,_rf_level_dba,_rf_listener_x,_rf_listener_y,_rf_listener_z,_rf_car_length,_rf_car_width,_rf_car_height,_rf_seat_x,_rf_seat_y,_rf_seat_z,_rf_engine_x,_rf_engine_z,_rf_source_x,_rf_source_y,_rf_source_z,_rf_placement,_rf_sheet_level,_rf_sheet_line_loss,_rf_fuel_lph,_rf_fuel_l_per_100km,_rf_fuel_trip_l,_rf_fuel_avg_l_per_100km,_rf_trip_km,_rf_co2_g_per_km,_rf_oil_gph,_rf_oil_trip_ml,_rf_oil_ml_per_1000km,_rf_energy_kwh_per_100km,_rf_energy_avg_kwh_per_100km,_rf_energy_trip_kwh,_rf_reset_trip")
else()
  add_executable(revforge-render cli/render.cpp)
  target_link_libraries(revforge-render PRIVATE revforge_core)

  enable_testing()
  add_executable(revforge_tests tests/main.cpp tests/engine_sound_test.cpp tests/engine_config_test.cpp tests/driving_model_test.cpp tests/powertrain_test.cpp tests/gearbox_test.cpp tests/acoustics_test.cpp tests/starter_test.cpp tests/shift_modes_test.cpp tests/turbo_test.cpp tests/consumption_test.cpp tests/turbo_pipe_test.cpp)
  target_include_directories(revforge_tests PRIVATE tests/vendor core/src)
  target_link_libraries(revforge_tests PRIVATE revforge_core)
  # Avertissement d'un en-tête de la bibliothèque standard inclus par doctest.
  target_compile_options(revforge_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-\#warnings> $<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>)
  add_test(NAME revforge_tests COMMAND revforge_tests)

  # Empreintes du rendu 16 bits : changer le son doit être un choix, et se voit ici.
  function(golden name hash)
    add_test(NAME golden_${name} COMMAND revforge-render --out - ${ARGN})
    set_tests_properties(golden_${name} PROPERTIES PASS_REGULAR_EXPRESSION "^${hash}")
  endfunction()
  golden(steady_city 05db5790 --preset city_14 --scenario steady --rpm 5000)
  golden(launch_v8 078d8bf0 --cylinders 8 --layout cross_plane --cc 7000 --bore 99 --redline 6000 --exhaust sport --body coupe --mass 1700 --rim 19 --drive rear --scenario launch)
  golden(start_v8 c25701f7 --preset v8_cross_plane --scenario start)
  golden(blips_stock e34a6e54 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust stock --scenario blips)
  golden(blips_straight ff222b09 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust straight --scenario blips)
  golden(lap_v10 00779fce --preset v10_open --scenario lap)
  golden(lap_boxer_turbo 555e6153 --preset boxer_four_turbo --scenario lap)
  golden(lap_diesel 524d49e4 --preset diesel_four --scenario lap)
  golden(lap_electric 9542ce3b --preset electric --scenario lap)
  # Les trajets et les modes de boîte : un moteur sans turbo, sur 40 s.
  golden(drive_stop_go_eco 40a3403c --preset inline_six --scenario lap --seconds 40 --drive-scenario stop_go --shift-mode eco)
  golden(drive_country_sport 3bd2f6e2 --preset inline_six --scenario lap --seconds 40 --drive-scenario country --shift-mode sport)
  golden(drive_motorway_normal 9b590593 --preset inline_six --scenario lap --seconds 40 --drive-scenario motorway --shift-mode normal)
  golden(drive_mountain_sport b582784f --preset inline_six --scenario lap --seconds 40 --drive-scenario mountain --shift-mode sport)
  golden(drive_drag_track c3666b07 --preset inline_six --scenario lap --seconds 40 --drive-scenario drag --shift-mode track)
  golden(drive_track_track 35997d21 --preset inline_six --scenario lap --seconds 40 --drive-scenario track --shift-mode track)
  golden(drive_hill_start_eco 67cec9e3 --preset inline_six --scenario lap --seconds 40 --drive-scenario hill_start --shift-mode eco)
  golden(drive_towing_normal f7acef8f --preset inline_six --scenario lap --seconds 40 --drive-scenario towing --shift-mode normal)
  golden(turbo_pipe_long a0889b48 --preset hot_hatch_turbo --scenario turbo --turbo-pipe 2500)
  golden(turbo_pipe_short 7cab0ac1 --preset hot_hatch_turbo --scenario turbo --turbo-pipe 150)
endif()
