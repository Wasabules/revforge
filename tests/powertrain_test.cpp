#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

double hpOf(const Powertrain& p) { return powerSummary(p).peakPowerKw * kKwToHp; }

Powertrain withEngine(int cc, int cylinders = 4) {
    Powertrain p = referencePowertrain();
    p.engine.displacementCc = cc;
    p.engine.cylinders = cylinders;
    return p;
}

}  // namespace

// Rien n'est saisi à la main : ces chiffres sortent de la cylindrée, du régime et de l'alimentation.
TEST_CASE("les préréglages ont la puissance de vrais moteurs comparables") {
    const std::pair<const char*, double> known[] = {
        {"city_14", 75}, {"hot_hatch_na", 200}, {"hot_hatch_turbo", 300}, {"inline_six", 343},
        {"v8_cross_plane", 400}, {"v10_open", 525}, {"v12", 690}, {"diesel_four", 150},
    };
    for (const auto& [id, hp] : known) {
        INFO(id);
        const double computed = hpOf(findPreset(id)->powertrain());
        CHECK(computed > hp * 0.7);
        CHECK(computed < hp * 1.3);
    }
}

TEST_CASE("la puissance est bien le couple multiplié par le régime") {
    const Powertrain p = findPreset("v8_cross_plane")->powertrain();
    const PowerSummary s = powerSummary(p);
    const double kw = torqueCurveNm(p, s.peakPowerRpm) * s.peakPowerRpm * 2.0 * 3.141592653589793 / 60.0 / 1000.0;
    CHECK(s.peakPowerKw == doctest::Approx(kw).epsilon(0.001));
    CHECK(s.peakTorqueRpm < s.peakPowerRpm);
}

TEST_CASE("plus de cylindrée, plus de couple ; un turbo en donne encore plus") {
    const Powertrain small = withEngine(1500);
    const Powertrain big = withEngine(3000);
    CHECK(powerSummary(big).peakTorqueNm > powerSummary(small).peakTorqueNm * 1.8);
    Powertrain turbo = small;
    turbo.engine.aspiration = Aspiration::Turbo;
    CHECK(powerSummary(turbo).peakTorqueNm > powerSummary(small).peakTorqueNm * 1.5);
    // Le couple d'un turbo vient plus tôt.
    CHECK(powerSummary(turbo).peakTorqueRpm < powerSummary(small).peakTorqueRpm);
}

TEST_CASE("une ligne libre gagne en haut et perd en bas") {
    Powertrain stock = referencePowertrain();
    Powertrain free = stock;
    free.engine.exhaust = Exhaust::Straight;
    CHECK(torqueCurveNm(free, 5500.0) > torqueCurveNm(stock, 5500.0));
    Powertrain sport = stock;
    sport.engine.exhaust = Exhaust::Sport;
    CHECK(torqueCurveNm(free, 1500.0) < torqueCurveNm(sport, 1500.0));
}

TEST_CASE("la pression du turbo donne le couple de vrais moteurs") {
    Powertrain p = referencePowertrain();
    p.engine.aspiration = Aspiration::Turbo;
    p.engine.displacementCc = 1991;
    p.engine.boreMm = 83;
    p.engine.redlineRpm = 7200;
    p.engine.exhaust = Exhaust::Sport;
    // Un 2.0 de série à 1,3 bar donne environ 400 Nm ; le même à 2,1 bar, 500 Nm (AMG A 45 S).
    p.engine.boostBar = 1.3f;
    const PowerSummary road = powerSummary(p);
    CHECK(road.peakTorqueNm > 400.0 * 0.8);
    CHECK(road.peakTorqueNm < 400.0 * 1.2);
    p.engine.boostBar = 2.1f;
    const PowerSummary tuned = powerSummary(p);
    CHECK(tuned.peakTorqueNm > 500.0 * 0.8);
    CHECK(tuned.peakTorqueNm < 500.0 * 1.2);
    CHECK(tuned.peakPowerKw * kKwToHp > 421.0 * 0.75);
    CHECK(tuned.peakPowerKw > road.peakPowerKw * 1.2);
}

TEST_CASE("plus de pression demande un plus gros turbo, qui se réveille plus tard") {
    Powertrain small = referencePowertrain();
    small.engine.aspiration = Aspiration::Turbo;
    small.engine.boostBar = 0.8f;
    Powertrain big = small;
    big.engine.boostBar = 3.0f;
    // Sous le seuil de lancement : au-dessus, les deux sont à leur pression, chacun la sienne.
    CHECK(steadyBoost(big, 1500.0) < steadyBoost(small, 1500.0));
    const auto spool = [](const Powertrain& p) {
        DrivingModel model(p);
        double t = 0.0;
        while (model.boost() < 0.8 && t < 10.0) {
            model.stepNeutral(0.01, 1.0);
            t += 0.01;
        }
        return t;
    };
    CHECK(spool(big) > spool(small));
}

