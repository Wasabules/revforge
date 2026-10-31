// Rend un moteur en WAV, sans navigateur : pour écouter, mesurer et comparer.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

constexpr int kTickDivider = 20;

void usage() {
    std::puts(
        "revforge-render --out fichier.wav|- [--preset ID | réglages] [--scenario lap|blips|steady]\n"
        "  --list            liste les préréglages\n"
        "  --seconds N       durée (lap : 60, blips : 14, steady : 4)\n"
        "  --rpm N           régime pour steady\n"
        "  --rate N          fréquence d'échantillonnage (44100)\n"
        "  --volume X        volume (0.7)\n"
        "Réglages : --cylinders N --layout inline|v|cross_plane|boxer --cc N --bore N --redline N\n"
        "           --exhaust stock|sport|straight --aspiration natural|turbo --fuel petrol|diesel\n"
        "           --pops off|light|strong --release dump_valve|flutter --multiplier X");
}

template <typename T>
T pick(const std::string& value, std::initializer_list<std::pair<const char*, T>> options) {
    for (const auto& [name, option] : options) {
        if (value == name) return option;
    }
    std::fprintf(stderr, "Valeur inconnue : %s\n", value.c_str());
    std::exit(2);
}

void writeWav(const std::string& path, const std::vector<float>& pcm, int rate) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    for (float s : pcm) {
        const int v = std::clamp(static_cast<int>(s * 32767.0f), -32768, 32767);
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
    }
}

/** Trois montées franches suivies d'un lever de pied, comme le bouton « Coups de gaz ». */
void blips(EngineSound& sound, const EngineProfile& p, std::vector<float>& out, int rate) {
    const int tick = rate / kTickDivider;
    const float top = p.idleRpm + (p.redlineRpm - p.idleRpm) * 0.85f;
    std::size_t done = 0;
    int step = 0;
    while (done < out.size()) {
        const int offset = step - 20;
        const int inBlip = ((offset % 80) + 80) % 80;
        float target;
        if (step < 20) {
            target = p.idleRpm;
        } else if (inBlip < 16) {
            target = p.idleRpm + (top - p.idleRpm) * (static_cast<float>(inBlip) / 16.0f);
        } else if (inBlip < 24) {
            target = top;
        } else if (inBlip < 54) {
            const float it = 1.0f - static_cast<float>(inBlip - 24) / 30.0f;
            target = p.idleRpm + (top - p.idleRpm) * it * it;
        } else {
            target = p.idleRpm;
        }
        sound.setTargetRpm(target);
        const int n = static_cast<int>(std::min<std::size_t>(tick, out.size() - done));
        sound.render(out.data() + done, n);
        done += n;
        ++step;
    }
}

/** Le modèle de conduite pilote le son, comme le mode balade du site. */
void lap(EngineSound& sound, const EngineProfile& p, std::vector<float>& out, int rate) {
    DrivingModel model;
    const int tick = rate / kTickDivider;
    std::size_t done = 0;
    while (done < out.size()) {
        model.step(0.05);
        sound.setTargetRpm(p.fromCarRpm(static_cast<float>(model.rpm())));
        const int n = static_cast<int>(std::min<std::size_t>(tick, out.size() - done));
        sound.render(out.data() + done, n);
        done += n;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string out, scenario = "lap", presetId;
    EngineConfig config;
    bool customConfig = false;
    int rate = 44100, seconds = 0;
    float rpm = 3000.0f, volume = 0.7f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--list") {
            std::size_t count = 0;
            const Preset* all = presets(count);
            for (std::size_t n = 0; n < count; ++n) std::printf("%-18s %s\n", all[n].id, all[n].name);
            return 0;
        }
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        const std::string value = argv[++i];
        if (arg == "--out") out = value;
        else if (arg == "--scenario") scenario = value;
        else if (arg == "--preset") presetId = value;
        else if (arg == "--rate") rate = std::atoi(value.c_str());
        else if (arg == "--seconds") seconds = std::atoi(value.c_str());
        else if (arg == "--rpm") rpm = static_cast<float>(std::atof(value.c_str()));
        else if (arg == "--volume") volume = static_cast<float>(std::atof(value.c_str()));
        else {
            customConfig = true;
            if (arg == "--cylinders") config.cylinders = std::atoi(value.c_str());
            else if (arg == "--cc") config.displacementCc = std::atoi(value.c_str());
            else if (arg == "--bore") config.boreMm = std::atoi(value.c_str());
            else if (arg == "--redline") config.redlineRpm = std::atoi(value.c_str());
            else if (arg == "--multiplier") config.revMultiplier = static_cast<float>(std::atof(value.c_str()));
            else if (arg == "--layout") config.layout = pick<Layout>(value, {{"inline", Layout::Inline}, {"v", Layout::V}, {"cross_plane", Layout::CrossPlane}, {"boxer", Layout::Boxer}});
            else if (arg == "--exhaust") config.exhaust = pick<Exhaust>(value, {{"stock", Exhaust::Stock}, {"sport", Exhaust::Sport}, {"straight", Exhaust::Straight}});
            else if (arg == "--aspiration") config.aspiration = pick<Aspiration>(value, {{"natural", Aspiration::Natural}, {"turbo", Aspiration::Turbo}});
            else if (arg == "--fuel") config.fuel = pick<Fuel>(value, {{"petrol", Fuel::Petrol}, {"diesel", Fuel::Diesel}});
            else if (arg == "--pops") config.pops = pick<PopLevel>(value, {{"off", PopLevel::Off}, {"light", PopLevel::Light}, {"strong", PopLevel::Strong}});
            else if (arg == "--release") config.turboRelease = pick<TurboRelease>(value, {{"dump_valve", TurboRelease::DumpValve}, {"flutter", TurboRelease::Flutter}});
            else {
                std::fprintf(stderr, "Option inconnue : %s\n", arg.c_str());
                return 2;
            }
        }
    }
    if (out.empty()) {
        usage();
        return 2;
    }

    EngineProfile profile = config.toProfile();
    if (!presetId.empty()) {
        const Preset* preset = findPreset(presetId.c_str());
        if (preset == nullptr) {
            std::fprintf(stderr, "Préréglage inconnu : %s\n", presetId.c_str());
            return 2;
        }
        profile = preset->profile();
    } else if (!customConfig) {
        std::size_t count = 0;
        profile = presets(count)[0].profile();
    }

    if (seconds == 0) seconds = scenario == "lap" ? 60 : (scenario == "blips" ? 14 : 4);
    EngineSound sound(rate);
    sound.setProfile(profile);
    sound.setVolume(volume);
    std::vector<float> pcm(static_cast<std::size_t>(rate) * seconds);
    if (scenario == "lap") {
        lap(sound, profile, pcm, rate);
    } else if (scenario == "blips") {
        blips(sound, profile, pcm, rate);
    } else if (scenario == "steady") {
        sound.setTargetRpm(rpm);
        sound.render(pcm.data(), static_cast<int>(pcm.size()));
    } else {
        usage();
        return 2;
    }
    if (out == "-") {
        // Empreinte du rendu 16 bits : détecte tout écart, jusqu'au dernier bit.
        std::uint32_t hash = 2166136261u;
        for (float sample : pcm) {
            const int v = std::clamp(static_cast<int>(sample * 32767.0f), -32768, 32767);
            hash = (hash ^ static_cast<std::uint32_t>(v & 0xFFFF)) * 16777619u;
        }
        std::printf("%08x\n", hash);
        return 0;
    }
    writeWav(out, pcm, rate);
    return 0;
}
