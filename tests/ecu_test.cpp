#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/ecu.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

namespace {

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

/** Le moteur et son son, pas à pas, comme le rendu : `drive` fait avancer le modèle d'une tranche. */
struct Run {
    DrivingModel model;
    EngineSound sound{kRate};
    Samples out;

    explicit Run(const Powertrain& p) : model(p) {
        sound.setProfile(p.profile());
        sound.setVolume(0.7f);
    }

    template <typename Drive>
    void play(double seconds, Drive drive) {
        constexpr int kTick = 128;
        const double dt = static_cast<double>(kTick) / kRate;
        for (double t = 0.0; t < seconds; t += dt) {
            drive(dt);
            sound.setTargetRpm(static_cast<float>(model.rpm()));
            sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
            sound.setTurbo(model.turboDrive());
            sound.setEcu(model.ecuDrive());
            const std::size_t at = out.size();
            out.resize(at + kTick);
            sound.render(out.data() + at, kTick);
        }
    }
};

/** Au point mort, pied au plancher : le moteur va au rupteur et y reste. */
void floorInNeutral(Run& run, double seconds) {
    run.play(seconds, [&](double dt) { run.model.stepNeutral(dt, 1.0); });
}

}  // namespace

// ── La carte : avance et richesse ──

TEST_CASE("calculateur : le couple selon l'avance suit la corrélation d'Ayala et Heywood, et ne devient jamais négatif") {
    CHECK(sparkTorqueShare(0.0) == 1.0);
    // 1 − 0,168·(√(1 + 0,004443·Δ²) − 1) : 0,9 % à 5°, 3,4 % à 10°, 11 % à 20° ; symétrique du côté avancé.
    CHECK(sparkTorqueShare(5.0) == doctest::Approx(0.9909).epsilon(0.0005));
    CHECK(sparkTorqueShare(10.0) == doctest::Approx(0.9661).epsilon(0.0005));
    CHECK(sparkTorqueShare(20.0) == doctest::Approx(0.8880).epsilon(0.0005));
    CHECK(sparkTorqueShare(-10.0) == sparkTorqueShare(10.0));
    CHECK(sparkTorqueShare(200.0) == 0.0);
    // L'antipatinage la retourne : le retard qui laisse 90 % du couple redonne 90 %.
    double retard = 0.0, fired = 1.0;
    tractionActuation(0.9, kTractionMaxRetardDeg, retard, fired);
    CHECK(fired == 1.0);
    CHECK(sparkTorqueShare(retard) == doctest::Approx(0.9).epsilon(1e-9));
    // Le retard chauffe le gaz, l'avance non : le travail perdu y part dans les parois.
    CHECK(sparkExhaustGain(20.0) > sparkExhaustGain(10.0));
    CHECK(sparkExhaustGain(-10.0) == 0.0);
    // Le bilan de l'anti-lag : le rendement du cylindre, ce que l'étincelle perd, moins 20 % dans les parois.
    CHECK(sparkExhaustGain(55.0) == doctest::Approx(0.375 * (1.0 - sparkTorqueShare(55.0)) * 0.8));
}

TEST_CASE("calculateur : l'avance du meilleur couple croît avec le régime et à faible charge") {
    // Ordres de grandeur de cartes publiées : 25 à 35° à pleine charge, plus à mi-charge.
    CHECK(ecuMbtAdvanceDeg(3000.0, 1.0) == doctest::Approx(26.0).epsilon(0.02));
    CHECK(ecuMbtAdvanceDeg(6000.0, 1.0) > ecuMbtAdvanceDeg(3000.0, 1.0));
    CHECK(ecuMbtAdvanceDeg(3000.0, 0.3) > ecuMbtAdvanceDeg(3000.0, 1.0));
    const EngineConfig e = preset("hot_hatch_na").engine;
    EngineConfig advanced = e;
    advanced.ignitionOffsetDeg = 5;
    CHECK(demandedAdvanceDeg(advanced, 4000.0, 1.0) == doctest::Approx(demandedAdvanceDeg(e, 4000.0, 1.0) + 5.0));
    // Le cliquetis lu de la combustion retire de l'avance.
    CHECK(demandedAdvanceDeg(e, 4000.0, 1.0, 4.0) == doctest::Approx(demandedAdvanceDeg(e, 4000.0, 1.0) - 4.0));
}

