#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

#include "doctest.h"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/supercharger.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kStep = 0.001;
constexpr double kPi = 3.141592653589793;
constexpr double kAtmosphere = 101325.0;

Powertrain engine(Aspiration aspiration, SuperchargerKind kind = SuperchargerKind::Roots, float bar = 1.0f) {
    Powertrain p;
    p.engine.cylinders = 4;
    p.engine.displacementCc = 2000;
    p.engine.boreMm = 84;
    p.engine.redlineRpm = 6800;
    p.engine.aspiration = aspiration;
    p.engine.superchargerKind = kind;
    p.engine.boostBar = bar;
    p.car.body = Body::Compact;
    p.car.massKg = 1350.0f;
    p.car.rimInch = 18;
    return p;
}

Powertrain roots(float bar = 1.0f) { return engine(Aspiration::Supercharger, SuperchargerKind::Roots, bar); }
Powertrain screw(float bar = 1.0f) { return engine(Aspiration::Supercharger, SuperchargerKind::TwinScrew, bar); }
Powertrain wheel(float bar = 1.0f) { return engine(Aspiration::Supercharger, SuperchargerKind::Centrifugal, bar); }

SuperchargerSteady steadyAt(const Powertrain& p, double rpm, double fill = 1.0) {
    return superchargerSteady(superchargerSpecOf(p), rpm, breathingOf(p, rpm), fill);
}

/** Tient ce régime et ce remplissage, sans voiture : le temps que tout se stabilise. */
Supercharger heldAt(const Powertrain& p, double rpm, double fill, double seconds = 10.0) {
    Supercharger charger;
    charger.setSpec(superchargerSpecOf(p));
    SuperchargerInput in;
    in.rpm = rpm;
    in.fill = fill;
    in.breathing = breathingOf(p, rpm);
    for (double t = 0.0; t < seconds; t += kStep) charger.step(kStep, in);
    return charger;
}

/** Temps que met la pression à atteindre 80 % de ce qu'elle va tenir, plein gaz d'un coup à ce régime. L'échangeur, lui, met quelques secondes à chauffer. */
double timeToBoost(const Powertrain& p, double rpm) {
    const double goal = 0.8 * steadyAt(p, rpm).boost * p.engine.boostBar * 1e5 + kAtmosphere;
    Supercharger charger = heldAt(p, rpm, 0.2, 2.0);
    SuperchargerInput in;
    in.rpm = rpm;
    in.fill = 1.0;
    in.breathing = breathingOf(p, rpm);
    double t = 0.0;
    while (t < 20.0 && charger.state().chargePa < goal) {
        charger.step(kStep, in);
        t += kStep;
    }
    return t;
}

// ── Le son ──

/** Le son décrit, à partir de l'avant : tout passe par `setSupercharger`, par tranches de 256 échantillons. */
Samples renderCharger(const Powertrain& powertrain, float rpm, float load, float boost, const std::function<SuperchargerDrive(int)>& drive, int block = 256, int updates = 400, Listener listener = Listener::EngineBay) {
    EngineSound core(kRate);
    core.setProfile(powertrain.profile());
    core.setVolume(0.7f);
    core.setListener(listener);
    Samples out(static_cast<std::size_t>(updates) * 256);
    for (int u = 0; u < updates; ++u) {
        core.setTargetRpm(rpm);
        core.setDrive(load, boost, false, false, load);
        core.setSupercharger(drive(u));
        for (int done = 0; done < 256; done += block) core.render(out.data() + static_cast<std::size_t>(u) * 256 + done, block);
    }
    return out;
}

SuperchargerDrive stateFor(const Powertrain& p, double rpm, double liftBar, double flowShare) {
    const SuperchargerSpec spec = superchargerSpecOf(p);
    SuperchargerDrive d;
    d.ratio = static_cast<float>(spec.driveRatio);
    d.liftPa = static_cast<float>(liftBar * 1e5);
    d.flowShare = static_cast<float>(flowShare);
    d.speedShare = static_cast<float>(rpm * 2.0 * kPi / 60.0 * spec.driveRatio / spec.referenceOmega);
    return d;
}

