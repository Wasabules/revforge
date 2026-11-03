#include "revforge/sound_sheet.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "exhaust_line.hpp"
#include "revforge/engine_sound.hpp"

namespace revforge {

namespace {

constexpr int kRate = 44100;

/** Moteur tenu à ce régime et cette charge. Niveau moyen sur la fin, sans ce qui précède : ça se stabilise d'abord. */
float heldLevel(EngineSound& sound, const EngineProfile& profile, float rpm, float load) {
    sound.setProfile(profile);
    sound.setVolume(0.7f);
    sound.setTargetRpm(rpm);
    sound.setDrive(load, load > 0.9f && (profile.turbo || profile.supercharged) ? 1.0f : 0.0f, false, false);
    constexpr int kBlock = 512;
    std::vector<float> buffer(kBlock);
    double energy = 0.0;
    int counted = 0;
    // 0,4 s pour que le son s'installe (le filtre de son du sonomètre compris), 0,25 s de mesure.
    for (int done = 0; done < kRate * 65 / 100; done += kBlock) {
        sound.render(buffer.data(), kBlock);
        if (done > kRate * 4 / 10) {
            energy += std::pow(10.0, static_cast<double>(sound.levelDbA()) / 10.0);
            ++counted;
        }
    }
    return static_cast<float>(10.0 * std::log10(energy / counted));
}

}  // namespace

double standstillTestRpm(const Powertrain& p) {
    const double s = powerSummary(p).peakPowerRpm;
    if (s <= 5000.0) return 0.75 * s;
    if (s < 7500.0) return 3750.0;
    return 0.5 * s;
}

SoundSheet soundSheet(const Powertrain& p) {
    SoundSheet sheet;
    const EngineProfile profile = p.profile();
    const float idle = profile.idleRpm;
    const float full = profile.electric ? 6000.0f : 0.75f * profile.redlineRpm;
    if (!profile.electric) {
        EngineSound sound(kRate);
        const Point3& outlet = profile.car.sources[static_cast<std::size_t>(Source::ExhaustA)].at;
        const float side = outlet.y >= 0.0f ? 1.0f : -1.0f;
        sound.setListenerPosition(outlet.x - 0.354f, outlet.y + side * 0.354f, std::max(outlet.z, 0.2f));
        // Sans charge : le papillon à peine ouvert.
        sheet.standstillDb = heldLevel(sound, profile, static_cast<float>(standstillTestRpm(p)), 0.2f);
    }
    {
        EngineSound sound(kRate);
        sound.setListener(Listener::Outside);
        sheet.outsideFullDb = heldLevel(sound, profile, full, 1.0f);
    }
    {
        EngineSound sound(kRate);
        sound.setListener(Listener::Cabin);
        sheet.cabinIdleDb = heldLevel(sound, profile, idle, 0.12f);
        sheet.cabinFullDb = heldLevel(sound, profile, full, 1.0f);
    }
    return sheet;
}

float lineLossDb(const Powertrain& p, double fromHz, double toHz) {
    if (p.electric) return 0.0f;
    const EngineProfile profile = p.profile();
    const double displacement = 0.7854 * static_cast<double>(profile.boreM) * profile.boreM * profile.strokeM * profile.cylinders;
    ExhaustLine line(static_cast<float>(kRate));
    line.compute(Exhaust::Straight, profile.exhaustScale, displacement, profile.cylinders);
    const double open = line.lossDb(fromHz, toHz);
    line.compute(profile.exhaust, profile.exhaustScale, displacement, profile.cylinders, profile.filterResistance);
    return static_cast<float>(line.lossDb(fromHz, toHz) - open);
}

}  // namespace revforge
