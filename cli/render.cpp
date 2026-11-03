// Rend un moteur en WAV, sans navigateur : pour écouter, mesurer et comparer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

// Par petits blocs, comme le navigateur : le son suit le modèle de très près.
constexpr int kTick = 128;

void usage() {
    std::puts(
        "revforge-render --out file.wav|-|none [--preset ID | settings] [--scenario lap|blips|launch|start|steady|passby|rattle|shifts|turbo|grind|reverse|clutchblips|idle|cruise|wltc]\n"
        "  --out none        drive the model without rendering any sound: for the figures of --fuel\n"
        "  --list            list the presets\n"
        "  --seconds N       duration (lap: 60, or one loop of --drive-scenario; blips: 14, steady: 4)\n"
        "  --drive-scenario city|stop_go|country|motorway|mountain|drag|track|hill_start|towing   the autopilot's journey, with --scenario lap\n"
        "  --shift-mode normal|eco|sport|track|paddles   how the automatic picks its gears (paddles: the autopilot drives in normal)\n"
        "  --rpm N           engine speed for steady\n"
        "  --kmh N           speed held by cruise (the trip is measured once settled, after 60 s)\n"
        "  --rate N          sample rate (44100)\n"
        "  --volume X        volume (0.7)\n"
        "Engine:   --cylinders N --layout inline|v|cross_plane|boxer --cc N --bore N --redline N\n"
        "          --exhaust stock|sport|straight|no_muffler|no_resonator|quiet --routing single|dual|h|x\n"
        "          --aspiration natural|turbo|supercharger --fuel petrol|diesel\n"
        "          --pops off|light|strong --release dump_valve|flutter --boost BAR --turbo small|medium|large\n"
        "          --supercharger roots|twin_screw|centrifugal --antilag off|retard|air|cut --turbo-pipe MM\n"
        "          --cam dohc|sohc --filter stock|panel|cone|stacks\n"
        "          --stroke MM  sets --cc from the bore and the cylinders, in any order, and replaces --cc if both are given.\n"
        "                       On a --preset: applied to the preset's own bore and cylinders, which --bore and --cylinders do not change.\n"
        "Car:      --body city|compact|coupe|supercar|van --mass KG --rim INCHES --drive front|rear|all\n"
        "          --placement front|mid|rear --insulation stripped|light|standard|comfort|luxury --windows open|closed\n"
        "Gearbox:  --gearbox synchro|dog|sequential|dual_clutch|converter --gears N --family matched|short|close|long\n"
        "          --ratios 3.2,2.0,1.4,1.1,0.9 --final X --teeth helical|spur --helix DEG --module MM --limit NM\n"
        "          --flywheel single|dual_mass\n"
        "Listener: --listener outside|front|side|far|pipe|cabin|bay  or  --at X,Y,Z (m: x forward, y left, z up)\n"
        "          --stereo 1: two ears (otherwise one microphone at the centre of the head)\n"
        "          --db 1: also print the estimated A-weighted level\n"
        "          --fuel 1: also print the estimated fuel, oil or energy used, and the best of the engine's fuel map\n"
        "          --walk 1: the head walks through the car at 1.2 m/s (with lap)");
}

template <typename T>
T pick(const std::string& value, std::initializer_list<std::pair<const char*, T>> options) {
    for (const auto& [name, option] : options) {
        if (value == name) return option;
    }
    std::fprintf(stderr, "Unknown value: %s\n", value.c_str());
    std::exit(2);
}

/** `pcm` : un seul canal, ou deux entrelacés. */
void writeWav(const std::string& path, const std::vector<float>& pcm, int rate, int channels) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(static_cast<std::uint16_t>(channels));
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 2 * static_cast<std::uint32_t>(channels));
    u16(static_cast<std::uint16_t>(2 * channels));
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    for (float s : pcm) {
        const int v = std::clamp(static_cast<int>(s * 32767.0f), -32768, 32767);
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
    }
}

/** Deux oreilles, ou un micro : le même son, et le sonomètre lu au passage. */
struct Output {
    std::vector<float>& pcm;
    int channels;
    double energy = 0.0;
    double loudest = -200.0;
    long reads = 0;
    std::vector<float> left, right;

    std::size_t silentFrames = 0;  // --out none : le modèle roule, sans rendre de son

