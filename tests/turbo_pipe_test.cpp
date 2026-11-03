#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/turbo.hpp"
#include "test_support.hpp"

using namespace revforge;

namespace {

constexpr double kStep = 0.001;

Powertrain piped(int mm, const char* id = "hot_hatch_turbo") {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.turboPipeMm = mm;
    return p;
}

TurboInput at(const Powertrain& p, double rpm, double fill, bool burning = true) {
    TurboInput in;
    in.rpm = rpm;
    in.fill = fill;
    in.breathing = breathingOf(p, rpm);
    in.burning = burning;
    return in;
}

void run(TurboCharger& turbo, const TurboInput& in, double seconds) {
    const int steps = static_cast<int>(seconds / kStep);
    for (int i = 0; i < steps; ++i) turbo.step(kStep, in);
}

struct Stab {
    double to50 = 30.0;
    double to90 = 30.0;
    double gasC = 0.0;  // devant la turbine, à 90 % de la pression
};

/**
 * Un coup de gaz à régime tenu. `warmS` : ce que le moteur a roulé avant, à mi-charge, pour chauffer les parois.
 * Le 50 et le 90 % sont ceux de la pression que ce réglage tient à ce régime : chacun est mesuré à sa propre arrivée.
 */
Stab stabWith(const Powertrain& p, const TurboSpec& spec, double rpm, double warmS) {
    TurboCharger turbo;
    turbo.setSpec(spec);
    run(turbo, at(p, rpm, 0.5), warmS);
    run(turbo, at(p, rpm, 0.2), 8.0);
    const double wanted = turboSteady(spec, rpm, breathingOf(p, rpm)).boost;
    Stab out;
    for (int i = 0; i < 30000; ++i) {
        turbo.step(kStep, at(p, rpm, 1.0));
        const double seconds = static_cast<double>(i) * kStep;
        if (out.to50 >= 30.0 && turbo.boost() >= 0.5 * wanted) out.to50 = seconds;
        if (turbo.boost() >= 0.9 * wanted) {
            out.to90 = seconds;
            out.gasC = turbo.state().turbineInK - 273.15;
            return out;
        }
    }
    return out;
}

Stab stab(const Powertrain& p, double rpm, double warmS) { return stabWith(p, turboSpecOf(p), rpm, warmS); }

/** La montée de `--scenario turbo` : en quatrième vers 1800 tr/min, puis à fond. Même conduite que la CLI, sans le son. */
struct Climb {
    double to50 = 60.0;
    double to90 = 60.0;
    double gasC = 0.0;
    double peak = 0.0;
};

Climb scenarioTurbo(const Powertrain& p, double warmS = 0.0) {
    constexpr double dt = 128.0 / 44100.0;
    DrivingModel model(p);
    model.setManual(true);
    model.requestShift(1);
    double t = 0.0;
    int phase = 0;
    double since = 0.0;
    Climb out;
    const int top = std::min(model.gears(), 4);
    while (t < 60.0 + warmS) {
        const double engine = model.rpm();
        double pedal = 0.0;
        double brake = 0.0;
        if (phase == 0) {
            pedal = t > 0.3 ? 0.625 : 0.0;
            if (model.gear() < top && !model.shifting() && engine > 3000.0) model.requestShift(1);
            if (model.gear() == top && !model.shifting()) {
                phase = 1;
                since = t;
            }
        } else if (phase == 1) {
            pedal = engine < 1800.0 ? 0.25 : 0.0;
            brake = engine > 2000.0 ? 0.125 : 0.0;
            if (t - since > 5.0 + warmS) {
                phase = 2;
                since = t;
            }
        } else {
            pedal = 1.0;
            const double seconds = t - since;
            const double boost = model.boost();
            if (out.to50 >= 60.0 && boost >= 0.5) out.to50 = seconds;
            if (out.to90 >= 60.0 && boost >= 0.9) {
                out.to90 = seconds;
                out.gasC = model.turbo().state().turbineInK - 273.15;
            }
            out.peak = std::max(out.peak, boost);
            if (engine > 4200.0) break;
        }
        model.stepPedal(dt, pedal, brake);
        t += dt;
    }
    return out;
}

constexpr int kLengths[] = {150, 400, 1000, 2000, 3000};

}  // namespace

