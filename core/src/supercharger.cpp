#include "revforge/supercharger.hpp"

#include <cmath>

#include "portable_math.hpp"
#include "revforge/air_filter.hpp"
#include "revforge/atmosphere.hpp"
#include "supercharger_laws.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAtmosphere = 101325.0;
constexpr double kAmbientK = 293.0;
constexpr double kAirR = 287.0;
constexpr double kAirCp = 1005.0;
constexpr double kGamma = 1.4;
// Remplissage du moteur, comme pour le turbo.
constexpr double kEngineVolumetric = 0.92;

// ── Les volumétriques ──
// Vitesse d'arbre au régime maximal : de l'ordre de 14 000 à 18 000 tr/min pour un Roots Eaton, 18 000 à 20 000 pour une vis
// (ordres de grandeur des constructeurs). Le rapport de poulie en découle.
constexpr double kRootsRotorRpm = 16000.0;
constexpr double kScrewRotorRpm = 20000.0;
constexpr double kMinRatio = 0.8;
constexpr double kMaxRatio = 3.5;
// Les jeux : section de fuite pour un volume engendré V, en V^(2/3), d'où une part de fuite d'autant plus forte que l'arbre
// tourne lentement. Supposé : donne 85 % de rendement volumétrique à 8 000 tr/min et 0,5 bar sur un rotor de 1,5 L.
constexpr double kRootsLeak = 0.008;
constexpr double kScrewLeak = 0.005;
// Compression interne d'une vis : le volume se referme de 1,3 avant de s'ouvrir sur la sortie (ordre de grandeur).
constexpr double kScrewVolumeRatio = 1.3;
// Roots : trois lobes par rotor, deux rotors, six poches par tour. Vis : trois lobes sur le rotor mâle, trois poches par tour.
constexpr int kRootsPulses = 6;
constexpr int kScrewPulses = 3;
// Paliers et pignons de synchronisation : de l'ordre de 2 kW à vitesse maximale pour 1,5 L (supposé).
constexpr double kRotorFrictionW = 2000.0;
constexpr double kReferenceRotorM3 = 1.5e-3;
// Sous 500 Pa, l'orifice suit une loi linéaire : la racine a une pente infinie en zéro.
constexpr double kOrificeSoftPa = 500.0;

// ── Le centrifuge ──
// Hauteur isentropique à faible débit, rapportée à U² (la même roue que le turbo, un peu avant son sommet).
constexpr double kHeadCoefficient = 0.5;
constexpr double kCentrifugalEta = 0.72;
constexpr double kFlowCoefficient = 0.065;
// Débit réduit au point nominal, plus bas qu'un turbo : une roue plus grande, moins rapide. Avec cette valeur, le calcul donne
// 101 mm et 67 000 tr/min sur un 6,2 L à 0,8 bar, 60 mm et 120 000 tr/min sur un 2,0 L à 1 bar : l'ordre de ProCharger et de Rotrex.
constexpr double kRatedReducedFlow = 1.5;
// À la vitesse nominale, le débit nominal fait perdre 10 % de la pression demandée sur la hauteur à vide.
constexpr double kCurveLossShare = 0.1;
constexpr double kChokeShare = 1.6;
constexpr double kTipLimit = 520.0;
constexpr double kMinWheel = 0.04;
constexpr double kMaxWheel = 0.16;
constexpr int kWheelBlades = 6;
// Ni vent ni débit : la roue brasse quand même de l'air (coefficient de pertes supposé, comme pour le turbo).
constexpr double kWindage = 0.003;

// ── L'entraînement ──
constexpr double kBeltEfficiency = 0.96;
constexpr double kBeltAndGearsEfficiency = 0.93;

