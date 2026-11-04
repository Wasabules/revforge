#include "revforge/driving_model.hpp"

#include "revforge/architecture.hpp"
#include "revforge/two_stroke.hpp"
#include "revforge/tyre.hpp"
#include "revforge/valve_float.hpp"

#include "portable_math.hpp"
#include "turbo_laws.hpp"
#include "revforge/atmosphere.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kStep = 0.001;
constexpr double kGravity = 9.81;
// Le pneu, `kTyreGrip` compris : tyre.hpp.
constexpr double kWheelInertia = 1.6;  // deux roues et le pont
constexpr double kPedalUpPerS = 6.0;
constexpr double kPedalDownPerS = 12.0;
constexpr double kBrakeRatePerS = 3.6;
// La route : la pente change progressivement, et un virage coûte une part de la force latérale (dérive des pneus).
constexpr double kGradePerSecond = 0.02;
constexpr double kCorneringDragShare = 0.05;
constexpr double kAirDensity = 1.2;
constexpr double kTrailerRolling = 0.012;
constexpr double kTrailerDragArea = 1.2;
// Le pied est au plancher depuis ce temps, et rien ne vient : la voiture n'en a pas la force.
constexpr double kStrainS = 2.0;
// Une étape où le conducteur appuie au moins à ce point est une étape où il veut sa vitesse ; la pédale est à son plafond à ce jeu près.
constexpr double kEagerPedal = 0.7;
constexpr double kCeilingSlack = 0.02;
constexpr double kNeutralBelowKmh = 8.0;
constexpr double kLimiterDropShare = 0.04;
constexpr double kLimiterMinCutS = 0.03;
constexpr double kStopRpm = 60.0;
constexpr double kPullInSeconds = 0.02;
constexpr double kReleaseAfterCatchS = 0.3;
constexpr double kMeshHoldS = 0.25;
constexpr double kStarterEfficiency = 0.85;
constexpr double kColdFriction = 1.5;
// Sous ce régime, en prise, la combustion ne suit plus : le moteur cale.
constexpr double kStallShare = 0.68;
// Le cône a fini sous cet écart : le baladeur peut passer.
constexpr double kSyncToleranceRad = 3.0;
// Sans débrayer, le cône abandonne vite : il ne peut pas freiner tout le moteur. Le baladeur force alors sur les dents.
constexpr double kBlockedPatienceS = 0.09;
// Le conducteur insiste, puis renonce.
constexpr double kGrindGiveUpS = 0.8;
// Sous charge, un rapport ne sort pas : les dents sont plaquées (part du couple admissible).
constexpr double kReleaseShare = 0.35;
constexpr double kReleaseGiveUpS = 0.45;
// Les crabots tombent tant que l'écart leur laisse le temps de trouver leur place (rad/s à l'arbre primaire).
constexpr double kDogWindowRad = 170.0;
// Une boîte en surcharge s'use d'autant plus vite que le couple dépasse ce qu'elle admet (règle de Miner).
constexpr double kWearRefS = 30.0;
// Les arbres de roue et le pneu, en série (Nm/rad et Nm·s/rad, vus à la roue).
constexpr double kShaftStiffness = 6000.0;
constexpr double kShaftDamping = 60.0;
// L'anti-lag : pied vraiment levé, et moteur assez lancé. Plus bas, le ralenti reprend la main (ordre de grandeur).
constexpr double kAntiLagLiftedPedal = 0.03;
constexpr double kAntiLagMinShare = 0.27;
constexpr double kAntiLagHoldShare = 0.95;
// Le capteur de cliquetis : chaque cliquetis détecté retire 1,5° à son cylindre, l'avance revient de 1° par seconde quand il se tait,
// sans jamais retirer plus de 20° (ordres de grandeur des calculateurs de série, supposés).
constexpr double kKnockRetardPerEventDeg = 1.5;
constexpr double kKnockRecoverDegPerS = 1.0;
constexpr double kKnockMaxRetardDeg = 20.0;
// Le calculateur (ecu.hpp). Launch control : pied au plancher, voiture arrêtée, moteur désaccouplé ; il s'arrête quand
// l'embrayage a rattrapé le moteur. La boîte pilotée tient l'embrayage ouvert tant qu'on garde le pied sur le frein.
constexpr double kLaunchPedal = 0.9;
constexpr double kLaunchArmMs = 1.0;
constexpr double kLaunchCaughtShare = 0.97;
constexpr double kLaunchBrake = 0.05;
// Le robot du départ ramène le moteur à son régime de départ en ce temps, en serrant un peu plus que son couple ; à 150 tr/min
// près, les disques tournent ensemble, et il serre (supposés : l'ordre de grandeur d'une boîte pilotée).
constexpr double kLaunchSettleS = 0.3;
constexpr double kLaunchSyncRpm = 150.0;
// Passage à fond : pied au-delà de 80 % à la montée.
constexpr double kFlatPedal = 0.8;
// Antipatinage : une régulation proportionnelle et intégrale du glissement (m/s) vers la part du couple retirée, ses gains
// placés sur la transmission du rapport engagé (`tractionGains`) : 6 rad/s (1 Hz), sous la torsion des arbres de roue (2 à
// 3 Hz en première), amortie à 0,7 (supposé, voir docs/modele.md).
constexpr double kTractionOmega = 6.0;
constexpr double kTractionDamping = 0.7;
// Le gaz à la sortie des cylindres : le modèle de la turbine (turbo.cpp), à l'air de référence.
constexpr double kPortIntakeK = 293.0;
constexpr double kPortHeatS = 0.35;
constexpr double kPortCoolS = 0.9;
// L'air dans le conduit d'admission au régime de la puissance maximale (m/s, ordre de grandeur), pour le film de carburant.
constexpr double kPortSpeedAtRatedMs = 70.0;

double approach(double from, double to, double maxStep) {
    const double delta = to - from;
    return std::fabs(delta) <= maxStep ? to : from + maxStep * (delta > 0 ? 1 : -1);
}

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

double toOmega(double rpm) { return rpm * 2.0 * kPi / 60.0; }
double toRpm(double omega) { return omega * 60.0 / (2.0 * kPi); }

}  // namespace

DrivingModel::DrivingModel() : DrivingModel(referencePowertrain()) {}

DrivingModel::DrivingModel(const Powertrain& powertrain) {
    setPowertrain(powertrain);
    omega_ = powertrain.electric ? 0.0 : toOmega(idle_);
    inputOmega_ = omega_;
    // Le modèle part moteur tournant, au ralenti. Moteur froid, il part aussi avec ce qu'un ralenti établi a dans son collecteur :
    // juste l'air qui équilibre ses frottements, huile froide comprise (la régulation de ralenti, `updateEngine`). Partant d'un
    // collecteur vide, le film d'une injection ou d'un carburateur se posait sur un débit nul ; la paroi froide buvait ensuite
    // la moitié de l'essence le temps de se mouiller, et le moteur calait en moins d'une seconde : tous les moteurs à injection
    // indirecte par −20 °C, la Cadillac dès 0 °C (docs/modele.md, « Le démarrage à froid »). Le démarreur, lui, passe par
    // `crank`, qui pose le film au moment où le moteur prend. Parti chaud, le collecteur part vide, comme à l'origine, au bit près.
    if (coldCoupled_) {
        const double friction = frictionTorqueNm(powertrain_, idle_) + hydroNmPerRpm_ * idle_ * viscosityExcess_;
        manifold_ = clamp(friction / std::max(fullLoadTorqueNm(powertrain_, idle_, 0.0) + frictionTorqueNm(powertrain_, idle_), 1.0), 0.0, 1.0);
    }
}

void DrivingModel::setPowertrain(const Powertrain& powertrain) {
    const bool sameBox = powertrain.gearbox == powertrain_.gearbox;
    // Un autre moteur ou une autre voiture : le trajet repart de zéro. Les vitres ou l'isolation ne changent pas ce qu'on brûle.
    const bool sameEngine = powertrain.electric == powertrain_.electric && powertrain.engine == powertrain_.engine;
    const bool sameConsumer = sameBox && powertrain.electric == powertrain_.electric && powertrain.engine == powertrain_.engine &&
                              powertrain.car.body == powertrain_.car.body && powertrain.car.massKg == powertrain_.car.massKg &&
                              powertrain.car.rimInch == powertrain_.car.rimInch && powertrain.car.drive == powertrain_.car.drive;
    powertrain_ = powertrain;
    consumption_.configure(powertrain);
    if (!sameConsumer) consumption_.resetTrip();
    gearing_ = gearingOf(powertrain);
    efficiency_ = gearing_.box.meshEfficiency;
    if (!sameBox) repairGearbox();
    radius_ = wheelRadiusM(powertrain.car);
    idle_ = idleRpmOf(powertrain);
    redline_ = redlineRpmOf(powertrain);
    inertia_ = engineInertia(powertrain);
    // L'embrayage, le convertisseur et la carte de passage sont des pièces et un étalonnage, faits dans l'air de référence :
    // monter en altitude ne les change pas. Dans l'air de référence, `sized` est le montage lui-même.
    const Powertrain sized = withReferenceAir(powertrain);
    const PowerSummary peak = powerSummary(sized);
    // Le radiateur aussi est une pièce, dimensionnée dans l'air de référence ; le moteur garde, lui, l'air du jour.
    configureThermal(powertrain, sized, peak.peakPowerRpm, peak.peakPowerKw);
    peakTorqueRpm_ = peak.peakTorqueRpm;
    shiftMap_ = buildShiftMap(sized, gearing_);
    // L'embrayage est une pièce : un autre échangeur, l'eau ou le protoxyde ne le changent pas, ni l'air du jour.
    clutchCapacity_ = 1.6 * (chargeAirActive(powertrain) ? powerSummary(chargeAirStock(sized)).peakTorqueNm : peak.peakTorqueNm);
    // Le robot change d'autant plus vite que la voiture est sportive ; sur une boîte à crabots, c'est le barillet qui décide.
    shiftPace_ = bodySpec(powertrain.car.body).shiftSeconds;
    const double litres = static_cast<double>(powertrain.engine.displacementCc) / 1000.0;
    // Le convertisseur est dimensionné pour caler vers 45 % du régime maximal à pleine charge.
    const double stallOmega = toOmega(0.4 * redline_);
    converterK_ = stallOmega / std::sqrt(std::max(fullLoadTorqueNm(sized, 0.4 * redline_, 1.0), 1.0));
    // Couple de chaque explosion : gaz et compression, un cylindre à la fois, une fois les autres retranchés.
    const double firings = static_cast<double>(powertrain.engine.firingsPerTwoTurns());
    const double overlap = firings <= 4.0 ? 1.0 : (firings <= 5.0 ? 0.8 : (firings <= 6.0 ? 0.55 : (firings <= 8.0 ? 0.35 : (firings <= 10.0 ? 0.25 : 0.15))));
    const double fuelScale = powertrain.engine.burnsDiesel() ? 2.0 : 1.0;
    // Un rotatif n'a pas de masses alternatives, et son couple s'étale sur 270° de chaque tour : moitié moins d'à-coups (supposé).
    const double perCylinder = powertrain.engine.sweptCcPerTwoTurns() / 1000.0 / firings * 4.0 * overlap * fuelScale * (isRotary(powertrain.engine) ? 0.5 : 1.0);
    rippleIdleNm_ = 45.0 * perCylinder;
    rippleNm_ = 160.0 * perCylinder;
    // Un collecteur plus gros met plus de temps à se remplir.
    manifoldTau_ = 0.06 + 0.02 * litres;
    starter_ = powertrain.profile().starter;
    turboEngine_ = !powertrain.electric && powertrain.engine.aspiration == Aspiration::Turbo;
    if (turboEngine_) turbo_.setSpec(turboSpecOf(powertrain));
    // La régulation de pression : rien à calculer sur une wastegate à ressort, qui reste dans `TurboCharger`.
    const bool controlled = turboEngine_ && powertrain.engine.boostControl != BoostControl::Spring;
    if (controlled && (!boostControlled_ || boostSpec_.kind != powertrain.engine.boostControl)) boostController_.reset();
    // Un étalonnage, comme la carte de passage : fait dans l'air de référence. La cible absolue, elle, se compare à l'air du jour.
    boostSpec_ = turboEngine_ ? boostControlSpecFor(sized, gearing_, kTyreGrip) : BoostControlSpec{};
    boostControlled_ = controlled;
    // Deux turbos, un twincharger ou des aubes : un autre modèle tient les arbres (twin_turbo.cpp).
    twinEngine_ = turboEngine_ && usesTwinTurbo(powertrain.engine);
    if (twinEngine_) twin_.setSpec(twinTurboSpecOf(powertrain));
    // Un diesel n'a ni papillon à entrouvrir, ni étincelle à retarder : voir docs/modele.md. L'anti-lag n'est modélisé que sur un turbo simple.
    antiLagEngine_ = turboEngine_ && !twinEngine_ && powertrain.engine.fuel == Fuel::Petrol && powertrain.engine.antiLag != AntiLag::Off;
    antiLag_.kind = antiLagEngine_ ? powertrain.engine.antiLag : AntiLag::Off;
    antiLag_.active = false;
    antiLagLostNm_ = 0.0;
    superchargerEngine_ = !powertrain.electric && powertrain.engine.aspiration == Aspiration::Supercharger;
    if (superchargerEngine_) supercharger_.setSpec(superchargerSpecOf(powertrain));
    carbEngine_ = !powertrain.electric && hasCarburetor(powertrain.engine);
    twoStrokeEngine_ = !powertrain.electric && isTwoStroke(powertrain.engine);
    if (carbEngine_) carb_.configure(carburetorSpecOf(powertrain.engine), powertrain.engine.sweptCcPerTwoTurns() * 1e-6);
    // Le cliquetis : seulement quand la combustion n'est plus celle d'origine. Sinon la carte d'origine reste sous sa limite, et rien ne tourne.
    const KnockMap* knock = knockMapOf(powertrain);
    knockEngine_ = knock != nullptr;
    if (knockEngine_) {
        knockMap_ = *knock;
        // Chaque cylindre a son capteur, en pratique son propre retard : ses combustions par seconde, à chaque tour par minute.
        const EngineConfig& e = powertrain.engine;
        knockPerCylinder_ = static_cast<double>(e.firingsPerTwoTurns()) / static_cast<double>(isRotary(e) ? e.rotors() : e.cylinders) / 120.0;
    } else {
        knockRetard_ = 0.0;
        knockAdvance_ = 0.0;
        knockLimit_ = 0.0;
        knockDrive_ = KnockDrive{};
    }
    // L'injection : chaque partie ne tourne que si elle est montée. Le filtre garde sa suie tant que le moteur ne change pas.
    portEngine_ = !powertrain.electric && fuelSystemOf(powertrain.engine) == FuelSystem::Injection && !powertrain.engine.burnsDiesel();
    portFilm_.prime();
    directEngine_ = !powertrain.electric && directInjected(powertrain.engine);
    egrEngine_ = !powertrain.electric && recirculated(powertrain.engine);
    egrShare_ = 0.0;
    const bool filterBefore = dpfEngine_;
    const ParticulateFilterSpec filterSpec = particulateFilterSpecOf(powertrain.engine);
    dpfEngine_ = !powertrain.electric && particulateFiltered(powertrain.engine);
    if (dpfEngine_ && (!filterBefore || dpf_.spec().volumeL != filterSpec.volumeL || dpf_.spec().diesel != filterSpec.diesel)) dpf_.configure(filterSpec);
    // L'air de charge : un autre moteur remplit la bouteille et ramène la boucle d'eau à l'air ambiant.
    chargeAirEngine_ = chargeAirActive(powertrain);
    if (chargeAirEngine_) chargeAir_.configure(chargeAirSetupOf(powertrain), !sameEngine);
    chargeFill_ = 0.0;
    if (gear_ > gearing_.gears) gear_ = gearing_.gears;
    step_ = ShiftStep::None;
    ignitionCut_ = false;
    torqueScale_ = 1.0;
    // Le calculateur : sa carte, et ce qu'il fait à chaque pas. Au défaut, rien de cela ne s'exécute.
    {
        const EngineConfig& e = powertrain.engine;
        // Le régime du rupteur : le régime maximal, sauf si les soupapes s'affolent avant (culbuteurs). Le calculateur d'origine est
        // étalonné sur la came d'origine de cette distribution et de ces ressorts : il ne connaît pas une came changée.
        EngineConfig stockCam = e;
        stockCam.camDurationDeg = 0;
        stockCam.camLiftMm = 0.0f;
        stockCam.lobeSeparationDeg = 0;
        stockCam.valveTiming = ValveTiming::Fixed;
        limiterRpm_ = limiterRpmFor(redline_, powertrain.electric ? 0.0 : valveFloatRpm(stockCam));
        ecuMap_ = !powertrain.electric && ecuMapTuned(e);
        mapRetard_ = ecuMap_ ? mapRetardDeg(e) : 0.0;
        lambdaRatio_ = ecuMap_ ? lambdaFullLoadRatio(e) : 1.0;
        sparkEngine_ = !powertrain.electric && !e.burnsDiesel();
        // Un diesel n'a pas d'étincelle à couper : sa coupure redevient celle de l'injection, comme à l'origine.
        limiterKind_ = powertrain.electric || (e.revLimiter == RevLimiter::IgnitionCut && !sparkEngine_) ? RevLimiter::Stock : e.revLimiter;
        launchRpm_ = !powertrain.electric && e.launchRpm > 0 ? std::min(static_cast<double>(e.launchRpm), 0.95 * redline_) : 0.0;
        flatShift_ = !powertrain.electric && e.flatShift;
        tractionTarget_ = tractionSlipTarget(e.tractionControl);
        // Les gains de l'antipatinage se placent sur le couple maximal : une calibration, faite dans l'air de référence.
        tractionTorqueNm_ = peak.peakTorqueNm;
        ecuEngine_ = limiterKind_ != RevLimiter::Stock || launchRpm_ > 0.0 || flatShift_ || tractionTarget_ > 0.0;
        ecu_ = EcuAction{};
        ecuShare_ = 1.0;
        fuelOnCut_ = false;
        launchHold_ = false;
        flatUpshift_ = false;
        flatCut_ = false;
        tractionIntegral_ = 0.0;
        unburntJoules_ = 0.0;
        burnt_.fuelShare = 1.0;
    }
    if (powertrain.electric) {
        gear_ = 1;
        pendingGear_ = 1;
        clutch_ = 1.0;
        locked_ = true;
        meshed_ = true;
        cut_ = false;
    } else if (gear_ != 0 && meshed_) {
        wheelOmega_ = speedMs_ / radius_;
        if (locked_) omega_ = std::max(wheelOmega_ * ratio(gear_), toOmega(idle_));
        inputOmega_ = wheelOmega_ * ratio(gear_);
        locked_ = false;
        twist_ = 0.0;
    } else if (gear_ == 0) {
        meshed_ = false;
    }
    omega_ = std::min(omega_, toOmega(redline_));
    if (!turboEngine_ && !superchargerEngine_) boost_ = 0.0;
    // La distribution variable : rien de cela ne tourne sur une came fixe.
    camTwoStep_ = !powertrain.electric && camTwoStep(powertrain.engine);
    camPhased_ = !powertrain.electric && camPhased(powertrain.engine);
    camVariable_ = camTwoStep_ || camPhased_;
    // Au-delà de l'affolement des soupapes (culbuteurs), le second profil ne servirait à rien : pas de bascule.
    camSwitchRpm_ = camTwoStep_ ? revforge::camSwitchRpm(powertrain.engine, valveFloatRpm(powertrain.engine)) : 0.0;
    camDrive_ = CamDrive{};
    if (camPhased_) camDrive_.phaserDeg = static_cast<float>(-kCamPhaserAuthorityDeg);
}