    std::size_t frames() const { return silentFrames != 0 ? silentFrames : pcm.size() / static_cast<std::size_t>(channels); }
    void render(EngineSound& sound, std::size_t at, int n) {
        if (silentFrames != 0) return;
        if (channels == 1) {
            sound.render(pcm.data() + at, n);
        } else {
            left.resize(static_cast<std::size_t>(n));
            right.resize(static_cast<std::size_t>(n));
            sound.renderStereo(left.data(), right.data(), n);
            for (int i = 0; i < n; ++i) {
                pcm[(at + static_cast<std::size_t>(i)) * 2] = left[static_cast<std::size_t>(i)];
                pcm[(at + static_cast<std::size_t>(i)) * 2 + 1] = right[static_cast<std::size_t>(i)];
            }
        }
        const double level = static_cast<double>(sound.levelDbA());
        energy += std::pow(10.0, level / 10.0);
        loudest = std::max(loudest, level);
        ++reads;
    }
};

/** Le modèle de conduite pilote le son : chaque tranche est une pédale, ou le pilote automatique. */
template <typename Drive>
void play(EngineSound& sound, DrivingModel& model, Output& out, int rate, Drive drive, int tick = 0) {
    if (tick == 0) tick = kTick;
    const double dt = static_cast<double>(tick) / static_cast<double>(rate);
    std::size_t done = 0;
    int step = 0;
    while (done < out.frames()) {
        drive(step++, dt);
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
        sound.setTurbo(model.turboDrive());
        sound.setStarter(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()));
        sound.setRoad(static_cast<float>(model.speedKmh() / 3.6), static_cast<float>(model.wheelSlip()));
        sound.setGearbox(model.box(), model.gearboxDrive());
        const int n = static_cast<int>(std::min<std::size_t>(tick, out.frames() - done));
        out.render(sound, done, n);
        done += static_cast<std::size_t>(n);
    }
}

/** Trois coups de gaz au point mort : pied au plancher jusqu'au rupteur, puis lever de pied. */
void blips(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        // Une seconde de ralenti, puis un coup de gaz de 1,2 s toutes les 4 s : assez pour atteindre le rupteur.
        const double t = static_cast<double>(step) * kTick / rate - 1.0;
        const double inBlip = t - 4.0 * std::floor(t / 4.0);
        model.stepNeutral(dt, t >= 0.0 && inBlip < 1.2 ? 1.0 : 0.0);
    });
}

/** Tout en première, puis les rapports : pied au plancher jusqu'à la fin du fichier. */
void launch(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        model.stepPedal(dt, static_cast<double>(step) * kTick / rate >= 1.0 ? 1.0 : 0.0, 0.0);
    });
}

/** Point mort, moteur au ralenti : embrayé, ça claque ; débrayé, ça se tait ; puis de nouveau embrayé. */
void rattle(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        model.setClutchPedal(t > 4.0 && t < 8.0 ? 1.0 : 0.0);
        model.stepNeutral(dt, 0.0);
    });
}

/** Tous les rapports à mi-gaz, puis on redescend pied levé : les passages s'entendent. */
void shifts(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.setManual(true);
    model.requestShift(1);
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        const int top = std::min(model.gears(), 4);
        // Valeurs exactes en float : le WASM reçoit ses pédales en float, et doit donner le même son.
        double pedal = t > 0.3 ? 0.625 : 0.0;
        if (t < 15.0) {
            if (model.gear() < top && !model.shifting() && model.rpm() > model.redlineRpm() * 0.62) model.requestShift(1);
        } else {
            pedal = 0.0;
            if (model.gear() > 1 && !model.shifting() && model.rpm() < model.redlineRpm() * 0.42) model.requestShift(-1);
        }
        model.stepPedal(dt, pedal, t > 15.0 ? 0.125 : 0.0);
    });
}

/**
 * Pour entendre et mesurer le retard : en quatrième autour de 1800 tr/min, un coup de gaz à fond jusqu'à 4200 tr/min,
 * un lever de pied bref, la reprise, puis un long lever de pied. Tout en valeurs exactes en float, comme `shifts`.
 */
