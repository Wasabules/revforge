// Interface C du moteur pour WebAssembly : un seul moteur, piloté depuis l'AudioWorklet.
#include <new>

#include "revforge/atmosphere.hpp"
#include "revforge/cam.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/injection.hpp"
#include "revforge/particulate_filter.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/sound_sheet.hpp"
#include "revforge/valve_float.hpp"
#include "../core/src/exhaust_line.hpp"

using namespace revforge;

namespace {

constexpr int kMaxFrames = 8192;

enum Mode { kOff = 0, kNeutral = 1, kDrive = 2, kLap = 3, kManual = 4 };

EngineSound* g_sound = nullptr;
DrivingModel* g_model = nullptr;
Powertrain g_powertrain;
float g_buffer[kMaxFrames];
float g_bufferRight[kMaxFrames];
float g_sampleRate = 48000.0f;
int g_mode = kOff;
double g_pedal = 0.0;
double g_brake = 0.0;
float g_manualRpm = 0.0f;
bool g_manualGearbox = false;
bool g_clutchAssist = true;
ShiftMode g_shiftMode = ShiftMode::Normal;
DriveScenario g_scenario = DriveScenario::City;

void applyPowertrain() {
    g_sound->setProfile(g_powertrain.profile());
    g_model->setPowertrain(g_powertrain);
}

CarAcoustics carNow() { return g_powertrain.profile().car; }

}  // namespace