TEST_CASE("le rupteur rebondit une dizaine de fois par seconde, sans fermer le papillon") {
    for (const char* id : {"city_14", "v8_cross_plane", "v12"}) {
        INFO(id);
        DrivingModel model(findPreset(id)->powertrain());
        for (int i = 0; i < 300; ++i) model.stepNeutral(0.01, 1.0);
        int cuts = 0;
        bool was = model.cut();
        double low = 1e9, high = 0.0;
        constexpr int kSteps = 2000;
        for (int i = 0; i < kSteps; ++i) {
            model.stepNeutral(0.001, 1.0);
            if (model.cut() && !was) ++cuts;
            was = model.cut();
            low = std::min(low, model.rpm());
            high = std::max(high, model.rpm());
            REQUIRE(model.load() > 0.9);
        }
        const double perSecond = cuts / (kSteps * 0.001);
        MESSAGE(id << " : " << perSecond << " coupures/s, de " << low << " à " << high << " tr/min");
        CHECK(perSecond > 5.0);
        CHECK(perSecond < 22.0);
        CHECK(high - low > model.redlineRpm() * 0.03);
        CHECK(high <= model.redlineRpm() * 1.01);
    }
}

TEST_CASE("le turbo n'arrive pas tout de suite") {
    Powertrain p = referencePowertrain();
    p.engine.aspiration = Aspiration::Turbo;
    DrivingModel model(p);
    model.stepNeutral(0.1, 1.0);
    const double early = model.boost();
    for (int i = 0; i < 10; ++i) model.stepNeutral(0.1, 1.0);
    CHECK(early < 0.3);
    CHECK(model.boost() > early);
}

TEST_CASE("un gros moteur prend ses tours moins vite qu'un petit") {
    const auto timeTo = [](const Powertrain& p, double rpm) {
        DrivingModel model(p);
        double t = 0.0;
        while (model.rpm() < rpm && t < 5.0) {
            model.stepNeutral(0.005, 1.0);
            t += 0.005;
        }
        return t;
    };
    Powertrain small = referencePowertrain();
    small.engine.redlineRpm = 6000;
    Powertrain big = small;
    big.engine.displacementCc = 7000;
    big.engine.cylinders = 8;
    big.engine.boreMm = 99;
    CHECK(timeTo(big, 5000.0) > 0.45);
    CHECK(engineInertia(big) > engineInertia(small) * 2.0);
}

TEST_CASE("la masse et la puissance décident du 0 à 100") {
    Powertrain light = findPreset("v8_cross_plane")->powertrain();
    Powertrain heavy = light;
    heavy.car.massKg = 2600;
    Powertrain small = light;
    small.engine.displacementCc = 2500;
    const double reference = measurePerformance(light).zeroTo100S;
    CHECK(reference > 3.0);
    CHECK(reference < 10.0);
    CHECK(measurePerformance(heavy).zeroTo100S > reference * 1.2);
    CHECK(measurePerformance(small).zeroTo100S > reference);
}

TEST_CASE("le résultat ne dépend pas du découpage du temps") {
    DrivingModel a, b;
    for (int i = 0; i < 4000; ++i) a.stepPedal(0.0025, 0.7, 0.0);
    for (int i = 0; i < 200; ++i) b.stepPedal(0.05, 0.7, 0.0);
    CHECK(a.speedKmh() == doctest::Approx(b.speedKmh()).epsilon(0.001));
    CHECK(a.rpm() == doctest::Approx(b.rpm()).epsilon(0.002));
}

TEST_CASE("boîte manuelle : rien ne monte tout seul, et un rétrogradage dangereux est refusé") {
    DrivingModel model;
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 20 * 20; ++i) {
        model.stepPedal(0.05, 1.0, 0.0);
        REQUIRE(model.rpm() <= 6300.0);
    }
    CHECK(model.gear() == 1);
    model.requestShift(1);
    for (int i = 0; i < 30 * 20; ++i) model.stepPedal(0.05, 1.0, 0.0);
    CHECK(model.gear() == 2);
    CHECK(model.speedKmh() > 60.0);
    model.requestShift(-1);
    for (int i = 0; i < 20; ++i) model.stepPedal(0.05, 1.0, 0.0);
    // En deuxième à fond, la première ferait exploser le moteur.
    CHECK(model.gear() == 2);
}

