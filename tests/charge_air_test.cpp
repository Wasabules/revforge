#include <algorithm>
#include <cmath>
#include <cstdio>

#include "doctest.h"
#include "revforge/charge_air.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/turbo.hpp"

using namespace revforge;

namespace {

constexpr double kStep = 0.001;
constexpr double kPi = 3.141592653589793;

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

Powertrain with(const char* id, Intercooler cooler, bool water = false, int nitrousKw = 0) {
    Powertrain p = preset(id);
    p.engine.intercooler = cooler;
    p.engine.waterInjection = water;
    p.engine.nitrousKw = nitrousKw;
    return p;
}

double ratedRpm(const Powertrain& p) { return 0.88 * static_cast<double>(p.engine.redlineRpm); }

/** Un coup de gaz à régime tenu : le temps pour atteindre 90 % de la pression établie, turbo seul. */
double stabSeconds(const Powertrain& p, double rpm) {
    const TurboSpec s = turboSpecOf(p);
    TurboCharger turbo;
    turbo.setSpec(s);
    TurboInput in;
    in.rpm = rpm;
    in.breathing = breathingOf(p, rpm);
    in.burning = true;
    in.fill = 0.2;
    for (int i = 0; i < 8000; ++i) turbo.step(kStep, in);
    const double wanted = 0.9 * turboSteady(s, rpm, in.breathing).boost;
    in.fill = 1.0;
    for (int i = 0; i < 30000; ++i) {
        turbo.step(kStep, in);
        if (turbo.boost() >= wanted) return static_cast<double>(i) * kStep;
    }
    return 30.0;
}

/** Trois départs arrêtés à fond, 8 s chacun, après une minute à l'arrêt : la charge au collecteur à la fin de chacun. */
struct Soak {
    double atRest = 0.0;
    double ends[3] = {0.0, 0.0, 0.0};
    double startsWater[3] = {0.0, 0.0, 0.0};
};

Soak soak(const Powertrain& p) {
    DrivingModel model(p);
    Soak out;
    for (int i = 0; i < 60000; ++i) model.stepPedal(kStep, 0.0, 0.3);
    out.atRest = model.manifoldCharge().temperatureK;
    for (int pull = 0; pull < 3; ++pull) {
        out.startsWater[pull] = model.chargeAir().waterLoopK();
        for (int i = 0; i < 8000; ++i) model.stepPedal(kStep, 1.0, 0.0);
        out.ends[pull] = model.manifoldCharge().temperatureK;
        for (int i = 0; i < 30000 && model.speedKmh() > 0.5; ++i) model.stepPedal(kStep, 0.0, 0.6);
        for (int i = 0; i < 20000; ++i) model.stepPedal(kStep, 0.0, 0.3);
    }
    return out;
}

}  // namespace

// ── Les propriétés ──

TEST_CASE("air de charge : la vapeur saturante de l'eau suit les tables") {
    CHECK(waterSaturationPa(293.15) == doctest::Approx(2339.0).epsilon(0.01));
    CHECK(waterSaturationPa(333.15) == doctest::Approx(19946.0).epsilon(0.01));
    CHECK(waterSaturationPa(373.15) == doctest::Approx(101325.0).epsilon(1e-9));
    CHECK(waterLatentJPerKg(273.15) == doctest::Approx(2.501e6).epsilon(0.005));
}

TEST_CASE("air de charge : le protoxyde saturé suit ESDU 91022 et le NIST") {
    // NIST : 3,12 MPa à 0 °C, 5,05 MPa à 20 °C ; liquide 786 kg/m³ et vapeur 159 kg/m³ à 20 °C ; chaleur latente de l'ordre de 170 kJ/kg.
    const NitrousSaturation cold = nitrousSaturation(273.15);
    const NitrousSaturation warm = nitrousSaturation(293.15);
    CHECK(cold.pressurePa == doctest::Approx(3.12e6).epsilon(0.01));
    CHECK(warm.pressurePa == doctest::Approx(5.05e6).epsilon(0.01));
    CHECK(warm.liquidKgM3 == doctest::Approx(786.0).epsilon(0.01));
    CHECK(warm.vapourKgM3 == doctest::Approx(159.0).epsilon(0.02));
    CHECK(warm.latentJPerKg == doctest::Approx(170e3).epsilon(0.05));
    // La chaleur latente s'annule au point critique, et la pression y monte.
    CHECK(nitrousSaturation(305.0).latentJPerKg < 0.7 * warm.latentJPerKg);
    CHECK(nitrousSaturation(305.0).pressurePa > warm.pressurePa);
}