double DrivingModel::rpm() const {
    const double value = toRpm(omega_);
    // Le moteur électrique siffle encore à l'arrêt : l'onduleur reste sous tension.
    return powertrain_.electric ? std::max(value, idle_) : value;
}

double DrivingModel::powerKw() const { return torque_ * omega_ / 1000.0; }

double DrivingModel::wheelRpm() const { return toRpm(wheelOmega_); }

double DrivingModel::inputRpm() const { return toRpm(powertrain_.electric ? omega_ : inputOmega_); }

double DrivingModel::outputRpm() const { return toRpm(wheelOmega_ * gearing_.box.finalDrive); }

double DrivingModel::ratio(int gear) const {
    if (gear < 0) return gearing_.reverseOverall;
    const int index = clamp(gear, 1, gearing_.gears) - 1;
    return gearing_.overall[static_cast<std::size_t>(index)];
}

double DrivingModel::rpmInGear(int gear) const { return toRpm(speedMs_ / radius_ * ratio(gear)); }

void DrivingModel::setManual(bool manual) {
    manual_ = manual;
}

void DrivingModel::requestShift(int direction) {
    if (powertrain_.electric || broken_) return;
    // Le levier bouge encore : la demande attend la fin du passage, au lieu de se perdre.
    if (step_ != ShiftStep::None) {
        queuedShift_ = direction;
        return;
    }
    int next = clamp(gear_ + direction, -1, gearing_.gears);
    if (next == gear_) return;
    // Aux palettes, l'automatique garde la main sur le point mort et ne recule pas.
    if (!manual_ && shiftMode_ == ShiftMode::Paddles && next < 1) return;
    // Du point mort, en roulant, le levier va droit au rapport qui convient à cette vitesse.
    if (gear_ == 0 && direction > 0) {
        while (next < gearing_.gears && rpmInGear(next) > redline_ * 0.8) ++next;
    }
    if (next > 0 && rpmInGear(next) > redline_ * 0.98) return;
    // La marche arrière ne s'engage qu'à l'arrêt : assisté, le levier refuse ; au pied, elle craque (pignon droit, sans synchro).
    if (next < 0 && speedMs_ > 0.7 && assisted()) return;
    beginShift(next);
}

void DrivingModel::repairGearbox() {
    broken_ = false;
    wear_ = 0.0;
    overload_ = 0.0;
}

void DrivingModel::crank() {
    if (powertrain_.electric) return;
    // Jamais deux démarrages de la même durée.
    crankLeft_ = kPullInSeconds + 0.7 + 0.25 * std::fmod(clock_ * 7.31 + 0.4, 1.0);
    pullInLeft_ = kPullInSeconds;
    overrunLeft_ = 0.0;
    crankAngle_ = std::fmod(clock_ * 3.7, 4.0 * kPi);
    stalled_ = false;
    catching_ = true;
    fastIdle_ = 0.45;
    omega_ = 0.0;
    gear_ = 0;
    clutch_ = 0.0;
    locked_ = false;
    meshed_ = false;
    step_ = ShiftStep::None;
    ignitionCut_ = false;
    torqueScale_ = 1.0;
}

void DrivingModel::halt() {
    omega_ = 0.0;
    inputOmega_ = 0.0;
    crankLeft_ = 0.0;
    pullInLeft_ = 0.0;
    overrunLeft_ = 0.0;
    meshHoldLeft_ = 0.0;
    stalled_ = false;
    catching_ = false;
}

void DrivingModel::step(double dt) {
    clock_ += dt;
    cycleSec_ = scenarioCycle(scenarioOf(scenario_), clock_ - scenarioStart_);
    advance(dt, Driver::Pilot, 0.0, 0.0);
}

void DrivingModel::stepPedal(double dt, double pedal, double brakePedal) {
    clock_ += dt;
    advance(dt, Driver::Pedal, clamp(pedal, 0.0, 1.0), clamp(brakePedal, 0.0, 1.0));
}

void DrivingModel::stepNeutral(double dt, double pedal) {
    clock_ += dt;
    advance(dt, Driver::Neutral, clamp(pedal, 0.0, 1.0), 0.0);
}

void DrivingModel::stepOff(double dt) {
    clock_ += dt;
    advance(dt, Driver::Off, 0.0, 0.0);
}

void DrivingModel::advance(double dt, Driver driver, double pedal, double brakePedal) {
    pending_ += dt;
    while (pending_ >= kStep) {
        pending_ -= kStep;
        substep(driver, pedal, brakePedal);
    }
}

void DrivingModel::pilot(double& pedal, double& brakePedal) {
    // Le trajet : des données (driver_scenarios.cpp). Le modèle ne fait que lui donner ce que le conducteur voit.
    const Scenario& scenario = scenarioOf(scenario_);
    legIndex_ = scenarioLegAt(scenario, cycleSec_);
    const ScenarioLeg& leg = scenario.legs[legIndex_];
    gradeTarget_ = leg.gradePct / 100.0;
    cornerRadius_ = leg.cornerRadiusM;
    trailerKg_ = scenario.trailerKg;

    DriverView view;
    view.kmh = speedKmh();
    view.cycleSec = cycleSec_;
    view.lifting = shiftLiftsPedal();
    const double wheelNm = fullLoadTorqueNm(powertrain_, rpm(), boost_) * ratio(gear_ == 0 ? 1 : gear_) * efficiency_;
    view.holdPedal = (roadLoadN(powertrain_, speedMs_) + roadExtraN()) * radius_ / std::max(wheelNm, 1.0);
    // Sur une pente, tenir la vitesse peut demander plus que la pédale n'en donne.
    if (scenario_ != DriveScenario::City) view.holdPedal = std::min(view.holdPedal, 1.0);
    const DriverOrder order = followLeg(scenario, legIndex_, view);
    pedal = order.pedal;
    brakePedal = order.brake;
    legTarget_ = order.targetKmh;

    const bool stuck = throttle_ > 0.9 && order.targetKmh < kFlatOutKmh && order.targetKmh - view.kmh > 5.0 && acceleration_ < 0.1;
    // Ou bien le conducteur, pressé, appuie autant que l'étape le lui permet, et à ce train la voiture n'atteindra pas la vitesse de
    // l'étape avant qu'elle finisse : elle accélère encore, mais n'en a pas la force. Un conducteur qui ne veut pas aller vite ne dit rien.
    const ScenarioLeg& now = scenario.legs[legIndex_];
    const double legKmh = legTargetKmh(scenario, now);
    const double ceiling = std::max(now.eagerness, view.holdPedal);
    const double legLeft = (legIndex_ + 1 < scenario.legCount ? scenario.legs[legIndex_ + 1].fromSec : scenario.loopSec) - cycleSec_;
    const bool pressing = ceiling >= kEagerPedal && throttle_ >= std::min(ceiling, 1.0) - kCeilingSlack;
    const bool late = legKmh < kFlatOutKmh && legKmh - view.kmh > 5.0 && acceleration_ * legLeft < (legKmh - view.kmh) / 3.6;
    const bool flatOut = stuck || (pressing && late);
    strain_ = flatOut ? std::min(strain_ + kStep, kStrainS * 2.0) : std::max(strain_ - 2.0 * kStep, 0.0);
    underpowered_ = strain_ > kStrainS;
}

void DrivingModel::substep(Driver driver, double pedal, double brakePedal) {
    sinceShift_ += kStep;
    piloted_ = driver == Driver::Pilot;
    if (piloted_) pilot(pedal, brakePedal);
    grade_ = approach(grade_, gradeTarget_, kGradePerSecond * kStep);
    updateDemand();

    const DriverStyle& style = scenarioOf(scenario_).style;
    const double pedalUp = piloted_ ? style.pedalUpPerS : kPedalUpPerS;
    const double pedalDown = piloted_ ? style.pedalDownPerS : kPedalDownPerS;

    if (driver == Driver::Neutral || driver == Driver::Off) {
        if (!powertrain_.electric) {
            gear_ = 0;
            pendingGear_ = 0;
            step_ = ShiftStep::None;
            meshed_ = false;
            ignitionCut_ = false;
            torqueScale_ = 1.0;
            queuedShift_ = 0;
        }
        brakePedal = speedMs_ > 0.0 ? 0.4 : 0.0;
    } else if (!powertrain_.electric) {
        selectGear(pedal > 0.02);
        // Le passage à fond garde le pied au plancher : c'est le calculateur qui coupe.
        if (shiftLiftsPedal() && !flatUpshift_) pedal = 0.0;
    }
    updateClutch(pedal > 0.02);

    // Rev-match : au rétrogradage, un coup de gaz cale le moteur sur le nouveau rapport.
    if (step_ != ShiftStep::None && downshifting_ && assisted() && gear_ > 0 && toRpm(omega_) < rpmInGear(gear_) - 80.0) pedal = 0.75;

    const double wanted = pedal;
    throttle_ = approach(throttle_, wanted, (wanted > throttle_ ? pedalUp : pedalDown) * kStep);
    brake_ = approach(brake_, brakePedal, kBrakeRatePerS * kStep);
    updateEngine(throttle_, driver == Driver::Off);
    integrate(brake_);
    meterConsumption();
    if (thermalEngine_) stepThermal();
}

void DrivingModel::meterConsumption() {
    burnt_.rpm = toRpm(omega_);
    burnt_.omega = omega_;
    burnt_.speedMs = speedMs_;
    consumption_.step(burnt_, kStep);
}

void DrivingModel::configureThermal(const Powertrain& p, const Powertrain& sized, double peakPowerRpm, double peakPowerKw) {
    thermalEngine_ = !p.electric;
    // Intégration : l'eau que lit le film venait du réseau ; repassé au départ chaud, le moteur retrouve l'eau de référence.
    if (coldCoupled_) coolantC_ = kWarmCoolantC;
    coldCoupled_ = false;
    if (!thermalEngine_) {
        thermalReady_ = false;
        return;
    }
    // L'énergie du carburant à la puissance maximale, par le même bilan que le compteur : l'échelle de ce qui part aux parois.
    const double omega = toOmega(peakPowerRpm);
    const double brake = omega > 0.0 ? peakPowerKw * 1000.0 / omega : 0.0;
    const double friction = frictionTorqueNm(sized, peakPowerRpm);
    const double fullGross = fullLoadTorqueNm(sized, peakPowerRpm, steadyBoost(sized, peakPowerRpm)) + friction;
    const ConsumptionModel& model = consumption_.model();
    ThermalSpec spec = thermalSpecOf(sized, model.fuelGps(peakPowerRpm, brake, friction, fullGross) * model.fuel().lowerHeatingJPerG);
    // Le radiateur est une pièce, dimensionnée dans l'air de référence ; l'air qui le traverse est celui du jour.
    spec.airPressureRatio = ambientAirOf(p.engine).pressureRatio;
    thermal_.configure(spec);
    hydroNmPerRpm_ = hydrodynamicNmPerRpm(p.engine);
    // Un autre départ ou un autre air : le moteur repart de là. Sinon, il garde ses températures, même si on change de ligne.
    if (!thermalReady_ || p.engine.startTemp != startTemp_ || p.engine.ambientC != ambientC_) {
        thermal_.reset(p.engine.startTemp, static_cast<double>(p.engine.ambientC));
        heatFuelW_ = 0.0;
        heatFrictionW_ = 0.0;
        heatSteps_ = 0;
    }
    thermalReady_ = true;
    startTemp_ = p.engine.startTemp;
    ambientC_ = p.engine.ambientC;
    // Parti chaud, le moteur reste celui de référence : le réseau ne sert qu'aux jauges.
    coldCoupled_ = p.engine.startTemp == StartTemp::Cold;
    burnt_.enrichment = 0.0;
    coldFrictionNm_ = 0.0;
    updateColdEffects();
}

