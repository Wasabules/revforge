#include "revforge/gearbox.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kSteelDensity = 7850.0;
constexpr double kPressureAngleRad = 20.0 * kPi / 180.0;
// Les roues sont évidées et portées par un moyeu : elles ne sont pas des disques pleins.
constexpr double kWebShare = 0.6;

// Tables de route, mesurées sur de vraies voitures. La dernière est celle d'une ZF à huit rapports.
constexpr std::array<double, 5> kRoad5{3.42, 1.81, 1.28, 0.98, 0.77};
constexpr std::array<double, 6> kRoad6{3.55, 2.05, 1.45, 1.12, 0.89, 0.73};
constexpr std::array<double, 7> kRoad7{3.6, 2.4, 1.8, 1.4, 1.12, 0.92, 0.76};
constexpr std::array<double, 8> kRoad8{4.71, 3.14, 2.11, 1.67, 1.29, 1.0, 0.84, 0.67};

// Pour les rapports d'une famille : le premier et le dernier, le resserrement vers le haut,
// et de combien le dernier rapport tire par rapport à celui qui atteint juste la puissance maximale.
struct FamilyShape {
    double first;
    double top;
    double progression;
    double topFactor;
};

constexpr FamilyShape kMatchedShape{3.6, 0.76, 1.1, 1.0};
constexpr FamilyShape kShortShape{3.25, 0.95, 1.06, 1.25};
constexpr FamilyShape kCloseShape{2.92, 0.85, 1.04, 1.05};
constexpr FamilyShape kLongShape{3.8, 0.68, 1.1, 0.8};

// Calés sur des boîtes réelles : 300 Nm pour 70 mm d'entraxe, en supposant le couple proportionnel au cube.
constexpr double kReferenceTorqueNm = 300.0;
constexpr double kReferenceCenterMm = 70.0;
// Contrainte de flexion admissible ramenée à la formule de Lewis, calée pour que la boîte de référence tienne.
constexpr double kBendingFactor = 0.243;

// Cône de synchro (Socin et Walters) : µ = 0,1, 6,5° de demi-angle, 250 N sur le baladeur.
constexpr double kConeFriction = 0.1;
constexpr double kConeSinAngle = 0.113;
constexpr double kShiftForceN = 250.0;

// Rendement sous charge d'un étage, hors roulements (Joachim, Börner et Kurz 2009).
constexpr double kHelicalMesh = 0.985;
constexpr double kSpurMesh = 0.99;
constexpr double kHypoidMesh = 0.955;
constexpr double kSeals = 0.97;
constexpr double kTransferCase = 0.97;

double squared(double v) { return v * v; }

/** Racine cubique par la méthode de Newton : mêmes résultats partout. */
double cubeRoot(double v) {
    double x = v < 1.0 ? 1.0 : v;
    for (int i = 0; i < 40; ++i) x -= (x * x * x - v) / (3.0 * x * x);
    return x;
}

/** Développements limités : la denture reste bien en dessous de 45°, et le résultat ne dépend d'aucune bibliothèque. */
double cosSmall(double a) {
    const double b = a * a;
    return 1.0 - b / 2.0 + b * b / 24.0 - b * b * b / 720.0 + b * b * b * b / 40320.0;
}
double sinSmall(double a) {
    const double b = a * a;
    return a * (1.0 - b / 6.0 + b * b / 120.0 - b * b * b / 5040.0 + b * b * b * b / 362880.0);
}

