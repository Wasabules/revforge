#include "revforge/engine_config.hpp"

#include <cmath>

#include "kotlin_math.hpp"

namespace revforge {

namespace {

const std::array<float, 8> kCrossPlaneV8{0.0f, 0.129f, 0.25f, 0.375f, 0.504f, 0.625f, 0.75f, 0.879f};
const std::array<float, 8> kCrossPlaneBanks{1.0f, 0.74f, 1.0f, 1.0f, 0.74f, 1.0f, 0.74f, 0.74f};
const std::array<float, 6> kHeaderSpread{0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.8f};
const std::array<float, 6> kCylinderSpread{1.0f, 0.94f, 1.05f, 0.97f, 1.02f, 0.95f};

constexpr float kMinCarRpm = 100.0f;
constexpr float kSoftCeilingFrom = 0.8f;

}  // namespace

const ExhaustSpec& exhaustSpec(Exhaust exhaust) {
    static const ExhaustSpec stock{3, 2200.0f, 0.22f, 2.6f, 0.8f, 0.45f, 0.45f, 700.0f, 0.05f};
    static const ExhaustSpec sport{2, 4800.0f, 0.34f, 2.2f, 1.0f, 0.85f, 1.0f, 1500.0f, 0.04f};
    static const ExhaustSpec straight{0, 11000.0f, 0.48f, 1.8f, 1.25f, 1.3f, 1.5f, 2400.0f, 0.045f};
    switch (exhaust) {
        case Exhaust::Sport: return sport;
        case Exhaust::Straight: return straight;
        case Exhaust::Stock: break;
    }
    return stock;
}

float popsPerSecond(PopLevel level) {
    switch (level) {
        case PopLevel::Light: return 1.5f;
        case PopLevel::Strong: return 4.0f;
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
        if (layout == Layout::CrossPlane && cylinders == 8) {
            p.firing[i] = kCrossPlaneV8[i];
        } else if (layout == Layout::Boxer && cylinders % 2 == 0) {
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
            case Layout::CrossPlane: p.firingGain[i] = spread * kCrossPlaneBanks[i % kCrossPlaneBanks.size()]; break;
        }
        // Longueurs réalistes : égales en ligne, par rangée en V, très inégales sur un boxer.
        const float headerSpread = kHeaderSpread[i % kHeaderSpread.size()];
        switch (layout) {
            case Layout::Inline: p.headerM[i] = 0.62f + 0.05f * headerSpread; break;
            case Layout::V:
            case Layout::CrossPlane: p.headerM[i] = (i % 2 == 0 ? 0.55f : 0.72f) + 0.04f * headerSpread; break;
            case Layout::Boxer: p.headerM[i] = i % 2 == 0 ? 0.48f : 1.05f; break;
        }
    }

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

}  // namespace revforge
