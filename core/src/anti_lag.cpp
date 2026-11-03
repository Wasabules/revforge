#include "revforge/anti_lag.hpp"

#include <cmath>

#include "revforge/turbo.hpp"

namespace revforge {

namespace {

// ── Ce que fait le calculateur : des ordres de grandeur de préparations de rallye, voir docs/modele.md (supposés) ──
// Étincelle à 35° après le point mort haut, soit 55° de retard sur le meilleur couple.
constexpr double kRetardDeg = 55.0;
// L'injection d'air demande moins au cylindre : le moteur doit surtout laisser passer l'air qui porte le surplus d'essence.
constexpr double kAirRetardDeg = 40.0;
// Le papillon : assez d'air pour la chaleur voulue. L'injection d'air a sa propre arrivée, et n'a pas à l'ouvrir autant.
// Moins ouvert, elle reste à moitié maigre : le surplus manque, faute d'air dans les cylindres pour le porter.
constexpr double kRetardOpening = 0.30;
constexpr double kAirOpening = 0.25;
constexpr double kCutOpening = 0.35;
// Le retard noie un peu le cylindre, surtout pour le refroidir : ce surplus sort imbrûlé, faute d'oxygène.
constexpr double kRetardLambda = 0.9;
// Au-delà, le cylindre ne s'allume plus : c'est le carburant que l'air injecté peut trouver.
constexpr double kRichestLambda = 0.65;

// ── La physique ──
// Le couple selon le retard, une parabole : la forme des tables « torque ratio » des calculateurs (ordre de grandeur).
constexpr double kNoTorqueDeg = 68.0;
// Le rendement du cylindre, celui de `ConsumptionModel` : le travail perdu est de la chaleur en plus dans le gaz.
constexpr double kIndicatedEfficiency = 0.375;
// Une part de cette chaleur part quand même dans les parois (supposé).
constexpr double kLateWallShare = 0.2;
// La part de ce qui arrive imbrûlé qui brûle vraiment avant la turbine (supposé) : une poche froide s'allume mal,
// un mélange brassé avec de l'air frais brûle mieux.
constexpr double kCutBurnShare = 0.45;
constexpr double kAirBurnShare = 0.7;
constexpr double kIgnitionFromK = 800.0;
constexpr double kIgnitionFullK = 1100.0;

// ── La vanne d'air ──
// Une vanne de 17 mm pour 2 L, du même ordre que la soupape de décharge (supposé). Un clapet empêche le gaz de remonter.
constexpr double kAirAreaAtRated = 1.1e-3;  // m² par kg/s de débit nominal
constexpr double kAirDischarge = 0.65;
constexpr double kAirValveS = 0.03;
constexpr double kAirR = 287.0;
constexpr double kAtmosphere = 101325.0;

double squared(double v) { return v * v; }

double retardGain(AntiLag kind) {
    const double share = 1.0 - squared((kind == AntiLag::AirInjection ? kAirRetardDeg : kRetardDeg) / kNoTorqueDeg);
    return kIndicatedEfficiency * (1.0 - share) * (1.0 - kLateWallShare);
}

}  // namespace

double antiLagOpening(AntiLag kind) {
    switch (kind) {
        case AntiLag::Retard: return kRetardOpening;
        case AntiLag::AirInjection: return kAirOpening;
        case AntiLag::CylinderCut: return kCutOpening;
        case AntiLag::Off: break;
    }
    return 0.0;
}

double antiLagTorqueShare(AntiLag kind) {
    switch (kind) {
        case AntiLag::Retard: return 1.0 - squared(kRetardDeg / kNoTorqueDeg);
        case AntiLag::AirInjection: return 1.0 - squared(kAirRetardDeg / kNoTorqueDeg);
        case AntiLag::CylinderCut: return kAntiLagFiredShare;
        case AntiLag::Off: break;
    }
    return 1.0;
}

double antiLagIgnition(double collectorK) {
    const double share = (collectorK - kIgnitionFromK) / (kIgnitionFullK - kIgnitionFromK);
    return share < 0.0 ? 0.0 : (share > 1.0 ? 1.0 : share);
}

void TurboCharger::antiLagExhaust(const AntiLagExhaust& in, double& gas, double& hotTarget) {
    AntiLagState& a = antiLag_;
    const TurboState& s = state_;
    const AntiLag kind = in.command.kind;
    const bool on = in.command.active;

    a.airValve += ((on && kind == AntiLag::AirInjection ? 1.0 : 0.0) - a.airValve) * in.dt / kAirValveS;
    a.surplusFuel = 0.0;
    a.collectorHeatW = 0.0;
    a.evoRatio = 1.0;

    // La vanne d'air : le compresseur pousse, le collecteur résiste, et ce qu'elle laisse passer fait monter sa pression.
    // Résolu ici, par dichotomie : à un pas de 1 ms, un calcul explicite se mettrait à battre.
    double air = 0.0;
    if (a.airValve > 1e-4) {
        const double passage = spec_.turbineK + spec_.wastegateK * s.wastegate;
        const double rho = s.chargePa / (kAirR * s.coolerOutK);
        const double unit = kAirDischarge * kAirAreaAtRated * spec_.ratedFlow * a.airValve * std::sqrt(2.0 * rho);
        const double before = in.engineAir * (1.0 + in.fuelPerAir);
        const auto wanted = [&](double flow) {
            const double through = before + flow;
            const double outPa = kAtmosphere + spec_.lineDrop * through * through;
            const double inPa = std::sqrt(outPa * outPa + through * through * s.turbineInK / (passage * passage));
            const double across = s.chargePa - inPa;
            return across > 0.0 ? unit * across / std::sqrt(across + 500.0) : 0.0;
        };
        double low = 0.0;
        double high = wanted(0.0);
        for (int i = 0; i < 10 && high > 0.0; ++i) {
            const double middle = 0.5 * (low + high);
            (wanted(middle) > middle ? low : high) = middle;
        }
        air = 0.5 * (low + high);
    }
    a.airFlow = air;

    if (!on) {
        // La vanne se referme : de l'air frais, qui ne fait que refroidir.
        if (air > 0.0 && gas + air > 0.0) {
            hotTarget = (gas * hotTarget + air * s.coolerOutK) / (gas + air);
            gas += air;
        }
        return;
    }

    const double m = in.engineAir;
    const double burntFuel = m * in.fuelPerAir;
    const double intake = s.coolerOutK;
    const double share = (hotTarget - intake) / in.heatRiseK;
    const double ignition = antiLagIgnition(collectorK());
    // Un carburateur ne dose pas sur commande : le seul surplus est celui du mélange qu'il débite déjà. -1 : un calculateur qui dose.
    const double carbSurplus = in.carbRichness > 0.0 ? burntFuel * (in.carbRichness > 1.0 ? in.carbRichness - 1.0 : 0.0) : -1.0;

    switch (kind) {
        case AntiLag::Retard: {
            // Le gaz sort avant d'avoir fini de brûler : le travail que le piston n'a pas reçu part vers la turbine.
            const double gain = retardGain(kind);
            a.evoRatio = (share + gain) / share;
            a.surplusFuel = carbSurplus >= 0.0 ? carbSurplus : burntFuel * (1.0 / kRetardLambda - 1.0);
            a.collectorHeatW = burntFuel * in.heatValue * gain;
            hotTarget = intake + (share + gain) * in.heatRiseK;
            gas = m + burntFuel + a.surplusFuel;
            break;
        }
        case AntiLag::CylinderCut: {
            // Les cylindres allumés donnent un gaz normal ; les autres, un mélange qui s'enflamme à leur contact, s'il fait assez chaud.
            const double burn = (1.0 - kAntiLagFiredShare) * kCutBurnShare * ignition;
            a.collectorHeatW = burntFuel * in.heatValue * burn;
            hotTarget = intake + (kAntiLagFiredShare * share + burn) * in.heatRiseK;
            break;
        }
        case AntiLag::AirInjection: {
            // Le cylindre brûle riche, et ce surplus trouve l'air frais dans le collecteur : une flamme continue, hors du cycle.
            const double gain = retardGain(kind);
            a.evoRatio = (share + gain) / share;
            const double most = carbSurplus >= 0.0 ? carbSurplus : burntFuel * (1.0 / kRichestLambda - 1.0);
            const double matched = air * in.fuelPerAir;
            a.surplusFuel = matched < most ? matched : most;
            const double heat = a.surplusFuel * in.heatValue * kAirBurnShare * ignition;
            a.collectorHeatW = heat;
            const double fromCylinders = m + burntFuel;
            const double total = fromCylinders + air + a.surplusFuel;
            const double cylinderK = intake + (share + gain) * in.heatRiseK;
            hotTarget = (fromCylinders * cylinderK + air * s.coolerOutK + heat / in.gasCp) / total;
            gas = total;
            break;
        }
        case AntiLag::Off: break;
    }
}

}  // namespace revforge