// ── Les pertes et l'échangeur ──
// La perte d'entrée, à part : c'est là que se branche un filtre à air.
constexpr double kRatedFilterPa = 3000.0;
// Un échangeur court, souvent air-eau, sur le collecteur : moins de perte que le long circuit d'un turbo.
constexpr double kRatedCoolerBar = 0.05;
constexpr double kCoolerEffectLow = 0.86;
constexpr double kCoolerEffectFall = 0.14;
constexpr double kCoolerMassS = 1.5;

// ── La soupape de dérivation ──
// Le vide du collecteur la tient ouverte ; elle se referme quand le papillon approche du plein gaz.
constexpr double kBypassFullOpenFill = 0.7;
constexpr double kBypassClosedFill = 0.95;
constexpr double kBypassS = 0.04;
// Grande ouverte, elle coûte de l'ordre de 8 kPa au débit maximal : 50 mm pour un compresseur de 1 L (ordre de grandeur).
constexpr double kBypassRefPa = 8000.0;
constexpr double kDischarge = 0.65;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }
double positive(double v) { return v > 0.0 ? v : 0.0; }
double magnitude(double v) { return v < 0.0 ? -v : v; }

bool centrifugal(const SuperchargerSpec& spec) { return spec.kind == SuperchargerKind::Centrifugal; }

double airDensity(double pa, double kelvin) { return pa / (kAirR * kelvin); }

/** x^(7/2). */
double powerSevenHalves(double x) { return x * x * x * std::sqrt(x); }

/** Hauteur isentropique pour ce rapport de pression (J/kg). */
double headFor(double ratio, double inletK = kAmbientK) { return kAirCp * inletK * (portable::pow(ratio > 0.1 ? ratio : 0.1, 2.0 / 7.0) - 1.0); }

/** Un orifice, régularisé autour de zéro : débit volumique par unité de section, signé. */
double orificeSpeed(double across, double rho) { return std::sqrt(2.0 / rho) * across / std::sqrt(magnitude(across) + kOrificeSoftPa); }

/** Le volume engendré par seconde. */
double sweptFlow(const SuperchargerSpec& spec, double omega) { return spec.rotorVolumeM3 * omega / (2.0 * kPi); }

/** Ce que déplace le compresseur (kg/s) pour cette vitesse et ces pressions. */
double compressorFlowAt(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa) {
    if (centrifugal(spec)) {
        const double tip = omega * 0.5 * spec.wheelDiameterM;
        const double ratio = powerSevenHalves(1.0 + kHeadCoefficient * tip * tip / (kAirCp * inletK));
        const double choke = spec.chokeFlow * omega / spec.referenceOmega;
        const double flow = spec.curveConductance * (inletPa * ratio - outletPa);
        return flow > choke ? choke : (flow < -spec.chokeFlow ? -spec.chokeFlow : flow);
    }
    const double rho = airDensity(inletPa, inletK);
    // Les fuites suivent l'écart de pression, pas la vitesse : à bas régime, elles mangent une bonne part de ce qui passe.
    return rho * (sweptFlow(spec, omega) - spec.leakArea * orificeSpeed(outletPa - inletPa, rho));
}

/**
 * Le travail d'un volume engendré, entre ces pressions (J/m³) : compression interne de `volumeRatio`, puis mise à la pression
 * de sortie d'un coup, quand la poche s'ouvre. Un Roots n'a que ce second temps : Δp, quel que soit le rendement de ses rotors.
 */
double displacementWork(const SuperchargerSpec& spec, double inletPa, double outletPa) {
    const double inner = kGamma / (kGamma - 1.0) * inletPa * (spec.innerRise - 1.0);
    return positive(inner + (outletPa - inletPa * spec.innerPressure) / spec.volumeRatio);
}