void turboStab(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.setManual(true);
    model.requestShift(1);
    int phase = 0;
    double since = 0.0;
    play(sound, model, out, rate, [&model, &phase, &since, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        const float engine = static_cast<float>(model.rpm());
        const int top = std::min(model.gears(), 4);
        double pedal = 0.0;
        double brake = 0.0;
        switch (phase) {
            case 0:
                pedal = t > 0.3 ? 0.625 : 0.0;
                if (model.gear() < top && !model.shifting() && engine > 3000.0f) model.requestShift(1);
                if (model.gear() == top && !model.shifting()) { phase = 1; since = t; }
                break;
            case 1:
                pedal = engine < 1800.0f ? 0.25 : 0.0;
                brake = engine > 2000.0f ? 0.125 : 0.0;
                if (t - since > 5.0) { phase = 2; since = t; }
                break;
            case 2:
                pedal = 1.0;
                if (engine > 4200.0f) { phase = 3; since = t; }
                break;
            case 3:
                if (t - since > 0.35) { phase = 4; since = t; }
                break;
            case 4:
                pedal = 1.0;
                if (t - since > 1.5) { phase = 5; since = t; }
                break;
            default:
                brake = engine > 1500.0f ? 0.125 : 0.0;
                break;
        }
        model.stepPedal(dt, pedal, brake);
    });
}

/** En troisième, on rétrograde sans débrayer ni monter le régime : ça gratte. Puis on débraye, et le rapport entre. */
void grind(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.setManual(true);
    model.requestShift(1);
    int state = 0;
    play(sound, model, out, rate, [&model, &state, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        double pedal = 0.75, clutch = 0.0;
        if (state == 0) {
            if (model.gear() < 3 && !model.shifting() && model.rpm() > model.redlineRpm() * 0.55) model.requestShift(1);
            if (model.gear() == 3 && !model.shifting() && model.rpm() > model.redlineRpm() * 0.5) {
                model.setClutchAssist(false);
                state = 1;
            }
        } else if (state == 1) {
            pedal = 0.0;
            if (t > 6.0) {
                model.requestShift(-1);
                state = 2;
            }
        } else if (state == 2) {
            pedal = 0.0;
            if (!model.shifting() && model.gear() == 0) state = 3;
        } else if (state == 3) {
            pedal = 0.0;
            clutch = 1.0;
            if (!model.shifting() && model.gear() < 2) model.requestShift(1);
            if (model.gear() >= 1 && model.meshed() && !model.shifting()) {
                if (model.gear() < 2) model.requestShift(1);
                else state = 4;
            }
        } else if (state == 4) {
            pedal = 0.25;
        }
        model.setClutchPedal(clutch);
        model.stepPedal(dt, pedal, 0.0);
    });
}

/** Marche arrière : le pignon droit de toutes les voitures, et son sifflement à faible vitesse. */
void reverse(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.setManual(true);
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (t > 0.5 && model.gear() == 0) model.requestShift(-1);
        model.stepPedal(dt, t > 1.0 ? 0.125 : 0.0, 0.0);
    });
}

/** En troisième, on débraye et on donne des coups de gaz en roulant : le sifflement suit les roues, pas le moteur. */
void clutchBlips(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.setManual(true);
    model.requestShift(1);
    play(sound, model, out, rate, [&model, rate](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (model.gear() < 3 && !model.shifting() && model.rpm() > model.redlineRpm() * 0.65) model.requestShift(1);
        const bool coasting = model.gear() == 3 && (model.speedKmh() > 70.0 || t > 9.0);
        const double pedal = coasting ? (std::fmod(t, 1.2) < 0.3 ? 1.0 : 0.0) : 0.75;
        model.setClutchPedal(coasting ? 1.0 : 0.0);
        model.stepPedal(dt, pedal, 0.0);
    });
}

/** Contact : le démarreur, le moteur qui prend, le ralenti. */
void start(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    model.halt();
    model.crank();
    // Par petits blocs, comme dans le navigateur : la hauteur du démarreur suit le régime de très près.
    play(sound, model, out, rate, [&model](int, double dt) { model.stepNeutral(dt, 0.0); }, 128);
}

void lap(EngineSound& sound, DrivingModel& model, Output& out, int rate, bool walk = false) {
    play(sound, model, out, rate, [&](int step, double dt) {
        // Mêmes pas que dans tools/check_wasm.mjs : de derrière à devant, à travers l'habitacle.
        if (walk) sound.setListenerPosition(static_cast<float>(-8.0 + 1.2 * step * kTick / rate), 0.3f, 1.2f);
        model.step(dt);
    });
}

/** Le trajet ne compte qu'une fois le régime établi : on mesure un état, pas sa mise en route. */
constexpr double kCruiseSettleS = 60.0;