TEST_CASE("calculateur : la puissance culmine vers λ 0,85 à 0,9, et le gaz y est plus froid qu'à λ 1") {
    double best = 0.0, bestLambda = 0.0;
    for (double lambda = 0.70; lambda <= 1.101; lambda += 0.01) {
        if (lambdaTorqueShare(lambda) > best) {
            best = lambdaTorqueShare(lambda);
            bestLambda = lambda;
        }
    }
    CHECK(bestLambda >= 0.85);
    CHECK(bestLambda <= 0.90);
    CHECK(best > 1.03);
    CHECK(lambdaTorqueShare(1.1) < 0.95);
    // Le gaz brûlé est le plus chaud un peu au-delà de la stœchiométrie, et l'enrichissement le refroidit.
    CHECK(lambdaHeatShare(0.95) > lambdaHeatShare(1.0));
    CHECK(lambdaHeatShare(0.85) < lambdaHeatShare(1.0));
    CHECK(lambdaHeatShare(0.70) < lambdaHeatShare(0.85));
    CHECK(lambdaHeatShare(1.1) < lambdaHeatShare(1.0));
}

TEST_CASE("calculateur : ce qu'il lit des autres sujets ne change rien à leur valeur de référence") {
    const EngineConfig e = preset("hot_hatch_na").engine;
    // Moteur chaud (90 °C) : pas d'enrichissement ; froid, le calculateur enrichit.
    CHECK(warmupRichness(90.0) == 1.0);
    CHECK(demandedLambda(e, 1.0, 90.0) == demandedLambda(e, 1.0));
    CHECK(demandedLambda(e, 0.3, 20.0) < 0.9);
    CHECK(warmupRichness(-20.0) > warmupRichness(20.0));
    // Sans affolement des soupapes connu, le rupteur reste au régime maximal ; s'il arrive avant, il coupe juste dessous.
    CHECK(limiterRpmFor(7300.0, 0.0) == 7300.0);
    CHECK(limiterRpmFor(7300.0, 8000.0) == 7300.0);
    CHECK(limiterRpmFor(7300.0, 7000.0) == doctest::Approx(6860.0));
    // Sans cliquetis lu, la carte reste au meilleur couple ; avec, elle recule et le couple baisse.
    CHECK(mapRetardDeg(e) == 0.0);
    CHECK(ecuFullLoadShare(e, 6.0) == doctest::Approx(sparkTorqueShare(6.0)));
}

TEST_CASE("calculateur : au défaut, la carte est celle d'origine, et rien ne s'en écarte") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const EngineConfig e = all[i].powertrain().engine;
        CHECK(!ecuMapTuned(e));
        CHECK(ecuFullLoadShare(e) == 1.0);
        CHECK(!ecuMapExhaust(e).active());
    }
    // Le λ d'origine écrit tel quel ne change rien non plus.
    EngineConfig e = preset("hot_hatch_na").engine;
    e.fullLoadLambda = 0.88f;
    CHECK(!ecuMapTuned(e));
    CHECK(fullLoadLambdaOf(e) == stockFullLoadLambda(e));
    // Un diesel n'a ni étincelle ni richesse à régler.
    EngineConfig diesel = preset("diesel_four").engine;
    diesel.ignitionOffsetDeg = -8;
    diesel.fullLoadLambda = 0.8f;
    CHECK(!ecuMapTuned(diesel));
}

