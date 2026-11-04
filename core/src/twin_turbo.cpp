#include "revforge/twin_turbo.hpp"

#include <cmath>

#include "portable_math.hpp"
#include "revforge/air_filter.hpp"
#include "revforge/architecture.hpp"
#include "revforge/powertrain.hpp"
#include "supercharger_laws.hpp"
#include "turbo_laws.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;

// ── Le séquentiel ──
// Le secondaire se lance, s'ouvre, puis se referme selon le débit d'air du moteur, en part du débit nominal d'un turbo :
// le primaire seul approche alors de son blocage. Calé sur le 2JZ-GTE (vanne d'échappement du second turbo ouverte vers
// 3800 tr/min, pré-lancement juste avant), puis comparé au 959 et au 13B-REW : voir « Deux turbos » dans docs/modele.md.
constexpr double kPreSpoolShare = 1.25;
constexpr double kSwitchShare = 1.42;
constexpr double kReturnShare = 1.1;
// Pendant le pré-lancement, une petite vanne laisse passer un peu de gaz vers le secondaire : le rapport des sections
// d'une vanne de 20 mm et de la grande vanne de 50 mm (supposé).
constexpr double kPreSpoolOpening = 0.16;
// Vannes à dépression : de l'ordre du dixième de seconde (supposé).
constexpr double kExhaustValveS = 0.12;
constexpr double kRecirculationS = 0.05;
// Le volume entre le compresseur secondaire et son clapet : un bout de conduit, 0,3 L pour 2 L (supposé).
constexpr double kSecondaryVolumeShare = 0.15;

// ── Les collecteurs séparés ──
// Ce que le gaz perd en chaleur par mètre de collecteur, en part de son écart à l'air : de l'ordre de 15 % sur 0,6 m entre la
// soupape et la turbine (ordre de grandeur des températures mesurées). Deux rangées aux collecteurs inégaux n'envoient pas
// le même gaz à leurs turbines : les deux arbres ne tournent pas à la même vitesse.
constexpr double kHeaderLossPerM = 0.27;

// ── La géométrie variable ──
// Une turbine à aubes passe d'environ 1 à 4 en section (ordre de grandeur des turbines à aubes récentes, supposé). Sans
// soupape, sa plus grande ouverture est celle qui tient la pression demandée au régime maximal.
constexpr double kVaneRange = 4.0;
// Rendement : meilleur vers 60 % de la course, 14 % de moins aubes fermées, 6 % grandes ouvertes (ordre de grandeur des
// cartes publiées de turbines à aubes, supposé).
constexpr double kVaneBest = 0.6;
constexpr double kVaneLoss = 0.4;
// Actionneur électrique : moins d'un dixième de seconde (supposé).
constexpr double kVaneS = 0.08;
// Grandes ouvertes, les aubes passent 15 % de plus que ce qu'il faut au régime maximal (marge supposée).
constexpr double kVaneMargin = 1.15;

// ── Le twincharger ──
// Vitesse d'un Roots Eaton à son maximum, comme `kRootsRotorRpm` de supercharger.cpp : elle donne le rapport d'entraînement
// une fois choisi le régime où l'embrayage s'ouvre. Une vis tourne plus vite.
constexpr double kRootsTopRpm = 16000.0;
constexpr double kScrewTopRpm = 20000.0;
// Deux rotors d'aluminium de 0,4 kg, rayon de giration de 2,2 cm, pour 0,5 L par tour ; l'inertie suit le volume à la
// puissance 5/3, par similitude (supposé).
constexpr double kRotorInertiaRef = 4e-4;
constexpr double kRotorVolumeRef = 5e-4;
// L'embrayage tient une fois et demie le couple des rotors au point de dimensionnement (facteur de service supposé).
constexpr double kClutchService = 1.5;
// Le régime de coupure, en part du régime où le turbo tient seul : 3500 / 2400 sur le 1.4 TSI (VW, programme d'autoformation 359).
constexpr double kCutoutShare = 3500.0 / 2400.0;
// Le pied qui demande la pression, et celui qui la rend.
constexpr double kDemandFill = 0.8;
constexpr double kReleaseFill = 0.5;
// L'embrayage se ferme si la charge manque de plus de 10 %, sous le régime de coupure moins 10 % (hystérésis).
constexpr double kEngageShare = 0.9;
constexpr double kClutchHysteresis = 0.1;
// Volet grand ouvert depuis une demi-seconde : le turbo tient seul, le compresseur ne sert plus.
constexpr double kFlapIdleS = 0.5;
constexpr double kFlapS = 0.05;
// Le volume entre les deux étages, et la perte du volet grand ouvert au débit nominal (supposés).
constexpr double kInterstageShare = 0.6;
constexpr double kFlapDropPa = 3000.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }
double magnitude(double v) { return v < 0.0 ? -v : v; }
double positive(double v) { return v > 0.0 ? v : 0.0; }

/** Le rendement d'une turbine à aubes, en part du nominal, selon leur ouverture. */
double vaneEta(const TwinTurboSpec& spec, double vane) {
    if (!spec.variableGeometry) return 1.0;
    const double span = spec.vaneMax - spec.vaneMin;
    const double off = (vane - spec.vaneMin) / span - kVaneBest;
    return 1.0 - kVaneLoss * off * off;
}

/** Les lois d'un filtre non d'origine, comme `turboSpecOf`. */
void withFilter(TurboSpec& unit, const TurboSpec& reference, AirFilter filter) {
    if (filter == AirFilter::Stock) return;
    unit.filterDrop *= airFilterDropShare(filter);
    unit.inletFilter = reference.inletFilter;
    unit.inletPlacement = reference.inletPlacement;
}

/** Le turbo d'une moitié du moteur : la moitié du débit, une roue √2 plus petite, une inertie 2^(5/2) plus faible. */
TurboSpec halfTurbo(const EngineConfig& e, double breathingAtRated, const TurboSpec& reference) {
    EngineConfig half = e;
    half.turboPipeMm = 400;
    half.displacementCc = e.displacementCc / 2;
    half.cylinders = e.cylinders > 1 ? e.cylinders / 2 : 1;
    TurboSpec unit = turboSpecFor(half, breathingAtRated);
    unit.displacementM3 = 0.5 * reference.displacementM3;
    withFilter(unit, reference, e.airFilter);
    return unit;
}

