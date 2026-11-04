// Rend un moteur en WAV, sans navigateur : pour écouter, mesurer et comparer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "revforge/architecture.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"
#include "revforge/valve_float.hpp"
#include "revforge/two_stroke.hpp"

using namespace revforge;

namespace {

// Par petits blocs, comme le navigateur : le son suit le modèle de très près.
constexpr int kTick = 128;

void usage() {
    std::puts(
        "revforge-render --out file.wav|-|none [--preset ID | settings] [--scenario lap|blips|launch|launch_control|start|steady|passby|rattle|shifts|turbo|grind|reverse|clutchblips|idle|cruise|wltc|tipin|soak]\n"
        "  --scenario tipin  held at --rpm in third, then flat out: prints the torque of the first second\n"
        "  --scenario launch_control  brake and throttle held for 3 s, then the brake released: prints the 0-100 km/h from the release\n"
        "  --scenario soak   a minute at a standstill, then three flat-out starts: prints the charge-air temperatures\n"
        "  --out none        drive the model without rendering any sound: for the figures of --fuel\n"
        "  --list            list the presets\n"
        "  --seconds N       duration (lap: 60, or one loop of --drive-scenario; blips: 14, steady: 4)\n"
        "  --drive-scenario city|stop_go|country|motorway|mountain|drag|track|hill_start|towing   the autopilot's journey, with --scenario lap\n"
        "  --shift-mode normal|eco|sport|track|paddles   how the automatic picks its gears (paddles: the autopilot drives in normal)\n"
        "  --rpm N           engine speed for steady\n"
        "  --kmh N           speed held by cruise (the trip is measured once settled, after 60 s)\n"
        "  --rate N          sample rate (44100)\n"
        "  --volume X        volume (0.7)\n"
        "Engine:   --cylinders N (1 to 16) --layout inline|v|cross_plane|boxer|rotary|vr|w --cc N --bore N --redline N\n"
        "          --layout vr: VR5 or VR6, one head over two rows 15 deg apart. --layout w: W8, W12 or W16, two VR banks.\n"
        "                       Elsewhere, and above 12 cylinders unless v or w, the engine is built as a v.\n"
        "          --exhaust stock|sport|straight|no_muffler|no_resonator|quiet|expansion --routing single|dual|h|x\n"
        "          --aspiration natural|turbo|supercharger --fuel petrol|diesel\n"
        "          --pops off|light|strong --release dump_valve|flutter --boost BAR --turbo small|medium|large\n"
        "          --supercharger roots|twin_screw|centrifugal --antilag off|retard|air|cut --turbo-pipe MM\n"
        "          --cam dohc|sohc|ohv --filter stock|panel|cone|stacks --fuel-system injection|single|multi|direct (or --carb)\n"
        "          --stroke MM  sets --cc from the bore and the cylinders, in any order, and replaces --cc if both are given.\n"
        "                       On a --preset: applied to the preset's own bore and cylinders, which --bore and --cylinders do not change.\n"
        "                       Ignored on a rotary, which has no stroke; --bore and --cam are ignored there too.\n"
        "          Each option below overrides a --preset, as --layout (on the preset's cylinders), --exhaust, --routing,\n"
        "          --supercharger, --antilag, --turbo-pipe, --cam, --filter and --fuel-system do. A number at 0 keeps the engine's own value.\n"
        "          Architecture: --cycle four_stroke|two_stroke\n"
        "          Combustion:   --compression X (6 to 24) --grade ron95|ron98|race102|e85|methanol\n"
        "          Valvetrain:   --cam-duration DEG --cam-lift MM --lsa DEG --vvt fixed|phased|two_step|phased_two_step\n"
        "                        --springs stock|stiff\n"
        "          Fueling:      --diesel-injection mechanical|common_rail --egr 0|1 --dpf 0|1\n"
        "          Charging:     --turbo-layout single|twin_parallel|twin_sequential|twincharger --vgt 0|1\n"
        "                        --boost-control spring|electronic|by_gear --intercooler air|water|none --water 0|1 --nitrous KW\n"
        "          ECU:          --advance DEG --lambda X --limiter stock|soft|ignition_cut --launch RPM --flat-shift 0|1\n"
        "                        --traction off|sport|full\n"
        "          Conditions:   --ambient C --altitude M --start warm|half_hour|two_hours|cold\n"
        "                        (warm: just driven; half_hour, two_hours: parked that long, cooled by the thermal network;\n"
        "                        cold: overnight, everything at the ambient air)\n"
        "          Starting:     --oil 5w30|0w20|10w40|15w40|20w50|sae30 (the preset's oil otherwise)\n"
        "                        --battery PCT (state of charge, 100) --crank-max S (key held at most, 10)\n"
        "                        --attempts N (with --scenario start: retries after 30 s, 1) --crank FILE (CSV every 10 ms)\n"
        "Car:      --body city|compact|coupe|supercar|van|retro --mass KG --rim INCHES --drive front|rear|all\n"
        "          --placement front|mid|rear --insulation stripped|light|standard|comfort|luxury --windows open|closed\n"
        "Gearbox:  --gearbox synchro|dog|sequential|dual_clutch|converter --gears N --family matched|short|close|long\n"
        "          --ratios 3.2,2.0,1.4,1.1,0.9 --final X --teeth helical|spur --helix DEG --module MM --limit NM\n"
        "          --flywheel single|dual_mass\n"
        "Listener: --listener outside|front|side|far|pipe|cabin|bay  or  --at X,Y,Z (m: x forward, y left, z up)\n"
        "          --stereo 1: two ears (otherwise one microphone at the centre of the head)\n"
        "          --db 1: also print the estimated A-weighted level\n"
        "          --fuel 1: also print the estimated fuel, oil or energy used, and the best of the engine's fuel map\n"
        "                    (--fuel 0|1 is this display; --fuel petrol|diesel chooses the engine's fuel)\n"
        "          --trace FILE: write the turbo, its boost control, its anti-lag, the fuel, the knock, the engine management and the charge air as CSV, one line per slice\n"
        "          --thermal FILE: write the engine temperatures and what a cold engine changes as CSV, one line per 0.1 s\n"
        "          --walk 1: the head walks through the car at 1.2 m/s (with lap)\n"
        "          --valves 1: also print the valvetrain: moving mass, springs, valve float rpm");
}

/** La distribution : ses pièces, son ressort et son régime d'affolement (voir « Les culbuteurs et l'affolement »). */
void printValves(const Powertrain& powertrain) {
    const EngineConfig& e = powertrain.engine;
    if (powertrain.electric || !hasValves(e)) {
        std::printf("Valvetrain: none, no valves\n");
        return;
    }
    const Camshaft kind = e.valvetrain();
    const ValvetrainParts parts = valvetrainParts(kind, static_cast<double>(e.boreMm), static_cast<double>(e.strokeMm()));
    const CamLobe cam = camLobeOf(e);
    const ValveSpring spring = valveSpringOf(e);
    const double floatRpm = valveFloatRpm(e);
    const double redline = static_cast<double>(e.redlineRpm);
    const char* name = kind == Camshaft::Ohv ? "pushrods (OHV)" : (kind == Camshaft::Sohc ? "single overhead cam" : "twin overhead cams");
    std::printf("Valvetrain: %s, %d valves per cylinder, rocker ratio %.2f, intake head %.1f mm\n", name, twoValves(kind) ? 2 : 4, parts.ratio, parts.headMm);
    std::printf("Moving mass at the valve %.1f g: valve %.1f, retainer %.1f, a third of the spring %.1f, bucket %.1f, rocker %.1f, lifter and pushrod / ratio^2 %.1f\n",
                parts.effectiveKg * 1000.0, parts.valveKg * 1000.0, parts.retainerKg * 1000.0, parts.springKg * 1000.0 / 3.0, parts.bucketKg * 1000.0,
                parts.rockerAtValveKg * 1000.0, (parts.lifterKg + parts.pushrodKg) / (parts.ratio * parts.ratio) * 1000.0);
    std::printf("Stiffness at the valve %.1f kN/mm, natural frequency %.0f Hz\n", parts.stiffnessNPerM / 1e6, parts.naturalHz);
    std::printf("Cam: %.2f mm over %.0f deg; nose deceleration %.0f m/s^2 at the redline\n", cam.liftMm, cam.durationDeg, noseDecelerationMs2(cam, redline));
    std::printf("Spring: %.0f N seated, %.0f N at full lift, %.1f N/mm\n", spring.preloadN, spring.forceN(cam.liftMm), spring.rateNPerMm);
    std::printf("Valve float: %.0f rpm, %+.1f %% from the redline\n", floatRpm, (floatRpm / redline - 1.0) * 100.0);
    std::printf("Cam friction at the redline: %.3f bar, of which stiff springs %.3f bar\n", followerFrictionBar(e, redline), springFrictionDeltaBar(e, redline));
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

/** `--trace` : le turbo et ce qu'il coûte, tranche par tranche, pour mesurer sans rien écouter. */
std::FILE* g_trace = nullptr;

void traceHeader() {
    std::fputs("t,rpm,throttle,gear,boost,shaft_rpm,t3_c,turbine_kw,compressor_kw,air_g_s,heat_kw,fuel_g_s,torque_nm,lost_nm,antilag,kmh,flow_kg_s,blowoff,wastegate,charge_bar,exhaust_bar,trip_l,pops,advance_deg,knock_limit_deg,knock_retard_deg,knock_share,knocks,"
               "ecu_advance_deg,lambda,fired,limiter,launch,flat,traction,slip_ms,target_abs_bar,charge_abs_bar,duty,"
               "shaft2_rpm,exhaust2_bar,stage,vane,vane2,clutch,charger_nm,interstage_bar,flap,"
               "charge_c,water_loop_c,water_g_s,nitrous_g_s,bottle_bar,bottle_kg\n", g_trace);
}

void traceLine(const DrivingModel& model, const EngineSound& sound, double t) {
    const TurboState& s = model.turboState();
    const AntiLagState& a = model.turbo().antiLag();
    std::fprintf(g_trace, "%.4f,%.1f,%.3f,%d,%.4f,%.0f,%.1f,%.3f,%.3f,%.3f,%.3f,%.4f,%.2f,%.2f,%d,%.2f,%.4f,%.3f,%.3f,%.4f,%.4f,%.6f,%u,%.2f,%.2f,%.2f,%.4f,%u,", t, model.rpm(), model.throttle(), model.gear(), model.boost(),
                 s.shaftOmega * 60.0 / (2.0 * 3.141592653589793), s.turbineInK - 273.15, s.turbinePowerW / 1000.0, s.compressorPowerW / 1000.0, a.airFlow * 1000.0, a.collectorHeatW / 1000.0,
                 model.consumption().fuelGps(), model.torqueNm(), model.antiLagLostNm(), model.antiLagActive() ? 1 : 0, model.speedKmh(), s.compressorFlow, s.blowOff, s.wastegate,
                 (s.chargePa - model.turbo().spec().ambientPa) / 1e5, (s.turbineInPa - model.turbo().spec().ambientPa) / 1e5, model.consumption().fuelTripL(), static_cast<unsigned>(sound.pops()),
                 model.sparkAdvanceDeg(), model.knockLimitDeg(), model.knockRetardDeg(), model.knockShare(), static_cast<unsigned>(sound.knocks()));
    // Le calculateur : avance face à la carte d'origine, λ demandé, part des explosions, ce qui agit, glissement des roues.
    std::fprintf(g_trace, "%.2f,%.3f,%.3f,%d,%d,%d,%.3f,%.3f,", model.ecuAdvanceDeg(), model.ecuLambda(), model.ecuFiredShare(), model.limiterActive() ? 1 : 0, model.launchHolding() ? 1 : 0,
                 model.flatShifting() ? 1 : 0, model.ecuAction().traction, model.wheelSlip());
    // La régulation de pression : la cible et la charge, en absolu (bar), et le rapport cyclique de l'électrovanne (−1 : ressort).
    std::fprintf(g_trace, "%.4f,%.4f,%.3f", model.boostTargetPa() / 1e5, s.chargePa / 1e5, model.boostDuty());
    // Deux turbos, twincharger, aubes : le second arbre, l'étape du séquentiel, les aubes, l'embrayage du compresseur.
    const double ambientBar = model.turbo().spec().ambientPa / 1e5;
    const TwinTurboState& w = model.twin().state();
    const bool twin = model.twinTurbo();
    std::fprintf(g_trace, ",%.0f,%.4f,%d,%.3f,%.3f,%d,%.2f,%.4f,%.3f,", twin ? w.unit[1].shaftOmega * 60.0 / (2.0 * 3.141592653589793) : 0.0, twin ? w.unit[1].turbineInPa / 1e5 - ambientBar : 0.0,
                 twin ? w.stage : 0, twin ? w.vane[0] : 1.0, twin ? w.vane[1] : 1.0, twin && w.clutch ? 1 : 0, twin ? w.chargerNm : 0.0, twin ? w.interstagePa / 1e5 - ambientBar : 0.0, twin ? w.flap : 1.0);
    // L'air de charge : la charge au collecteur, la boucle d'eau, l'eau et le protoxyde injectés, la bouteille (pression relative).
    const ChargeAir& charge = model.chargeAir();
    const bool charged = model.chargeAirEngine();
    std::fprintf(g_trace, "%.2f,%.2f,%.3f,%.3f,%.3f,%.4f\n", model.manifoldCharge().temperatureK - 273.15, charge.waterLoopK() - 273.15,
                 charged ? charge.waterGps() : 0.0, charged ? charge.nitrousKgS() * 1000.0 : 0.0,
                 charged ? (charge.bottle().pressurePa - 101325.0) / 1e5 : 0.0, charged ? charge.bottle().massKg : 0.0);
}

/** `--thermal` : les températures et ce que le moteur froid change, pour mesurer la mise en température sans rien écouter. */
std::FILE* g_thermal = nullptr;
double g_thermalNext = 0.0;

/** Les instants de la mise en température : le premier passage de chaque seuil (s), -1 tant qu'il n'est pas atteint. */
struct WarmUp {
    double thermostatS = -1.0;
    double coolant70S = -1.0;
    double oil80S = -1.0;
    double idleWarmS = -1.0;
    double peakCoolantC = -1000.0;
    double peakOilC = -1000.0;
    double fanS = 0.0;
    bool active = false;  // rien de tout cela sur l'électrique
    bool cold = false;    // pas parti chaud : la mise en température se mesure
    StartTemp start = StartTemp::Warm;
    ThermalNetwork atStart;  // le moteur au départ, après son arrêt

    const char* startName() const {
        switch (start) {
            case StartTemp::Cold: return "cold";
            case StartTemp::HalfHour: return "half-hour";
            case StartTemp::TwoHours: return "two-hour";
            case StartTemp::Warm: break;
        }
        return "warm";
    }

    void track(const DrivingModel& model, double t, double dt) {
        if (!active) return;
        const ThermalNetwork& th = model.thermal();
        if (thermostatS < 0.0 && th.thermostat() > 0.01) thermostatS = t;
        if (coolant70S < 0.0 && th.coolantC() >= 70.0) coolant70S = t;
        if (oil80S < 0.0 && th.oilC() >= 80.0) oil80S = t;
        if (idleWarmS < 0.0 && cold && model.coldIdleRpm() <= 0.0) idleWarmS = t;
        peakCoolantC = std::max(peakCoolantC, th.coolantC());
        peakOilC = std::max(peakOilC, th.oilC());
        if (th.fan()) fanS += dt;
    }
};
WarmUp g_warmUp;

void thermalLine(const DrivingModel& model, double t) {
    const ThermalNetwork& th = model.thermal();
    std::fprintf(g_thermal, "%.1f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%d,%.3f,%.3f,%.1f,%.3f,%.5f,%.0f,%.0f\n", t, model.rpm(), model.speedKmh(), th.coolantC(), th.oilC(), th.metalC(), th.radiatorC(),
                 th.thermostat(), th.fan() ? 1 : 0, th.radiatorW() / 1000.0, model.coldFrictionNm(), model.coldIdleRpm(), model.coldEnrichment(), model.consumption().fuelTripL(), th.wallW(), model.torqueNm());
}

/** Le modèle de conduite pilote le son : chaque tranche est une pédale, ou le pilote automatique. */
template <typename Drive>
void play(EngineSound& sound, DrivingModel& model, Output& out, int rate, Drive drive, int tick = 0) {
    if (tick == 0) tick = kTick;
    const double dt = static_cast<double>(tick) / static_cast<double>(rate);
    std::size_t done = 0;
    int step = 0;
    while (done < out.frames()) {
        drive(step++, dt);
        if (g_trace != nullptr) traceLine(model, sound, static_cast<double>(step) * dt);
        g_warmUp.track(model, static_cast<double>(step) * dt, dt);
        if (g_thermal != nullptr && static_cast<double>(step) * dt >= g_thermalNext) {
            thermalLine(model, static_cast<double>(step) * dt);
            g_thermalNext += 0.1;
        }
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
        sound.setTurbo(model.turboDrive());
        if (model.twinTurbo() && model.twin().spec().units == 2) sound.setTurboB(model.turboDriveB());
        sound.setSupercharger(model.superchargerDrive());
        sound.setEcu(model.ecuDrive());
        sound.setCharge(static_cast<float>(model.chargeFill()));
        sound.setStarter(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()), static_cast<float>(model.starterSupply()));
        sound.setStart(model.startDrive());
        sound.setRoad(static_cast<float>(model.speedKmh() / 3.6), static_cast<float>(model.wheelSlip()));
        sound.setGearbox(model.box(), model.gearboxDrive());
        sound.setKnock(model.knockDrive());
        sound.setCam(model.camDrive());
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

/**
 * Un départ arrêté comme sur une piste : une seconde au ralenti, puis pied sur le frein et au plancher pendant
 * `kLaunchStageS`, puis on lâche le frein, pied au plancher jusqu'au bout. Avec le launch control, il tient le régime et
 * la pression monte ; sans lui, le rupteur tient le moteur et l'embrayage patine contre les freins. Écrit le régime et la
 * pression au lâcher, et le 0-100 km/h compté depuis le lâcher.
 */
void launchControl(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    const double release = 1.0 + kLaunchStageS;
    double rpmAtRelease = 0.0, boostAtRelease = 0.0, reached = 0.0;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (rpmAtRelease == 0.0 && t >= release) {
            rpmAtRelease = model.rpm();
            boostAtRelease = model.boost();
        }
        model.stepPedal(dt, t >= 1.0 ? 1.0 : 0.0, t >= 1.0 && t < release ? 1.0 : 0.0);
        if (reached == 0.0 && t >= release && model.speedKmh() >= 100.0) reached = t + dt - release;
    });
    std::printf("Launch: %.0f rpm and %.2f of the boost at brake release; 0-100 km/h in %.2f s from the release%s\n", rpmAtRelease, boostAtRelease, reached,
                reached == 0.0 ? " (not reached)" : "");
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

/** `--crank` : le lancement, toutes les 10 ms. */
std::FILE* g_crank = nullptr;
/** `--out -` n'écrit que l'empreinte : le relevé du démarrage la décalerait dans tools/check_wasm.mjs. */
bool g_hashOnly = false;

/**
 * Contact : le démarreur, le moteur qui prend, le ralenti. Le conducteur tient la clé jusqu'à ce que le moteur prenne, au plus
 * `holdS` ; raté, il attend 30 s et recommence, `attempts` fois au plus. Écrit le relevé du démarrage.
 */
void start(EngineSound& sound, DrivingModel& model, Output& out, int rate, double holdS, int attempts) {
    model.halt();
    model.crank();
    int tried = 1;
    double keyAt = 0.0;
    double nextLine = 0.0;
    double restUntil = -1.0;
    // Par petits blocs, comme dans le navigateur : la hauteur du démarreur suit le régime de très près.
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * dt;
        if (model.keyHeld() && t - keyAt >= holdS) model.releaseKey();
        if (model.startFailed() && restUntil < 0.0 && tried < attempts) restUntil = t + 30.0;
        if (restUntil >= 0.0 && t >= restUntil) {
            restUntil = -1.0;
            ++tried;
            keyAt = t;
            model.crank();
        }
        model.stepNeutral(dt, 0.0);
        if (g_crank != nullptr && t >= nextLine) {
            nextLine += 0.01;
            const StartModel& s = model.start();
            std::fprintf(g_crank, "%.2f,%.1f,%.2f,%.1f,%d,%d,%.3f,%.1f,%.3f,%d,%d\n", t, model.rpm(), s.batteryVolts(), s.starterAmps(), model.cranking() ? 1 : 0,
                         model.stalled() ? 1 : 0, s.firingChance(), s.glowC(), s.battery().charge, s.stats().firings, s.stats().misfires);
        }
    }, 128);
    if (g_hashOnly) return;
    const StartStats& s = model.start().stats();
    const bool running = !model.stalled() && !model.cranking() && model.rpm() > 0.8 * model.idleRpm();
    std::printf("Start: %s after %.2f s from the key (attempt %d of %d), cranking %.0f rpm on average before the first firing, %d firings and %d misfires,\n"
                "       battery %.0f A on average, %.2f V at the lowest, %.3f Ah drawn, %.0f %% left, at %.1f C; oil %s; preheat %.1f s\n",
                running ? "running" : (s.caught ? "caught, then stalled" : "failed"), s.caught ? s.caughtAfterS : s.crankedS, s.attempts, attempts,
                s.meanCrankRpm, s.firings, s.misfires, s.meanAmps, s.minVolts, s.drawnAh, model.start().battery().charge * 100.0, model.start().battery().celsius,
                oilGradeName(model.start().oil()), model.start().preheatS());
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

/**
 * La chaleur qui s'accumule dans l'air de charge : une minute à l'arrêt, puis trois départs arrêtés à fond pendant 8 s, un
 * freinage, et 20 s de ralenti entre eux. Écrit la charge au collecteur et la boucle d'eau au début et à la fin de chaque départ.
 */
void soak(EngineSound& sound, DrivingModel& model, Output& out, int rate) {
    int pull = 0;
    int phase = 0;
    double since = 0.0;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        const auto report = [&](const char* what) {
            std::printf("%s %d at %.0f s: charge %.1f C, water loop %.1f C, %.0f km/h\n", what, pull, t, model.manifoldCharge().temperatureK - 273.15,
                        model.chargeAir().waterLoopK() - 273.15, model.speedKmh());
        };
        switch (phase) {
            case 0:
                model.stepPedal(dt, 0.0, 0.3);
                if (pull < 3 && t - since >= (pull == 0 ? 60.0 : 20.0)) {
                    ++pull;
                    report("Before pull");
                    phase = 1;
                    since = t;
                }
                break;
            case 1:
                model.stepPedal(dt, 1.0, 0.0);
                if (t - since >= 8.0) {
                    report("End of pull");
                    phase = 2;
                    since = t;
                }
                break;
            default:
                model.stepPedal(dt, 0.0, 0.6);
                if (model.speedKmh() < 0.5) {
                    phase = 0;
                    since = t;
                }
                break;
        }
    });
    std::printf("After %d pulls: charge %.1f C, water loop %.1f C\n", pull, model.manifoldCharge().temperatureK - 273.15, model.chargeAir().waterLoopK() - 273.15);
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

