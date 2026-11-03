#include "revforge/engine_config.hpp"

#include <algorithm>
#include <cmath>

#include "kotlin_math.hpp"
#include "revforge/air_filter.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/injection.hpp"
#include "revforge/particulate_filter.hpp"
#include "portable_math.hpp"

namespace revforge {

namespace {

// Un V8 cross-plane allume à intervalles égaux, mais pas en alternance : gauche, droite, gauche,
// gauche, droite, gauche, droite, droite. Deux bouffées de suite dans une même rangée se gênent
// dans son collecteur : c'est ce déséquilibre qui fait le « glouglou ».
const std::array<bool, 8> kCrossPlaneLeft{true, false, true, true, false, true, false, false};
const std::array<float, 8> kCrossPlaneCrowded{1.0f, 1.0f, 1.0f, 0.74f, 1.0f, 1.0f, 1.0f, 0.74f};
const std::array<float, 6> kHeaderSpread{0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.8f};
const std::array<float, 6> kCylinderSpread{1.0f, 0.94f, 1.05f, 0.97f, 1.02f, 0.95f};

constexpr float kMinCarRpm = 100.0f;
constexpr float kSoftCeilingFrom = 0.8f;
// Pied levé, le circuit de ralenti débite encore, mais ce mélange très dilué par les gaz résiduels ne brûle qu'un cycle sur deux environ (supposé).
constexpr float kOverrunBurn = 0.5f;

/** Ce que le son doit savoir du carburateur. Le partage ne donne pas à chaque cylindre le même mélange : les explosions en diffèrent. */
void applyCarburetor(const EngineConfig& engine, EngineProfile& p) {
    const CarburetorSpec carb = carburetorSpecOf(engine);
    p.fuelSystem = carb.kind;
    p.carbThroats = carb.throats;
    p.carbThroatM = static_cast<float>(carb.throatM);
    p.carbRunnerM = static_cast<float>(carb.pipeLengthM + 0.15);
    p.overrunFuel = kOverrunBurn;
    if (carb.throats > 1) return;
    // Sur une pipe commune, les cylindres du bout sont plus pauvres que ceux du milieu : un sur deux dans l'ordre d'allumage d'un 4 en ligne.
    const double settled = 1.0 / carb.partLambda;
    const double lean = torqueOfRichness(settled * (1.0 - carb.spread)) / torqueOfRichness(settled);
    const double rich = torqueOfRichness(settled * (1.0 + carb.spread)) / torqueOfRichness(settled);
    for (int i = 0; i < p.firingCount; ++i) p.firingGain[i] *= static_cast<float>(i % 2 == 0 ? lean : rich);
}

/**
 * Ce que le son doit savoir de l'injection : le tic d'une injection directe, ce que la rampe commune et la recirculation font
 * au bruit de combustion, le filtre dans la ligne. Rien de tout cela sur l'injection indirecte et la pompe mécanique d'origine.
 */
void applyInjection(const EngineConfig& e, EngineProfile& p) {
    p.directInjection = directInjected(e);
    if (particulateFiltered(e)) {
        const ParticulateFilterSpec spec = particulateFilterSpecOf(e);
        ParticulateFilter mean;
        mean.configure(spec);
        p.filterResistance = static_cast<float>(filterLineResistance(spec, mean.sootG()));
    }
    p.commonRail = commonRailed(e);
    p.egr = recirculated(e);
}

// ── Le moteur rotatif : voir « Le moteur rotatif » dans docs/modele.md ──
// Proportions du moteur de référence : R 105, e 15, b 80 mm, soit 654 cm³ par chambre.
constexpr double kRotaryEccentricityShare = 15.0 / 105.0;
constexpr double kRotaryWidthShare = 80.0 / 105.0;
constexpr double kSqrtThree = 1.7320508075688772;
constexpr double kRotaryCompressionNatural = 9.4;
constexpr double kRotaryCompressionBoosted = 8.5;
constexpr double kPistonCompressionNatural = 10.5;
constexpr double kPistonCompressionBoosted = 9.5;
constexpr double kExpansionGamma = 1.3;
// Part de la cylindrée déjà parcourue quand la lumière s'ouvre, 195° d'arbre après le point mort haut : (1 - cos(2·195°/3))/2.
constexpr double kOpenedShare = 0.821;
// Section de la lumière d'échappement, en part de R·b : environ 14 cm² sur le moteur de référence (supposé, à 30 % près).
constexpr double kExhaustPortShare = 0.167;
// La référence du son : un cylindre de 500 cm³, à 10,5 de compression, et une soupape de 30 mm levée de 9 mm (8,5 cm²).
constexpr double kPistonBlowdownS = 0.002;
constexpr double kPistonVolumeOverAreaCm = 500.0 * (1.0 / 9.5 + kOpenedShare) / 8.5;
// Une unité de surface en plus, sur le rapport au cylindre carré, coûte cette part du rendement indiqué : une part de la chaleur
// perdue aux parois serait devenue du travail. Ordre de grandeur (Heywood : de 10 à 15 % de l'énergie du carburant passe aux parois).
constexpr double kWallLossPerRatio = 0.125;
// Fuites par les segments d'apex : la part que les mesures de compression laissent attendre (supposé).
constexpr double kApexLeakLoss = 0.05;

double cubeRoot(double value) {
    double x = 100.0;
    for (int i = 0; i < 48; ++i) x = (2.0 * x + value / (x * x)) / 3.0;
    return x;
}

double compressionOf(Aspiration aspiration, double natural, double boosted) { return aspiration != Aspiration::Natural ? boosted : natural; }

/** Le rendement du cycle idéal, à ce taux de compression. */
double ottoEfficiency(double ratio) { return 1.0 - portable::pow(ratio, 1.0 - kExpansionGamma); }

/** Ce qui change sur un rotatif, une fois le moteur rempli comme les autres : voir `EngineProfile::rotary`. */
void applyRotary(const EngineConfig& e, EngineProfile& p) {
    const int rotors = e.rotors();
    const int firings = 2 * rotors;
    const double chamberCc = static_cast<double>(e.displacementCc) / rotors;
    const double compression = compressionOf(e.aspiration, kRotaryCompressionNatural, kRotaryCompressionBoosted);
    const double pistonCompression = compressionOf(e.aspiration, kPistonCompressionNatural, kPistonCompressionBoosted);
    p.rotary = true;
    p.rotors = rotors;
    p.cylinders = firings;
    p.firingCount = firings;
    // Au même moment de la détente, un plus faible taux a moins détendu le gaz : la lumière s'ouvre sur plus de pression.
    const double expandedRotary = 1.0 + kOpenedShare * (compression - 1.0);
    const double expandedPiston = 1.0 + kOpenedShare * (pistonCompression - 1.0);
    const float release = static_cast<float>(portable::pow(expandedPiston / expandedRotary, kExpansionGamma));
    for (int i = 0; i < kMaxCylinders; ++i) {
        p.firing[static_cast<std::size_t>(i)] = 0.0f;
        p.firingGain[static_cast<std::size_t>(i)] = 0.0f;
        p.headerM[static_cast<std::size_t>(i)] = 0.0f;
        p.bank[static_cast<std::size_t>(i)] = 0;
    }
    for (int i = 0; i < firings; ++i) {
        // Une explosion par rotor et par tour d'arbre, à intervalles égaux : 360°/rotors, sur un cycle de deux tours.
        const std::size_t at = static_cast<std::size_t>(i);
        const std::size_t rotor = static_cast<std::size_t>(i % rotors);
        p.firing[at] = static_cast<float>(i) / static_cast<float>(firings);
        p.firingGain[at] = kCylinderSpread[rotor % kCylinderSpread.size()] * release;
        // Une lumière par rotor, donc un seul tube : deux explosions du même rotor passent par le même.
        p.headerM[at] = 0.42f + 0.04f * kHeaderSpread[rotor % kHeaderSpread.size()];
        p.bank[at] = static_cast<int>(rotor % 2);
    }
    p.twoBanks = rotors >= 2 && e.routing != ExhaustRouting::Single;
    // Un cylindre équivalent, carré : le volume seul compte pour le débit et la ligne.
    const float side = static_cast<float>(cubeRoot(chamberCc * 1000.0 / 0.7854)) / 1000.0f;
    p.boreM = side;
    p.strokeM = side;
    const RotaryGeometry shape = rotaryGeometry(chamberCc);
    const double portCm2 = kExhaustPortShare * shape.radiusMm * shape.widthMm * 0.01;
    const double openCc = chamberCc * (1.0 / (compression - 1.0) + kOpenedShare);
    p.blowdownS = static_cast<float>(kPistonBlowdownS * (openCc / portCm2) / kPistonVolumeOverAreaCm);
    p.starter.compressionRatio = static_cast<float>(compression);
}

}  // namespace

RotaryGeometry rotaryGeometry(double chamberCc) {
    RotaryGeometry g;
    g.radiusMm = cubeRoot(chamberCc * 1000.0 / (3.0 * kSqrtThree * kRotaryEccentricityShare * kRotaryWidthShare));
    g.eccentricityMm = g.radiusMm * kRotaryEccentricityShare;
    g.widthMm = g.radiusMm * kRotaryWidthShare;
    return g;
}

float rotaryCompressionRatio(Aspiration aspiration) {
    return static_cast<float>(compressionOf(aspiration, kRotaryCompressionNatural, kRotaryCompressionBoosted));
}

double rotaryWallRatio(float compressionRatio) {
    // Sans dimension : à proportions égales, le rapport ne dépend pas de la taille. On le calcule à R = 1.
    const double ratio = static_cast<double>(compressionRatio);
    const double volume = 3.0 * kSqrtThree * kRotaryEccentricityShare * kRotaryWidthShare;
    const double clearance = volume / (ratio - 1.0);
    // Au point mort haut, la chambre est une longue lame : la corde entre deux sommets, sur la largeur du rotor.
    const double length = kSqrtThree;
    const double height = clearance / (length * kRotaryWidthShare);
    const double rotarySurface = 2.0 * length * kRotaryWidthShare + 2.0 * length * height;
    // Le cylindre carré de même volume : deux disques et le peu de chemise découverte.
    const double side = cubeRoot(volume * 1e9 / 0.7854) / 1000.0;
    const double disc = 0.7854 * side * side;
    const double pistonSurface = 2.0 * disc + 3.141592653589793 * side * (clearance / disc);
    return rotarySurface / pistonSurface;
}

double rotaryEfficiencyShare(Aspiration aspiration) {
    const double compression = compressionOf(aspiration, kRotaryCompressionNatural, kRotaryCompressionBoosted);
    const double pistonCompression = compressionOf(aspiration, kPistonCompressionNatural, kPistonCompressionBoosted);
    const double wall = rotaryWallRatio(static_cast<float>(compression));
    return ottoEfficiency(compression) / ottoEfficiency(pistonCompression) * (1.0 - kWallLossPerRatio * (wall - 1.0)) * (1.0 - kApexLeakLoss);
}

const ExhaustSpec& exhaustSpec(Exhaust exhaust) {
    static const ExhaustSpec stock{2.6f, 0.45f, 0.45f, 700.0f, 0.05f, 1.0f, 1.0f};
    static const ExhaustSpec sport{2.2f, 0.85f, 1.0f, 1500.0f, 0.04f, 0.35f, 2.0f};
    static const ExhaustSpec straight{1.8f, 1.3f, 1.5f, 2400.0f, 0.045f, 0.15f, 3.0f};
    static const ExhaustSpec noMuffler{2.0f, 1.15f, 1.4f, 2000.0f, 0.045f, 0.25f, 2.5f};
    static const ExhaustSpec noResonator{2.4f, 0.5f, 0.6f, 800.0f, 0.05f, 1.0f, 1.1f};
    static const ExhaustSpec quiet{2.8f, 0.3f, 0.3f, 500.0f, 0.055f, 0.6f, 0.6f};
    switch (exhaust) {
        case Exhaust::Sport: return sport;
        // Socle : la chambre de détente se comporte comme la ligne directe.
        case Exhaust::Straight:
        case Exhaust::Expansion: return straight;
        case Exhaust::MufflerDelete: return noMuffler;
        case Exhaust::ResonatorDelete: return noResonator;
        case Exhaust::Quiet: return quiet;
        case Exhaust::Stock: break;
    }
    return stock;
}

float popsPerSecond(PopLevel level) {
    switch (level) {
        case PopLevel::Light: return 1.5f;
        case PopLevel::Strong: return 7.0f;
        case PopLevel::Off: break;
    }
    return 0.0f;
}

float fullRangeMultiplier(float idle, float redline) {
    return (redline - idle) / (kCarRedlineRpm - kCarIdleRpm);
}

float EngineProfile::fromCarRpm(float carRpm) const {
    if (carRpm < kMinCarRpm) return 0.0f;
    if (carRpm < kCarIdleRpm) return idleRpm * carRpm / kCarIdleRpm;
    const float above = (carRpm - kCarIdleRpm) * revMultiplier;
    const float room = km::max(redlineRpm - idleRpm, 1.0f);
    const float knee = room * kSoftCeilingFrom;
    const float rest = room - knee;
    const float climbed = above <= knee ? above : knee + rest * km::tanh((above - knee) / rest);
    return idleRpm + climbed;
}

float EngineConfig::cylinderCc() const {
    return static_cast<float>(displacementCc) / static_cast<float>(cylinders);
}

double EngineConfig::effectiveBoreMm() const {
    if (layout != Layout::Rotary) return static_cast<double>(boreMm);
    return cubeRoot(static_cast<double>(displacementCc) / rotors() * 1000.0 / 0.7854);
}

float EngineConfig::strokeMm() const {
    // Un rotatif n'a pas de course : le cylindre carré de sa chambre, celui que son profil donne déjà à la ligne.
    if (layout == Layout::Rotary) return static_cast<float>(effectiveBoreMm());
    const float boreCm = static_cast<float>(boreMm) / 10.0f;
    return cylinderCc() / (static_cast<float>(km::kPi) / 4.0f * boreCm * boreCm) * 10.0f;
}

EngineProfile EngineConfig::toProfile() const {
    const bool isDiesel = burnsDiesel();
    const float cc = cylinderCc();
    const float idle = km::coerceAtLeast(900.0f - 0.25f * cc, 600.0f) + (isDiesel ? 80.0f : 0.0f);
    const float redline = static_cast<float>(redlineRpm);
    const ExhaustSpec& spec = exhaustSpec(exhaust);

    EngineProfile p;
    p.cylinders = cylinders;
    p.firingCount = cylinders;
    p.idleRpm = idle;
    p.redlineRpm = redline;
    p.revMultiplier = revMultiplier > 0.0f ? revMultiplier : fullRangeMultiplier(idle, redline);

    for (int i = 0; i < cylinders; ++i) {
        const float even = static_cast<float>(i) / static_cast<float>(cylinders);
        // Temps d'allumage
        if (layout == Layout::Boxer && cylinders % 2 == 0) {
            // Collecteur à longueurs inégales : les impulsions arrivent par paires.
            p.firing[i] = even + (i % 2 == 1 ? 0.14f / static_cast<float>(cylinders) : 0.0f);
        } else {
            p.firing[i] = even;
        }
        // Force de chaque explosion
        const float spread = kCylinderSpread[i % kCylinderSpread.size()];
        switch (layout) {
            case Layout::Inline: p.firingGain[i] = spread; break;
            // Socle : un VR et un W se comportent comme un V.
            case Layout::V:
            case Layout::Vr:
            case Layout::W: p.firingGain[i] = spread * (i % 2 == 0 ? 1.0f : 0.96f); break;
            case Layout::Boxer: p.firingGain[i] = spread * (i % 2 == 0 ? 1.0f : 0.76f); break;
            // Deux explosions de suite sur une même rangée : le « glouglou ».
            case Layout::CrossPlane: p.firingGain[i] = spread * kCrossPlaneCrowded[i % kCrossPlaneCrowded.size()]; break;
            case Layout::Rotary: break;  // voir `applyRotary`
        }
        // Longueurs réalistes : égales en ligne, par rangée en V, très inégales sur un boxer.
        const float headerSpread = kHeaderSpread[i % kHeaderSpread.size()];
        switch (layout) {
            case Layout::Inline: p.headerM[i] = 0.62f + 0.05f * headerSpread; break;
            case Layout::V:
            case Layout::Vr:
            case Layout::W: p.headerM[i] = (i % 2 == 0 ? 0.55f : 0.72f) + 0.04f * headerSpread; break;
            case Layout::CrossPlane: p.headerM[i] = (kCrossPlaneLeft[i % kCrossPlaneLeft.size()] ? 0.5f : 0.86f) + 0.04f * headerSpread; break;
            case Layout::Boxer: p.headerM[i] = i % 2 == 0 ? 0.48f : 1.05f; break;
            case Layout::Rotary: break;
        }
        // La rangée de ce cylindre : en ligne, il n'y en a qu'une. Les petits moteurs rejoignent vite une seule ligne.
        switch (layout) {
            case Layout::Inline: p.bank[i] = 0; break;
            case Layout::CrossPlane: p.bank[i] = kCrossPlaneLeft[i % kCrossPlaneLeft.size()] ? 0 : 1; break;
            default: p.bank[i] = i % 2; break;
        }
    }
    p.routing = routing;
    p.twoBanks = layout != Layout::Inline && cylinders >= 4 && routing != ExhaustRouting::Single;

    p.boreM = static_cast<float>(boreMm) / 1000.0f;
    p.strokeM = strokeMm() / 1000.0f;
    p.exhaust = exhaust;
    p.diesel = isDiesel;
    p.turbo = aspiration == Aspiration::Turbo;
    p.supercharged = aspiration == Aspiration::Supercharger;
    if (p.supercharged) {
        // La respiration n'est connue que du groupe : à 1 ici, ce qui ne sert qu'au son sans modèle de conduite.
        const SuperchargerSpec charger = superchargerSpecFor(*this, 1.0, 1.0);
        p.superchargerKind = superchargerKind;
        p.superchargerRatio = static_cast<float>(charger.driveRatio);
        p.superchargerPocketM3 = static_cast<float>(charger.rotorVolumeM3 / charger.pulsesPerTurn);
        p.superchargerInnerPressure = static_cast<float>(charger.innerPressure);
    }
    p.pops = isDiesel ? PopLevel::Off : pops;
    // Grosse cylindrée : plus de carburant imbrûlé. La turbine absorbe une partie de l'onde.
    p.popForce = km::pow(km::coerceIn(cc / 500.0f, 0.4f, 3.0f), 0.3f) * (aspiration == Aspiration::Turbo ? 0.72f : 1.0f);
    // Le tube résonne selon sa longueur ; un gros cylindre pousse plus d'air, plus grave.
    p.popBodyHz = 520.0f / (2.0f * spec.pipeM) * km::pow(km::coerceIn(500.0f / cc, 0.3f, 2.5f), 0.2f);
    p.turboRelease = turboRelease;
    p.boostBar = boostBar;
    p.airFilter = airFilter;
    if (airFilter == AirFilter::Stacks) {
        p.stackM = static_cast<float>(stackLengthM(*this));
        p.stackRadiusM = static_cast<float>(revforge::stackRadiusM(*this));
    }
    p.camshaft = valvetrain();

    // Le démarreur dépend de ce qu'il doit lancer : la puissance suit la cylindrée et la compression.
    // Un rotatif se lance comme le quatre temps de deux fois sa cylindrée : il comprime autant à chaque tour.
    const float litres = static_cast<float>(sweptCcPerTwoTurns()) / 1000.0f;
    StarterSpec& s = p.starter;
    s.kind = (cylinders >= 8 || litres > 4.5f) ? StarterKind::Direct : StarterKind::Reduction;
    // Un moteur suralimenté comprime moins pour ne pas détoner, quelle que soit la façon dont il l'est.
    s.compressionRatio = isDiesel ? 18.0f : (aspiration != Aspiration::Natural ? 9.5f : 10.5f);
    s.powerKw = (0.75f + 0.38f * litres) * (isDiesel ? 1.15f : 1.0f);
    s.ringTeeth = static_cast<int>(km::coerceIn(105.0f + 9.0f * litres, 110.0f, 170.0f));
    if (s.kind == StarterKind::Direct) {
        s.pinionTeeth = 9;
        s.helixDeg = 0.0f;
        s.reduction = 1.0f;
        s.commutatorBars = 31;
        s.motorFreeRpm = 4000.0f;
        s.motorInertia = 0.0009f + 0.0001f * litres;
    } else {
        s.pinionTeeth = 10;
        s.helixDeg = isDiesel ? 8.0f : 14.0f;
        // Un diesel se lance plus lentement : réduction plus forte, pour vaincre sa compression.
        s.reduction = isDiesel ? 4.4f : 3.6f;
        s.commutatorBars = 23;
        s.motorFreeRpm = 16500.0f;
        s.motorInertia = 0.00008f + 0.00001f * litres;
    }
    if (layout == Layout::Rotary) applyRotary(*this, p);
    // Après le rotatif : les explosions inégales d'une pipe commune se posent sur les siennes.
    if (hasCarburetor(*this)) applyCarburetor(*this, p);
    if (directInjected(*this) || particulateFiltered(*this) || commonRailed(*this) || recirculated(*this)) applyInjection(*this, p);
    return p;
}

EngineProfile electricProfile() {
    EngineProfile p;
    p.cylinders = 2;  // virtuel
    p.firingCount = 0;
    p.idleRpm = 200.0f;
    p.redlineRpm = 12000.0f;
    p.revMultiplier = fullRangeMultiplier(p.idleRpm, p.redlineRpm);
    p.electric = true;
    return p;
}

int displacementForStroke(int boreMm, int cylinders, double strokeMm) {
    const double boreCm = static_cast<double>(boreMm) / 10.0;
    const double cc = km::kPi / 4.0 * boreCm * boreCm * (strokeMm / 10.0) * static_cast<double>(cylinders);
    return static_cast<int>(std::floor(cc + 0.5));
}

}  // namespace revforge