TEST_CASE("à 400 mm, le tube n'existe pas") {
    const TurboSpec spec = turboSpecOf(piped(400));
    CHECK_FALSE(spec.pipe.active);
    CHECK(spec.pipe.frictionDrop == 0.0);
    // La longueur a un effet partout ailleurs : un test qui ne bouge pas ne dirait rien.
    CHECK(turboSpecOf(piped(450)).pipe.active);
    CHECK(turboSpecOf(piped(350)).pipe.active);
}

TEST_CASE("le retard croît régulièrement avec la longueur") {
    for (const double rpm : {2500.0, 3500.0}) {
        double previous = 0.0;
        for (int mm = 150; mm <= 3000; mm += 350) {
            const Stab warm = stab(piped(mm), rpm, 90.0);
            INFO(mm << " mm à " << rpm);
            CHECK(warm.to90 > previous);
            previous = warm.to90;
        }
    }
    // Tout près de la référence, des deux côtés, l'effet tend vers rien : le réglage n'a pas de marche à 400 mm.
    const double reference = stab(piped(400), 3500.0, 90.0).to90;
    CHECK(stab(piped(350), 3500.0, 90.0).to90 == doctest::Approx(reference).epsilon(0.03));
    CHECK(stab(piped(450), 3500.0, 90.0).to90 == doctest::Approx(reference).epsilon(0.03));
}

// Le tableau de `docs/modele.md`, à la demande : revforge_tests --no-skip -tc="le tube : tableau*"
TEST_CASE("sur tous les turbos, un tube plus long retarde et un plus court avance, sans jamais diverger") {
    for (const char* id : {"three_cyl_turbo", "five_cyl_turbo", "boxer_four_turbo", "diesel_four"}) {
        INFO(id);
        const double rpm = 0.5 * static_cast<double>(piped(400, id).engine.redlineRpm);
        const double reference = stab(piped(400, id), rpm, 60.0).to90;
        CHECK(stab(piped(3000, id), rpm, 60.0).to90 > reference);
        CHECK(stab(piped(150, id), rpm, 60.0).to90 < reference);
        for (const int mm : {150, 3000}) {
            const Powertrain p = piped(mm, id);
            TurboCharger turbo;
            turbo.setSpec(turboSpecOf(p));
            // Plein gaz, lever de pied, moteur coupé : tout ce qui pourrait annuler un débit ou une détente.
            for (int i = 0; i < 12000; ++i) {
                const bool off = i > 9000;
                const double fill = i < 5000 ? 1.0 : 0.05;
                turbo.step(kStep, at(p, off ? 0.0 : rpm, off ? 0.0 : fill, i < 5000));
                const TurboState& s = turbo.state();
                REQUIRE(std::isfinite(s.turbineInK));
                REQUIRE(std::isfinite(s.turbineInPa));
                REQUIRE(std::isfinite(s.shaftOmega));
                REQUIRE(s.turbineInK > 200.0);
                REQUIRE(s.turbineInK < 1600.0);
            }
        }
    }
}