void DrivingModel::updateColdEffects() {
    if (!coldCoupled_) return;
    viscosityExcess_ = revforge::coldViscosityExcess(thermal_.oilC());
    // L'eau du réseau, que lit le film de carburant d'une injection indirecte (injection) : seulement après un départ à froid.
    coolantC_ = thermal_.coolantC();
    coldIdleRise_ = revforge::coldIdleRise(thermal_.coolantC());
    coldEnrichment_ = revforge::coldEnrichment(powertrain_.engine, thermal_.coolantC());
}

void DrivingModel::stepThermal() {
    // Le réseau avance par pas de 10 ms : ses constantes de temps vont de la seconde à l'heure.
    constexpr int kThermalSteps = 10;
    heatFrictionW_ += std::max(frictionHeatNm_, 0.0) * omega_;
    if (burnt_.burning) {
        const ConsumptionModel& model = consumption_.model();
        heatFuelW_ += model.fuelGps(burnt_.rpm, burnt_.brakeNm, burnt_.frictionNm, burnt_.fullGrossNm) * model.fuel().lowerHeatingJPerG;
    }
    if (++heatSteps_ < kThermalSteps) return;
    ThermalInput in;
    in.fuelW = heatFuelW_ / kThermalSteps;
    in.frictionW = heatFrictionW_ / kThermalSteps;
    in.rpm = toRpm(omega_);
    in.speedMs = speedMs_;
    in.ambientC = static_cast<double>(ambientC_);
    thermal_.step(in, kThermalSteps * kStep);
    heatFuelW_ = 0.0;
    heatFrictionW_ = 0.0;
    heatSteps_ = 0;
    updateColdEffects();
}

void DrivingModel::selectGear(bool wantsToMove) {
    if (broken_) return;
    if (step_ != ShiftStep::None) {
        advanceShift();
        return;
    }
    if (queuedShift_ != 0) {
        const int direction = queuedShift_;
        queuedShift_ = 0;
        requestShift(direction);
        return;
    }
    if (!manual_) autoGear(wantsToMove);
}

void DrivingModel::autoGear(bool wantsToMove) {
    // Le robot ne recule pas de lui-même : il sort la marche arrière dès que la voiture le permet.
    if (gear_ < 0) {
        if (speedKmh() < kNeutralBelowKmh) {
            gear_ = 0;
            pendingGear_ = 0;
            meshed_ = false;
            locked_ = false;
        }
        return;
    }
    const double rpmNow = toRpm(omega_);
    const double downshiftRpm = downshiftFloor(shiftMap_, ShiftMode::Normal);
    if (gear_ == 0 && wantsToMove) {
        beginShift(1);
    } else if (gear_ > 0 && !wantsToMove && speedKmh() < kNeutralBelowKmh) {
        // Le robot débraye pour laisser le moteur tourner, et sort le rapport : plus rien ne pousse les dents.
        gear_ = 0;
        pendingGear_ = 0;
        meshed_ = false;
        locked_ = false;
    } else if (activeMode() != ShiftMode::Normal || grade_ != 0.0 || gradeTarget_ != 0.0 || trailerKg_ > 0.0) {
        // Les autres modes, et le mode normal dès que la route n'est plus plate : il faut alors regarder ce que la route demande.
        chooseGear();
    } else if (gear_ >= 1 && gear_ < gearing_.gears && throttle_ > 0.05 && rpmNow > upshiftRpm() && settled()) {
        // Pied levé, on ne monte pas un rapport : on se prépare à ralentir.
        beginShift(gear_ + 1);
    } else if (gear_ > 1 && locked_ && rpmNow < downshiftRpm) {
        beginShift(gear_ - 1);
    } else if (gear_ > 1 && throttle_ > 0.85 && rpmNow < redline_ * 0.42 && rpmInGear(gear_ - 1) < redline_ * 0.8 && settled()) {
        // Rétrogradage franc pour dépasser, s'il ne fait pas sur-régimer.
        beginShift(gear_ - 1);
    }
}

void DrivingModel::chooseGear() {
    if (gear_ < 1) return;
    const bool converter = gearing_.box.kind == GearboxKind::Converter;
    ShiftView view;
    view.gear = gear_;
    view.rpm = toRpm(omega_);
    // Ce sont les roues qui tiennent le moteur, même quand elles patinent : pas la vitesse de la voiture.
    view.roadRpm = toRpm(wheelOmega_ * ratio(gear_));
    view.throttle = throttle_;
    view.demand = demand_;
    view.speedKmh = speedKmh();
    view.sinceShift = sinceShift_;
    view.slipRatio = wheelSlipRatio();
    view.accelMs2 = acceleration_;
    const Drive drive = powertrain_.car.drive;
    const double frontShare = frontShareOf(powertrain_.car);
    const double drivenShare = drive == Drive::All ? 1.0 : (drive == Drive::Front ? frontShare : 1.0 - frontShare);
    view.tractionN = kTyreGrip * drivenShare * static_cast<double>(powertrain_.car.massKg) * kGravity;
    view.resistanceN = roadLoadN(powertrain_, speedMs_) + roadExtraN();
    view.coupled = locked_ || converter;
    view.braking = brake_ > 0.02;
    view.cornering = cornerRadius_ > 0.0;
    view.descending = grade_ < -0.02;
    const int decision = decideShift(shiftMap_, activeMode(), view);
    if (decision != 0) beginShift(gear_ + decision);
}

void DrivingModel::updateDemand() {
    // Le pied, sa vitesse, et sa mémoire : un mode sportif ne repasse pas en douceur parce qu'on a levé le pied un instant.
    const ShiftTraits& traits = shiftTraits(activeMode());
    // Reprendre le pied après un passage n'est pas une demande de plus.
    const bool free = step_ == ShiftStep::None && sinceShift_ > 0.4;
    const double rate = free ? std::max(throttle_ - lastThrottle_, 0.0) / kStep : 0.0;
    lastThrottle_ = throttle_;
    const double asked = clamp(throttle_ + traits.pedalRateSec * rate, 0.0, 1.0);
    const double memory = traits.demandDecaySec > 0.0 ? demand_ - kStep / traits.demandDecaySec : 0.0;
    demand_ = std::max(asked, memory);
    // Le rapport qu'on vient de descendre au freinage, ou qu'on tient dans le virage, reste pour la sortie.
    if (brake_ > 0.02 || cornerRadius_ > 0.0) demand_ = std::max(demand_, traits.brakeDemand);
}

void DrivingModel::setRoadGrade(double percent) {
    grade_ = percent / 100.0;
    gradeTarget_ = grade_;
}

void DrivingModel::setDriveScenario(DriveScenario scenario) {
    if (scenario == scenario_) return;
    scenario_ = scenario;
    const Scenario& next = scenarioOf(scenario);
    // En roulant, on prend le trajet là où il roule aussi : pas d'arrêt pour commencer, seulement de nouvelles consignes.
    double from = 0.0;
    if (speedMs_ > 1.5) {
        for (int i = 0; i < next.legCount; ++i) {
            if (next.legs[i].targetKmh > 0.0) {
                from = next.legs[i].fromSec;
                break;
            }
        }
    }
    scenarioStart_ = clock_ - from;
    strain_ = 0.0;
    underpowered_ = false;
}

double DrivingModel::slopeShare() const { return grade_ / std::sqrt(1.0 + grade_ * grade_); }

double DrivingModel::roadExtraN() const {
    if (grade_ == 0.0 && cornerRadius_ <= 0.0 && trailerKg_ <= 0.0) return 0.0;
    const double carKg = static_cast<double>(powertrain_.car.massKg);
    double force = (carKg + trailerKg_) * kGravity * slopeShare();
    if (trailerKg_ > 0.0 && speedMs_ > 0.1) {
        force += kTrailerRolling * trailerKg_ * kGravity + 0.5 * (kAirDensity * airDensityRatioOf(powertrain_.engine)) * kTrailerDragArea * speedMs_ * speedMs_;
    }
    if (cornerRadius_ > 0.0) force += kCorneringDragShare * carKg * speedMs_ * speedMs_ / cornerRadius_;
    return force;
}

bool DrivingModel::settled() const {
    // Un convertisseur glisse toujours un peu à fond : il n'a pas à se ponter pour passer le rapport suivant.
    const bool coupled = locked_ || gearing_.box.kind == GearboxKind::Converter;
    return coupled && sinceShift_ > shiftTraits(ShiftMode::Normal).gapSec;
}

bool DrivingModel::shiftLiftsPedal() const {
    // Au pied, c'est le conducteur qui décide : il peut donner un coup de gaz au point mort, ou forcer sans lever le pied.
    if (step_ == ShiftStep::None || powerShift() || !assisted()) return false;
    // Sur une séquentielle, le pied reste au plancher : c'est le calculateur qui coupe l'allumage à la montée.
    return gearing_.box.kind != GearboxKind::Sequential;
}

double DrivingModel::stepSeconds(ShiftStep step) const {
    const GearboxKind kind = gearing_.box.kind;
    if (kind == GearboxKind::Sequential) {
        // Barillet motorisé : de 30 à 80 ms au total (HP Academy).
        switch (step) {
            case ShiftStep::Declutch: return 0.012;
            case ShiftStep::PullOut: return 0.008;
            case ShiftStep::Flight: return 0.022;
            case ShiftStep::Engage: return 0.004;
            case ShiftStep::Reclutch: return 0.02;
            default: return 0.0;
        }
    }
    // Sans cône, le conducteur peut aller bien plus vite : il n'attend pas que le blocage cède.
    const double pace = (kind == GearboxKind::Dog ? std::min(shiftPace_, 0.14) : shiftPace_) * shiftTraits(activeMode()).pace;
    switch (step) {
        case ShiftStep::Declutch: return 0.25 * pace;
        case ShiftStep::PullOut: return 0.08 * pace;
        case ShiftStep::Flight: return 0.1 * pace;
        case ShiftStep::Engage: return 0.07 * pace;
        case ShiftStep::Reclutch: return 0.25 * pace;
        default: return 0.0;
    }
}

void DrivingModel::enterStep(ShiftStep next) {
    step_ = next;
    stepLeft_ = stepSeconds(next);
    stepHeld_ = 0.0;
}

void DrivingModel::beginShift(int next) {
    downshifting_ = next < gear_ && next > 0;
    pendingGear_ = next;
    fromNeutral_ = !meshed_;
    flatUpshift_ = flatShift_ && !powerShift() && !fromNeutral_ && gear_ > 0 && next > gear_ && throttle_ > kFlatPedal;
    // Passer la première à l'arrêt n'est pas un changement de rapport : le suivant n'a pas à attendre.
    if (!fromNeutral_) sinceShift_ = 0.0;
    selectorClicks_ += 1.0;
    // Pressé, le conducteur pousse deux fois plus fort sur le levier : le cône serre d'autant.
    shiftEffort_ = 0.6 + 1.4 * throttle_;
    if (powerShift() && gear_ > 0 && next > 0) {
        // Le rapport suivant est déjà engagé de l'autre côté : le couple passe d'un embrayage à l'autre.
        step_ = ShiftStep::Handover;
        stepLeft_ = (gearing_.box.kind == GearboxKind::Converter ? 0.45 : 0.3) * shiftTraits(activeMode()).pace;
        gear_ = next;
        inputOmega_ = wheelOmega_ * ratio(gear_);
        locked_ = false;
        twist_ = 0.0;
        return;
    }
    if (powerShift()) {
        // Depuis le point mort, le robot prépare simplement le rapport : le disque s'arrête et le crabot entre.
        enterStep(ShiftStep::Declutch);
        return;
    }
    enterStep(ShiftStep::Declutch);
    // À la montée, sur une boîte à crabots, on décharge les dents en coupant l'allumage.
    if (dogBox() && assisted() && !fromNeutral_) ignitionCut_ = next > gear_;
}

void DrivingModel::leaveOldGear() {
    meshed_ = false;
    gear_ = pendingGear_;
    locked_ = locked_ && clutch_ > 0.5;
    twist_ = 0.0;
}

void DrivingModel::engageGear() {
    const double target = wheelOmega_ * ratio(pendingGear_);
    const double gap = inputOmega_ - target;
    const GearboxSpec& box = gearing_.box;
    // Le choc d'engagement : presque rien après un cône, violent sur une boîte à crabots.
    impacts_ += 0.5 * box.inputInertia * gap * gap;
    selectorClicks_ += 1.0;
    meshed_ = true;
    gear_ = pendingGear_;
    inputOmega_ = target;
    // Si l'embrayage était fermé, le moteur rejoint le nouveau rapport en le faisant patiner.
    locked_ = false;
    ignitionCut_ = false;
    twist_ = 0.0;
}

void DrivingModel::abandonShift() {
    gear_ = 0;
    pendingGear_ = 0;
    grinding_ = 0.0;
    coneSlip_ = 0.0;
    coneTorque_ = 0.0;
    step_ = ShiftStep::None;
    ignitionCut_ = false;
    meshed_ = false;
}

