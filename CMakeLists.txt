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
  core/src/driving_model.cpp
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
    "-sEXPORTED_FUNCTIONS=_rf_init,_rf_buffer,_rf_render,_rf_set_config,_rf_set_electric,_rf_set_mode,_rf_set_controls,_rf_set_manual_rpm,_rf_set_volume,_rf_rpm,_rf_car_rpm,_rf_speed,_rf_gear,_rf_throttle,_rf_load,_rf_boost,_rf_from_car_rpm,_rf_idle_rpm,_rf_multiplier")
else()
  add_executable(revforge-render cli/render.cpp)
  target_link_libraries(revforge-render PRIVATE revforge_core)

  enable_testing()
  add_executable(revforge_tests tests/main.cpp tests/engine_sound_test.cpp tests/engine_config_test.cpp tests/driving_model_test.cpp)
  target_include_directories(revforge_tests PRIVATE tests/vendor)
  target_link_libraries(revforge_tests PRIVATE revforge_core)
  # Avertissement d'un en-tête de la bibliothèque standard inclus par doctest.
  target_compile_options(revforge_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-\#warnings> $<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>)
  add_test(NAME revforge_tests COMMAND revforge_tests)

  # Empreintes du rendu 16 bits : changer le son doit être un choix, et se voit ici.
  # Les valeurs d'origine sont identiques, au bit près, à la version Kotlin de CANCitroen.
  function(golden name hash)
    add_test(NAME golden_${name} COMMAND revforge-render --out - ${ARGN})
    set_tests_properties(golden_${name} PROPERTIES PASS_REGULAR_EXPRESSION "^${hash}")
  endfunction()
  golden(steady_city b35f0e57 --preset city_14 --scenario steady --rpm 5000)
  golden(blips_stock 079d8b52 --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust stock --scenario blips)
  golden(blips_straight 8981275c --cylinders 4 --cc 2000 --bore 86 --redline 7200 --pops strong --exhaust straight --scenario blips)
  golden(lap_v10 e5089da9 --preset v10_open --scenario lap)
  golden(lap_boxer_turbo c52c08e8 --preset boxer_four_turbo --scenario lap)
  golden(lap_diesel fef5cacb --preset diesel_four --scenario lap)
  golden(lap_electric 60045629 --preset electric --scenario lap)
  golden(lap_multiplier 0f9c3bf7 --cylinders 4 --cc 2000 --bore 86 --redline 12000 --exhaust sport --multiplier 2.6 --scenario lap)
endif()