/** Le primaire d'un séquentiel, seul sur tout le moteur : sa roue et sa turbine, la ligne et l'échangeur communs. */
TurboSpec primaryAlone(const TwinTurboSpec& spec) {
    TurboSpec t = spec.unit[0];
    const TurboSpec& r = spec.reference;
    t.displacementM3 = r.displacementM3;
    t.ratedFlow = r.ratedFlow;
    t.filterDrop = r.filterDrop;
    t.coolerDrop = r.coolerDrop;
    t.lineDrop = r.lineDrop;
    t.ratedBackPa = r.ratedBackPa;
    return t;
}

/** Le turbo à aubes ouvertes de `vane` : toute la turbine, sans soupape. */
TurboSpec withVanes(const TwinTurboSpec& spec, const TurboSpec& unit, double vane) {
    TurboSpec t = unit;
    t.turbineK = unit.turbineK * vane;
    t.wastegateK = 0.0;
    t.turbineEtaShare = vaneEta(spec, vane);
    return t;
}

/** L'équilibre d'un turbo, aubes réglées au plus ouvert qui tienne encore la pression demandée. */
TurboSteady vaneSteady(const TwinTurboSpec& spec, const TurboSpec& unit, double rpm, double breathing, double* vaneOut) {
    if (!spec.variableGeometry) {
        *vaneOut = 1.0;
        return turboSteady(unit, rpm, breathing);
    }
    TurboSteady closed = turboSteady(withVanes(spec, unit, spec.vaneMin), rpm, breathing);
    if (closed.boost < 0.999) {
        *vaneOut = spec.vaneMin;
        return closed;
    }
    TurboSteady open = turboSteady(withVanes(spec, unit, spec.vaneMax), rpm, breathing);
    if (open.boost >= 0.999) {
        *vaneOut = spec.vaneMax;
        return open;
    }
    double low = spec.vaneMin;
    double high = spec.vaneMax;
    for (int i = 0; i < 10; ++i) {
        const double middle = 0.5 * (low + high);
        (turboSteady(withVanes(spec, unit, middle), rpm, breathing).boost >= 0.999 ? low : high) = middle;
    }
    *vaneOut = low;
    return turboSteady(withVanes(spec, unit, low), rpm, breathing);
}

/** Le régime où ce turbo tient seul la pression demandée, à pleine charge : là s'ouvre l'embrayage du compresseur. */
double turboAloneRpm(const TwinTurboSpec& spec, const Powertrain& p) {
    const double redline = spec.redlineRpm;
    const auto reaches = [&](double rpm) {
        double vane = 1.0;
        return vaneSteady(spec, spec.unit[0], rpm, breathingOf(p, rpm), &vane).boost >= 0.98;
    };
    double low = 0.15 * redline;
    double high = 0.9 * redline;
    if (reaches(low)) return low;
    if (!reaches(high)) return high;
    for (int i = 0; i < 12; ++i) {
        const double middle = 0.5 * (low + high);
        (reaches(middle) ? high : low) = middle;
    }
    return high;
}

}  // namespace

// ── Le dimensionnement ──