TEST_CASE("calculateur : la carte réglée change la courbe de couple, la consommation suit l'air et non le couple") {
    const Powertrain stock = preset("hot_hatch_na");
    Powertrain lean = stock;
    lean.engine.fullLoadLambda = 1.0f;
    Powertrain retarded = stock;
    retarded.engine.ignitionOffsetDeg = -10;
    const double rpm = 4500.0;
    const double friction = frictionTorqueNm(stock, rpm);
    const double gross = torqueCurveNm(stock, rpm) + friction;
    // λ 1 au lieu de 0,88 : 6 % de couple indiqué en moins. Les frottements, eux, restent.
    CHECK((torqueCurveNm(lean, rpm) + friction) / gross == doctest::Approx(lambdaTorqueShare(1.0) / lambdaTorqueShare(0.88)).epsilon(1e-6));
    // 10° de retard passent par la carte du cliquetis (combustion). À 4500 tr/min, sa carte d'origine est au meilleur couple de
    // son cycle : ils coûtent ce que dit la corrélation du calculateur, à un demi-point près. Un seul modèle, pas deux sommets.
    const KnockMap* knock = knockMapOf(retarded);
    REQUIRE(knock != nullptr);
    CHECK(knock->at(rpm, 1.0).stockDeg == doctest::Approx(knock->at(rpm, 1.0).mbtDeg));
    CHECK((torqueCurveNm(retarded, rpm) + friction) / gross == doctest::Approx(knock->steadyTorqueShare(rpm)).epsilon(1e-6));
    CHECK((torqueCurveNm(retarded, rpm) + friction) / gross == doctest::Approx(sparkTorqueShare(10.0)).epsilon(0.005));
    // Même air, même carburant : l'étincelle retardée en tire moins, donc chaque kWh coûte plus.
    const double brake = 0.6 * torqueCurveNm(retarded, 3000.0);
    CHECK(brakeSpecificFuel(retarded, 3000.0, brake) > brakeSpecificFuel(stock, 3000.0, brake) * 1.015);
    // À la puissance maximale, la richesse coûte : λ 1 dépense moins par kWh que la carte d'origine, λ 0,75 bien plus.
    Powertrain rich = stock;
    rich.engine.fullLoadLambda = 0.75f;
    const double peak = powerSummary(stock).peakPowerRpm;
    CHECK(brakeSpecificFuel(lean, peak, torqueCurveNm(lean, peak)) < brakeSpecificFuel(stock, peak, torqueCurveNm(stock, peak)));
    CHECK(brakeSpecificFuel(rich, peak, torqueCurveNm(rich, peak)) > brakeSpecificFuel(stock, peak, torqueCurveNm(stock, peak)) * 1.1);
    // Le gaz : plus chaud pauvre ou retardé, plus froid riche.
    CHECK(fullLoadExhaustK(lean.engine, peak) > fullLoadExhaustK(stock.engine, peak));
    CHECK(fullLoadExhaustK(retarded.engine, peak) > fullLoadExhaustK(stock.engine, peak));
    CHECK(fullLoadExhaustK(rich.engine, peak) < fullLoadExhaustK(stock.engine, peak));
}

TEST_CASE("calculateur : sans réglage, il ne fait rien de tout un tour, et le son ne le voit pas") {
    DrivingModel model(preset("boxer_four_turbo"));
    bool acted = false;
    for (int i = 0; i < 4000; ++i) {
        model.step(0.01);
        const EcuAction& a = model.ecuAction();
        acted = acted || model.ecuDrive().active || a.retardDeg != 0.0 || a.firedShare != 1.0 || a.traction != 0.0 || a.launch || a.flat;
    }
    CHECK_FALSE(acted);
}

TEST_CASE("calculateur : le rupteur à coupure d'allumage laisse passer l'essence, qui détone dans la ligne") {
    Powertrain stock = preset("hot_hatch_na");
    Powertrain cut = stock;
    cut.engine.revLimiter = RevLimiter::IgnitionCut;
    Run a(stock), b(cut);
    floorInNeutral(a, 6.0);
    floorInNeutral(b, 6.0);
    // L'essence passe pendant la coupure : on en brûle bien plus au rupteur.
    CHECK(b.model.consumption().fuelTripL() > a.model.consumption().fuelTripL() * 1.3);
    // Le collecteur, chaud, l'enflamme : des détonations, bien plus que les quelques reprises du rupteur d'origine.
    CHECK(b.sound.pops() >= a.sound.pops() + 20u);
}

TEST_CASE("calculateur : le rupteur progressif tient le régime sans coupure franche") {
    Powertrain p = preset("hot_hatch_na");
    p.engine.revLimiter = RevLimiter::Soft;
    DrivingModel model(p);
    double top = 0.0, firedAtTop = 1.0;
    bool cutSeen = false;
    for (int i = 0; i < 4000; ++i) {
        model.stepNeutral(0.001, 1.0);
        if (i < 2000) continue;
        top = std::max(top, model.rpm());
        cutSeen = cutSeen || model.cut();
        firedAtTop = std::min(firedAtTop, model.ecuFiredShare());
    }
    CHECK_FALSE(cutSeen);
    // Il tient à 2 % du régime maximal, en ne gardant qu'une part des explosions.
    CHECK(top < model.redlineRpm() * 1.02);
    CHECK(top > model.redlineRpm());
    CHECK(firedAtTop < 0.7);
    CHECK(model.ecuAction().retardDeg == doctest::Approx(kSoftRetardDeg));
}