extern "C" {

void rf_init(int sampleRate) {
    g_sampleRate = static_cast<float>(sampleRate);
    delete g_sound;
    delete g_model;
    g_sound = new EngineSound(sampleRate);
    g_powertrain = Powertrain{};
    g_model = new DrivingModel(g_powertrain);
    g_model->halt();
    g_sound->setVolume(0.7f);
    g_mode = kOff;
    g_pedal = 0.0;
    g_brake = 0.0;
    g_shiftMode = ShiftMode::Normal;
    g_scenario = DriveScenario::City;
}

float* rf_buffer() { return g_buffer; }
float* rf_buffer_right() { return g_bufferRight; }

void rf_set_config(int cylinders, int layout, int displacementCc, int boreMm, int redlineRpm, int exhaust, int aspiration, int fuel,
                   float multiplier, int pops, int release, float boostBar, int routing, int turboSize,
                   int superchargerKind, int antiLag, int turboPipeMm, int camshaft, int airFilter, int fuelSystem) {
    // Les réglages du socle, posés par rf_set_tuning, restent : tous les autres champs sont écrits ici.
    EngineConfig c = g_powertrain.engine;
    c.cylinders = cylinders;
    c.layout = static_cast<Layout>(layout);
    c.displacementCc = displacementCc;
    c.boreMm = boreMm;
    c.redlineRpm = redlineRpm;
    c.exhaust = static_cast<Exhaust>(exhaust);
    c.aspiration = static_cast<Aspiration>(aspiration);
    c.fuel = static_cast<Fuel>(fuel);
    c.revMultiplier = multiplier;
    c.pops = static_cast<PopLevel>(pops);
    c.turboRelease = static_cast<TurboRelease>(release);
    c.boostBar = boostBar;
    c.routing = static_cast<ExhaustRouting>(routing);
    c.turboSize = static_cast<TurboSize>(turboSize);
    c.superchargerKind = static_cast<SuperchargerKind>(superchargerKind);
    c.antiLag = static_cast<AntiLag>(antiLag);
    c.turboPipeMm = turboPipeMm;
    c.camshaft = static_cast<Camshaft>(camshaft);
    c.airFilter = static_cast<AirFilter>(airFilter);
    c.fuelSystem = static_cast<FuelSystem>(fuelSystem);
    g_powertrain.engine = c;
    g_powertrain.electric = false;
    applyPowertrain();
}

/**
 * Les réglages du socle moteur, dans l'ordre d'`EngineConfig` et de `tuningArgs` (config.ts). Ne reconstruit rien : à
 * appeler juste avant rf_set_config ou rf_set_electric, qui les appliquent, comme rf_set_gear_ratio avant rf_set_gearbox.
 * Un seul changement de moteur, donc, et jamais un moteur à moitié réglé entre deux appels.
 */
void rf_set_tuning(float ambientC, int altitudeM, int startTemp, float compressionRatio, int fuelGrade, int ignitionOffsetDeg,
                   float fullLoadLambda, int revLimiter, int launchRpm, int flatShift, int tractionControl, int dieselInjection, int egr,
                   int particulateFilter, int camDurationDeg, float camLiftMm, int lobeSeparationDeg, int valveTiming, int valveSprings,
                   int turboLayout, int variableGeometry, int boostControl, int intercooler, int waterInjection, int nitrousKw, int cycle) {
    EngineConfig& c = g_powertrain.engine;
    c.ambientC = ambientC;
    c.altitudeM = altitudeM;
    c.startTemp = static_cast<StartTemp>(startTemp);
    c.compressionRatio = compressionRatio;
    c.fuelGrade = static_cast<FuelGrade>(fuelGrade);
    c.ignitionOffsetDeg = ignitionOffsetDeg;
    c.fullLoadLambda = fullLoadLambda;
    c.revLimiter = static_cast<RevLimiter>(revLimiter);
    c.launchRpm = launchRpm;
    c.flatShift = flatShift != 0;
    c.tractionControl = static_cast<TractionControl>(tractionControl);
    c.dieselInjection = static_cast<DieselInjection>(dieselInjection);
    c.egr = egr != 0;
    c.particulateFilter = particulateFilter != 0;
    c.camDurationDeg = camDurationDeg;
    c.camLiftMm = camLiftMm;
    c.lobeSeparationDeg = lobeSeparationDeg;
    c.valveTiming = static_cast<ValveTiming>(valveTiming);
    c.valveSprings = static_cast<ValveSprings>(valveSprings);
    c.turboLayout = static_cast<TurboLayout>(turboLayout);
    c.variableGeometry = variableGeometry != 0;
    c.boostControl = static_cast<BoostControl>(boostControl);
    c.intercooler = static_cast<Intercooler>(intercooler);
    c.waterInjection = waterInjection != 0;
    c.nitrousKw = nitrousKw;
    c.cycle = static_cast<EngineCycle>(cycle);
}

void rf_set_electric() {
    g_powertrain.electric = true;
    applyPowertrain();
}

/** La boîte comme pièce : le type, le nombre de rapports, la famille, le pont, la denture, la taille. */
void rf_set_gearbox(int kind, int gears, int family, float finalDrive, int teeth, float helixDeg, float moduleMm, float limitNm, int flywheel) {
    g_powertrain.gearbox.kind = static_cast<GearboxKind>(kind);
    g_powertrain.gearbox.gears = gears;
    g_powertrain.gearbox.family = static_cast<GearFamily>(family);
    g_powertrain.gearbox.finalDrive = finalDrive;
    g_powertrain.gearbox.form = static_cast<ToothForm>(teeth);
    g_powertrain.gearbox.helixDeg = helixDeg;
    g_powertrain.gearbox.moduleMm = moduleMm;
    g_powertrain.gearbox.limitNm = limitNm;
    g_powertrain.gearbox.flywheel = static_cast<Flywheel>(flywheel);
    applyPowertrain();
}

/** Famille sur mesure : le rapport de chaque vitesse. À appeler avant rf_set_gearbox. */
void rf_set_gear_ratio(int gear, float ratio) {
    if (gear >= 1 && gear <= kMaxGears) g_powertrain.gearbox.ratios[static_cast<std::size_t>(gear - 1)] = ratio;
}

void rf_set_car(int body, int massKg, int rimInch, int drive, int placement, int insulation) {
    const bool windows = g_powertrain.car.windowsOpen;
    g_powertrain.car = CarConfig{static_cast<Body>(body), massKg, rimInch, static_cast<Drive>(drive), static_cast<EnginePlacement>(placement), static_cast<Insulation>(insulation), windows};
    applyPowertrain();
}

void rf_set_windows(int open) {
    g_powertrain.car.windowsOpen = open != 0;
    applyPowertrain();
}

/** Changer de mode repart d'une voiture à l'arrêt, sauf entre point mort et conduite. */
void rf_set_mode(int mode) {
    if (mode == g_mode) return;
    if (mode == kLap || g_mode == kLap) {
        g_model->~DrivingModel();
        new (g_model) DrivingModel(g_powertrain);
        // Le pilote automatique ne sait conduire qu'une automatique : le choix de la manuelle attend qu'on reprenne le volant.
        g_model->setManual(g_manualGearbox && mode != kLap);
        g_model->setClutchAssist(g_clutchAssist);
        g_model->setShiftMode(g_shiftMode);
        g_model->setDriveScenario(g_scenario);
    }
    g_mode = mode;
}

void rf_set_controls(float pedal, float brake, float clutch) {
    g_pedal = pedal;
    g_brake = brake;
    g_model->setClutchPedal(clutch);
}

void rf_set_transmission(int manual) {
    g_manualGearbox = manual != 0;
    g_model->setManual(g_manualGearbox && g_mode != kLap);
}

void rf_shift(int direction) { g_model->requestShift(direction); }

/** Comment l'automatique choisit ses rapports : position dans `ShiftMode`. Reste valable d'un mode de conduite à l'autre. */
void rf_set_shift_mode(int mode) {
    if (mode < 0 || mode >= kShiftModes) return;
    g_shiftMode = static_cast<ShiftMode>(mode);
    g_model->setShiftMode(g_shiftMode);
}

/** Le trajet du pilote automatique : position dans `DriveScenario`. Il change en roulant, sans repartir de zéro. */
void rf_set_scenario(int scenario) {
    if (scenario < 0 || scenario >= kDriveScenarios) return;
    g_scenario = static_cast<DriveScenario>(scenario);
    g_model->setDriveScenario(g_scenario);
}

void rf_set_clutch_assist(int assisted) {
    g_clutchAssist = assisted != 0;
    g_model->setClutchAssist(g_clutchAssist);
}

void rf_repair_gearbox() { g_model->repairGearbox(); }

void rf_crank() {
    g_model->crank();
}

void rf_set_listener(int listener) { g_sound->setListener(static_cast<Listener>(listener)); }

void rf_set_listener_position(float x, float y, float z) { g_sound->setListenerPosition(x, y, z); }

void rf_set_manual_rpm(float rpm) { g_manualRpm = rpm; }

void rf_set_volume(float volume) { g_sound->setVolume(volume); }

/** Le modèle de conduite avance d'autant, et le son reçoit son état. */
static void advance(int frames) {
    const double dt = static_cast<double>(frames) / static_cast<double>(g_sampleRate);
    float target = 0.0f;
    bool modelled = true;
    switch (g_mode) {
        case kNeutral: g_model->stepNeutral(dt, g_pedal); break;
        case kDrive: g_model->stepPedal(dt, g_pedal, g_brake); break;
        case kLap: g_model->step(dt); break;
        case kManual:
            modelled = false;
            target = g_manualRpm;
            break;
        default: g_model->stepOff(dt); break;
    }
    if (modelled) {
        target = static_cast<float>(g_model->rpm());
        g_sound->setDrive(static_cast<float>(g_model->load()), static_cast<float>(g_model->boost()), g_model->cut(), g_model->cranking(), static_cast<float>(g_model->throttle()));
        g_sound->setTurbo(g_model->turboDrive());
        if (g_model->twinTurbo() && g_model->twin().spec().units == 2) g_sound->setTurboB(g_model->turboDriveB());
        g_sound->setSupercharger(g_model->superchargerDrive());
        g_sound->setEcu(g_model->ecuDrive());
        g_sound->setStarter(g_model->starterPowered(), g_model->starterMeshed(), static_cast<float>(g_model->starterLoad()));
        g_sound->setRoad(static_cast<float>(g_model->speedKmh() / 3.6), static_cast<float>(g_model->wheelSlip()));
        g_sound->setGearbox(g_model->box(), g_model->gearboxDrive());
        g_sound->setKnock(g_model->knockDrive());
        g_sound->setCam(g_model->camDrive());
        if (g_mode == kOff && g_powertrain.electric) target = 0.0f;
    }
    // Régime imposé de l'extérieur : plus de voiture, donc plus de boîte qui tourne.
    if (!modelled) g_sound->setGearbox(g_model->box(), GearboxDrive{});
    g_sound->setTargetRpm(target);
}

/** Un seul canal dans rf_buffer : un micro au centre de la tête. */
void rf_render(int frames) {
    if (frames > kMaxFrames) frames = kMaxFrames;
    advance(frames);
    g_sound->render(g_buffer, frames);
}

/** Les deux oreilles, dans rf_buffer et rf_buffer_right. */
void rf_render_stereo(int frames) {
    if (frames > kMaxFrames) frames = kMaxFrames;
    advance(frames);
    g_sound->renderStereo(g_buffer, g_bufferRight, frames);
}

float rf_rpm() { return g_sound->targetRpm(); }
float rf_speed() { return static_cast<float>(g_model->speedKmh()); }
int rf_gear() { return g_model->gear(); }
float rf_throttle() { return static_cast<float>(g_model->throttle()); }
float rf_load() { return g_sound->load(); }
float rf_boost() { return static_cast<float>(g_model->boost()); }
float rf_turbine() { return g_sound->turbine(); }
float rf_turbo_release() { return g_sound->turboRelease(); }
/** L'arbre et les pressions, en unités physiques : tr/min, bar au-dessus de l'atmosphère, °C, kg/s. */
float rf_turbo_rpm() { return static_cast<float>(g_model->turboState().shaftOmega * 60.0 / (2.0 * 3.141592653589793)); }
// Comme un manomètre de suralimentation : au-dessus de l'atmosphère du jour (101 325 Pa à 0 m, voir atmosphere.hpp).
static float gaugeBar(double pascals) { return static_cast<float>((pascals - ambientAirOf(g_powertrain.engine).pressurePa) / 1e5); }
// Le chemin de l'air est le même avec un compresseur : mêmes jauges, lues là où il est.
float rf_turbo_charge() { return gaugeBar(g_model->supercharged() ? g_model->supercharger().state().chargePa : g_model->turboState().chargePa); }
float rf_turbo_plenum() { return gaugeBar(g_model->supercharged() ? g_model->supercharger().state().plenumPa : g_model->turboState().plenumPa); }
float rf_turbo_outlet() { return gaugeBar(g_model->supercharged() ? g_model->supercharger().state().outletPa : g_model->turboState().outletPa); }
float rf_turbo_inlet() { return gaugeBar(g_model->supercharged() ? g_model->supercharger().state().inletPa : g_model->turboState().inletPa); }
float rf_turbo_exhaust() { return gaugeBar(g_model->turboState().turbineInPa); }
float rf_turbo_wastegate() { return static_cast<float>(g_model->turboState().wastegate); }
float rf_turbo_blowoff() { return static_cast<float>(g_model->turboState().blowOff); }
float rf_turbo_flow() { return static_cast<float>(g_model->supercharged() ? g_model->supercharger().state().engineFlow : g_model->turboState().compressorFlow); }
float rf_turbo_out_c() { return static_cast<float>((g_model->supercharged() ? g_model->supercharger().state().compressorOutK : g_model->turboState().compressorOutK) - 273.15); }
float rf_turbo_cool_c() { return static_cast<float>((g_model->supercharged() ? g_model->supercharger().state().coolerOutK : g_model->turboState().coolerOutK) - 273.15); }
float rf_turbo_gas_c() { return static_cast<float>(g_model->turboState().turbineInK - 273.15); }
int rf_antilag_active() { return g_model->antiLagActive() ? 1 : 0; }
float rf_antilag_heat_kw() { return static_cast<float>(g_model->turbo().antiLag().collectorHeatW / 1000.0); }
float rf_antilag_air_flow() { return static_cast<float>(g_model->turbo().antiLag().airFlow); }
float rf_antilag_lost_nm() { return static_cast<float>(g_model->antiLagLostNm()); }
/**
 * La régulation de pression, en bar absolus : la pression demandée et celle qui règne avant le papillon. Le rapport cyclique
 * de l'électrovanne, de 0 à 1 ; −1 sur une wastegate à ressort, qui n'en a pas. Sans turbo : l'air ambiant, et −1.
 */
float rf_boost_target() { return static_cast<float>(g_model->boostTargetPa() / 1e5); }
float rf_boost_charge() { return static_cast<float>((g_model->supercharged() ? g_model->supercharger().state().chargePa : g_model->turbo().state().chargePa) / 1e5); }
float rf_boost_duty() { return static_cast<float>(g_model->boostDuty()); }
/** Ce que le moteur a choisi : diamètre de la roue (mm), inertie de l'arbre (kg·m²). */
float rf_intake_air_c(float rpm) { return static_cast<float>(intakeAirK(g_powertrain, static_cast<double>(rpm)) - 273.15); }
float rf_intake_drop_kpa(float rpm) { return static_cast<float>(intakeDropPa(g_powertrain, static_cast<double>(rpm)) / 1000.0); }
static TurboSpec turboNow() { return usesTwinTurbo(g_powertrain.engine) && !g_powertrain.electric ? twinTurboSpecOf(g_powertrain).unit[0] : turboSpecOf(g_powertrain); }
float rf_turbo_wheel_mm() { return static_cast<float>(turboNow().wheelDiameterM * 1000.0); }
float rf_turbo_inertia() { return static_cast<float>(turboNow().inertia); }
/** Le compresseur mécanique : tr/min de l'arbre, kW pris au vilebrequin, part du débit rendue à l'entrée. */
static bool twincharged() { return g_model->twinTurbo() && g_model->twin().hasCharger(); }
float rf_supercharger_rpm() { return static_cast<float>((twincharged() ? g_model->twin().state().rotorOmega : g_model->supercharger().state().rotorOmega) * 60.0 / (2.0 * 3.141592653589793)); }
float rf_supercharger_kw() { return static_cast<float>(twincharged() ? g_model->twin().state().chargerNm * g_model->rpm() * 2.0 * 3.141592653589793 / 60.0 / 1000.0 : g_model->supercharger().state().shaftW / 1000.0); }
float rf_supercharger_spill() {
    if (!twincharged()) return static_cast<float>(g_model->supercharger().state().spillShare);
    const TwinTurboState& w = g_model->twin().state();
    return static_cast<float>(w.chargerFlow > 1e-6 && w.flapFlow > 0.0 ? std::min(w.flapFlow / w.chargerFlow, 1.0) : 0.0);
}
/** Ce que le moteur a choisi : rapport d'entraînement, volume engendré (L par tour), diamètre de la roue (mm). */
static SuperchargerSpec chargerNow() {
    const EngineConfig& e = g_powertrain.engine;
    return !g_powertrain.electric && usesTwinTurbo(e) && e.turboLayout == TurboLayout::Twincharger ? twinTurboSpecOf(g_powertrain).charger : superchargerSpecOf(g_powertrain);
}
float rf_supercharger_ratio() { return static_cast<float>(chargerNow().driveRatio); }
float rf_supercharger_volume() { const SuperchargerSpec spec = chargerNow(); return spec.kind == SuperchargerKind::Centrifugal ? 0.0f : static_cast<float>(spec.rotorVolumeM3 * 1000.0); }
float rf_supercharger_wheel_mm() { const SuperchargerSpec spec = chargerNow(); return spec.kind == SuperchargerKind::Centrifugal ? static_cast<float>(spec.wheelDiameterM * 1000.0) : 0.0f; }
/**
 * Deux turbos, twincharger, aubes (twin_turbo.cpp) : le second arbre, l'étape du séquentiel et ses vannes, l'embrayage et le
 * volet du compresseur, l'ouverture des aubes (0 fermées, 1 grandes ouvertes). Tout à zéro sur un turbo simple.
 */
static bool twinNow() { return g_model->twinTurbo(); }
int rf_turbo_units() { return twinNow() ? g_model->twin().spec().units : (!g_powertrain.electric && g_powertrain.engine.aspiration == Aspiration::Turbo ? 1 : 0); }
float rf_turbo2_rpm() { return twinNow() ? static_cast<float>(g_model->turboStateB().shaftOmega * 60.0 / (2.0 * 3.141592653589793)) : 0.0f; }
float rf_turbo2_exhaust() { return twinNow() && g_model->twin().spec().units == 2 ? gaugeBar(g_model->turboStateB().turbineInPa) : 0.0f; }
float rf_turbo2_wastegate() { return twinNow() ? static_cast<float>(g_model->turboStateB().wastegate) : 0.0f; }
float rf_turbo2_flow() { return twinNow() ? static_cast<float>(g_model->turboStateB().compressorFlow) : 0.0f; }
float rf_turbo2_gas_c() { return twinNow() && g_model->twin().spec().units == 2 ? static_cast<float>(g_model->turboStateB().turbineInK - 273.15) : 20.0f; }
static float vaneOpening(int unit) {
    if (!twinNow() || !g_model->twin().spec().variableGeometry) return 0.0f;
    const TwinTurboSpec& spec = g_model->twin().spec();
    return static_cast<float>((g_model->twin().state().vane[static_cast<std::size_t>(unit)] - spec.vaneMin) / (spec.vaneMax - spec.vaneMin));
}
float rf_turbo_vane() { return vaneOpening(0); }
float rf_turbo2_vane() { return vaneOpening(1); }
int rf_twin_stage() { return twinNow() ? g_model->twin().state().stage : 0; }
float rf_twin_valve() { return twinNow() ? static_cast<float>(g_model->twin().state().exhaustValve) : 0.0f; }
int rf_twin_online() { return twinNow() && g_model->twin().state().secondaryOnline ? 1 : 0; }
int rf_charger_clutch() { return twincharged() && g_model->twin().state().clutch ? 1 : 0; }
float rf_charger_flap() { return twincharged() ? static_cast<float>(g_model->twin().state().flap) : 0.0f; }
float rf_charger_interstage() { return twincharged() ? gaugeBar(g_model->twin().state().interstagePa) : 0.0f; }
float rf_charger_off_rpm() { const EngineConfig& e = g_powertrain.engine; return !g_powertrain.electric && usesTwinTurbo(e) && e.turboLayout == TurboLayout::Twincharger ? static_cast<float>(twinTurboSpecOf(g_powertrain).clutchOffRpm) : 0.0f; }
float rf_torque() { return static_cast<float>(g_model->torqueNm()); }
float rf_power() { return static_cast<float>(g_model->powerKw()); }
float rf_clutch() { return static_cast<float>(g_model->clutch()); }
float rf_wheel_rpm() { return static_cast<float>(g_model->wheelRpm()); }
float rf_slip() { return static_cast<float>(g_model->wheelSlip()); }
int rf_pops() { return static_cast<int>(g_sound->pops()); }
int rf_cut() { return g_model->cut() ? 1 : 0; }
int rf_shifting() { return g_model->shifting() ? 1 : 0; }
int rf_cranking() { return g_model->cranking() ? 1 : 0; }
int rf_stalled() { return g_model->stalled() ? 1 : 0; }
int rf_lugging() { return g_model->lugging() ? 1 : 0; }
int rf_leg_kind() { return static_cast<int>(g_model->legKind()); }
float rf_leg_target() { return static_cast<float>(g_model->legTarget()); }
float rf_grade_pct() { return static_cast<float>(g_model->roadGradePct()); }
int rf_underpowered() { return g_model->underpowered() ? 1 : 0; }
float rf_scenario_seconds() { return static_cast<float>(scenarioOf(g_scenario).loopSec); }
float rf_level_dba() { return g_sound->levelDbA(); }
float rf_listener_x() { return g_sound->listenerX(); }
float rf_listener_y() { return g_sound->listenerY(); }
float rf_listener_z() { return g_sound->listenerZ(); }
float rf_input_rpm() { return static_cast<float>(g_model->inputRpm()); }
float rf_output_rpm() { return static_cast<float>(g_model->outputRpm()); }
int rf_meshed() { return g_model->meshed() ? 1 : 0; }
int rf_shift_step() { return g_model->shiftStep(); }
float rf_grinding() { return static_cast<float>(g_model->grinding()); }
float rf_overload() { return static_cast<float>(g_model->overload()); }
float rf_box_wear() { return static_cast<float>(g_model->wear()); }
int rf_box_broken() { return g_model->broken() ? 1 : 0; }
int rf_box_pending() { return g_model->pendingGear(); }
float rf_box_cone_slip() { return static_cast<float>(g_model->coneSlip()); }
float rf_box_rattle() { return g_sound->gearboxStrike(); }
float rf_box_loose(int index) { return g_sound->gearboxLoose(index); }

// ── Consommation estimée : voir docs/modele.md ──

float rf_fuel_lph() { return static_cast<float>(g_model->consumption().fuelLph()); }
float rf_fuel_l_per_100km() { return static_cast<float>(g_model->consumption().fuelLper100km()); }
float rf_fuel_trip_l() { return static_cast<float>(g_model->consumption().fuelTripL()); }
float rf_fuel_avg_l_per_100km() { return static_cast<float>(g_model->consumption().fuelAvgLper100km()); }
float rf_trip_km() { return static_cast<float>(g_model->consumption().tripKm()); }
float rf_co2_g_per_km() { return static_cast<float>(g_model->consumption().co2GPerKm()); }
float rf_oil_gph() { return static_cast<float>(g_model->consumption().oilGph()); }
float rf_oil_trip_ml() { return static_cast<float>(g_model->consumption().oilTripMl()); }
float rf_oil_ml_per_1000km() { return static_cast<float>(g_model->consumption().oilMlPer1000km()); }
float rf_energy_kwh_per_100km() { return static_cast<float>(g_model->consumption().energyKwhPer100km()); }
float rf_energy_avg_kwh_per_100km() { return static_cast<float>(g_model->consumption().energyAvgKwhPer100km()); }
float rf_energy_trip_kwh() { return static_cast<float>(g_model->consumption().energyTripKwh()); }
void rf_reset_trip() { g_model->resetTrip(); }

// ── L'embrayage, pour l'affichage ──

float rf_clutch_input_rpm() { return static_cast<float>(g_model->inputRpm()); }
float rf_clutch_slip_kw() { return static_cast<float>(g_model->clutchSlipKw()); }
float rf_clutch_torque() { return static_cast<float>(g_model->clutchTorqueNm()); }

// ── La ligne d'échappement, pour l'affichage : le même calcul que celui qui filtre le son ──

/** Réponse impulsionnelle de la ligne de ce réglage. Allouée au premier appel, jamais par le worklet. */
const float* rf_line_impulse() {
    if (g_powertrain.electric) return nullptr;
    static ExhaustLine* line = nullptr;
    if (!line) line = new ExhaustLine(g_sampleRate);
    const EngineProfile profile = g_powertrain.profile();
    return line->compute(profile.exhaust, profile.exhaustScale, g_powertrain.engine.sweptCcPerTwoTurns() * 1e-6, profile.cylinders).data();
}
int rf_line_impulse_length() { return ExhaustLine::kImpulseLength; }

// ── Fiche technique, sans rien jouer ──

float rf_torque_at(float rpm) { return static_cast<float>(torqueCurveNm(g_powertrain, rpm)); }
float rf_power_at(float rpm) {
    return static_cast<float>(torqueCurveNm(g_powertrain, rpm) * rpm * 2.0 * 3.141592653589793 / 60.0 / 1000.0);
}
float rf_peak_torque() { return static_cast<float>(powerSummary(g_powertrain).peakTorqueNm); }
float rf_peak_torque_rpm() { return static_cast<float>(powerSummary(g_powertrain).peakTorqueRpm); }
float rf_peak_power() { return static_cast<float>(powerSummary(g_powertrain).peakPowerKw); }
float rf_peak_power_rpm() { return static_cast<float>(powerSummary(g_powertrain).peakPowerRpm); }
float rf_idle_rpm() { return static_cast<float>(idleRpmOf(g_powertrain)); }
float rf_redline() { return static_cast<float>(redlineRpmOf(g_powertrain)); }
int rf_starter_direct() { return g_powertrain.profile().starter.kind == StarterKind::Direct ? 1 : 0; }
float rf_starter_kw() { return g_powertrain.profile().starter.powerKw; }
int rf_starter_ring() { return g_powertrain.profile().starter.ringTeeth; }
int rf_starter_pinion() { return g_powertrain.profile().starter.pinionTeeth; }
float rf_starter_helix() { return g_powertrain.profile().starter.helixDeg; }
// Le carburateur, pour la fiche : 0 corps sur une injection, un diesel ou un moteur électrique.
int rf_carb_throats() { return g_powertrain.electric ? 0 : carburetorSpecOf(g_powertrain.engine).throats; }
float rf_carb_throat_mm() { return g_powertrain.electric ? 0.0f : static_cast<float>(carburetorSpecOf(g_powertrain.engine).throatM * 1000.0); }
/** Ce que le venturi et la pipe prennent à la puissance maximale (kPa). */
float rf_carb_drop_kpa() {
    if (g_powertrain.electric) return 0.0f;
    const CarburetorSpec carb = carburetorSpecOf(g_powertrain.engine);
    if (carb.kind == FuelSystem::Injection) return 0.0f;
    Powertrain injected = g_powertrain;
    injected.engine.fuelSystem = FuelSystem::Injection;
    const double rpm = powerSummary(g_powertrain).peakPowerRpm;
    return static_cast<float>(carburetorDropPa(carb, g_powertrain.engine.sweptCcPerTwoTurns() * 1e-6, rpm, 0.9 * breathingOf(injected, rpm)) / 1000.0);
}
// ── La combustion, pour la fiche : le taux réel, ce qu'il rend, et la marge au cliquetis à pleine charge ──
float rf_compression_ratio() { return g_powertrain.electric ? 0.0f : compressionRatioOf(g_powertrain.engine); }
float rf_compression_efficiency() { return g_powertrain.electric ? 1.0f : static_cast<float>(compressionEfficiencyShare(g_powertrain.engine)); }
/** La carte de cliquetis de la fiche, même sur un moteur d'origine : construite une fois par réglage, sur le fil de la page seulement. */
static const KnockMap& specKnock() {
    static KnockMap map;
    static Powertrain built;
    static bool ready = false;
    if (!ready || !(built.engine == g_powertrain.engine) || built.electric != g_powertrain.electric || placementOf(built.car) != placementOf(g_powertrain.car)) {
        map.build(g_powertrain);
        built = g_powertrain;
        ready = true;
    }
    return map;
}
int rf_knock_sensor() { return specKnock().valid() && specKnock().sensor() ? 1 : 0; }
/** La plus petite marge de la carte d'origine sous la limite de cliquetis, à pleine charge (degrés, négative s'il cliquette), et son régime. */
static float knockMargin(bool wantRpm) {
    if (g_powertrain.electric || g_powertrain.engine.burnsDiesel()) return 0.0f;
    const KnockMap& map = specKnock();
    const double idle = idleRpmOf(g_powertrain);
    const double redline = redlineRpmOf(g_powertrain);
    double worst = 1e9, at = idle;
    for (double rpm = idle; rpm <= redline; rpm += 100.0) {
        const KnockPoint point = map.at(rpm, 1.0);
        const double margin = point.limitDeg - (point.stockDeg + map.advanceOffsetDeg());
        if (margin < worst) {
            worst = margin;
            at = rpm;
        }
    }
    return static_cast<float>(wantRpm ? at : worst);
}
float rf_knock_margin_deg() { return knockMargin(false); }
float rf_knock_margin_rpm() { return knockMargin(true); }
// ── Le cliquetis, en roulant : l'avance appliquée, la limite, ce que le capteur retire, la part des cycles qui cliquettent ──
float rf_spark_advance() { return static_cast<float>(g_model->sparkAdvanceDeg()); }
float rf_knock_limit() { return static_cast<float>(g_model->knockLimitDeg()); }
float rf_knock_retard() { return static_cast<float>(g_model->knockRetardDeg()); }
float rf_knock_share() { return static_cast<float>(g_model->knockShare()); }
int rf_gears() { return g_model->gearing().gears; }
float rf_overall_ratio(int gear) {
    const Gearing& g = g_model->gearing();
    return gear >= 1 && gear <= g.gears ? static_cast<float>(g.overall[static_cast<std::size_t>(gear - 1)]) : 0.0f;
}
float rf_reverse_ratio() { return static_cast<float>(g_model->gearing().reverseOverall); }
float rf_box_ratio(int gear) {
    const Gearing& g = g_model->gearing();
    return gear >= 1 && gear <= g.gears ? static_cast<float>(g.box.boxRatio[static_cast<std::size_t>(gear - 1)]) : 0.0f;
}
int rf_box_driver(int gear) {
    const Gearing& g = g_model->gearing();
    return gear >= 1 && gear <= g.gears ? g.box.pairs[static_cast<std::size_t>(gear - 1)].driver : 0;
}
int rf_box_driven(int gear) {
    const Gearing& g = g_model->gearing();
    return gear >= 1 && gear <= g.gears ? g.box.pairs[static_cast<std::size_t>(gear - 1)].driven : 0;
}
float rf_box_contact(int gear) {
    const Gearing& g = g_model->gearing();
    if (gear < 1 || gear > g.gears) return 0.0f;
    const GearPair& pair = g.box.pairs[static_cast<std::size_t>(gear - 1)];
    return pair.profile + pair.overlap;
}
float rf_box_mesh_hz(int gear, float inputRpm) { return static_cast<float>(g_model->box().meshHz(gear, inputRpm)); }
float rf_box_final() { return static_cast<float>(g_model->box().finalDrive); }
int rf_box_final_pinion() { return g_model->box().finalPair.driver; }
int rf_box_final_ring() { return g_model->box().finalPair.driven; }
int rf_box_input_driver() { return g_model->box().inputPair.driver; }
int rf_box_input_driven() { return g_model->box().inputPair.driven; }
int rf_box_direct() { return g_model->box().directGear; }
int rf_box_kind() { return static_cast<int>(g_model->box().kind); }
float rf_box_limit() { return static_cast<float>(g_model->box().limitNm); }
float rf_box_module() { return static_cast<float>(g_model->box().moduleMm); }
float rf_box_helix() { return static_cast<float>(g_model->box().helixDeg); }
float rf_box_center() { return static_cast<float>(g_model->box().centerMm); }
float rf_box_inertia() { return static_cast<float>(g_model->box().inputInertia); }
float rf_box_efficiency() { return static_cast<float>(g_model->box().meshEfficiency); }
int rf_box_three_shafts() { return g_model->box().threeShafts ? 1 : 0; }
int rf_box_hypoid() { return g_model->box().hypoid ? 1 : 0; }
float rf_box_face() { return static_cast<float>(g_model->box().faceMm); }
int rf_box_dog_teeth() { return g_model->box().dogTeeth; }
float rf_box_cone_radius() { return static_cast<float>(g_model->box().coneRadiusM * 1000.0); }
/** Marche arrière, en dents : 0 le pignon, 1 l'intermédiaire, 2 la roue. */
int rf_box_reverse_teeth(int which) {
    const GearboxSpec& box = g_model->box();
    return which == 0 ? box.reverseIn.driver : which == 1 ? box.reverseIn.driven : box.reverseOut.driven;
}
float rf_wheel_radius() { return static_cast<float>(wheelRadiusM(g_powertrain.car)); }
float rf_zero_to_100() { return static_cast<float>(measurePerformance(g_powertrain).zeroTo100S); }
float rf_top_speed() { return static_cast<float>(measurePerformance(g_powertrain).topSpeedKmh); }

// ── La voiture vue de dessus et sa fiche acoustique, sans rien jouer ──

float rf_car_length() { return carNow().lengthM; }
float rf_car_width() { return carNow().widthM; }
float rf_car_height() { return carNow().heightM; }
float rf_seat_x() { return carNow().seat.x; }
float rf_seat_y() { return carNow().seat.y; }
float rf_seat_z() { return carNow().seat.z; }
float rf_engine_x() { return carNow().engine.x; }
float rf_engine_z() { return carNow().engine.z; }
float rf_source_x(int source) { return carNow().sources[static_cast<std::size_t>(source)].at.x; }
float rf_source_y(int source) { return carNow().sources[static_cast<std::size_t>(source)].at.y; }
float rf_source_z(int source) { return carNow().sources[static_cast<std::size_t>(source)].at.z; }
int rf_placement() { return static_cast<int>(placementOf(g_powertrain.car)); }


float rf_sheet_level(int item) { return soundSheet(g_powertrain).level(item); }
float rf_sheet_line_loss(float fromHz, float toHz) { return lineLossDb(g_powertrain, fromHz, toHz); }

// ── Les températures du moteur, pour les jauges : le réseau tourne toujours, il n'agit qu'après un départ à froid ──

/** Un moteur électrique n'a ni eau ni huile : l'air ambiant. */
static float thermalReading(double celsius) { return static_cast<float>(g_powertrain.electric ? static_cast<double>(g_powertrain.engine.ambientC) : celsius); }
float rf_coolant_c() { return thermalReading(g_model->thermal().coolantC()); }
float rf_oil_c() { return thermalReading(g_model->thermal().oilC()); }
float rf_metal_c() { return thermalReading(g_model->thermal().metalC()); }
/** Levée du calorstat (0 à 1), ventilateur en marche (0 ou 1). */
float rf_thermostat() { return g_powertrain.electric ? 0.0f : static_cast<float>(g_model->thermal().thermostat()); }
int rf_fan() { return !g_powertrain.electric && g_model->thermal().fan() ? 1 : 0; }
/** Viscosité de l'huile à sa température (mPa·s), loi de Vogel. */
float rf_oil_viscosity() { return g_powertrain.electric ? 0.0f : static_cast<float>(oilViscosityPaS(g_model->thermal().oilC()) * 1000.0); }
/** Ce que le moteur froid change en ce moment : frottements en plus (Nm), ralenti visé en plus (tr/min), carburant en plus (part). */
float rf_cold_friction_nm() { return static_cast<float>(g_model->coldFrictionNm()); }
float rf_cold_idle_rpm() { return static_cast<float>(g_model->coldIdleRpm()); }
float rf_cold_enrichment() { return static_cast<float>(g_model->coldEnrichment()); }
/** Les frottements du moteur chaud à ce régime (Nm) : pour lire les frottements en plus en pourcentage. */
float rf_warm_friction_nm(float rpm) { return static_cast<float>(frictionTorqueNm(g_powertrain, static_cast<double>(rpm))); }

// ── Le calculateur : ce qu'il fait en ce moment, et sa carte pour la fiche (ecu.hpp) ──

/** L'avance face à la carte d'origine (°, positif : plus tôt), et le λ demandé (0 : sans objet). */
float rf_ecu_advance() { return static_cast<float>(g_model->ecuAdvanceDeg()); }
float rf_ecu_lambda() { return static_cast<float>(g_model->ecuLambda()); }
/** Part des explosions qui ont lieu, de 0 à 1. */
float rf_ecu_fired() { return static_cast<float>(g_model->ecuFiredShare()); }
int rf_ecu_limiter() { return g_model->limiterActive() ? 1 : 0; }
int rf_ecu_launch() { return g_model->launchHolding() ? 1 : 0; }
int rf_ecu_flat() { return g_model->flatShifting() ? 1 : 0; }
/** Part du couple que retire l'antipatinage, de 0 à 1. */
float rf_ecu_traction() { return static_cast<float>(g_model->ecuAction().traction); }
/** La fiche : le couple de pleine charge face à la carte d'origine, l'avance demandée, le gaz à la sortie des cylindres (°C). */
float rf_ecu_full_share() { return g_powertrain.electric ? 1.0f : static_cast<float>(ecuFullLoadShare(g_powertrain.engine)); }
float rf_ecu_spark_deg(float rpm, float load) { return g_powertrain.electric ? 0.0f : static_cast<float>(demandedAdvanceDeg(g_powertrain.engine, rpm, load)); }
float rf_ecu_exhaust_c(float rpm, int stockMap) {
    return g_powertrain.electric ? 0.0f : static_cast<float>(fullLoadExhaustK(g_powertrain.engine, rpm, stockMap != 0) - 273.15);
}

// ── L'injection, pour la fiche : 0 là où elle n'a pas de sens (voir `injectionSummary`) ──

/**
 * 0 : refroidissement de la charge d'une injection directe (K) ; 1 : remplissage gagné (part) ; 2 : pression d'injection à
 * pleine charge (bar) ; 3 : couple gagné à 1500 tr/min par la rampe commune (part) ; 4 : claquement au ralenti face à la pompe
 * mécanique (dB) ; 5 : part de gaz recirculés la plus forte.
 */
float rf_injection_figure(int item) {
    if (g_powertrain.electric) return 0.0f;
    const InjectionSummary s = injectionSummary(g_powertrain.engine);
    const double figures[6] = {s.coolingK, s.fillGainShare, s.railBar, s.lowEndTorqueShare, s.knockDb, s.egrShare};
    return item >= 0 && item < 6 ? static_cast<float>(figures[item]) : 0.0f;
}
/** Contre-pression du filtre à particules à la puissance maximale (kPa) : `full` 0 propre, 1 plein. 0 sans filtre. */
float rf_dpf_kpa_at(int full) {
    if (g_powertrain.electric || !particulateFiltered(g_powertrain.engine)) return 0.0f;
    const ParticulateFilterSpec spec = particulateFilterSpecOf(g_powertrain.engine);
    const double rpm = powerSummary(g_powertrain).peakPowerRpm;
    const ExhaustState gas = exhaustStateOf(g_powertrain, rpm, 1.0, steadyBoost(g_powertrain, rpm));
    return static_cast<float>(filterBackPressurePa(spec, full != 0 ? spec.startGPerL * spec.volumeL : 0.0, gas.kgS, gas.kelvin) / 1000.0);
}

// ── Le filtre à particules et la recirculation, en marche ──

/** Le remplissage du filtre, de la fin de la dernière régénération (0) à la demande de la suivante (1). 0 sans filtre. */
float rf_dpf_load() { return g_model->filtered() ? static_cast<float>(g_model->particulateFilter().loadShare()) : 0.0f; }
float rf_dpf_kpa() { return g_model->filtered() ? static_cast<float>(g_model->particulateFilter().backPressurePa() / 1000.0) : 0.0f; }
int rf_dpf_regen() { return g_model->filtered() && g_model->particulateFilter().regenerating() ? 1 : 0; }
float rf_dpf_c() { return g_model->filtered() ? static_cast<float>(g_model->particulateFilter().temperatureK() - 273.15) : 0.0f; }
float rf_egr_share() { return static_cast<float>(g_model->egrShare()); }

// ── La came : sa fiche, calculée, et ce que fait la distribution variable en ce moment ──

/**
 * 0 à 3 : ouverture et fermeture de l'admission, puis de l'échappement (degrés du cycle de 720°, à 1 mm, profil de base) ;
 * 4 : levée (mm) ; 5 : croisement (°) ; 6 : fermeture de l'admission après le point mort bas (°) ; 7 : compression effective ;
 * 8 : régime de bascule (0 : sans) ; 9, 10, 11 : au ralenti, pression au collecteur (kPa), gaz résiduels, COV (parts) ;
 * 12 : durée siège à siège (°) ; 13 à 16 : ouvertures et fermetures du second profil. 0 sans soupapes.
 */
float rf_cam_spec(int which) {
    if (g_powertrain.electric || !camHasValves(g_powertrain.engine)) return 0.0f;
    const EngineConfig& e = g_powertrain.engine;
    const CamProfile cam = camProfileOf(e);
    const CamProfile high = highCamProfileOf(e);
    switch (which) {
        case 0: return static_cast<float>(cam.ivoDeg);
        case 1: return static_cast<float>(cam.ivcDeg);
        case 2: return static_cast<float>(cam.evoDeg);
        case 3: return static_cast<float>(cam.evcDeg);
        case 4: return static_cast<float>(cam.liftMm);
        case 5: return static_cast<float>(valveOverlapDeg(cam));
        case 6: return static_cast<float>(intakeClosingAbdcDeg(cam));
        case 7: return static_cast<float>(effectiveCompressionRatio(e.compressionRatio > 0.0f ? e.compressionRatio : g_powertrain.profile().starter.compressionRatio, intakeClosingAbdcDeg(cam)));
        case 8: return static_cast<float>(camSwitchRpm(e));
        case 9: return static_cast<float>(camIdle(e).manifoldKpa);
        case 10: return static_cast<float>(camIdle(e).residual);
        case 11: return static_cast<float>(camIdle(e).cov);
        case 12: return static_cast<float>(2.0 * cam.halfSpanDeg);
        case 13: return static_cast<float>(high.ivoDeg);
        case 14: return static_cast<float>(high.ivcDeg);
        case 15: return static_cast<float>(high.evoDeg);
        case 16: return static_cast<float>(high.evcDeg);
        default: return 0.0f;
    }
}
int rf_cam_high() { return g_model->camDrive().high ? 1 : 0; }
float rf_cam_phaser() { return g_model->camDrive().phaserDeg; }

/**
 * La distribution de ce réglage (voir « Les culbuteurs et l'affolement ») : 0 régime d'affolement (tr/min, 0 sans soupapes),
 * 1 masse ramenée à la soupape (g), 2 et 3 ressort posé et à pleine levée (N), 4 fréquence propre de la chaîne (Hz),
 * 5 rapport du culbuteur.
 */
float rf_valve_sheet(int item) {
    const EngineConfig& e = g_powertrain.engine;
    if (g_powertrain.electric || !hasValves(e)) return 0.0f;
    if (item == 0) return static_cast<float>(valveFloatRpm(e));
    const ValvetrainParts parts = valvetrainParts(e.valvetrain(), static_cast<double>(e.boreMm), static_cast<double>(e.strokeMm()));
    const ValveSpring spring = valveSpringOf(e);
    switch (item) {
        case 1: return static_cast<float>(parts.effectiveKg * 1000.0);
        case 2: return static_cast<float>(spring.preloadN);
        case 3: return static_cast<float>(spring.forceN(camLobeOf(e).liftMm));
        case 4: return static_cast<float>(parts.naturalHz);
        case 5: return static_cast<float>(parts.ratio);
        default: return 0.0f;
    }
}

}  // extern "C"