TwinTurboSpec twinTurboSpecOf(const Powertrain& p) {
    const EngineConfig& e = p.engine;
    TwinTurboSpec s;
    s.layout = e.turboLayout;
    s.variableGeometry = e.variableGeometry;
    s.diesel = e.burnsDiesel();
    // Le tube déporté n'est pas modélisé ici : la référence est à sa longueur d'origine.
    Powertrain base = p;
    base.engine.turboPipeMm = 400;
    s.reference = turboSpecOf(base);
    const TurboSpec& ref = s.reference;
    s.displacementM3 = ref.displacementM3;
    s.targetBar = ref.targetBar;
    s.redlineRpm = ref.redlineRpm;
    s.chargeVolumeM3 = ref.chargeVolumeM3;
    s.blowOffArea = ref.blowOffArea;
    s.blowOffValve = ref.blowOffValve;
    const double breathingAtRated = breathingOf(p, 0.88 * s.redlineRpm);

    const bool twin = e.turboLayout == TurboLayout::TwinParallel || e.turboLayout == TurboLayout::TwinSequential;
    if (twin) {
        s.units = 2;
        s.unit[0] = halfTurbo(e, breathingAtRated, ref);
        s.unit[1] = s.unit[0];
        // Deux chemins d'air, chacun avec son compresseur et son conduit chaud, jusqu'à un même papillon.
        s.chargeVolumeM3 = s.unit[0].chargeVolumeM3 + s.unit[1].chargeVolumeM3;
    } else {
        s.units = 1;
        s.unit[0] = ref;
    }
    if (e.variableGeometry) {
        // Sans soupape, les aubes grandes ouvertes doivent encore tenir la pression demandée au régime maximal : on cherche
        // l'ouverture qui l'y tient, avec une marge, et la course de 1 à 4 en dessous. Deux passes, le rendement suivant la course.
        const auto& k = turbo_laws::constants();
        s.vaneMax = 4.0 / (s.diesel ? k.ratedTurbineShareDiesel : k.ratedTurbineShare);
        s.vaneMin = s.vaneMax / kVaneRange;
        const double top = s.redlineRpm;
        for (int pass = 0; pass < 2; ++pass) {
            double vane = 1.0;
            vaneSteady(s, s.unit[0], top, breathingOf(p, top), &vane);
            s.vaneMax = kVaneMargin * vane;
            s.vaneMin = s.vaneMax / kVaneRange;
        }
    }

    if (e.turboLayout == TurboLayout::TwinParallel) {
        // Chaque turbine reçoit sa rangée, ou sa moitié de cylindres, dans l'ordre d'allumage.
        const EngineProfile profile = e.toProfile();
        const int firings = profile.firingCount > 0 ? profile.firingCount : e.firingsPerTwoTurns();
        // Une turbine par culasse (architectures) : deux sur un V, un boxer ou un W ; une seule culasse (en ligne, VR) se coupe en deux.
        const bool banks = architectureOf(e).heads == 2;
        std::array<double, 2> count{0.0, 0.0};
        std::array<double, 2> length{0.0, 0.0};
        for (int i = 0; i < firings && i < kMaxCylinders; ++i) {
            const int u = banks ? (profile.bank[static_cast<std::size_t>(i)] != 0 ? 1 : 0) : i % 2;
            count[static_cast<std::size_t>(u)] += 1.0;
            length[static_cast<std::size_t>(u)] += static_cast<double>(profile.headerM[static_cast<std::size_t>(i)]);
        }
        if (count[0] <= 0.0 || count[1] <= 0.0) {
            s.gasShare = {0.5, 0.5};
            s.heatShare = {1.0, 1.0};
        } else {
            s.gasShare = {count[0] / (count[0] + count[1]), count[1] / (count[0] + count[1])};
            const double meanA = length[0] / count[0];
            const double meanB = length[1] / count[1];
            const double mean = 0.5 * (meanA + meanB);
            // Rangée de collecteurs plus longs : un gaz un peu moins chaud à sa turbine.
            s.heatShare = {portable::exp(-kHeaderLossPerM * (meanA - mean)), portable::exp(-kHeaderLossPerM * (meanB - mean))};
        }
    } else {
        s.gasShare = {1.0, 0.0};
        s.heatShare = {1.0, 1.0};
    }

    if (e.turboLayout == TurboLayout::TwinSequential) {
        const double rated = s.unit[0].ratedFlow;
        s.preSpoolFlow = kPreSpoolShare * rated;
        s.switchFlow = kSwitchShare * rated;
        s.returnFlow = kReturnShare * rated;
        s.secondaryVolumeM3 = kSecondaryVolumeShare * s.displacementM3;
        s.recirculationArea = s.unit[1].blowOffArea;
    }

    if (e.turboLayout == TurboLayout::Twincharger) {
        // Le turbo tient seul la pression au-delà d'un régime : le compresseur est dimensionné pour ce qu'il y a en dessous, et
        // ses rotors plafonnent au régime de coupure, dans le même rapport que sur le 1.4 TSI (2400 et 3500 tr/min).
        s.turboAloneRpm = turboAloneRpm(s, p);
        s.clutchOffRpm = clamp(kCutoutShare * s.turboAloneRpm, s.turboAloneRpm, 0.9 * s.redlineRpm);
        const double off = s.clutchOffRpm;
        EngineConfig low = e;
        low.redlineRpm = static_cast<int>(off + 0.5);
        SuperchargerSpec c = superchargerSpecFor(low, breathingOf(p, kVolumetricSizingShare * off), breathingOf(p, kSuperchargerRatedShare * off));
        // Le rapport d'entraînement : la vitesse maximale des rotors au régime de coupure. Même volume engendré par tour de
        // vilebrequin que le dimensionnement, plafonné en rapport.
        const double top = e.superchargerKind == SuperchargerKind::TwinScrew ? kScrewTopRpm : kRootsTopRpm;
        if (c.kind != SuperchargerKind::Centrifugal) {
            const double ratio = top / off;
            const double scale = c.driveRatio / ratio;
            c.rotorVolumeM3 *= scale;
            c.leakArea *= portable::pow(scale, 2.0 / 3.0);
            c.frictionW *= scale;
            c.referenceOmega *= ratio / c.driveRatio;
            c.driveRatio = ratio;
        }
        if (e.airFilter != AirFilter::Stock) {
            c.filterDrop *= airFilterDropShare(e.airFilter);
            c.inletFilter = e.airFilter;
            c.inletPlacement = placementOf(p.car);
        }
        s.charger = c;
        const double volume = c.kind == SuperchargerKind::Centrifugal ? kRotorVolumeRef : c.rotorVolumeM3;
        s.rotorInertia = kRotorInertiaRef * portable::pow(volume / kRotorVolumeRef, 5.0 / 3.0);
        // Le couple des rotors au point de dimensionnement, à la pression demandée.
        const auto& k = turbo_laws::constants();
        const double sizingOmega = kVolumetricSizingShare * off * 2.0 * kPi / 60.0 * c.driveRatio;
        const double outletPa = k.atmosphere + s.targetBar * 1e5;
        const double flow = supercharger_laws::compressorFlow(c, sizingOmega, k.atmosphere, k.ambientK, outletPa);
        double outK = k.ambientK;
        const double power = supercharger_laws::shaftPower(c, sizingOmega, k.atmosphere, k.ambientK, outletPa, flow, &outK);
        s.clutchNm = kClutchService * power / (sizingOmega > 1.0 ? sizingOmega : 1.0);
        s.interstageVolumeM3 = kInterstageShare * s.displacementM3;
        const double rho = k.atmosphere / (k.airR * k.ambientK);
        s.flapArea = ref.ratedFlow / (supercharger_laws::discharge() * std::sqrt(2.0 * rho * kFlapDropPa));
    }
    return s;
}

// ── L'équilibre ──