/** Énergie à une fréquence (Goertzel). */
double powerAt(const Samples& x, double hz) {
    const double w = 2.0 * kPi * hz / kRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (float sample : x) {
        const double s0 = static_cast<double>(sample) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double band(const Samples& x, double from, double to, double step = 10.0) {
    double sum = 0.0;
    int count = 0;
    for (double hz = from; hz <= to; hz += step, ++count) sum += powerAt(x, hz);
    return sum / count;
}

/** La fréquence la plus forte d'une bande. */
double loudestIn(const Samples& x, double from, double to) {
    double best = from;
    double most = -1.0;
    for (double hz = from; hz <= to; hz += 2.0) {
        const double power = powerAt(x, hz);
        if (power > most) {
            most = power;
            best = hz;
        }
    }
    return best;
}

/** L'énergie de la raie la plus forte d'une bande étroite. */
double strongestIn(const Samples& x, double from, double to) {
    double most = 0.0;
    for (double hz = from; hz <= to; hz += 0.25) most = std::max(most, powerAt(x, hz));
    return most;
}

double decibels(double ratio) { return 10.0 * std::log10(ratio); }

}  // namespace

TEST_CASE("le compresseur volumétrique a sa pression dès le bas, le centrifuge seulement en haut") {
    const double rootsLow = steadyAt(roots(), 2000.0).boost;
    const double wheelLow = steadyAt(wheel(), 2000.0).boost;
    CHECK(rootsLow > 0.7);
    CHECK(wheelLow < 0.2);
    // Plus de 90 % à 40 % du régime maximal : c'est là qu'on le dimensionne.
    CHECK(steadyAt(roots(), 0.4 * 6800.0).boost == doctest::Approx(1.0).epsilon(0.01));
    CHECK(steadyAt(screw(), 0.4 * 6800.0).boost == doctest::Approx(1.0).epsilon(0.01));
    // À 88 %, le centrifuge a enfin ce qu'on lui demande.
    CHECK(steadyAt(wheel(), 0.88 * 6800.0).boost > 0.9);
}

TEST_CASE("la pression d'un centrifuge croît avec le carré de la vitesse") {
    const Powertrain p = wheel(0.6f);
    const double half = steadyAt(p, 3000.0).boost;
    const double full = steadyAt(p, 6000.0).boost;
    REQUIRE(full > 0.0);
    // Le rapport de pression suit U² ; la perte sur la caractéristique en retire un peu aux deux.
    CHECK(half / full == doctest::Approx(0.25).epsilon(0.25));
}

TEST_CASE("les fuites d'un Roots pèsent à bas régime") {
    // La vis fuit moins : à régime égal et à dimensionnement égal, elle tient mieux le bas.
    CHECK(steadyAt(screw(), 1000.0).boost > steadyAt(roots(), 1000.0).boost);
    // Sans fuite, un volumétrique serait à sa pression dès le ralenti : on vérifie qu'on ne l'est pas.
    CHECK(steadyAt(roots(), 800.0).boost < 0.8);
    CHECK(steadyAt(roots(), 800.0).boost > 0.0);
}

TEST_CASE("un Roots chauffe plus que la vis et le centrifuge, à même pression") {
    constexpr double rpm = 5000.0;
    const SuperchargerSteady r = steadyAt(roots(), rpm);
    const SuperchargerSteady s = steadyAt(screw(), rpm);
    const SuperchargerSteady c = steadyAt(wheel(), rpm + 900.0);
    REQUIRE(r.boost > 0.99);
    REQUIRE(s.boost > 0.99);
    REQUIRE(c.boost > 0.9);
    CHECK(r.outK > s.outK + 5.0);
    CHECK(r.outK > c.outK + 5.0);
    // Ordres de grandeur : isentropique 64 K à ce rapport de pression, et jamais moins.
    CHECK(s.outK > 293.0 + 64.0);
    CHECK(c.outK > 293.0 + 64.0);
    CHECK(r.outK < 293.0 + 200.0);
}

TEST_CASE("l'air plus chaud d'un Roots donne moins de couple brut à même pression") {
    constexpr double rpm = 4000.0;
    const double a = fullLoadTorqueNm(roots(), rpm, 1.0);
    const double b = fullLoadTorqueNm(screw(), rpm, 1.0);
    CHECK(a < b);
    // À zéro de pression, c'est le même moteur atmosphérique que le turbo sans pression.
    CHECK(fullLoadTorqueNm(roots(), rpm, 0.0) == doctest::Approx(fullLoadTorqueNm(engine(Aspiration::Turbo), rpm, 0.0)));
}

TEST_CASE("la puissance d'entraînement se paie sur le couple net") {
    constexpr double rpm = 5500.0;
    const Powertrain p = roots();
    const SuperchargerSteady s = steadyAt(p, rpm);
    REQUIRE(s.driveNm > 0.0);
    CHECK(torqueCurveNm(p, rpm) == doctest::Approx(fullLoadTorqueNm(p, rpm, s.boost) - s.driveNm));
    // Une dizaine de pour cent du couple net à 1 bar : l'ordre de ce qu'on mesure sur un Roots (Hellcat : 80 à 100 ch sur 700).
    const double share = s.driveNm / torqueCurveNm(p, rpm);
    CHECK(share > 0.05);
    CHECK(share < 0.4);
    // Et le Roots en coûte plus que la vis : il ne comprime qu'à l'ouverture de la poche.
    CHECK(s.driveNm > steadyAt(screw(), rpm).driveNm);
}

TEST_CASE("soupape ouverte, le compresseur ne donne pas de pression et ne coûte presque rien") {
    for (const Powertrain& p : {roots(), screw(), wheel()}) {
        constexpr double rpm = 3000.0;
        const Supercharger light = heldAt(p, rpm, 0.3);
        const Supercharger full = heldAt(p, rpm, 1.0);
        CHECK(light.state().chargePa - kAtmosphere < 0.12e5);
        CHECK(light.state().spillShare > 0.5);
        CHECK(light.driveNm(rpm) < 0.5 * full.driveNm(rpm));
    }
    // Contre-épreuve de la soupape : à régime égal, le volumétrique, plein gaz, y met bien sa pression.
    CHECK(heldAt(roots(), 3000.0, 1.0).state().chargePa - kAtmosphere > 0.9e5);
}

TEST_CASE("le pas de temps rejoint l'équilibre de la fiche") {
    for (const Powertrain& p : {roots(), screw(), wheel(), engine(Aspiration::Supercharger, SuperchargerKind::Roots, 2.5f)}) {
        for (double rpm : {1500.0, 3500.0, 6000.0}) {
            const Supercharger held = heldAt(p, rpm, 1.0);
            const SuperchargerSteady steady = steadyAt(p, rpm);
            CHECK(held.state().chargePa == doctest::Approx(steady.chargePa).epsilon(0.01));
            CHECK(held.driveNm(rpm) == doctest::Approx(steady.driveNm).epsilon(0.03));
        }
    }
}

TEST_CASE("pas de retard d'arbre : la pression arrive bien avant celle d'un turbo") {
    // L'arbre suit le vilebrequin dès le premier pas.
    const Powertrain p = roots();
    Supercharger charger;
    charger.setSpec(superchargerSpecOf(p));
    SuperchargerInput in;
    in.rpm = 3000.0;
    in.fill = 1.0;
    in.breathing = breathingOf(p, 3000.0);
    charger.step(kStep, in);
    CHECK(charger.state().rotorOmega == doctest::Approx(3000.0 * 2.0 * kPi / 60.0 * superchargerSpecOf(p).driveRatio));

    // Le temps de remplir le volume entre le compresseur et le papillon : pas celui de lancer un arbre.
    for (const Powertrain& charged : {roots(), screw()}) {
        const double t = timeToBoost(charged, 2500.0);
        MESSAGE("compresseur : 80 % en ", t, " s");
        CHECK(t < 0.3);
        CHECK(t > 0.02);
    }
    const Powertrain turbo = engine(Aspiration::Turbo);
    TurboCharger shaft;
    shaft.setSpec(turboSpecOf(turbo));
    TurboInput pedal;
    pedal.rpm = 2500.0;
    pedal.breathing = breathingOf(turbo, 2500.0);
    pedal.fill = 0.2;
    pedal.burning = true;
    for (double t = 0.0; t < 2.0; t += kStep) shaft.step(kStep, pedal);
    pedal.fill = 1.0;
    const double goal = 0.8 * steadyBoost(turbo, 2500.0) * 1e5 + kAtmosphere;
    double t = 0.0;
    while (t < 20.0 && shaft.state().chargePa < goal) {
        shaft.step(kStep, pedal);
        t += kStep;
    }
    MESSAGE("turbo : 80 % en ", t, " s");
    CHECK(t > 2.0 * timeToBoost(roots(), 2500.0));
}

TEST_CASE("sur la route, le modèle tient la courbe de la fiche") {
    const Powertrain p = roots();
    DrivingModel model(p);
    model.setManual(true);
    model.requestShift(1);
    // Jusqu'en troisième, sans forcer ; puis plein gaz d'un coup : le compresseur est là tout de suite.
    for (int i = 0; i < 2000 && model.gear() < 3; ++i) {
        model.stepPedal(0.01, 0.5, 0.0);
        if (!model.shifting() && model.rpm() > 3000.0 && model.gear() < 3) model.requestShift(1);
    }
    for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.4, 0.0);
    REQUIRE(model.gear() == 3);
    for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 1.0, 0.0);
    REQUIRE(model.rpm() < 6500.0);
    CHECK(model.boost() > 0.9);
    CHECK(model.supercharger().state().shaftW > 5000.0);
    CHECK(model.torqueNm() == doctest::Approx(torqueCurveNm(p, model.rpm())).epsilon(0.05));
}