/**
 * Un conducteur qui tient une consigne de vitesse : une action proportionnelle et intégrale, et le frein s'il va trop vite.
 * Sert à mesurer la consommation : ni le son ni les empreintes n'en dépendent.
 */
class SpeedFollower {
public:
    void drive(DrivingModel& model, double dt, double targetKmh) {
        // En boîte automatique, le moteur peut s'éteindre sans se déclarer calé : le conducteur relance, et on le dit.
        if (!model.cranking() && (model.stalled() || model.rpm() < 100.0)) {
            model.crank();
            ++restarts;
        }
        const double error = targetKmh - model.speedKmh();
        if (targetKmh <= 0.0 && model.speedKmh() < 0.5) {
            held_ = 0.0;
            model.stepPedal(dt, 0.0, 0.3);
            return;
        }
        const double wanted = 0.04 * error + held_;
        if (wanted > 0.0 && wanted < 1.0) held_ += 0.012 * error * dt;
        if (held_ < 0.0) held_ = 0.0;
        const double brake = error < -1.5 ? std::min(-0.04 * error, 0.6) : 0.0;
        // Sous 4 %, le pied ne fait qu'effleurer : assez pour que la boîte engage, pas assez pour avancer.
        const double pedal = targetKmh <= 0.0 || brake > 0.0 || wanted < 0.04 ? 0.0 : std::min(wanted, 1.0);
        model.stepPedal(dt, pedal, brake);
    }

    int restarts = 0;

private:
    double held_ = 0.0;
};

void cruise(EngineSound& sound, DrivingModel& model, Output& out, int rate, double kmh, SpeedFollower& driver) {
    bool measuring = false;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (!measuring && t >= kCruiseSettleS) {
            model.resetTrip();
            measuring = true;
        }
        driver.drive(model, dt, kmh);
    });
}

/** Au point mort, pied levé : le ralenti, une fois retombé le coup de gaz du démarrage. */
void idling(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    bool measuring = false;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (!measuring && t >= 5.0) {
            model.resetTrip();
            measuring = true;
        }
        model.stepNeutral(dt, 0.0);
    });
}

constexpr float kWltc[] = {
#include "wltc_class3b.inc"
};

/** Le cycle d'homologation, à chaud et sans arrêt automatique du moteur. Le conducteur anticipe d'une demi-seconde. */
void wltc(EngineSound& sound, DrivingModel& model, Output& out, int rate, SpeedFollower& driver) {
    const int last = static_cast<int>(sizeof(kWltc) / sizeof(kWltc[0])) - 1;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate + 0.5;
        const int at = std::min(static_cast<int>(t), last - 1);
        const double share = std::min(t - at, 1.0);
        driver.drive(model, dt, kWltc[at] + (kWltc[at + 1] - kWltc[at]) * share);
    });
}

void printFuel(const DrivingModel& model, const Powertrain& powertrain, double seconds, int restarts) {
    const ConsumptionMeter& c = model.consumption();
    if (restarts > 0) std::printf("The driver had to restart the engine %d time(s).\n", restarts);
    const double hours = std::max(seconds, 1e-9) / 3600.0;
    if (powertrain.electric) {
        std::printf("Energy (estimate, at the battery): %.3f kWh, %.1f kWh/100 km over %.2f km\n", c.energyTripKwh(), c.energyAvgKwhPer100km(), c.tripKm());
    } else {
        const FuelMapSummary best = fuelMapSummary(powertrain);
        std::printf("Fuel (estimate): %.4f L in %.0f s, %.2f L/h, %.2f L/100 km over %.2f km, %.0f g CO2/km, average lambda %.2f\n", c.fuelTripL(), seconds, c.fuelTripL() / hours, c.fuelAvgLper100km(), c.tripKm(), c.co2GPerKm(), c.lambdaAverage());
        std::printf("Fuel map: best %.0f g/kWh at %.0f rpm, %.1f bar; %.0f g/kWh at peak power\n", best.bestGPerKwh, best.bestRpm, best.bestBmepBar, best.peakPowerGPerKwh);
        std::printf("Oil (order of magnitude, within a factor of three): %.2f mL, %.1f g/h, %.0f mL/1000 km\n", c.oilTripMl(), c.oilTripMl() * 0.85 / hours, c.oilMlPer1000km());
    }
    // Au bit près, pour tools/check_wasm.mjs.
    std::printf("raw fuel_lph=%.17g fuel_trip_l=%.17g trip_km=%.17g oil_trip_ml=%.17g energy_trip_kwh=%.17g\n", c.fuelLph(), c.fuelTripL(), c.tripKm(), c.oilTripMl(), c.energyTripKwh());
}