/**
 * Une reprise : tenu en troisième à ce régime, puis pied au plancher. Écrit le couple de la première seconde, et le creux du
 * mélange s'il y a un carburateur. Sert à mesurer : ni le son ni les empreintes n'en dépendent.
 */
void tipIn(EngineSound& sound, DrivingModel& model, Output& out, int rate, double fromRpm, double wheelRadius) {
    constexpr double kHoldS = 12.0;
    model.setManual(true);
    model.requestShift(1);
    double held = 0.1, sum = 0.0, worst = 1.0, leanest = 0.0, entry = 0.0;
    int samples = 0;
    play(sound, model, out, rate, [&](int step, double dt) {
        const double t = static_cast<double>(step) * kTick / rate;
        if (t < kHoldS) {
            // On ne passe que si le rapport suivant retombe au-dessus du régime visé : pas de quoi caler. Ce régime se lit à la
            // vitesse de la voiture, comme au compteur : ni au moteur, qui tourne bien plus vite que la voiture ne roule tant que
            // l'embrayage patine au départ, ni aux roues, qui peuvent patiner. Lu au moteur, la troisième entrait à 2 km/h, et le
            // moteur calait dès que l'embrayage s'y refermait.
            const int now = model.gear();
            if (now >= 1 && now < 3 && !model.shifting()) {
                const double wheels = model.speedKmh() / 3.6 / wheelRadius * 60.0 / (2.0 * 3.141592653589793);
                const double road = wheels * model.gearing().overall[static_cast<std::size_t>(now - 1)];
                const double next = wheels * model.gearing().overall[static_cast<std::size_t>(now)];
                if (next > std::max(fromRpm, model.idleRpm() * 1.4) || road > model.redlineRpm() * 0.85) model.requestShift(1);
            }
            const double error = (fromRpm - model.rpm()) / 1000.0;
            held = std::clamp(held + 0.5 * error * dt, 0.02, 1.0);
            const double pedal = now < 3 ? 0.5 : std::clamp(held + 1.0 * error, 0.02, 1.0);
            model.stepPedal(dt, pedal, now == 3 && error < -0.1 ? std::min(-error, 0.5) : 0.0);
            entry = model.rpm();
            return;
        }
        model.stepPedal(dt, 1.0, 0.0);
        if (t < kHoldS + 1.0) {
            sum += model.torqueNm();
            ++samples;
            if (model.carbureted()) {
                worst = std::min(worst, model.carburetor().torqueShare());
                leanest = std::max(leanest, model.carburetor().lambda());
            }
        }
    });
    std::printf("Tip-in from %.0f rpm: mean torque of the first second %.1f Nm, mixture dip %.1f %%, leanest lambda %.2f\n", entry, sum / std::max(samples, 1), (1.0 - worst) * 100.0, leanest);
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
        const ThermalNetwork& th = model.thermal();
        const WarmUp& w = g_warmUp;
        std::printf("Temperatures (%s start): coolant %.1f C (peak %.1f), oil %.1f C (peak %.1f), metal %.1f C, thermostat %.0f %%, fan %.0f s\n",
                    w.startName(), th.coolantC(), w.peakCoolantC, th.oilC(), w.peakOilC, th.metalC(), th.thermostat() * 100.0, w.fanS);
        if (w.cold) {
            // Ce que le réseau a calculé de l'arrêt : le moteur au moment de démarrer.
            const ThermalNetwork& s = w.atStart;
            std::printf("At the start (%s, %.0f C outside): coolant %.1f C, oil %.1f C, metal %.1f C, radiator %.1f C, battery %.1f C\n", w.startName(), static_cast<double>(powertrain.engine.ambientC),
                        s.coolantC(), s.oilC(), s.metalC(), s.radiatorC(), s.batteryC());
            std::printf("Warm-up: thermostat opens at %.0f s, coolant 70 C at %.0f s, oil 80 C at %.0f s, fast idle over at %.0f s (-1: not reached)\n", w.thermostatS, w.coolant70S, w.oil80S, w.idleWarmS);
        }
    }
    const PowerSummary peak = powerSummary(powertrain);
    std::printf("Engine: %.1f Nm at %.0f rpm, %.1f kW at %.0f rpm, 0-100 km/h in %.2f s\n", peak.peakTorqueNm, peak.peakTorqueRpm, peak.peakPowerKw, peak.peakPowerRpm, measurePerformance(powertrain).zeroTo100S);
    // Au bit près, pour tools/check_wasm.mjs.
    // Le réseau thermique aussi : l'électrique n'en a pas, il lit l'air ambiant, comme rf_coolant_c.
    const double ambient = static_cast<double>(powertrain.engine.ambientC);
    const double coolant = powertrain.electric ? ambient : model.thermal().coolantC();
    const double oil = powertrain.electric ? ambient : model.thermal().oilC();
    // L'état au départ, après l'arrêt choisi : ce que rf_start_thermal donne aux jauges du site.
    const ThermalNetwork atStart = model.startThermal();
    const double startCoolant = powertrain.electric ? ambient : atStart.coolantC();
    const double startOil = powertrain.electric ? ambient : atStart.oilC();
    const double startBattery = powertrain.electric ? ambient : atStart.batteryC();
    std::printf("raw fuel_lph=%.17g fuel_trip_l=%.17g trip_km=%.17g oil_trip_ml=%.17g energy_trip_kwh=%.17g coolant_c=%.17g oil_c=%.17g start_coolant_c=%.17g start_oil_c=%.17g start_battery_c=%.17g\n", c.fuelLph(), c.fuelTripL(), c.tripKm(), c.oilTripMl(),
                c.energyTripKwh(), coolant, oil, startCoolant, startOil, startBattery);
    if (model.chargeAirEngine()) {
        // L'air de charge : la fiche à 100 km/h, face au même moteur sans eau, sans protoxyde, avec l'échangeur d'origine.
        const ManifoldCharge now = steadyManifoldCharge(powertrain, peak.peakPowerRpm);
        const ManifoldCharge stock = steadyManifoldCharge(chargeAirStock(powertrain), peak.peakPowerRpm);
        std::printf("Charge air (estimate, at %.0f rpm, full load): %.1f C at the manifold (stock intercooler, dry: %.1f C), %.2f bar, %.0f%% of the air\n", peak.peakPowerRpm,
                    now.temperatureK - 273.15, stock.temperatureK - 273.15, now.pressurePa / 1e5, now.airShare * 100.0);
        const ChargeAir& charge = model.chargeAir();
        if (charge.setup().nitrous.ratedKw > 0.0) {
            std::printf("Nitrous: from %.0f rpm, %.1f g/s with a full bottle; bottle now %.1f bar, %.0f C, %.2f kg left\n", charge.setup().nitrous.activationRpm, charge.setup().nitrous.nitrousKgS * 1000.0,
                        (charge.bottle().pressurePa - 101325.0) / 1e5, charge.bottle().kelvin - 273.15, charge.bottle().massKg);
        }
    }
}

