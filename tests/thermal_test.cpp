#include "doctest.h"

#include <algorithm>
#include <cmath>

#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"
#include "revforge/thermal.hpp"

using namespace revforge;

namespace {

constexpr double kDt = 0.002;

Powertrain city() { return findPreset("city_14")->powertrain(); }

Powertrain started(Powertrain p, StartTemp start, float ambientC = 20.0f) {
    p.engine.startTemp = start;
    p.engine.ambientC = ambientC;
    return p;
}

/** Ralenti au point mort pendant `seconds`, puis le régime moyen de la dernière seconde. */
double idleAfter(DrivingModel& m, double seconds) {
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        m.stepNeutral(kDt, 0.0);
        if (seconds - i * kDt <= 1.0) {
            sum += m.rpm();
            ++count;
        }
    }
    return sum / std::max(count, 1);
}

/** Ce que reçoit le réseau d'un 1,4 L au ralenti, à peu près : 5,3 kW de carburant, 1 kW de frottements. */
ThermalInput idleHeat(double ambientC) {
    ThermalInput in;
    in.fuelW = 5300.0;
    in.frictionW = 1000.0;
    in.rpm = 850.0;
    in.ambientC = ambientC;
    return in;
}

}  // namespace

TEST_CASE("thermique : la loi de Vogel retrouve la fiche de l'huile") {
    // Motorcraft 5W-30 : 60 mm²/s à 40 °C, 10,5 mm²/s à 100 °C, 4250 mPa·s à −30 °C ; densité 0,853 à 15,6 °C.
    auto density = [](double c) { return 853.0 * (1.0 - 0.00065 * (c - 15.6)); };
    CHECK(oilViscosityPaS(40.0) / density(40.0) * 1e6 == doctest::Approx(60.0).epsilon(0.005));
    CHECK(oilViscosityPaS(100.0) / density(100.0) * 1e6 == doctest::Approx(10.5).epsilon(0.005));
    CHECK(oilViscosityPaS(-30.0) == doctest::Approx(4.25).epsilon(0.005));
    // Froide, elle est une quinzaine de fois plus épaisse qu'à chaud : l'ordre de grandeur publié.
    CHECK(oilViscosityPaS(20.0) / oilViscosityPaS(100.0) == doctest::Approx(14.4).epsilon(0.02));
}

TEST_CASE("thermique : l'huile froide double presque les frottements, l'huile chaude n'y change rien") {
    const Powertrain p = city();
    // Sandoval et Heywood : environ deux fois plus de frottements à froid. Ici, selon le régime, à 20 °C puis à 0 °C.
    CHECK(frictionFactor(p, 20.0, 2000.0) > 1.4);
    CHECK(frictionFactor(p, 20.0, 2000.0) < 2.0);
    CHECK(frictionFactor(p, 0.0, 2000.0) > 2.0);
    CHECK(frictionFactor(p, 0.0, 2000.0) > frictionFactor(p, 20.0, 2000.0));
    // La référence chaude est exacte : aucun écart, pas un 1,0 calculé.
    CHECK(coldFrictionNm(p, kWarmOilC, 3000.0) == 0.0);
    CHECK(coldFrictionNm(p, 130.0, 3000.0) == 0.0);
    CHECK(frictionFactor(p, kWarmOilC, 3000.0) == 1.0);
    // Les termes hydrodynamiques suivent le régime : deux fois plus vite, deux fois plus de frottements en plus.
    CHECK(coldFrictionNm(p, 20.0, 4000.0) == doctest::Approx(2.0 * coldFrictionNm(p, 20.0, 2000.0)));
    // Électrique : rien de tout cela.
    Powertrain electric = p;
    electric.electric = true;
    CHECK(coldFrictionNm(electric, -20.0, 3000.0) == 0.0);
}

TEST_CASE("thermique : calculateur et starter, nuls dès 70 °C d'eau, rien sur un diesel") {
    EngineConfig petrol = city().engine;
    CHECK(coldIdleRise(-30.0) == doctest::Approx(1200.0 / 825.0 - 1.0));
    CHECK(coldIdleRise(70.0) == 0.0);
    CHECK(coldIdleRise(90.0) == 0.0);
    CHECK(coldEnrichment(petrol, 20.0) == doctest::Approx(0.15));
    CHECK(coldEnrichment(petrol, 70.0) == 0.0);
    EngineConfig carb = petrol;
    carb.fuelSystem = FuelSystem::Carburetor;
    CHECK(coldEnrichment(carb, 20.0) > coldEnrichment(petrol, 20.0));
    EngineConfig diesel = petrol;
    diesel.fuel = Fuel::Diesel;
    CHECK(coldEnrichment(diesel, -20.0) == 0.0);
}