/** Puissance sur l'arbre du compresseur (W), avec la température de sortie qui en résulte. */
double shaftPower(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa, double flow, double* outK) {
    const double share = omega / spec.referenceOmega;
    if (centrifugal(spec)) {
        const double head = positive(headFor(outletPa / inletPa, inletK));
        const double tip = omega * 0.5 * spec.wheelDiameterM;
        const double rho = airDensity(spec.ambientPa, spec.ambientK);
        const double windage = kWindage * rho * tip * tip * tip * spec.wheelDiameterM * spec.wheelDiameterM;
        *outK = inletK + head / kCentrifugalEta / kAirCp;
        return positive(flow) * head / kCentrifugalEta + windage + spec.frictionW * share * share;
    }
    const double swept = sweptFlow(spec, omega);
    const double work = swept * displacementWork(spec, inletPa, outletPa);
    // Tout ce travail finit dans l'air qui sort, y compris celui de ce qui a fui et qu'il a fallu recomprimer.
    const double floorFlow = 0.2 * airDensity(inletPa, inletK) * swept;
    const double net = flow > floorFlow ? flow : floorFlow;
    *outK = inletK + (net > 1e-6 ? work / (net * kAirCp) : 0.0);
    return work + spec.frictionW * share * share;
}

double coolerOutlet(double hotK, double flow, double rated, double ambientK) {
    const double effect = kCoolerEffectLow - kCoolerEffectFall * clamp(flow / rated, 0.0, 1.5);
    return hotK - effect * (hotK - ambientK);
}

/**
 * L'échangeur à l'équilibre : l'air-air ε-NTU devant la soufflerie du banc, dans l'air du jour (finitions), ou celui du réglage
 * (charge_air.cpp). La loi d'origine ne sert plus qu'au dimensionnement, avant que le faisceau ne le soit.
 */
double coolerOutletOf(const SuperchargerSpec& spec, double hotK, double flow) {
    if (spec.cooler.kind == Intercooler::AirToAir) {
        if (spec.cooler.faceM2 <= 0.0) return coolerOutlet(hotK, flow, spec.ratedFlow, spec.ambientK);
        ChargeConditions air;
        air.ambientK = spec.ambientK;
        air.ambientPa = spec.ambientPa;
        return benchCoolerOutletK(spec.cooler, hotK, flow, air);
    }
    return chargeCoolerSteadyK(spec.cooler, hotK, flow);
}

/** Le vide du collecteur ouvre la soupape : pas de pression inutile pédale relâchée. */
double bypassWanted(double fill) { return clamp((kBypassClosedFill - fill) / (kBypassClosedFill - kBypassFullOpenFill), 0.0, 1.0); }

double bypassFlowAt(const SuperchargerSpec& spec, double opening, double outletPa, double inletPa) {
    if (opening <= 1e-4) return 0.0;
    const double rho = airDensity(outletPa > inletPa ? outletPa : inletPa, spec.ambientK);
    return kDischarge * spec.bypassArea * opening * rho * orificeSpeed(outletPa - inletPa, rho);
}

/** Ce que le moteur avale : la part de pression qui passe le papillon. */
double engineFlowAt(const SuperchargerSpec& spec, double rpm, double breathing, double plenumPa, double coolK) {
    return kEngineVolumetric * breathing * airDensity(plenumPa, coolK) * spec.displacementM3 * rpm / 120.0;
}

double shaftOmegaAt(const SuperchargerSpec& spec, double rpm) { return rpm * 2.0 * kPi / 60.0 * spec.driveRatio; }

/** Les deux côtés du compresseur, pour que le papillon voie `chargePa`. La perte d'entrée est à part, et la seule à dépendre du filtre. */
struct Ends {
    double inletPa;
    double outletPa;
    double inletK;
};

