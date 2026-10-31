#include "revforge/presets.hpp"

#include <cstring>

namespace revforge {

namespace {
const Preset kPresets[] = {
#include "presets_data.inc"
};
}  // namespace

const Preset* presets(std::size_t& count) {
    count = sizeof(kPresets) / sizeof(kPresets[0]);
    return kPresets;
}

const Preset* findPreset(const char* id) {
    for (const Preset& preset : kPresets) {
        if (std::strcmp(preset.id, id) == 0) return &preset;
    }
    return nullptr;
}

}  // namespace revforge
