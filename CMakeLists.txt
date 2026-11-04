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
  core/src/air_filter.cpp
  core/src/intake_path.cpp
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
  core/src/valvetrain.cpp
  core/src/turbo.cpp
  core/src/turbo_pipe.cpp
  core/src/anti_lag.cpp
  core/src/supercharger.cpp
  core/src/supercharger_sound.cpp
  core/src/consumption.cpp
  core/src/twin_turbo.cpp
  core/src/presets.cpp
  core/src/carburetor.cpp
  core/src/architecture.cpp
  core/src/atmosphere.cpp
  core/src/thermal.cpp
  core/src/combustion.cpp
  core/src/knock_sound.cpp
  core/src/ecu.cpp
  core/src/injection.cpp
  core/src/injector_sound.cpp
  core/src/particulate_filter.cpp
  core/src/cam.cpp
  core/src/valve_float.cpp
  core/src/valvetrain_sound.cpp
  core/src/boost_control.cpp
  core/src/charge_air.cpp
  core/src/two_stroke.cpp
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
    "-sEXPORTED_FUNCTIONS=_rf_init,_rf_buffer,_rf_render,_rf_set_config,_rf_set_tuning,_rf_set_electric,_rf_set_car,_rf_set_mode,_rf_set_shift_mode,_rf_set_scenario,_rf_leg_kind,_rf_leg_target,_rf_grade_pct,_rf_underpowered,_rf_scenario_seconds,_rf_set_controls,_rf_set_transmission,_rf_shift,_rf_crank,_rf_set_listener,_rf_set_manual_rpm,_rf_set_volume,_rf_rpm,_rf_speed,_rf_gear,_rf_throttle,_rf_load,_rf_boost,_rf_turbine,_rf_turbo_release,_rf_turbo_rpm,_rf_turbo_charge,_rf_turbo_plenum,_rf_turbo_outlet,_rf_turbo_inlet,_rf_turbo_exhaust,_rf_turbo_wastegate,_rf_turbo_blowoff,_rf_turbo_flow,_rf_turbo_out_c,_rf_turbo_cool_c,_rf_turbo_gas_c,_rf_intake_air_c,_rf_intake_drop_kpa,_rf_turbo_wheel_mm,_rf_turbo_inertia,_rf_torque,_rf_power,_rf_clutch,_rf_wheel_rpm,_rf_slip,_rf_pops,_rf_cut,_rf_shifting,_rf_cranking,_rf_stalled,_rf_lugging,_rf_starter_direct,_rf_starter_kw,_rf_starter_ring,_rf_starter_pinion,_rf_starter_helix,_rf_torque_at,_rf_power_at,_rf_peak_torque,_rf_peak_torque_rpm,_rf_peak_power,_rf_peak_power_rpm,_rf_idle_rpm,_rf_redline,_rf_gears,_rf_overall_ratio,_rf_zero_to_100,_rf_top_speed,_rf_clutch_input_rpm,_rf_clutch_slip_kw,_rf_clutch_torque,_rf_line_impulse,_rf_line_impulse_length,_rf_set_gearbox,_rf_set_gear_ratio,_rf_set_clutch_assist,_rf_repair_gearbox,_rf_input_rpm,_rf_output_rpm,_rf_meshed,_rf_shift_step,_rf_grinding,_rf_overload,_rf_box_wear,_rf_box_broken,_rf_reverse_ratio,_rf_box_ratio,_rf_box_driver,_rf_box_driven,_rf_box_contact,_rf_box_mesh_hz,_rf_box_final,_rf_box_final_pinion,_rf_box_final_ring,_rf_box_input_driver,_rf_box_input_driven,_rf_box_direct,_rf_box_kind,_rf_box_limit,_rf_box_module,_rf_box_helix,_rf_box_center,_rf_box_inertia,_rf_box_efficiency,_rf_box_pending,_rf_box_cone_slip,_rf_box_rattle,_rf_box_loose,_rf_box_three_shafts,_rf_box_hypoid,_rf_box_face,_rf_box_dog_teeth,_rf_box_cone_radius,_rf_box_reverse_teeth,_rf_wheel_radius,_rf_buffer_right,_rf_render_stereo,_rf_set_listener_position,_rf_set_windows,_rf_level_dba,_rf_listener_x,_rf_listener_y,_rf_listener_z,_rf_car_length,_rf_car_width,_rf_car_height,_rf_seat_x,_rf_seat_y,_rf_seat_z,_rf_engine_x,_rf_engine_z,_rf_source_x,_rf_source_y,_rf_source_z,_rf_placement,_rf_sheet_level,_rf_sheet_line_loss,_rf_fuel_lph,_rf_fuel_l_per_100km,_rf_fuel_trip_l,_rf_fuel_avg_l_per_100km,_rf_trip_km,_rf_co2_g_per_km,_rf_oil_gph,_rf_oil_trip_ml,_rf_oil_ml_per_1000km,_rf_energy_kwh_per_100km,_rf_energy_avg_kwh_per_100km,_rf_energy_trip_kwh,_rf_reset_trip,_rf_antilag_active,_rf_antilag_heat_kw,_rf_antilag_air_flow,_rf_antilag_lost_nm,_rf_supercharger_rpm,_rf_supercharger_kw,_rf_supercharger_spill,_rf_supercharger_ratio,_rf_supercharger_volume,_rf_supercharger_wheel_mm,_rf_carb_throats,_rf_carb_throat_mm,_rf_carb_drop_kpa,_rf_coolant_c,_rf_oil_c,_rf_metal_c,_rf_thermostat,_rf_fan,_rf_oil_viscosity,_rf_cold_friction_nm,_rf_cold_idle_rpm,_rf_cold_enrichment,_rf_warm_friction_nm,_rf_compression_ratio,_rf_compression_efficiency,_rf_knock_sensor,_rf_knock_margin_deg,_rf_knock_margin_rpm,_rf_spark_advance,_rf_knock_limit,_rf_knock_retard,_rf_knock_share,_rf_ecu_advance,_rf_ecu_lambda,_rf_ecu_fired,_rf_ecu_limiter,_rf_ecu_launch,_rf_ecu_flat,_rf_ecu_traction,_rf_ecu_full_share,_rf_ecu_spark_deg,_rf_ecu_exhaust_c,_rf_injection_figure,_rf_dpf_kpa_at,_rf_dpf_load,_rf_dpf_kpa,_rf_dpf_regen,_rf_dpf_c,_rf_egr_share,_rf_cam_spec,_rf_cam_high,_rf_cam_phaser,_rf_valve_sheet,_rf_boost_target,_rf_boost_charge,_rf_boost_duty,_rf_turbo_units,_rf_turbo2_rpm,_rf_turbo2_exhaust,_rf_turbo2_wastegate,_rf_turbo2_flow,_rf_turbo2_gas_c,_rf_turbo_vane,_rf_turbo2_vane,_rf_twin_stage,_rf_twin_valve,_rf_twin_online,_rf_charger_clutch,_rf_charger_flap,_rf_charger_interstage,_rf_charger_off_rpm,_rf_charge_c,_rf_charge_water_c,_rf_water_gps,_rf_nitrous_bar,_rf_nitrous_kg,_rf_nitrous_flow,_rf_nitrous_refill,_rf_nitrous_activation_rpm")
