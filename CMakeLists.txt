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
  core/src/tyre.cpp
  core/src/cold_start.cpp
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
    "-sEXPORTED_FUNCTIONS=_rf_init,_rf_buffer,_rf_render,_rf_set_config,_rf_set_tuning,_rf_set_electric,_rf_set_car,_rf_set_mode,_rf_set_shift_mode,_rf_set_scenario,_rf_leg_kind,_rf_leg_target,_rf_grade_pct,_rf_underpowered,_rf_scenario_seconds,_rf_set_controls,_rf_set_transmission,_rf_shift,_rf_crank,_rf_set_listener,_rf_set_manual_rpm,_rf_set_volume,_rf_rpm,_rf_speed,_rf_gear,_rf_throttle,_rf_load,_rf_boost,_rf_turbine,_rf_turbo_release,_rf_turbo_rpm,_rf_turbo_charge,_rf_turbo_plenum,_rf_turbo_outlet,_rf_turbo_inlet,_rf_turbo_exhaust,_rf_turbo_wastegate,_rf_turbo_blowoff,_rf_turbo_flow,_rf_turbo_out_c,_rf_turbo_cool_c,_rf_turbo_gas_c,_rf_intake_air_c,_rf_intake_drop_kpa,_rf_turbo_wheel_mm,_rf_turbo_inertia,_rf_torque,_rf_power,_rf_clutch,_rf_wheel_rpm,_rf_slip,_rf_pops,_rf_cut,_rf_shifting,_rf_cranking,_rf_stalled,_rf_lugging,_rf_starter_direct,_rf_starter_kw,_rf_starter_ring,_rf_starter_pinion,_rf_starter_helix,_rf_torque_at,_rf_power_at,_rf_peak_torque,_rf_peak_torque_rpm,_rf_peak_power,_rf_peak_power_rpm,_rf_idle_rpm,_rf_redline,_rf_gears,_rf_overall_ratio,_rf_zero_to_100,_rf_top_speed,_rf_clutch_input_rpm,_rf_clutch_slip_kw,_rf_clutch_torque,_rf_line_impulse,_rf_line_impulse_length,_rf_set_gearbox,_rf_set_gear_ratio,_rf_set_clutch_assist,_rf_repair_gearbox,_rf_input_rpm,_rf_output_rpm,_rf_meshed,_rf_shift_step,_rf_grinding,_rf_overload,_rf_box_wear,_rf_box_broken,_rf_reverse_ratio,_rf_box_ratio,_rf_box_driver,_rf_box_driven,_rf_box_contact,_rf_box_mesh_hz,_rf_box_final,_rf_box_final_pinion,_rf_box_final_ring,_rf_box_input_driver,_rf_box_input_driven,_rf_box_direct,_rf_box_kind,_rf_box_limit,_rf_box_module,_rf_box_helix,_rf_box_center,_rf_box_inertia,_rf_box_efficiency,_rf_box_pending,_rf_box_cone_slip,_rf_box_rattle,_rf_box_loose,_rf_box_three_shafts,_rf_box_hypoid,_rf_box_face,_rf_box_dog_teeth,_rf_box_cone_radius,_rf_box_reverse_teeth,_rf_wheel_radius,_rf_buffer_right,_rf_render_stereo,_rf_set_listener_position,_rf_set_windows,_rf_level_dba,_rf_listener_x,_rf_listener_y,_rf_listener_z,_rf_car_length,_rf_car_width,_rf_car_height,_rf_seat_x,_rf_seat_y,_rf_seat_z,_rf_engine_x,_rf_engine_z,_rf_source_x,_rf_source_y,_rf_source_z,_rf_placement,_rf_sheet_level,_rf_sheet_line_loss,_rf_fuel_lph,_rf_fuel_l_per_100km,_rf_fuel_trip_l,_rf_fuel_avg_l_per_100km,_rf_trip_km,_rf_co2_g_per_km,_rf_oil_gph,_rf_oil_trip_ml,_rf_oil_ml_per_1000km,_rf_energy_kwh_per_100km,_rf_energy_avg_kwh_per_100km,_rf_energy_trip_kwh,_rf_reset_trip,_rf_antilag_active,_rf_antilag_heat_kw,_rf_antilag_air_flow,_rf_antilag_lost_nm,_rf_supercharger_rpm,_rf_supercharger_kw,_rf_supercharger_spill,_rf_supercharger_ratio,_rf_supercharger_volume,_rf_supercharger_wheel_mm,_rf_carb_throats,_rf_carb_throat_mm,_rf_carb_drop_kpa,_rf_coolant_c,_rf_oil_c,_rf_metal_c,_rf_thermostat,_rf_fan,_rf_oil_viscosity,_rf_cold_friction_nm,_rf_cold_idle_rpm,_rf_cold_enrichment,_rf_warm_friction_nm,_rf_compression_ratio,_rf_compression_efficiency,_rf_knock_sensor,_rf_knock_margin_deg,_rf_knock_margin_rpm,_rf_spark_advance,_rf_knock_limit,_rf_knock_retard,_rf_knock_share,_rf_ecu_advance,_rf_ecu_lambda,_rf_ecu_fired,_rf_ecu_limiter,_rf_ecu_launch,_rf_ecu_flat,_rf_ecu_traction,_rf_ecu_full_share,_rf_ecu_spark_deg,_rf_ecu_exhaust_c,_rf_injection_figure,_rf_dpf_kpa_at,_rf_dpf_load,_rf_dpf_kpa,_rf_dpf_regen,_rf_dpf_c,_rf_egr_share,_rf_cam_spec,_rf_cam_high,_rf_cam_phaser,_rf_valve_sheet,_rf_boost_target,_rf_boost_charge,_rf_boost_duty,_rf_turbo_units,_rf_turbo2_rpm,_rf_turbo2_exhaust,_rf_turbo2_wastegate,_rf_turbo2_flow,_rf_turbo2_gas_c,_rf_turbo_vane,_rf_turbo2_vane,_rf_twin_stage,_rf_twin_valve,_rf_twin_online,_rf_charger_clutch,_rf_charger_flap,_rf_charger_interstage,_rf_charger_off_rpm,_rf_charge_c,_rf_charge_water_c,_rf_water_gps,_rf_nitrous_bar,_rf_nitrous_kg,_rf_nitrous_flow,_rf_nitrous_refill,_rf_nitrous_activation_rpm,_rf_release_key,_rf_battery_v,_rf_starter_a,_rf_start_failed,_rf_preheat")
