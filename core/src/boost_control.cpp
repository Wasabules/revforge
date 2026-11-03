#include "revforge/boost_control.hpp"

#include <cmath>

#include "revforge/powertrain.hpp"
#include "revforge/twin_turbo.hpp"

namespace revforge {

namespace {

constexpr double kStep = 0.001;
// La wastegate à ressort de `TurboCharger` : mêmes constantes que turbo.cpp (kWastegateBand, kWastegateLeadS).
constexpr double kSpringBand = 0.12;
constexpr double kSpringLeadS = 0.12;
// Le filtre de la dérivée : N = 10, l'usage courant (Åström et Hägglund).
constexpr double kDerivativeFilter = 10.0;
// Le banc : un échelon d'un point d'ouverture, assez petit pour rester linéaire.
constexpr double kBenchStep = 0.01;
constexpr double kBenchWindowS = 3.0;
// Où l'on mesure l'arbre, en part du régime maximal : là où la wastegate travaille.
constexpr double kBenchShares[4] = {0.4, 0.55, 0.7, 0.88};
// Une wastegate qui n'a presque rien à jeter ne dit rien de sa pente : on ne la mesure pas là.
constexpr double kBenchMinGate = 0.04;
// La protection des roues se cherche bien au-dessus de la cible : là où le turbo pourrait encore pousser sans elle.
constexpr double kProtectProbeShare = 1.6;
constexpr double kProtectProbeBar = 0.5;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

int ticks(double seconds, double dt) {
    const int n = static_cast<int>(seconds / dt + 0.5);
    return n < 1 ? 1 : n;
}

/** L'ouverture d'équilibre pour cette cible, entre les deux tables : la turbine travaille en rapport de pression. */
double feedForwardGate(const BoostControlSpec& spec, double rpm, double targetPa, double ambientPa) {
    const double half = spec.referencePa + 0.5 * spec.boostBar * 1e5;
    const double ratio = targetPa / ambientPa;
    const double ratioHalf = half / spec.referencePa;
    const double ratioFull = spec.calibratedPa / spec.referencePa;
    const double share = (ratio - ratioHalf) / (ratioFull - ratioHalf);
    const double atHalf = boostGridAt(spec, spec.gateAtHalf, rpm);
    const double atFull = boostGridAt(spec, spec.gateAtCalibrated, rpm);
    return clamp(atHalf + share * (atFull - atHalf), 0.0, 1.0);
}

BoostPid pidAt(const BoostControlSpec& spec, double rpm) {
    const double at = clamp((rpm - spec.firstRpm) / spec.stepRpm, 0.0, static_cast<double>(kBoostGrid - 1));
    const int i = static_cast<int>(at);
    const int j = i + 1 < kBoostGrid ? i + 1 : i;
    const double share = at - static_cast<double>(i);
    const BoostPid& a = spec.pid[static_cast<std::size_t>(i)];
    const BoostPid& b = spec.pid[static_cast<std::size_t>(j)];
    BoostPid out;
    out.kc = a.kc + share * (b.kc - a.kc);
    out.tauI = a.tauI + share * (b.tauI - a.tauI);
    out.tauD = a.tauD + share * (b.tauD - a.tauD);
    return out;
}

}  // namespace

// ── Les fonctions pures ──

double boostGridAt(const BoostControlSpec& spec, const std::array<double, kBoostGrid>& table, double rpm) {
    const double at = clamp((rpm - spec.firstRpm) / spec.stepRpm, 0.0, static_cast<double>(kBoostGrid - 1));
    const int i = static_cast<int>(at);
    const int j = i + 1 < kBoostGrid ? i + 1 : i;
    const double share = at - static_cast<double>(i);
    return table[static_cast<std::size_t>(i)] + share * (table[static_cast<std::size_t>(j)] - table[static_cast<std::size_t>(i)]);
}

double boostTargetPa(const BoostControlSpec& spec, double rpm, int gear, double ambientPa) {
    // La membrane d'une wastegate à ressort respire l'air ambiant : elle tient une pression relative.
    if (spec.kind == BoostControl::Spring) return ambientPa + spec.boostBar * 1e5;
    // Le capteur du calculateur mesure une pression absolue : il vise la même, quelle que soit l'altitude…
    double target = spec.calibratedPa;
    // …sauf si le compresseur dépassait alors la vitesse que ses roues supportent : c'est un rapport de pression, l'air ambiant le fixe.
    const double protect = boostGridAt(spec, spec.protectPa, rpm) / spec.referencePa * ambientPa;
    if (protect < target) target = protect;
    if (spec.kind == BoostControl::ByGear && spec.gears > 0) {
        // Au point mort ou en marche arrière, le calculateur ne sait pas quel couple passera : il prend la première.
        const int g = gear < 1 ? 1 : (gear > spec.gears ? spec.gears : gear);
        const double cap = boostGridAt(spec, spec.gearCapPa[static_cast<std::size_t>(g - 1)], rpm);
        if (cap < target) target = cap;
    }
    // Une wastegate ne fait pas de vide : au plus bas, la turbine ne pousse plus rien.
    return target < ambientPa ? ambientPa : target;
}

double boostTargetGaugeBar(const BoostControlSpec& spec, double rpm, int gear, double ambientPa) {
    return (boostTargetPa(spec, rpm, gear, ambientPa) - ambientPa) / 1e5;
}

double springGateDemand(double targetBar, double chargePa, double chargeRisePaS, double ambientPa) {
    const double anticipated = chargePa + kSpringLeadS * chargeRisePaS;
    return clamp((anticipated - (ambientPa + (1.0 - 0.2 * kSpringBand) * targetBar * 1e5)) / (kSpringBand * targetBar * 1e5), 0.0, 1.0);
}

double effectiveDuty(const BoostActuatorSpec& actuator, double duty) {
    const double period = 1.0 / actuator.pwmHz;
    const double on = clamp(duty, 0.0, 1.0) * period;
    // L'impulsion est plus courte que l'ouverture : l'électrovanne ne décolle pas. La pause plus courte que la fermeture : elle reste ouverte.
    if (on <= actuator.openS) return 0.0;
    if (period - on <= actuator.closeS) return 1.0;
    return clamp((on - actuator.openS + actuator.closeS) / period, 0.0, 1.0);
}

double gateForVacuum(double vacuumShare) { return clamp(1.0 - vacuumShare, 0.0, 1.0); }

double dutyForGate(const BoostActuatorSpec& actuator, double gate) {
    const double vacuum = 1.0 - clamp(gate, 0.0, 1.0);
    if (vacuum <= 0.0) return 0.0;
    if (vacuum >= 1.0) return 1.0;
    const double shift = (actuator.closeS - actuator.openS) * actuator.pwmHz;
    const double low = actuator.openS * actuator.pwmHz;
    const double high = 1.0 - actuator.closeS * actuator.pwmHz;
    return clamp(vacuum - shift, low, high);
}

double boostLoopDelayS(const BoostPlant& plant, const BoostActuatorSpec& actuator) {
    // Un bloqueur d'ordre zéro retarde en moyenne d'une demi-période : la tâche, puis le découpage.
    return plant.deadS + 0.5 * actuator.taskS + 0.5 / actuator.pwmHz;
}

BoostPid simcPid(const BoostPlant& plant, const BoostActuatorSpec& actuator) {
    const double theta = boostLoopDelayS(plant, actuator);
    // Skogestad : τc = θ, le réglage serré mais robuste. Forme série, la dérivée compense le tuyau de la membrane.
    const double closedLoop = theta;
    const double kc = plant.tauS / (plant.gainPa * (closedLoop + theta));
    const double fourTimes = 4.0 * (closedLoop + theta);
    const double tauI = plant.tauS < fourTimes ? plant.tauS : fourTimes;
    const double tauD = actuator.chamberS;
    // De la forme série à la forme idéale.
    BoostPid pid;
    pid.kc = kc * (1.0 + tauD / tauI);
    pid.tauI = tauI + tauD;
    pid.tauD = tauI * tauD / (tauI + tauD);
    return pid;
}

namespace {

/**
 * Le banc, quel que soit le montage : `stepAt(gate)` avance l'arbre d'un pas, soupape(s) à cette ouverture, et rend la pression de
 * charge. Tenu `settleS` à `gate`, puis un échelon d'un point.
 */
template <class StepAt>
BoostPlant identifyPlant(StepAt&& stepAt, double rpm, double gate, double settleS) {
    const int settle = ticks(settleS, kStep);
    double before = 0.0;
    for (int i = 0; i < settle; ++i) before = stepAt(gate);
    // On ferme d'un point si l'on peut, sinon on ouvre : la pente est la même au premier ordre.
    const double step = gate >= kBenchStep ? -kBenchStep : kBenchStep;
    const int window = ticks(kBenchWindowS, kStep);
    // Trois points également espacés de la fin : la queue d'une exponentielle, que le Δ² d'Aitken prolonge jusqu'au bout.
    const int third = window / 4;
    double p1 = 0.0, p2 = 0.0, p3 = 0.0;
    // Les instants où la réponse passe 28 % et 63 % : il faut la valeur finale, donc on garde la trace.
    static double trace[4096];
    const int kept = window < 4096 ? window : 4096;
    for (int i = 0; i < window; ++i) {
        const double pa = stepAt(gate + step);
        if (i < kept) trace[i] = pa;
        if (i == window - 1 - 2 * third) p1 = pa;
        if (i == window - 1 - third) p2 = pa;
        if (i == window - 1) p3 = pa;
    }
    const double bend = p1 + p3 - 2.0 * p2;
    double last = p3;
    // Δ² d'Aitken, seulement si la queue décroît bien comme une exponentielle (même sens, rétrécissement).
    if (std::fabs(bend) > 1e-9 && (p2 - p1) * (p3 - p2) > 0.0 && std::fabs(p3 - p2) < std::fabs(p2 - p1)) last = (p1 * p3 - p2 * p2) / bend;
    const double change = last - before;
    BoostPlant plant;
    plant.rpm = rpm;
    plant.gainPa = std::fabs(change / step);
    double t28 = -1.0, t63 = -1.0;
    for (int i = 0; i < kept; ++i) {
        const double reached = change != 0.0 ? (trace[i] - before) / change : 1.0;
        if (t28 < 0.0 && reached >= 0.283) t28 = static_cast<double>(i + 1) * kStep;
        if (t63 < 0.0 && reached >= 0.632) {
            t63 = static_cast<double>(i + 1) * kStep;
            break;
        }
    }
    if (t28 < 0.0) t28 = 0.5 * kBenchWindowS;
    if (t63 < 0.0) t63 = kBenchWindowS;
    // Smith : τ = 1,5·(t63 − t28), θ = t63 − τ.
    plant.tauS = 1.5 * (t63 - t28);
    if (plant.tauS < 0.02) plant.tauS = 0.02;
    plant.deadS = t63 - plant.tauS;
    if (plant.deadS < kStep) plant.deadS = kStep;
    if (plant.gainPa < 1.0) plant.gainPa = 1.0;
    return plant;
}

}  // namespace

BoostPlant identifyBoostPlant(TurboCharger& turbo, double rpm, double breathing, double gate, double settleS) {
    TurboInput in;
    in.rpm = rpm;
    in.fill = 1.0;
    in.breathing = breathing;
    in.burning = true;
    return identifyPlant(
        [&](double opening) {
            in.gateDemand = opening;
            turbo.step(kStep, in);
            return turbo.state().chargePa;
        },
        rpm, gate, settleS);
}

BoostPlant identifyTwinBoostPlant(TwinTurbo& twin, double rpm, double breathing, double gate, double settleS) {
    TwinTurboInput in;
    in.rpm = rpm;
    in.fill = 1.0;
    in.breathing = breathing;
    in.burning = true;
    return identifyPlant(
        [&](double opening) {
            in.wastegate = opening;
            twin.step(kStep, in);
            return twin.state().unit[0].chargePa;
        },
        rpm, gate, settleS);
}

double boostProtectPa(const TurboSpec& reference, double rpm, double breathing, double boostBar, double noLimitPa) {
    // Les roues limitent-elles ? Oui si, sans leur limite, le turbo tiendrait plus de pression.
    TurboSpec probe = reference;
    probe.targetBar = kProtectProbeShare * boostBar + kProtectProbeBar;
    TurboSpec free = probe;
    free.tipLimit = 1e9;
    const TurboSteady limited = turboSteady(probe, rpm, breathing);
    const TurboSteady unlimited = turboSteady(free, rpm, breathing);
    return unlimited.chargePa > limited.chargePa + 100.0 ? limited.chargePa : noLimitPa;
}

double gearTorqueCapNm(const Powertrain& p, const Gearing& gearing, int gear, double tyreGrip) {
    const BodySpec& body = bodySpec(p.car.body);
    constexpr double kGravity = 9.81;
    const double weight = static_cast<double>(p.car.massKg) * kGravity;
    const double front = frontShareOf(p.car);
    const double lever = body.cgHeightM / body.wheelbaseM;
    // La force que les pneus moteurs passent, report de charge compris : l'accélération qu'elle donne charge l'arrière.
    double force = tyreGrip * weight;
    if (p.car.drive == Drive::Front) force = tyreGrip * weight * front / (1.0 + tyreGrip * lever);
    else if (p.car.drive == Drive::Rear) force = tyreGrip * weight * (1.0 - front) / (1.0 - tyreGrip * lever > 0.2 ? 1.0 - tyreGrip * lever : 0.2);
    const int g = gear < 1 ? 1 : (gear > gearing.gears ? gearing.gears : gear);
    const double overall = gearing.overall[static_cast<std::size_t>(g - 1)];
    const double tyreNm = force * wheelRadiusM(p.car) / (overall * gearing.box.meshEfficiency);
    return gearing.box.limitNm > 0.0 && gearing.box.limitNm < tyreNm ? gearing.box.limitNm : tyreNm;
}

BoostControlSpec boostControlSpecFor(const Powertrain& p, const Gearing& gearing, double tyreGrip) {
    BoostControlSpec spec;
    const EngineConfig& e = p.engine;
    spec.kind = e.boostControl;
    spec.boostBar = static_cast<double>(e.boostBar);
    spec.calibratedPa = spec.referencePa + spec.boostBar * 1e5;
    if (spec.kind == BoostControl::Spring || p.electric || e.aspiration != Aspiration::Turbo) return spec;

    const double redline = static_cast<double>(e.redlineRpm);
    spec.firstRpm = 1000.0;
    spec.stepRpm = (redline - spec.firstRpm) / static_cast<double>(kBoostGrid - 1);
    if (spec.stepRpm < 50.0) spec.stepRpm = 50.0;
    // Deux turbos ou des aubes : le même étalonnage, sur le banc de ce montage. Un twincharger garde son volet (voir `twin`), et
    // ne lit de tout ceci que la cible.
    spec.twin = usesTwinTurbo(e) && e.turboLayout != TurboLayout::Twincharger;
    const TurboSpec turbo = turboSpecOf(p);
    const TwinTurboSpec twinSpec = spec.twin ? twinTurboSpecOf(p) : TwinTurboSpec{};

    // ── La cible et la précommande : l'équilibre de l'arbre, plein gaz, à chaque régime de la grille ──
    TurboSpec half = turbo;
    half.targetBar = 0.5 * spec.boostBar;
    for (int i = 0; i < kBoostGrid; ++i) {
        const double rpm = spec.firstRpm + spec.stepRpm * static_cast<double>(i);
        const double breathing = breathingOf(p, rpm);
        const std::size_t at = static_cast<std::size_t>(i);
        if (spec.twin) {
            // Chaque arbre ouvre sa soupape dès que sa roue approche de sa vitesse limite (`TwinTurbo::step`) : la cible n'a pas à
            // la protéger. La précommande : l'ouverture commune qui tient la pression, avec le primaire seul avant la bascule.
            spec.gateAtCalibrated[at] = twinSteadyGate(twinSpec, rpm, breathing, spec.boostBar);
            spec.gateAtHalf[at] = twinSteadyGate(twinSpec, rpm, breathing, 0.5 * spec.boostBar);
            spec.protectPa[at] = 10.0 * spec.calibratedPa;
            continue;
        }
        spec.gateAtCalibrated[at] = turboSteady(turbo, rpm, breathing).wastegate;
        spec.gateAtHalf[at] = turboSteady(half, rpm, breathing).wastegate;
        spec.protectPa[at] = boostProtectPa(turbo, rpm, breathing, spec.boostBar, 10.0 * spec.calibratedPa);
    }

    // ── Le banc : l'arbre et sa pression, là où la wastegate travaille ──
    TurboCharger bench;
    TwinTurbo twinBench;
    if (spec.twin) twinBench.setSpec(twinSpec);
    else bench.setSpec(turbo);
    const auto identify = [&](double rpm, double gate, double settleS) {
        return spec.twin ? identifyTwinBoostPlant(twinBench, rpm, breathingOf(p, rpm), gate, settleS) : identifyBoostPlant(bench, rpm, breathingOf(p, rpm), gate, settleS);
    };
    bool first = true;
    for (const double share : kBenchShares) {
        const double rpm = share * redline;
        const double gate = boostGridAt(spec, spec.gateAtCalibrated, rpm);
        if (gate < kBenchMinGate) continue;
        spec.plants[static_cast<std::size_t>(spec.plantCount)] = identify(rpm, gate, first ? 5.0 : 3.0);
        ++spec.plantCount;
        first = false;
    }
    // Rien à jeter nulle part : le turbo n'atteint jamais sa cible. On mesure quand même au point nominal, wastegate entrouverte.
    if (spec.plantCount == 0) {
        const double rpm = kBenchShares[3] * redline;
        spec.plants[0] = identify(rpm, kBenchMinGate, 5.0);
        spec.plantCount = 1;
    }

    // ── Les gains, mesure par mesure, puis interpolés en régime : le calculateur les tabule de même ──
    for (int i = 0; i < kBoostGrid; ++i) {
        const double rpm = spec.firstRpm + spec.stepRpm * static_cast<double>(i);
        int k = 0;
        while (k + 1 < spec.plantCount && spec.plants[static_cast<std::size_t>(k + 1)].rpm <= rpm) ++k;
        const BoostPid a = simcPid(spec.plants[static_cast<std::size_t>(k)], spec.actuator);
        BoostPid pid = a;
        if (k + 1 < spec.plantCount && rpm > spec.plants[static_cast<std::size_t>(k)].rpm) {
            const BoostPid b = simcPid(spec.plants[static_cast<std::size_t>(k + 1)], spec.actuator);
            const double lo = spec.plants[static_cast<std::size_t>(k)].rpm;
            const double hi = spec.plants[static_cast<std::size_t>(k + 1)].rpm;
            const double share = clamp((rpm - lo) / (hi - lo), 0.0, 1.0);
            pid.kc = a.kc + share * (b.kc - a.kc);
            pid.tauI = a.tauI + share * (b.tauI - a.tauI);
            pid.tauD = a.tauD + share * (b.tauD - a.tauD);
        }
        spec.pid[static_cast<std::size_t>(i)] = pid;
    }

    // ── Selon le rapport : la pression qui donne le couple que ce rapport peut passer ──
    spec.gears = gearing.gears;
    for (int g = 1; g <= gearing.gears; ++g) {
        const double capNm = gearTorqueCapNm(p, gearing, g, tyreGrip);
        for (int i = 0; i < kBoostGrid; ++i) {
            const double rpm = spec.firstRpm + spec.stepRpm * static_cast<double>(i);
            double& cap = spec.gearCapPa[static_cast<std::size_t>(g - 1)][static_cast<std::size_t>(i)];
            if (fullLoadTorqueNm(p, rpm, 1.0) <= capNm) {
                cap = spec.calibratedPa;
                continue;
            }
            // Le couple croît avec la pression : une dichotomie retrouve celle du plafond. C'est le modèle de remplissage, à l'envers.
            double low = 0.0, high = 1.0;
            if (fullLoadTorqueNm(p, rpm, 0.0) >= capNm) {
                high = 0.0;
            } else {
                for (int n = 0; n < 18; ++n) {
                    const double middle = 0.5 * (low + high);
                    (fullLoadTorqueNm(p, rpm, middle) > capNm ? high : low) = middle;
                }
            }
            cap = spec.referencePa + high * spec.boostBar * 1e5;
        }
    }
    return spec;
}

// ── Le calculateur, pas à pas ──

void BoostController::reset() { *this = BoostController{}; }

double BoostController::step(const BoostControlSpec& spec, double dt, const BoostControlInput& input) {
    const BoostActuatorSpec& actuator = spec.actuator;
    target_ = boostTargetPa(spec, input.rpm, input.gear, input.ambientPa);
    if (!started_) {
        lastPa_ = input.chargePa;
        started_ = true;
    }
    // La tâche du calculateur : un PID en temps discret, dérivée sur la mesure filtrée, précommande, anti-emballement.
    if (taskTicks_ <= 0) {
        taskTicks_ = ticks(actuator.taskS, dt);
        const double ts = static_cast<double>(taskTicks_) * dt;
        const BoostPid pid = pidAt(spec, input.rpm);
        const double error = target_ - input.chargePa;
        const double feedForward = dutyForGate(actuator, feedForwardGate(spec, input.rpm, target_, input.ambientPa));
        // La dérivée de la mesure, pas de l'écart : une cible qui saute au passage d'un rapport ne donne pas de coup.
        const double filter = pid.tauD / kDerivativeFilter;
        derivative_ = (filter * derivative_ - pid.kc * pid.tauD * (input.chargePa - lastPa_)) / (filter + ts);
        lastPa_ = input.chargePa;
        // L'étagement d'un séquentiel : ce que l'intégrale a appris sur un seul turbo ne vaut plus pour deux. Elle reste à zéro tant
        // que le second ne débite pas ; la précommande, la proportionnelle et la dérivée tiennent la pression.
        if (input.staging) integral_ = 0.0;
        const double unsaturated = feedForward + pid.kc * error + integral_ + derivative_;
        command_ = clamp(unsaturated, 0.0, 1.0);
        // Anti-emballement par intégration conditionnelle (Åström et Hägglund) : l'intégrale s'arrête quand l'électrovanne est en
        // butée et que l'écart l'y pousse encore, et quand la pression rejoint d'elle-même la cible avant τI. Elle ne corrige que
        // ce qui resterait : le turbo qui se lance ne la gonfle pas.
        const bool pinned = (unsaturated > 1.0 && error > 0.0) || (unsaturated < 0.0 && error < 0.0);
        const double rate = pid.kc * pid.tauD > 0.0 ? -derivative_ / (pid.kc * pid.tauD) : 0.0;
        const bool closing = (error > 0.0 && rate > 0.0) || (error < 0.0 && rate < 0.0);
        const bool soon = closing && (rate > 0.0 ? rate : -rate) * pid.tauI > (error > 0.0 ? error : -error);
        if (!input.staging && (!spec.antiWindup || (!pinned && !soon))) integral_ += pid.kc * ts / pid.tauI * error;
        // Bien sous la cible, électrovanne en butée, la boucle est ouverte : ce que l'intégrale avait appris ne vaut plus pour la
        // prochaine montée, comme quand un calculateur sort de la régulation. Elle repart de zéro, la précommande tient le reste.
        if (spec.antiWindup && pinned && unsaturated - integral_ > 1.0) integral_ = 0.0;
    }
    --taskTicks_;
    // L'électrovanne prend le nouveau rapport cyclique au début de chaque période de découpage.
    if (pwmTicks_ <= 0) {
        pwmTicks_ = ticks(1.0 / actuator.pwmHz, dt);
        latched_ = command_;
    }
    --pwmTicks_;
    // La membrane et son tuyau suivent la moyenne du découpage.
    vacuum_ += (effectiveDuty(actuator, latched_) - vacuum_) * dt / actuator.chamberS;
    return gateForVacuum(vacuum_);
}

}  // namespace revforge
