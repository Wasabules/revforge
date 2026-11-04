#include <algorithm>
#include <cmath>

#include "doctest.h"
#include "portable_math.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/ecu.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/tyre.hpp"

using namespace revforge;

namespace {

/** Le V8 de 7 L du calculateur : une propulsion qui patine à fond en première. */
Powertrain bigV8(TractionControl traction = TractionControl::Off) {
    Powertrain p;
    p.engine.cylinders = 8;
    p.engine.layout = Layout::CrossPlane;
    p.engine.displacementCc = 7000;
    p.engine.boreMm = 99;
    p.engine.redlineRpm = 6000;
    p.engine.exhaust = Exhaust::Sport;
    p.engine.tractionControl = traction;
    p.car = CarConfig{Body::Coupe, 1700, 19, Drive::Rear, EnginePlacement::Natural, Insulation::Standard, false};
    return p;
}

}  // namespace

TEST_CASE("pneu : l'arc tangente portable suit celle de la bibliothèque") {
    double worst = 0.0;
    for (int i = -4000; i <= 4000; ++i) {
        const double x = i * 0.0137 * (1.0 + 0.5 * (i % 5));
        worst = std::max(worst, std::fabs(portable::atan(x) - std::atan(x)));
    }
    CHECK(worst < 1e-14);
}