TEST_CASE("le tube : tableau de mesure" * doctest::skip()) {
    const auto fullBoostRpm = [](const Powertrain& p) {
        for (double rpm = 1000.0; rpm < 8000.0; rpm += 50.0) {
            if (steadyBoost(p, rpm) >= 0.9) return rpm;
        }
        return 0.0;
    };
    for (const int mm : kLengths) {
        const Powertrain p = piped(mm);
        const Climb cold = scenarioTurbo(p);
        const Climb warm = scenarioTurbo(p, 120.0);
        const Stab held = stab(p, 2500.0, 120.0);
        const PowerSummary power = powerSummary(p);
        const Stab held3500 = stab(p, 3500.0, 120.0);
        TurboCharger settled;
        settled.setSpec(turboSpecOf(p));
        run(settled, at(p, 2500.0, 1.0), 200.0);
        const double low = settled.state().turbineInK - 273.15;
        run(settled, at(p, 5500.0, 1.0), 200.0);
        const double high = settled.state().turbineInK - 273.15;
        std::printf("%4d mm | scenario froid %.2f / %.2f s %.0f C | chaud %.2f / %.2f s %.0f C | 2500 tr/min %.2f / %.2f s | 3500 tr/min %.2f / %.2f s | etabli %.0f C a 2500, %.0f C a 5500 | %.1f Nm a 2500, %.1f Nm a 5000 | %.0f Nm %.1f kW | 0-100 %.2f s | 90 %% tenu des %.0f tr/min\n", mm, cold.to50, cold.to90, cold.gasC, warm.to50, warm.to90, warm.gasC, held.to50, held.to90, held3500.to50, held3500.to90, low, high,
                    torqueCurveNm(p, 2500.0), torqueCurveNm(p, 5000.0), power.peakTorqueNm, power.peakPowerKw, measurePerformance(p).zeroTo100S, fullBoostRpm(p));
    }
    // Un V8 de 4,8 L à 0,48 bar, gros turbo, pour se comparer à l'essai de R. Holdener (+3,66 m d'échappement).
    for (const int mm : {400, 1000, 3000}) {
        Powertrain p = piped(mm, "v8_cross_plane");
        p.engine.aspiration = Aspiration::Turbo;
        p.engine.displacementCc = 4800;
        p.engine.boostBar = 0.48f;
        p.engine.turboSize = TurboSize::Large;
        const PowerSummary power = powerSummary(p);
        std::printf("V8 %4d mm | 90 %% tenu des %.0f tr/min | %.0f Nm %.1f kW\n", mm, fullBoostRpm(p), power.peakTorqueNm, power.peakPowerKw);
    }
    CHECK(true);
}

// Ce que pèse chaque cause, en les retirant une à une : voir `docs/modele.md`. revforge_tests --no-skip -tc="le tube : poids*"
TEST_CASE("le tube : poids de chaque cause" * doctest::skip()) {
    const Powertrain p = piped(3000);
    const Stab held = stab(p, 3500.0, 120.0);
    const Climb warm = scenarioTurbo(p, 120.0);
    std::printf("poids | 3500 tr/min %.2f s | scenario chaud %.2f s\n", held.to90, warm.to90);
    CHECK(true);
}

TEST_CASE("un tube plus long refroidit le gaz, un tube plus court le rend plus chaud") {
    const auto settledK = [](int mm, double rpm) {
        const Powertrain p = piped(mm);
        TurboCharger turbo;
        turbo.setSpec(turboSpecOf(p));
        run(turbo, at(p, rpm, 1.0), 150.0);
        return turbo.state().turbineInK;
    };
    double previous = 1e9;
    for (const int mm : kLengths) {
        const double gas = settledK(mm, 5000.0);
        INFO(mm << " mm : " << gas - 273.15 << " °C");
        CHECK(gas < previous);
        previous = gas;
    }
    // À faible débit, la différence se voit le mieux : retirer du tube rend vraiment de la chaleur, en ajouter en prend beaucoup.
    CHECK(settledK(150, 2500.0) > settledK(400, 2500.0) + 5.0);
    CHECK(settledK(3000, 2500.0) < settledK(400, 2500.0) - 100.0);
}

TEST_CASE("le gaz arrive moins vite tant que le volume n'a pas lissé les bouffées : c'est branché sur l'arbre") {
    // Sans cylindres, la cadence d'allumage est nulle : le filtre laisse tout passer. Même tube, sans cette seule cause.
    const Powertrain p = piped(3000);
    const TurboSpec spec = turboSpecOf(p);
    TurboSpec unfiltered = spec;
    unfiltered.pipe.cylinders = 0;
    const auto halfBoostS = [&p](const TurboSpec& s) {
        TurboCharger turbo;
        turbo.setSpec(s);
        run(turbo, at(p, 2500.0, 0.5), 120.0);
        run(turbo, at(p, 2500.0, 0.2), 8.0);
        for (int i = 0; i < 30000; ++i) {
            turbo.step(kStep, at(p, 2500.0, 1.0));
            if (turbo.boost() >= 0.5) return static_cast<double>(i) * kStep;
        }
        return 30.0;
    };
    const double lost = halfBoostS(spec);
    const double kept = halfBoostS(unfiltered);
    MESSAGE("50 % de la pression à 2500 tr/min : " << lost << " s, " << kept << " s sans ce filtre");
    CHECK(lost > 1.02 * kept);
    const double filteredBoost = turboSteady(spec, 2100.0, breathingOf(p, 2100.0)).boost;
    const double fullBoost = turboSteady(unfiltered, 2100.0, breathingOf(p, 2100.0)).boost;
    CHECK(filteredBoost < fullBoost);
}