TEST_CASE("air de charge : l'efficacité à courants croisés suit ses limites") {
    CHECK(crossflowEffectiveness(0.0, 0.5) == 0.0);
    CHECK(crossflowEffectiveness(1.0, 0.0) == doctest::Approx(1.0 - std::exp(-1.0)).epsilon(1e-9));
    // Table de Kays et London, deux fluides non brassés, NTU 1, Cr 1 : 0,476.
    CHECK(crossflowEffectiveness(1.0, 1.0) == doctest::Approx(0.476).epsilon(0.02));
    CHECK(crossflowEffectiveness(20.0, 0.5) > 0.97);
    CHECK(crossflowEffectiveness(3.0, 0.5) > crossflowEffectiveness(2.0, 0.5));
    CHECK(crossflowEffectiveness(2.0, 0.2) > crossflowEffectiveness(2.0, 0.8));
}

// ── Au défaut, rien ne s'exécute ──

TEST_CASE("air de charge : au défaut, aucun préréglage n'est touché") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        CAPTURE(all[i].id);
        CHECK_FALSE(chargeAirActive(p));
        CHECK_FALSE(chargeAirSetupOf(p).active);
        if (p.engine.aspiration == Aspiration::Turbo) CHECK(turboSpecOf(p).cooler.kind == Intercooler::AirToAir);
    }
    // Un réglage qui n'a pas de sens sur ce moteur reste sans effet : l'échangeur d'un atmosphérique, l'eau d'un diesel.
    CHECK_FALSE(chargeAirActive(with("hot_hatch_na", Intercooler::None)));
    CHECK_FALSE(chargeAirActive(with("diesel_four", Intercooler::AirToAir, true, 50)));
    CHECK(chargeAirActive(with("diesel_four", Intercooler::None)));
}

// ── L'échangeur ──

TEST_CASE("air de charge : l'air-air par ε-NTU rend l'échangeur d'origine au point de calcul, et suit la vitesse") {
    const ChargeConditions c;
    const ChargeCoolerSpec spec = airToAirCoolerFor(0.21, 408.0, c);
    const double stock = stockCoolerOutletK(408.0, 1.0);
    CHECK(airToAirOutletK(spec, 408.0, 0.21, kChargeReferenceSpeedMs, c) == doctest::Approx(stock).epsilon(1e-6));
    // À l'arrêt, seul le ventilateur souffle : bien plus chaud. Plus vite, mieux refroidi.
    CHECK(airToAirOutletK(spec, 408.0, 0.21, 0.0, c) > stock + 20.0);
    CHECK(airToAirOutletK(spec, 408.0, 0.21, 200.0 / 3.6, c) < stock - 3.0);
    // À faible débit, l'efficacité tend vers 1, quand l'échangeur d'origine la plafonne à 0,86.
    CHECK(airToAirOutletK(spec, 330.0, 0.02, kChargeReferenceSpeedMs, c) < stockCoolerOutletK(330.0, 0.02 / 0.21));
}