TEST_CASE("le 0 à 100 km/h profite d'un compresseur, et d'un Roots plus tôt qu'un centrifuge") {
    const double natural = measurePerformance(engine(Aspiration::Natural)).zeroTo100S;
    const double withRoots = measurePerformance(roots(0.8f)).zeroTo100S;
    CHECK(withRoots < natural);
    CHECK(powerSummary(roots(0.8f)).peakTorqueNm > 1.3 * powerSummary(engine(Aspiration::Natural)).peakTorqueNm);
    // Le couple maximal d'un Roots arrive bien plus bas que celui d'un centrifuge de même pression.
    CHECK(powerSummary(roots(0.8f)).peakTorqueRpm < powerSummary(wheel(0.8f)).peakTorqueRpm);
    // La puissance prise au vilebrequin : à pression égale, le Roots a moins de puissance nette en haut que la vis.
    CHECK(powerSummary(roots(1.5f)).peakPowerKw < powerSummary(screw(1.5f)).peakPowerKw);
}

TEST_CASE("le carburant paie l'entraînement du compresseur") {
    // À la même puissance au frein et au même régime, il faut brûler de quoi entraîner le compresseur.
    const Powertrain charged = roots();
    const double rpm = 5500.0;
    const double brake = 0.95 * torqueCurveNm(charged, rpm);
    const double paid = brakeSpecificFuel(charged, rpm, brake);
    REQUIRE(paid > 0.0);
    // Le même calcul, sans rien à entraîner.
    ConsumptionModel model;
    model.configure(charged);
    const double friction = frictionTorqueNm(charged, rpm);
    const double fullGross = fullLoadTorqueNm(charged, rpm, steadyBoost(charged, rpm)) + friction;
    const double kw = brake * rpm * 2.0 * kPi / 60.0 / 1000.0;
    const double free = model.fuelGps(rpm, brake, friction, fullGross) * 3600.0 / kw;
    CHECK(paid > 1.05 * free);
    // Sur la route aussi : le modèle compte le compresseur dans ce que paient les pistons.
    DrivingModel car(charged);
    car.setManual(true);
    car.requestShift(1);
    for (int i = 0; i < 300; ++i) car.stepPedal(0.01, 1.0, 0.0);
    const double drive = car.supercharger().driveNm(car.rpm());
    REQUIRE(drive > 10.0);
    CHECK(car.consumption().fuelLph() > 0.0);
}

