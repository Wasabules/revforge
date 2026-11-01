#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"

extern std::atomic<long> g_allocations;

using namespace revforge;

namespace {

constexpr double kDt = 0.002;

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

Powertrain sizedEngine(int cc, int cylinders) {
    Powertrain p = referencePowertrain();
    p.engine.displacementCc = cc;
    p.engine.cylinders = cylinders;
    return p;
}

Powertrain dieselVersion(Powertrain p) {
    p.engine.fuel = Fuel::Diesel;
    p.engine.redlineRpm = 4500;
    return p;
}

/** Tient une vitesse, à la façon du conducteur de `--scenario cruise`. */
void holdSpeed(DrivingModel& m, double kmh, double seconds) {
    double held = 0.0;
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        const double error = kmh - m.speedKmh();
        const double wanted = 0.04 * error + held;
        if (wanted > 0.0 && wanted < 1.0) held += 0.012 * error * kDt;
        if (held < 0.0) held = 0.0;
        m.stepPedal(kDt, std::clamp(wanted, 0.0, 1.0), error < -1.5 ? 0.2 : 0.0);
    }
}

/** L/100 km établis à cette vitesse : après la mise en route, comme sur une route plate. */
double steadyLper100(const Powertrain& p, double kmh) {
    DrivingModel m(p);
    holdSpeed(m, kmh, 60.0);
    m.resetTrip();
    holdSpeed(m, kmh, 30.0);
    return m.consumption().fuelAvgLper100km();
}

double idleLph(const Powertrain& p) {
    DrivingModel m(p);
    for (int i = 0; i < static_cast<int>(10.0 / kDt); ++i) m.stepNeutral(kDt, 0.0);
    return m.consumption().fuelLph();
}

/** Passe en manuelle sur ce rapport, à cette vitesse, puis tient la vitesse sur ce rapport. */
DrivingModel inGear(const Powertrain& p, int gear, double kmh) {
    DrivingModel m(p);
    m.setManual(true);
    m.requestShift(1);
    double held = 0.0;
    for (int i = 0; i < static_cast<int>(60.0 / kDt); ++i) {
        const int now = m.gear();
        if (now >= 1 && now < gear && !m.shifting()) {
            // Le régime qu'imposeraient les roues sur le rapport suivant : pas celui d'un embrayage qui patine au départ.
            const double next = m.speedKmh() / 3.6 / wheelRadiusM(p.car) * m.gearing().overall[static_cast<std::size_t>(now)] * 60.0 / (2.0 * 3.141592653589793);
            if (next > m.idleRpm() * 1.5) m.requestShift(1);
        }
        const double error = kmh - m.speedKmh();
        const double wanted = 0.04 * error + held;
        if (wanted > 0.0 && wanted < 1.0) held += 0.012 * error * kDt;
        if (held < 0.0) held = 0.0;
        m.stepPedal(kDt, std::clamp(wanted, 0.0, 1.0), error < -1.5 ? 0.2 : 0.0);
    }
    return m;
}

/** Le meilleur point de la carte, et la pleine charge au régime de puissance maximale. */
struct MapReading {
    double best;
    double fullLoad;
};

MapReading readMap(const Powertrain& p) {
    const FuelMapSummary summary = fuelMapSummary(p);
    return {summary.bestGPerKwh, summary.peakPowerGPerKwh};
}

std::uint32_t soundHash(bool touchMeter) {
    const Powertrain p = preset("hot_hatch_turbo");
    DrivingModel model(p);
    EngineSound sound(44100);
    sound.setProfile(p.profile());
    std::vector<float> block(128);
    std::uint32_t hash = 2166136261u;
    for (int i = 0; i < 2000; ++i) {
        model.stepPedal(128.0 / 44100.0, i > 300 ? 0.8 : 0.0, 0.0);
        if (touchMeter) {
            if (i % 50 == 0) model.resetTrip();
            hash += static_cast<std::uint32_t>(model.consumption().fuelLph() > -1.0 ? 0 : 1);
        }
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
        sound.setRoad(static_cast<float>(model.speedKmh() / 3.6), static_cast<float>(model.wheelSlip()));
        sound.setGearbox(model.box(), model.gearboxDrive());
        sound.render(block.data(), 128);
        for (float s : block) {
            const int v = std::clamp(static_cast<int>(s * 32767.0f), -32768, 32767);
            hash = (hash ^ static_cast<std::uint32_t>(v & 0xFFFF)) * 16777619u;
        }
    }
    return hash;
}

}  // namespace

