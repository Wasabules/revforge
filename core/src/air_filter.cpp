#include "revforge/air_filter.hpp"

#include <cmath>

#include "portable_math.hpp"

namespace revforge {

namespace {

constexpr double kAtmosphere = 101325.0;
constexpr double kAmbientK = 293.0;
constexpr double kRatedDropPa = 3000.0;

// Le papier et sa boîte : environ 3 kPa au nominal. Sur un banc, le meilleur filtre dans une boîte d'origine perd 2,56 kPa
// à 6 m³/min (0,12 kg/s) et le cône le plus fermé de l'essai 1,21 kPa (fastcar.co.uk). Boîte sans élément : 1,25 kPa
// (5 po d'eau, Bob Is The Oil Guy) : la boîte et sa prise d'air pèsent autant que l'élément, d'où le faible gain d'un panneau.
// Les cornets : une bouche arrondie et le moins de grille possible ; supposé.
constexpr AirFilterSpec kStock{kRatedDropPa, 1200.0, 0.0, false};
constexpr AirFilterSpec kPanel{2550.0, 750.0, 0.0, false};
constexpr AirFilterSpec kCone{1210.0, 1210.0, 1.0, true};
constexpr AirFilterSpec kStacks{200.0, 0.0, 1.0, true};

// Le compartiment moteur au point nominal, en roulant, au-dessus de l'air extérieur. Ordres de grandeur supposés : voir
// docs/modele.md. Au centre, l'échappement est enfermé avec le moteur et peu d'air frais le traverse.
constexpr double kBayRiseFrontK = 12.0;
constexpr double kBayRiseMidK = 20.0;
constexpr double kBayRiseRearK = 16.0;
// Part de la ventilation qui ne dépend pas de la vitesse de la voiture : le ventilateur.
constexpr double kFanShare = 0.35;

// Une boîte grandit comme une boîte : sa surface comme V^(2/3). Un gros moteur y perd donc plus, jusqu'à ce que tolère un
// constructeur : 20 à 25 po d'eau (5 à 6 kPa) sur un filtre chargé, donc au plus 5 kPa sur un filtre neuf.
constexpr double kDesignFlowKgS = 0.1104;  // 2 L à 6000 tr/min, remplissage de 0,92
constexpr double kDesignDisplacementM3 = 0.002;
constexpr double kDropCeilingPa = 5000.0;

constexpr double kSoundSpeed = 343.0;
constexpr double kTunedShare = 0.72;
// De la mi-course à la fermeture de la soupape : 150° de vilebrequin. En secondes : 150/360 de 60/N, soit 25/N.
constexpr double kTravelSecondsRpm = 25.0;
// La dépression lancée à mi-course revient en surpression à la moitié d'une période, puis à chaque période de plus.
// Le premier maximum à une soupape de ces dimensions est le second : trois demi-périodes.
constexpr double kFirstPeak = 1.5;
constexpr double kRamAmplitude = 0.03;  // Heywood : quelques % de remplissage. Bas : la boîte d'origine est déjà accordée.
constexpr double kRamQ = 6.0;
constexpr double kRunnerToBore = 0.5;

double clampTo(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

}  // namespace

const AirFilterSpec& airFilterSpec(AirFilter filter) {
    switch (filter) {
        case AirFilter::Panel: return kPanel;
        case AirFilter::Cone: return kCone;
        case AirFilter::Stacks: return kStacks;
        case AirFilter::Stock: break;
    }
    return kStock;
}

double airFilterDropShare(AirFilter filter) { return airFilterSpec(filter).designDropPa / kRatedDropPa; }

double airFilterRiseK(AirFilter filter, EnginePlacement placement, double regime) {
    const double share = airFilterSpec(filter).bayAirShare;
    if (share <= 0.0) return 0.0;
    const double bay = placement == EnginePlacement::Mid ? kBayRiseMidK : (placement == EnginePlacement::Rear ? kBayRiseRearK : kBayRiseFrontK);
    const double r = clampTo(regime, 0.1, 1.15);
    return share * bay * r / (kFanShare + (1.0 - kFanShare) * r);
}

AirInlet airInletBehind(AirFilter filter, EnginePlacement placement, double flowKgS, double ratedFlowKgS, double regime) {
    AirInlet in;
    const double share = flowKgS / ratedFlowKgS;
    in.pressurePa = kAtmosphere - kRatedDropPa * airFilterDropShare(filter) * share * share;
    in.temperatureK = kAmbientK + airFilterRiseK(filter, placement, regime);
    return in;
}

double naturalFilterCoefficient(double displacementM3, double ratedFlowKgS) {
    const double design = kDesignFlowKgS * portable::pow(displacementM3 / kDesignDisplacementM3, 2.0 / 3.0);
    const double sized = kRatedDropPa / (design * design);
    const double ceiling = kDropCeilingPa / (ratedFlowKgS * ratedFlowKgS);
    return sized < ceiling ? sized : ceiling;
}

double stackRadiusM(const EngineConfig& engine) { return 0.5 * kRunnerToBore * static_cast<double>(engine.boreMm) / 1000.0; }

double stackLengthM(const EngineConfig& engine) {
    // Le maximum d'accord tombe là où l'aller-retour a duré ce qu'il faut : f·25/N = 1,5.
    const double tuned = kFirstPeak * kTunedShare * static_cast<double>(engine.redlineRpm) / kTravelSecondsRpm;
    return clampTo(kSoundSpeed / (4.0 * tuned) - 0.61 * stackRadiusM(engine), 0.12, 0.5);
}

double stackResonanceHz(const EngineConfig& engine) { return kSoundSpeed / (4.0 * (stackLengthM(engine) + 0.61 * stackRadiusM(engine))); }

double stackRamGain(const EngineConfig& engine, double rpm) {
    if (rpm < 200.0) return 0.0;
    const double trips = stackResonanceHz(engine) * kTravelSecondsRpm / rpm;
    const double late = trips > kFirstPeak ? trips - kFirstPeak : 0.0;
    return -kRamAmplitude * portable::cos(2.0 * portable::kPi * trips) * portable::exp(-portable::kPi * late / kRamQ);
}

}  // namespace revforge