TwinTurboSteady twinTurboSteady(const TwinTurboSpec& spec, double rpm, double breathing) {
    TwinTurboSteady out;
    const auto& k = turbo_laws::constants();
    if (rpm <= 0.0 || breathing <= 0.0) return out;
    const auto fill = [&out](const TurboSteady& t, int unit, double vane) {
        const std::size_t u = static_cast<std::size_t>(unit);
        out.shaftRpm[u] = t.shaftRpm;
        out.turbineInPa[u] = t.turbineInPa;
        out.vane[u] = vane;
    };
    switch (spec.layout) {
        case TurboLayout::TwinParallel: {
            // Deux moitiés symétriques : chacune est le turbo de sa moitié de moteur, à la même pression.
            double vane = 1.0;
            const TurboSpec& unit = spec.unit[0];
            const TurboSteady t = vaneSteady(spec, unit, rpm, breathing, &vane);
            fill(t, 0, vane);
            fill(t, 1, vane);
            out.boost = t.boost;
            out.chargePa = t.chargePa;
            out.flow = 2.0 * t.flow;
            out.stage = 2;
            out.backPressureNm = 2.0 * turboBackPressureNm(spec.variableGeometry ? withVanes(spec, unit, vane) : unit, t.chargePa, t.turbineInPa, t.flow, 1.0);
            return out;
        }
        case TurboLayout::TwinSequential: {
            // Le primaire seul, puis les deux : le régime de bascule est un passage, l'équilibre est de part ou d'autre.
            double vane = 1.0;
            const TurboSpec alone = primaryAlone(spec);
            const TurboSteady first = vaneSteady(spec, alone, rpm, breathing, &vane);
            if (first.flow < spec.switchFlow) {
                fill(first, 0, vane);
                out.boost = first.boost;
                out.chargePa = first.chargePa;
                out.flow = first.flow;
                out.stage = 0;
                out.backPressureNm = turboBackPressureNm(spec.variableGeometry ? withVanes(spec, alone, vane) : alone, first.chargePa, first.turbineInPa, first.flow, 1.0);
                return out;
            }
            const TurboSpec& unit = spec.unit[0];
            const TurboSteady both = vaneSteady(spec, unit, rpm, breathing, &vane);
            fill(both, 0, vane);
            fill(both, 1, vane);
            out.boost = both.boost;
            out.chargePa = both.chargePa;
            out.flow = 2.0 * both.flow;
            out.stage = 2;
            out.backPressureNm = 2.0 * turboBackPressureNm(spec.variableGeometry ? withVanes(spec, unit, vane) : unit, both.chargePa, both.turbineInPa, both.flow, 1.0);
            return out;
        }
        case TurboLayout::Twincharger: {
            double vane = 1.0;
            const TurboSpec& unit = spec.unit[0];
            const TurboSteady t = vaneSteady(spec, unit, rpm, breathing, &vane);
            fill(t, 0, vane);
            out.flow = t.flow;
            out.backPressureNm = turboBackPressureNm(spec.variableGeometry ? withVanes(spec, unit, vane) : unit, t.chargePa, t.turbineInPa, t.flow, 1.0);
            out.boost = t.boost;
            out.chargePa = t.chargePa;
            if (rpm >= spec.clutchOffRpm || t.boost >= 0.999) return out;
            // Sous le régime de coupure, le compresseur fournit le rapport qui manque au turbo : les rapports se multiplient.
            // Approché : le turbo garde le rapport qu'il aurait seul, alors que l'air plus dense lui donne en vrai un peu plus de gaz.
            const SuperchargerSpec& c = spec.charger;
            const SuperchargerSteady alone = superchargerSteady(c, rpm, breathing);
            const double turboRatio = t.chargePa / k.atmosphere;
            const double wanted = (k.atmosphere + spec.targetBar * 1e5) / (k.atmosphere * turboRatio);
            const double ratio = clamp(wanted, 1.0, alone.chargePa / k.atmosphere);
            const double interstagePa = k.atmosphere * ratio;
            out.chargePa = interstagePa * turboRatio;
            out.boost = clamp((out.chargePa - k.atmosphere) / (spec.targetBar * 1e5), 0.0, 1.0);
            const double omega = rpm * 2.0 * kPi / 60.0;
            const double rotor = omega * c.driveRatio;
            const double inletPa = k.atmosphere - c.filterDrop * t.flow * t.flow;
            const double flow = supercharger_laws::compressorFlow(c, rotor, inletPa, k.ambientK, interstagePa);
            double outK = k.ambientK;
            out.chargerNm = supercharger_laws::shaftPower(c, rotor, inletPa, k.ambientK, interstagePa, flow, &outK) / c.driveEfficiency / omega;
            out.clutch = true;
            out.flow = t.flow * ratio;
            return out;
        }
        case TurboLayout::Single: break;
    }
    double vane = 1.0;
    const TurboSpec& unit = spec.unit[0];
    const TurboSteady t = vaneSteady(spec, unit, rpm, breathing, &vane);
    fill(t, 0, vane);
    out.boost = t.boost;
    out.chargePa = t.chargePa;
    out.flow = t.flow;
    out.backPressureNm = turboBackPressureNm(spec.variableGeometry ? withVanes(spec, unit, vane) : unit, t.chargePa, t.turbineInPa, t.flow, 1.0);
    return out;
}

double twinTorqueCurveNm(const Powertrain& p, double rpm) {
    const TwinTurboSteady steady = twinTurboSteady(twinTurboSpecOf(p), rpm, breathingOf(p, rpm));
    return fullLoadTorqueNm(p, rpm, steady.boost) - steady.backPressureNm - steady.chargerNm;
}

double twinSteadyBoost(const Powertrain& p, double rpm) { return twinTurboSteady(twinTurboSpecOf(p), rpm, breathingOf(p, rpm)).boost; }

double twinIntakeDropPa(const Powertrain& p, double rpm) {
    const TwinTurboSpec spec = twinTurboSpecOf(p);
    const double flow = twinTurboSteady(spec, rpm, breathingOf(p, rpm)).flow;
    // Un filtre commun : le débit du moteur entier.
    return spec.reference.filterDrop * flow * flow;
}

double twinSteadyGate(const TwinTurboSpec& spec, double rpm, double breathing, double targetBar) {
    if (rpm <= 0.0 || breathing <= 0.0) return 0.0;
    TwinTurboSpec s = spec;
    s.targetBar = targetBar;
    s.reference.targetBar = targetBar;
    for (TurboSpec& unit : s.unit) unit.targetBar = targetBar;
    // L'arbre qui porte le débit à ce régime : le primaire seul d'un séquentiel avant la bascule, sinon chaque moitié.
    TurboSpec carried = s.unit[0];
    if (s.layout == TurboLayout::TwinSequential) {
        double vane = 1.0;
        const TurboSpec alone = primaryAlone(s);
        if (vaneSteady(s, alone, rpm, breathing, &vane).flow < s.switchFlow) carried = alone;
    }
    if (!s.variableGeometry) return turboSteady(carried, rpm, breathing).wastegate;
    double vane = 1.0;
    vaneSteady(s, carried, rpm, breathing, &vane);
    return clamp((vane - s.vaneMin) / (s.vaneMax - s.vaneMin), 0.0, 1.0);
}

void twinchargerProfile(const Powertrain& p, EngineProfile& profile) {
    const SuperchargerSpec charger = twinTurboSpecOf(p).charger;
    profile.supercharged = true;
    profile.superchargerKind = charger.kind;
    profile.superchargerRatio = static_cast<float>(charger.driveRatio);
    profile.superchargerPocketM3 = static_cast<float>(charger.rotorVolumeM3 / charger.pulsesPerTurn);
    profile.superchargerInnerPressure = static_cast<float>(charger.innerPressure);
}

// ── Le pas de temps ──

void TwinTurbo::setSpec(const TwinTurboSpec& spec) {
    const bool first = spec_.unit[0].referenceOmega <= 0.0 || spec_.layout != spec.layout;
    spec_ = spec;
    for (std::size_t u = 0; u < 2; ++u) {
        if (first && u == 1 && spec.units < 2) {
            state_.unit[u] = TurboState{};
            omegaSquared_[u] = 0.0;
        }
        state_.unit[u].shaftShare = state_.unit[u].shaftOmega / spec_.unit[u].referenceOmega;
        if (!spec.variableGeometry) state_.vane[u] = 1.0;
        else state_.vane[u] = clamp(state_.vane[u], spec.vaneMin, spec.vaneMax);
    }
    if (spec.layout != TurboLayout::TwinSequential) {
        state_.stage = spec.units == 2 ? 2 : 0;
        state_.secondaryOnline = spec.units == 2;
        state_.exhaustValve = spec.units == 2 ? 1.0 : 0.0;
    }
    if (spec.layout != TurboLayout::Twincharger) {
        state_.clutch = false;
        state_.clutchLocked = false;
        state_.rotorOmega = 0.0;
        state_.chargerNm = 0.0;
        state_.flap = 1.0;
    }
}