TEST_CASE("air de charge : l'air-air ε-NTU, prêt à brancher, chiffré face à l'échangeur d'origine") {
    // L'écart qu'il donnerait au couple à la puissance maximale, à pression égale : la densité de la charge, Tstock/Tε-NTU.
    for (const char* id : {"three_cyl_turbo", "hot_hatch_turbo", "five_cyl_turbo", "boxer_four_turbo", "diesel_four", "v8_supercharged"}) {
        const Powertrain p = preset(id);
        const double rpm = powerSummary(p).peakPowerRpm;
        double hot = 0.0, flow = 0.0, rated = 0.0;
        if (p.engine.aspiration == Aspiration::Turbo) {
            const TurboSpec spec = turboSpecOf(p);
            const TurboSteady t = turboSteady(spec, rpm, breathingOf(p, rpm));
            hot = t.outK;
            flow = t.flow;
            rated = spec.ratedFlow;
        } else {
            const SuperchargerSpec spec = superchargerSpecOf(p);
            const SuperchargerSteady t = superchargerSteady(spec, rpm, breathingOf(p, rpm));
            hot = t.outK;
            flow = t.flow;
            rated = spec.ratedFlow;
        }
        const ChargeCoolerSpec ntu = airToAirCoolerFor(rated, hot);
        const double stock = stockCoolerOutletK(hot, flow / rated);
        char line[200];
        std::snprintf(line, sizeof line, "%s %.0f tr/min : origine %.1f °C ; ε-NTU 0/50/100/150 km/h %.1f/%.1f/%.1f/%.1f °C ; couple %+.1f/%+.1f/%+.1f/%+.1f %%", id, rpm,
                      stock - 273.15, airToAirOutletK(ntu, hot, flow, 0.0) - 273.15, airToAirOutletK(ntu, hot, flow, 50.0 / 3.6) - 273.15,
                      airToAirOutletK(ntu, hot, flow, 100.0 / 3.6) - 273.15, airToAirOutletK(ntu, hot, flow, 150.0 / 3.6) - 273.15,
                      (stock / airToAirOutletK(ntu, hot, flow, 0.0) - 1.0) * 100.0, (stock / airToAirOutletK(ntu, hot, flow, 50.0 / 3.6) - 1.0) * 100.0,
                      (stock / airToAirOutletK(ntu, hot, flow, 100.0 / 3.6) - 1.0) * 100.0, (stock / airToAirOutletK(ntu, hot, flow, 150.0 / 3.6) - 1.0) * 100.0);
        MESSAGE(line);
        // Au nominal et à 100 km/h, il retrouve l'origine : l'écart à la puissance maximale reste de l'ordre du pour cent.
        CHECK(std::fabs(stock / airToAirOutletK(ntu, hot, flow, kChargeReferenceSpeedMs) - 1.0) < 0.03);
    }
}

TEST_CASE("air de charge : l'air-eau est dimensionné comme l'air-air d'origine au nominal, à 100 km/h") {
    const ChargeConditions c;
    const ChargeCoolerSpec spec = chargeCoolerFor(Intercooler::AirToWater, 0.21, 408.0, c);
    CHECK(brickEffectiveness(spec, 0.21) == doctest::Approx(0.85).epsilon(1e-6));
    CHECK(chargeCoolerSteadyK(spec, 408.0, 0.21, c) == doctest::Approx(stockCoolerOutletK(408.0, 1.0)).epsilon(1e-6));
    // Le radiateur basse température rejette beaucoup moins à l'arrêt : la boucle s'y équilibre plus haut.
    CHECK(radiatorConductanceWK(spec, 0.0, c) < 0.3 * radiatorConductanceWK(spec, kChargeReferenceSpeedMs, c));
    CHECK(waterLoopSteadyK(spec, 408.0, 0.21, 0.0, c) > waterLoopSteadyK(spec, 408.0, 0.21, kChargeReferenceSpeedMs, c) + 30.0);
    // Au ralenti, à l'arrêt : le compartiment chauffe la boucle au-dessus de l'air.
    CHECK(waterLoopSteadyK(spec, 300.0, 0.005, 0.0, c) > c.ambientK + 1.0);
    // Aucun échangeur : la sortie du compresseur.
    CHECK(chargeCoolerSteadyK(chargeCoolerFor(Intercooler::None, 0.21, 408.0), 408.0, 0.21) == 408.0);
}

TEST_CASE("air de charge : sans échangeur, moins de volume et de perte, une charge plus chaude et moins de couple") {
    const Powertrain stock = preset("hot_hatch_turbo");
    const Powertrain none = with("hot_hatch_turbo", Intercooler::None);
    const Powertrain water = with("hot_hatch_turbo", Intercooler::AirToWater);
    const TurboSpec a = turboSpecOf(stock), n = turboSpecOf(none), w = turboSpecOf(water);
    CHECK(n.chargeVolumeM3 < 0.4 * a.chargeVolumeM3);
    CHECK(w.chargeVolumeM3 < 0.7 * a.chargeVolumeM3);
    CHECK(n.coolerDrop < 0.3 * a.coolerDrop);
    CHECK(w.coolerDrop < a.coolerDrop);
    // Le volume se remplit plus vite : moins de retard.
    const double rpm = 3000.0;
    char line[160];
    std::snprintf(line, sizeof line, "90 %% de la pression à 3000 tr/min : air-air %.2f s, air-eau %.2f s, aucun %.2f s ; volume %.1f/%.1f/%.1f L", stabSeconds(stock, rpm),
                  stabSeconds(water, rpm), stabSeconds(none, rpm), a.chargeVolumeM3 * 1000.0, w.chargeVolumeM3 * 1000.0, n.chargeVolumeM3 * 1000.0);
    MESSAGE(line);
    CHECK(stabSeconds(none, rpm) < stabSeconds(stock, rpm));
    CHECK(stabSeconds(water, rpm) < stabSeconds(stock, rpm));
    // À la puissance maximale : la sortie du compresseur, plus de 100 °C, et la densité qui va avec.
    const double at = ratedRpm(stock);
    const TurboSteady hot = turboSteady(n, at, breathingOf(none, at));
    CHECK(hot.coolK == hot.outK);
    CHECK(hot.coolK - 273.15 > 100.0);
    const double lost = torqueCurveNm(none, at) / torqueCurveNm(stock, at);
    CHECK(lost < 0.85);
    CHECK(lost > 0.7);
    // La boîte et l'embrayage restent ceux du moteur d'origine.
    CHECK(gearingOf(none).box.finalDrive == gearingOf(stock).box.finalDrive);
}