Ends endsFor(const SuperchargerSpec& spec, double chargePa, double engineFlow, double coolK, double rpm) {
    Ends e;
    if (spec.inletFilter == AirFilter::Stock) {
        e.inletPa = spec.ambientPa - spec.filterDrop * engineFlow * engineFlow;
        e.inletK = spec.ambientK;
    } else {
        // Le compresseur reste celui qui a été dimensionné sur le papier d'origine : seul change l'air qu'il reçoit.
        const AirInlet in = airInletBehind(spec.inletFilter, spec.inletPlacement, engineFlow, spec.ratedFlow, rpm / (kSuperchargerRatedShare * spec.redlineRpm), spec.ambientPa, spec.ambientK);
        e.inletPa = in.pressurePa;
        e.inletK = in.temperatureK;
    }
    e.outletPa = chargePa + spec.coolerDrop * engineFlow * engineFlow / airDensity(chargePa, coolK);
    return e;
}

struct Point {
    double surplus = 0.0;  // kg/s en trop dans le volume
    double engineFlow = 0.0;
    double compressorFlow = 0.0;
    double coolK = kAmbientK;
    double outK = kAmbientK;
    double shaftW = 0.0;
};

/** Un point d'équilibre candidat. L'air chauffe avec la compression, et sa densité en dépend : deux passes suffisent. */
Point pointAt(const SuperchargerSpec& spec, double rpm, double breathing, double chargePa, double fill) {
    Point p;
    const double omega = shaftOmegaAt(spec, rpm);
    const double plenum = chargePa * fill;
    double coolK = spec.ambientK;
    Ends ends{spec.ambientPa, spec.ambientPa, spec.ambientK};
    for (int pass = 0; pass < 2; ++pass) {
        p.engineFlow = engineFlowAt(spec, rpm, breathing, plenum, coolK);
        ends = endsFor(spec, chargePa, p.engineFlow, coolK, rpm);
        p.compressorFlow = compressorFlowAt(spec, omega, ends.inletPa, ends.inletK, ends.outletPa);
        p.shaftW = shaftPower(spec, omega, ends.inletPa, ends.inletK, ends.outletPa, p.compressorFlow, &p.outK);
        coolK = coolerOutletOf(spec, p.outK, p.engineFlow);
    }
    p.coolK = coolK;
    p.surplus = p.compressorFlow - p.engineFlow - bypassFlowAt(spec, bypassWanted(fill), ends.outletPa, ends.inletPa);
    return p;
}

}  // namespace

// ── Le dimensionnement ──

namespace {

SuperchargerSpec sizeSupercharger(const EngineConfig& e, double breathingAtSizing, double breathingAtRated);

/**
 * Les derniers compresseurs dimensionnés. Le modèle de conduite en redemande à chaque pas, par `superchargerSpecOf` (le couple,
 * la consommation, l'entraînement) et par le profil du son (`idleRpmOf`), avec deux moteurs différents : d'où quatre places.
 * Fusion des finitions : depuis que le dimensionnement passe cinq fois par le faisceau ε-NTU, un pas de conduite d'un moteur à
 * compresseur coûtait dix fois plus qu'à l'intégration (le 0 à 100 de la fiche du V8 à compresseur : 5,8 s dans le WASM).
 * Le résultat ne dépend que de ce qu'on lui passe : le même calcul, au bit près, une fois par réglage.
 */
struct SizingMemo {
    bool valid = false;
    EngineConfig engine;
    double atSizing = 0.0;
    double atRated = 0.0;
    SuperchargerSpec spec;
};
constexpr int kSizingMemos = 4;
SizingMemo g_sizing[kSizingMemos];
int g_sizingNext = 0;

}  // namespace

SuperchargerSpec superchargerSpecFor(const EngineConfig& e, double breathingAtSizing, double breathingAtRated) {
    for (const SizingMemo& memo : g_sizing) {
        if (memo.valid && memo.atSizing == breathingAtSizing && memo.atRated == breathingAtRated && memo.engine == e) return memo.spec;
    }
    const SuperchargerSpec spec = sizeSupercharger(e, breathingAtSizing, breathingAtRated);
    SizingMemo& slot = g_sizing[g_sizingNext];
    g_sizingNext = (g_sizingNext + 1) % kSizingMemos;
    slot.engine = e;
    slot.atSizing = breathingAtSizing;
    slot.atRated = breathingAtRated;
    slot.spec = spec;
    slot.valid = true;
    return spec;
}