TEST_CASE("thermique : ε-NTU d'un échangeur à courants croisés") {
    CHECK(crossFlowEffectiveness(0.0, 0.5) == 0.0);
    // Un seul fluide qui change de température : 1 − e^−NTU.
    CHECK(crossFlowEffectiveness(1.0, 0.0) == doctest::Approx(1.0 - std::exp(-1.0)));
    // Deux débits égaux, NTU = 1 : 0,476 dans les tables, la formule approchée donne 0,469.
    CHECK(crossFlowEffectiveness(1.0, 1.0) == doctest::Approx(0.476).epsilon(0.03));
    // Plus de surface, plus d'efficacité, sans jamais dépasser 1.
    CHECK(crossFlowEffectiveness(3.0, 0.5) > crossFlowEffectiveness(1.0, 0.5));
    CHECK(crossFlowEffectiveness(20.0, 0.5) <= 1.0);
}

TEST_CASE("thermique : le calorstat s'ouvre à 87 °C et se referme plus bas") {
    ThermalNetwork net;
    ThermalSpec spec = thermalSpecOf(city(), 180000.0);
    net.configure(spec);
    net.reset(StartTemp::Cold, 20.0);
    // Ralenti par 20 °C : l'eau monte, le calorstat reste fermé tant qu'elle n'a pas atteint son seuil.
    double openedAt = -1.0;
    for (int i = 0; i < 4000 * 100 && openedAt < 0.0; ++i) {
        ThermalInput in = idleHeat(20.0);
        in.fuelW = 30000.0;
        in.rpm = 2500.0;
        net.step(in, 0.01);
        if (net.thermostat() > 0.0) openedAt = net.coolantC();
    }
    REQUIRE(openedAt > 0.0);
    CHECK(openedAt == doctest::Approx(87.0).epsilon(0.01));
    // Plus de chaleur : il s'ouvre en grand ; le moteur s'arrête de chauffer : il ne se referme qu'une fois l'eau 2 K plus bas.
    double lastLift = 0.0;
    bool closedAbove85 = false;
    for (int i = 0; i < 600 * 100; ++i) {
        ThermalInput in = idleHeat(20.0);
        in.fuelW = 0.0;
        in.frictionW = 0.0;
        in.rpm = 2500.0;
        net.step(in, 0.01);
        if (net.thermostat() <= 0.0 && lastLift > 0.0 && net.coolantC() > 85.05) closedAbove85 = true;
        lastLift = net.thermostat();
    }
    CHECK_FALSE(closedAbove85);
    CHECK(net.thermostat() == 0.0);
}

TEST_CASE("thermique : par 45 °C au ralenti, le ventilateur tient l'eau ; en panne, elle surchauffe") {
    const ThermalSpec spec = thermalSpecOf(city(), 180000.0);
    ThermalNetwork working;
    working.configure(spec);
    working.reset(StartTemp::Warm, 45.0);
    ThermalSpec broken = spec;
    broken.fanMs = 0.0;
    ThermalNetwork failing;
    failing.configure(broken);
    failing.reset(StartTemp::Warm, 45.0);
    bool fanRan = false;
    for (int i = 0; i < 3600 * 100; ++i) {
        working.step(idleHeat(45.0), 0.01);
        failing.step(idleHeat(45.0), 0.01);
        fanRan = fanRan || working.fan();
    }
    CHECK(fanRan);
    CHECK(working.coolantC() < 105.0);
    CHECK(failing.coolantC() > 115.0);
}

TEST_CASE("thermique : parti chaud, le réseau ne fait que les jauges, au bit près") {
    // Deux airs très différents : les températures divergent, mais rien n'en revient vers le moteur. Si le réseau agissait sur
    // un moteur parti chaud, ces deux conduites se sépareraient. (Quand l'air ambiant agira sur la densité, comparer à air égal
    // un réseau perturbé autrement.)
    DrivingModel mild(started(city(), StartTemp::Warm, 20.0f));
    DrivingModel frost(started(city(), StartTemp::Warm, -30.0f));
    for (int i = 0; i < static_cast<int>(120.0 / kDt); ++i) {
        const double pedal = std::fmod(i * kDt, 20.0) < 8.0 ? 0.6 : 0.0;
        mild.stepPedal(kDt, pedal, 0.0);
        frost.stepPedal(kDt, pedal, 0.0);
        REQUIRE(mild.rpm() == frost.rpm());
        REQUIRE(mild.torqueNm() == frost.torqueNm());
        REQUIRE(mild.load() == frost.load());
    }
    CHECK(mild.consumption().fuelTripL() == frost.consumption().fuelTripL());
    CHECK(mild.thermal().coolantC() != frost.thermal().coolantC());
    CHECK_FALSE(mild.thermalCoupled());
    CHECK(mild.coldFrictionNm() == 0.0);
}