else()
  add_executable(revforge-render cli/render.cpp)
  target_link_libraries(revforge-render PRIVATE revforge_core)

  enable_testing()
  add_executable(revforge_tests tests/main.cpp tests/engine_sound_test.cpp tests/engine_config_test.cpp tests/driving_model_test.cpp tests/powertrain_test.cpp tests/gearbox_test.cpp tests/acoustics_test.cpp tests/starter_test.cpp tests/shift_modes_test.cpp tests/turbo_test.cpp tests/consumption_test.cpp tests/retro_body_test.cpp tests/air_filter_test.cpp tests/turbo_pipe_test.cpp tests/anti_lag_test.cpp tests/valvetrain_test.cpp tests/supercharger_test.cpp tests/carburetor_test.cpp tests/rotary_test.cpp tests/jonctions_test.cpp tests/architecture_test.cpp tests/atmosphere_test.cpp tests/thermal_test.cpp tests/combustion_test.cpp tests/ecu_test.cpp tests/injection_test.cpp tests/cam_test.cpp tests/valve_float_test.cpp tests/boost_control_test.cpp tests/twin_turbo_test.cpp tests/charge_air_test.cpp tests/two_stroke_test.cpp tests/tyre_test.cpp tests/cold_start_test.cpp tests/demarreur_test.cpp)
  target_include_directories(revforge_tests PRIVATE tests/vendor core/src)
  target_link_libraries(revforge_tests PRIVATE revforge_core)
  # Avertissement d'un en-tête de la bibliothèque standard inclus par doctest.
  target_compile_options(revforge_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-\#warnings> $<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>)
  add_test(NAME revforge_tests COMMAND revforge_tests)
  # La reprise de la CLI : elle passait la troisième pendant que l'embrayage patinait encore au départ, et le moteur calait
  # (« Tip-in from 0 rpm ») avec --advance 6. Elle lit maintenant la vitesse de la voiture : le moteur tient ses 1500 tr/min.
  add_test(NAME tipin_advance COMMAND revforge-render --out none --preset hot_hatch_na --scenario tipin --rpm 1500 --advance 6)
  set_tests_properties(tipin_advance PROPERTIES PASS_REGULAR_EXPRESSION "Tip-in from 1[45][0-9][0-9] rpm")

  # Empreintes du rendu 16 bits : changer le son doit être un choix, et se voit ici.
  # L'empreinte commence une ligne, pas forcément la sortie : un avertissement sur stderr peut la précéder (sous Linux, il passe avant).
  function(golden name hash)
    add_test(NAME golden_${name} COMMAND revforge-render --out - ${ARGN})
    set_tests_properties(golden_${name} PROPERTIES PASS_REGULAR_EXPRESSION "(^|\n)${hash}")
  endfunction()
  # Finitions (régulation, thermique et air, conduite, puis leur fusion) : 62 des 76 empreintes de l'intégration
  # (`feat/integration-moteur`) changent, toutes voulues, et une s'ajoute (twin_sequential_electronic). Chaque ligne dit ce qui la
  # fait bouger, et son niveau face à l'intégration (rendu entier, au plus 0,5 dB). Les causes :
  # - thermique et air : le réseau thermique agit moteur chaud (eau tenue vers 86 à 90 °C par le calorstat, huile vers 95 à
  #   102 °C par un échangeur eau-huile, écarts à la référence de 90 et 100 °C) ; l'air-air ε-NTU est l'échangeur de série d'un
  #   turbo, d'un biturbo et d'un compresseur ; la carte du cliquetis lit l'eau réelle ;
  # - conduite : le pneu à sommet d'adhérence (Pacejka) et la roue intégrée implicitement ; le robot du départ ne serre que le
  #   couple annoncé ; la logique de passage ne monte pas, roues patinant, un rapport que la vitesse ne tient pas ;
  # - régulation : le cycle du cliquetis perd sa chaleur aux parois pas à pas (Woschni) : seules bougent les empreintes où la
  #   carte travaille près de sa limite, ou dont l'avance s'écarte de l'origine ;
  # - fusion : le banc d'étalonnage de la régulation attend l'équilibre (`identifyPlant`), et `turboSteady` ouvre la soupape
  #   quand ce sont les roues qui limitent.
  # Les rendus sans modèle de conduite (`steady`) ne bougent pas. Contrôle croisé : réseau tenu à la référence après un départ
  # chaud, ancien air-air (dimensionnement du compresseur compris) et ancien banc, 67 empreintes sur 77 retrouvent celles de
  # `feat/fin-conduite` au bit près ; les dix autres sont les deux départs à froid, les sept du cycle de Woschni et
  # twin_sequential_electronic, qui n'existe pas sur cette branche.
  golden(steady_city 05db5790 --preset city_14 --scenario steady --rpm 5000)
  # Finitions : 078d8bf0 -> 96e24864. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet : sans antipatinage, la propulsion de 7 L patine au-delà du sommet en première et en deuxième. Niveau −0,18 dB face à l'intégration.
  golden(launch_v8 96e24864 --cylinders 8 --layout cross_plane --cc 7000 --bore 99 --redline 6000 --exhaust sport --body coupe --mass 1700 --rim 19 --drive rear --scenario launch)
  # Finitions : c25701f7 -> ad114b42. thermique et air : réseau thermique branché moteur chaud. Niveau −0,04 dB face à l'intégration.
  golden(start_v8 ad114b42 --preset v8_cross_plane --scenario start)
  # Finitions : e34a6e54 -> 2ad91c64. thermique et air : réseau thermique branché moteur chaud. Niveau +0,11 dB face à l'intégration.
  golden(blips_stock 2ad91c64 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust stock --scenario blips)
  # Finitions : ff222b09 -> e77380a6. thermique et air : réseau thermique branché moteur chaud. Niveau −0,38 dB face à l'intégration.
  golden(blips_straight e77380a6 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust straight --scenario blips)
  # Finitions : 00779fce -> 6679c29f. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,04 dB face à l'intégration.
  golden(lap_v10 6679c29f --preset v10_open --scenario lap)
  # Finitions : 555e6153 -> 415306c4. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,05 dB face à l'intégration.
  golden(lap_boxer_turbo 415306c4 --preset boxer_four_turbo --scenario lap)
  # Finitions : 524d49e4 -> 601e91a3. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,24 dB face à l'intégration.
  golden(lap_diesel 601e91a3 --preset diesel_four --scenario lap)
  # Finitions : 9542ce3b -> 5cfa8cab. conduite : pneu à sommet. Niveau +0,12 dB face à l'intégration.
  golden(lap_electric 5cfa8cab --preset electric --scenario lap)
  # `--stroke` ne fait que calculer `--cc` : mêmes empreintes que la cylindrée écrite en entier, quel que soit l'ordre.
  golden(stroke_cc 56bd11c4 --cylinders 4 --bore 96 --cc 1999 --redline 6800 --scenario steady --seconds 1)
  golden(stroke_args 56bd11c4 --stroke 69.05 --redline 6800 --bore 96 --cylinders 4 --cc 3000 --scenario steady --seconds 1)
  # Sur un préréglage, la course s'applique à son alésage et à ses cylindres, pas à ceux donnés en plus.
  golden(stroke_preset 7f3a02ce --preset v10_open --stroke 95 --scenario steady --seconds 1)
  golden(stroke_preset_bore 7f3a02ce --preset v10_open --stroke 95 --bore 70 --cylinders 4 --scenario steady --seconds 1)
  # Le rotatif : son calage, ses rangs, et le débit de deux fois sa cylindrée.
  # Finitions : 5174c427 -> 58c5aec9. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau +0,43 dB face à l'intégration.
  golden(lap_twin_rotor 58c5aec9 --preset twin_rotor_13 --scenario lap --seconds 20)
  # Un rotatif n'a pas de course : l'option ne change rien, même empreinte que sans elle.
  # Finitions : 5174c427 -> 58c5aec9. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau +0,43 dB face à l'intégration.
  golden(lap_twin_rotor_stroke 58c5aec9 --preset twin_rotor_13 --scenario lap --seconds 20 --stroke 95)
  golden(steady_three_rotor_turbo a454f216 --layout rotary --cylinders 3 --cc 1962 --redline 8000 --exhaust sport --aspiration turbo --boost 0.8 --scenario steady --rpm 5000)
  # Les trajets et les modes de boîte : un moteur sans turbo, sur 40 s.
  # Finitions : 40a3403c -> 83ac0e71. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau −0,12 dB face à l'intégration.
  golden(drive_stop_go_eco 83ac0e71 --preset inline_six --scenario lap --seconds 40 --drive-scenario stop_go --shift-mode eco)
  # Finitions : 3bd2f6e2 -> c6d42df5. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau +0,24 dB face à l'intégration.
  golden(drive_country_sport c6d42df5 --preset inline_six --scenario lap --seconds 40 --drive-scenario country --shift-mode sport)
  # Finitions : 9b590593 -> e6083cce. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau +0,13 dB face à l'intégration.
  golden(drive_motorway_normal e6083cce --preset inline_six --scenario lap --seconds 40 --drive-scenario motorway --shift-mode normal)
  # Finitions : b582784f -> 2dce7e13. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau +0,24 dB face à l'intégration.
  golden(drive_mountain_sport 2dce7e13 --preset inline_six --scenario lap --seconds 40 --drive-scenario mountain --shift-mode sport)
  # Finitions : c3666b07 -> 2c73066f. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau −0,09 dB face à l'intégration.
  golden(drive_drag_track 2c73066f --preset inline_six --scenario lap --seconds 40 --drive-scenario drag --shift-mode track)
  # Finitions : 35997d21 -> 4a1f10e1. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau −0,10 dB face à l'intégration.
  golden(drive_track_track 4a1f10e1 --preset inline_six --scenario lap --seconds 40 --drive-scenario track --shift-mode track)
  # Finitions : 67cec9e3 -> e2e77d19. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau −0,41 dB face à l'intégration.
  golden(drive_hill_start_eco e2e77d19 --preset inline_six --scenario lap --seconds 40 --drive-scenario hill_start --shift-mode eco)
  # Finitions : f7acef8f -> 02ed74fb. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, et la logique de passage ne monte plus, roues patinant, un rapport que la vitesse de la voiture ne tient pas. Niveau −0,05 dB face à l'intégration.
  golden(drive_towing_normal 02ed74fb --preset inline_six --scenario lap --seconds 40 --drive-scenario towing --shift-mode normal)
  # La carrosserie des années 80, sur un moteur aux réglages neutres : sa ligne, son plan et sa caisse.
  # Finitions : fe72a187 -> 076aa0ed. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,16 dB face à l'intégration.
  golden(lap_retro 076aa0ed --cylinders 6 --cc 2500 --bore 84 --redline 6400 --exhaust stock --body retro --mass 1150 --rim 14 --drive rear --insulation light --scenario lap)
  # Finitions : a0889b48 -> aaef18f2. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,22 dB face à l'intégration.
  golden(turbo_pipe_long aaef18f2 --preset hot_hatch_turbo --scenario turbo --turbo-pipe 2500)
  # Finitions : 3898802e -> dbb43fb3. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,09 dB face à l'intégration.
  golden(turbo_pipe_short dbb43fb3 --preset hot_hatch_turbo --scenario turbo --turbo-pipe 150)
  # Finitions : 463a064f -> 11d4d98e. thermique et air : réseau thermique branché moteur chaud. Niveau +0,05 dB face à l'intégration.
  golden(carb_multi 11d4d98e --preset carb_twin_cam --scenario blips)
  # Finitions : 4d1f365e -> a72cb163. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,10 dB face à l'intégration.
  golden(carb_single a72cb163 --preset hot_hatch_na --carb single --scenario launch --seconds 10)
  # Les architectures : un VR6 (une culasse), un W16 et un V14 en deux lignes (culasses, intervalles inégaux), un W12 au démarreur.
  golden(steady_vr6 dc6c1925 --preset vr6_golf --scenario steady --rpm 4000 --seconds 2)
  golden(steady_w16 ff905d02 --cylinders 16 --layout w --cc 7993 --bore 86 --redline 6400 --exhaust sport --routing dual --scenario steady --rpm 4000 --seconds 2)
  golden(steady_v14 1ce07227 --cylinders 14 --layout v --cc 7000 --bore 86 --redline 6000 --exhaust sport --routing dual --scenario steady --rpm 3000 --seconds 2)
  # Finitions : f62d0dc4 -> 0b08eb1b. thermique et air : réseau thermique branché moteur chaud. Niveau −0,07 dB face à l'intégration.
  golden(start_w12 0b08eb1b --cylinders 12 --layout w --cc 5998 --bore 84 --redline 6500 --scenario start)
  # Finitions : efe03b28 -> ce5051da. thermique et air : réseau thermique branché moteur chaud. Niveau −0,05 dB face à l'intégration.
  golden(blips_w8 ce5051da --cylinders 8 --layout w --cc 3999 --bore 84 --redline 6400 --exhaust sport --routing dual --scenario blips)
  # `--layout` l'emporte sur un préréglage, sur ses cylindres : le V12 en W12, même empreinte que le W12 écrit en entier.
  golden(layout_preset 14b15d69 --preset v12 --layout w --scenario steady --seconds 1)
  golden(layout_explicit 14b15d69 --cylinders 12 --layout w --cc 6496 --bore 94 --redline 8900 --exhaust no_muffler --routing dual --body supercar --mass 1650 --rim 21 --drive all --placement mid --insulation light --gearbox dual_clutch --scenario steady --seconds 1)
  # L'air du jour : un turbo à 3000 m, une citadine par grand froid, un carburateur qui s'enrichit en altitude, l'électrique.
  # Finitions : 31d03535 -> f55b7994. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,20 dB face à l'intégration.
  golden(altitude_turbo f55b7994 --preset hot_hatch_turbo --scenario turbo --altitude 3000)
  # Finitions : 054e6de4 -> ef6fef51. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau −0,14 dB face à l'intégration.
  golden(ambient_cold_lap ef6fef51 --preset city_14 --scenario lap --seconds 20 --ambient -20)
  # Finitions : d84f8e06 -> 2d6eb403. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle. Niveau −0,01 dB face à l'intégration.
  golden(altitude_carb 2d6eb403 --preset carb_twin_cam --scenario blips --altitude 2500)
  # Finitions : d0299648 -> f923ce33. conduite : pneu à sommet. Niveau +0,09 dB face à l'intégration.
  golden(ambient_electric f923ce33 --preset electric --scenario lap --seconds 20 --ambient 45 --altitude 1500)
  # Départ à froid : ralenti accéléré, frottements de l'huile froide ; et le starter d'un carburateur par -10 °C.
  # Intégration : b3a7b95a -> 25dc320d. Après un départ à froid, le film de l'injection indirecte lit l'eau du réseau thermique, et la paroi froide réchauffe moins l'air piégé.
  # Finitions : 25dc320d -> d3d404e4. thermique et air : départ à froid : l'échangeur eau-huile réchauffe l'huile par l'eau, et l'air piégé lit l'eau, non plus le métal. Niveau −0,48 dB face à l'intégration.
# Démarreur (froid-démarreur) : d3d404e4 -> d6e113cd. Huile à 20 °C, sous 80 °C : le lancement se joue cycle par cycle. La
  # batterie est à l'air (20 °C) et non au compartiment chaud de la référence (32 °C), l'huile freine en (μ/μ₈₀)^0,86, le
  # calculateur se synchronise en tours, puis la première explosion lance le moteur : prise à 0,99 s (0,82 s avant), 283 tr/min
  # d'entraînement. Niveau +0,3 dB (LAeq sur 8 s).
  golden(start_cold_city d6e113cd --preset city_14 --start cold --scenario start --seconds 8)
  # Intégration : par -10 °C, l'air est aussi plus dense (atmosphère) ; avec l'air de référence, l'empreinte redevient d3a95f44.
  # Intégration : fdd3932c -> 558e94b7. Après un départ à froid, la paroi froide réchauffe moins l'air piégé (thermique et atmosphère),
  # et le ralenti accéléré garde l'autorité du démarrage (0,8 du remplissage), et non 0,45 comme moteur chaud.
  # Finitions : 558e94b7 -> f9aa6b8e. thermique et air : départ à froid : l'échangeur eau-huile réchauffe l'huile par l'eau, et l'air piégé, comme le film des carburateurs, lit l'eau, non plus le métal, carte du cliquetis à l'eau réelle (par −10 °C, elle existe) ; conduite : départ à froid, le modèle posé au ralenti part collecteur au remplissage du ralenti, et non vide. Niveau +0,11 dB face à l'intégration.
# Démarreur (froid-démarreur) : f9aa6b8e -> c14ff92d. Les carburateurs des années 1970 ont leur huile, une 20W-50 minérale :
  # par -10 °C (huile sous 80 °C), sa viscosité suit sa propre loi de Vogel, deux fois celle de la 5W-30 de référence ; le ralenti
  # froid frotte plus, les coups de gaz montent moins : niveau −1,3 dB. Moteur chaud, rien ne change (au-dessus de 80 °C, chaque
  # huile frotte comme la référence).
  golden(blips_cold_carb c14ff92d --preset carb_twin_cam --start cold --ambient -10 --scenario blips --seconds 8)
  # Démarreur : par -30 °C, la batterie froide et l'huile épaisse lancent la citadine à 153 tr/min ; synchronisation en tours,
  # explosions une à une, prise à 1,8 s. Le diesel préchauffe 5,8 s par -20 °C, puis prend. La Cadillac en SAE 30 d'été par
  # -10 °C : 103 tr/min, prise à 1,5 s. Niveaux, face au démarrage d'avant : +2,2 dB, −3,4 dB (le préchauffage, muet), +1,5 dB.
  golden(start_cold_minus30 2d044622 --preset city_14 --start cold --ambient -30 --scenario start --seconds 6)
  golden(start_cold_diesel 0c8b3e85 --preset diesel_four --start cold --ambient -20 --scenario start --seconds 10)
  golden(start_cold_cadillac f3dfe054 --preset v16_cadillac --start cold --ambient -10 --scenario start --seconds 6)
  # La combustion : des carburateurs à 12:1 qui cliquettent sans capteur, l'E85 sur un turbo, un diesel moins comprimé au démarrage, un rotor plus creusé.
  # Intégration : cd653d6e -> 854924ae. La vaporisation prend à l'air la part d'un carburateur (0,5, injection), non plus 0,15 pour toute alimentation.
  # Finitions : 854924ae -> c9315cfc. régulation : le cycle du cliquetis perd sa chaleur aux parois pas à pas (Woschni) : ces carburateurs sans capteur cliquettent plus, et plus haut ; thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau −0,02 dB face à l'intégration.
  golden(knock_carb c9315cfc --preset carb_twin_cam --compression 12 --scenario turbo --seconds 16)
  # Intégration : 8c6eee0c -> 98ff3f12. La vaporisation prend à l'air la part d'une injection indirecte (0,2, injection), non plus 0,15.
  # Finitions : 98ff3f12 -> 29ab8533. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle, air-air ε-NTU ; conduite : pneu à sommet. Niveau 0,00 dB face à l'intégration.
  golden(compression_e85_turbo 29ab8533 --preset hot_hatch_turbo --compression 10.5 --grade e85 --scenario launch --seconds 6)
  # Finitions : a48c481b -> 74995596. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU. Niveau −0,13 dB face à l'intégration.
  golden(compression_diesel_start 74995596 --preset diesel_four --compression 15 --scenario start)
  # Finitions : f21ca74c -> 3df8973b. régulation : le cycle du cliquetis perd sa chaleur aux parois (Woschni), et la carte d'origine en est recalculée ; thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau +0,09 dB face à l'intégration.
  golden(compression_rotary 3df8973b --preset twin_rotor_13 --compression 11 --scenario lap --seconds 10)
  # Le calculateur, actif : la coupure d'allumage et ses détonations, le rupteur progressif avec une carte réglée, le
  # launch control au frein (le 0-100 s'écrit avant l'empreinte), le passage à fond, l'antipatinage.
  # Finitions : 2af90e2c -> d4162846. thermique et air : réseau thermique branché moteur chaud. Niveau −0,10 dB face à l'intégration.
  golden(ecu_ignition_cut d4162846 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust sport --limiter ignition_cut --scenario blips)
  # Intégration : 99069727 -> 6b783b4b. La part de couple de l'avance passe par la carte du cliquetis, qui part de la carte d'origine réelle ; le calculateur garde carburant et chaleur.
  # Finitions : 6b783b4b -> 2d0b1ae4. régulation : le meilleur couple est le sommet du cycle aux pertes aux parois : +6° y coûtent, et les 15° du rupteur progressif 7,0 % (Ayala) ; thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle. Niveau −0,05 dB face à l'intégration.
  golden(ecu_soft_map 2d0b1ae4 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --exhaust sport --limiter soft --advance 6 --lambda 0.95 --scenario blips)
  # Finitions : 7fd42b07 -> 4af3a60d. régulation : les 25° de retard du launch control coûtent 15,9 % de couple (Ayala) au lieu de 13,5 % ; thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet, et le robot du départ ne serre plus que le couple annoncé par le moteur. Niveau 0,00 dB face à l'intégration.
  golden(ecu_launch_control 4af3a60d --preset hot_hatch_turbo --launch 4500 --scenario launch_control)
  # Finitions : 7d3dc773 -> be544e4d. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau +0,02 dB face à l'intégration.
  golden(ecu_flat_shift be544e4d --preset hot_hatch_turbo --flat-shift 1 --scenario launch --seconds 12)
  # Finitions : 0bc683ee -> f87f7ea5. régulation : l'antipatinage retire son couple par l'inverse de la corrélation d'Ayala ; thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet, l'antipatinage vise un glissement κ (le sommet en sport) avec des gains placés sur la transmission. Niveau +0,04 dB face à l'intégration.
  golden(ecu_traction f87f7ea5 --cylinders 8 --layout cross_plane --cc 7000 --bore 99 --redline 6000 --exhaust sport --body coupe --mass 1700 --rim 19 --drive rear --traction sport --scenario launch --seconds 10)
  # L'injection : le tic d'une injection directe (sous le capot, au ralenti), la rampe commune, la recirculation et le filtre.
  # Intégration : 19ac1ec5 -> 038cacdf. La chaleur latente vient du carburant (350 kJ/kg, combustion), et la carte du cliquetis lit le refroidissement de l'injection directe.
  # Finitions : 038cacdf -> 808814ba. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle. Niveau −0,14 dB face à l'intégration.
  golden(injection_direct_blips 808814ba --preset hot_hatch_na --fuel-system direct --scenario blips)
  # Intégration : 29fbf825 -> be119bb8. La chaleur latente vient du carburant (350 kJ/kg, combustion), et la carte du cliquetis lit le refroidissement de l'injection directe.
  # Finitions : be119bb8 -> 92457b85. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle. Niveau −0,12 dB face à l'intégration.
  golden(injection_direct_bay 92457b85 --preset hot_hatch_na --fuel-system direct --scenario idle --seconds 4 --listener bay)
  # Finitions : 4781fcd4 -> c9bb827d. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,29 dB face à l'intégration.
  golden(injection_common_rail c9bb827d --preset diesel_four --diesel-injection common_rail --scenario lap --seconds 20)
  # Finitions : 089dcc16 -> 38fa9b94. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,19 dB face à l'intégration.
  golden(injection_diesel_egr_dpf 38fa9b94 --preset diesel_four --egr 1 --dpf 1 --scenario lap --seconds 20)
  # Finitions : db5318c3 -> bb46f745. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,03 dB face à l'intégration.
  golden(injection_petrol_egr bb46f745 --preset city_14 --egr 1 --scenario lap --seconds 20)
  # La came : ralenti d'une came pointue, bascule de deux profils, déphaseur sur un tour.
  # Finitions : 84564533 -> 6693b901. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle. Niveau +0,33 dB face à l'intégration.
  golden(cam_race_idle 6693b901 --preset hot_hatch_na --scenario idle --seconds 6 --cam-duration 300 --cam-lift 13 --lsa 104)
  # Finitions : 8f78b11d -> 11622649. régulation : le cycle du cliquetis perd sa chaleur aux parois (Woschni) : la marge la plus faible passe de −2,8 à −2,3° ; thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau −0,19 dB face à l'intégration.
  golden(cam_two_step_launch 11622649 --preset hot_hatch_na --vvt two_step --scenario launch --seconds 10)
  # Finitions : 6b59c2ee -> 48d0daf2. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau +0,01 dB face à l'intégration.
  golden(cam_phased_lap 48d0daf2 --preset hot_hatch_na --vvt phased --scenario lap --seconds 20)
  # Les culbuteurs : le choc des poussoirs au ralenti sous le capot, puis un tour avec des ressorts raides. L'affolement :
  # le son seul, tenu au-delà (8140 tr/min sur ce moteur), là où aucun rupteur ne l'arrête.
  golden(ohv_idle_bay 8a51c3c3 --preset v8_cross_plane --cam ohv --scenario steady --rpm 900 --listener bay --seconds 2)
  # Finitions : bd8fba88 -> 4ebd1052. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,04 dB face à l'intégration.
  golden(ohv_lap_stiff 4ebd1052 --preset v8_cross_plane --cam ohv --springs stiff --scenario lap --seconds 20)
  golden(valve_float_bay 73732eab --preset hot_hatch_na --scenario steady --rpm 9000 --listener bay --seconds 2)
  # La régulation de pression : l'électrovanne et son PID sur le coup de gaz en quatrième ; selon le rapport, une traction qui part.
  # Finitions : 6b201abd -> 8de72f23. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet ; fusion : le banc d'étalonnage attend que la pression soit établie, faisceau stabilisé (`identifyPlant`). Niveau −0,18 dB face à l'intégration.
  golden(boost_electronic 8de72f23 --preset hot_hatch_turbo --scenario turbo --boost-control electronic)
  # Finitions : 64bde81c -> 1ca1cdc0. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet : la cible selon le rapport le tient près de son sommet ; fusion : le banc d'étalonnage attend que la pression soit établie, faisceau stabilisé (`identifyPlant`). Niveau −0,17 dB face à l'intégration.
  golden(boost_by_gear 1ca1cdc0 --preset three_cyl_turbo --scenario launch --seconds 10 --boost-control by_gear)
  # Deux turbos, twincharger, aubes : twin_turbo.cpp, et le second sifflement.
  # Finitions : 4a3f8582 -> f8f28aa4. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau −0,06 dB face à l'intégration.
  golden(twin_parallel_v6 f8f28aa4 --cylinders 6 --layout v --cc 3000 --bore 86 --redline 6800 --aspiration turbo --boost 1 --exhaust sport --body coupe --mass 1500 --rim 18 --drive rear --scenario turbo --seconds 12 --turbo-layout twin_parallel)
  # Finitions : 32336632 -> 03704d26. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau +0,04 dB face à l'intégration.
  golden(twin_sequential 03704d26 --preset hot_hatch_turbo --turbo-layout twin_sequential --scenario turbo --seconds 12)
  # Finitions : 48a5fa18 -> 33937b13. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU, et le compresseur dimensionné avec lui ; conduite : pneu à sommet. Niveau +0,01 dB face à l'intégration.
  golden(twincharger 33937b13 --preset hot_hatch_turbo --turbo-layout twincharger --scenario launch --seconds 10)
  # Finitions : c1df8a8e -> c3bb0c16. thermique et air : réseau thermique branché moteur chaud, air-air ε-NTU ; conduite : pneu à sommet. Niveau +0,08 dB face à l'intégration.
  golden(vgt_diesel c3bb0c16 --preset diesel_four --vgt 1 --scenario turbo --seconds 12)
  # Finitions (régulation) : l'électrovanne pilote les deux soupapes d'un séquentiel, PID étalonné sur le banc des deux turbos. 960d1218 -> 76b61dc9 : réseau thermique branché moteur chaud et air-air ε-NTU, comme twin_sequential, pneu à sommet ; fusion : le banc d'étalonnage attend l'équilibre, et la précommande des petits turbos tient leurs roues à leur limite (`turboSteady`).
  golden(twin_sequential_electronic 76b61dc9 --preset hot_hatch_turbo --turbo-layout twin_sequential --boost-control electronic --scenario turbo --seconds 12)
  # L'air de charge : sans échangeur, air-eau, injection d'eau, protoxyde.
  # Intégration : 07dfbb8d -> 34de3f5e. La carte du cliquetis lit la charge sans échangeur (air de charge), plus chaude.
  # Finitions : 34de3f5e -> fa3b9772. régulation : le cycle du cliquetis perd sa chaleur aux parois (Woschni) ; thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle, l'air-air ε-NTU de série, sur lequel restent dimensionnés la boîte, l'embrayage et la carte d'origine du cliquetis ; conduite : pneu à sommet. Niveau −0,02 dB face à l'intégration.
  golden(charge_none_turbo fa3b9772 --preset hot_hatch_turbo --intercooler none --scenario turbo)
  # Finitions : efc2c12e -> fc147f6a. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle, l'air-air ε-NTU de série, sur lequel restent dimensionnés la boîte, l'embrayage, la carte d'origine du cliquetis et le compresseur ; conduite : pneu à sommet. Niveau −0,52 dB face à l'intégration.
  golden(charge_water_cooler fc147f6a --preset v8_supercharged --intercooler water --scenario launch --seconds 10)
  # Finitions : 59ac8a9c -> 4e855775. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle, air-air ε-NTU ; conduite : pneu à sommet. Niveau +0,01 dB face à l'intégration.
  golden(charge_water_injection 4e855775 --preset hot_hatch_turbo --water 1 --scenario launch --seconds 10)
  # Finitions : bedbec6d -> 965ba0ce. thermique et air : réseau thermique branché moteur chaud, carte du cliquetis à l'eau réelle ; conduite : pneu à sommet. Niveau −0,17 dB face à l'intégration.
  golden(charge_nitrous 965ba0ce --preset hot_hatch_na --nitrous 75 --scenario launch --seconds 10)
  # Le deux-temps : son cycle intégré, ses lumières, ses ratés (à leur propre générateur), puis sa chambre de détente.
  # Finitions : 6d847fab -> 0af7a40d. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau 0,00 dB face à l'intégration.
  golden(lap_two_stroke 0af7a40d --preset two_stroke_triple --scenario lap --seconds 20)
  # Finitions : fc2d68ae -> 3dbb4d96. thermique et air : réseau thermique branché moteur chaud. Niveau −0,16 dB face à l'intégration.
  golden(idle_two_stroke 3dbb4d96 --preset two_stroke_triple --scenario idle --seconds 10)
  # Finitions : fde00d9e -> 0ad085b7. thermique et air : réseau thermique branché moteur chaud ; conduite : pneu à sommet. Niveau −0,06 dB face à l'intégration.
  golden(lap_two_stroke_expansion 0ad085b7 --preset two_stroke_triple --exhaust expansion --scenario lap --seconds 20)
  # Un bicylindre comme la Trabant, sans modèle de conduite : la charge déduite du régime, et ses ratés.
  golden(steady_two_stroke_twin 38e43381 --cylinders 2 --cc 595 --bore 72 --redline 4500 --cycle two_stroke --carb single --scenario steady --rpm 900 --seconds 3)
  # `--fuel` se lit à sa valeur : petrol ou diesel, le carburant ; 0 ou 1, l'affichage de la consommation. Un diesel n'a pas d'étincelle.
  add_test(NAME cli_fuel_diesel COMMAND revforge-render --out none --cylinders 4 --cc 1900 --bore 82 --redline 4500 --fuel diesel --scenario steady --seconds 1 --knock 1)
  set_tests_properties(cli_fuel_diesel PROPERTIES PASS_REGULAR_EXPRESSION "Knock: none \\(no spark\\)")
endif()