TEST_SUITE("consommation") {

TEST_CASE("rien ne brûle au rupteur : le débit tombe à zéro à chaque coupure") {
    DrivingModel m(referencePowertrain());
    int cuts = 0;
    for (int i = 0; i < static_cast<int>(6.0 / 0.001); ++i) {
        m.stepNeutral(0.001, 1.0);
        if (m.cut()) {
            ++cuts;
            CHECK(m.consumption().fuelGps() == 0.0);
        }
    }
    CHECK(cuts > 100);
}

TEST_CASE("rien ne brûle pied levé en roulant : le moteur est entraîné par les roues") {
    DrivingModel m(referencePowertrain());
    holdSpeed(m, 100.0, 40.0);
    CHECK(m.consumption().fuelLph() > 1.0);
    for (int i = 0; i < static_cast<int>(2.0 / kDt); ++i) m.stepPedal(kDt, 0.0, 0.0);
    CHECK(m.gear() > 0);
    CHECK(m.rpm() > m.idleRpm() * 1.2);
    CHECK(m.consumption().fuelLph() == 0.0);
    CHECK(m.consumption().fuelLper100km() == 0.0);
}

TEST_CASE("contact coupé, moteur à l'arrêt : ni essence ni huile") {
    DrivingModel m(referencePowertrain());
    for (int i = 0; i < static_cast<int>(3.0 / kDt); ++i) m.stepNeutral(kDt, 0.0);
    CHECK(m.consumption().fuelLph() > 0.0);
    const double before = m.consumption().fuelTripL();
    const double oilBefore = m.consumption().oilTripMl();
    for (int i = 0; i < static_cast<int>(6.0 / kDt); ++i) m.stepOff(kDt);
    CHECK(m.stopped());
    CHECK(m.consumption().fuelLph() == 0.0);
    CHECK(m.consumption().oilGph() == 0.0);
    CHECK(m.consumption().fuelTripL() == before);
    const double oilAtStop = m.consumption().oilTripMl();
    CHECK(oilAtStop >= oilBefore);
    for (int i = 0; i < static_cast<int>(5.0 / kDt); ++i) m.stepOff(kDt);
    CHECK(m.consumption().oilTripMl() == oilAtStop);
}

TEST_CASE("calé, le moteur ne dépense plus rien") {
    Powertrain p = referencePowertrain();
    DrivingModel m(p);
    m.setManual(true);
    m.requestShift(1);
    for (int i = 0; i < static_cast<int>(1.0 / kDt); ++i) m.stepPedal(kDt, 0.0, 0.0);
    // En cinquième à l'arrêt, sans gaz, freins serrés : il cale.
    for (int g = 0; g < 4; ++g) {
        m.requestShift(1);
        for (int i = 0; i < static_cast<int>(1.0 / kDt); ++i) m.stepPedal(kDt, 0.5, 0.0);
    }
    for (int i = 0; i < static_cast<int>(10.0 / kDt) && !m.stalled(); ++i) m.stepPedal(kDt, 0.0, 1.0);
    REQUIRE(m.stalled());
    const double total = m.consumption().fuelTripL();
    for (int i = 0; i < static_cast<int>(5.0 / kDt); ++i) m.stepPedal(kDt, 0.0, 1.0);
    CHECK(m.consumption().fuelLph() == 0.0);
    CHECK(m.consumption().fuelTripL() == total);
}

TEST_CASE("le ralenti dépense quelques dixièmes de litre par heure, et plus sur un gros moteur") {
    // Argonne : 0,61 L/h pour un 2,0 L, 1,48 L/h pour un 4,6 L, à vide. À 40 % près : le modèle n'a ni alternateur ni direction.
    const double small = idleLph(preset("city_14"));
    const double two = idleLph(preset("four_cyl_sport"));
    const double big = idleLph(preset("v8_cross_plane"));
    CHECK(small > 0.3);
    CHECK(small < 0.9);
    CHECK(two > 0.61 * 0.6);
    CHECK(two < 0.61 * 1.4);
    CHECK(big > 1.48 * 0.7);
    CHECK(big < 1.48 * 1.5);
    CHECK(big > 2.0 * small);
}

TEST_CASE("la pleine charge coûte plus cher que le meilleur point, en essence comme en turbo") {
    for (const char* id : {"city_14", "four_cyl_sport", "hot_hatch_turbo", "v8_cross_plane"}) {
        CAPTURE(id);
        const MapReading map = readMap(preset(id));
        CHECK(map.fullLoad > map.best * 1.1);
    }
}

TEST_CASE("l'essence enrichit à pleine charge, le turbo davantage, le diesel jamais") {
    // Au même régime, de 85 % à 100 % du couple : le rendement mécanique s'améliore, tout ce qui monte vient du mélange.
    const auto jump = [](const char* id) {
        const Powertrain p = preset(id);
        const double rpm = powerSummary(p).peakPowerRpm;
        const double full = torqueCurveNm(p, rpm);
        return brakeSpecificFuel(p, rpm, full) / brakeSpecificFuel(p, rpm, 0.85 * full);
    };
    CHECK(jump("hot_hatch_na") > 1.03);
    CHECK(jump("hot_hatch_turbo") > 1.1);
    CHECK(jump("hot_hatch_turbo") > jump("hot_hatch_na"));
    CHECK(jump("diesel_four") < 1.0);
}

TEST_CASE("le meilleur rendement du moteur reste dans ce que publient les constructeurs") {
    // Wikipédia, table citant Muta (SAE 2004-01-0064) et Ford : de 225 à 250 g/kWh en essence, 197 à 205 en diesel.
    for (const char* id : {"city_14", "four_cyl_sport", "hot_hatch_na", "inline_six", "v8_cross_plane", "hot_hatch_turbo", "three_cyl_turbo"}) {
        CAPTURE(id);
        const double best = readMap(preset(id)).best;
        CHECK(best > 225.0);
        CHECK(best < 260.0);
    }
    const double diesel = readMap(preset("diesel_four")).best;
    CHECK(diesel > 190.0);
    CHECK(diesel < 210.0);
}

TEST_CASE("à faible charge, le pompage et les frottements dégradent le rendement") {
    const Powertrain p = preset("city_14");
    const double at = 2500.0;
    const double full = fullLoadTorqueNm(p, at, 0.0);
    const double light = brakeSpecificFuel(p, at, 0.1 * full);
    const double middle = brakeSpecificFuel(p, at, 0.3 * full);
    const double high = brakeSpecificFuel(p, at, 0.8 * full);
    CHECK(light > 1.4 * middle);
    CHECK(middle > 1.2 * high);
}

TEST_CASE("le papillon coûte à l'essence ce qu'il ne coûte pas au diesel : l'écart se creuse à faible charge") {
    const Powertrain petrol = sizedEngine(2000, 4);
    const Powertrain diesel = dieselVersion(petrol);
    const double at = 2000.0;
    const double light = 10.0 * 1000.0 / (at * 2.0 * 3.141592653589793 / 60.0);
    const double strong = 40.0 * 1000.0 / (at * 2.0 * 3.141592653589793 / 60.0);
    const double gapLight = brakeSpecificFuel(petrol, at, light) / brakeSpecificFuel(diesel, at, light);
    const double gapStrong = brakeSpecificFuel(petrol, at, strong) / brakeSpecificFuel(diesel, at, strong);
    CHECK(gapLight > gapStrong);
    CHECK(gapStrong > 1.0);
}

TEST_CASE("à puissance égale, le diesel consomme moins que l'essence, en masse comme en volume") {
    const Powertrain petrol = sizedEngine(2000, 4);
    const Powertrain diesel = dieselVersion(petrol);
    for (double kw : {10.0, 20.0, 40.0}) {
        CAPTURE(kw);
        const double rpm = 2200.0;
        const double brake = kw * 1000.0 / (rpm * 2.0 * 3.141592653589793 / 60.0);
        const double petrolG = brakeSpecificFuel(petrol, rpm, brake);
        const double dieselG = brakeSpecificFuel(diesel, rpm, brake);
        CHECK(dieselG < petrolG);
        CHECK(dieselG / fuelSpecOf(Fuel::Diesel).densityKgPerL < petrolG / fuelSpecOf(Fuel::Petrol).densityKgPerL);
    }
}

TEST_CASE("à la même vitesse, un gros moteur consomme plus qu'un petit") {
    Powertrain small = preset("city_14");
    Powertrain big = small;
    big.engine.displacementCc = 3000;
    big.engine.cylinders = 6;
    big.engine.boreMm = 86;
    CHECK(steadyLper100(big, 90.0) > steadyLper100(small, 90.0) * 1.15);
}

TEST_CASE("un rapport plus court consomme plus à la même vitesse") {
    const Powertrain p = preset("city_14");
    DrivingModel third = inGear(p, 3, 70.0);
    DrivingModel fifth = inGear(p, 5, 70.0);
    REQUIRE(third.gear() == 3);
    REQUIRE(fifth.gear() == 5);
    REQUIRE_FALSE(third.stalled());
    REQUIRE_FALSE(fifth.stalled());
    CHECK(third.speedKmh() == doctest::Approx(70.0).epsilon(0.03));
    CHECK(fifth.speedKmh() == doctest::Approx(70.0).epsilon(0.03));
    CHECK(third.rpm() > 1.4 * fifth.rpm());
    CHECK(third.consumption().fuelLph() > 1.1 * fifth.consumption().fuelLph());
}

TEST_CASE("l'embrayage qui patine brûle pour rien : du gaz, et une voiture qui n'avance presque pas") {
    const Powertrain p = preset("city_14");
    // En première à l'arrêt, le pied gauche à mi-chemin : le moteur tire, l'embrayage chauffe.
    DrivingModel slipping(p);
    slipping.setManual(true);
    slipping.requestShift(1);
    for (int i = 0; i < static_cast<int>(1.0 / kDt); ++i) slipping.stepPedal(kDt, 0.0, 0.0);
    slipping.resetTrip();
    ConsumptionModel model;
    model.configure(p);
    double hottest = 0.0;
    int compared = 0;
    for (int i = 0; i < static_cast<int>(2.0 / kDt); ++i) {
        slipping.setClutchPedal(0.9);
        slipping.stepPedal(kDt, 0.5, 0.0);
        hottest = std::max(hottest, slipping.clutchSlipKw());
        // Le débit suit le couple du moteur, pas ce que l'embrayage laisse passer : la chaleur du disque se paie aussi.
        const double rpm = slipping.rpm();
        if (slipping.clutchSlipKw() > 3.0 && !slipping.cut() && slipping.torqueNm() > 10.0) {
            const double friction = frictionTorqueNm(p, rpm);
            const double expected = model.fuelGps(rpm, slipping.torqueNm(), friction, fullLoadTorqueNm(p, rpm, 0.0) + friction);
            CHECK(slipping.consumption().fuelGps() == doctest::Approx(expected).epsilon(0.03));
            ++compared;
        }
    }
    CHECK(compared > 50);
    REQUIRE(hottest > 3.0);
    CHECK(slipping.speedKmh() < 25.0);
    // Tout ce que brûle le moteur compte, même ce qui ne va pas aux roues : bien plus que pour tenir cette vitesse.
    DrivingModel steady = inGear(p, 2, std::max(slipping.speedKmh(), 15.0));
    const double averageLph = slipping.consumption().fuelTripL() / (2.0 / 3600.0);
    CHECK(averageLph > 1.5 * steady.consumption().fuelLph());
}

TEST_CASE("les pneus qui patinent gaspillent : les roues tournent, la distance comptée est celle de la voiture") {
    Powertrain p = preset("hot_hatch_turbo");
    p.car.drive = Drive::Front;
    p.car.massKg = 800;
    DrivingModel m(p);
    double wheelMetres = 0.0;
    double slipSeen = 0.0;
    for (int i = 0; i < static_cast<int>(3.0 / kDt); ++i) {
        m.stepPedal(kDt, 1.0, 0.0);
        wheelMetres += m.wheelRpm() / 60.0 * 2.0 * 3.141592653589793 * wheelRadiusM(p.car) * kDt;
        slipSeen = std::max(slipSeen, m.wheelSlip());
    }
    REQUIRE(slipSeen > 2.0);
    CHECK(m.consumption().fuelTripL() > 0.0);
    CHECK(m.consumption().tripKm() * 1000.0 < 0.9 * wheelMetres);
}

TEST_CASE("le total de litres est bien l'intégrale du débit") {
    DrivingModel m(preset("four_cyl_sport"));
    double litres = 0.0;
    double km = 0.0;
    constexpr double dt = 0.001;
    // Par pas du modèle : le débit lu est celui qui vient d'être compté.
    for (int i = 0; i < 60000; ++i) {
        m.step(dt);
        litres += m.consumption().fuelLph() / 3600.0 * dt;
        km += m.speedKmh() / 3600.0 * dt;
    }
    CHECK(litres > 0.02);
    CHECK(m.consumption().fuelTripL() == doctest::Approx(litres).epsilon(1e-6));
    CHECK(m.consumption().tripKm() == doctest::Approx(km).epsilon(1e-6));
    CHECK(m.consumption().fuelAvgLper100km() == doctest::Approx(litres / km * 100.0).epsilon(1e-6));
    const double co2 = litres * fuelSpecOf(Fuel::Petrol).densityKgPerL * 1000.0 * fuelSpecOf(Fuel::Petrol).co2GPerG / km;
    CHECK(m.consumption().co2GPerKm() == doctest::Approx(co2).epsilon(1e-6));
}

TEST_CASE("le résultat ne dépend pas de la taille des blocs") {
    const Powertrain p = preset("hot_hatch_turbo");
    const auto run = [&p](int frames) {
        DrivingModel m(p);
        const double dt = static_cast<double>(frames) / 44100.0;
        const int steps = static_cast<int>(30.0 / dt);
        for (int i = 0; i < steps; ++i) m.stepPedal(dt, 0.6, 0.0);
        return m.consumption().fuelTripL();
    };
    const double small = run(64);
    const double middle = run(128);
    const double big = run(1024);
    // Le modèle avance par pas de 1 ms ; seul le régime de ralenti lit une horloge par bloc, et il ne joue pas ici.
    CHECK(small > 0.01);
    CHECK(middle == doctest::Approx(small).epsilon(0.002));
    CHECK(big == doctest::Approx(small).epsilon(0.002));
}

TEST_CASE("lire et remettre à zéro le compteur ne change ni la voiture ni le son") {
    CHECK(soundHash(false) != 0u);
    // Le hachage additionne zéro à chaque lecture : tout écart vient du son.
    CHECK(soundHash(true) == soundHash(false));

    const Powertrain p = preset("v8_cross_plane");
    DrivingModel a(p), b(p);
    for (int i = 0; i < 20000; ++i) {
        a.step(0.002);
        b.step(0.002);
        if (i % 500 == 0) b.resetTrip();
        REQUIRE(a.rpm() == b.rpm());
        REQUIRE(a.speedKmh() == b.speedKmh());
        REQUIRE(a.torqueNm() == b.torqueNm());
    }
}

TEST_CASE("le trajet repart de zéro à la demande et quand le moteur change, pas quand on ouvre les vitres") {
    Powertrain p = preset("city_14");
    DrivingModel m(p);
    holdSpeed(m, 50.0, 20.0);
    CHECK(m.consumption().fuelTripL() > 0.0);
    CHECK(m.consumption().tripKm() > 0.0);
    CHECK(m.consumption().oilTripMl() > 0.0);

    p.car.windowsOpen = true;
    m.setPowertrain(p);
    CHECK(m.consumption().fuelTripL() > 0.0);

    m.resetTrip();
    CHECK(m.consumption().fuelTripL() == 0.0);
    CHECK(m.consumption().tripKm() == 0.0);
    CHECK(m.consumption().oilTripMl() == 0.0);
    CHECK(m.consumption().fuelAvgLper100km() == 0.0);

    holdSpeed(m, 50.0, 5.0);
    CHECK(m.consumption().fuelTripL() > 0.0);
    p.engine.displacementCc = 1600;
    m.setPowertrain(p);
    CHECK(m.consumption().fuelTripL() == 0.0);
    CHECK(m.consumption().tripKm() == 0.0);
}

TEST_CASE("à l'arrêt, des L/100 km ne veulent rien dire : la lecture vaut zéro, pas l'infini") {
    DrivingModel m(referencePowertrain());
    for (int i = 0; i < static_cast<int>(3.0 / kDt); ++i) m.stepNeutral(kDt, 0.0);
    CHECK(m.consumption().fuelLph() > 0.0);
    CHECK(m.consumption().fuelLper100km() == 0.0);
    CHECK(m.consumption().fuelAvgLper100km() == 0.0);
    CHECK(m.consumption().co2GPerKm() == 0.0);
}

TEST_CASE("la seconde méthode confirme la première : l'air admis correspond à ce qui brûle") {
    // Par le débit d'air, une essence doit tourner près de la richesse 1, un diesel bien plus pauvre.
    DrivingModel petrol(preset("four_cyl_sport"));
    holdSpeed(petrol, 120.0, 60.0);
    petrol.resetTrip();
    holdSpeed(petrol, 120.0, 20.0);
    CHECK(petrol.consumption().lambdaAverage() > 0.85);
    CHECK(petrol.consumption().lambdaAverage() < 1.25);

    DrivingModel laps(preset("city_14"));
    for (int i = 0; i < static_cast<int>(120.0 / kDt); ++i) laps.step(kDt);
    CHECK(laps.consumption().lambdaAverage() > 0.85);
    CHECK(laps.consumption().lambdaAverage() < 1.25);

    DrivingModel diesel(preset("diesel_four"));
    holdSpeed(diesel, 120.0, 60.0);
    diesel.resetTrip();
    holdSpeed(diesel, 120.0, 20.0);
    CHECK(diesel.consumption().lambdaAverage() > 1.3);
}

TEST_CASE("l'huile : un ordre de grandeur, qui suit le régime et la charge, et plus sur un turbo") {
    DrivingModel m(preset("city_14"));
    holdSpeed(m, 90.0, 60.0);
    m.resetTrip();
    holdSpeed(m, 90.0, 30.0);
    // De 0,05 % du carburant, soit 40 mL/1000 km, à la limite de 0,4 L/1000 km (MS Motorservice).
    CHECK(m.consumption().oilMlPer1000km() > 20.0);
    CHECK(m.consumption().oilMlPer1000km() < 400.0);

    ConsumptionModel natural, turbo;
    Powertrain withTurbo = preset("city_14");
    natural.configure(withTurbo);
    withTurbo.engine.aspiration = Aspiration::Turbo;
    turbo.configure(withTurbo);
    // Pied levé aussi : la projection sur la chemise suit le régime, sans charge.
    CHECK(natural.oilGps(4000.0, 0.0) > 1.5 * natural.oilGps(2000.0, 0.0));
    CHECK(natural.oilGps(3000.0, 80.0) > natural.oilGps(3000.0, 20.0));
    CHECK(turbo.oilGps(3000.0, 50.0) > natural.oilGps(3000.0, 50.0));
    CHECK(natural.oilGps(0.0, 0.0) == 0.0);

    // En g/kWh, à une charge d'autoroute : dans la fourchette publiée.
    const double kw = 60.0 * 3500.0 * 2.0 * 3.141592653589793 / 60.0 / 1000.0;
    const double gPerKwh = natural.oilGps(3500.0, 60.0) * 3600.0 / kw;
    CHECK(gPerKwh > 0.1);
    CHECK(gPerKwh < 0.5);
}

TEST_CASE("électrique : de l'énergie à la batterie, ni essence ni huile, et la récupération rend une part") {
    DrivingModel m(preset("electric"));
    holdSpeed(m, 90.0, 30.0);
    CHECK(m.consumption().fuelLph() == 0.0);
    CHECK(m.consumption().fuelTripL() == 0.0);
    CHECK(m.consumption().oilGph() == 0.0);
    CHECK(m.consumption().oilTripMl() == 0.0);
    CHECK(m.consumption().co2GPerKm() == 0.0);
    CHECK(m.consumption().energyKwhPer100km() > 5.0);
    CHECK(m.consumption().energyKwhPer100km() < 40.0);
    const double used = m.consumption().energyTripKwh();
    CHECK(used > 0.0);
    // Pied levé : la machine freine et recharge.
    for (int i = 0; i < static_cast<int>(3.0 / kDt); ++i) m.stepPedal(kDt, 0.0, 0.0);
    CHECK(m.consumption().energyKwhPer100km() < 0.0);
    CHECK(m.consumption().energyTripKwh() < used);
}

TEST_CASE("le compteur n'alloue rien") {
    DrivingModel m(preset("hot_hatch_turbo"));
    const long before = g_allocations.load();
    for (int i = 0; i < 5000; ++i) m.stepPedal(0.001, 0.8, 0.0);
    CHECK(g_allocations.load() == before);
}

}