void DrivingModel::advanceShift() {
    stepLeft_ -= kStep;
    stepHeld_ += kStep;
    const GearboxSpec& box = gearing_.box;
    const double target = wheelOmega_ * ratio(pendingGear_);
    const double gap = std::fabs(inputOmega_ - target);
    switch (step_) {
        case ShiftStep::Declutch: {
            // Assisté, on attend que l'embrayage soit ouvert ; sur une séquentielle, que l'allumage ait fait tomber le couple.
            const bool usesClutch = assisted() && (box.kind != GearboxKind::Sequential || fromNeutral_);
            if (stepLeft_ <= 0.0 && (!usesClutch || clutch_ <= 0.05)) {
                if (fromNeutral_) {
                    gear_ = pendingGear_;
                    enterStep(ShiftStep::Flight);
                } else {
                    enterStep(ShiftStep::PullOut);
                }
            }
            break;
        }
        case ShiftStep::PullOut:
            // Les dents sont plaquées par le couple : il faut le faire tomber pour sortir le rapport.
            if (std::fabs(gearTorque_) > kReleaseShare * box.limitNm && clutch_ > 0.3) {
                if (stepHeld_ > kReleaseGiveUpS) {
                    step_ = ShiftStep::None;
                    ignitionCut_ = false;
                }
                break;
            }
            if (stepLeft_ <= 0.0) {
                leaveOldGear();
                enterStep(ShiftStep::Flight);
            }
            break;
        case ShiftStep::Flight:
            if (stepLeft_ <= 0.0) {
                if (pendingGear_ == 0) {
                    step_ = ShiftStep::None;
                    ignitionCut_ = false;
                } else if (box.coneTorqueNm > 0.0 && pendingGear_ > 0) {
                    enterStep(ShiftStep::Sync);
                    selectorClicks_ += 0.5;
                } else {
                    enterStep(ShiftStep::Sync);
                }
            }
            break;
        case ShiftStep::Sync: {
            // Le baladeur reste bloqué tant que le cône n'a pas égalisé les vitesses (Socin et Walters).
            const bool cones = box.coneTorqueNm > 0.0 && pendingGear_ > 0;
            const double window = cones ? kSyncToleranceRad : (pendingGear_ < 0 ? kSyncToleranceRad * 4.0 : kDogWindowRad);
            coneSlip_ = cones ? gap : 0.0;
            const double patience = cones ? kBlockedPatienceS : 0.0;
            if (gap <= window) {
                grinding_ = 0.0;
                coneSlip_ = 0.0;
                coneTorque_ = 0.0;
                enterStep(ShiftStep::Engage);
            } else if (!assisted() && clutch_ > 0.2) {
                // L'embrayage relie encore le moteur : le cône ne suffit pas, et le baladeur force sur les dents.
                if (stepHeld_ > patience) grinding_ = gap;
                if (stepHeld_ > kGrindGiveUpS) abandonShift();
            } else if (!cones) {
                // Pas de cône : on attend que le moteur soit tombé, et les crabots ratissent en attendant.
                grinding_ = gap;
                if (stepHeld_ > 0.05 && assisted()) {
                    grinding_ = 0.0;
                    enterStep(ShiftStep::Engage);
                } else if (stepHeld_ > kGrindGiveUpS) {
                    abandonShift();
                }
            } else {
                grinding_ = 0.0;
            }
            break;
        }
        case ShiftStep::Engage:
            if (stepLeft_ <= 0.0) {
                engageGear();
                enterStep(ShiftStep::Reclutch);
            }
            break;
        case ShiftStep::Reclutch:
            if (stepLeft_ <= 0.0) step_ = ShiftStep::None;
            break;
        case ShiftStep::Handover: {
            // Montée : l'embrayage qui arrive prend tout le couple, et le calculateur retient le moteur le temps qu'il tombe.
            // Descente : il attend que le moteur soit monté.
            const bool closed = locked_ || std::fabs(omega_ - inputOmega_) < toOmega(40.0);
            if (closed || stepLeft_ <= 0.0) {
                step_ = ShiftStep::None;
                torqueScale_ = 1.0;
            } else {
                torqueScale_ = downshifting_ ? 1.0 : 0.5 * shiftTraits(activeMode()).pace;
            }
            break;
        }
        case ShiftStep::None:
            break;
    }
}

double DrivingModel::upshiftRpm() const {
    return revforge::upshiftPoint(shiftMap_, ShiftMode::Normal, gear_, throttle_);
}

void DrivingModel::updateClutch(bool wantsToMove) {
    if (powertrain_.electric) {
        clutch_ = 1.0;
        return;
    }
    double wanted = 1.0;
    double rate = 8.0;
    const GearboxKind kind = gearing_.box.kind;
    if (kind == GearboxKind::Converter) {
        // Pas de pédale : un pontage, fermé quand le convertisseur ne glisse presque plus.
        const double slipRatio = omega_ > 1.0 ? inputOmega_ / omega_ : 0.0;
        const ShiftTraits& traits = shiftTraits(activeMode());
        const bool pontable = meshed_ && gear_ >= traits.lockGear && step_ == ShiftStep::None && slipRatio > traits.lockSlip && throttle_ < traits.lockThrottle;
        wanted = pontable ? 1.0 : 0.0;
        rate = 4.0;
        clutch_ = approach(clutch_, wanted, rate * kStep);
        if (clutch_ <= 0.5) locked_ = false;
        return;
    }
    if (step_ == ShiftStep::Handover) {
        // Le relais : l'embrayage qui arrive se serre vite à la montée, et laisse monter le moteur à la descente.
        const double gapRpm = rpmInGear(gear_) - toRpm(omega_);
        wanted = downshifting_ && gapRpm > 150.0 ? 0.12 : 1.0;
        rate = 16.0 / shiftTraits(activeMode()).pace;
    } else if (step_ != ShiftStep::None && assisted() && (kind != GearboxKind::Sequential || fromNeutral_)) {
        // Il se referme en douceur : le moteur rejoint le nouveau rapport sans à-coup.
        // En première à l'arrêt, il attend qu'on accélère pour se refermer.
        const double quarter = stepSeconds(ShiftStep::Reclutch);
        const bool waitingInFirst = std::abs(gear_) == 1 && speedMs_ < 1.0;
        wanted = (step_ != ShiftStep::Reclutch || waitingInFirst) ? 0.0 : 0.3;
        rate = 1.0 / std::max(quarter, 0.04);
    } else if (step_ == ShiftStep::None && meshed_ && gear_ != 0 && !locked_) {
        if (gear_ == 1 && speedKmh() < 25.0) {
            // Démarrage : l'embrayage patine autour d'un régime qui suit l'accélérateur.
            // Le launch control tient le moteur au régime choisi : l'embrayage patine autour de lui.
            const double launch = launchHold_ ? launchRpm_ : idle_ + (std::max(peakTorqueRpm_ * 0.8, idle_ * 1.6) - idle_) * throttle_;
            wanted = clamp((toRpm(omega_) - launch * 0.85) / (launch * 0.3), 0.0, 1.0);
            if (!wantsToMove && speedMs_ < 0.5) wanted = 0.0;
            // Pied sur le frein, la boîte pilotée garde l'embrayage ouvert : on le lâche, elle part.
            if (launchHold_ && !manual_ && brake_ > kLaunchBrake) wanted = 0.0;
            // Le robot ne serre pas au-delà du couple que le moteur annonce (celui de l'air admis, moins ce que le calculateur
            // retire : l'antipatinage, le launch control), plus de quoi ramener le moteur à son régime de départ en
            // `kLaunchSettleS`. Serré selon le seul régime, l'embrayage montait d'un coup à 1,6 fois le couple du moteur, et le
            // volant se vidait dans les roues : un pneu sans sommet l'absorbait ; avec un sommet, ce surplus fait patiner, et
            // l'antipatinage n'y pouvait rien (docs/modele.md, « Le pneu, l'antipatinage et le départ »). Une fois les disques
            // presque à la même vitesse, pied posé, il n'y a plus d'élan à vider : il serre, et l'embrayage colle. Pied levé, il
            // reste plafonné : la voiture qui ralentit en côte n'entraîne pas le moteur jusqu'à le caler.
            if (!(std::fabs(omega_ - inputOmega_) <= toOmega(kLaunchSyncRpm) && throttle_ > 0.05)) {
                const double announced = ecuShare_ * manifold_ * fullLoadTorqueNm(powertrain_, toRpm(omega_), boost_);
                const double settle = inertia_ * std::max(omega_ - toOmega(launch), 0.0) / kLaunchSettleS;
                wanted = std::min(wanted, std::max(announced + settle, 0.0) / clutchCapacity_);
            }
            rate = 2.5;
        } else if (gear_ < 0 && speedKmh() < 15.0) {
            // Marche arrière : même démarrage, plus doux.
            const double launch = idle_ * (1.0 + 0.5 * throttle_);
            wanted = clamp((toRpm(omega_) - launch * 0.85) / (launch * 0.3), 0.0, 1.0);
            if (!wantsToMove && speedMs_ < 0.5) wanted = 0.0;
            rate = 2.5;
        } else {
            // Une fois les vitesses presque égales, l'embrayage peut transmettre tout le couple.
            const double gap = std::fabs(toRpm(omega_) - toRpm(wheelOmega_ * ratio(gear_)));
            wanted = 0.3 + 0.7 * (1.0 - clamp(gap / 400.0, 0.0, 1.0));
            // Sous charge, il doit transmettre plus que le moteur ne produit, sinon il patine sans fin.
            if (torque_ > 0.0) wanted = std::max(wanted, std::min(1.0, 1.3 * torque_ / clutchCapacity_));
            rate = 6.0;
            // Une boîte à crabots ne débraye pas : l'embrayage reste serré, et patine sous le choc s'il le faut.
            if (kind == GearboxKind::Sequential) wanted = 1.0;
        }
    }
    clutch_ = approach(clutch_, std::min(wanted, 1.0 - clutchPedal_), rate * kStep);
    if (clutch_ <= 0.01) locked_ = false;
}

double DrivingModel::bmepPumping(double rpm) const {
    // Moteur tiré par les roues, soupapes fermées à tour de rôle : un frein bien plus fort qu'un moteur qui tourne.
    return pumpingTorqueNm(powertrain_, std::max(rpm, idle_ + 1500.0), 0.0) * clamp(rpm / 1000.0, 0.2, 1.0);
}

double DrivingModel::rotaryCompressionLoadNm() const {
    // Chaque rotor a trois chambres, décalées d'un tour d'arbre : le même calcul qu'un cylindre, sur un cycle de trois tours.
    // Le volume suit 2/3 de l'angle de l'arbre : une compression dure plus longtemps, et pousse moins fort sur l'arbre.
    const EngineConfig& e = powertrain_.engine;
    const int rotors = e.rotors();
    const double swept = static_cast<double>(e.displacementCc) * 1e-6 / rotors;
    const double ratio = static_cast<double>(starter_.compressionRatio);
    const double clearance = swept / (ratio - 1.0);
    const double full = clearance + swept;
    // L'air enfermé part de l'atmosphère du jour : moins de compression à lancer en altitude.
    const double atmosphere = 1e5 * ambientAirOf(powertrain_.engine).pressureRatio;
    // Les segments d'apex fuient à bas régime : de l'ordre de 8,5 bar à 250 tr/min, contre 13 à 14 sur un moteur à pistons.
    constexpr double kCrankPolytropic = 0.96;
    constexpr double kExpansionReturn = 0.8;
    constexpr double kShare = 2.0 / 3.0;
    double load = 0.0;
    for (int rotor = 0; rotor < rotors; ++rotor) {
        for (int face = 0; face < 3; ++face) {
            const double shaft = crankAngle_ + 2.0 * kPi * (static_cast<double>(face) + static_cast<double>(rotor) / rotors);
            const double phi = std::fmod(shaft * kShare, 4.0 * kPi);
            const bool compressing = phi >= 3.0 * kPi;
            if (!compressing && phi >= kPi) continue;
            const double volume = clearance + swept * 0.5 * (1.0 - portable::cos(phi));
            const double pressure = atmosphere * portable::pow(full / volume, kCrankPolytropic);
            const double push = (pressure - atmosphere) * swept * 0.5 * kShare * -std::sin(phi);
            load += compressing ? push : kExpansionReturn * push;
        }
    }
    return load;
}

double DrivingModel::twoStrokeCompressionLoadNm() const {
    // Chaque cylindre comprime à chaque tour, mais seulement une fois la lumière d'échappement fermée : du volume qu'elle enferme.
    const EngineConfig& e = powertrain_.engine;
    const TwoStrokePorts ports = twoStrokePortsOf(e);
    const int count = e.pistons();
    const double swept = static_cast<double>(e.displacementCc) * 1e-6 / count;
    const double clearance = swept / (ports.geometricCompression - 1.0);
    const double closed = clearance + swept * (1.0 - ports.exhaustHeight);
    const double closesAt = (360.0 - ports.exhaustOpenDeg) * kPi / 180.0;
    const double opensAt = ports.exhaustOpenDeg * kPi / 180.0;
    constexpr double kAtmosphere = 1e5;
    // Mêmes fuites et même retour qu'un quatre-temps (voir `compressionLoadNm`).
    constexpr double kCrankPolytropic = 1.13;
    constexpr double kExpansionReturn = 0.8;
    double load = 0.0;
    for (int i = 0; i < count; ++i) {
        const double phi = std::fmod(crankAngle_ + 2.0 * kPi * i / count, 2.0 * kPi);
        const bool compressing = phi >= closesAt;
        if (!compressing && phi >= opensAt) continue;
        const double travel = pistonTravelShare(phi * 180.0 / kPi, ports.rodOverStroke);
        const double volume = clearance + swept * travel;
        const double pressure = kAtmosphere * portable::pow(closed / volume, kCrankPolytropic);
        // Le bras de levier : dV/dφ, calculé par la même cinématique.
        const double ahead = pistonTravelShare(phi * 180.0 / kPi + 0.5, ports.rodOverStroke);
        const double lever = swept * (ahead - travel) / (0.5 * kPi / 180.0);
        const double push = (pressure - kAtmosphere) * lever;
        load += compressing ? -push : -kExpansionReturn * push;
    }
    return load;
}

double DrivingModel::compressionLoadNm() const {
    if (isRotary(powertrain_.engine)) return rotaryCompressionLoadNm();
    if (twoStrokeEngine_) return twoStrokeCompressionLoadNm();
    // Chaque cylindre comprime l'air enfermé, puis le gaz lui rend presque tout en se détendant.
    const double swept = static_cast<double>(powertrain_.engine.displacementCc) * 1e-6 / powertrain_.engine.cylinders;
    const double ratio = static_cast<double>(starter_.compressionRatio);
    const double clearance = swept / (ratio - 1.0);
    const double full = clearance + swept;
    const double atmosphere = 1e5 * ambientAirOf(powertrain_.engine).pressureRatio;
    // À 250 tr/min, le gaz a le temps de fuir et de refroidir : 13 à 14 bar en essence, 25 à 30 en diesel,
    // au lieu des 21 et 42 qu'une compression sans perte donnerait.
    constexpr double kCrankPolytropic = 1.13;
    constexpr double kExpansionReturn = 0.8;
    double load = 0.0;
    // Un VR, un W, plus de douze cylindres : les compressions tombent aux vrais intervalles, inégaux s'ils le sont.
    const bool own = ownArchitecture(powertrain_.engine);
    const Architecture architecture = own ? architectureOf(powertrain_.engine) : Architecture{};
    const int count = own ? architecture.cylinders : powertrain_.engine.cylinders;
    // Segments, soupapes : aucun cylindre ne comprime tout à fait comme son voisin.
    constexpr double kCylinderSeal[6] = {1.0, 0.88, 1.07, 0.93, 1.1, 0.85};
    for (int i = 0; i < count; ++i) {
        const double lead = own ? 4.0 * kPi * (1.0 - architecture.firingDeg[static_cast<std::size_t>((count - i) % count)] / 720.0) : 4.0 * kPi * i / count;
        const double phi = std::fmod(crankAngle_ + lead, 4.0 * kPi);
        const bool compressing = phi >= 3.0 * kPi;
        if (!compressing && phi >= kPi) continue;
        const double volume = clearance + swept * 0.5 * (1.0 - portable::cos(phi));
        const double pressure = atmosphere * portable::pow(full / volume, kCrankPolytropic);
        const double push = kCylinderSeal[i % 6] * (pressure - atmosphere) * swept * 0.5 * -std::sin(phi);
        load += compressing ? push : kExpansionReturn * push;
    }
    return load;
}

