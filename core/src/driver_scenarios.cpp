#include "revforge/driver_scenarios.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

constexpr double kGravity = 9.81;
// Le temps de passer d'une pédale à l'autre, et que le frein monte.
constexpr double kReactionS = 0.4;

constexpr double clampTo(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

template <int N>
constexpr int count(const ScenarioLeg (&)[N]) {
    return N;
}

using K = LegKind;

// Ville, feu, voie rapide, dépassement, arrêt : la boucle d'origine, valeurs inchangées.
constexpr ScenarioLeg kCityLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {5.0, 50.0, 0.55, 0.0, 0.0, K::PullAway},
    {20.0, 50.0, 0.4, 0.0, 0.0, K::CityPace},
    {26.0, 0.0, 0.0, 0.0, 0.0, K::TrafficLight},
    {36.0, 112.0, 1.0, 0.0, 0.0, K::Expressway},
    {62.0, 90.0, 0.35, 0.0, 0.0, K::Cruise},
    {76.0, 128.0, 1.0, 0.0, 0.0, K::Overtake},
    {92.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Bouchon : on repart, on s'arrête, jamais vite ni fort. Beaucoup de premières et de secondes, très peu de gaz.
constexpr ScenarioLeg kStopAndGoLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, 22.0, 0.3, 0.0, 0.0, K::Queue},
    {10.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
    {16.0, 30.0, 0.35, 0.0, 0.0, K::Queue},
    {25.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
    {31.0, 18.0, 0.3, 0.0, 0.0, K::Queue},
    {36.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
    {41.0, 40.0, 0.4, 0.0, 0.0, K::Queue},
    {52.0, 25.0, 0.3, 0.0, 0.0, K::Queue},
    {58.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Route de campagne : lignes droites à 90, virages lents, petites côtes. Les virages sont réels : ils limitent la vitesse.
constexpr ScenarioLeg kCountryLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, 80.0, 0.6, 0.0, 0.0, K::PullAway},
    {15.0, 90.0, 0.5, 2.0, 0.0, K::Straight},
    {30.0, 90.0, 0.5, 2.0, 120.0, K::Bend},
    {38.0, 90.0, 0.6, -2.0, 0.0, K::Straight},
    {48.0, 90.0, 0.5, 0.0, 60.0, K::Bend},
    {55.0, 85.0, 0.7, 0.0, 0.0, K::Straight},
    {66.0, 85.0, 0.5, 0.0, 45.0, K::Bend},
    {72.0, 90.0, 0.8, 3.0, 0.0, K::Climb},
    {88.0, 90.0, 0.5, 1.0, 150.0, K::Bend},
    {96.0, 80.0, 0.5, -3.0, 0.0, K::Descent},
};

// Autoroute : bretelle à fond, longue croisière pied léger, dépassement en rétrogradant, faux plat, sortie.
constexpr ScenarioLeg kMotorwayLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, 125.0, 1.0, 0.0, 0.0, K::SlipRoad},
    {24.0, 130.0, 0.4, 0.0, 0.0, K::Cruise},
    {50.0, 150.0, 1.0, 0.0, 0.0, K::Overtake},
    {60.0, 130.0, 0.4, 0.0, 0.0, K::Cruise},
    {76.0, 130.0, 0.75, 3.0, 0.0, K::Climb},
    {92.0, 120.0, 0.3, -2.0, 0.0, K::Descent},
    {104.0, 90.0, 0.3, 0.0, 0.0, K::Exit},
    {112.0, 50.0, 0.3, 0.0, 0.0, K::Exit},
    {118.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Col : 8 à 10 % à la montée, épingles à vitesse de piéton pressé, puis tout redescendre au frein moteur.
constexpr ScenarioLeg kMountainLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, 50.0, 0.7, 4.0, 0.0, K::PullAway},
    {12.0, 70.0, 0.85, 8.0, 0.0, K::Climb},
    {30.0, 70.0, 0.6, 8.0, 18.0, K::Hairpin},
    {38.0, 70.0, 0.9, 8.0, 0.0, K::Climb},
    {56.0, 70.0, 0.6, 9.0, 18.0, K::Hairpin},
    {64.0, 60.0, 1.0, 10.0, 0.0, K::Climb},
    {82.0, 50.0, 0.4, 1.0, 0.0, K::Summit},
    {88.0, 70.0, 0.2, -8.0, 0.0, K::Descent},
    {104.0, 70.0, 0.2, -8.0, 18.0, K::Hairpin},
    {111.0, 75.0, 0.2, -9.0, 0.0, K::Descent},
    {127.0, 75.0, 0.2, -8.0, 20.0, K::Hairpin},
    {134.0, 65.0, 0.2, -7.0, 0.0, K::Descent},
    {148.0, 50.0, 0.3, -2.0, 0.0, K::Descent},
    {156.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Le quart de mille : tout dans la pédale, freinage franc, retour au pas.
constexpr ScenarioLeg kDragLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {4.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::Launch},
    {20.0, 0.0, 0.0, 0.0, 0.0, K::HeavyBraking},
    {30.0, 40.0, 0.3, 0.0, 0.0, K::CityPace},
    {41.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Un tour de piste : pleine charge sur les lignes droites, gros freinages, virages à la limite des pneus.
constexpr ScenarioLeg kTrackLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::Launch},
    {15.0, kFlatOutKmh, 0.8, 0.0, 40.0, K::Corner},
    {21.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::FlatOut},
    {30.0, kFlatOutKmh, 0.8, 0.0, 90.0, K::Corner},
    {35.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::FlatOut},
    {41.0, kFlatOutKmh, 0.8, 0.0, 25.0, K::Corner},
    {46.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::FlatOut},
    {60.0, kFlatOutKmh, 0.8, 0.0, 120.0, K::Corner},
    {66.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::FlatOut},
    {74.0, kFlatOutKmh, 0.8, 0.0, 35.0, K::Corner},
    {79.0, kFlatOutKmh, 1.0, 0.0, 0.0, K::FlatOut},
};

// Démarrer en côte : à l'arrêt sur la pente, repartir, recommencer plus raide, redescendre.
constexpr ScenarioLeg kHillStartLegs[] = {
    {0.0, 0.0, 0.0, 12.0, 0.0, K::Stopped},
    {6.0, 25.0, 0.55, 12.0, 0.0, K::HillStart},
    {14.0, 35.0, 0.6, 12.0, 0.0, K::Climb},
    {23.0, 0.0, 0.0, 12.0, 0.0, K::Stopping},
    {29.0, 20.0, 0.8, 16.0, 0.0, K::HillStart},
    {38.0, 0.0, 0.0, 16.0, 0.0, K::Stopping},
    {44.0, 40.0, 0.1, -10.0, 0.0, K::Descent},
    {56.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

// Avec une remorque : la voiture pèse le double, et la côte se tient en rétrogradant.
constexpr ScenarioLeg kTowingLegs[] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, K::Stopped},
    {3.0, 80.0, 1.0, 0.0, 0.0, K::PullAway},
    {30.0, 90.0, 0.7, 0.0, 0.0, K::Cruise},
    {50.0, 90.0, 1.0, 5.0, 0.0, K::Climb},
    {80.0, 80.0, 0.6, 0.0, 0.0, K::Summit},
    {90.0, 70.0, 0.2, -5.0, 0.0, K::Descent},
    {110.0, 0.0, 0.0, 0.0, 0.0, K::Stopping},
};

constexpr DriverStyle kCityStyle{1.6, 3.5, 0.09, 4.0, 0.03, 0.42, 0.3, 12.0, 0.0, 0.3};
constexpr DriverStyle kTrafficStyle{1.0, 4.0, 0.05, 2.0, 0.03, 0.3, 0.25, 8.0, 1.6, 0.25};
constexpr DriverStyle kRoadStyle{1.4, 3.0, 0.08, 4.0, 0.03, 0.45, 0.3, 12.0, 2.2, 0.32};
constexpr DriverStyle kMotorwayStyle{1.4, 3.0, 0.07, 4.0, 0.025, 0.4, 0.3, 12.0, 2.0, 0.3};
constexpr DriverStyle kMountainStyle{1.6, 4.0, 0.12, 3.0, 0.05, 0.55, 0.3, 12.0, 2.5, 0.38};
constexpr DriverStyle kDragStyle{8.0, 12.0, 1.0, 4.0, 0.08, 0.85, 0.35, 12.0, 0.0, 0.5};
constexpr DriverStyle kTrackStyle{6.0, 12.0, 0.3, 2.0, 0.1, 0.95, 0.4, 12.0, 8.5, 0.95};
constexpr DriverStyle kHillStyle{1.6, 4.0, 0.1, 3.0, 0.04, 0.45, 0.35, 12.0, 2.0, 0.3};
constexpr DriverStyle kTowingStyle{1.2, 3.0, 0.08, 4.0, 0.03, 0.4, 0.3, 12.0, 1.5, 0.25};

constexpr Scenario kScenarios[kDriveScenarios] = {
    {"city", kCityLegs, count(kCityLegs), 120.0, 0.0, 0.0, kCityStyle},
    {"stop_go", kStopAndGoLegs, count(kStopAndGoLegs), 66.0, 0.0, 0.0, kTrafficStyle},
    {"country", kCountryLegs, count(kCountryLegs), 110.0, 3.0, 0.0, kRoadStyle},
    {"motorway", kMotorwayLegs, count(kMotorwayLegs), 125.0, 0.0, 0.0, kMotorwayStyle},
    {"mountain", kMountainLegs, count(kMountainLegs), 162.0, 0.0, 0.0, kMountainStyle},
    {"drag", kDragLegs, count(kDragLegs), 46.0, 0.0, 0.0, kDragStyle},
    {"track", kTrackLegs, count(kTrackLegs), 90.0, 3.0, 0.0, kTrackStyle},
    {"hill_start", kHillStartLegs, count(kHillStartLegs), 62.0, 0.0, 0.0, kHillStyle},
    {"towing", kTowingLegs, count(kTowingLegs), 122.0, 0.0, 1500.0, kTowingStyle},
};

}  // namespace

const Scenario& scenarioOf(DriveScenario scenario) { return kScenarios[static_cast<int>(scenario)]; }

double scenarioCycle(const Scenario& s, double seconds) {
    if (s.loopBackSec <= 0.0 || seconds < s.loopSec) return std::fmod(seconds, s.loopSec);
    return s.loopBackSec + std::fmod(seconds - s.loopBackSec, s.loopSec - s.loopBackSec);
}

int scenarioLegAt(const Scenario& s, double cycleSec) {
    int at = 0;
    for (int i = 0; i < s.legCount; ++i) {
        if (s.legs[i].fromSec <= cycleSec) at = i;
    }
    return at;
}

double legTargetKmh(const Scenario& s, const ScenarioLeg& leg) {
    if (leg.cornerRadiusM <= 0.0) return leg.targetKmh;
    return std::min(leg.targetKmh, std::sqrt(s.style.lateralG * kGravity * leg.cornerRadiusM) * 3.6);
}

DriverOrder followLeg(const Scenario& s, int index, const DriverView& v) {
    const ScenarioLeg& leg = s.legs[index];
    const DriverStyle& style = s.style;
    double target = legTargetKmh(s, leg);

    if (style.anticipateDecel > 0.0) {
        // Arriver à la vitesse de l'étape suivante en s'y prenant à temps : c'est ce qui fait ralentir avant le virage.
        const bool last = index + 1 >= s.legCount;
        int nextIndex = last ? 0 : index + 1;
        if (last) nextIndex = scenarioLegAt(s, s.loopBackSec);
        const ScenarioLeg& next = s.legs[nextIndex];
        const double nextKmh = legTargetKmh(s, next);
        if (nextKmh < target) {
            const double until = (last ? s.loopSec : next.fromSec) - v.cycleSec;
            // En descente, la pente mange une partie du freinage.
            const double slope = std::max(-next.gradePct, 0.0) / 100.0;
            const double decel = std::max(style.anticipateDecel - kGravity * slope, 0.5);
            // La vitesse à ne pas dépasser maintenant, pour y arriver : elle descend toute seule à mesure qu'on approche.
            const double ceiling = nextKmh + decel * 3.6 * std::max(until - kReactionS, 0.0);
            target = std::min(target, ceiling);
            // Le pas à pas des derniers mètres n'a pas de sens : on finit de s'arrêter.
            if (nextKmh == 0.0 && target < style.stopBelowKmh * 0.5) target = 0.0;
        }
    }

    DriverOrder order;
    order.targetKmh = target;
    const double error = target - v.kmh;
    if (!v.lifting && target != 0.0) {
        order.pedal = clampTo(v.holdPedal + error * style.throttlePerKmh, 0.0, std::max(leg.eagerness, v.holdPedal));
    }
    const double wanted = error < -style.brakeMarginKmh ? std::min(-error * style.brakePerKmh, style.brakeMax) : 0.0;
    order.brake = (target == 0.0 && v.kmh < style.stopBelowKmh) ? style.stopBrake : wanted;
    return order;
}

}  // namespace revforge