TEST_CASE("le frottement du tube se paie en contre-pression, seulement pour le moteur") {
    const double reference = turboBackPressureNm(turboSpecOf(piped(400)), 2e5, 2.2e5, 0.15, 1.0);
    CHECK(turboBackPressureNm(turboSpecOf(piped(3000)), 2e5, 2.2e5, 0.15, 1.0) > reference + 0.5);
    CHECK(turboBackPressureNm(turboSpecOf(piped(150)), 2e5, 2.2e5, 0.15, 1.0) < reference);
}

TEST_CASE("la paroi d'un tube froid prend de la chaleur au gaz, puis de moins en moins") {
    const Powertrain p = piped(3000);
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    run(turbo, at(p, 4000.0, 1.0), 3.0);
    const double early = turbo.state().turbineInK;
    run(turbo, at(p, 4000.0, 1.0), 120.0);
    const double late = turbo.state().turbineInK;
    MESSAGE("devant la turbine : " << early - 273.15 << " °C après 3 s, " << late - 273.15 << " °C après 2 min");
    CHECK(late > early + 30.0);
    // La chaleur va à la paroi : plusieurs dizaines de secondes, pour un kilo d'acier par mètre.
    run(turbo, at(p, 4000.0, 1.0), 240.0);
    CHECK(turbo.state().turbineInK == doctest::Approx(late).epsilon(0.05));
}

TEST_CASE("les bouffées s'atténuent avec le volume du tube, et d'autant plus vite que le moteur tourne vite") {
    const auto pulse = [](int mm, double rpm) {
        const TurboPipeSpec pipe = turboSpecOf(piped(mm)).pipe;
        const TurboSpec spec = turboSpecOf(piped(mm));
        return turboPipePulseShare(pipe, rpm, 1.5e5, 1.1e5, 1000.0, spec.turbineK);
    };
    CHECK(pulse(150, 2000.0) > 1.0);
    CHECK(pulse(1000, 2000.0) < 1.0);
    CHECK(pulse(3000, 2000.0) < pulse(1000, 2000.0));
    CHECK(pulse(3000, 2000.0) > 0.0);
    CHECK(pulse(3000, 6000.0) < pulse(3000, 2000.0));
}

TEST_CASE("déporté, le compresseur part avec le turbo : le retour d'air s'allonge, pas sous la référence") {
    const TurboSpec reference = turboSpecOf(piped(400));
    const TurboSpec rear = turboSpecOf(piped(3000));
    const TurboSpec close = turboSpecOf(piped(150));
    CHECK(rear.chargeVolumeM3 > 1.5 * reference.chargeVolumeM3);
    CHECK(rear.ductInertance > reference.ductInertance);
    CHECK(rear.coolerDrop > reference.coolerDrop);
    CHECK(close.chargeVolumeM3 == reference.chargeVolumeM3);
    CHECK(close.ductInertance == reference.ductInertance);
}

TEST_CASE("le point d'équilibre du tube retrouve ce que la simulation atteint, parois chaudes") {
    for (const int mm : {150, 1000, 3000}) {
        const Powertrain p = piped(mm);
        const TurboSpec spec = turboSpecOf(p);
        for (const double rpm : {3000.0, 5000.0}) {
            INFO(mm << " mm à " << rpm);
            TurboCharger turbo;
            turbo.setSpec(spec);
            run(turbo, at(p, rpm, 1.0), 200.0);
            const TurboSteady steady = turboSteady(spec, rpm, breathingOf(p, rpm));
            CHECK(turbo.boost() == doctest::Approx(steady.boost).epsilon(0.06));
            CHECK(turbo.state().shaftOmega * 60.0 / (2.0 * 3.141592653589793) == doctest::Approx(steady.shaftRpm).epsilon(0.13));
        }
    }
}