TEST_CASE("air de charge : l'air-eau chauffe à l'arrêt, puis à chaque départ, et refroidit lentement") {
    const Soak s = soak(with("hot_hatch_turbo", Intercooler::AirToWater));
    const Soak stock = soak(preset("hot_hatch_turbo"));
    char line[200];
    std::snprintf(line, sizeof line, "air-eau : arrêt %.1f °C, fins de départ %.1f/%.1f/%.1f °C, eau au départ %.1f/%.1f/%.1f °C ; air-air %.1f/%.1f/%.1f °C",
                  s.atRest - 273.15, s.ends[0] - 273.15, s.ends[1] - 273.15, s.ends[2] - 273.15, s.startsWater[0] - 273.15, s.startsWater[1] - 273.15,
                  s.startsWater[2] - 273.15, stock.ends[0] - 273.15, stock.ends[1] - 273.15, stock.ends[2] - 273.15);
    MESSAGE(line);
    CHECK(s.startsWater[0] > 293.5);
    CHECK(s.startsWater[1] > s.startsWater[0] + 2.0);
    CHECK(s.startsWater[2] > s.startsWater[1]);
    CHECK(s.ends[2] > s.ends[0] + 1.5);
    // Le premier départ profite de la masse de la boucle : plus frais que l'air-air, qui n'en a presque pas.
    CHECK(s.ends[0] < stock.ends[0]);
    // L'air-air d'origine ne garde rien d'un départ à l'autre.
    CHECK(std::fabs(stock.ends[2] - stock.ends[0]) < 1.0);
}

TEST_CASE("air de charge : sur un compresseur, l'échangeur change aussi") {
    const Powertrain stock = preset("v8_supercharged");
    const Powertrain none = with("v8_supercharged", Intercooler::None);
    const SuperchargerSpec a = superchargerSpecOf(stock), n = superchargerSpecOf(none);
    CHECK(n.chargeVolumeM3 < 0.5 * a.chargeVolumeM3);
    CHECK(n.coolerDrop < a.coolerDrop);
    // Le compresseur reste le même : rotor, rapport.
    CHECK(n.rotorVolumeM3 == a.rotorVolumeM3);
    CHECK(n.driveRatio == a.driveRatio);
    const double at = ratedRpm(stock);
    CHECK(superchargerSteady(n, at, breathingOf(none, at)).coolK > superchargerSteady(a, at, breathingOf(stock, at)).coolK + 30.0);
    CHECK(torqueCurveNm(none, at) < 0.92 * torqueCurveNm(stock, at));
    // Un air-eau : à peu près l'origine sur la fiche.
    CHECK(torqueCurveNm(with("v8_supercharged", Intercooler::AirToWater), at) == doctest::Approx(torqueCurveNm(stock, at)).epsilon(0.03));
}

// ── L'eau ──