int greatestCommonDivisor(int a, int b) {
    while (b != 0) {
        const int r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/** Une progression géométrique qui se resserre vers les hauts rapports, comme sur une vraie boîte. */
void progressive(int gears, const FamilyShape& shape, double* out) {
    if (gears == 1) {
        out[0] = shape.top;
        return;
    }
    const double steps = gears - 1;
    const double triangle = steps * (steps - 1.0) / 2.0;
    double spread = shape.first / shape.top;
    for (int i = 0; i < static_cast<int>(triangle); ++i) spread /= shape.progression;
    // Racine (gears - 1)-ième, par dichotomie : mêmes résultats partout.
    double low = 1.0, high = 4.0;
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        double power = 1.0;
        for (int k = 0; k < gears - 1; ++k) power *= middle;
        (power > spread ? high : low) = middle;
    }
    const double step = 0.5 * (low + high);
    double value = shape.top;
    double grow = step;
    out[gears - 1] = value;
    for (int i = gears - 2; i >= 0; --i) {
        value *= grow;
        grow *= shape.progression;
        out[i] = value;
    }
}

const FamilyShape& shapeOf(GearFamily family) {
    switch (family) {
        case GearFamily::Short: return kShortShape;
        case GearFamily::Close: return kCloseShape;
        case GearFamily::Long: return kLongShape;
        case GearFamily::Matched:
        case GearFamily::Custom: break;
    }
    return kMatchedShape;
}

/** Rapports voulus, de la première à la dernière. Ils sont forcés à décroître : un rapport ne peut pas en doubler un autre. */
void nominalRatios(const GearboxConfig& config, int gears, double* out) {
    if (config.family == GearFamily::Custom) {
        double previous = 0.0;
        for (int i = gears - 1; i >= 0; --i) {
            const double wanted = static_cast<double>(config.ratios[static_cast<std::size_t>(i)]);
            double value = wanted > 0.2 ? wanted : (previous > 0.0 ? previous * 1.3 : 0.8);
            if (previous > 0.0) value = std::max(value, previous * 1.02);
            out[i] = value;
            previous = value;
        }
        return;
    }
    if (config.family == GearFamily::Matched) {
        const double* table = nullptr;
        if (gears == 5) table = kRoad5.data();
        else if (gears == 6) table = kRoad6.data();
        else if (gears == 7) table = kRoad7.data();
        else if (gears == 8) table = kRoad8.data();
        if (table != nullptr) {
            for (int i = 0; i < gears; ++i) out[i] = table[i];
            return;
        }
    }
    progressive(gears, shapeOf(config.family), out);
}

/**
 * Des dents entières pour ce rapport, à entraxe à peu près constant. On préfère des nombres premiers entre eux :
 * deux dents se retrouvent alors le plus rarement possible, et leurs défauts ne se répètent pas à chaque tour.
 */
GearPair choosePair(double ratio, int toothSum, int minTeeth) {
    GearPair best;
    double bestScore = 1e9;
    for (int sum = toothSum - 2; sum <= toothSum + 2; ++sum) {
        const double ideal = static_cast<double>(sum) / (1.0 + ratio);
        const int centre = static_cast<int>(ideal + 0.5);
        for (int driver = std::max(centre - 1, minTeeth); driver <= std::max(centre + 1, minTeeth); ++driver) {
            const int driven = sum - driver;
            if (driven < minTeeth) continue;
            const double actual = static_cast<double>(driven) / driver;
            double score = std::fabs(actual / ratio - 1.0) + 0.0015 * std::abs(sum - toothSum);
            if (greatestCommonDivisor(driver, driven) != 1) score += 0.006;
            if (score < bestScore) {
                bestScore = score;
                best.driver = driver;
                best.driven = driven;
            }
        }
    }
    // Rapport trop grand pour cet entraxe : on garde le plus petit pignon et on allonge la grande roue.
    if (best.driver == 0) {
        best.driver = minTeeth;
        best.driven = static_cast<int>(minTeeth * ratio + 0.5);
    }
    return best;
}

/** Rapports de conduite de profil et de recouvrement, à partir de la géométrie d'une denture normale. */
void contactRatios(GearPair& pair, double moduleMm, double helixRad, double faceMm) {
    const double cosHelix = cosSmall(helixRad);
    const double transverse = moduleMm / cosHelix;
    const double tanNormal = sinSmall(kPressureAngleRad) / cosSmall(kPressureAngleRad);
    const double tanTransverse = tanNormal / cosHelix;
    const double cosAngle = 1.0 / std::sqrt(1.0 + tanTransverse * tanTransverse);
    const double sinAngle = tanTransverse * cosAngle;
    const double r1 = pair.driver * transverse / 2.0;
    const double r2 = pair.driven * transverse / 2.0;
    const double base1 = r1 * cosAngle;
    const double base2 = r2 * cosAngle;
    const double approach = std::sqrt(squared(r1 + moduleMm) - base1 * base1);
    const double recess = std::sqrt(squared(r2 + moduleMm) - base2 * base2);
    const double length = approach + recess - (r1 + r2) * sinAngle;
    pair.profile = static_cast<float>(std::clamp(length / (kPi * transverse * cosAngle), 1.05, 2.4));
    pair.overlap = static_cast<float>(faceMm * sinSmall(helixRad) / (kPi * moduleMm));
}

double gearInertia(int teeth, double transverseMm, double faceMm) {
    const double d = teeth * transverseMm * 1e-3;
    return kPi / 32.0 * kSteelDensity * faceMm * 1e-3 * d * d * d * d * kWebShare;
}

/** Un pont réel : petit pignon, grande couronne, nombres premiers entre eux. */
GearPair chooseFinal(double ratio, bool hypoid) {
    GearPair best;
    double bestError = 1e9;
    const int lowPinion = hypoid ? 8 : 12;
    const int highPinion = hypoid ? 14 : 22;
    for (int pinion = lowPinion; pinion <= highPinion; ++pinion) {
        const int ring = static_cast<int>(pinion * ratio + 0.5);
        const int sum = pinion + ring;
        // La couronne ne peut pas grandir sans fin : le carter a sa taille.
        const double tooBig = sum > (hypoid ? 56 : 90) ? 0.01 * (sum - (hypoid ? 56 : 90)) : 0.0;
        const double error = std::fabs(static_cast<double>(ring) / pinion / ratio - 1.0) + tooBig + (greatestCommonDivisor(pinion, ring) != 1 ? 0.004 : 0.0);
        if (error < bestError) {
            bestError = error;
            best.driver = pinion;
            best.driven = ring;
        }
    }
    return best;
}

}  // namespace