namespace {

SuperchargerSpec sizeSupercharger(const EngineConfig& e, double breathingAtSizing, double breathingAtRated) {
    SuperchargerSpec s;
    s.kind = e.superchargerKind;
    // Le volume avalé en deux tours d'arbre : tous les débits ci-dessous s'en déduisent avec rpm/120.
    s.displacementM3 = e.sweptCcPerTwoTurns() * 1e-6;
    s.targetBar = static_cast<double>(e.boostBar);
    s.redlineRpm = static_cast<double>(e.redlineRpm);
    s.chargeVolumeM3 = clamp(1.0e-3 + 0.8 * s.displacementM3, 1.5e-3, 8e-3);

    const double chargePa = kAtmosphere + s.targetBar * 1e5;
    const double ratedRpm = kSuperchargerRatedShare * s.redlineRpm;
    // Une première idée de l'air chaud : la compression isentropique, à 60 %. Les passes suivantes la corrigent.
    double coolK = kAmbientK + (1.0 - kCoolerEffectLow + kCoolerEffectFall) * headFor(chargePa / kAtmosphere) / 0.6 / kAirCp;
    const auto setLosses = [&s, chargePa, ratedRpm, breathingAtRated](double cool) {
        const double rho = airDensity(chargePa, cool);
        s.ratedFlow = kEngineVolumetric * breathingAtRated * rho * s.displacementM3 * ratedRpm / 120.0;
        s.filterDrop = kRatedFilterPa / (s.ratedFlow * s.ratedFlow);
        s.coolerDrop = kRatedCoolerBar * 1e5 * rho / (s.ratedFlow * s.ratedFlow);
    };

    if (s.kind == SuperchargerKind::Centrifugal) {
        setLosses(coolK);
        s.driveEfficiency = kBeltAndGearsEfficiency;
        s.pulsesPerTurn = kWheelBlades;
        const double lossPa = kCurveLossShare * s.targetBar * 1e5;
        s.curveConductance = s.ratedFlow / lossPa;
        const Ends ends = endsFor(s, chargePa, s.ratedFlow, coolK, ratedRpm);
        const double head = headFor((ends.outletPa + lossPa) / ends.inletPa);
        double tip = std::sqrt(head / kHeadCoefficient);
        // Une roue d'aluminium ne tient pas plus : au-delà, la pression demandée n'est plus atteinte.
        if (tip > kTipLimit) tip = kTipLimit;
        const double rho = airDensity(kAtmosphere, kAmbientK);
        s.wheelDiameterM = clamp(std::sqrt(s.ratedFlow / (kRatedReducedFlow * kFlowCoefficient * rho * tip)), kMinWheel, kMaxWheel);
        const double wheelOmega = tip / (0.5 * s.wheelDiameterM);
        s.driveRatio = wheelOmega / (ratedRpm * 2.0 * kPi / 60.0);
        s.referenceOmega = wheelOmega;
        s.chokeFlow = kChokeShare * s.ratedFlow;
        // Paliers rapides et pignons multiplicateurs : quelques centaines de watts (supposé).
        s.frictionW = 400.0 + 150.0 * s.displacementM3 / 1e-3;
        s.bypassArea = s.chokeFlow / (kDischarge * std::sqrt(2.0 * rho * kBypassRefPa));
        return s;
    }

    const bool screw = s.kind == SuperchargerKind::TwinScrew;
    s.driveEfficiency = kBeltEfficiency;
    s.pulsesPerTurn = screw ? kScrewPulses : kRootsPulses;
    s.volumeRatio = screw ? kScrewVolumeRatio : 1.0;
    s.innerRise = screw ? portable::pow(kScrewVolumeRatio, kGamma - 1.0) : 1.0;
    s.innerPressure = screw ? portable::pow(kScrewVolumeRatio, kGamma) : 1.0;
    s.driveRatio = clamp((screw ? kScrewRotorRpm : kRootsRotorRpm) / s.redlineRpm, kMinRatio, kMaxRatio);
    const double leak = screw ? kScrewLeak : kRootsLeak;
    const double sizingRpm = kVolumetricSizingShare * s.redlineRpm;
    const double sizingOmega = shaftOmegaAt(s, sizingRpm);
    s.referenceOmega = shaftOmegaAt(s, ratedRpm);

    // Le volume engendré qui tient la pression demandée à 40 % du régime maximal. Il dépend de ses propres fuites et de
    // l'air qu'il chauffe : quelques passes.
    s.rotorVolumeM3 = s.displacementM3;
    for (int pass = 0; pass < 5; ++pass) {
        setLosses(coolK);
        const double flow = kEngineVolumetric * breathingAtSizing * airDensity(chargePa, coolK) * s.displacementM3 * sizingRpm / 120.0;
        const Ends ends = endsFor(s, chargePa, flow, coolK, sizingRpm);
        s.leakArea = leak * portable::pow(s.rotorVolumeM3, 2.0 / 3.0);
        const double rho = airDensity(ends.inletPa, kAmbientK);
        const double perSecond = flow / rho + s.leakArea * orificeSpeed(ends.outletPa - ends.inletPa, rho);
        s.rotorVolumeM3 = perSecond * 2.0 * kPi / sizingOmega;
        double outK = kAmbientK;
        shaftPower(s, sizingOmega, ends.inletPa, ends.inletK, ends.outletPa, flow, &outK);
        // Finitions : dimensionné avec l'échangeur de série, le faisceau ε-NTU devant la soufflerie du banc, qui rend plus que
        // l'ancienne loi à ce débit partiel : la pression demandée est bien tenue à 40 % du régime maximal.
        coolK = benchCoolerOutletK(airToAirCoolerFor(s.ratedFlow, outK), outK, flow);
    }
    s.leakArea = leak * portable::pow(s.rotorVolumeM3, 2.0 / 3.0);
    s.frictionW = kRotorFrictionW * s.rotorVolumeM3 / kReferenceRotorM3;
    const double rho = airDensity(kAtmosphere, kAmbientK);
    const double fullFlow = rho * s.rotorVolumeM3 * s.referenceOmega / (2.0 * kPi) / kSuperchargerRatedShare;
    s.bypassArea = fullFlow / (kDischarge * std::sqrt(2.0 * rho * kBypassRefPa));
    return s;
}

}  // namespace