TEST_CASE("changer la longueur en marche n'arrête pas l'arbre et ne refroidit pas d'un coup") {
    const Powertrain p = piped(400);
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    run(turbo, at(p, 4500.0, 1.0), 6.0);
    const double omega = turbo.state().shaftOmega;
    const double gas = turbo.state().turbineInK;
    turbo.setSpec(turboSpecOf(piped(2000)));
    turbo.step(kStep, at(p, 4500.0, 1.0));
    CHECK(turbo.state().shaftOmega == doctest::Approx(omega).epsilon(0.01));
    CHECK(turbo.state().turbineInK == doctest::Approx(gas).epsilon(0.01));
    for (int i = 0; i < 4000; ++i) {
        turbo.step(kStep, at(p, 4500.0, 1.0));
        REQUIRE(std::isfinite(turbo.state().turbineInK));
        REQUIRE(std::isfinite(turbo.state().turbineInPa));
        REQUIRE(std::isfinite(turbo.state().shaftOmega));
    }
}

TEST_CASE("sans turbo, le tube ne change rien") {
    Powertrain a = findPreset("city_14")->powertrain();
    Powertrain b = a;
    b.engine.turboPipeMm = 3000;
    CHECK(torqueCurveNm(a, 4000.0) == torqueCurveNm(b, 4000.0));
    DrivingModel ma(a);
    DrivingModel mb(b);
    for (int i = 0; i < 500; ++i) {
        ma.stepPedal(0.01, 1.0, 0.0);
        mb.stepPedal(0.01, 1.0, 0.0);
    }
    CHECK(ma.rpm() == mb.rpm());
    CHECK(ma.speedKmh() == mb.speedKmh());
}

TEST_CASE("le pas fixe ne dépend pas de la durée demandée, tube déporté compris") {
    const Powertrain p = piped(2000);
    DrivingModel coarse(p);
    DrivingModel fine(p);
    for (int i = 0; i < 400; ++i) coarse.stepPedal(0.008, 1.0, 0.0);
    for (int i = 0; i < 1600; ++i) fine.stepPedal(0.002, 1.0, 0.0);
    CHECK(coarse.boost() == doctest::Approx(fine.boost()).epsilon(1e-3));
    CHECK(coarse.turbo().state().turbineInK == doctest::Approx(fine.turbo().state().turbineInK).epsilon(1e-3));
}

TEST_CASE("le pompage ralentit quand le retour d'air s'allonge") {
    const auto surgeHz = [](int mm) {
        Powertrain p = piped(mm);
        p.engine.turboRelease = TurboRelease::Flutter;
        TurboCharger turbo;
        turbo.setSpec(turboSpecOf(p));
        run(turbo, at(p, 4500.0, 1.0), 6.0);
        int reversals = 0;
        bool wasForward = true;
        constexpr double kSeconds = 1.5;
        for (int i = 0; i < static_cast<int>(kSeconds / kStep); ++i) {
            turbo.step(kStep, at(p, 4500.0, 0.05, false));
            const bool forward = turbo.state().compressorFlow > 0.0;
            if (forward && !wasForward) ++reversals;
            wasForward = forward;
        }
        return static_cast<double>(reversals) / kSeconds;
    };
    const double reference = surgeHz(400);
    const double rear = surgeHz(3000);
    MESSAGE("pompage : " << reference << " Hz à 400 mm, " << rear << " Hz à 3000 mm");
    CHECK(rear < reference);
    CHECK(rear > 0.0);
}

TEST_CASE("la pression avant la turbine met le temps de remplir le volume du tube") {
    const auto riseMs = [](int mm) {
        const Powertrain p = piped(mm);
        TurboCharger turbo;
        turbo.setSpec(turboSpecOf(p));
        run(turbo, at(p, 3000.0, 0.2), 90.0);
        const double from = turbo.state().turbineInPa;
        TurboCharger settled = turbo;
        run(settled, at(p, 3000.0, 1.0), 0.15);
        const double wanted = from + 0.63 * (settled.state().turbineInPa - from);
        for (int i = 0; i < 150; ++i) {
            turbo.step(kStep, at(p, 3000.0, 1.0));
            if (turbo.state().turbineInPa >= wanted) return static_cast<double>(i);
        }
        return 150.0;
    };
    const double reference = riseMs(400);
    const double rear = riseMs(3000);
    MESSAGE("63 % de la montée de pression avant la turbine : " << reference << " ms à 400 mm, " << rear << " ms à 3000 mm");
    CHECK(rear > reference + 3.0);
    // Un volume de quelques litres se remplit en quelques dizaines de millisecondes : ce n'est pas lui, le gros du retard.
    CHECK(rear < 120.0);
}