TEST_CASE("pneu : la force culmine vers 11 % de glissement, puis retombe") {
    const double peak = tyrePeakSlip();
    // Le sommet entre 8 et 12 % de glissement, et c'est bien un sommet.
    CHECK(peak > 0.08);
    CHECK(peak < 0.12);
    CHECK(tyreGripShare(peak) == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(tyreGripShare(0.5 * peak) < 0.95);
    CHECK(tyreGripShare(2.0 * peak) < 0.97);
    CHECK(std::fabs(tyreGripSlope(peak)) < 1e-6);
    // Roue bloquée ou patinant franchement : 0,65/0,85 du sommet (Bosch, béton sec), à 5 % près.
    CHECK(tyreGripShare(1.0) == doctest::Approx(0.65 / 0.85).epsilon(0.05));
    // La raideur de glissement à l'origine : K = 21,5 fois la charge, rapportée au sommet μ·N.
    CHECK(tyreGripSlope(0.0) * kTyreGrip == doctest::Approx(kTyreSlipStiffness).epsilon(1e-9));
    // Impaire : au frein moteur, la même courbe dans l'autre sens.
    CHECK(tyreGripShare(-0.2) == doctest::Approx(-tyreGripShare(0.2)));
    // La montée est monotone : la cible de l'antipatinage s'y lit sans ambiguïté.
    for (double s = 0.1; s < 1.0; s += 0.1) CHECK(tyreGripShare(tyreSlipForShare(s)) == doctest::Approx(s).epsilon(1e-9));
    // À l'arrêt, le glissement se rapporte à 1 m/s : une roue qui tourne à 0,5 m/s sous une voiture arrêtée glisse de 50 %.
    CHECK(tyreSlipRatio(0.5, 0.0) == doctest::Approx(0.5));
    CHECK(tyreSlipRatio(22.0, 20.0) == doctest::Approx(0.1));
}

TEST_CASE("pneu : la roue suit sa raideur sans osciller d'un pas à l'autre") {
    // À basse vitesse, la raideur du pneu se compte en dizaines de kN par m/s : intégrée telle quelle, la roue battrait d'un
    // pas au suivant. On compte les changements de sens du glissement, pas à pas, sur un départ à fond puis un freinage.
    DrivingModel model(bigV8());
    int flips = 0, steps = 0;
    double previous = 0.0, previousDelta = 0.0;
    for (int i = 0; i < 6000; ++i) {
        model.stepPedal(0.001, i < 4000 ? 1.0 : 0.0, i < 4000 ? 0.0 : 0.3);
        const double now = model.wheelSlip();
        const double delta = now - previous;
        if (i > 1 && std::fabs(delta) > 1e-6 && std::fabs(previousDelta) > 1e-6 && (delta > 0.0) != (previousDelta > 0.0)) ++flips;
        if (std::fabs(delta) > 1e-6) previousDelta = delta;
        previous = now;
        ++steps;
    }
    MESSAGE("changements de sens du glissement : " << flips << " sur " << steps << " pas");
    // Les à-coups de la transmission (2 à 3 Hz) et le rupteur en donnent quelques dizaines ; un pas qui bat, des milliers.
    CHECK(flips < 300);
    CHECK(model.speedKmh() > 20.0);
}

TEST_CASE("pneu : la force que reçoit la caisse est celle que reçoit la roue") {
    DrivingModel model(bigV8());
    for (int i = 0; i < 3000; ++i) model.stepPedal(0.001, 1.0, 0.0);
    // Pied au plancher en première, la propulsion patine au-delà du sommet : la force retombe sous μ·N.
    const double slip = model.wheelSlipRatio();
    CHECK(slip > tyrePeakSlip());
    const double force = model.tyreForceN();
    CHECK(force > 0.0);
    CHECK(force < kTyreGrip * 1700.0 * 9.81);
}

TEST_CASE("pneu : en côte, une propulsion qui patine ne monte pas la deuxième à l'allure d'un piéton") {
    // Le départ en côte du trajet `hill_start` : 12 %, pied à 55 %, mode éco. La propulsion de 3,2 L patine au-delà du sommet ;
    // lue au régime, la deuxième entrait à 4 km/h, et le moteur calait.
    DrivingModel model(findPreset("inline_six")->powertrain());
    model.setShiftMode(ShiftMode::Eco);
    model.setRoadGrade(12.0);
    double top = 0.0, slipTop = 0.0;
    bool stalled = false;
    for (int i = 0; i < 10000; ++i) {
        model.stepPedal(0.001, 0.55, 0.0);
        top = std::max(top, model.speedKmh());
        slipTop = std::max(slipTop, model.wheelSlipRatio());
        stalled = stalled || model.stalled();
        // Tant que les roues patinent au-delà du sommet, la boîte ne monte pas, sauf au régime maximal.
        if (model.wheelSlipRatio() > 2.0 * tyrePeakSlip() && model.speedKmh() < 10.0) CHECK(model.gear() <= 1);
    }
    MESSAGE("départ en côte : " << top << " km/h au plus, glissement jusqu'à " << slipTop);
    CHECK(slipTop > tyrePeakSlip());
    CHECK_FALSE(stalled);
    CHECK(top > 25.0);
}

TEST_CASE("pneu : l'antipatinage tient le glissement près du sommet, et fait gagner du temps") {
    const auto meanSlip = [](const Powertrain& p) {
        DrivingModel model(p);
        double sum = 0.0;
        int n = 0;
        // Sur les premiers rapports, une fois la voiture lancée et l'embrayage serré.
        for (int i = 0; i < 6000; ++i) {
            model.stepPedal(0.001, 1.0, 0.0);
            if (model.gear() >= 1 && model.meshed() && !model.shifting() && model.speedKmh() > 5.0 && model.clutch() > 0.95) {
                sum += model.wheelSlipRatio();
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };
    const Powertrain off = bigV8(), sport = bigV8(TractionControl::Sport), full = bigV8(TractionControl::Full);
    // Sans antipatinage, la propulsion patine bien au-delà du sommet.
    CHECK(meanSlip(off) > 2.0 * tyrePeakSlip());
    // Le sport vise le sommet ; le complet, 90 % de la force, plus tôt sur la courbe.
    CHECK(tractionSlipTarget(TractionControl::Sport) == doctest::Approx(tyrePeakSlip()));
    CHECK(tractionSlipTarget(TractionControl::Full) < tractionSlipTarget(TractionControl::Sport));
    CHECK(meanSlip(sport) == doctest::Approx(tractionSlipTarget(TractionControl::Sport)).epsilon(0.25));
    CHECK(meanSlip(full) == doctest::Approx(tractionSlipTarget(TractionControl::Full)).epsilon(0.25));
    // Au-delà du sommet, le pneu perd de la force : tenir le glissement fait gagner du temps, le sport plus que le complet.
    const double tOff = measurePerformance(off).zeroTo100S, tSport = measurePerformance(sport).zeroTo100S, tFull = measurePerformance(full).zeroTo100S;
    MESSAGE("0-100 km/h, V8 7 L propulsion : " << tOff << " s sans, " << tSport << " s sport, " << tFull << " s complet");
    CHECK(tSport < tOff - 0.5);
    CHECK(tFull < tOff);
    CHECK(tSport < tFull);
}