/**
 * `--knock 1` : la carte d'avance à pleine charge stabilisée, et la limite de cliquetis de ce moteur, régime par régime. Calculée même sur
 * un moteur d'origine, où le modèle de conduite ne la lit pas : c'est ainsi qu'on vérifie qu'aucun préréglage ne cliquette.
 */
void printKnock(const Powertrain& powertrain, const EngineSound& sound) {
    std::printf("Knocking combustions heard: %u\n", static_cast<unsigned>(sound.knocks()));
    if (powertrain.electric || powertrain.engine.burnsDiesel()) {
        const float ratio = compressionRatioOf(powertrain.engine);
        std::printf("Knock: none (no spark). Compression %.1f:1 (stock %.1f:1), efficiency x%.4f\n", ratio, stockCompressionRatio(powertrain.engine), powertrain.electric ? 1.0 : compressionEfficiencyShare(powertrain.engine));
        return;
    }
    // La carte que lit le modèle : l'air du jour, la came, l'injection directe et l'avance du calculateur (combustion.cpp).
    KnockMap map;
    map.build(powertrain, knockInputsOf(powertrain));
    const FuelProperties& fuel = fuelPropertiesOf(powertrain.engine);
    std::printf("Knock: compression %.1f:1 (stock %.1f:1), RON %.0f, efficiency x%.4f, fuel x%.4f, %s\n", compressionRatioOf(powertrain.engine), stockCompressionRatio(powertrain.engine), fuel.ron,
                compressionEfficiencyShare(powertrain.engine), combustionTorqueShare(powertrain.engine) / compressionEfficiencyShare(powertrain.engine), map.sensor() ? "knock sensor" : "no knock sensor");
    std::puts("  rpm   MBT  stock  limit  margin  applied  torque  gas_K  draper_Hz");
    double worst = 1e9, worstRpm = 0.0;
    const double idle = idleRpmOf(powertrain);
    const double redline = redlineRpmOf(powertrain);
    for (double rpm = std::ceil(idle / 250.0) * 250.0; rpm <= redline; rpm += 250.0) {
        const KnockPoint point = map.at(rpm, 1.0);
        // Face à l'avance demandée : la carte d'origine et l'écart du calculateur, comme `rf_knock_margin_deg`.
        const double margin = point.limitDeg - (point.stockDeg + map.advanceOffsetDeg());
        if (margin < worst) {
            worst = margin;
            worstRpm = rpm;
        }
        // Un rotatif : le premier mode en longueur de sa chambre en lame, c/(2L), L la corde entre deux sommets.
        const EngineConfig& e = powertrain.engine;
        const double hz = isRotary(e) ? burnedSoundMs(point.gasK) / (2.0 * 1.7320508 * rotaryGeometry(static_cast<double>(e.displacementCc) / e.rotors()).radiusMm / 1000.0)
                                       : draperHz(e.boreMm / 1000.0, point.gasK);
        std::printf("%5.0f %5.1f %6.1f %6.1f %7.1f %8.1f %7.4f %6.0f %9.0f\n", rpm, point.mbtDeg, point.stockDeg, point.limitDeg, margin, map.steadyAdvanceDeg(rpm), map.steadyTorqueShare(rpm), point.gasK, hz);
    }
    std::printf("Knock margin at full load: %.1f deg at %.0f rpm\n", worst, worstRpm);
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
    bool stereo = false, showDb = false, walk = false, showFuel = false, showKnock = false, showValves = false;
    ShiftMode shiftMode = ShiftMode::Normal;
    DriveScenario driveScenario = DriveScenario::City;
    double kmh = 90.0;
    double batteryPct = 100.0;
    double crankMaxS = 10.0;
    int attempts = 1;
    std::optional<ExhaustRouting> routing;
    std::optional<Exhaust> exhaustChoice;
    std::optional<SuperchargerKind> superchargerChoice;
    std::optional<AntiLag> antiLagChoice;
    std::optional<int> turboPipeChoice;
    std::optional<Camshaft> camshaftChoice;
    std::optional<AirFilter> airFilterChoice;
    std::optional<double> strokeChoice;
    std::optional<FuelSystem> fuelSystemChoice;
    std::optional<Layout> layoutChoice;
    // Les réglages du socle moteur : retenus ici, posés après le préréglage, qu'ils remplacent.
    std::vector<std::function<void(EngineConfig&)>> tuning;
    auto later = [&tuning](auto field, auto value) { tuning.push_back([field, value](EngineConfig& e) { e.*field = value; }); };
    auto number = [](const std::string& text) { return static_cast<float>(std::atof(text.c_str())); };
    auto whole = [](const std::string& text) { return std::atoi(text.c_str()); };
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
        // `--fuel` dit deux choses, que sa valeur distingue : 0 ou 1, l'affichage de la consommation ; petrol ou diesel, le carburant
        // (plus bas, avec les réglages du moteur). Avant, l'affichage prenait tout, et `--fuel diesel` ne donnait jamais un diesel.
        else if (arg == "--fuel" && (value == "0" || value == "1")) showFuel = value != "0";
        else if (arg == "--knock") showKnock = value != "0";
        else if (arg == "--valves") showValves = value != "0";
        else if (arg == "--trace") {
            g_trace = std::fopen(value.c_str(), "w");
            if (g_trace == nullptr) {
                std::fprintf(stderr, "Cannot write: %s\n", value.c_str());
                return 2;
            }
            traceHeader();
        }
        else if (arg == "--thermal") {
            g_thermal = std::fopen(value.c_str(), "w");
            if (g_thermal == nullptr) {
                std::fprintf(stderr, "Cannot write: %s\n", value.c_str());
                return 2;
            }
            std::fputs("t,rpm,kmh,coolant_c,oil_c,metal_c,radiator_c,thermostat,fan,radiator_kw,cold_friction_nm,cold_idle_rpm,enrichment,trip_l,wall_w,torque_nm\n", g_thermal);
        }
        else if (arg == "--crank") {
            g_crank = std::fopen(value.c_str(), "w");
            if (g_crank == nullptr) {
                std::fprintf(stderr, "Cannot write: %s\n", value.c_str());
                return 2;
            }
            std::fputs("t,rpm,battery_v,starter_a,cranking,stalled,firing_chance,glow_c,charge,firings,misfires\n", g_crank);
        }
        else if (arg == "--battery") batteryPct = std::atof(value.c_str());
        else if (arg == "--crank-max") crankMaxS = std::atof(value.c_str());
        else if (arg == "--attempts") attempts = std::max(std::atoi(value.c_str()), 1);
        else if (arg == "--kmh") kmh = std::atof(value.c_str());
        else if (arg == "--walk") walk = value != "0";
        else if (arg == "--placement") { car.placement = pick<EnginePlacement>(value, {{"front", EnginePlacement::Front}, {"mid", EnginePlacement::Mid}, {"rear", EnginePlacement::Rear}}); customCar = true; }
        else if (arg == "--insulation") { car.insulation = pick<Insulation>(value, {{"stripped", Insulation::Stripped}, {"light", Insulation::Light}, {"standard", Insulation::Standard}, {"comfort", Insulation::Comfort}, {"luxury", Insulation::Luxury}}); customCar = true; }
        else if (arg == "--windows") { car.windowsOpen = value == "open"; customCar = true; }
        else if (arg == "--body") { car.body = pick<Body>(value, {{"city", Body::City}, {"compact", Body::Compact}, {"coupe", Body::Coupe}, {"supercar", Body::Supercar}, {"van", Body::Van}, {"retro", Body::Retro}}); customCar = true; }
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
            else if (arg == "--layout") { config.layout = pick<Layout>(value, {{"inline", Layout::Inline}, {"v", Layout::V}, {"cross_plane", Layout::CrossPlane}, {"boxer", Layout::Boxer}, {"rotary", Layout::Rotary}, {"vr", Layout::Vr}, {"w", Layout::W}}); layoutChoice = config.layout; }
            else if (arg == "--exhaust") { config.exhaust = pick<Exhaust>(value, {{"stock", Exhaust::Stock}, {"sport", Exhaust::Sport}, {"straight", Exhaust::Straight}, {"no_muffler", Exhaust::MufflerDelete}, {"no_resonator", Exhaust::ResonatorDelete}, {"quiet", Exhaust::Quiet}, {"expansion", Exhaust::Expansion}}); exhaustChoice = config.exhaust; }
            else if (arg == "--routing") { config.routing = pick<ExhaustRouting>(value, {{"single", ExhaustRouting::Single}, {"dual", ExhaustRouting::Dual}, {"h", ExhaustRouting::HPipe}, {"x", ExhaustRouting::XPipe}}); routing = config.routing; }
            else if (arg == "--aspiration") config.aspiration = pick<Aspiration>(value, {{"natural", Aspiration::Natural}, {"turbo", Aspiration::Turbo}, {"supercharger", Aspiration::Supercharger}});
            else if (arg == "--supercharger") superchargerChoice = pick<SuperchargerKind>(value, {{"roots", SuperchargerKind::Roots}, {"twin_screw", SuperchargerKind::TwinScrew}, {"centrifugal", SuperchargerKind::Centrifugal}});
            else if (arg == "--antilag") antiLagChoice = pick<AntiLag>(value, {{"off", AntiLag::Off}, {"retard", AntiLag::Retard}, {"air", AntiLag::AirInjection}, {"cut", AntiLag::CylinderCut}});
            else if (arg == "--turbo-pipe") turboPipeChoice = std::atoi(value.c_str());
            else if (arg == "--cam") camshaftChoice = pick<Camshaft>(value, {{"dohc", Camshaft::Dohc}, {"sohc", Camshaft::Sohc}, {"ohv", Camshaft::Ohv}});
            else if (arg == "--filter") airFilterChoice = pick<AirFilter>(value, {{"stock", AirFilter::Stock}, {"panel", AirFilter::Panel}, {"cone", AirFilter::Cone}, {"stacks", AirFilter::Stacks}});
            else if (arg == "--stroke") {
                strokeChoice = std::atof(value.c_str());
                if (*strokeChoice <= 0.0) {
                    std::fprintf(stderr, "Expected a stroke in mm: %s\n", value.c_str());
                    return 2;
                }
            }
            else if (arg == "--carb" || arg == "--fuel-system") fuelSystemChoice = pick<FuelSystem>(value, {{"injection", FuelSystem::Injection}, {"single", FuelSystem::Carburetor}, {"multi", FuelSystem::MultiCarb}, {"direct", FuelSystem::Direct}});
            // Les réglages du socle moteur, dans l'ordre d'EngineConfig : une ligne par réglage.
            else if (arg == "--ambient") later(&EngineConfig::ambientC, number(value));
            else if (arg == "--altitude") later(&EngineConfig::altitudeM, whole(value));
            else if (arg == "--start") later(&EngineConfig::startTemp, pick<StartTemp>(value, {{"warm", StartTemp::Warm}, {"cold", StartTemp::Cold}, {"half_hour", StartTemp::HalfHour}, {"two_hours", StartTemp::TwoHours}}));
            else if (arg == "--compression") later(&EngineConfig::compressionRatio, number(value));
            else if (arg == "--grade") later(&EngineConfig::fuelGrade, pick<FuelGrade>(value, {{"ron95", FuelGrade::Ron95}, {"ron98", FuelGrade::Ron98}, {"race102", FuelGrade::Race102}, {"e85", FuelGrade::E85}, {"methanol", FuelGrade::Methanol}}));
            else if (arg == "--advance") later(&EngineConfig::ignitionOffsetDeg, whole(value));
            else if (arg == "--lambda") later(&EngineConfig::fullLoadLambda, number(value));
            else if (arg == "--limiter") later(&EngineConfig::revLimiter, pick<RevLimiter>(value, {{"stock", RevLimiter::Stock}, {"soft", RevLimiter::Soft}, {"ignition_cut", RevLimiter::IgnitionCut}}));
            else if (arg == "--launch") later(&EngineConfig::launchRpm, whole(value));
            else if (arg == "--flat-shift") later(&EngineConfig::flatShift, value != "0");
            else if (arg == "--traction") later(&EngineConfig::tractionControl, pick<TractionControl>(value, {{"off", TractionControl::Off}, {"sport", TractionControl::Sport}, {"full", TractionControl::Full}}));
            else if (arg == "--diesel-injection") later(&EngineConfig::dieselInjection, pick<DieselInjection>(value, {{"mechanical", DieselInjection::Mechanical}, {"common_rail", DieselInjection::CommonRail}}));
            else if (arg == "--egr") later(&EngineConfig::egr, value != "0");
            else if (arg == "--dpf") later(&EngineConfig::particulateFilter, value != "0");
            else if (arg == "--cam-duration") later(&EngineConfig::camDurationDeg, whole(value));
            else if (arg == "--cam-lift") later(&EngineConfig::camLiftMm, number(value));
            else if (arg == "--lsa") later(&EngineConfig::lobeSeparationDeg, whole(value));
            else if (arg == "--vvt") later(&EngineConfig::valveTiming, pick<ValveTiming>(value, {{"fixed", ValveTiming::Fixed}, {"phased", ValveTiming::Phased}, {"two_step", ValveTiming::TwoStep}, {"phased_two_step", ValveTiming::PhasedTwoStep}}));
            else if (arg == "--springs") later(&EngineConfig::valveSprings, pick<ValveSprings>(value, {{"stock", ValveSprings::Stock}, {"stiff", ValveSprings::Stiff}}));
            else if (arg == "--turbo-layout") later(&EngineConfig::turboLayout, pick<TurboLayout>(value, {{"single", TurboLayout::Single}, {"twin_parallel", TurboLayout::TwinParallel}, {"twin_sequential", TurboLayout::TwinSequential}, {"twincharger", TurboLayout::Twincharger}}));
            else if (arg == "--vgt") later(&EngineConfig::variableGeometry, value != "0");
            else if (arg == "--boost-control") later(&EngineConfig::boostControl, pick<BoostControl>(value, {{"spring", BoostControl::Spring}, {"electronic", BoostControl::Electronic}, {"by_gear", BoostControl::ByGear}}));
            else if (arg == "--intercooler") later(&EngineConfig::intercooler, pick<Intercooler>(value, {{"air", Intercooler::AirToAir}, {"water", Intercooler::AirToWater}, {"none", Intercooler::None}}));
            else if (arg == "--water") later(&EngineConfig::waterInjection, value != "0");
            else if (arg == "--nitrous") later(&EngineConfig::nitrousKw, whole(value));
            else if (arg == "--cycle") later(&EngineConfig::cycle, pick<EngineCycle>(value, {{"four_stroke", EngineCycle::FourStroke}, {"two_stroke", EngineCycle::TwoStroke}}));
            else if (arg == "--oil") later(&EngineConfig::oilGrade, pick<OilGrade>(value, {{"5w30", OilGrade::Sae5W30}, {"0w20", OilGrade::Sae0W20}, {"10w40", OilGrade::Sae10W40}, {"15w40", OilGrade::Sae15W40}, {"20w50", OilGrade::Sae20W50}, {"sae30", OilGrade::Sae30}}));
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
    if (fuelSystemChoice) powertrain.engine.fuelSystem = *fuelSystemChoice;
    // La disposition aussi : sur les cylindres du préréglage, qu'elle ne change pas. Un VR ou un W qui n'existe pas à ce nombre se construit en V.
    if (layoutChoice) powertrain.engine.layout = *layoutChoice;
    for (const auto& apply : tuning) apply(powertrain.engine);
    if (powertrain.engine.cylinders < 1 || powertrain.engine.cylinders > kMaxCylinders) {
        std::fprintf(stderr, "Expected 1 to %d cylinders: %d\n", kMaxCylinders, powertrain.engine.cylinders);
        return 2;
    }
    if (!powertrain.electric && builtLayout(powertrain.engine.layout, powertrain.engine.cylinders) != powertrain.engine.layout) {
        std::fprintf(stderr, "--layout: no such engine with %d cylinders, built as v (vr: 5 or 6, w: 8, 12 or 16, more than 12: v or w)\n", powertrain.engine.cylinders);
    }
    // À la fin : la course se lit sur l'alésage et les cylindres retenus, ceux du préréglage s'il y en a un.
    if (strokeChoice) {
        // Un rotatif n'a pas de course : l'option ne dit rien de sa cylindrée.
        if (isRotary(powertrain.engine)) std::fprintf(stderr, "--stroke ignored: a rotary engine has no stroke, use --cc\n");
        else powertrain.engine.displacementCc = displacementForStroke(powertrain.engine.boreMm, powertrain.engine.cylinders, *strokeChoice);
    }
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
    // Un deux-temps balayé par son carter n'a ni diesel ni suralimentation : ce qui n'existe pas revient à ce qui existe.
    if (coherentTwoStroke(powertrain.engine)) std::fprintf(stderr, "two-stroke: crankcase-scavenged, petrol and naturally aspirated only (at most %d cylinders)\n", powertrain.engine.pistons());
    const EngineProfile profile = powertrain.profile();
    DrivingModel model(powertrain);
    if (batteryPct != 100.0) model.setBatteryCharge(std::clamp(batteryPct, 0.0, 100.0) / 100.0);
    g_warmUp.active = !powertrain.electric;
    g_warmUp.cold = powertrain.engine.startTemp != StartTemp::Warm;
    g_warmUp.start = powertrain.engine.startTemp;
    g_warmUp.atStart = model.startThermal();
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
        else if (scenario == "tipin") seconds = 16;
        else if (scenario == "launch_control") seconds = 16;
        else if (scenario == "soak") seconds = 170;
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
    g_hashOnly = out == "-";
    if (scenario == "lap") {
        lap(sound, model, output, rate, walk);
    } else if (scenario == "blips") {
        blips(sound, model, output, rate);
    } else if (scenario == "launch") {
        launch(sound, model, output, rate);
    } else if (scenario == "launch_control") {
        launchControl(sound, model, output, rate);
    } else if (scenario == "start") {
        start(sound, model, output, rate, crankMaxS, attempts);
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
    } else if (scenario == "tipin") {
        tipIn(sound, model, output, rate, rpm, wheelRadiusM(powertrain.car));
    } else if (scenario == "soak") {
        soak(sound, model, output, rate);
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
        if (showKnock) printKnock(powertrain, sound);
        if (showValves) printValves(powertrain);
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
        if (showKnock) printKnock(powertrain, sound);
        if (showValves) printValves(powertrain);
        return 0;
    }
    writeWav(out, pcm, rate, channels);
    if (showFuel) printFuel(model, powertrain, measured, driver.restarts);
    if (showKnock) printKnock(powertrain, sound);
    if (showValves) printValves(powertrain);
    if (showDb) std::printf("LAeq %.1f dB(A), LAFmax %.1f dB(A)\n", 10.0 * std::log10(output.energy / static_cast<double>(std::max(output.reads, 1L))), output.loudest);
    return 0;
}