TEST_CASE("embrayage enfoncé, le moteur s'emballe sans faire avancer la voiture") {
    DrivingModel model;
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 20; ++i) model.stepPedal(0.05, 0.0, 0.0);
    model.setClutchPedal(1.0);
    for (int i = 0; i < 40; ++i) model.stepPedal(0.05, 1.0, 0.0);
    CHECK(model.rpm() > 4000.0);
    CHECK(model.speedKmh() < 1.0);
}

TEST_CASE("le démarreur lance le moteur, qui prend puis se stabilise au ralenti") {
    DrivingModel model;
    model.crank();
    model.stepNeutral(0.3, 0.0);
    CHECK(model.cranking());
    CHECK(model.rpm() < 400.0);
    double top = 0.0;
    for (int i = 0; i < 40; ++i) {
        model.stepNeutral(0.05, 0.0);
        top = std::max(top, model.rpm());
    }
    CHECK_FALSE(model.cranking());
    CHECK(top > model.idleRpm() * 1.2);
    for (int i = 0; i < 80; ++i) model.stepNeutral(0.05, 0.0);
    CHECK(model.rpm() == doctest::Approx(model.idleRpm()).epsilon(0.06));
}

TEST_CASE("contact coupé, le moteur finit sa course puis s'arrête") {
    DrivingModel model;
    for (int i = 0; i < 20; ++i) model.stepNeutral(0.05, 1.0);
    const double before = model.rpm();
    model.stepOff(0.05);
    CHECK(model.rpm() > before * 0.5);
    for (int i = 0; i < 100; ++i) model.stepOff(0.05);
    CHECK(model.stopped());
}

TEST_CASE("trop de couple sur deux roues : elles patinent, quatre roues motrices tiennent mieux") {
    Powertrain front = findPreset("v12")->powertrain();
    front.car.drive = Drive::Front;
    Powertrain all = front;
    all.car.drive = Drive::All;
    const auto worstSlip = [](const Powertrain& p) {
        DrivingModel model(p);
        double worst = 0.0;
        for (int i = 0; i < 4 * 100; ++i) {
            model.stepPedal(0.01, 1.0, 0.0);
            worst = std::max(worst, model.wheelSlip());
        }
        return worst;
    };
    CHECK(worstSlip(front) > 3.0);
    CHECK(worstSlip(all) < worstSlip(front));
    CHECK(measurePerformance(all).zeroTo100S < measurePerformance(front).zeroTo100S);
}

namespace {

/** Lancée en manuelle, jusqu'au sixième rapport d'une compacte de 157 ch. */
DrivingModel inTopGear() {
    DrivingModel model(findPreset("four_cyl_sport")->powertrain());
    model.setManual(true);
    for (int gear = 1; gear <= 6; ++gear) {
        model.requestShift(1);
        for (int i = 0; i < (gear == 1 ? 8 : 5) * 100; ++i) model.stepPedal(0.01, 1.0, 0.0);
    }
    return model;
}

}  // namespace

TEST_CASE("en manuelle, trop bas en sixième, le moteur cale") {
    DrivingModel model = inTopGear();
    REQUIRE(model.gear() == 6);
    REQUIRE(model.speedKmh() > 80.0);
    REQUIRE_FALSE(model.stalled());
    bool lugged = false;
    for (int i = 0; i < 40 * 100 && !model.stalled(); ++i) {
        model.stepPedal(0.01, 0.0, 0.35);
        lugged = lugged || model.lugging();
    }
    CHECK(lugged);
    CHECK(model.stalled());
    for (int i = 0; i < 5 * 100; ++i) model.stepPedal(0.01, 0.0, 0.3);
    CHECK(model.rpm() < 100.0);
    CHECK(model.speedKmh() < 5.0);
}

TEST_CASE("repartir en sixième à l'arrêt : le moteur cale") {
    DrivingModel model(findPreset("four_cyl_sport")->powertrain());
    model.setManual(true);
    for (int gear = 1; gear <= 6; ++gear) {
        model.requestShift(1);
        for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.0, 0.0);
    }
    REQUIRE(model.gear() == 6);
    for (int i = 0; i < 400; ++i) model.stepPedal(0.01, 1.0, 0.0);
    CHECK(model.stalled());
    CHECK(model.speedKmh() < 20.0);
}

TEST_CASE("embrayage enfoncé, on s'arrête en sixième sans caler") {
    DrivingModel model = inTopGear();
    model.setClutchPedal(1.0);
    for (int i = 0; i < 30 * 100; ++i) model.stepPedal(0.01, 0.0, 0.6);
    CHECK_FALSE(model.stalled());
    CHECK(model.speedKmh() < 1.0);
    CHECK(model.rpm() > model.idleRpm() * 0.8);
}