TEST_CASE("air de charge : l'eau s'évapore jusqu'à saturer l'air, et le bilan d'enthalpie ferme") {
    // De l'air sec à 20 °C et 1 atm : la température de saturation adiabatique est de l'ordre de 6 à 8 °C.
    const WaterSpray dry = waterSpray(293.15, 101325.0, 0.023, 293.15);
    CHECK(dry.outK > 278.0);
    CHECK(dry.outK < 284.0);
    CHECK(dry.liquidPerAir > 0.0);
    CHECK(dry.vapourPerAir == doctest::Approx(0.622 * waterSaturationPa(dry.outK) / (101325.0 - waterSaturationPa(dry.outK))).epsilon(1e-4));
    // Une charge chaude et sous pression : tout s'évapore jusqu'à la saturation, et la vapeur ne dépasse jamais ce que l'air porte.
    const WaterSpray boosted = waterSpray(330.0, 2.3e5, 0.023, 293.15);
    CHECK(boosted.outK < 330.0 - 15.0);
    CHECK(boosted.vapourPerAir <= 0.622 * waterSaturationPa(boosted.outK) / (2.3e5 - waterSaturationPa(boosted.outK)) * (1.0 + 1e-6));
    const double released = 1005.0 * (330.0 - boosted.outK);
    const double taken = 0.023 * 4186.0 * (boosted.outK - 293.15) + boosted.vapourPerAir * waterLatentJPerKg(boosted.outK);
    CHECK(released == doctest::Approx(taken).epsilon(1e-4));
    // Un peu d'eau dans beaucoup de chaleur : tout s'évapore.
    CHECK(waterSpray(400.0, 2e5, 0.005, 293.15).liquidPerAir == 0.0);
    CHECK(boosted.densityShare > 1.0);
}

TEST_CASE("air de charge : l'injection d'eau refroidit la charge et ajoute du couple, plein gaz seulement") {
    const Powertrain stock = preset("hot_hatch_turbo");
    const Powertrain water = with("hot_hatch_turbo", Intercooler::AirToAir, true);
    const double at = ratedRpm(stock);
    const double gain = torqueCurveNm(water, at) / torqueCurveNm(stock, at);
    CHECK(gain > 1.03);
    CHECK(gain < 1.12);
    CHECK(steadyManifoldCharge(water, at).temperatureK < steadyManifoldCharge(stock, at).temperatureK - 10.0);
    CHECK(waterPerAirAt(0.5) == 0.0);
    CHECK(waterPerAirAt(1.0) == doctest::Approx(0.3 / (14.7 * 0.88)));
    // En conduite, à mi-charge, la pompe ne débite rien.
    DrivingModel model(water);
    for (int i = 0; i < 3000; ++i) model.stepPedal(kStep, 0.2, 0.0);
    CHECK(model.chargeAir().waterGps() == 0.0);
    for (int i = 0; i < 3000; ++i) model.stepPedal(kStep, 1.0, 0.0);
    CHECK(model.chargeAir().waterGps() > 1.0);
}

// ── Le protoxyde ──

TEST_CASE("air de charge : le kit de protoxyde donne sa puissance, au-delà de son régime d'activation") {
    const Powertrain stock = preset("hot_hatch_na");
    const Powertrain nos = with("hot_hatch_na", Intercooler::AirToAir, false, 75);
    const ChargeAirSetup setup = chargeAirSetupOf(nos);
    CHECK(setup.nitrous.activationRpm == doctest::Approx(std::max(3000.0, 0.4 * 7300.0)));
    // Sous le régime d'activation, rien ; au-dessus, la puissance du kit, à l'air ambiant près que le protoxyde refroidit ou chasse.
    CHECK(torqueCurveNm(nos, 2500.0) == torqueCurveNm(stock, 2500.0));
    for (double rpm : {3500.0, 5000.0, 6500.0}) {
        const double addedKw = (torqueCurveNm(nos, rpm) - torqueCurveNm(stock, rpm)) * rpm * 2.0 * kPi / 60.0 / 1000.0;
        CAPTURE(rpm);
        CHECK(addedKw > 0.85 * 75.0);
        CHECK(addedKw < 1.15 * 75.0);
    }
    // Il refroidit la charge, et l'oxygène du comburant passe de 23 % à plus.
    const ManifoldCharge m = steadyManifoldCharge(nos, 5000.0);
    CHECK(m.temperatureK < 293.0 - 20.0);
    CHECK(m.oxygenShare > 0.25);
    // La boîte reste celle d'origine.
    CHECK(gearingOf(nos).box.finalDrive == gearingOf(stock).box.finalDrive);
}