double TwinTurbo::boost() const { return (state_.unit[0].chargePa - turbo_laws::constants().atmosphere) / (spec_.targetBar * 1e5); }

double TwinTurbo::backPressureNm(double fill) const {
    const TwinTurboState& s = state_;
    const TurboSpec& ref = spec_.reference;
    const double chargePa = s.unit[0].chargePa;
    const double flow = s.unit[0].engineFlow;
    // Les cylindres de chaque rangée poussent contre leur turbine ; la référence est celle d'un turbo simple bien apparié.
    double before = s.unit[0].turbineInPa;
    if (spec_.units == 2 && spec_.layout == TurboLayout::TwinParallel) {
        before = spec_.gasShare[0] * s.unit[0].turbineInPa + spec_.gasShare[1] * s.unit[1].turbineInPa;
    }
    const double share = flow / ref.ratedFlow;
    const double difference = clamp(before - chargePa - ref.ratedBackPa * share * share, -5e4, 4e5);
    return difference * ref.displacementM3 / (4.0 * kPi) * clamp(fill, 0.0, 1.0);
}

TurboDrive TwinTurbo::drive(int unit) const {
    TurboDrive d;
    if (unit < 0 || unit >= spec_.units) return d;
    const std::size_t u = static_cast<std::size_t>(unit);
    const auto& k = turbo_laws::constants();
    const TurboState& s = state_.unit[u];
    const TurboSpec& spec = spec_.unit[u];
    const double rated = spec.ratedFlow;
    d.shaftHz = static_cast<float>(s.shaftOmega / (2.0 * kPi));
    d.shaftShare = static_cast<float>(s.shaftShare);
    d.flowShare = static_cast<float>(s.compressorFlow > 0.0 ? s.compressorFlow / rated : 0.0);
    const double gauge = (s.chargePa - k.atmosphere) / (spec_.targetBar * 1e5);
    const double pressure = std::sqrt(gauge > 0.0 ? gauge : 0.0);
    // La soupape de décharge est commune : elle sort avec le premier arbre.
    if (unit == 0) d.blowOffShare = static_cast<float>(s.blowOffFlow / spec_.reference.ratedFlow * pressure);
    d.backflowShare = static_cast<float>((s.compressorFlow < 0.0 ? -s.compressorFlow / rated : 0.0) * pressure);
    d.wastegate = static_cast<float>(s.wastegate);
    const double passage = spec.turbineK * state_.vane[u] + (spec_.variableGeometry ? 0.0 : spec.wastegateK * s.wastegate);
    const double gas = gas_[u];
    d.wastegateFlowShare = spec_.variableGeometry || passage <= 0.0 ? 0.0f : static_cast<float>(gas / rated * spec.wastegateK * s.wastegate / passage);
    // Deux petits compresseurs font chacun la moitié du bruit d'un grand : l'énergie suit le débit nominal.
    d.levelShare = static_cast<float>(std::sqrt(rated / spec_.reference.ratedFlow));
    return d;
}

SuperchargerDrive TwinTurbo::chargerDrive() const {
    SuperchargerDrive d;
    if (!hasCharger()) return d;
    const SuperchargerSpec& c = spec_.charger;
    const double crank = lastRpm_ * 2.0 * kPi / 60.0;
    d.ratio = static_cast<float>(crank > 1.0 ? state_.rotorOmega / crank : 0.0);
    d.liftPa = static_cast<float>(state_.interstagePa - lastInletPa_);
    d.inletPa = static_cast<float>(lastInletPa_);
    d.flowShare = static_cast<float>(positive(state_.chargerFlow) / c.ratedFlow);
    d.speedShare = static_cast<float>(state_.rotorOmega / c.referenceOmega);
    return d;
}

void TwinTurbo::stepSequence(double dt, double fill) {
    TwinTurboState& s = state_;
    const double air = s.unit[0].engineFlow;
    const bool wanted = fill > kDemandFill;
    switch (s.stage) {
        case 0:
            if (air > spec_.preSpoolFlow && wanted) s.stage = 1;
            break;
        case 1:
            if (air > spec_.switchFlow) s.stage = 2;
            else if (air < spec_.returnFlow || !wanted) s.stage = 0;
            break;
        default:
            if (air < spec_.returnFlow) s.stage = 0;
            break;
    }
    if (s.stage == 0) s.secondaryOnline = false;
    const double valveGoal = s.stage == 0 ? 0.0 : (s.stage == 1 ? kPreSpoolOpening : 1.0);
    s.exhaustValve += (valveGoal - s.exhaustValve) * clamp(dt / kExhaustValveS, 0.0, 1.0);
    // Le retour du compresseur secondaire vers son entrée : ouvert tant qu'il ne débite pas sur la charge.
    const double recirculationGoal = s.stage == 2 ? 0.0 : 1.0;
    recirculation_ += (recirculationGoal - recirculation_) * clamp(dt / kRecirculationS, 0.0, 1.0);
}