void DrivingModel::stepTwin(double rpmNow, bool burning) {
    TwinTurboInput input;
    input.rpm = rpmNow;
    input.fill = manifold_;
    input.breathing = breathingOf(powertrain_, rpmNow);
    input.burning = burning;
    if (boostControlled_) {
        // Intégration : le calculateur donne la cible (plafonnée par la vitesse des roues, ou selon le rapport), et la loi des
        // soupapes (ou des aubes) du biturbo la suit. Son PID et son anticipation ne sont identifiés que sur le banc d'un turbo
        // seul : branchés tels quels, ils dépassaient la cible de 0,49 bar sur un séquentiel, dont l'étagement oscillait
        // (docs/modele.md, « Quand les réglages se rencontrent (2) »). Les arbres du biturbo comptent dans l'air de référence.
        const double ambientPa = turbo_laws::constants().atmosphere;
        input.targetBar = (revforge::boostTargetPa(boostSpec_, rpmNow, gear_, ambientPa) - ambientPa) / 1e5;
    }
    twin_.step(kStep, input);
    boost_ = std::max(twin_.boost(), 0.0);
}

void DrivingModel::stepTurbo(double rpmNow, bool burning) {
    if (twinEngine_) {
        stepTwin(rpmNow, burning);
        return;
    }
    TurboInput input;
    input.rpm = rpmNow;
    input.fill = manifold_;
    input.breathing = breathingOf(powertrain_, rpmNow);
    input.burning = burning;
    input.antiLag = antiLag_;
    if (carbEngine_ && antiLag_.active) input.carbRichness = carburetorRichness(carb_.spec(), manifold_, rpmNow);
    if (ecuEngine_ && burning) {
        input.ecu.retardDeg = ecu_.retardDeg;
        input.ecu.firedShare = cut_ ? 0.0 : ecu_.firedShare;
        input.ecu.fuelOnCut = fuelOnCut_;
    }
    if (boostControlled_) {
        // Le calculateur lit la pression de charge et le rapport engagé, et commande la membrane par son électrovanne.
        BoostControlInput control;
        control.rpm = rpmNow;
        control.gear = gear_;
        control.chargePa = turbo_.state().chargePa;
        control.ambientPa = turbo_.spec().ambientPa;
        input.gateDemand = boostController_.step(boostSpec_, kStep, control);
    }
    if (chargeAirEngine_) input.cooledK = chargeAir_.stepCooler(kStep, turbo_.state().compressorOutK, turbo_.state().compressorFlow, speedMs_);
    turbo_.step(kStep, input);
    boost_ = std::max(turbo_.boost(), 0.0);
}

double DrivingModel::boostTargetPa() const {
    // L'air du jour (atmosphère) : exactement 101 325 Pa à 0 m.
    if (!turboEngine_) return powertrain_.electric ? 101325.0 : ambientAirOf(powertrain_.engine).pressurePa;
    // Le biturbo compte ses arbres dans l'air de référence ; son calculateur n'a pas de PID (stepTwin).
    const double ambientPa = twinEngine_ ? turbo_laws::constants().atmosphere : turbo_.spec().ambientPa;
    return boostControlled_ && !twinEngine_ && boostController_.targetPa() > 0.0 ? boostController_.targetPa() : revforge::boostTargetPa(boostSpec_, rpm(), gear_, ambientPa);
}

void DrivingModel::updateAntiLag(double throttle, double rpmNow, bool off) {
    const double threshold = kAntiLagMinShare * redline_ * (antiLag_.active ? kAntiLagHoldShare : 1.0);
    antiLag_.active = !off && !cut_ && !catching_ && throttle < kAntiLagLiftedPedal && rpmNow >= threshold;
}

double DrivingModel::stepKnock(double rpmNow) {
    // La pression de charge, rapportée à la pleine charge stabilisée à ce régime : c'est elle qui comprime les gaz frais.
    const bool charged = turboEngine_ || superchargerEngine_;
    const double bar = charged ? static_cast<double>(powertrain_.engine.boostBar) : 0.0;
    const double full = 1.0 + bar * knockMap_.fullBoost(rpmNow);
    const double now = std::max(manifold_, 0.0) * (1.0 + bar * std::max(boost_, 0.0));
    const KnockPoint point = knockMap_.at(rpmNow, now / full);
    const double wanted = point.stockDeg + knockMap_.advanceOffsetDeg();
    double applied = wanted - knockRetard_;
    double share = cut_ ? 0.0 : knockingShare(applied - point.meanLimitDeg);
    if (knockMap_.sensor()) {
        // Chaque cliquetis détecté retire de l'avance à son cylindre ; elle revient peu à peu quand il se tait.
        const double rate = kKnockRetardPerEventDeg * share * knockPerCylinder_ * rpmNow - (knockRetard_ > 0.0 ? kKnockRecoverDegPerS : 0.0);
        knockRetard_ = clamp(knockRetard_ + rate * kStep, 0.0, kKnockMaxRetardDeg);
        applied = wanted - knockRetard_;
        share = cut_ ? 0.0 : knockingShare(applied - point.meanLimitDeg);
    }
    knockAdvance_ = applied;
    knockLimit_ = point.limitDeg;
    knockDrive_.active = true;
    knockDrive_.excessDeg = static_cast<float>(cut_ ? -100.0 : applied - point.meanLimitDeg);
    knockDrive_.spreadDeg = static_cast<float>(kPhaseSpreadDeg);
    knockDrive_.gasK = static_cast<float>(point.gasK);
    knockDrive_.share = static_cast<float>(share);
    // Le même air, brûlé plus tôt ou plus tard que la carte d'origine : le travail des gaz en dépend.
    return knockMap_.torqueShare(rpmNow, applied, point.stockDeg);
}

void DrivingModel::stepSupercharger(double rpmNow) {
    SuperchargerInput input;
    input.rpm = rpmNow;
    input.fill = manifold_;
    input.breathing = breathingOf(powertrain_, rpmNow);
    if (chargeAirEngine_) input.cooledK = chargeAir_.stepCooler(kStep, supercharger_.state().compressorOutK, supercharger_.state().engineFlow, speedMs_);
    supercharger_.step(kStep, input);
    boost_ = std::max(supercharger_.boost(), 0.0);
}

double DrivingModel::stepChargeAir(double rpmNow, double throttle, double indicated) {
    ChargeAirInput in;
    in.dt = kStep;
    in.rpm = rpmNow;
    in.throttle = throttle;
    in.fill = manifold_;
    in.speedMs = speedMs_;
    in.burning = !cut_;
    const ChargeCoolerSpec& cooler = chargeAir_.setup().cooler;
    if (turboEngine_) {
        const TurboState& s = turbo_.state();
        in.compressorOutK = s.compressorOutK;
        in.chargeFlowKgS = s.compressorFlow;
        in.chargeK = s.coolerOutK;
        // Le couple de base d'un turbo suppose l'échangeur d'origine, à la sortie du compresseur de ce pas.
        in.referenceK = cooler.kind == Intercooler::AirToAir ? s.coolerOutK : stockCoolerOutletK(s.compressorOutK, s.compressorFlow / turbo_.spec().ratedFlow);
        in.plenumPa = s.plenumPa;
        in.airKgS = s.engineFlow;
    } else if (superchargerEngine_) {
        const SuperchargerState& s = supercharger_.state();
        in.compressorOutK = s.compressorOutK;
        in.chargeFlowKgS = s.engineFlow;
        in.chargeK = s.coolerOutK;
        // Celui d'un compresseur suppose son échangeur à l'équilibre : la boucle d'eau en est l'écart.
        in.referenceK = cooler.kind == Intercooler::AirToAir ? s.coolerOutK : chargeCoolerSteadyK(cooler, s.compressorOutK, s.engineFlow);
        in.plenumPa = s.plenumPa;
        in.airKgS = s.engineFlow;
    } else {
        in.chargeK = intakeAirK(powertrain_, rpmNow);
        in.referenceK = in.chargeK;
        in.plenumPa = chargeAir_.setup().conditions.ambientPa * manifold_;
        in.airKgS = consumption_.model().airGps(rpmNow, manifold_, indicated) / 1000.0;
    }
    chargeAir_.step(in);
    const double share = chargeAir_.torqueShare();
    // Pour le son : chaque bar de turbo y ajoute 0,4 à la pression d'ouverture, pour 0,9 de couple (`boostFill`, engine_sound.cpp ;
    // `kBoostEfficiency`, powertrain.cpp). Le couple ajouté se compte en part de celui d'un atmosphérique à pleine charge.
    constexpr double kFillPerTorqueShare = 0.4 / 0.9;
    const double natural = indicated / (1.0 + 0.9 * static_cast<double>(powertrain_.engine.boostBar) * boost_);
    const double added = (share - 1.0) * manifold_ * indicated + (chargeAir_.nitrousOn() ? chargeAir_.nitrousNm() : 0.0);
    chargeFill_ = natural > 1.0 ? kFillPerTorqueShare * added / natural : 0.0;
    return share;
}

ManifoldCharge DrivingModel::manifoldCharge() const {
    if (chargeAirEngine_) return chargeAir_.manifold();
    ManifoldCharge m;
    if (turboEngine_) {
        m.pressurePa = turbo_.state().plenumPa;
        m.temperatureK = turbo_.state().coolerOutK;
    } else if (superchargerEngine_) {
        m.pressurePa = supercharger_.state().plenumPa;
        m.temperatureK = supercharger_.state().coolerOutK;
    } else {
        m.pressurePa = 101325.0 * manifold_;
        m.temperatureK = intakeAirK(powertrain_, toRpm(omega_));
    }
    return m;
}