// ── L'équilibre ──

SuperchargerSteady superchargerSteady(const SuperchargerSpec& spec, double rpm, double breathing, double fill) {
    SuperchargerSteady out;
    if (rpm <= 0.0 || breathing <= 0.0) return out;
    const double share = clamp(fill, 0.0, 1.0);
    const double target = spec.ambientPa + spec.targetBar * 1e5;
    Point found = pointAt(spec, rpm, breathing, target, share);
    double chargePa = target;
    // Assez de débit pour la pression demandée : la soupape rend le surplus à l'entrée. Sinon, le volume s'équilibre plus bas.
    if (found.surplus < 0.0) {
        double low = spec.ambientPa - 0.3e5;
        double high = target;
        for (int i = 0; i < 18; ++i) {
            const double middle = 0.5 * (low + high);
            (pointAt(spec, rpm, breathing, middle, share).surplus > 0.0 ? low : high) = middle;
        }
        chargePa = 0.5 * (low + high);
        found = pointAt(spec, rpm, breathing, chargePa, share);
    }
    out.chargePa = chargePa;
    out.boost = clamp((chargePa - spec.ambientPa) / (spec.targetBar * 1e5), 0.0, 1.0);
    out.outK = found.outK;
    out.coolK = found.coolK;
    out.flow = found.engineFlow;
    const double returned = found.compressorFlow - found.engineFlow;
    out.spillShare = found.compressorFlow > 1e-6 ? clamp(returned / found.compressorFlow, 0.0, 1.0) : 0.0;
    out.shaftW = found.shaftW / spec.driveEfficiency;
    out.driveNm = out.shaftW / (rpm * 2.0 * kPi / 60.0);
    out.rotorRpm = rpm * spec.driveRatio;
    return out;
}