void TwinTurbo::stepCharger(double dt, double rpm, double fill, double targetPa) {
    TwinTurboState& s = state_;
    const auto& k = turbo_laws::constants();
    const SuperchargerSpec& c = spec_.charger;
    const TurboSpec& ref = spec_.reference;
    const double crank = rpm * 2.0 * kPi / 60.0;
    const double driven = crank * c.driveRatio;
    const double chargePa = s.unit[0].chargePa;
    const double targetBar = (targetPa - k.atmosphere) / 1e5;

    // L'embrayage, comme le 1.4 TSI : toujours fermé sous pression demandée en dessous du régime où le turbo tient seul ;
    // au-dessus, seulement s'il manque de la pression, et rouvert dès que le volet n'a plus rien à retenir ; jamais au-delà
    // du régime de coupure, où les rotors atteindraient leur vitesse maximale.
    const bool below = rpm < spec_.turboAloneRpm;
    if (!s.clutch) {
        const bool lacking = below || chargePa < k.atmosphere + kEngageShare * targetBar * 1e5;
        if (fill > kDemandFill && rpm < spec_.clutchOffRpm * (1.0 - kClutchHysteresis) && lacking && rpm > 300.0) {
            s.clutch = true;
            s.clutchLocked = false;
            s.flapOpenFor = 0.0;
        }
    } else if (rpm > spec_.clutchOffRpm || fill < kReleaseFill || (!below && s.flapOpenFor > kFlapIdleS)) {
        s.clutch = false;
        s.clutchLocked = false;
        s.flapOpenFor = 0.0;
    }

    // L'air : le filtre, commun, puis le compresseur ou le volet qui le court-circuite, jusqu'au volume avant le turbo.
    const double drawn = s.unit[0].compressorFlow;
    const double inletPa = k.atmosphere - ref.filterDrop * drawn * drawn;
    const double inletK = turbo_laws::inletTemperature(spec_.unit[0], rpm);
    lastInletPa_ = inletPa;
    lastRpm_ = rpm;

    // Le volet : il rend à l'entrée ce qui dépasse la pression demandée. Embrayage ouvert, il laisse l'air contourner les rotors.
    const double over = (chargePa - (k.atmosphere + (1.0 - 0.2 * k.wastegateBand) * targetBar * 1e5)) / (k.wastegateBand * targetBar * 1e5);
    const double flapGoal = s.clutch ? clamp(over, 0.0, 1.0) : 1.0;
    s.flap += (flapGoal - s.flap) * clamp(dt / kFlapS, 0.0, 1.0);
    s.flapOpenFor = s.clutch && s.flap > 0.97 ? s.flapOpenFor + dt : 0.0;

    // Le volume entre les étages : ce que les rotors y mettent, moins ce qui repart par le volet et ce que prend le turbo.
    // Implicite : grand ouvert, le volet relie ce petit volume à l'entrée presque sans perte, et le pas de 1 ms ne le tiendrait pas.
    const double rotor = s.rotorOmega;
    const double taken = s.unit[0].compressorFlow;
    const double interK = s.interstageK;
    const double opening = s.flap;
    const double area = spec_.flapArea;
    const auto flapAt = [&k, inletPa, inletK, interK, opening, area](double pa) {
        const double upstream = pa > inletPa ? pa : inletPa;
        const double rho = upstream / (k.airR * (pa > inletPa ? interK : inletK));
        return supercharger_laws::discharge() * area * opening * rho * supercharger_laws::orificeSpeed(pa - inletPa, rho);
    };
    const auto surplus = [&c, &flapAt, rotor, inletPa, inletK, taken](double pa) {
        return supercharger_laws::compressorFlow(c, rotor, inletPa, inletK, pa) - flapAt(pa) - taken;
    };
    const double gain = k.airR * interK / spec_.interstageVolumeM3;
    const double now = surplus(s.interstagePa);
    const double slope = (surplus(s.interstagePa + 200.0) - now) / 200.0;
    s.interstagePa += dt * gain * now / (1.0 - dt * gain * (slope < 0.0 ? slope : 0.0));
    if (s.interstagePa < 0.5 * k.atmosphere) s.interstagePa = 0.5 * k.atmosphere;
    const double flowSc = supercharger_laws::compressorFlow(c, rotor, inletPa, inletK, s.interstagePa);
    const double flapFlow = flapAt(s.interstagePa);
    double outK = inletK;
    const double power = rotor > 1.0 ? supercharger_laws::shaftPower(c, rotor, inletPa, inletK, s.interstagePa, flowSc, &outK) : 0.0;
    const double load = rotor > 1.0 ? power / rotor : 0.0;

    // Les rotors : l'embrayage les lance en glissant, puis les tient au rapport du vilebrequin. Ouvert, ils ralentissent seuls.
    if (s.clutch && !s.clutchLocked) {
        s.rotorOmega += (spec_.clutchNm - load) / spec_.rotorInertia * dt;
        if (s.rotorOmega >= driven) {
            s.rotorOmega = driven;
            s.clutchLocked = true;
        }
        s.slipW = spec_.clutchNm * positive(driven - s.rotorOmega);
        s.chargerNm = spec_.clutchNm * c.driveRatio / c.driveEfficiency;
    } else if (s.clutch) {
        s.rotorOmega = driven;
        s.slipW = 0.0;
        s.chargerNm = crank > 20.0 ? power / c.driveEfficiency / crank : 0.0;
    } else {
        s.rotorOmega -= load / spec_.rotorInertia * dt;
        if (s.rotorOmega < 1.0) s.rotorOmega = 0.0;
        s.slipW = 0.0;
        s.chargerNm = 0.0;
    }
    s.chargerFlow = flowSc;
    s.flapFlow = flapFlow;
    s.chargerOutK = rotor > 1.0 && flowSc > 0.0 ? outK : inletK;
    // L'air qui sort des rotors, mêlé à celui qui les contourne.
    const double hot = positive(flowSc);
    const double cool = positive(-flapFlow);
    const double mixK = hot + cool > 1e-6 ? (hot * s.chargerOutK + cool * inletK) / (hot + cool) : s.interstageK;
    s.interstageK += (mixK - s.interstageK) * clamp(dt / k.outletS, 0.0, 1.0);
}