TEST_CASE("air de charge : la bouteille se vide comme le disent les préparateurs, et sa pression baisse") {
    const NitrousKit kit = nitrousKitFor(75, 7000.0, 101325.0);
    // Règle des préparateurs : 0,8 lb (0,36 kg) par 100 ch (74,6 kW) et par 10 s.
    const double perTenSeconds = kit.nitrousKgS * 10.0 * 74.6 / 75.0;
    CHECK(perTenSeconds > 0.27);
    CHECK(perTenSeconds < 0.45);
    NitrousBottle bottle = fullBottle(kit, 293.0);
    CHECK(bottle.massKg == doctest::Approx(4.536));
    CHECK(bottle.pressurePa == doctest::Approx(nitrousSaturation(293.0).pressurePa));
    // Dix secondes à fond : le liquide qui bout refroidit la bouteille, la pression baisse.
    for (int i = 0; i < 10000; ++i) stepBottle(bottle, kit, nitrousFlowKgS(kit, bottle, 101325.0), kStep, 293.0);
    CHECK(bottle.kelvin < 292.5);
    CHECK(bottle.pressurePa < nitrousSaturation(293.0).pressurePa - 0.3e5);
    // Jusqu'au bout : la pression et le débit s'effondrent quand il n'y a plus de liquide.
    double seconds = 10.0;
    double lastLiquid = 0.0;
    while (bottle.liquidKg > 0.0 && seconds < 600.0) {
        lastLiquid = nitrousFlowKgS(kit, bottle, 101325.0);
        stepBottle(bottle, kit, lastLiquid, kStep, 293.0);
        seconds += kStep;
    }
    for (int i = 0; i < 5000; ++i) stepBottle(bottle, kit, nitrousFlowKgS(kit, bottle, 101325.0), kStep, 293.0);
    char line[160];
    std::snprintf(line, sizeof line, "bouteille de 10 lb, kit de 75 kW : liquide épuisé après %.0f s, à %.1f °C ; 5 s plus tard, %.1f bar, %.1f g/s", seconds, bottle.kelvin - 273.15,
                  bottle.pressurePa / 1e5, nitrousFlowKgS(kit, bottle, 101325.0) * 1000.0);
    MESSAGE(line);
    CHECK(seconds > 100.0);
    CHECK(seconds < 200.0);
    CHECK(nitrousFlowKgS(kit, bottle, 101325.0) < 0.3 * lastLiquid);
}

TEST_CASE("air de charge : en conduite, le protoxyde s'ouvre pied au plancher, se paie en essence, et l'embrayage reste le même") {
    const Powertrain stock = preset("hot_hatch_na");
    const Powertrain nos = with("hot_hatch_na", Intercooler::AirToAir, false, 75);
    DrivingModel a(stock), b(nos);
    // Le même embrayage.
    for (int i = 0; i < 2000; ++i) {
        a.stepNeutral(kStep, 0.0);
        b.stepNeutral(kStep, 0.0);
    }
    // À mi-pédale, rien ne s'ouvre ; pied au plancher au-delà de 3000 tr/min, le kit s'ouvre et la bouteille se vide.
    for (int i = 0; i < 400; ++i) b.stepNeutral(kStep, 0.6);
    CHECK_FALSE(b.chargeAir().nitrousOn());
    const double full = b.chargeAir().bottle().massKg;
    bool opened = false;
    double fuelOn = 0.0;
    for (int i = 0; i < 3000 && !opened; ++i) {
        b.stepNeutral(kStep, 1.0);
        if (b.chargeAir().nitrousOn()) {
            opened = true;
            CHECK(b.rpm() >= chargeAirSetupOf(nos).nitrous.activationRpm);
            fuelOn = b.consumption().fuelGps();
        }
    }
    CHECK(opened);
    CHECK(b.chargeAir().bottle().massKg < full);
    // Le gicleur d'essence se paie : plus que l'air seul ne demanderait.
    CHECK(fuelOn > b.chargeAir().nitrousFuelGps());
    CHECK(b.chargeFill() > 0.1);
    b.refillNitrous();
    CHECK(b.chargeAir().bottle().massKg == doctest::Approx(full));
    // La fiche du même moteur : le couple ajouté se voit, mais 0-100 km/h aussi.
    CHECK(measurePerformance(nos).zeroTo100S < measurePerformance(stock).zeroTo100S - 0.5);
}

TEST_CASE("air de charge : la charge au collecteur se lit aussi au défaut") {
    DrivingModel model(preset("hot_hatch_turbo"));
    for (int i = 0; i < 4000; ++i) model.stepPedal(kStep, 1.0, 0.0);
    const ManifoldCharge m = model.manifoldCharge();
    CHECK(m.temperatureK == model.turbo().state().coolerOutK);
    CHECK(m.pressurePa == model.turbo().state().plenumPa);
    CHECK(m.waterPerAir == 0.0);
    CHECK(m.oxygenShare == doctest::Approx(0.2314));
    CHECK(model.chargeFill() == 0.0);
}
