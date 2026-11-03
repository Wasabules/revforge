#include "revforge/engine_config.hpp"

#include <cmath>

#include "kotlin_math.hpp"
#include "revforge/air_filter.hpp"

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

}  // namespace

const ExhaustSpec& exhaustSpec(Exhaust exhaust) {
    static const ExhaustSpec stock{2.6f, 0.45f, 0.45f, 700.0f, 0.05f, 1.0f, 1.0f};
    static const ExhaustSpec sport{2.2f, 0.85f, 1.0f, 1500.0f, 0.04f, 0.35f, 2.0f};
    static const ExhaustSpec straight{1.8f, 1.3f, 1.5f, 2400.0f, 0.045f, 0.15f, 3.0f};
    static const ExhaustSpec noMuffler{2.0f, 1.15f, 1.4f, 2000.0f, 0.045f, 0.25f, 2.5f};
    static const ExhaustSpec noResonator{2.4f, 0.5f, 0.6f, 800.0f, 0.05f, 1.0f, 1.1f};
    static const ExhaustSpec quiet{2.8f, 0.3f, 0.3f, 500.0f, 0.055f, 0.6f, 0.6f};
    switch (exhaust) {
        case Exhaust::Sport: return sport;
        case Exhaust::Straight: return straight;
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

float EngineConfig::strokeMm() const {
    const float boreCm = static_cast<float>(boreMm) / 10.0f;
    return cylinderCc() / (static_cast<float>(km::kPi) / 4.0f * boreCm * boreCm) * 10.0f;
}

EngineProfile EngineConfig::toProfile() const {
    const bool isDiesel = fuel == Fuel::Diesel;
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
            case Layout::V: p.firingGain[i] = spread * (i % 2 == 0 ? 1.0f : 0.96f); break;
            case Layout::Boxer: p.firingGain[i] = spread * (i % 2 == 0 ? 1.0f : 0.76f); break;
            // Deux explosions de suite sur une même rangée : le « glouglou ».
            case Layout::CrossPlane: p.firingGain[i] = spread * kCrossPlaneCrowded[i % kCrossPlaneCrowded.size()]; break;
        }
        // Longueurs réalistes : égales en ligne, par rangée en V, très inégales sur un boxer.
        const float headerSpread = kHeaderSpread[i % kHeaderSpread.size()];
        switch (layout) {
            case Layout::Inline: p.headerM[i] = 0.62f + 0.05f * headerSpread; break;
            case Layout::V: p.headerM[i] = (i % 2 == 0 ? 0.55f : 0.72f) + 0.04f * headerSpread; break;
            case Layout::CrossPlane: p.headerM[i] = (kCrossPlaneLeft[i % kCrossPlaneLeft.size()] ? 0.5f : 0.86f) + 0.04f * headerSpread; break;
            case Layout::Boxer: p.headerM[i] = i % 2 == 0 ? 0.48f : 1.05f; break;
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

    // Le démarreur dépend de ce qu'il doit lancer : la puissance suit la cylindrée et la compression.
    const float litres = static_cast<float>(displacementCc) / 1000.0f;
    StarterSpec& s = p.starter;
    s.kind = (cylinders >= 8 || litres > 4.5f) ? StarterKind::Direct : StarterKind::Reduction;
    s.compressionRatio = isDiesel ? 18.0f : (aspiration == Aspiration::Turbo ? 9.5f : 10.5f);
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