TEST_CASE("en croisière, le compresseur ouvert coûte peu") {
    const auto cruise = [](const Powertrain& p, double* watts, double* spill) {
        DrivingModel model(p);
        model.setManual(false);
        for (int i = 0; i < 4000; ++i) model.stepPedal(0.01, model.speedKmh() < 88.0 ? 0.5 : (model.speedKmh() > 92.0 ? 0.0 : 0.2), 0.0);
        model.resetTrip();
        for (int i = 0; i < 2000; ++i) model.stepPedal(0.01, model.speedKmh() < 88.0 ? 0.3 : (model.speedKmh() > 92.0 ? 0.0 : 0.2), 0.0);
        if (watts) *watts = model.supercharger().state().shaftW;
        if (spill) *spill = model.supercharger().state().spillShare;
        return model.consumption().fuelAvgLper100km();
    };
    double watts = 0.0;
    double spill = 0.0;
    const double natural = cruise(engine(Aspiration::Natural), nullptr, nullptr);
    const double charged = cruise(roots(), &watts, &spill);
    REQUIRE(natural > 0.0);
    // À 90 km/h, le besoin est de l'ordre de 15 kW : le compresseur n'en prend qu'une petite part, soupape ouverte.
    CHECK(watts < 2500.0);
    CHECK(spill > 0.5);
    CHECK(charged < 1.2 * natural);
}