TEST_CASE("calculateur : le launch control tient le régime à l'arrêt et fait monter la pression du turbo") {
    Powertrain base = preset("hot_hatch_turbo");
    Powertrain launch = base;
    launch.engine.launchRpm = 4500;
    Run without(base), with(launch);
    // Une seconde au ralenti, puis pied sur le frein et au plancher pendant trois secondes.
    const auto hold = [](Run& run) {
        double t = 0.0;
        run.play(4.0, [&](double dt) {
            run.model.stepPedal(dt, t >= 1.0 ? 1.0 : 0.0, t >= 1.0 ? 1.0 : 0.0);
            t += dt;
        });
    };
    hold(without);
    hold(with);
    CHECK(with.model.launchHolding());
    CHECK(with.model.speedKmh() < 0.5);
    CHECK(std::fabs(with.model.rpm() - 4500.0) < 0.07 * 4500.0);
    // Le retard et la coupure d'étincelle envoient de la chaleur à la turbine : la pression y est déjà.
    CHECK(with.model.boost() > 0.8);
    CHECK(with.model.turbo().state().turbineInK > without.model.turbo().state().turbineInK + 100.0);
    CHECK(with.sound.pops() >= 10u);
    // On lâche le frein : il lâche dès que la voiture roule.
    with.play(1.0, [&](double dt) { with.model.stepPedal(dt, 1.0, 0.0); });
    CHECK_FALSE(with.model.launchHolding());
    CHECK(with.model.speedKmh() > 5.0);
    // Le 0-100 de la fiche, chronomètre au lâcher du frein : le launch control fait gagner du temps.
    // Fusion des finitions : avec l'antipatinage sport, comme les autres essais que le pneu à sommet contredisait. Sans lui, le
    // faisceau ε-NTU, froid au départ, laisse assez de couple pour que les quatre roues patinent au lâcher (κ = 2) : la première
    // s'épuise à 30 km/h au régime maximal, la deuxième retombe à 2500 tr/min, et le launch ne gagne plus rien (6,59 s contre
    // 6,57). Avec : 6,25 contre 6,57 s. Seuls, la thermique et l'air comme la conduite gagnaient 0,66 s sans antipatinage.
    Powertrain launchSport = launch, baseSport = base;
    launchSport.engine.tractionControl = TractionControl::Sport;
    baseSport.engine.tractionControl = TractionControl::Sport;
    CHECK(measurePerformance(launchSport).zeroTo100S < measurePerformance(baseSport).zeroTo100S - 0.3);
}

TEST_CASE("calculateur : le passage à fond garde la pression du turbo pendant la montée de rapport") {
    Powertrain base = preset("hot_hatch_turbo");
    Powertrain flat = base;
    flat.engine.flatShift = true;
    const auto lowestDuringShifts = [](const Powertrain& p, int& flatSteps) {
        DrivingModel model(p);
        double lowest = 10.0;
        flatSteps = 0;
        for (int i = 0; i < 12000; ++i) {
            model.stepPedal(0.001, 1.0, 0.0);
            if (model.flatShifting()) ++flatSteps;
            if (model.shifting() && model.gear() >= 2 && model.speedKmh() > 30.0) lowest = std::min(lowest, model.boost());
        }
        return lowest;
    };
    int stockFlat = 0, flatSteps = 0;
    const double stock = lowestDuringShifts(base, stockFlat);
    const double kept = lowestDuringShifts(flat, flatSteps);
    CHECK(stockFlat == 0);
    CHECK(flatSteps > 0);
    CHECK(stock < 0.5);
    CHECK(kept > 0.85);
    CHECK(measurePerformance(flat).zeroTo100S < measurePerformance(base).zeroTo100S - 0.15);
}

TEST_CASE("calculateur : l'antipatinage ramène le glissement vers sa cible, et le pneu à sommet le fait gagner") {
    Powertrain base;
    base.engine.cylinders = 8;
    base.engine.layout = Layout::CrossPlane;
    base.engine.displacementCc = 7000;
    base.engine.boreMm = 99;
    base.engine.redlineRpm = 6000;
    base.engine.exhaust = Exhaust::Sport;
    base.car = CarConfig{Body::Coupe, 1700, 19, Drive::Rear, EnginePlacement::Natural, Insulation::Standard, false};
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
    Powertrain sport = base, full = base;
    sport.engine.tractionControl = TractionControl::Sport;
    full.engine.tractionControl = TractionControl::Full;
    const double free = meanSlip(base);
    CHECK(free > 0.3);
    CHECK(meanSlip(sport) == doctest::Approx(tractionSlipTarget(TractionControl::Sport)).epsilon(0.25));
    CHECK(meanSlip(full) == doctest::Approx(tractionSlipTarget(TractionControl::Full)).epsilon(0.25));
    // Le pneu perd de l'adhérence au-delà de son sommet : tenir le glissement fait gagner du temps (voir docs/modele.md).
    CHECK(measurePerformance(full).zeroTo100S < measurePerformance(base).zeroTo100S);
}