else()
  add_executable(revforge-render cli/render.cpp)
  target_link_libraries(revforge-render PRIVATE revforge_core)

  enable_testing()
  add_executable(revforge_tests tests/main.cpp tests/engine_sound_test.cpp tests/engine_config_test.cpp tests/driving_model_test.cpp tests/powertrain_test.cpp tests/gearbox_test.cpp tests/acoustics_test.cpp tests/starter_test.cpp tests/shift_modes_test.cpp tests/turbo_test.cpp tests/consumption_test.cpp tests/retro_body_test.cpp tests/air_filter_test.cpp tests/turbo_pipe_test.cpp tests/anti_lag_test.cpp tests/valvetrain_test.cpp tests/supercharger_test.cpp tests/carburetor_test.cpp tests/rotary_test.cpp tests/jonctions_test.cpp tests/architecture_test.cpp tests/atmosphere_test.cpp tests/thermal_test.cpp tests/combustion_test.cpp tests/ecu_test.cpp tests/injection_test.cpp tests/cam_test.cpp tests/valve_float_test.cpp tests/boost_control_test.cpp tests/twin_turbo_test.cpp tests/charge_air_test.cpp tests/two_stroke_test.cpp)
  target_include_directories(revforge_tests PRIVATE tests/vendor core/src)
  target_link_libraries(revforge_tests PRIVATE revforge_core)
  # Avertissement d'un en-tête de la bibliothèque standard inclus par doctest.
  target_compile_options(revforge_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-\#warnings> $<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>)
  add_test(NAME revforge_tests COMMAND revforge_tests)

  # Empreintes du rendu 16 bits : changer le son doit être un choix, et se voit ici.
  # L'empreinte commence une ligne, pas forcément la sortie : un avertissement sur stderr peut la précéder (sous Linux, il passe avant).
  function(golden name hash)
    add_test(NAME golden_${name} COMMAND revforge-render --out - ${ARGN})
    set_tests_properties(golden_${name} PROPERTIES PASS_REGULAR_EXPRESSION "(^|\n)${hash}")
  endfunction()
  # Finitions « thermique et air » : 60 empreintes changent, toutes celles que pilote le modèle de conduite d'un moteur thermique,
  # pour trois raisons voulues. (1) Le réseau thermique agit moteur chaud : l'eau, que le calorstat tient vers 86 à 90 °C, et
  # l'huile, qu'un échangeur eau-huile tient vers 95 à 102 °C, s'écartent de la référence (90 et 100 °C) ; frottements, film de
  # carburant et air piégé en suivent l'écart. (2) L'air-air ε-NTU est l'échangeur de série d'un turbo, d'un biturbo et d'un
  # compresseur : l'air de route selon la vitesse, la masse du faisceau, la fiche au banc à 100 km/h. (3) La carte du cliquetis
  # lit l'eau réelle. Les écarts sont de quelques dixièmes de pour cent de couple : le niveau bouge de 0,5 dB au plus, mais le
  # régime se décale d'un rien et l'onde se déphase, d'où des différences de l'ordre du signal lui-même. Le niveau donné est
  # celui du rendu entier, face à l'intégration (`feat/integration-moteur`). Les rendus sans modèle de conduite (`steady`,
  # l'électrique) ne bougent pas ; l'air du jour de la came et du biturbo, nul dans l'air de référence, ne change aucune empreinte.
  golden(steady_city 05db5790 --preset city_14 --scenario steady --rpm 5000)
  # Finitions : 078d8bf0 -> eac2644d. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,02 dB par la thermique.
  golden(launch_v8 eac2644d --cylinders 8 --layout cross_plane --cc 7000 --bore 99 --redline 6000 --exhaust sport --body coupe --mass 1700 --rim 19 --drive rear --scenario launch)
  # Finitions : c25701f7 -> ad114b42. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,04 dB par la thermique.
  golden(start_v8 ad114b42 --preset v8_cross_plane --scenario start)
  # Finitions : e34a6e54 -> 2ad91c64. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,11 dB par la thermique.
  golden(blips_stock 2ad91c64 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust stock --scenario blips)
  # Finitions : ff222b09 -> e77380a6. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,38 dB par la thermique.
  golden(blips_straight e77380a6 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust straight --scenario blips)
  # Finitions : 00779fce -> f6a95f86. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,01 dB par la thermique.
  golden(lap_v10 f6a95f86 --preset v10_open --scenario lap)
  # Finitions : 555e6153 -> 498688d0. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,02 dB par la thermique, −0,06 dB par l'air-air.
  golden(lap_boxer_turbo 498688d0 --preset boxer_four_turbo --scenario lap)
  # Finitions : 524d49e4 -> 47d2861c. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau 0,00 dB par la thermique, −0,17 dB par l'air-air.
  golden(lap_diesel 47d2861c --preset diesel_four --scenario lap)
  golden(lap_electric 9542ce3b --preset electric --scenario lap)
  # `--stroke` ne fait que calculer `--cc` : mêmes empreintes que la cylindrée écrite en entier, quel que soit l'ordre.
  golden(stroke_cc 56bd11c4 --cylinders 4 --bore 96 --cc 1999 --redline 6800 --scenario steady --seconds 1)
  golden(stroke_args 56bd11c4 --stroke 69.05 --redline 6800 --bore 96 --cylinders 4 --cc 3000 --scenario steady --seconds 1)
  # Sur un préréglage, la course s'applique à son alésage et à ses cylindres, pas à ceux donnés en plus.
  golden(stroke_preset 7f3a02ce --preset v10_open --stroke 95 --scenario steady --seconds 1)
  golden(stroke_preset_bore 7f3a02ce --preset v10_open --stroke 95 --bore 70 --cylinders 4 --scenario steady --seconds 1)
  # Le rotatif : son calage, ses rangs, et le débit de deux fois sa cylindrée.
  # Finitions : 5174c427 -> 5a73ce6b. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,04 dB par la thermique.
  golden(lap_twin_rotor 5a73ce6b --preset twin_rotor_13 --scenario lap --seconds 20)
  # Un rotatif n'a pas de course : l'option ne change rien, même empreinte que sans elle.
  # Finitions : 5174c427 -> 5a73ce6b. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,04 dB par la thermique.
  golden(lap_twin_rotor_stroke 5a73ce6b --preset twin_rotor_13 --scenario lap --seconds 20 --stroke 95)
  golden(steady_three_rotor_turbo a454f216 --layout rotary --cylinders 3 --cc 1962 --redline 8000 --exhaust sport --aspiration turbo --boost 0.8 --scenario steady --rpm 5000)
  # Les trajets et les modes de boîte : un moteur sans turbo, sur 40 s.
  # Finitions : 40a3403c -> 08537aa3. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,02 dB par la thermique.
  golden(drive_stop_go_eco 08537aa3 --preset inline_six --scenario lap --seconds 40 --drive-scenario stop_go --shift-mode eco)
  # Finitions : 3bd2f6e2 -> 17e6c35d. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,03 dB par la thermique.
  golden(drive_country_sport 17e6c35d --preset inline_six --scenario lap --seconds 40 --drive-scenario country --shift-mode sport)
  # Finitions : 9b590593 -> dd405034. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,02 dB par la thermique.
  golden(drive_motorway_normal dd405034 --preset inline_six --scenario lap --seconds 40 --drive-scenario motorway --shift-mode normal)
  # Finitions : b582784f -> 2c5904b7. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,02 dB par la thermique.
  golden(drive_mountain_sport 2c5904b7 --preset inline_six --scenario lap --seconds 40 --drive-scenario mountain --shift-mode sport)
  # Finitions : c3666b07 -> 0a8a66e1. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,02 dB par la thermique.
  golden(drive_drag_track 0a8a66e1 --preset inline_six --scenario lap --seconds 40 --drive-scenario drag --shift-mode track)
  # Finitions : 35997d21 -> 281a7434. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,01 dB par la thermique.
  golden(drive_track_track 281a7434 --preset inline_six --scenario lap --seconds 40 --drive-scenario track --shift-mode track)
  # Finitions : 67cec9e3 -> 6566eae2. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,03 dB par la thermique.
  golden(drive_hill_start_eco 6566eae2 --preset inline_six --scenario lap --seconds 40 --drive-scenario hill_start --shift-mode eco)
  # Finitions : f7acef8f -> 96907f0b. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,01 dB par la thermique.
  golden(drive_towing_normal 96907f0b --preset inline_six --scenario lap --seconds 40 --drive-scenario towing --shift-mode normal)
  # La carrosserie des années 80, sur un moteur aux réglages neutres : sa ligne, son plan et sa caisse.
  # Finitions : fe72a187 -> 367fbcce. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau 0,00 dB par la thermique.
  golden(lap_retro 367fbcce --cylinders 6 --cc 2500 --bore 84 --redline 6400 --exhaust stock --body retro --mass 1150 --rim 14 --drive rear --insulation light --scenario lap)
  # Finitions : a0889b48 -> f66ca256. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau 0,00 dB par la thermique, −0,14 dB par l'air-air.
  golden(turbo_pipe_long f66ca256 --preset hot_hatch_turbo --scenario turbo --turbo-pipe 2500)
  # Finitions : 3898802e -> 261e644d. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau 0,00 dB par la thermique, −0,05 dB par l'air-air.
  golden(turbo_pipe_short 261e644d --preset hot_hatch_turbo --scenario turbo --turbo-pipe 150)
  # Finitions : 463a064f -> 11d4d98e. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,05 dB par la thermique.
  golden(carb_multi 11d4d98e --preset carb_twin_cam --scenario blips)
  # Finitions : 4d1f365e -> 2148b2b4. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,01 dB par la thermique.
  golden(carb_single 2148b2b4 --preset hot_hatch_na --carb single --scenario launch --seconds 10)
  # Les architectures : un VR6 (une culasse), un W16 et un V14 en deux lignes (culasses, intervalles inégaux), un W12 au démarreur.
  golden(steady_vr6 dc6c1925 --preset vr6_golf --scenario steady --rpm 4000 --seconds 2)
  golden(steady_w16 ff905d02 --cylinders 16 --layout w --cc 7993 --bore 86 --redline 6400 --exhaust sport --routing dual --scenario steady --rpm 4000 --seconds 2)
  golden(steady_v14 1ce07227 --cylinders 14 --layout v --cc 7000 --bore 86 --redline 6000 --exhaust sport --routing dual --scenario steady --rpm 3000 --seconds 2)
  # Finitions : f62d0dc4 -> 0b08eb1b. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,07 dB par la thermique.
  golden(start_w12 0b08eb1b --cylinders 12 --layout w --cc 5998 --bore 84 --redline 6500 --scenario start)
  # Finitions : efe03b28 -> ce5051da. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,05 dB par la thermique.
  golden(blips_w8 ce5051da --cylinders 8 --layout w --cc 3999 --bore 84 --redline 6400 --exhaust sport --routing dual --scenario blips)
  # `--layout` l'emporte sur un préréglage, sur ses cylindres : le V12 en W12, même empreinte que le W12 écrit en entier.
  golden(layout_preset 14b15d69 --preset v12 --layout w --scenario steady --seconds 1)
  golden(layout_explicit 14b15d69 --cylinders 12 --layout w --cc 6496 --bore 94 --redline 8900 --exhaust no_muffler --routing dual --body supercar --mass 1650 --rim 21 --drive all --placement mid --insulation light --gearbox dual_clutch --scenario steady --seconds 1)
  # L'air du jour : un turbo à 3000 m, une citadine par grand froid, un carburateur qui s'enrichit en altitude, l'électrique.
  # Finitions : 31d03535 -> d18f44e7. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle ; air-air ε-NTU (air de route, faisceau). Niveau −0,07 dB par la thermique, −0,12 dB par l'air-air.
  golden(altitude_turbo d18f44e7 --preset hot_hatch_turbo --scenario turbo --altitude 3000)
  # Finitions : 054e6de4 -> 83c6c520. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,03 dB par la thermique.
  golden(ambient_cold_lap 83c6c520 --preset city_14 --scenario lap --seconds 20 --ambient -20)
  # Finitions : d84f8e06 -> 2d6eb403. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,01 dB par la thermique.
  golden(altitude_carb 2d6eb403 --preset carb_twin_cam --scenario blips --altitude 2500)
  golden(ambient_electric d0299648 --preset electric --scenario lap --seconds 20 --ambient 45 --altitude 1500)
  # Départ à froid : ralenti accéléré, frottements de l'huile froide ; et le starter d'un carburateur par -10 °C.
  # Intégration : b3a7b95a -> 25dc320d. Après un départ à froid, le film de l'injection indirecte lit l'eau du réseau thermique, et la paroi froide réchauffe moins l'air piégé.
  # Finitions : 25dc320d -> d3d404e4. Départ à froid : l'échangeur eau-huile réchauffe l'huile par l'eau, et l'air piégé lit l'eau, non plus le métal. Niveau −0,48 dB par la thermique.
  golden(start_cold_city d3d404e4 --preset city_14 --start cold --scenario start --seconds 8)
  # Intégration : par -10 °C, l'air est aussi plus dense (atmosphère) ; avec l'air de référence, l'empreinte redevient d3a95f44.
  # Intégration : fdd3932c -> 558e94b7. Après un départ à froid, la paroi froide réchauffe moins l'air piégé (thermique et atmosphère),
  # et le ralenti accéléré garde l'autorité du démarrage (0,8 du remplissage), et non 0,45 comme moteur chaud.
  # Finitions : 558e94b7 -> 90b6a08d. Départ à froid : l'échangeur eau-huile réchauffe l'huile par l'eau, et l'air piégé, comme le film des carburateurs, lit l'eau, non plus le métal ; carte du cliquetis à l'eau réelle (par −10 °C, elle existe). Niveau +0,15 dB par la thermique.
  golden(blips_cold_carb 90b6a08d --preset carb_twin_cam --start cold --ambient -10 --scenario blips --seconds 8)
  # La combustion : des carburateurs à 12:1 qui cliquettent sans capteur, l'E85 sur un turbo, un diesel moins comprimé au démarrage, un rotor plus creusé.
  # Intégration : cd653d6e -> 854924ae. La vaporisation prend à l'air la part d'un carburateur (0,5, injection), non plus 0,15 pour toute alimentation.
  # Finitions (régulation) : 854924ae -> 116e2eb4. le cycle de la carte du cliquetis perd sa chaleur aux parois pas à pas (Woschni) et libère 0,95 de l'énergie de l'air au lieu de 0,80 net : la pression des gaz frais monte plus pendant la combustion, la limite de ces carburateurs sans capteur recule de 3 à 5°, et la zone qui cliquette monte de 2000 à 3000 tr/min (371 combustions entendues en 16 s au lieu de 111).
  # Finitions : 854924ae -> c43cfd2d. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,04 dB par la thermique.
  golden(knock_carb d71c1a4a --preset carb_twin_cam --compression 12 --scenario turbo --seconds 16)
  # Intégration : 8c6eee0c -> 98ff3f12. La vaporisation prend à l'air la part d'une injection indirecte (0,2, injection), non plus 0,15.
  # Finitions : 98ff3f12 -> 45a5fcab. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle ; air-air ε-NTU (air de route, faisceau). Niveau −0,02 dB par la thermique, +0,02 dB par l'air-air.
  golden(compression_e85_turbo 45a5fcab --preset hot_hatch_turbo --compression 10.5 --grade e85 --scenario launch --seconds 6)
  # Finitions : a48c481b -> 74995596. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau −0,41 dB par la thermique, +0,28 dB par l'air-air.
  golden(compression_diesel_start 74995596 --preset diesel_four --compression 15 --scenario start)
  # Finitions (régulation) : f21ca74c -> 6a26909f. le cycle de la carte du cliquetis perd sa chaleur aux parois pas à pas (Woschni) et libère 0,95 de l'énergie de l'air au lieu de 0,80 net ; la carte d'origine est recalculée par le même cycle, et le capteur retire autrement l'avance (marge la plus faible −12,4 -> −10,1°).
  # Finitions : f21ca74c -> 836c484e. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,07 dB par la thermique.
  golden(compression_rotary c55191a0 --preset twin_rotor_13 --compression 11 --scenario lap --seconds 10)
  # Le calculateur, actif : la coupure d'allumage et ses détonations, le rupteur progressif avec une carte réglée, le
  # launch control au frein (le 0-100 s'écrit avant l'empreinte), le passage à fond, l'antipatinage.
  # Finitions : 2af90e2c -> d4162846. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,10 dB par la thermique.
  golden(ecu_ignition_cut d4162846 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust sport --limiter ignition_cut --scenario blips)
  # Intégration : 99069727 -> 6b783b4b. La part de couple de l'avance passe par la carte du cliquetis, qui part de la carte d'origine réelle ; le calculateur garde carburant et chaleur.
  # Finitions (régulation) : 6b783b4b -> 32de2eaa. Le meilleur couple est le sommet du cycle (pertes aux parois), où la carte d'origine se place au-dessus de 3000 tr/min : +6° y coûtent (2.0 atmosphérique, puissance maximale −1,5 % au lieu de +2,1 %) ; les 15° du rupteur progressif coûtent 7,0 % (corrélation d'Ayala) au lieu de 4,9 % (parabole de 68°).
  # Finitions : 6b783b4b -> 05e1439a. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,03 dB par la thermique.
  golden(ecu_soft_map 2d0b1ae4 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --exhaust sport --limiter soft --advance 6 --lambda 0.95 --scenario blips)
  # Finitions (régulation) : 7fd42b07 -> 872a4d0a. Les 25° de retard du launch control coûtent 15,9 % de couple (corrélation d'Ayala, que le cycle retrouve) au lieu de 13,5 % : 4411 tr/min au lâcher au lieu de 4406, 0-100 en 6,11 s au lieu de 6,10.
  # Finitions : 7fd42b07 -> 431df6d2. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,08 dB par la thermique, 0,00 dB par l'air-air.
  golden(ecu_launch_control 996d66c4 --preset hot_hatch_turbo --launch 4500 --scenario launch_control)
  # Finitions : 7d3dc773 -> 3a6d04d4. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,03 dB par la thermique, −0,02 dB par l'air-air.
  golden(ecu_flat_shift 3a6d04d4 --preset hot_hatch_turbo --flat-shift 1 --scenario launch --seconds 12)
  # Finitions (régulation) : 0bc683ee -> 23397c54. L'antipatinage retire le même couple, mais par un autre retard (l'inverse de la corrélation d'Ayala) : plus de chaleur au gaz, et 25° laissent 84,1 % au lieu de 86,5 % avant de couper. 0-100 de la fiche inchangé, 6,20 s.
  # Finitions : 0bc683ee -> 33364913. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau 0,00 dB par la thermique.
  golden(ecu_traction 3a62fcae --cylinders 8 --layout cross_plane --cc 7000 --bore 99 --redline 6000 --exhaust sport --body coupe --mass 1700 --rim 19 --drive rear --traction sport --scenario launch --seconds 10)
  # L'injection : le tic d'une injection directe (sous le capot, au ralenti), la rampe commune, la recirculation et le filtre.
  # Intégration : 19ac1ec5 -> 038cacdf. La chaleur latente vient du carburant (350 kJ/kg, combustion), et la carte du cliquetis lit le refroidissement de l'injection directe.
  # Finitions : 038cacdf -> 808814ba. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,14 dB par la thermique.
  golden(injection_direct_blips 808814ba --preset hot_hatch_na --fuel-system direct --scenario blips)
  # Intégration : 29fbf825 -> be119bb8. La chaleur latente vient du carburant (350 kJ/kg, combustion), et la carte du cliquetis lit le refroidissement de l'injection directe.
  # Finitions : be119bb8 -> 92457b85. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,12 dB par la thermique.
  golden(injection_direct_bay 92457b85 --preset hot_hatch_na --fuel-system direct --scenario idle --seconds 4 --listener bay)
  # Finitions : 4781fcd4 -> b6e38fad. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,06 dB par la thermique, −0,12 dB par l'air-air.
  golden(injection_common_rail b6e38fad --preset diesel_four --diesel-injection common_rail --scenario lap --seconds 20)
  # Finitions : 089dcc16 -> ce2eae61. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,16 dB par la thermique, −0,19 dB par l'air-air.
  golden(injection_diesel_egr_dpf ce2eae61 --preset diesel_four --egr 1 --dpf 1 --scenario lap --seconds 20)
  # Finitions : db5318c3 -> 9c589d63. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,05 dB par la thermique.
  golden(injection_petrol_egr 9c589d63 --preset city_14 --egr 1 --scenario lap --seconds 20)
  # La came : ralenti d'une came pointue, bascule de deux profils, déphaseur sur un tour.
  # Finitions : 84564533 -> 6693b901. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau +0,33 dB par la thermique.
  golden(cam_race_idle 6693b901 --preset hot_hatch_na --scenario idle --seconds 6 --cam-duration 300 --cam-lift 13 --lsa 104)
  # Finitions (régulation) : 8f78b11d -> d7b94152. le cycle de la carte du cliquetis perd sa chaleur aux parois pas à pas (Woschni) et libère 0,95 de l'énergie de l'air au lieu de 0,80 net : marge la plus faible −2,8 -> −2,3°, et 2 combustions entendues qui cliquettent en 10 s au lieu de 0.
  # Finitions : 8f78b11d -> 072d6a0f. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,01 dB par la thermique.
  golden(cam_two_step_launch 29004f94 --preset hot_hatch_na --vvt two_step --scenario launch --seconds 10)
  # Finitions : 6b59c2ee -> 3f5a9b78. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,03 dB par la thermique.
  golden(cam_phased_lap 3f5a9b78 --preset hot_hatch_na --vvt phased --scenario lap --seconds 20)
  # Les culbuteurs : le choc des poussoirs au ralenti sous le capot, puis un tour avec des ressorts raides. L'affolement :
  # le son seul, tenu au-delà (8140 tr/min sur ce moteur), là où aucun rupteur ne l'arrête.
  golden(ohv_idle_bay 8a51c3c3 --preset v8_cross_plane --cam ohv --scenario steady --rpm 900 --listener bay --seconds 2)
  # Finitions : bd8fba88 -> 09b4545f. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,06 dB par la thermique.
  golden(ohv_lap_stiff 09b4545f --preset v8_cross_plane --cam ohv --springs stiff --scenario lap --seconds 20)
  golden(valve_float_bay 73732eab --preset hot_hatch_na --scenario steady --rpm 9000 --listener bay --seconds 2)
  # La régulation de pression : l'électrovanne et son PID sur le coup de gaz en quatrième ; selon le rapport, une traction qui part.
  # Finitions : 6b201abd -> f9beacfe. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau 0,00 dB par la thermique, −0,12 dB par l'air-air.
  # Fusion des finitions : f9beacfe -> 891ebb82. Le banc d'étalonnage attend que la pression soit établie, faisceau stabilisé (`identifyPlant`).
  golden(boost_electronic 891ebb82 --preset hot_hatch_turbo --scenario turbo --boost-control electronic)
  # Finitions : 64bde81c -> cc4f3baa. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,03 dB par la thermique, −0,18 dB par l'air-air.
  # Fusion des finitions : cc4f3baa -> 8497b132. Le banc d'étalonnage attend que la pression soit établie, faisceau stabilisé (`identifyPlant`).
  golden(boost_by_gear 8497b132 --preset three_cyl_turbo --scenario launch --seconds 10 --boost-control by_gear)
  # Deux turbos, twincharger, aubes : twin_turbo.cpp, et le second sifflement.
  # Finitions : 4a3f8582 -> 08ed5aaa. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,08 dB par la thermique, −0,14 dB par l'air-air.
  golden(twin_parallel_v6 08ed5aaa --cylinders 6 --layout v --cc 3000 --bore 86 --redline 6800 --aspiration turbo --boost 1 --exhaust sport --body coupe --mass 1500 --rim 18 --drive rear --scenario turbo --seconds 12 --turbo-layout twin_parallel)
  # Finitions : 32336632 -> d955fc5e. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau −0,07 dB par la thermique, +0,15 dB par l'air-air.
  golden(twin_sequential d955fc5e --preset hot_hatch_turbo --turbo-layout twin_sequential --scenario turbo --seconds 12)
  # Finitions : 48a5fa18 -> 8918a89c. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau), et le compresseur dimensionné avec lui. Niveau −0,01 dB par la thermique, 0,00 dB par l'air-air.
  golden(twincharger 8918a89c --preset hot_hatch_turbo --turbo-layout twincharger --scenario launch --seconds 10)
  # Finitions : c1df8a8e -> 1e20d532. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; air-air ε-NTU (air de route, faisceau). Niveau +0,07 dB par la thermique, −0,11 dB par l'air-air.
  golden(vgt_diesel 1e20d532 --preset diesel_four --vgt 1 --scenario turbo --seconds 12)
  # Finitions (régulation) : l'électrovanne pilote les deux soupapes d'un séquentiel, PID étalonné sur le banc des deux turbos.
  # Fusion avec la thermique et l'air : 960d1218 -> 8fd23b6e, réseau thermique branché et air-air ε-NTU, comme twin_sequential ; la
  # précommande des petits turbos tient leurs roues à leur limite (`turboSteady`), et le banc attend l'équilibre (`identifyPlant`).
  golden(twin_sequential_electronic 8fd23b6e --preset hot_hatch_turbo --turbo-layout twin_sequential --boost-control electronic --scenario turbo --seconds 12)
  # L'air de charge : sans échangeur, air-eau, injection d'eau, protoxyde.
  # Intégration : 07dfbb8d -> 34de3f5e. La carte du cliquetis lit la charge sans échangeur (air de charge), plus chaude.
  # Finitions (régulation) : 34de3f5e -> 7f5a5c89. le cycle de la carte du cliquetis perd sa chaleur aux parois pas à pas (Woschni) et libère 0,95 de l'énergie de l'air au lieu de 0,80 net : 94 combustions entendues qui cliquettent au lieu de 89, marge la plus faible −11,9° au lieu de −11,8°.
  # Finitions : 34de3f5e -> ac91ad56. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle ; l'air-air ε-NTU de série, sur lequel restent dimensionnés la boîte, l'embrayage et la carte d'origine du cliquetis. Niveau −0,02 dB par la thermique, −0,03 dB par l'air-air.
  golden(charge_none_turbo 4e363a4f --preset hot_hatch_turbo --intercooler none --scenario turbo)
  # Finitions : efc2c12e -> 2ff14546. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle ; l'air-air ε-NTU de série, sur lequel restent dimensionnés la boîte, l'embrayage, la carte d'origine du cliquetis et le compresseur. Niveau +0,01 dB par la thermique, −0,01 dB par l'air-air.
  golden(charge_water_cooler 2ff14546 --preset v8_supercharged --intercooler water --scenario launch --seconds 10)
  # Finitions : 59ac8a9c -> 2fde6b1d. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle ; air-air ε-NTU (air de route, faisceau). Niveau 0,00 dB par la thermique, +0,03 dB par l'air-air.
  golden(charge_water_injection 2fde6b1d --preset hot_hatch_turbo --water 1 --scenario launch --seconds 10)
  # Finitions : bedbec6d -> ebc65937. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C) ; carte du cliquetis à l'eau réelle. Niveau −0,06 dB par la thermique.
  golden(charge_nitrous ebc65937 --preset hot_hatch_na --nitrous 75 --scenario launch --seconds 10)
  # Le deux-temps : son cycle intégré, ses lumières, ses ratés (à leur propre générateur), puis sa chambre de détente.
  # Finitions : 6d847fab -> 64396e39. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,02 dB par la thermique.
  golden(lap_two_stroke 64396e39 --preset two_stroke_triple --scenario lap --seconds 20)
  # Finitions : fc2d68ae -> 3dbb4d96. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau −0,16 dB par la thermique.
  golden(idle_two_stroke 3dbb4d96 --preset two_stroke_triple --scenario idle --seconds 10)
  # Finitions : fde00d9e -> c9f79040. Réseau thermique branché moteur chaud (eau 86 à 90 °C, huile 95 à 102 °C). Niveau +0,01 dB par la thermique.
  golden(lap_two_stroke_expansion c9f79040 --preset two_stroke_triple --exhaust expansion --scenario lap --seconds 20)
  # Un bicylindre comme la Trabant, sans modèle de conduite : la charge déduite du régime, et ses ratés.
  golden(steady_two_stroke_twin 38e43381 --cylinders 2 --cc 595 --bore 72 --redline 4500 --cycle two_stroke --carb single --scenario steady --rpm 900 --seconds 3)
  # `--fuel` se lit à sa valeur : petrol ou diesel, le carburant ; 0 ou 1, l'affichage de la consommation. Un diesel n'a pas d'étincelle.
  add_test(NAME cli_fuel_diesel COMMAND revforge-render --out none --cylinders 4 --cc 1900 --bore 82 --redline 4500 --fuel diesel --scenario steady --seconds 1 --knock 1)
  set_tests_properties(cli_fuel_diesel PROPERTIES PASS_REGULAR_EXPRESSION "Knock: none \\(no spark\\)")
endif()