TEST_CASE("calé, le démarreur relance le moteur") {
    DrivingModel model = inTopGear();
    for (int i = 0; i < 40 * 100 && !model.stalled(); ++i) model.stepPedal(0.01, 0.0, 0.5);
    REQUIRE(model.stalled());
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.0, 0.5);
    model.crank();
    CHECK_FALSE(model.stalled());
    for (int i = 0; i < 400; ++i) model.stepNeutral(0.01, 0.0);
    CHECK(model.rpm() > model.idleRpm() * 0.9);
}

TEST_CASE("une boîte automatique ne cale jamais") {
    DrivingModel model(findPreset("four_cyl_sport")->powertrain());
    for (int i = 0; i < 25 * 100; ++i) model.stepPedal(0.01, 1.0, 0.0);
    for (int i = 0; i < 30 * 100; ++i) {
        model.stepPedal(0.01, 0.0, 0.5);
        REQUIRE_FALSE(model.stalled());
    }
    CHECK(model.rpm() > model.idleRpm() * 0.8);
}

TEST_CASE("sous le ralenti, le couple s'effondre") {
    const Powertrain p = findPreset("four_cyl_sport")->powertrain();
    const double idle = idleRpmOf(p);
    CHECK(fullLoadTorqueNm(p, idle * 0.5, 0.0) < fullLoadTorqueNm(p, idle, 0.0) * 0.4);
}

TEST_CASE("le frein moteur ralentit la voiture pied levé") {
    DrivingModel coast, free;
    for (int i = 0; i < 15 * 20; ++i) {
        coast.stepPedal(0.05, 1.0, 0.0);
        free.stepPedal(0.05, 1.0, 0.0);
    }
    const double start = coast.speedKmh();
    for (int i = 0; i < 3 * 20; ++i) coast.stepPedal(0.05, 0.0, 0.0);
    CHECK(coast.speedKmh() < start);
    CHECK(coast.torqueNm() < 0.0);
}

TEST_CASE("électrique : un seul rapport, tout le couple dès l'arrêt") {
    const Powertrain p = findPreset("electric")->powertrain();
    CHECK(gearingOf(p).gears == 1);
    CHECK(fullLoadTorqueNm(p, 500.0, 0.0) > 300.0);
    DrivingModel model(p);
    for (int i = 0; i < 10 * 20; ++i) model.stepPedal(0.05, 1.0, 0.0);
    CHECK(model.gear() == 1);
    CHECK(model.speedKmh() > 90.0);
}

TEST_CASE("la jante change le rayon de roulement, donc la démultiplication") {
    Powertrain p = referencePowertrain();
    const double small = wheelRadiusM(p.car);
    p.car.rimInch = 20;
    CHECK(wheelRadiusM(p.car) > small);
}

TEST_CASE("tous les préréglages roulent sans erreur") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        INFO(all[i].id);
        DrivingModel model(all[i].powertrain());
        double top = 0.0;
        for (int step = 0; step < 60 * 20; ++step) {
            model.step(0.05);
            REQUIRE(std::isfinite(model.rpm()));
            REQUIRE(std::isfinite(model.speedKmh()));
            top = std::max(top, model.speedKmh());
        }
        CHECK(top > 80.0);
        CHECK(model.rpm() <= model.redlineRpm() + 100.0);
    }
}

TEST_CASE("à cylindrée constante, la course ne pèse sur le couple qu'au-delà de 24 m/s") {
    Powertrain square = referencePowertrain();
    square.engine.displacementCc = 2000;
    square.engine.boreMm = 86;
    square.engine.redlineRpm = 6000;
    Powertrain shortStroke = square;
    shortStroke.engine.boreMm = 96;
    Powertrain longStroke = square;
    longStroke.engine.boreMm = 76;
    longStroke.engine.redlineRpm = 8500;
    // Sous 24 m/s, le carré et le super-carré respirent pareil : le modèle n'a pas d'autre effet de la course.
    for (double rpm = 1000.0; rpm <= 6000.0; rpm += 500.0) {
        CHECK(torqueCurveNm(shortStroke, rpm) == doctest::Approx(torqueCurveNm(square, rpm)));
    }
    // 110 mm à 7000 tr/min : 25,7 m/s. Une course plus courte, même alésage de régime, passe sans perte.
    Powertrain sameRedline = longStroke;
    sameRedline.engine.boreMm = 96;
    CHECK(longStroke.engine.strokeMm() > 110.0f);
    CHECK(torqueCurveNm(longStroke, 7000.0) < torqueCurveNm(sameRedline, 7000.0) * 0.98);
    // La fiche le voit : le pic de puissance recule et baisse.
    CHECK(powerSummary(longStroke).peakPowerKw < powerSummary(sameRedline).peakPowerKw * 0.97);
    CHECK(powerSummary(longStroke).peakPowerRpm < powerSummary(sameRedline).peakPowerRpm);
}