double GearboxSpec::coneTorqueFor(int gear) const { return gear >= 1 && gear <= 2 ? coneTorqueNm * 2.0 : coneTorqueNm; }

double GearboxSpec::meshHz(int gear, double inputRpm) const {
    const double turnsPerS = inputRpm / 60.0;
    if (gear < 1 || gear > gears) return 0.0;
    const GearPair& pair = pairs[static_cast<std::size_t>(gear - 1)];
    return threeShafts ? turnsPerS / inputPair.ratio() * pair.driver : turnsPerS * pair.driver;
}

double drivelineEfficiency(GearboxKind kind, ToothForm form, bool rearOrAll, bool allWheels) {
    const double stage = form == ToothForm::Spur ? kSpurMesh : kHelicalMesh;
    double efficiency = stage * stage * kSeals;
    // Le moteur électrique est monté sur l'essieu : deux étages cylindriques, pas de couple conique.
    efficiency *= rearOrAll && kind != GearboxKind::Reducer ? kHypoidMesh : kHelicalMesh;
    if (allWheels) efficiency *= kTransferCase;
    // Le convertisseur ponté et sa pompe de pression prennent leur part.
    if (kind == GearboxKind::Converter) efficiency *= 0.96;
    else if (kind == GearboxKind::DualClutch) efficiency *= 0.985;
    return efficiency;
}