/**
 * Un passage en bord de route, comme ISO 362 : 7,5 m de l'axe, micro à 1,2 m. La voiture accélère jusqu'à la
 * vitesse voulue puis garde le pied à mi-course ; dans son repère, c'est l'auditeur qui défile, d'où le Doppler.
 */
void passby(EngineSound& sound, DrivingModel& model, Output& out, int rate, float lateral, float height) {
    const float total = static_cast<float>(out.frames()) / static_cast<float>(rate);
    double travelled = 0.0;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double seconds = static_cast<double>(step) * dt;
        model.stepPedal(dt, seconds < 8.0 && model.speedKmh() < 50.0 ? 1.0 : 0.35, 0.0);
        travelled += model.speedKmh() / 3.6 * dt;
        // La voiture passe devant l'auditeur à mi-parcours.
        const float ahead = static_cast<float>(model.speedKmh() / 3.6) * total * 0.5f;
        sound.setListenerPosition(ahead - static_cast<float>(travelled), lateral, height, 150.0f);
    });
}

}  // namespace

int main(int argc, char** argv) {
    std::string out, scenario = "lap", presetId, at;
    bool stereo = false, showDb = false, walk = false, showFuel = false;
    ShiftMode shiftMode = ShiftMode::Normal;
    DriveScenario driveScenario = DriveScenario::City;
    double kmh = 90.0;
    std::optional<ExhaustRouting> routing;
    std::optional<Exhaust> exhaustChoice;
    std::optional<SuperchargerKind> superchargerChoice;
    std::optional<AntiLag> antiLagChoice;
    std::optional<int> turboPipeChoice;
    std::optional<Camshaft> camshaftChoice;
    std::optional<AirFilter> airFilterChoice;
    std::optional<double> strokeChoice;
    bool customListener = false;
    Powertrain powertrain = referencePowertrain();
    EngineConfig& config = powertrain.engine;
    Listener listener = Listener::Outside;
    bool customConfig = false;
    bool customCar = false;
    CarConfig car = powertrain.car;
    GearboxConfig box;
    bool customBox = false;
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
        else if (arg == "--shift-mode") shiftMode = pick<ShiftMode>(value, {{"normal", ShiftMode::Normal}, {"eco", ShiftMode::Eco}, {"sport", ShiftMode::Sport}, {"track", ShiftMode::Track}, {"paddles", ShiftMode::Paddles}});
        else if (arg == "--drive-scenario") {
            bool found = false;
            for (int n = 0; n < kDriveScenarios; ++n) {
                if (value == scenarioOf(static_cast<DriveScenario>(n)).id) {
                    driveScenario = static_cast<DriveScenario>(n);
                    found = true;
                }
            }
            if (!found) {
                std::fprintf(stderr, "Unknown value: %s\n", value.c_str());
                return 2;
            }
        }
        else if (arg == "--rate") rate = std::atoi(value.c_str());
        else if (arg == "--seconds") seconds = std::atoi(value.c_str());
        else if (arg == "--rpm") rpm = static_cast<float>(std::atof(value.c_str()));
        else if (arg == "--volume") volume = static_cast<float>(std::atof(value.c_str()));
        else if (arg == "--listener") { listener = pick<Listener>(value, {{"outside", Listener::Outside}, {"cabin", Listener::Cabin}, {"bay", Listener::EngineBay}, {"front", Listener::Front}, {"side", Listener::Side}, {"far", Listener::Far}, {"pipe", Listener::Pipe}}); customListener = true; }
        else if (arg == "--at") at = value;
        else if (arg == "--stereo") stereo = value != "0";
        else if (arg == "--db") showDb = value != "0";
        else if (arg == "--fuel") showFuel = value != "0";
        else if (arg == "--kmh") kmh = std::atof(value.c_str());
        else if (arg == "--walk") walk = value != "0";
        else if (arg == "--placement") { car.placement = pick<EnginePlacement>(value, {{"front", EnginePlacement::Front}, {"mid", EnginePlacement::Mid}, {"rear", EnginePlacement::Rear}}); customCar = true; }
        else if (arg == "--insulation") { car.insulation = pick<Insulation>(value, {{"stripped", Insulation::Stripped}, {"light", Insulation::Light}, {"standard", Insulation::Standard}, {"comfort", Insulation::Comfort}, {"luxury", Insulation::Luxury}}); customCar = true; }
        else if (arg == "--windows") { car.windowsOpen = value == "open"; customCar = true; }
        else if (arg == "--body") { car.body = pick<Body>(value, {{"city", Body::City}, {"compact", Body::Compact}, {"coupe", Body::Coupe}, {"supercar", Body::Supercar}, {"van", Body::Van}}); customCar = true; }
        else if (arg == "--mass") { car.massKg = std::atoi(value.c_str()); customCar = true; }
        else if (arg == "--rim") { car.rimInch = std::atoi(value.c_str()); customCar = true; }
        else if (arg == "--drive") { car.drive = pick<Drive>(value, {{"front", Drive::Front}, {"rear", Drive::Rear}, {"all", Drive::All}}); customCar = true; }
        else if (arg == "--gearbox") { box.kind = pick<GearboxKind>(value, {{"synchro", GearboxKind::Synchro}, {"dog", GearboxKind::Dog}, {"sequential", GearboxKind::Sequential}, {"dual_clutch", GearboxKind::DualClutch}, {"converter", GearboxKind::Converter}}); customBox = true; }
        else if (arg == "--gears") { box.gears = std::atoi(value.c_str()); customBox = true; }
        else if (arg == "--family") { box.family = pick<GearFamily>(value, {{"matched", GearFamily::Matched}, {"short", GearFamily::Short}, {"close", GearFamily::Close}, {"long", GearFamily::Long}}); customBox = true; }
        else if (arg == "--ratios") {
            box.family = GearFamily::Custom;
            std::size_t from = 0;
            for (std::size_t n = 0; n < box.ratios.size() && from < value.size(); ++n) {
                const std::size_t comma = value.find(',', from);
                box.ratios[n] = static_cast<float>(std::atof(value.substr(from, comma - from).c_str()));
                box.gears = static_cast<int>(n) + 1;
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            customBox = true;
        }
        else if (arg == "--final") { box.finalDrive = static_cast<float>(std::atof(value.c_str())); customBox = true; }
        else if (arg == "--teeth") { box.form = pick<ToothForm>(value, {{"helical", ToothForm::Helical}, {"spur", ToothForm::Spur}}); customBox = true; }
        else if (arg == "--helix") { box.helixDeg = static_cast<float>(std::atof(value.c_str())); customBox = true; }
        else if (arg == "--module") { box.moduleMm = static_cast<float>(std::atof(value.c_str())); customBox = true; }
        else if (arg == "--limit") { box.limitNm = static_cast<float>(std::atof(value.c_str())); customBox = true; }
        else if (arg == "--flywheel") { box.flywheel = pick<Flywheel>(value, {{"single", Flywheel::Single}, {"dual_mass", Flywheel::DualMass}}); customBox = true; }
        else {
            customConfig = true;
            if (arg == "--cylinders") config.cylinders = std::atoi(value.c_str());
            else if (arg == "--cc") config.displacementCc = std::atoi(value.c_str());
            else if (arg == "--bore") config.boreMm = std::atoi(value.c_str());
            else if (arg == "--redline") config.redlineRpm = std::atoi(value.c_str());
            else if (arg == "--boost") config.boostBar = static_cast<float>(std::atof(value.c_str()));
            else if (arg == "--turbo") config.turboSize = pick<TurboSize>(value, {{"small", TurboSize::Small}, {"medium", TurboSize::Medium}, {"large", TurboSize::Large}});
            else if (arg == "--layout") config.layout = pick<Layout>(value, {{"inline", Layout::Inline}, {"v", Layout::V}, {"cross_plane", Layout::CrossPlane}, {"boxer", Layout::Boxer}});
            else if (arg == "--exhaust") { config.exhaust = pick<Exhaust>(value, {{"stock", Exhaust::Stock}, {"sport", Exhaust::Sport}, {"straight", Exhaust::Straight}, {"no_muffler", Exhaust::MufflerDelete}, {"no_resonator", Exhaust::ResonatorDelete}, {"quiet", Exhaust::Quiet}}); exhaustChoice = config.exhaust; }
            else if (arg == "--routing") { config.routing = pick<ExhaustRouting>(value, {{"single", ExhaustRouting::Single}, {"dual", ExhaustRouting::Dual}, {"h", ExhaustRouting::HPipe}, {"x", ExhaustRouting::XPipe}}); routing = config.routing; }
            else if (arg == "--aspiration") config.aspiration = pick<Aspiration>(value, {{"natural", Aspiration::Natural}, {"turbo", Aspiration::Turbo}, {"supercharger", Aspiration::Supercharger}});
            else if (arg == "--supercharger") superchargerChoice = pick<SuperchargerKind>(value, {{"roots", SuperchargerKind::Roots}, {"twin_screw", SuperchargerKind::TwinScrew}, {"centrifugal", SuperchargerKind::Centrifugal}});
            else if (arg == "--antilag") antiLagChoice = pick<AntiLag>(value, {{"off", AntiLag::Off}, {"retard", AntiLag::Retard}, {"air", AntiLag::AirInjection}, {"cut", AntiLag::CylinderCut}});
            else if (arg == "--turbo-pipe") turboPipeChoice = std::atoi(value.c_str());
            else if (arg == "--cam") camshaftChoice = pick<Camshaft>(value, {{"dohc", Camshaft::Dohc}, {"sohc", Camshaft::Sohc}});
            else if (arg == "--filter") airFilterChoice = pick<AirFilter>(value, {{"stock", AirFilter::Stock}, {"panel", AirFilter::Panel}, {"cone", AirFilter::Cone}, {"stacks", AirFilter::Stacks}});
            else if (arg == "--stroke") {
                strokeChoice = std::atof(value.c_str());
                if (*strokeChoice <= 0.0) {
                    std::fprintf(stderr, "Expected a stroke in mm: %s\n", value.c_str());
                    return 2;
                }
            }
            else if (arg == "--fuel") config.fuel = pick<Fuel>(value, {{"petrol", Fuel::Petrol}, {"diesel", Fuel::Diesel}});
            else if (arg == "--pops") config.pops = pick<PopLevel>(value, {{"off", PopLevel::Off}, {"light", PopLevel::Light}, {"strong", PopLevel::Strong}});
            else if (arg == "--release") config.turboRelease = pick<TurboRelease>(value, {{"dump_valve", TurboRelease::DumpValve}, {"flutter", TurboRelease::Flutter}});
            else {
                std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
                return 2;
            }
        }
    }
    if (out.empty()) {
        usage();
        return 2;
    }

    if (!presetId.empty()) {
        const Preset* preset = findPreset(presetId.c_str());
        if (preset == nullptr) {
            std::fprintf(stderr, "Unknown preset: %s\n", presetId.c_str());
            return 2;
        }
        powertrain = preset->powertrain();
    } else if (!customConfig) {
        std::size_t count = 0;
        powertrain = presets(count)[0].powertrain();
    }
    // Sur un préréglage, les réglages donnés en plus l'emportent.
    if (exhaustChoice) powertrain.engine.exhaust = *exhaustChoice;
    if (routing) powertrain.engine.routing = *routing;
    if (superchargerChoice) powertrain.engine.superchargerKind = *superchargerChoice;
    if (antiLagChoice) powertrain.engine.antiLag = *antiLagChoice;
    if (turboPipeChoice) powertrain.engine.turboPipeMm = *turboPipeChoice;
    if (camshaftChoice) powertrain.engine.camshaft = *camshaftChoice;
    if (airFilterChoice) powertrain.engine.airFilter = *airFilterChoice;
    // À la fin : la course se lit sur l'alésage et les cylindres retenus, ceux du préréglage s'il y en a un.
    if (strokeChoice) powertrain.engine.displacementCc = displacementForStroke(powertrain.engine.boreMm, powertrain.engine.cylinders, *strokeChoice);
    if (customCar) {
        if (!presetId.empty()) {
            if (car.placement != EnginePlacement::Natural) powertrain.car.placement = car.placement;
            powertrain.car.insulation = car.insulation;
            powertrain.car.windowsOpen = car.windowsOpen;
        } else {
            powertrain.car = car;
        }
    }
    if (customBox) powertrain.gearbox = box;
    const EngineProfile profile = powertrain.profile();
    DrivingModel model(powertrain);
    model.setShiftMode(shiftMode);
    model.setDriveScenario(driveScenario);

    // Un trajet choisi se rend en entier : une boucle.
    if (seconds == 0 && scenario == "lap" && driveScenario != DriveScenario::City) seconds = static_cast<int>(scenarioOf(driveScenario).loopSec);
    if (seconds == 0) {
        seconds = scenario == "lap" ? 60 : (scenario == "blips" ? 14 : (scenario == "launch" ? 20 : (scenario == "start" ? 5 : (scenario == "passby" ? 16 : 4))));
        if (scenario == "rattle" || scenario == "reverse") seconds = 12;
        else if (scenario == "shifts") seconds = 26;
        else if (scenario == "turbo") seconds = 28;
        else if (scenario == "grind" || scenario == "clutchblips") seconds = 14;
        else if (scenario == "idle") seconds = 65;
        else if (scenario == "cruise") seconds = 180;
        else if (scenario == "wltc") seconds = 1800;
    }
    EngineSound sound(rate);
    sound.setProfile(profile);
    sound.setVolume(volume);
    if (customListener || at.empty()) sound.setListener(listener);
    if (!at.empty()) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(at.c_str(), "%f,%f,%f", &x, &y, &z) != 3) {
            std::fprintf(stderr, "Expected position: X,Y,Z\n");
            return 2;
        }
        sound.setListenerPosition(x, y, z);
    }
    const int channels = stereo ? 2 : 1;
    const bool silent = out == "none";
    const std::size_t frames = static_cast<std::size_t>(rate) * static_cast<std::size_t>(seconds);
    std::vector<float> pcm(silent ? 0 : frames * static_cast<std::size_t>(channels));
    Output output{pcm, channels};
    if (silent) output.silentFrames = frames;
    SpeedFollower driver;
    if (scenario == "lap") {
        lap(sound, model, output, rate, walk);
    } else if (scenario == "blips") {
        blips(sound, model, output, rate);
    } else if (scenario == "launch") {
        launch(sound, model, output, rate);
    } else if (scenario == "start") {
        start(sound, model, output, rate);
    } else if (scenario == "passby") {
        passby(sound, model, output, rate, -7.5f, 1.2f);
            } else if (scenario == "rattle") {
        rattle(sound, model, output, rate);
    } else if (scenario == "turbo") {
        turboStab(sound, model, output, rate);
    } else if (scenario == "shifts") {
        shifts(sound, model, output, rate);
    } else if (scenario == "grind") {
        grind(sound, model, output, rate);
    } else if (scenario == "reverse") {
        reverse(sound, model, output, rate);
    } else if (scenario == "clutchblips") {
        clutchBlips(sound, model, output, rate);
    } else if (scenario == "idle") {
        idling(sound, model, output, rate);
    } else if (scenario == "cruise") {
        cruise(sound, model, output, rate, kmh, driver);
    } else if (scenario == "wltc") {
        wltc(sound, model, output, rate, driver);
    } else if (scenario == "steady") {
        sound.setTargetRpm(rpm);
        output.render(sound, 0, static_cast<int>(output.frames()));
    } else {
        usage();
        return 2;
    }
    // Sans le temps de mise en route que ces deux scénarios ne comptent pas.
    const double measured = static_cast<double>(seconds) - (scenario == "cruise" ? kCruiseSettleS : (scenario == "idle" ? 5.0 : 0.0));
    if (silent) {
        if (showFuel) printFuel(model, powertrain, measured, driver.restarts);
        return 0;
    }
    if (out == "-") {
        // Empreinte du rendu 16 bits : détecte tout écart, jusqu'au dernier bit.
        std::uint32_t hash = 2166136261u;
        for (float sample : pcm) {
            const int v = std::clamp(static_cast<int>(sample * 32767.0f), -32768, 32767);
            hash = (hash ^ static_cast<std::uint32_t>(v & 0xFFFF)) * 16777619u;
        }
        std::printf("%08x\n", hash);
        if (showFuel) printFuel(model, powertrain, measured, driver.restarts);
        return 0;
    }
    writeWav(out, pcm, rate, channels);
    if (showFuel) printFuel(model, powertrain, measured, driver.restarts);
    if (showDb) std::printf("LAeq %.1f dB(A), LAFmax %.1f dB(A)\n", 10.0 * std::log10(output.energy / static_cast<double>(std::max(output.reads, 1L))), output.loudest);
    return 0;
}
