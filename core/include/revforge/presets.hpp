#pragma once

#include <cstddef>

#include "revforge/engine_config.hpp"

namespace revforge {

struct Preset {
    const char* id;
    const char* name;
    const char* description;
    EngineConfig config;
    bool electric;

    EngineProfile profile() const { return electric ? electricProfile() : config.toProfile(); }
};

const Preset* presets(std::size_t& count);
const Preset* findPreset(const char* id);

}  // namespace revforge