TEST_CASE("le type de compresseur ne change rien sur un moteur qui n'en a pas") {
    Powertrain a = engine(Aspiration::Natural, SuperchargerKind::Roots);
    Powertrain b = engine(Aspiration::Natural, SuperchargerKind::Centrifugal);
    CHECK(torqueCurveNm(a, 3000.0) == torqueCurveNm(b, 3000.0));
    a = engine(Aspiration::Turbo, SuperchargerKind::Roots);
    b = engine(Aspiration::Turbo, SuperchargerKind::Centrifugal);
    CHECK(torqueCurveNm(a, 3000.0) == torqueCurveNm(b, 3000.0));
    // Et il n'est pas un turbo : pas d'arbre.
    DrivingModel model(roots());
    CHECK(model.turbo().state().shaftOmega == 0.0);
    CHECK(model.supercharged());
    CHECK_FALSE(DrivingModel(a).supercharged());
}

TEST_CASE("le sifflement d'un Roots est à six poches par tour d'arbre, et suit le régime") {
    const Powertrain p = roots();
    const double ratio = superchargerSpecOf(p).driveRatio;
    for (double rpm : {2500.0, 5000.0}) {
        const double expected = rpm / 60.0 * ratio * 6.0;
        const Samples tone = renderCharger(p, static_cast<float>(rpm), 1.0f, 1.0f, [&](int) { return stateFor(p, rpm, 1.0, 0.8); });
        const Samples silent = renderCharger(p, static_cast<float>(rpm), 1.0f, 1.0f, [&](int) { return stateFor(p, rpm, 0.0, 0.0); });
        CHECK(loudestIn(tone, expected * 0.9, expected * 1.1) == doctest::Approx(expected).epsilon(0.015));
        // Ôter l'écart de pression ôte la raie : c'est lui qui la fait.
        CHECK(decibels(strongestIn(tone, expected - 2.0, expected + 2.0) / strongestIn(silent, expected - 2.0, expected + 2.0)) > 10.0);
    }
}

TEST_CASE("la vis, à la pression qu'elle atteint d'elle-même, siffle moins que sur une pression plus haute") {
    const Powertrain p = screw();
    const double rpm = 4000.0;
    const double expected = rpm / 60.0 * superchargerSpecOf(p).driveRatio * 3.0;
    const double inner = p.engine.toProfile().superchargerInnerPressure;
    const Samples matched = renderCharger(p, 4000.0f, 1.0f, 0.5f, [&](int) { return stateFor(p, rpm, (inner - 1.0) * 1.013, 0.8); });
    const Samples above = renderCharger(p, 4000.0f, 1.0f, 0.5f, [&](int) { return stateFor(p, rpm, 1.5, 0.8); });
    CHECK(decibels(strongestIn(above, expected - 2.0, expected + 2.0) / strongestIn(matched, expected - 2.0, expected + 2.0)) > 6.0);
}