void DrivingModel::updateEngine(double throttle, bool off) {
    const double rpmNow = toRpm(omega_);
    burnt_.burning = false;
    // La post-injection d'une régénération ne vaut que pour ce pas : le démarreur et le calage n'en ont pas.
    if (dpfEngine_) burnt_.extraFuelGps = 0.0;
    // Le démarreur et le calage quittent cette fonction sans repasser par l'anti-lag : il s'arrête d'abord.
    if (antiLagEngine_) {
        antiLag_.active = false;
        antiLagLostNm_ = 0.0;
        burnt_.extraFuelGps = 0.0;
        burnt_.extraAirGps = 0.0;
    }
    if (chargeAirEngine_) burnt_.extraFuelGps = 0.0;
    if (powertrain_.electric) {
        manifold_ = throttle;
        cut_ = off;
        const double regen = speedMs_ > 0.5 ? 0.18 * fullLoadTorqueNm(powertrain_, rpmNow, 0.0) : 0.0;
        double available = fullLoadTorqueNm(powertrain_, rpmNow, 0.0);
        if (gear_ == 0) available *= 0.12;
        torque_ = off ? 0.0 : (throttle > 0.01 ? throttle * available : -regen * (1.0 - throttle));
        // L'antipatinage d'une voiture électrique retire directement du couple au moteur.
        if (ecuEngine_) {
            stepEcu(throttle, rpmNow, off);
            if (torque_ > 0.0 && ecu_.traction > 0.0) torque_ *= 1.0 - ecu_.traction;
        }
        burnt_.motorNm = torque_;
        torque_ -= frictionTorqueNm(powertrain_, rpmNow);
        return;
    }

    if (overrunLeft_ > 0.0) overrunLeft_ = std::max(overrunLeft_ - kStep, 0.0);
    if (meshHoldLeft_ > 0.0) {
        meshHoldLeft_ = rpmNow > idle_ * 0.85 ? 0.0 : meshHoldLeft_ - kStep;
        starterLoad_ *= 1.0 - kStep / 0.05;
    }
    if (crankLeft_ > 0.0) {
        // Rien ne brûle encore : l'arbre, s'il tournait, continue sur son élan.
        if (turboEngine_) stepTurbo(rpmNow, false);
        if (superchargerEngine_) stepSupercharger(rpmNow);
        if (carbEngine_) carb_.prime();
        crankLeft_ -= kStep;
        cut_ = true;
        manifold_ = 0.0;
        torque_ = 0.0;
        if (crankLeft_ <= 0.0) {
            overrunLeft_ = kReleaseAfterCatchS;
            // Le démarreur reste engagé pendant que le moteur prend : le pignon ne sort qu'une fois dépassé.
            meshHoldLeft_ = kMeshHoldS;
            // Le collecteur s'est rempli pendant le lancement : les premières explosions sont franches.
            manifold_ = 0.7;
        }
        if (pullInLeft_ > 0.0) {
            // Le solénoïde pousse le pignon vers la couronne : rien ne tourne encore.
            pullInLeft_ -= kStep;
            return;
        }
        // Moteur à courant continu : tout son couple à l'arrêt, plus rien à sa vitesse libre.
        const double gear = static_cast<double>(starter_.ringTeeth) / starter_.pinionTeeth;
        const double reduction = static_cast<double>(starter_.reduction);
        const double freePinion = toOmega(static_cast<double>(starter_.motorFreeRpm)) / reduction;
        const double stallAtPinion = 4.0 * static_cast<double>(starter_.powerKw) * 1000.0 / freePinion;
        const double pinionTorque = std::max(stallAtPinion * (1.0 - omega_ * gear / freePinion), 0.0);
        const double motorInertia = static_cast<double>(starter_.motorInertia) * reduction * reduction;
        const double total = inertia_ + motorInertia * gear * gear;
        const double driven = pinionTorque * gear * kStarterEfficiency;
        double crankFriction = kColdFriction * frictionTorqueNm(powertrain_, rpmNow);
        // L'huile froide freine aussi le lancement : le démarreur tourne moins vite.
        if (coldCoupled_) crankFriction += hydroNmPerRpm_ * std::max(rpmNow, 0.0) * viscosityExcess_;
        frictionHeatNm_ = crankFriction;
        const double net = driven - compressionLoadNm() - crankFriction;
        const double alpha = net / total;
        // Ce qui passe vraiment par les dents : le couple du moteur, moins ce qu'il faut pour accélérer
        // l'induit. Il s'annule, et change de sens, quand une détente fait filer le vilebrequin.
        starterLoad_ = (driven - motorInertia * gear * gear * alpha) / (stallAtPinion * gear);
        // La roue libre du pignon : le vilebrequin peut filer, jamais repartir en arrière.
        omega_ = std::max(omega_ + alpha * kStep, 0.0);
        crankAngle_ = std::fmod(crankAngle_ + omega_ * kStep, 4.0 * kPi);
        return;
    }

    if (stalled_) {
        // Calé : plus rien ne brûle, seuls les frottements et les compressions restent.
        cut_ = true;
        manifold_ += (0.0 - manifold_) * std::min(1.0, kStep / manifoldTau_);
        // Calé : plus de gaz, mais l'arbre garde son élan.
        if (turboEngine_) stepTurbo(rpmNow, false);
        if (superchargerEngine_) stepSupercharger(rpmNow);
        if (carbEngine_) carb_.prime();
        double stallFriction = kColdFriction * frictionTorqueNm(powertrain_, rpmNow);
        if (coldCoupled_) stallFriction += hydroNmPerRpm_ * std::max(rpmNow, 0.0) * viscosityExcess_;
        frictionHeatNm_ = stallFriction;
        torque_ = -stallFriction - bmepPumping(rpmNow);
        return;
    }

    // Rupteur : la combustion s'arrête, puis reprend un peu plus bas.
    // Elle laisse le moteur redescendre de 4 % : c'est ce rebond qu'on entend, une dizaine de fois par seconde.
    cutHeld_ = std::max(cutHeld_ - kStep, 0.0);
    // Le calculateur : le launch control déplace ce régime ; le rupteur progressif remplace la coupure franche (ecu.hpp).
    double limit = limiterRpm_;
    bool hardLimiter = true;
    if (ecuEngine_) {
        limit = stepEcu(throttle, rpmNow, off);
        hardLimiter = limiterKind_ != RevLimiter::Soft || launchHold_;
    }
    if (hardLimiter && rpmNow >= limit && !limiterCut_) {
        limiterCut_ = true;
        cutHeld_ = kLimiterMinCutS;
    } else if (limiterCut_ && cutHeld_ <= 0.0 && rpmNow < limit * (1.0 - limiterDrop_)) {
        limiterCut_ = false;
        // Jamais deux rebonds identiques : le calculateur reprend à une explosion près.
        limiterSeed_ = limiterSeed_ * 1664525u + 1013904223u;
        const double draw = static_cast<double>(limiterSeed_ >> 16 & 0xFFFF) / 65535.0;
        limiterDrop_ = kLimiterDropShare * (0.65 + 0.7 * draw);
    }
    cut_ = off || limiterCut_ || ignitionCut_ || flatCut_;
    if (antiLagEngine_) updateAntiLag(throttle, rpmNow, off);

    double friction = frictionTorqueNm(powertrain_, rpmNow);
    // Moteur froid : l'huile épaisse ajoute ses frottements hydrodynamiques. La combustion, elle, reste celle du moteur chaud.
    if (coldCoupled_) {
        coldFrictionNm_ = hydroNmPerRpm_ * std::max(rpmNow, 0.0) * viscosityExcess_;
        friction += coldFrictionNm_;
    }
    frictionHeatNm_ = friction;
    double indicated = fullLoadTorqueNm(powertrain_, rpmNow, boost_) + frictionTorqueNm(powertrain_, rpmNow);
    // Moteur froid : des parois plus froides chauffent moins l'air piégé (`trappedAirK`, atmosphère) ; il en entre d'autant plus.
    // La paroi est le métal du réseau thermique. Parti chaud, rien de cela : la paroi de référence est celle du moteur chaud.
    if (coldCoupled_) {
        const double airK = manifoldCharge().temperatureK;
        indicated *= trappedAirK(airK) / trappedAirK(airK, thermal_.metalC() + 273.0);
    }
    // Au démarrage, le calculateur enrichit le mélange : le moteur prend même très bas.
    if (catching_ && rpmNow < idle_) indicated = std::max(indicated, 0.8 * fullLoadTorqueNm(powertrain_, idle_, 0.0));

    // Régulation de ralenti : juste assez d'air pour compenser les frottements, jusqu'à ce que
    // le pied prenne le relais. Au démarrage, le moteur prend une poignée de tours en trop.
    const double ripple = 9.0 * portable::sin(clock_ * 2.1) + 4.0 * portable::sin(clock_ * 5.3);
    // Ralenti accéléré : le moteur vient de partir, il redescend en une seconde environ.
    double idleTarget = idle_ * (1.0 + fastIdle_) + ripple;
    // Moteur froid : le calculateur, ou la came de ralenti accéléré d'un carburateur, vise plus haut tant que l'eau est froide.
    if (coldCoupled_) idleTarget += idle_ * coldIdleRise_;
    if (catching_) {
        if (rpmNow > idleTarget * 0.98) catching_ = false;
    } else {
        fastIdle_ *= 1.0 - kStep / 0.9;
    }
    const double balance = friction / std::max(indicated, 1.0);
    // Intégration : après un départ à froid, le ralenti accéléré (came d'un carburateur, vanne du calculateur) ouvre autant qu'au
    // démarrage. Limité à 0,45 comme moteur chaud, il ne tenait pas l'huile froide d'un moteur peu comprimé : la Cadillac, à son
    // taux publié de 5,35, calait par 5 °C. Parti chaud, rien ne change.
    const double idleAuthority = catching_ || coldCoupled_ ? 0.8 : 0.45;
    const double idleOpen = off ? 0.0 : clamp(balance * (1.0 + (idleTarget - rpmNow) / 60.0), 0.0, idleAuthority);

    // Un petit filet d'accélérateur remplit vite à bas régime, moins à haut régime.
    const double fraction = clamp(rpmNow / redline_, 0.0, 1.0);
    const double softness = 0.15 + 0.6 * fraction;
    const double opened = throttle * (1.0 + softness) / (throttle + softness);
    double wantedManifold = std::max(opened, idleOpen);
    if (antiLag_.active) wantedManifold = std::max(wantedManifold, antiLagOpening(antiLag_.kind));
    const double fillBefore = manifold_;
    manifold_ += (wantedManifold - manifold_) * std::min(1.0, kStep / manifoldTau_);
    if (camVariable_) stepCam(rpmNow);

    // Turbine : tout est dans `TurboCharger`. Le moteur lui donne ce qu'il rejette, et reçoit la pression qui en résulte.
    // Le compresseur, lui, ne demande rien aux gaz : il tourne avec le vilebrequin.
    // Une coupure d'allumage laisse passer le carburant : il brûle encore, mais dans la ligne.
    if (ecuEngine_ || ecuMap_) finishEcu(rpmNow, indicated, off);
    if (turboEngine_) stepTurbo(rpmNow, !cut_ || fuelOnCut_);
    else if (superchargerEngine_) stepSupercharger(rpmNow);
    else boost_ = 0.0;
    // L'échangeur, l'eau, le protoxyde : l'air admis pèse autrement. Rien de cela au défaut.
    if (chargeAirEngine_) indicated *= stepChargeAir(rpmNow, throttle, indicated);

    double produced = cut_ ? 0.0 : manifold_ * indicated * torqueScale_;
    if (knockEngine_) produced *= stepKnock(rpmNow);
    // Le calculateur : ce que retirent le retard et les cylindres coupés, et la richesse, qui ne s'écarte qu'à pleine charge.
    if (ecuEngine_ && ecuShare_ != 1.0) produced *= ecuShare_;
    if (lambdaRatio_ != 1.0) produced *= lambdaLoadRatio(powertrain_.engine, manifold_) / lambdaRatio_;
    // Un deux-temps balaie mieux à faible débit, mais il rate : ce que son air frais enfermé donne vraiment (voir docs/modele.md).
    if (twoStrokeEngine_ && !cut_) produced *= twoStrokePartLoad(powertrain_.engine, rpmNow, manifold_).torqueShare;
    antiLagLostNm_ = 0.0;
    if (antiLag_.active) {
        const double kept = produced * antiLagTorqueShare(antiLag_.kind);
        antiLagLostNm_ = produced - kept;
        produced = kept;
    }
    if (carbEngine_) {
        CarburetorInput in;
        in.rpm = rpmNow;
        in.airGps = consumption_.model().airGps(rpmNow, manifold_, indicated);
        in.fill = manifold_;
        in.fillRate = (manifold_ - fillBefore) / kStep;
        in.throttle = throttle;
        // En part de l'air de référence : l'air du jour, puis ce que la suralimentation y ajoute.
        in.density = (turboEngine_ || superchargerEngine_ ? 1.0 + boost_ * static_cast<double>(powertrain_.engine.boostBar) : 1.0) * airDensityRatioOf(powertrain_.engine);
        carb_.step(in, kStep);
        // Le moteur qui prend a son propre enrichissement : le film ne compte qu'ensuite.
        if (catching_) carb_.prime();
        else produced *= carb_.torqueShare();
    }
    // Une injection indirecte : le calculateur compense son film, exactement à 90 °C. Ailleurs, le mélange s'en écarte.
    portFilmActive_ = portEngine_ && coolantC_ != kWarmCoolantC;
    if (portFilmActive_) {
        PortFilmInput in;
        in.wantedGps = consumption_.model().airGps(rpmNow, manifold_, indicated) / 14.7;
        in.manifoldShare = std::max(manifold_, 0.05) * (turboEngine_ || superchargerEngine_ ? 1.0 + boost_ * static_cast<double>(powertrain_.engine.boostBar) : 1.0);
        // L'air dans le conduit : le cylindre balaie le même volume à chaque tour, quelle que soit la pression.
        in.portSpeedMs = kPortSpeedAtRatedMs * rpmNow / (0.88 * redline_);
        in.coolantC = coolantC_;
        portFilm_.step(in, kStep);
        if (catching_) portFilm_.prime();
        else produced *= portFilm_.torqueShare();
    }
    // La recirculation : une charge diluée brûle plus lentement et plus froid. Sur un essence, le papillon s'ouvre plus pour
    // le même air frais : moins de pompage.
    double fresh = manifold_;
    if (egrEngine_) {
        const bool diesel = powertrain_.engine.burnsDiesel();
        egrShare_ = cut_ ? 0.0 : egrRate(diesel, manifold_, rpmNow, redline_);
        const double lambda = diesel ? smokeLambdaOf(powertrain_, rpmNow) / std::max(manifold_, 0.05) : 1.0;
        produced *= egrEfficiencyShare(diesel, egrShare_, lambda);
        if (!diesel) fresh = std::min(manifold_ / (1.0 - egrShare_), 1.0);
    }
    // Le protoxyde : sa propre combustion, plein gaz seulement, en plus de ce que donne l'air.
    if (chargeAirEngine_ && chargeAir_.nitrousOn()) produced += chargeAir_.nitrousNm() * torqueScale_;
    // Rupteur : le papillon reste ouvert, donc peu de pompage. Le moteur ne redescend que sur ses frottements.
    // Sur un turbo, s'y ajoute le gaz à pousser contre la turbine.
    // Sur un compresseur, ce que coûte son entraînement : la combustion le paie, et il s'ajoute au frein moteur.
    // Le compresseur d'un twincharger aussi, embrayé : glissement compris.
    double charger = superchargerEngine_ ? supercharger_.driveNm(rpmNow) : (twinEngine_ ? twin_.chargerDriveNm() : 0.0);
    // La pompe haute pression d'une injection directe : comme le compresseur, la combustion la paie.
    if (directEngine_ && !cut_) charger += directPumpTorqueNm(powertrain_, rpmNow, manifold_, boost_);
    const double pumping = pumpingTorqueNm(powertrain_, rpmNow, egrEngine_ ? fresh : manifold_) + (turboEngine_ ? (twinEngine_ ? twin_.backPressureNm(manifold_) : turbo_.backPressureNm(manifold_)) : 0.0) + charger;
    torque_ = produced - friction - pumping;
    // Le filtre à particules : le piston chasse le gaz contre lui. Une perte de plus pour le moteur, pas pour la combustion.
    if (dpfEngine_) {
        const ExhaustState gas = exhaustStateOf(powertrain_, rpmNow, manifold_, boost_);
        FilterInput in;
        in.exhaustKgS = gas.kgS;
        in.exhaustK = gas.kelvin;
        in.fuelGps = cut_ ? 0.0 : gas.fuelGps;
        in.lambda = gas.lambda;
        in.smokeLambda = smokeLambdaOf(powertrain_, rpmNow);
        in.burning = !cut_;
        in.running = !off;
        dpf_.step(in, kStep);
        torque_ -= filterPumpingNm(dpf_.spec(), dpf_.backPressurePa());
    }
    // Pour la consommation : le couple au frein que demande cette charge d'air. Le calculateur qui retient le moteur
    // pendant un relais retarde l'allumage : il gaspille, il ne dépense pas moins. Le broutement s'annule en moyenne.
    burnt_.burning = !cut_ || fuelOnCut_;
    // Une coupure d'injection partielle : l'air de ces cylindres passe, leur carburant non.
    if (ecuEngine_) burnt_.fuelShare = fuelOnCut_ ? 1.0 : ecu_.firedShare;
    // Le rupteur ne coupe que l'étincelle : le carburateur continue de débiter, et ce mélange part à l'échappement.
    if (carbEngine_) {
        burnt_.burning = !off;
        burnt_.pumpGps = carb_.pumpGps();
    }
    burnt_.brakeNm = manifold_ * indicated - friction - pumping;
    // La richesse ne s'écarte de l'origine qu'à pleine charge : le couple de cet air suit la même rampe que le moteur.
    if (lambdaRatio_ != 1.0) burnt_.brakeNm = manifold_ * indicated * lambdaLoadRatio(powertrain_.engine, manifold_) / lambdaRatio_ - friction - pumping;
    // Les pistons paient aussi le compresseur : c'est de l'essence, pas du couple à la roue.
    burnt_.frictionNm = friction + charger;
    burnt_.fullGrossNm = indicated;
    burnt_.load = manifold_;
    if (coldCoupled_) burnt_.enrichment = coldEnrichment_;
    // Le couple au frein ci-dessus suit l'air, donc le cylindre retardé ou coupé paie déjà son carburant : reste ce qui s'y ajoute.
    // Sur un carburateur, ce surplus est le mélange déjà payé ci-dessus : le compter ici le ferait payer deux fois.
    if (antiLagEngine_) {
        burnt_.extraFuelGps = carbEngine_ ? 0.0 : turbo_.antiLag().surplusFuel * 1000.0;
        burnt_.extraAirGps = turbo_.antiLag().airFlow * 1000.0;
    }
    // La post-injection d'une régénération : elle ne fait aucun couple, mais elle se paie.
    if (dpfEngine_) burnt_.extraFuelGps = (antiLagEngine_ ? burnt_.extraFuelGps : 0.0) + dpf_.extraFuelGps();
    // Tout le gicleur d'essence du kit se paie, qu'il brûle ou non ; le couple ci-dessus, lui, ne suit que l'air.
    if (chargeAirEngine_) burnt_.extraFuelGps += chargeAir_.nitrousFuelGps();
    lugging_ = manual_ && gear_ != 0 && meshed_ && locked_ && rpmNow < idle_ * 1.3 && speedMs_ > 0.5;
    // Trop bas en prise : chaque explosion donne un à-coup que rien n'amortit, ça broute.
    if (lugging_) {
        const double beat = clock_ * 2.0 * kPi * rpmNow / 120.0 * powertrain_.engine.firingsPerTwoTurns();
        torque_ += 0.5 * std::fabs(produced) * portable::sin(beat);
    }
}

void DrivingModel::stepCam(double rpmNow) {
    if (camTwoStep_) {
        // Le second profil n'entre qu'en charge, au-delà du régime où les courbes se croisent, et ne sort qu'en dessous de
        // lui moins l'hystérésis : à ce régime, les deux profils donnent le même couple, et la bascule ne se sent pas.
        const bool loaded = manifold_ >= kCamSwitchLoad && !cut_;
        if (!camDrive_.high) {
            camDrive_.high = loaded && camSwitchRpm_ > 0.0 && rpmNow > camSwitchRpm_ * (1.0 + kCamSwitchHysteresis);
        } else if (rpmNow < camSwitchRpm_ * (1.0 - kCamSwitchHysteresis) || manifold_ < kCamSwitchLoad - kCamSwitchLoadHysteresis) {
            camDrive_.high = false;
        }
    }
    if (camPhased_) {
        // Pleine charge : le meilleur calage du calcul. Pied levé et au ralenti : en butée retard, le moins de croisement possible.
        // Entre les deux, selon le remplissage. La palette tourne à vitesse finie.
        const double best = camPhaserTargetDeg(powertrain_.engine, rpmNow, camDrive_.high);
        const double share = clamp((manifold_ - kCamPhaserPartLoad) / (1.0 - kCamPhaserPartLoad), 0.0, 1.0);
        const double target = -kCamPhaserAuthorityDeg + (best + kCamPhaserAuthorityDeg) * share;
        camDrive_.phaserDeg = static_cast<float>(approach(static_cast<double>(camDrive_.phaserDeg), target, kCamPhaserDegPerS * kStep));
    }
}