namespace {

SuperchargerSpec inReferenceAir(SuperchargerSpec spec) {
    spec.ambientPa = kAtmosphere;
    spec.ambientK = kAmbientK;
    return spec;
}

}  // namespace

double superchargerAmbientChargeShare(const SuperchargerSpec& spec, double rpm, double breathing, double boost) {
    if (spec.ambientPa == kAtmosphere && spec.ambientK == kAmbientK) return 1.0;
    // La charge part de l'atmosphère du jour, à sa température, et les parois la réchauffent d'autant moins qu'elle est chaude.
    const SuperchargerSpec reference = inReferenceAir(spec);
    const double gauge = (boost > 0.0 ? boost : 0.0) * spec.targetBar * 1e5;
    const bool running = boost > 0.0 && rpm > 0.0 && breathing > 0.0;
    const auto trapped = [&](const SuperchargerSpec& s) {
        const double chargePa = s.ambientPa + gauge;
        return chargePa / trappedAirK(running ? pointAt(s, rpm, breathing, chargePa, 1.0).coolK : s.ambientK);
    };
    return trapped(spec) / trapped(reference);
}

double superchargerAirShare(const SuperchargerSpec& spec, double rpm, double breathing, double boost) {
    // Calibré dans l'air de référence : l'air du jour passe par `superchargerAmbientChargeShare`.
    if (spec.ambientPa != kAtmosphere || spec.ambientK != kAmbientK) return superchargerAirShare(inReferenceAir(spec), rpm, breathing, boost);
    if (boost <= 0.0) return 1.0;
    const double chargePa = kAtmosphere + boost * spec.targetBar * 1e5;
    if (rpm <= 0.0 || breathing <= 0.0) return chargePa / kAtmosphere;
    return chargePa / kAtmosphere * kAmbientK / pointAt(spec, rpm, breathing, chargePa, 1.0).coolK;
}

// ── Le pas de temps ──

void Supercharger::setSpec(const SuperchargerSpec& spec) { spec_ = spec; }

double Supercharger::boost() const { return (state_.chargePa - spec_.ambientPa) / (spec_.targetBar * 1e5); }

double Supercharger::driveNm(double rpm) const {
    const double omega = rpm * 2.0 * kPi / 60.0;
    return omega > 20.0 ? state_.shaftW / omega : 0.0;
}

SuperchargerDrive Supercharger::drive() const {
    SuperchargerDrive d;
    d.ratio = static_cast<float>(spec_.driveRatio);
    d.liftPa = static_cast<float>(state_.outletPa - state_.inletPa);
    d.inletPa = static_cast<float>(state_.inletPa);
    d.flowShare = static_cast<float>(state_.engineFlow / spec_.ratedFlow);
    d.speedShare = static_cast<float>(state_.rotorOmega / spec_.referenceOmega);
    return d;
}

