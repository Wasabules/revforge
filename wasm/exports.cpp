// Interface C du moteur pour WebAssembly : un seul moteur, piloté depuis l'AudioWorklet.
#include <new>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/sound_sheet.hpp"
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
                   int superchargerKind, int antiLag, int turboPipeMm, int camshaft, int airFilter) {
    EngineConfig c;
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
    g_powertrain.engine = c;
    g_powertrain.electric = false;
    applyPowertrain();
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
        g_sound->setStarter(g_model->starterPowered(), g_model->starterMeshed(), static_cast<float>(g_model->starterLoad()));
        g_sound->setRoad(static_cast<float>(g_model->speedKmh() / 3.6), static_cast<float>(g_model->wheelSlip()));
        g_sound->setGearbox(g_model->box(), g_model->gearboxDrive());
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
float rf_turbo_rpm() { return static_cast<float>(g_model->turbo().state().shaftOmega * 60.0 / (2.0 * 3.141592653589793)); }
static float gaugeBar(double pascals) { return static_cast<float>((pascals - 101325.0) / 1e5); }
float rf_turbo_charge() { return gaugeBar(g_model->turbo().state().chargePa); }
float rf_turbo_plenum() { return gaugeBar(g_model->turbo().state().plenumPa); }
float rf_turbo_outlet() { return gaugeBar(g_model->turbo().state().outletPa); }
float rf_turbo_inlet() { return gaugeBar(g_model->turbo().state().inletPa); }
float rf_turbo_exhaust() { return gaugeBar(g_model->turbo().state().turbineInPa); }
float rf_turbo_wastegate() { return static_cast<float>(g_model->turbo().state().wastegate); }
float rf_turbo_blowoff() { return static_cast<float>(g_model->turbo().state().blowOff); }
float rf_turbo_flow() { return static_cast<float>(g_model->turbo().state().compressorFlow); }
float rf_turbo_out_c() { return static_cast<float>(g_model->turbo().state().compressorOutK - 273.15); }
float rf_turbo_cool_c() { return static_cast<float>(g_model->turbo().state().coolerOutK - 273.15); }
float rf_turbo_gas_c() { return static_cast<float>(g_model->turbo().state().turbineInK - 273.15); }
/** Ce que le moteur a choisi : diamètre de la roue (mm), inertie de l'arbre (kg·m²). */
float rf_intake_air_c(float rpm) { return static_cast<float>(intakeAirK(g_powertrain, static_cast<double>(rpm)) - 273.15); }
float rf_intake_drop_kpa(float rpm) { return static_cast<float>(intakeDropPa(g_powertrain, static_cast<double>(rpm)) / 1000.0); }
float rf_turbo_wheel_mm() { return static_cast<float>(turboSpecOf(g_powertrain).wheelDiameterM * 1000.0); }
float rf_turbo_inertia() { return static_cast<float>(turboSpecOf(g_powertrain).inertia); }
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
    return line->compute(profile.exhaust, profile.exhaustScale, static_cast<double>(g_powertrain.engine.displacementCc) * 1e-6, profile.cylinders).data();
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

}  // extern "C"