TEST_CASE("le sifflement du centrifuge suit la roue, et sa vitesse") {
    const Powertrain p = wheel();
    const SuperchargerSpec spec = superchargerSpecOf(p);
    const double rpm = 3500.0;
    // Le balourd et les jeux de la roue : l'ordre 2 de l'arbre.
    const double expected = rpm / 60.0 * spec.driveRatio * 2.0;
    const Samples fast = renderCharger(p, 3500.0f, 1.0f, 0.3f, [&](int) { return stateFor(p, rpm, 0.3, 0.5); }, 256, 400, Listener::Front);
    const Samples still = renderCharger(p, 3500.0f, 1.0f, 0.3f, [&](int) {
        SuperchargerDrive d = stateFor(p, rpm, 0.3, 0.5);
        d.speedShare = 0.0f;
        return d;
    }, 256, 400, Listener::Front);
    CHECK(loudestIn(fast, expected * 0.98, expected * 1.02) == doctest::Approx(expected).epsilon(0.01));
    CHECK(decibels(strongestIn(fast, expected - 2.0, expected + 2.0) / strongestIn(still, expected - 2.0, expected + 2.0)) > 6.0);
}

TEST_CASE("le compresseur garde sa place au ralenti : ni devant l'échappement, ni bruit blanc") {
    // Au ralenti, soupape ouverte : presque pas d'écart de pression, donc presque rien du compresseur.
    const Powertrain plain = engine(Aspiration::Natural);
    const Powertrain charged = roots();
    const float idle = plain.engine.toProfile().idleRpm;
    const auto listen = [idle](const EngineConfig& c, const SuperchargerDrive& drive) {
        EngineSound core(kRate);
        core.setProfile(c.toProfile());
        core.setVolume(0.7f);
        core.setListener(Listener::Outside);
        Samples out(kRate * 2);
        std::size_t done = 0;
        while (done + 256 <= out.size()) {
            core.setTargetRpm(idle);
            core.setDrive(0.12f, 0.0f, false, false, 0.1f);
            core.setSupercharger(drive);
            core.render(out.data() + done, 256);
            done += 256;
        }
        return out;
    };
    SuperchargerDrive open = stateFor(charged, idle, 0.01, 0.02);
    const Samples a = listen(plain.engine, SuperchargerDrive{});
    const Samples b = listen(charged.engine, open);
    // Bande médiane et aigus : le niveau bouge à peine, ce qui est le signe qu'aucune source fixe n'est passée devant.
    const double mid = decibels(band(b, 500.0, 2000.0, 20.0) / band(a, 500.0, 2000.0, 20.0));
    const double high = decibels(band(b, 8000.0, 16000.0, 40.0) / band(a, 8000.0, 16000.0, 40.0));
    CHECK(std::fabs(mid) < 1.5);
    CHECK(std::fabs(high) < 1.5);
}

TEST_CASE("le son ne dépend pas de la taille des blocs, compresseur compris") {
    const Powertrain p = roots();
    const auto drive = [&](int u) { return stateFor(p, 3000.0 + 10.0 * u, u < 200 ? 0.2 : 1.0, 0.7); };
    const Samples a = renderCharger(p, 3000.0f, 0.9f, 0.9f, drive, 256);
    const Samples b = renderCharger(p, 3000.0f, 0.9f, 0.9f, drive, 64);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
}

TEST_CASE("le rendu d'un compresseur n'alloue rien") {
    for (const Powertrain& p : {roots(), screw(), wheel()}) {
        EngineSound core(kRate);
        core.setProfile(p.engine.toProfile());
        core.setVolume(0.7f);
        core.setTargetRpm(4000.0f);
        core.setDrive(1.0f, 1.0f, false, false, 1.0f);
        core.setSupercharger(stateFor(p, 4000.0, 1.0, 0.8));
        Samples out(4096);
        core.render(out.data(), 2048);
        const long before = g_allocations.load();
        for (int i = 0; i < 40; ++i) core.render(out.data(), 2048);
        CHECK(g_allocations.load() == before);
    }
}

TEST_CASE("le démarreur d'un moteur à compresseur lance une compression abaissée, comme un turbo") {
    EngineConfig c;
    c.aspiration = Aspiration::Supercharger;
    CHECK(c.toProfile().starter.compressionRatio == doctest::Approx(9.5f));
    c.aspiration = Aspiration::Natural;
    CHECK(c.toProfile().starter.compressionRatio == doctest::Approx(10.5f));
    // Pas de turbine sur le chemin : le coup de pop n'est pas amorti, à la différence d'un turbo.
    c.aspiration = Aspiration::Supercharger;
    const float charged = c.toProfile().popForce;
    c.aspiration = Aspiration::Turbo;
    CHECK(charged > c.toProfile().popForce);
}