TEST_CASE("le gaz met le temps de traverser le tube, d'autant plus que le débit est faible") {
    // La paroi et l'entrée sont tenues : seul le temps de parcours sépare encore la sortie de sa valeur finale.
    const auto advance = [](const TurboPipeSpec& pipe, TurboPipeState& state, double gas) {
        state.wallK = 900.0;
        state.portK = 1000.0;
        return state.stepHeat(pipe, kStep, gas, 1000.0, 1.3e5, 0.35, 0.9);
    };
    const auto riseMs = [&advance](double gas) {
        const TurboPipeSpec pipe = turboSpecOf(piped(3000)).pipe;
        TurboPipeState settled;
        settled.outK = 900.0;
        double last = 0.0;
        for (int i = 0; i < 20000; ++i) last = advance(pipe, settled, gas);
        TurboPipeState state;
        state.outK = 900.0;
        for (int i = 0; i < 20000; ++i) {
            if (advance(pipe, state, gas) >= 900.0 + 0.63 * (last - 900.0)) return static_cast<double>(i);
        }
        return 20000.0;
    };
    const double slow = riseMs(0.02);
    const double fast = riseMs(0.2);
    MESSAGE("le gaz à la sortie du tube : " << slow << " ms à 20 g/s, " << fast << " ms à 200 g/s");
    // 2,6 m de 50 mm à 1,3 bar et 900 K : 2,6 g de gaz, soit 130 ms et 13 ms.
    CHECK(slow == doctest::Approx(130.0).epsilon(0.25));
    CHECK(fast == doctest::Approx(13.0).epsilon(0.25));
}

TEST_CASE("les bouffées perdues comptent à bas régime : moins de pression tenue tout en bas") {
    double previous = 1e9;
    for (const int mm : kLengths) {
        const Powertrain p = piped(mm);
        const double boost = turboSteady(turboSpecOf(p), 1700.0, breathingOf(p, 1700.0)).boost;
        INFO(mm << " mm : " << boost);
        CHECK(boost <= previous);
        previous = boost;
    }
    CHECK(previous < steadyBoost(piped(400), 1700.0));
}

TEST_CASE("la puissance maximale bouge à peine, comme sur un vrai turbo déporté") {
    const double reference = powerSummary(piped(400)).peakPowerKw;
    const double rear = powerSummary(piped(3000)).peakPowerKw;
    // Le frottement du tube se paie un peu, et un tube plus court soulage aussi un peu.
    CHECK(rear < reference);
    CHECK(rear > 0.97 * reference);
    CHECK(powerSummary(piped(150)).peakPowerKw > reference);
    // La facture est dans la réponse : le 0-100 mesuré, retard compris.
    CHECK(measurePerformance(piped(3000)).zeroTo100S > measurePerformance(piped(400)).zeroTo100S);
    CHECK(measurePerformance(piped(150)).zeroTo100S < measurePerformance(piped(400)).zeroTo100S);
}

TEST_CASE("sur un V8, la pleine pression arrive quelques centaines de tours plus tard, comme à l'essai") {
    // R. Holdener (EngineLabs), V8 4,8 L biturbo à 0,48 bar, 3,66 m d'échappement en plus : environ 500 tr/min. Un ordre de grandeur.
    const auto fullBoostRpm = [](int mm) {
        Powertrain p = piped(mm, "v8_cross_plane");
        p.engine.aspiration = Aspiration::Turbo;
        p.engine.displacementCc = 4800;
        p.engine.boostBar = 0.48f;
        p.engine.turboSize = TurboSize::Large;
        for (double rpm = 1000.0; rpm < 8000.0; rpm += 50.0) {
            if (steadyBoost(p, rpm) >= 0.9) return rpm;
        }
        return 0.0;
    };
    const double later = fullBoostRpm(3000) - fullBoostRpm(400);
    MESSAGE("90 % de la pression " << later << " tr/min plus tard avec 2,6 m de tube en plus");
    CHECK(later > 150.0);
    CHECK(later < 1000.0);
}
