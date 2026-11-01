#pragma once

#include <cstddef>

#include "revforge/engine_config.hpp"
#include "revforge/powertrain.hpp"

namespace revforge {

struct Preset {
    const char* id;
    const char* name;
    const char* description;
    EngineConfig config;
    bool electric;
    CarConfig car;
    GearboxConfig gearbox;

    EngineProfile profile() const { return powertrain().profile(); }
    Powertrain powertrain() const { return Powertrain{config, electric, car, gearbox}; }
};

const Preset* presets(std::size_t& count);
const Preset* findPreset(const char* id);

}  // namespace revforge