void Supercharger::step(double dt, const SuperchargerInput& input) {
    SuperchargerState& s = state_;
    const SuperchargerSpec& spec = spec_;
    const double rpm = input.rpm > 0.0 ? input.rpm : 0.0;
    const double fill = clamp(input.fill, 0.0, 1.0);
    const double breathing = input.breathing;
    const double omega = shaftOmegaAt(spec, rpm);
    s.rotorOmega = omega;

    const double lag = dt / kBypassS;
    s.bypass += (bypassWanted(fill) - s.bypass) * (lag < 1.0 ? lag : 1.0);
    const double opening = s.bypass;

    const double coolK = s.coolerOutK;
    const double target = spec.ambientPa + spec.targetBar * 1e5;
    // Ce qui entre dans le volume moins ce qui en sort, pour une pression donnée.
    const auto surplus = [&spec, rpm, omega, fill, coolK, breathing, opening](double chargePa) {
        const double engine = engineFlowAt(spec, rpm, breathing, chargePa * fill, coolK);
        const Ends ends = endsFor(spec, chargePa, engine, coolK, rpm);
        return compressorFlowAt(spec, omega, ends.inletPa, ends.inletK, ends.outletPa) - engine - bypassFlowAt(spec, opening, ends.outletPa, ends.inletPa);
    };
    // Implicite : la pente est raide, soupape ouverte, et le pas de 1 ms ne la tiendrait pas.
    const double gain = kAirR * coolK / spec.chargeVolumeM3;
    const double now = surplus(s.chargePa);
    const double slope = (surplus(s.chargePa + 200.0) - now) / 200.0;
    double next = s.chargePa + dt * gain * now / (1.0 - dt * gain * (slope < 0.0 ? slope : 0.0));
    if (next < 0.5 * spec.ambientPa) next = 0.5 * spec.ambientPa;
    // La soupape tient la pression demandée : ce qui dépasse repart à l'entrée.
    if (next > target) next = target;
    s.chargePa = next;
    s.plenumPa = next * fill;

    const double engine = engineFlowAt(spec, rpm, breathing, s.plenumPa, coolK);
    const Ends ends = endsFor(spec, next, engine, coolK, rpm);
    s.engineFlow = engine;
    s.inletPa = ends.inletPa;
    s.outletPa = ends.outletPa;
    s.compressorFlow = compressorFlowAt(spec, omega, ends.inletPa, ends.inletK, ends.outletPa);
    const double returned = s.compressorFlow - engine;
    s.spillShare = s.compressorFlow > 1e-6 ? clamp(returned / s.compressorFlow, 0.0, 1.0) : 0.0;

    double outK = spec.ambientK;
    s.shaftW = shaftPower(spec, omega, ends.inletPa, ends.inletK, ends.outletPa, s.compressorFlow, &outK) / spec.driveEfficiency;
    s.compressorOutK = outK;
    // L'air de charge tient la boucle d'eau, ou il n'y a pas d'échangeur : il rend la température.
    if (input.cooledK > 0.0) {
        s.coolerOutK = input.cooledK;
    } else if (spec.cooler.kind == Intercooler::AirToAir && spec.cooler.faceM2 > 0.0) {
        // L'air-air : la charge traverse le faisceau, que l'air de route refroidit et dont l'aluminium garde la chaleur.
        ChargeConditions air;
        air.ambientK = spec.ambientK;
        air.ambientPa = spec.ambientPa;
        s.coolerOutK = core_.step(spec.cooler, dt, outK, engine, input.coolingAirMs, air);
    } else {
        s.coolerOutK += (coolerOutlet(outK, engine, spec.ratedFlow, spec.ambientK) - s.coolerOutK) * dt / kCoolerMassS;
    }
}

double superchargerCoolerOutletK(const SuperchargerSpec& spec, double hotK, double flow) { return coolerOutletOf(spec, hotK, flow); }

// ── Les mêmes lois, pour le twincharger (twin_turbo.cpp) : un renvoi, rien de recopié ──
namespace supercharger_laws {

double compressorFlow(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa) { return compressorFlowAt(spec, omega, inletPa, inletK, outletPa); }
double shaftPower(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa, double flow, double* outK) {
    return revforge::shaftPower(spec, omega, inletPa, inletK, outletPa, flow, outK);
}
double orificeSpeed(double across, double rho) { return revforge::orificeSpeed(across, rho); }
double discharge() { return kDischarge; }

}  // namespace supercharger_laws

}  // namespace revforge