double DrivingModel::inputDrag() const {
    const GearboxSpec& box = gearing_.box;
    return box.dragNm + box.dragPerRad * inputOmega_;
}

void DrivingModel::trackOverload(double torque) {
    gearTorque_ = torque;
    const double limit = gearing_.box.limitNm;
    // Le couple change de sens : les dents traversent leur jeu, et le flanc d'en face prend le choc. Il vaut ce que le
    // moteur a eu le temps de donner au primaire pendant la traversée : J·α·θ. Bien plus sur de larges crabots.
    const bool nowDriving = torque > 0.0;
    if (nowDriving != drivingFlank_ && std::fabs(torque) > 0.03 * limit) {
        drivingFlank_ = nowDriving;
        const double lash = dogBox() ? 0.25 : 0.006;
        impacts_ += gearing_.box.inputInertia * std::fabs(torque_) / inertia_ * lash;
    }
    overload_ = std::fabs(torque) / std::max(limit, 1.0);
    if (overload_ > 1.0) {
        // Règle de Miner : une pente raide en fatigue, ici la puissance sixième.
        const double o = overload_ * overload_ * overload_;
        wear_ += o * o / kWearRefS * kStep;
        if (wear_ >= 1.0 && !broken_ && !powertrain_.electric) {
            // Une dent cède : plus rien ne passe, et le choc s'entend.
            broken_ = true;
            impacts_ += 0.5 * gearing_.box.inputInertia * inputOmega_ * inputOmega_;
            meshed_ = false;
            gear_ = 0;
            pendingGear_ = 0;
            step_ = ShiftStep::None;
            locked_ = false;
        }
    }
}

/**
 * Un rapport en prise. Le primaire et la sortie de boîte tournent ensemble, l'embrayage les relie au moteur, et les
 * arbres de roue se tordent entre la sortie et les roues. Ce ressort, chargé de l'inertie du moteur vue par le
 * rapport, donne les à-coups : de 2 à 3 Hz en première, jusqu'à 10 Hz sur les hauts rapports.
 */
void DrivingModel::drivenStep(const TyreContact& tyre, double wheelInertia) {
    const double g = ratio(gear_);
    const double J = gearing_.box.inputInertia;
    const double drag = inputDrag();
    const double reflected = (locked_ ? inertia_ + J : J) * g * g * efficiency_;
    // Assez amorti pour s'éteindre en deux ou trois battements, sans jamais dépasser ce que le pas de 1 ms sait intégrer.
    // Embrayage ouvert, l'huile, les roulements et les pignons freinent seuls le primaire : il ne reste presque plus d'inertie.
    const double damping = std::min(locked_ ? kShaftDamping : 2.5 * kShaftDamping, 0.6 * reflected / kStep);
    const double shaft = kShaftStiffness * twist_ + damping * (inputOmega_ / g - wheelOmega_);
    const double onInput = shaft / (g * efficiency_);
    const double capacity = clutchCapacity_ * clutch_;
    double transmitted = 0.0;
    if (locked_) {
        const double alpha = (torque_ - drag - onInput) / (inertia_ + J);
        transmitted = torque_ - inertia_ * alpha;
        if (std::fabs(transmitted) > capacity * 1.05) {
            locked_ = false;
        } else {
            omega_ = std::max(omega_ + alpha * kStep, 0.0);
            inputOmega_ = omega_;
            clutchSlipKw_ = 0.0;
        }
    }
    if (!locked_) {
        const double difference = omega_ - inputOmega_;
        transmitted = difference > 0.0 ? capacity : -capacity;
        const double engineNext = omega_ + (torque_ - transmitted) / inertia_ * kStep;
        const double inputNext = inputOmega_ + (transmitted - drag - onInput) / J * kStep;
        const double after = engineNext - inputNext;
        if (capacity > 1.0 && (after > 0.0) != (difference > 0.0)) {
            // Les deux disques ont rejoint la même vitesse : l'embrayage colle.
            const double shared = (inertia_ * engineNext + J * inputNext) / (inertia_ + J);
            omega_ = std::max(shared, 0.0);
            inputOmega_ = omega_;
            locked_ = clutch_ > 0.5;
        } else {
            omega_ = std::max(engineNext, 0.0);
            inputOmega_ = std::max(inputNext, 0.0);
        }
        clutchSlipKw_ = std::fabs(difference * transmitted) / 1000.0;
    }
    const double soft = tyre.perWheelMs * radius_ * radius_;
    wheelOmega_ = std::max(wheelOmega_ + (shaft - tyre.forceN * radius_) * kStep / (wheelInertia + kStep * soft), 0.0);
    twist_ += (inputOmega_ / g - wheelOmega_) * kStep;
    trackOverload(transmitted - drag);
}

/** Aucun rapport en prise : le primaire ne dépend que de l'embrayage, et des cônes s'il y en a. */
void DrivingModel::freeStep(const TyreContact& tyre, double wheelInertia) {
    const GearboxSpec& box = gearing_.box;
    const double J = box.inputInertia;
    const double drag = inputDrag();
    double cone = 0.0;
    double coneOnWheels = 0.0;
    coneTorque_ = 0.0;
    if (step_ == ShiftStep::Sync && pendingGear_ != 0) {
        const double g = ratio(pendingGear_);
        const double target = wheelOmega_ * g;
        const double difference = target - inputOmega_;
        // À la montée, les pertes aident le cône ; à la descente, elles s'y opposent (Socin et Walters).
        const double available = pendingGear_ > 0 ? box.coneTorqueFor(pendingGear_) * shiftEffort_ : 0.0;
        cone = clamp(difference * J / kStep, -available, available);
        coneTorque_ = std::fabs(cone);
        // La réaction sur les roues, négligeable, mais physique.
        coneOnWheels = cone * g * efficiency_;
    }
    if (crankLeft_ > 0.0) {
        // Le démarreur fixe seul le régime pendant le lancement : le primaire suit s'il est embrayé, sans rien changer.
        locked_ = false;
        inputOmega_ = clutch_ > 0.5 ? omega_ : std::max(inputOmega_ - drag / J * kStep, 0.0);
        gearTorque_ = 0.0;
        clutchSlipKw_ = 0.0;
    } else {
        const double capacity = clutchCapacity_ * clutch_;
        if (locked_) {
            const double alpha = (torque_ + cone - drag) / (inertia_ + J);
            const double transmitted = torque_ - inertia_ * alpha;
            if (std::fabs(transmitted) > capacity * 1.05) {
                locked_ = false;
            } else {
                omega_ = std::max(omega_ + alpha * kStep, 0.0);
                inputOmega_ = omega_;
                clutchSlipKw_ = 0.0;
            }
        }
        if (!locked_) {
            const double difference = omega_ - inputOmega_;
            const double transmitted = difference > 0.0 ? capacity : -capacity;
            const double engineNext = omega_ + (torque_ - transmitted) / inertia_ * kStep;
            const double inputNext = inputOmega_ + (transmitted + cone - drag) / J * kStep;
            const double after = engineNext - inputNext;
            if (capacity > 0.1 && (after > 0.0) != (difference > 0.0)) {
                const double shared = (inertia_ * engineNext + J * inputNext) / (inertia_ + J);
                omega_ = std::max(shared, 0.0);
                inputOmega_ = omega_;
                locked_ = clutch_ > 0.5;
            } else {
                omega_ = std::max(engineNext, 0.0);
                inputOmega_ = std::max(inputNext, 0.0);
            }
            clutchSlipKw_ = std::fabs(difference * transmitted) / 1000.0;
        }
        gearTorque_ = 0.0;
        overload_ = 0.0;
    }
    const double soft = tyre.perWheelMs * radius_ * radius_;
    wheelOmega_ = std::max(wheelOmega_ - (coneOnWheels + tyre.forceN * radius_) * kStep / (wheelInertia + kStep * soft), 0.0);
}

/**
 * Le convertisseur : une pompe liée au moteur, une turbine à l'entrée de la boîte. La pompe absorbe (ω/K)², et la
 * turbine rend ce couple multiplié tant que les deux glissent : c'est ce qui fait décoller sans embrayage.
 */
void DrivingModel::converterTorque(double& pump, double& turbine) const {
    const double ratioNow = omega_ > 1.0 ? clamp(inputOmega_ / omega_, 0.0, 1.2) : 1.0;
    const double absorb = omega_ / converterK_;
    // La capacité chute quand les deux roues tournent à la même vitesse.
    const double factor = ratioNow < 0.85 ? 1.0 : std::max((1.0 - ratioNow) / 0.15, 0.0);
    pump = absorb * absorb * factor;
    const double multiply = ratioNow < 0.85 ? 2.0 - ratioNow / 0.85 : 1.0;
    // Pendant un passage, les embrayages internes glissent : le choc ne va pas jusqu'aux dents.
    const double torqueCap = clutchCapacity_ * (step_ == ShiftStep::Handover ? 1.0 : 1.25);
    turbine = std::min(pump * multiply, torqueCap);
}

void DrivingModel::converterStep(const TyreContact& tyre, double wheelInertia) {
    const double J = gearing_.box.inputInertia;
    const double drag = inputDrag();
    double pump = 0.0, turbine = 0.0;
    converterTorque(pump, turbine);
    const bool driving = meshed_ && gear_ != 0;
    const double g = driving ? ratio(gear_) : 1.0;
    const double soft = tyre.perWheelMs * radius_ * radius_;
    double transmitted = turbine;
    if (locked_) {
        const double wheelSide = wheelInertia / (g * g * efficiency_) + J;
        const double loadOnEngine = tyre.forceN * radius_ / (g * efficiency_) + drag;
        const double alpha = (torque_ - loadOnEngine) / (inertia_ + wheelSide + kStep * soft / (g * g * efficiency_));
        transmitted = torque_ - inertia_ * alpha;
        if (driving && std::fabs(transmitted) <= clutchCapacity_ * clutch_) {
            omega_ = std::max(omega_ + alpha * kStep, 0.0);
            wheelOmega_ = omega_ / g;
            inputOmega_ = omega_;
            trackOverload(transmitted - drag);
            return;
        }
        locked_ = false;
    }
    omega_ = std::max(omega_ + (torque_ - pump) / inertia_ * kStep, 0.0);
    if (driving) {
        const double wheelNext = wheelOmega_ + ((turbine - drag) * g * efficiency_ - tyre.forceN * radius_) * kStep / (wheelInertia + J * g * g * efficiency_ + kStep * soft);
        wheelOmega_ = std::max(wheelNext, 0.0);
        inputOmega_ = wheelOmega_ * g;
        trackOverload(turbine - drag);
        clutchSlipKw_ = 0.0;
        // Le pontage referme : les deux roues du convertisseur se rejoignent, sans perdre leur élan.
        if (clutch_ > 0.9) {
            const double wheelSide = wheelInertia / (g * g * efficiency_) + J;
            const double shared = (inertia_ * omega_ + wheelSide * inputOmega_) / (inertia_ + wheelSide);
            omega_ = shared;
            inputOmega_ = shared;
            wheelOmega_ = shared / g;
            locked_ = true;
        }
    } else {
        inputOmega_ = std::max(inputOmega_ + (turbine - drag) / J * kStep, 0.0);
        wheelOmega_ = std::max(wheelOmega_ - tyre.forceN * radius_ * kStep / (wheelInertia + kStep * soft), 0.0);
        gearTorque_ = 0.0;
    }
}

GearboxDrive DrivingModel::gearboxDrive() const {
    GearboxDrive d;
    const GearboxSpec& box = gearing_.box;
    const bool driving = meshed_ && gear_ != 0;
    const double overallFinal = box.finalDrive > 0.0 ? box.finalDrive : 1.0;
    d.inputOmega = static_cast<float>(powertrain_.electric ? omega_ : inputOmega_);
    d.outputOmega = static_cast<float>(wheelOmega_ * overallFinal);
    d.torqueNm = static_cast<float>(powertrain_.electric ? torque_ : (driving ? gearTorque_ : 0.0));
    d.firings = powertrain_.engine.firingsPerTwoTurns();
    d.gear = driving ? gear_ : 0;
    d.running = omega_ > 1.0 && !cut_ && crankLeft_ <= 0.0 && !stalled_ && !powertrain_.electric;
    d.coneSlip = static_cast<float>(coneSlip_);
    d.coneTorque = static_cast<float>(coneTorque_);
    d.grind = static_cast<float>(grinding_);
    d.clutchSlip = static_cast<float>(clutchSlipKw_);
    d.pedal = static_cast<float>(clutchPedal_);
    d.impacts = impacts_;
    d.selectorClicks = selectorClicks_;
    if (d.running && (locked_ || clutch_ > 0.7) && box.kind != GearboxKind::Converter) {
        // Ce que chaque explosion impose au primaire : le couple rapporté au volant, passé par l'amortisseur du disque.
        // Résonance de 40 à 80 Hz sous charge (LuK), plus bas avec le pré-amortisseur au point mort.
        double firesHz = toRpm(omega_) / 120.0 * powertrain_.engine.firingsPerTwoTurns();
        double swing = (rippleIdleNm_ + rippleNm_ * manifold_) / inertia_;
        // Des explosions manquent : moins de coups, plus espacés, et le volant a plus de temps pour ralentir entre deux.
        if (antiLag_.active && antiLag_.kind == AntiLag::CylinderCut) {
            firesHz *= kAntiLagFiredShare;
            swing /= kAntiLagFiredShare;
        }
        const double resonance = driving ? 55.0 : 22.0;
        const double r = firesHz / resonance;
        const double damping = 2.0 * 0.15 * r;
        const double spring = 1.0 - r * r;
        double through = 1.0 / std::sqrt(spring * spring + damping * damping);
        // Le double volant isole presque tout au-dessus de sa résonance, sous le ralenti.
        if (box.flywheel == Flywheel::DualMass) through = std::min(through, 0.12);
        d.acyclism = static_cast<float>(swing * through);
    }
    return d;
}