void TwinTurbo::step(double dt, const TwinTurboInput& input) {
    using namespace turbo_laws;
    const Constants& k = constants();
    TwinTurboState& s = state_;
    const TwinTurboSpec& spec = spec_;
    const TurboSpec& ref = spec.reference;
    const double rpm = input.rpm > 0.0 ? input.rpm : 0.0;
    const double fill = clamp(input.fill, 0.0, 1.0);
    const double targetBar = input.targetBar >= 0.0 ? input.targetBar : spec.targetBar;
    const double targetPa = k.atmosphere + targetBar * 1e5;
    const bool sequential = spec.layout == TurboLayout::TwinSequential;
    const bool charger = spec.layout == TurboLayout::Twincharger;
    const int units = spec.units;
    TurboState& a = s.unit[0];

    if (sequential) stepSequence(dt, fill);
    if (charger) stepCharger(dt, rpm, fill, targetPa);

    // ── L'air : chaque compresseur par son conduit, jusqu'au volume commun ──
    const double chargePa = a.chargePa;
    const double coolK = a.coolerOutK;
    const double chargeRho = chargePa / (k.airR * coolK);
    const bool secondaryFeeds = units == 2 && (!sequential || s.secondaryOnline);
    double drawn = a.compressorFlow;
    double into = a.compressorFlow;
    if (units == 2) {
        drawn += s.unit[1].compressorFlow - (secondaryFeeds ? 0.0 : s.recirculationFlow);
        if (secondaryFeeds) into += s.unit[1].compressorFlow;
    }
    const double commonInletPa = k.atmosphere - ref.filterDrop * drawn * drawn;
    const double inletK = inletTemperature(spec.unit[0], rpm);
    const double outletPa = chargePa + ref.coolerDrop * into * magnitude(into) / chargeRho;
    std::array<double, 2> flowNext{0.0, 0.0};
    std::array<double, 2> unitInletK{inletK, inletK};
    for (int i = 0; i < units; ++i) {
        const std::size_t u = static_cast<std::size_t>(i);
        TurboState& t = s.unit[u];
        const TurboSpec& unit = spec.unit[u];
        if (charger) {
            t.inletPa = s.interstagePa;
            unitInletK[u] = s.interstageK;
        } else {
            t.inletPa = commonInletPa;
        }
        t.outletPa = (i == 1 && !secondaryFeeds) ? s.secondaryPa : outletPa;
        // La colonne d'air du conduit, comme `TurboCharger::step` : implicite sur la pente descendante.
        const double tip = t.shaftOmega * 0.5 * unit.wheelDiameterM;
        const double flowNow = t.compressorFlow;
        double x = 0.0;
        const double headNow = compressorHead(unit, unitInletK[u], flowNow, tip, &x);
        const double delta = 1e-4 + 1e-3 * magnitude(flowNow);
        const double headNext = compressorHead(unit, unitInletK[u], flowNow + delta, tip);
        const double supplied = t.inletPa * pressureRatio(headNow, unitInletK[u]);
        double slope = t.inletPa * (pressureRatio(headNext, unitInletK[u]) - pressureRatio(headNow, unitInletK[u])) / delta;
        if (slope > 0.0) slope = 0.0;
        const double damped = clamp(flowNow, -k.ductDampedShare * unit.ratedFlow, k.ductDampedShare * unit.ratedFlow);
        const double ductLoss = k.ductLossAtRated / unit.ratedFlow * damped;
        const double lossSlope = flowNow > -k.ductDampedShare * unit.ratedFlow && flowNow < k.ductDampedShare * unit.ratedFlow ? k.ductLossAtRated / unit.ratedFlow : 0.0;
        const double rate = dt / unit.ductInertance;
        flowNext[u] = flowNow + rate * (supplied - t.outletPa - ductLoss) / (1.0 + rate * (lossSlope - slope));
        t.compressorFlow = flowNext[u];
    }

    // Le secondaire d'un séquentiel, avant son clapet : il souffle dans un petit volume, rendu à son entrée par une vanne.
    if (sequential) {
        TurboState& b = s.unit[1];
        if (!s.secondaryOnline) {
            const double across = s.secondaryPa - b.inletPa;
            const double rho = (across > 0.0 ? s.secondaryPa : b.inletPa) / (k.airR * (across > 0.0 ? b.compressorOutK : inletK));
            s.recirculationFlow = supercharger_laws::discharge() * spec.recirculationArea * recirculation_ * rho * supercharger_laws::orificeSpeed(across, rho);
            s.secondaryPa += k.airR * b.compressorOutK / spec.secondaryVolumeM3 * (flowNext[1] - s.recirculationFlow) * dt;
            if (s.secondaryPa < 0.5 * k.atmosphere) s.secondaryPa = 0.5 * k.atmosphere;
            // Le clapet s'ouvre de lui-même quand le secondaire pousse plus fort que la charge, une fois la vanne d'échappement ouverte.
            if (s.stage == 2 && s.secondaryPa >= chargePa) s.secondaryOnline = true;
        } else {
            s.recirculationFlow = 0.0;
            s.secondaryPa = b.outletPa;
        }
    }

    // ── Le volume commun, le papillon, la soupape de décharge ──
    double feeding = flowNext[0];
    if (units == 2 && (!sequential || s.secondaryOnline)) feeding += flowNext[1];
    a.plenumPa = spec.diesel ? chargePa : chargePa * fill;
    const double plenumRho = a.plenumPa / (k.airR * coolK);
    const double engine = k.volumetric * input.breathing * plenumRho * spec.displacementM3 * rpm / 120.0;
    a.engineFlow = engine;
    double wantedValve = 0.0;
    if (spec.blowOffValve && !spec.diesel) {
        const double vacuum = clamp((k.blowOffVacuumPa - a.plenumPa) / k.blowOffVacuumSpanPa, 0.0, 1.0);
        const double gauge = chargePa - k.atmosphere;
        if (vacuum > 0.0 && gauge > k.blowOffOpensAbovePa) valveHeld_ = true;
        if (vacuum <= 0.0 || (a.blowOff > 0.5 && a.blowOffFlow < 0.05 * ref.ratedFlow)) valveHeld_ = false;
        if (valveHeld_) wantedValve = vacuum;
    }
    a.blowOff += (wantedValve - a.blowOff) * (dt / k.blowOffS < 1.0 ? dt / k.blowOffS : 1.0);
    double valveFlow = 0.0;
    const double across = chargePa - k.atmosphere;
    if (a.blowOff > 1e-4 && across > 0.0) valveFlow = k.blowOffDischarge * spec.blowOffArea * a.blowOff * std::sqrt(2.0 * chargeRho) * across / std::sqrt(across + 500.0);
    a.blowOffFlow = valveFlow;
    const double gain = k.airR * coolK / spec.chargeVolumeM3;
    a.chargePa += gain * (feeding - engine - valveFlow) * dt;
    if (a.chargePa < 0.5 * k.atmosphere) a.chargePa = 0.5 * k.atmosphere;

    // ── Les températures : chaque sortie de compresseur, puis l'échangeur commun ──
    std::array<double, 2> heads{0.0, 0.0};
    std::array<double, 2> etas{1.0, 1.0};
    double mixFlow = 0.0;
    double mixHeat = 0.0;
    for (int i = 0; i < units; ++i) {
        const std::size_t u = static_cast<std::size_t>(i);
        TurboState& t = s.unit[u];
        const TurboSpec& unit = spec.unit[u];
        const double tip = t.shaftOmega * 0.5 * unit.wheelDiameterM;
        double x = 0.0;
        heads[u] = compressorHead(unit, unitInletK[u], flowNext[u], tip, &x);
        etas[u] = compressorEta(x);
        const double outTarget = flowNext[u] > 0.0 ? unitInletK[u] + (heads[u] > 0.0 ? heads[u] : 0.0) / etas[u] / k.airCp : t.compressorOutK;
        t.compressorOutK += (outTarget - t.compressorOutK) * dt / k.outletS;
        const bool feeds = i == 0 || secondaryFeeds;
        if (feeds && flowNext[u] > 0.0) {
            mixFlow += flowNext[u];
            mixHeat += flowNext[u] * t.compressorOutK;
        }
    }
    if (mixFlow > 0.02 * ref.ratedFlow) {
        a.coolerOutK += (coolerOutlet(mixHeat / mixFlow, mixFlow, ref.ratedFlow, ref.ambientK) - a.coolerOutK) * dt / k.coolerMassS;
    } else {
        a.coolerOutK += (k.ambientK - a.coolerOutK) * dt / k.coolerRestS;
    }

    // ── L'échappement ──
    const double fuel = fuelPerAir(ref, fill, input.burning);
    const double gas = engine * (1.0 + fuel);
    const double hotTarget = rpm > 50.0 ? exhaustTemperature(ref, coolK, rpm, fill, fuel) : k.ambientK;
    const double outPa = k.atmosphere + ref.lineDrop * gas * gas;
    // Les soupapes et les aubes : la même loi que le turbo simple, sur la pression commune.
    const double anticipated = a.chargePa + k.wastegateLeadS * (a.chargePa - chargePa) / dt;
    double overBar = (anticipated - (k.atmosphere + (1.0 - 0.2 * k.wastegateBand) * targetBar * 1e5)) / (k.wastegateBand * targetBar * 1e5);
    // Compresseur embrayé : le volet règle la pression, la soupape du turbo reste fermée pour le lancer, sauf 10 % de trop.
    if (charger && s.clutch) overBar = (a.chargePa - (k.atmosphere + 1.1 * targetBar * 1e5)) / (k.wastegateBand * targetBar * 1e5);
    std::array<double, 2> passage{0.0, 0.0};
    for (int i = 0; i < units; ++i) {
        const std::size_t u = static_cast<std::size_t>(i);
        TurboState& t = s.unit[u];
        const TurboSpec& unit = spec.unit[u];
        const double tip = t.shaftOmega * 0.5 * unit.wheelDiameterM;
        const double overSpeed = (tip - unit.tipLimit) / (0.02 * unit.tipLimit);
        double wanted = clamp(overBar > overSpeed ? overBar : overSpeed, 0.0, 1.0);
        if (input.wastegate >= 0.0) wanted = clamp(input.wastegate > overSpeed ? input.wastegate : overSpeed, 0.0, 1.0);
        // Le secondaire qu'on lance garde sa soupape fermée : tout le peu de gaz qu'il reçoit doit passer par sa roue.
        if (sequential && i == 1 && s.stage < 2) wanted = clamp(overSpeed, 0.0, 1.0);
        if (spec.variableGeometry) {
            const double goal = spec.vaneMin + (spec.vaneMax - spec.vaneMin) * wanted;
            s.vane[u] += (goal - s.vane[u]) * clamp(dt / kVaneS, 0.0, 1.0);
            t.wastegate = 0.0;
            passage[u] = unit.turbineK * s.vane[u];
        } else {
            t.wastegate += (wanted - t.wastegate) * dt / k.wastegateS;
            passage[u] = unit.turbineK + unit.wastegateK * t.wastegate;
        }
    }

    // Le partage du gaz : collecteurs séparés en parallèle ; un collecteur commun et la vanne du secondaire en séquentiel.
    std::array<double, 2> outlet{outPa, outPa};
    if (units == 2 && sequential) {
        const double secondary = s.exhaustValve * passage[1];
        const double total = passage[0] + secondary;
        const double manifold = std::sqrt(outPa * outPa + gas * gas * s.unit[0].turbineInK / (total * total));
        gas_[0] = gas * passage[0] / total;
        gas_[1] = gas * secondary / total;
        s.unit[0].turbineInPa = manifold;
        s.unit[1].turbineInPa = manifold;
        // La vanne est après la turbine secondaire : à demi fermée, elle prend une part de la détente.
        const double behind = manifold * manifold - gas_[1] * gas_[1] * s.unit[1].turbineInK / (passage[1] * passage[1]);
        outlet[1] = behind > outPa * outPa ? std::sqrt(behind) : outPa;
    } else {
        for (int i = 0; i < units; ++i) {
            const std::size_t u = static_cast<std::size_t>(i);
            gas_[u] = gas * spec.gasShare[u];
            s.unit[u].turbineInPa = std::sqrt(outPa * outPa + gas_[u] * gas_[u] * s.unit[u].turbineInK / (passage[u] * passage[u]));
        }
    }

    // ── Les arbres ──
    const double pulse = input.burning ? k.pulsePa * (spec.diesel ? 0.5 + 0.5 * fill : fill) * (1.0 - clamp(rpm / spec.redlineRpm, 0.0, 1.0)) : 0.0;
    for (int i = 0; i < units; ++i) {
        const std::size_t u = static_cast<std::size_t>(i);
        TurboState& t = s.unit[u];
        TurboSpec& unit = spec_.unit[u];
        // Chaque collecteur chauffe à son rythme ; la rangée aux tubes plus longs envoie un gaz un peu moins chaud.
        const double heat = units == 2 && !sequential ? spec.heatShare[u] : 1.0;
        const double target = gas_[u] > 0.01 * unit.ratedFlow ? k.ambientK + (hotTarget - k.ambientK) * heat : k.ambientK;
        const double hotTau = target > t.turbineInK ? k.heatUpS : k.coolDownS;
        t.turbineInK += (target - t.turbineInK) * dt / hotTau;
        unit.turbineEtaShare = vaneEta(spec, s.vane[u]);
        const double vanePassage = unit.turbineK * s.vane[u];
        const double through = passage[u] > 0.0 ? gas_[u] * vanePassage / passage[u] : 0.0;
        const double tip = t.shaftOmega * 0.5 * unit.wheelDiameterM;
        const double power = turbinePower(unit, through, t.turbineInK, t.turbineInPa, outlet[u], t.shaftOmega, gas_[u] > 0.0 ? pulse : 0.0);
        const double compressor = (flowNext[u] > 0.0 && heads[u] > 0.0 ? flowNext[u] * heads[u] / etas[u] : 0.0) + windagePower(unit, tip);
        t.turbinePowerW = power;
        t.compressorPowerW = compressor;
        omegaSquared_[u] += 2.0 * (power - compressor - bearingPower(unit, t.shaftOmega)) / unit.inertia * dt;
        if (omegaSquared_[u] < 0.0) omegaSquared_[u] = 0.0;
        t.shaftOmega = std::sqrt(omegaSquared_[u]);
        t.shaftShare = t.shaftOmega / unit.referenceOmega;
    }

    // Les grandeurs communes, recopiées sur le second arbre pour qui le lit.
    TurboState& b = s.unit[1];
    b.chargePa = a.chargePa;
    b.plenumPa = a.plenumPa;
    b.coolerOutK = a.coolerOutK;
    b.engineFlow = a.engineFlow;
    b.blowOff = a.blowOff;
    b.blowOffFlow = 0.0;
}

}  // namespace revforge
