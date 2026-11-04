#include "revforge/ecu.hpp"

#include <cmath>

#include "portable_math.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/thermal.hpp"
#include "revforge/turbo.hpp"

namespace revforge {

namespace {

// ── L'avance : ce que coûte un écart au meilleur couple. Ayala, Gerty et Heywood (SAE 2006-01-0229), corrélation mesurée sur
// la phase de la combustion, de toutes charges, régimes et richesses : T/T_MBT = 1 − 0,168·(√(1 + 0,004443·Δ²) − 1). Le cycle de
// la carte du cliquetis (combustion.cpp), qui brûle à durée fixe, la retrouve à moins d'un point jusqu'à 25° (testé). ──
constexpr double kSparkLossScale = 0.168;
constexpr double kSparkLossCurvature = 0.004443;
// Le bilan de l'anti-lag (anti_lag.cpp) : la part du travail perdu qui part dans les parois.
constexpr double kLateWallShare = 0.2;

// ── Le meilleur couple : la moitié de la combustion vers 8° après le point mort haut (Heywood, chap. 9) ──
// De l'étincelle à cette moitié : 34° à 3000 tr/min plein gaz, d'où 26° d'avance ; 32° à 6000 ; 31° à mi-charge à 1500.
// Recoupé à des cartes publiées de moteurs atmosphériques (25 à 35° à pleine charge) : un ordre de grandeur, supposé.
constexpr double kHalfBurnDeg = 34.0;
constexpr double kHalfBurnRefRpm = 3000.0;
constexpr double kHalfBurnSpeedExponent = 0.25;
constexpr double kHalfBurnLowLoad = 0.6;
constexpr double kHalfBurnAtdcDeg = 8.0;

// ── La richesse d'origine : celle de `ConsumptionModel` (consumption.cpp) ──
constexpr double kFullLoadLambda = 0.88;
constexpr double kFullLoadLambdaTurbo = 0.82;
constexpr double kEnrichFromShare = 0.85;
// L'enrichissement à froid, et la marge du rupteur sous l'affolement des soupapes : des ordres de grandeur, supposés.
constexpr double kWarmupRichness = 0.3;
constexpr double kWarmupColdC = -20.0;
constexpr double kWarmupEndC = 80.0;
constexpr double kFloatMargin = 0.02;

// ── La température de flamme adiabatique d'un hydrocarbure dans l'air, à l'équilibre, 298 K et 1 atm ──
// Échauffement rapporté à celui de φ = 1. Tables usuelles (méthane : 2226 K à φ = 1, sommet vers φ = 1,05) : un ordre
// de grandeur, que l'essence suit à quelques kelvins près.
constexpr int kFlamePoints = 11;
constexpr double kFlamePhi[kFlamePoints] = {0.6, 0.7, 0.8, 0.9, 1.0, 1.05, 1.1, 1.2, 1.3, 1.4, 1.5};
constexpr double kFlameRise[kFlamePoints] = {0.709, 0.799, 0.881, 0.952, 1.0, 1.005, 0.992, 0.950, 0.909, 0.871, 0.836};

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

bool carbureted(const EngineConfig& e) {
    const FuelSystem system = fuelSystemOf(e);
    return system == FuelSystem::Carburetor || system == FuelSystem::MultiCarb;
}

/** Une part entre 0 et 1, nulle sous 85 % de remplissage : là où la carte enrichit. */
double enrichment(double load) { return clamp((load - kEnrichFromShare) / (1.0 - kEnrichFromShare), 0.0, 1.0); }

}  // namespace

double sparkTorqueShare(double fromMbtDeg) {
    const double share = 1.0 - kSparkLossScale * (std::sqrt(1.0 + kSparkLossCurvature * fromMbtDeg * fromMbtDeg) - 1.0);
    return share > 0.0 ? share : 0.0;
}

double sparkExhaustGain(double fromMbtDeg, double indicatedEfficiency) {
    if (fromMbtDeg <= 0.0) return 0.0;
    return indicatedEfficiency * (1.0 - sparkTorqueShare(fromMbtDeg)) * (1.0 - kLateWallShare);
}

double ecuMbtAdvanceDeg(double rpm, double load) {
    const double speed = portable::pow((rpm > 300.0 ? rpm : 300.0) / kHalfBurnRefRpm, kHalfBurnSpeedExponent);
    const double halfBurn = kHalfBurnDeg * speed * (1.0 + kHalfBurnLowLoad * (1.0 - clamp(load, 0.0, 1.0)));
    return halfBurn - kHalfBurnAtdcDeg;
}

double lambdaTorqueShare(double lambda) {
    // La courbe que lit déjà le carburateur, d'après Heywood (chap. 5) : la même cause, le même chiffre.
    return torqueOfRichness(1.0 / (lambda > 0.3 ? lambda : 0.3)) / torqueOfRichness(1.0);
}

double lambdaHeatShare(double lambda) {
    const double phi = 1.0 / (lambda > 0.3 ? lambda : 0.3);
    if (phi <= kFlamePhi[0]) return kFlameRise[0];
    for (int i = 1; i < kFlamePoints; ++i) {
        if (phi <= kFlamePhi[i]) {
            const double t = (phi - kFlamePhi[i - 1]) / (kFlamePhi[i] - kFlamePhi[i - 1]);
            return kFlameRise[i - 1] + (kFlameRise[i] - kFlameRise[i - 1]) * t;
        }
    }
    return kFlameRise[kFlamePoints - 1];
}

double stockFullLoadLambda(const EngineConfig& e) {
    if (e.burnsDiesel()) return 1.0;
    return e.aspiration != Aspiration::Natural ? kFullLoadLambdaTurbo : kFullLoadLambda;
}

double fullLoadLambdaOf(const EngineConfig& e) {
    // Un carburateur dose par ses gicleurs, pas par le calculateur ; un diesel brûle toujours en excès d'air.
    const double stock = stockFullLoadLambda(e);
    // La richesse d'origine écrite telle quelle (en float, comme le réglage) reste la carte d'origine.
    if (e.burnsDiesel() || carbureted(e) || e.fullLoadLambda <= 0.0f || e.fullLoadLambda == static_cast<float>(stock)) return stock;
    return static_cast<double>(e.fullLoadLambda);
}

double warmupRichness(double coolantC) {
    return 1.0 + kWarmupRichness * clamp((kWarmupEndC - coolantC) / (kWarmupEndC - kWarmupColdC), 0.0, 1.0);
}

double demandedLambda(const EngineConfig& e, double load, double coolantC) {
    if (e.burnsDiesel()) return 1.0;
    // Intégration : l'enrichissement à froid est celui de la thermique (`coldEnrichment`, 0 au-delà de 70 °C), le même que paie la
    // consommation : une seule loi, pas deux. `warmupRichness` reste pour comparer.
    const double cold = coldEnrichment(e, coolantC);
    const double richness = (1.0 + (1.0 / fullLoadLambdaOf(e) - 1.0) * enrichment(load)) * (cold > 0.0 ? 1.0 + cold : 1.0);
    return 1.0 / richness;
}

double limiterRpmFor(double redlineRpm, double valveFloatRpm) {
    if (valveFloatRpm <= 0.0) return redlineRpm;
    const double safe = valveFloatRpm * (1.0 - kFloatMargin);
    return safe < redlineRpm ? safe : redlineRpm;
}

double demandedAdvanceDeg(const EngineConfig& e, double rpm, double load, double knockRetardDeg) {
    const double offset = e.burnsDiesel() ? 0.0 : static_cast<double>(e.ignitionOffsetDeg);
    return ecuMbtAdvanceDeg(rpm, load) + offset - knockRetardDeg;
}

bool ecuMapTuned(const EngineConfig& e) {
    if (e.burnsDiesel()) return false;
    return e.ignitionOffsetDeg != 0 || fullLoadLambdaOf(e) != stockFullLoadLambda(e);
}

double mapRetardDeg(const EngineConfig& e, double knockRetardDeg) {
    if (e.burnsDiesel()) return 0.0;
    return knockRetardDeg - static_cast<double>(e.ignitionOffsetDeg);
}

double lambdaFullLoadRatio(const EngineConfig& e) {
    const double tuned = fullLoadLambdaOf(e);
    const double stock = stockFullLoadLambda(e);
    if (tuned == stock) return 1.0;
    return lambdaTorqueShare(tuned) / lambdaTorqueShare(stock);
}

double lambdaLoadRatio(const EngineConfig& e, double load) {
    const double full = lambdaFullLoadRatio(e);
    return full == 1.0 ? 1.0 : 1.0 + (full - 1.0) * enrichment(load);
}

double ecuFullLoadShare(const EngineConfig& e, double knockRetardDeg) {
    if (!ecuMapTuned(e) && knockRetardDeg == 0.0) return 1.0;
    // Intégration : sur un quatre-temps à étincelle, le travail que change le décalage d'avance vient de la carte du cliquetis
    // (combustion), qui part de la carte d'origine réelle, souvent en retard sur le meilleur couple. Le compter ici aussi le
    // compterait deux fois. La consommation et la chaleur du gaz gardent, elles, le retard du calculateur (`mapRetardDeg`).
    const double retard = ecuAdvanceInKnockMap(e) ? knockRetardDeg : mapRetardDeg(e, knockRetardDeg);
    return sparkTorqueShare(retard) * lambdaFullLoadRatio(e);
}

double ecuActionShare(const EcuAction& action, double mapRetard) {
    const double spark = action.retardDeg == 0.0 ? 1.0 : sparkTorqueShare(mapRetard + action.retardDeg) / sparkTorqueShare(mapRetard);
    return spark * action.firedShare;
}

EcuExhaust ecuMapExhaust(const EngineConfig& e) {
    EcuExhaust out;
    // Le rendement du cylindre, celui de `ConsumptionModel` : il compte aussi pour un retard du pas, carte d'origine ou non.
    out.efficiency = e.aspiration != Aspiration::Natural ? 0.375 : 0.405;
    if (!ecuMapTuned(e)) return out;
    out.mapRetardDeg = mapRetardDeg(e);
    const double tuned = fullLoadLambdaOf(e);
    const double stock = stockFullLoadLambda(e);
    out.fullLoadHeatRatio = tuned == stock ? 1.0 : lambdaHeatShare(tuned) / lambdaHeatShare(stock);
    return out;
}

double fullLoadExhaustK(const EngineConfig& e, double rpm, bool stockMap) {
    constexpr double kReferenceK = 293.0;
    const bool diesel = e.burnsDiesel();
    const double rise = exhaustHeatRiseK(diesel, 1.0);
    const double normal = kReferenceK + exhaustHeatShare(diesel, static_cast<double>(e.redlineRpm), rpm, 1.0) * rise;
    const EcuExhaust map = stockMap ? EcuExhaust{} : ecuMapExhaust(e);
    return map.active() ? ecuExhaustK(map, kReferenceK, normal, rise, 1.0, 1.0) : normal;
}

double ecuExhaustK(const EcuExhaust& ecu, double intakeK, double normalK, double heatRiseK, double fill, double ignition) {
    const double share = (normalK > intakeK ? normalK - intakeK : 0.0) / heatRiseK;
    const double heat = 1.0 + (ecu.fullLoadHeatRatio - 1.0) * enrichment(fill);
    const double fired = clamp(ecu.firedShare, 0.0, 1.0);
    const double firedHeat = (share + sparkExhaustGain(ecu.mapRetardDeg + ecu.retardDeg, ecu.efficiency)) * heat;
    // Un cylindre dont seule l'étincelle est coupée envoie son mélange : une part s'enflamme au contact du gaz chaud.
    const double cutHeat = ecu.fuelOnCut ? kUnburntCollectorShare * clamp(ignition, 0.0, 1.0) : 0.0;
    return intakeK + heatRiseK * (fired * firedHeat + (1.0 - fired) * cutHeat);
}

double ecuEvoRatio(const EcuExhaust& ecu, double exhaustShare, double fill) {
    if (exhaustShare <= 0.0) return 1.0;
    const double heat = 1.0 + (ecu.fullLoadHeatRatio - 1.0) * enrichment(fill);
    return (exhaustShare + sparkExhaustGain(ecu.mapRetardDeg + ecu.retardDeg, ecu.efficiency)) * heat / exhaustShare;
}

double tractionSlipTarget(TractionControl mode) {
    double grip = 0.0;
    switch (mode) {
        case TractionControl::Sport: grip = kTractionSportGrip; break;
        case TractionControl::Full: grip = kTractionFullGrip; break;
        case TractionControl::Off: return 0.0;
    }
    // L'inverse de s/√(1+s²) : le glissement où le pneu donne cette part de son adhérence (m/s, pour 1 m/s de référence).
    return grip / std::sqrt(1.0 - grip * grip);
}

void tractionActuation(double torqueShare, double maxRetardDeg, double& retardDeg, double& firedShare) {
    const double wanted = clamp(torqueShare, 0.0, 1.0);
    const double floor = sparkTorqueShare(maxRetardDeg);
    if (wanted >= floor) {
        // L'inverse de `sparkTorqueShare` : le retard qui laisse juste cette part.
        const double root = 1.0 + (1.0 - wanted) / kSparkLossScale;
        retardDeg = std::sqrt((root * root - 1.0) / kSparkLossCurvature);
        firedShare = 1.0;
    } else {
        retardDeg = maxRetardDeg;
        firedShare = floor > 0.0 ? wanted / floor : 0.0;
    }
}

}  // namespace revforge