TEST_CASE("thermique : un départ à froid tient un ralenti plus haut, qui redescend avec l'eau") {
    DrivingModel warm(started(city(), StartTemp::Warm));
    DrivingModel cold(started(city(), StartTemp::Cold));
    const double warmIdle = idleAfter(warm, 10.0);
    const double coldIdle = idleAfter(cold, 10.0);
    // 20 °C d'eau : le calculateur vise environ 1,23 fois le ralenti chaud.
    CHECK(coldIdle > warmIdle * 1.15);
    CHECK(cold.thermal().coolantC() < 30.0);
    // Une fois l'eau passée à 70 °C, il redescend au ralenti du moteur chaud.
    const double later = idleAfter(cold, 900.0);
    CHECK(cold.thermal().coolantC() > 70.0);
    CHECK(later == doctest::Approx(idleAfter(warm, 1.0)).epsilon(0.03));
}

TEST_CASE("thermique : le moteur froid dépense plus, frottements et enrichissement compris") {
    DrivingModel warm(started(city(), StartTemp::Warm));
    DrivingModel cold(started(city(), StartTemp::Cold));
    for (int i = 0; i < static_cast<int>(120.0 / kDt); ++i) {
        warm.stepNeutral(kDt, 0.0);
        cold.stepNeutral(kDt, 0.0);
    }
    // Au ralenti par 20 °C : un tiers de plus environ dans les deux premières minutes.
    const double ratio = cold.consumption().fuelTripL() / warm.consumption().fuelTripL();
    CHECK(ratio > 1.15);
    CHECK(ratio < 1.6);
    CHECK(cold.coldEnrichment() > 0.0);
    CHECK(cold.coldFrictionNm() > 0.0);
}

TEST_CASE("thermique : l'huile froide freine le moteur, l'enrichissement se paie sans couple") {
    // Même pédale au point mort : le moteur froid plafonne plus bas, ses frottements en plus le retiennent.
    DrivingModel warm(started(city(), StartTemp::Warm));
    DrivingModel cold(started(city(), StartTemp::Cold));
    for (int i = 0; i < static_cast<int>(4.0 / kDt); ++i) {
        warm.stepNeutral(kDt, 0.2);
        cold.stepNeutral(kDt, 0.2);
    }
    CHECK(cold.rpm() < warm.rpm() - 100.0);
    // Le compteur paie ce que le calculateur ajoute, à couple égal : tout le surplus, rien de plus.
    ConsumptionMeter plain, enriched;
    plain.configure(city());
    enriched.configure(city());
    ConsumptionInput in;
    in.rpm = 2000.0;
    in.omega = 2000.0 * 2.0 * 3.141592653589793 / 60.0;
    in.brakeNm = 30.0;
    in.frictionNm = 14.0;
    in.fullGrossNm = 120.0;
    in.load = 0.3;
    in.burning = true;
    plain.step(in, 0.001);
    in.enrichment = 0.15;
    enriched.step(in, 0.001);
    CHECK(enriched.fuelGps() == doctest::Approx(plain.fuelGps() * 1.15));
    CHECK(enriched.lambdaNow() < plain.lambdaNow());
}

TEST_CASE("thermique : la fiche se mesure moteur chaud, et rien de tout cela sur l'électrique") {
    const Performance warm = measurePerformance(started(city(), StartTemp::Warm));
    const Performance cold = measurePerformance(started(city(), StartTemp::Cold));
    CHECK(cold.zeroTo100S == warm.zeroTo100S);
    Powertrain electric = started(city(), StartTemp::Cold, -20.0f);
    electric.electric = true;
    DrivingModel m(electric);
    for (int i = 0; i < static_cast<int>(5.0 / kDt); ++i) m.stepPedal(kDt, 0.5, 0.0);
    CHECK_FALSE(m.thermalCoupled());
    CHECK(m.coldFrictionNm() == 0.0);
    CHECK(m.coldIdleRpm() == 0.0);
}

TEST_CASE("thermique : chaque préréglage part à froid, ralenti plus haut et eau qui monte") {
    // Rotatif, turbo, compresseur, carburateurs, diesel : partout la même mécanique, et rien sur l'électrique.
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        CAPTURE(all[i].id);
        const Powertrain p = all[i].powertrain();
        DrivingModel warm(started(p, StartTemp::Warm, 5.0f));
        DrivingModel cold(started(p, StartTemp::Cold, 5.0f));
        const double warmIdle = idleAfter(warm, 20.0);
        const double coldIdle = idleAfter(cold, 20.0);
        if (p.electric) {
            CHECK_FALSE(cold.thermalCoupled());
            CHECK(coldIdle == warmIdle);
            continue;
        }
        CHECK(std::isfinite(coldIdle));
        CHECK(coldIdle > warmIdle * 1.1);
        CHECK(cold.thermal().coolantC() > 5.5);
        CHECK(cold.thermal().coolantC() < 40.0);
        CHECK(cold.consumption().fuelTripL() > warm.consumption().fuelTripL());
        CHECK_FALSE(cold.stalled());
    }
}