GearboxSpec designGearbox(const GearboxConfig& config, const GearboxContext& context) {
    GearboxSpec spec;
    spec.kind = context.electric ? GearboxKind::Reducer : config.kind;
    spec.form = spec.kind == GearboxKind::Reducer ? ToothForm::Helical : config.form;
    spec.flywheel = config.flywheel;
    spec.threeShafts = context.rearOrAll && spec.kind != GearboxKind::Reducer;
    spec.hypoid = context.rearOrAll && spec.kind != GearboxKind::Reducer;

    // Taille : couple admissible, entraxe, largeur. Le couple varie comme le cube de l'entraxe.
    // Sans couple imposé, une marge au-dessus de ce que l'embrayage peut passer. Un convertisseur double le couple au décollage.
    const double margin = config.kind == GearboxKind::Converter ? 2.4 : 1.7;
    const double wantedLimit = config.limitNm > 0.0f ? static_cast<double>(config.limitNm) : margin * context.engineTorqueNm;
    const double center = kReferenceCenterMm * cubeRoot(wantedLimit / kReferenceTorqueNm);
    spec.centerMm = center;
    const bool spur = spec.form == ToothForm::Spur;
    const double helix = spur ? 0.0 : (config.helixDeg > 0.0f ? static_cast<double>(config.helixDeg) : (spec.kind == GearboxKind::Reducer ? 22.0 : 28.0));
    spec.helixDeg = helix;
    const double helixRad = helix * kPi / 180.0;
    const double cosHelix = cosSmall(helixRad);
    // Une denture droite de course est plus grosse et plus étroite : la place manque pour six rapports.
    spec.moduleMm = config.moduleMm > 0.0f ? static_cast<double>(config.moduleMm) : (spur ? center / 22.0 : center / 28.0);
    spec.faceMm = spur ? 0.22 * center : 0.27 * center;
    const double transverse = spec.moduleMm / cosHelix;
    const int toothSum = std::max(static_cast<int>(2.0 * center / transverse + 0.5), 28);
    const int minTeeth = spur ? 12 : 10;

    const int gears = spec.kind == GearboxKind::Reducer ? 1 : std::clamp(config.gears > 0 ? config.gears : context.defaultGears, 2, kMaxGears);
    spec.gears = gears;

    double nominal[kMaxGears]{};
    nominalRatios(config, gears, nominal);

    const FamilyShape& shape = shapeOf(config.family);
    const bool customFinal = config.finalDrive > 0.0f;

    if (spec.kind == GearboxKind::Reducer) {
        // Deux étages : un pignon rapide sur une roue intermédiaire, puis un second pignon sur la couronne.
        const double wanted = customFinal ? static_cast<double>(config.finalDrive) : context.topOverall;
        const double first = std::sqrt(wanted) * 0.78;
        spec.inputPair = choosePair(first, 31 + static_cast<int>(31.0 * first + 0.5), 17);
        const double rest = wanted / spec.inputPair.ratio();
        GearPair second;
        second.driver = 23;
        second.driven = static_cast<int>(23.0 * rest + 0.5);
        spec.pairs[0] = spec.inputPair;
        spec.finalPair = second;
        spec.boxRatio[0] = spec.inputPair.ratio();
        spec.finalDrive = second.ratio();
        contactRatios(spec.pairs[0], spec.moduleMm, helixRad, spec.faceMm);
        contactRatios(spec.finalPair, spec.moduleMm, helixRad, spec.faceMm);
        spec.inputPair = spec.pairs[0];
    } else {
        // Boîte longitudinale : une paire d'entrée, toujours chargée, et l'avant-dernier rapport proche de 1 passe en prise directe.
        double inputRatio = 1.0;
        if (spec.threeShafts) {
            spec.inputPair = choosePair(1.45, toothSum, minTeeth);
            contactRatios(spec.inputPair, spec.moduleMm, helixRad, spec.faceMm);
            inputRatio = spec.inputPair.ratio();
        }
        int closest = 0;
        double closestGap = 0.03;
        if (spec.threeShafts) {
            for (int i = 0; i < gears; ++i) {
                const double gap = std::fabs(nominal[i] - 1.0);
                if (gap < closestGap) {
                    closestGap = gap;
                    closest = i + 1;
                }
            }
        }
        spec.directGear = closest;
        for (int i = 0; i < gears; ++i) {
            GearPair& pair = spec.pairs[static_cast<std::size_t>(i)];
            if (i + 1 == closest) {
                // Prise directe : l'arbre primaire est relié à l'arbre de sortie, aucune denture ne travaille.
                pair.driver = 1;
                pair.driven = 1;
                pair.profile = 2.0f;
                pair.overlap = 1.0f;
                spec.boxRatio[static_cast<std::size_t>(i)] = 1.0;
                continue;
            }
            pair = choosePair(nominal[i] / inputRatio, toothSum, minTeeth);
            contactRatios(pair, spec.moduleMm, helixRad, spec.faceMm);
            spec.boxRatio[static_cast<std::size_t>(i)] = inputRatio * pair.ratio();
        }
        // Le dernier rapport décide du pont, s'il n'a pas été choisi.
        const double top = spec.boxRatio[static_cast<std::size_t>(gears - 1)];
        const double finalWanted = customFinal ? static_cast<double>(config.finalDrive) : context.topOverall * shape.topFactor / top;
        spec.finalPair = chooseFinal(finalWanted, spec.hypoid);
        const double finalHelix = spec.hypoid ? 35.0 * kPi / 180.0 : helixRad;
        // Une couronne de pont a une large denture : au moins deux dents en prise à la fois en denture conique.
        contactRatios(spec.finalPair, spec.moduleMm * 1.1, finalHelix, spec.hypoid ? spec.faceMm * 1.3 : spec.faceMm * 1.2);
        if (spur && !spec.hypoid) spec.finalPair.overlap = 0.0f;
        spec.finalDrive = spec.finalPair.ratio();

        // Marche arrière : deux engrènements en denture droite, pignon intermédiaire, sans synchro le plus souvent.
        spec.reverseIn.driver = minTeeth + 3;
        spec.reverseIn.driven = std::max(minTeeth + 8, 22);
        spec.reverseOut.driver = spec.reverseIn.driven;
        spec.reverseOut.driven = static_cast<int>((3.45 / inputRatio) * spec.reverseIn.driver + 0.5);
        contactRatios(spec.reverseIn, spec.moduleMm, 0.0, spec.faceMm);
        contactRatios(spec.reverseOut, spec.moduleMm, 0.0, spec.faceMm);
        spec.reverseRatio = inputRatio * spec.reverseIn.ratio() * spec.reverseOut.ratio();
    }

    // Plus le pignon de première est petit, plus sa dent est faible : le module décide de ce qu'il tient en flexion.
    const GearPair& low = spec.pairs[0];
    const int lowTeeth = std::max(low.driver, minTeeth);
    const double bending = kBendingFactor * lowTeeth * spec.moduleMm * spec.moduleMm * spec.faceMm / cosHelix;
    spec.limitNm = std::min(wantedLimit, bending);

    // Inertie vue de l'arbre primaire : le disque d'embrayage, et les roues qu'il entraîne à leur vitesse.
    const double scale = wantedLimit / kReferenceTorqueNm;
    double carried = 0.0;
    for (int i = 0; i < gears; ++i) {
        const GearPair& pair = spec.pairs[static_cast<std::size_t>(i)];
        if (pair.driver <= 1) continue;
        carried += 0.5 * gearInertia(pair.driver, transverse, spec.faceMm);
        carried += 0.5 * gearInertia(pair.driven, transverse, spec.faceMm) * squared(static_cast<double>(pair.driver) / pair.driven);
    }
    const double disc = 0.006 * scale;
    spec.inputInertia = disc + 0.001 * scale + carried;

    // Le cône : µ·F·R / sin α, d'après Socin et Walters. Pas de cône sur une boîte à crabots.
    spec.coneRadiusM = 0.45 * center * 1e-3;
    const bool hasCones = spec.kind == GearboxKind::Synchro;
    spec.coneTorqueNm = hasCones ? kConeFriction * kShiftForceN * spec.coneRadiusM / kConeSinAngle : 0.0;

    spec.dragNm = 0.25 * cubeRoot(scale);
    spec.dragPerRad = 0.002 * cubeRoot(scale);
    spec.meshEfficiency = drivelineEfficiency(spec.kind, spec.form, context.rearOrAll, context.allWheels);
    // Les dents de la couronne d'engagement : une trentaine sur un baladeur, une demi-douzaine de larges crabots.
    spec.dogTeeth = (spec.kind == GearboxKind::Dog || spec.kind == GearboxKind::Sequential) ? 6 : 36;

    // Les roues folles qui claquent : les plus grosses et les plus rapides, celles de l'autre côté de la paire en prise.
    for (int i = 0; i < kLooseGears; ++i) {
        const int index = gears <= 1 ? 0 : i % gears;
        const GearPair& pair = spec.pairs[static_cast<std::size_t>(index)];
        LooseGear& gear = spec.loose[static_cast<std::size_t>(i)];
        if (pair.driver <= 1 || i >= gears) continue;
        gear.inertia = static_cast<float>(gearInertia(pair.driven, transverse, spec.faceMm));
        gear.speedRatio = static_cast<float>(static_cast<double>(pair.driver) / pair.driven);
        const double pitchRadius = pair.driven * transverse * 0.5e-3;
        // 70 µm de jeu au primitif sur une boîte de série, le double sur une boîte de course à larges tolérances.
        const double backlash = (spur ? 150e-6 : 70e-6) * cubeRoot(scale);
        gear.gap = static_cast<float>(backlash / pitchRadius);
        gear.drag = static_cast<float>(0.18 * cubeRoot(scale) * (0.4 + 0.2 * i));
    }
    return spec;
}

}  // namespace revforge