void DrivingModel::integrate(double brakePedal) {
    const CarConfig& car = powertrain_.car;
    const BodySpec& body = bodySpec(car.body);
    const double mass = static_cast<double>(car.massKg);
    const double totalMass = mass + trailerKg_;
    const double wheelInertia = kWheelInertia * (car.drive == Drive::All ? 2.0 : 1.0);

    // Motricité : le poids passe sur l'arrière à l'accélération.
    // Une côte charge l'arrière comme une accélération : c'est la gravité qui tire.
    const double transfer = mass * (acceleration_ + kGravity * slopeShare()) * body.cgHeightM / body.wheelbaseM;
    const double frontShare = frontShareOf(car);
    const double frontLoad = mass * kGravity * frontShare - transfer;
    const double rearLoad = mass * kGravity * (1.0 - frontShare) + transfer;
    double drivenLoad = mass * kGravity;
    if (car.drive == Drive::Front) drivenLoad = frontLoad;
    else if (car.drive == Drive::Rear) drivenLoad = rearLoad;
    // Le pneu (tyre.hpp) : sa force à ce glissement, avec son sommet, et sa raideur. À basse vitesse, elle se compte en dizaines
    // de kN par m/s : la roue l'intègre implicitement (linéarisée à ce pas), sans quoi elle oscillerait d'un pas à l'autre.
    const TyreContact tyre = tyreContact(kTyreGrip * std::max(drivenLoad, 0.0), wheelSpeedMs(), speedMs_);
    const double wheelBefore = wheelOmega_;

    if (powertrain_.electric) {
        const double g = ratio(1);
        const double reflected = g * g * efficiency_;
        const double wheelSide = wheelInertia / reflected;
        const double loadOnEngine = tyre.forceN * radius_ / (g * efficiency_);
        const double soft = tyre.perWheelMs * radius_ * radius_ / reflected;
        const double alpha = (torque_ - loadOnEngine) / (inertia_ + wheelSide + kStep * soft);
        omega_ = std::max(omega_ + alpha * kStep, 0.0);
        wheelOmega_ = omega_ / g;
        inputOmega_ = omega_;
        gearTorque_ = torque_ - inertia_ * alpha;
    } else if (gearing_.box.kind == GearboxKind::Converter) {
        converterStep(tyre, wheelInertia);
    } else if (meshed_ && gear_ != 0) {
        drivenStep(tyre, wheelInertia);
    } else {
        freeStep(tyre, wheelInertia);
    }

    if (!powertrain_.electric && !cut_ && crankLeft_ <= 0.0 && meshed_ && gear_ != 0 && gearing_.box.kind != GearboxKind::Converter &&
        toRpm(omega_) < idle_ * (manual_ ? kStallShare : 0.8)) {
        if (manual_) {
            // En manuelle, rien ne protège le moteur : si l'embrayage le relie aux roues, il cale.
            // La première reste assistée au démarrage, pour pouvoir partir au clavier.
            if (locked_ || (std::abs(gear_) >= 2 && clutch_ > 0.25)) stalled_ = true;
        } else {
            // L'embrayage piloté d'une boîte automatique s'ouvre avant que le moteur ne cale.
            locked_ = false;
            clutch_ = std::min(clutch_, 0.15);
        }
    }
    // Un moteur qui s'éteint le dit, quelle que soit la boîte : sans cela, plus rien ne propose de le relancer.
    if (!powertrain_.electric && !stalled_ && !cut_ && crankLeft_ <= 0.0 && toRpm(omega_) < 2.0 * kStopRpm) stalled_ = true;
    if (toRpm(omega_) < kStopRpm && cut_ && crankLeft_ <= 0.0 && !powertrain_.electric) omega_ = 0.0;

    // La force que la roue a reçue, à sa vitesse de fin de pas : la caisse reçoit la même, et rien ne se perd entre les deux.
    tyreUsed_ = tyre.forceN + tyre.perWheelMs * radius_ * (wheelOmega_ - wheelBefore);
    const double stopping = brakePedal * kTyreGrip * totalMass * kGravity;
    // À l'arrêt, freins serrés, la pente ne fait pas partir la voiture. Elle ne recule jamais : la vitesse reste positive (voir docs/modele.md).
    const double extra = speedMs_ <= 0.0 && brakePedal > 0.02 ? std::max(roadExtraN(), 0.0) : roadExtraN();
    const double force = tyreUsed_ - roadLoadN(powertrain_, speedMs_) - extra - (speedMs_ > 0.0 ? stopping : 0.0);
    acceleration_ = clamp(force / totalMass, -12.0, 12.0);
    speedMs_ = std::max(speedMs_ + force / totalMass * kStep, 0.0);
    // À l'arrêt, freins serrés : les roues ne tournent plus.
    if (speedMs_ <= 0.0 && brakePedal > 0.02 && !meshed_) wheelOmega_ = 0.0;
    if (!meshed_) wheelOmega_ += (speedMs_ / radius_ - wheelOmega_) * std::min(1.0, kStep / 0.05);
}

// ── Le calculateur : voir ecu.hpp et « Le calculateur » dans docs/modele.md ──

/**
 * Ce que le calculateur décide à ce pas : le launch control, l'antipatinage, le rupteur progressif, le passage à fond.
 * Renvoie le régime de la coupure franche : celui du launch control s'il tient le moteur, sinon le régime maximal.
 */
double DrivingModel::stepEcu(double throttle, double rpmNow, bool off) {
    EcuAction a;
    if (flatUpshift_ && step_ == ShiftStep::None) flatUpshift_ = false;

    // Launch control : armé à l'arrêt, pied au plancher, moteur désaccouplé. Il lâche dès que la voiture roule, ou que
    // l'embrayage a rattrapé le moteur : l'embrayage se ferme alors sur tout le couple, depuis ce régime.
    if (launchRpm_ > 0.0) {
        const bool floored = throttle >= kLaunchPedal && !off && !catching_ && !stalled_;
        const bool caught = meshed_ && gear_ != 0 && inputOmega_ >= kLaunchCaughtShare * omega_;
        if (!floored) launchHold_ = false;
        else if (!launchHold_) launchHold_ = speedMs_ < kLaunchArmMs && clutch_ < 0.5;
        else if (caught || speedMs_ >= kLaunchArmMs) launchHold_ = false;
    }

    // Antipatinage : le glissement que calcule déjà le modèle, ramené vers sa cible par une action proportionnelle et intégrale :
    // il retarde l'allumage, puis coupe des cylindres. Un embrayage qui patine ne transmet que sa capacité, quoi que fasse
    // l'allumage : il n'agit donc que roues tenues par le moteur, ou au départ, où le robot ne serre l'embrayage que jusqu'au
    // couple que le moteur annonce (`updateClutch`), ce que le calculateur retire compris, comme une boîte pilotée liée à l'ESP.
    if (tractionTarget_ > 0.0) {
        const bool coupled = powertrain_.electric || locked_ || clutch_ > 0.9 || gearing_.box.kind == GearboxKind::Converter;
        const bool driving = (coupled || robotLaunching()) && meshed_ && gear_ > 0 && throttle > 0.05 && !off && !cut_;
        // La cible est un glissement κ (tyre.hpp) ; l'écart se compte en m/s, celui que l'inertie de la roue intègre : le même
        // retrait de couple corrige le même écart de vitesse quelle que soit l'allure.
        const double error = wheelSlip() - tractionTarget_ * std::max(speedMs_, kTyreLowSpeedMs);
        double kp = 0.0, ki = 0.0;
        tractionGains(kp, ki);
        const double proportional = kp * error;
        // L'intégrale ne grossit pas dans le sens où la commande sature déjà : sans quoi, une fois le glissement retombé,
        // elle tiendrait le moteur coupé le temps de se vider, et il calerait presque (anti-emballement).
        const bool saturated = (proportional + tractionIntegral_ >= 1.0 && error > 0.0) || (proportional + tractionIntegral_ <= 0.0 && error < 0.0);
        if (!driving) tractionIntegral_ = 0.0;
        else if (!saturated) tractionIntegral_ = clamp(tractionIntegral_ + ki * error * kStep, 0.0, 1.0);
        a.traction = driving ? clamp(proportional + tractionIntegral_, 0.0, 1.0) : 0.0;
    }

    double retard = 0.0;
    double fired = 1.0;
    // Le rupteur progressif : du retard sur les derniers 3 %, puis des cylindres coupés au-delà du régime maximal.
    if (limiterKind_ == RevLimiter::Soft && !launchHold_) {
        const double start = limiterRpm_ * (1.0 - kSoftRetardBand);
        if (rpmNow > start) {
            a.limiter = true;
            if (sparkEngine_) retard += kSoftRetardDeg * clamp((rpmNow - start) / (limiterRpm_ * kSoftRetardBand), 0.0, 1.0);
            fired *= 1.0 - clamp((rpmNow - limiterRpm_) / (limiterRpm_ * kSoftCutBand), 0.0, 1.0);
        }
    }
    // Le launch control retarde l'étincelle tant qu'il tient : moins de couple, et le travail perdu part dans le gaz.
    if (launchHold_ && sparkEngine_) retard += kLaunchRetardDeg;
    // L'antipatinage retarde d'abord, puis coupe des cylindres. Sur une voiture électrique, `updateEngine` retire le couple.
    if (a.traction > 0.0 && !powertrain_.electric) {
        double tractionRetard = 0.0;
        double tractionFired = 1.0;
        tractionActuation(1.0 - a.traction, sparkEngine_ ? kTractionMaxRetardDeg : 0.0, tractionRetard, tractionFired);
        retard += tractionRetard;
        fired *= tractionFired;
    }

    // Le passage à fond : à la montée, pied au plancher, l'allumage coupé tient le moteur au régime du rapport suivant.
    flatCut_ = flatUpshift_ && step_ != ShiftStep::None && pendingGear_ > 0 && rpmNow > rpmInGear(pendingGear_);

    a.retardDeg = retard;
    a.firedShare = fired;
    a.launch = launchHold_;
    a.flat = flatCut_;
    ecu_ = a;
    ecuShare_ = retard == 0.0 && fired == 1.0 ? 1.0 : ecuActionShare(a, mapRetard_);
    return launchHold_ ? launchRpm_ : limiterRpm_;
}

bool DrivingModel::robotLaunching() const {
    // La branche du démarrage de `updateClutch` : première, sous 25 km/h, l'embrayage patine encore.
    return !powertrain_.electric && gearing_.box.kind != GearboxKind::Converter && step_ == ShiftStep::None && meshed_ && gear_ == 1 && !locked_ &&
           speedKmh() < 25.0;
}

double DrivingModel::slipPlantGain() const {
    // Ce que tout le couple du moteur ferait au glissement, en m/s² : r·T·g·η sur l'inertie vue de la roue (les roues, le
    // primaire et le moteur, ramenés par le rapport).
    const double g = ratio(std::max(gear_, 1));
    const double reflected = g * g * efficiency_;
    const double wheels = kWheelInertia * (powertrain_.car.drive == Drive::All ? 2.0 : 1.0);
    const double inertia = wheels + (gearing_.box.inputInertia + inertia_) * reflected;
    return std::max(radius_ * tractionTorqueNm_ * g * efficiency_ / inertia, 1.0);
}

void DrivingModel::tractionGains(double& kp, double& ki) const {
    // La boucle, un intégrateur de `slipPlantGain`, a ses pôles à ω et ζ : ω² = ki·gain, 2ζω = kp·gain. Le pneu y ajoute son
    // amortissement sous le sommet, et une instabilité au-delà.
    const double gain = slipPlantGain();
    kp = 2.0 * kTractionDamping * kTractionOmega / gain;
    ki = kTractionOmega * kTractionOmega / gain;
}

/**
 * Une fois la coupure connue : si le carburant passe encore, ce qu'il emporte vers la ligne, et le gaz qui peut l'enflammer.
 * Sur un turbo, c'est le gaz de `TurboCharger` ; sinon, le même modèle, suivi ici avec la même inertie des parois.
 */
void DrivingModel::finishEcu(double rpmNow, double indicated, bool off) {
    // La coupure d'allumage du rupteur ou du launch control, le passage à fond ; et sur un carburateur, qui ne sait pas couper
    // l'essence, les cylindres que le calculateur coupe. Le rupteur d'origine d'un carburateur reste celui d'origine.
    const bool sparkCut = (limiterCut_ && (limiterKind_ == RevLimiter::IgnitionCut || launchHold_)) || flatCut_ || (ignitionCut_ && flatUpshift_);
    fuelOnCut_ = sparkEngine_ && !off && (sparkCut || (carbEngine_ && ecu_.firedShare < 1.0));
    if (ecuEngine_) {
        ecu_.limiter = ecu_.limiter || (limiterCut_ && !launchHold_);
        ecu_.flat = flatCut_ || (ignitionCut_ && flatUpshift_);
    }

    const bool diesel = powertrain_.engine.burnsDiesel();
    exhaustShare_ = exhaustHeatShare(diesel, redline_, rpmNow, manifold_);
    if (turboEngine_) {
        ignition_ = antiLagIgnition(turbo_.collectorK());
    } else if (!powertrain_.electric) {
        // Le gaz à la sortie des cylindres : la carte, ce que le pas retarde ou coupe, et ce qui brûle encore au collecteur.
        const double rise = exhaustHeatRiseK(diesel, manifold_);
        const bool burning = !cut_ || fuelOnCut_;
        const double normal = burning ? kPortIntakeK + exhaustShare_ * rise : kPortIntakeK + 60.0;
        EcuExhaust gas = ecuMapExhaust(powertrain_.engine);
        gas.retardDeg = ecu_.retardDeg;
        gas.firedShare = cut_ ? 0.0 : ecu_.firedShare;
        gas.fuelOnCut = fuelOnCut_;
        const double target = burning && (gas.active() || cut_) ? ecuExhaustK(gas, kPortIntakeK, normal, rise, manifold_, antiLagIgnition(portK_)) : normal;
        portK_ += (target - portK_) * kStep / (target > portK_ ? kPortHeatS : kPortCoolS);
        ignition_ = antiLagIgnition(portK_);
    }

    // Le mélange d'un cylindre coupé : ce qui peut en brûler au collecteur, à chaque ouverture (J).
    unburntJoules_ = 0.0;
    if (fuelOnCut_) {
        const double air = consumption_.model().airGps(rpmNow, manifold_, indicated);
        const double events = std::max(rpmNow / 120.0 * powertrain_.engine.firingsPerTwoTurns(), 1.0);
        const FuelSpec& fuel = consumption_.model().fuel();
        const double grams = air / events / (fuel.stoichiometricAfr * demandedLambda(powertrain_.engine, manifold_, coldCoupled_ ? thermal_.coolantC() : kWarmCoolantC));
        unburntJoules_ = grams * fuel.lowerHeatingJPerG * kUnburntCollectorShare;
    }
}

EcuDrive DrivingModel::ecuDrive() const {
    EcuDrive d;
    if (!ecuEngine_ && !ecuMap_) return d;
    EcuExhaust gas = ecuMapExhaust(powertrain_.engine);
    gas.retardDeg = ecu_.retardDeg;
    const double evo = sparkEngine_ ? ecuEvoRatio(gas, exhaustShare_, manifold_) : 1.0;
    const bool cutting = cut_ && fuelOnCut_;
    if (evo == 1.0 && ecu_.firedShare >= 1.0 && !cutting) return d;
    d.active = true;
    d.evoRatio = static_cast<float>(evo);
    d.firedShare = static_cast<float>(ecu_.firedShare);
    d.fuelOnCut = fuelOnCut_;
    d.unburntJoules = static_cast<float>(unburntJoules_);
    d.ignition = static_cast<float>(ignition_);
    return d;
}

double DrivingModel::ecuAdvanceDeg() const {
    if (!sparkEngine_) return 0.0;
    // Ce que retire le capteur de cliquetis (combustion) s'y ajoute : 0 sans carte.
    return static_cast<double>(powertrain_.engine.ignitionOffsetDeg) - ecu_.retardDeg - knockRetard_;
}

double DrivingModel::ecuLambda() const {
    if (!sparkEngine_) return 0.0;
    // Un carburateur dose lui-même : c'est son mélange qu'on lit.
    if (carbEngine_) return carb_.lambda();
    // Moteur froid : l'enrichissement de la thermique, la même loi (`coldEnrichment`).
    return demandedLambda(powertrain_.engine, manifold_, coldCoupled_ ? thermal_.coolantC() : kWarmCoolantC);
}

}  // namespace revforge
