#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/turbo.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kStep = 0.001;
constexpr double kPi = 3.141592653589793;

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

Powertrain sized(const char* id, TurboSize size) {
    Powertrain p = preset(id);
    p.engine.turboSize = size;
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

/** Un coup de gaz à régime tenu, depuis une conduite tranquille : le temps pour atteindre 90 % de la pression établie. */
double stabSeconds(const Powertrain& p, double rpm, const TurboSpec* spec = nullptr) {
    const TurboSpec s = spec != nullptr ? *spec : turboSpecOf(p);
    TurboCharger turbo;
    turbo.setSpec(s);
    run(turbo, at(p, rpm, 0.2), 8.0);
    const double wanted = 0.9 * turboSteady(s, rpm, breathingOf(p, rpm)).boost;
    for (int i = 0; i < 30000; ++i) {
        turbo.step(kStep, at(p, rpm, 1.0));
        if (turbo.boost() >= wanted) return static_cast<double>(i) * kStep;
    }
    return 30.0;
}

}  // namespace

TEST_CASE("l'arbre intègre le bilan de puissance") {
    const Powertrain p = preset("hot_hatch_turbo");
    const TurboSpec spec = turboSpecOf(p);
    TurboCharger turbo;
    turbo.setSpec(spec);
    run(turbo, at(p, 3500.0, 1.0), 0.35);
    for (int i = 0; i < 300; ++i) {
        const double before = turbo.state().shaftOmega;
        turbo.step(kStep, at(p, 3500.0, 1.0));
        const TurboState& s = turbo.state();
        const double friction = spec.frictionW * (before / spec.referenceOmega) * (before / spec.referenceOmega);
        const double net = s.turbinePowerW - s.compressorPowerW - friction;
        // J·ω·dω/dt = P : en ω², c'est exact à chaque pas.
        const double wanted = before * before + 2.0 * net / spec.inertia * kStep;
        REQUIRE(s.shaftOmega * s.shaftOmega == doctest::Approx(wanted).epsilon(1e-9));
    }
}

TEST_CASE("le retard est long à bas régime, court à haut régime") {
    const Powertrain p = preset("hot_hatch_turbo");
    const double at2000 = stabSeconds(p, 2000.0);
    const double at3000 = stabSeconds(p, 3000.0);
    const double at4500 = stabSeconds(p, 4500.0);
    MESSAGE("90 % de la pression : " << at2000 << " s à 2000, " << at3000 << " s à 3000, " << at4500 << " s à 4500 tr/min");
    CHECK(at2000 > 1.6 * at3000);
    CHECK(at3000 > 1.3 * at4500);
    // Ordres de grandeur publiés sur un 2,0 L : autour de 2,8 s à 2000 tr/min, moins d'une seconde en haut.
    CHECK(at2000 > 1.5);
    CHECK(at2000 < 5.0);
    CHECK(at4500 < 1.0);
    CHECK(at4500 > 0.2);
}

TEST_CASE("plus d'inertie, un arbre plus lent à lancer") {
    const Powertrain p = preset("hot_hatch_turbo");
    TurboSpec heavy = turboSpecOf(p);
    const double light = stabSeconds(p, 3000.0, &heavy);
    heavy.inertia *= 2.0;
    const double slow = stabSeconds(p, 3000.0, &heavy);
    CHECK(slow > 1.6 * light);
}

TEST_CASE("sans énergie dans l'échappement, pas de pression") {
    const Powertrain p = preset("hot_hatch_turbo");
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    // Le moteur tourne, mais rien n'y brûle.
    run(turbo, at(p, 3000.0, 1.0, false), 5.0);
    const double cold = turbo.boost();
    CHECK(cold < 0.1);
    CHECK(turbo.state().shaftOmega * 60.0 / (2.0 * kPi) < 80000.0);
    // Au ralenti, à peine plus.
    TurboCharger idle;
    idle.setSpec(turboSpecOf(p));
    run(idle, at(p, 850.0, 0.15), 8.0);
    CHECK(idle.boost() < 0.03);
    // Moteur à l'arrêt, plus rien du tout.
    TurboCharger off;
    off.setSpec(turboSpecOf(p));
    run(off, at(p, 0.0, 0.0, false), 2.0);
    CHECK(off.state().shaftOmega == 0.0);
    CHECK(std::fabs(off.boost()) < 1e-6);
}

TEST_CASE("pied levé, l'arbre garde son élan") {
    const auto lift = [](TurboRelease release) {
        Powertrain p = preset("hot_hatch_turbo");
        p.engine.turboRelease = release;
        TurboCharger turbo;
        turbo.setSpec(turboSpecOf(p));
        run(turbo, at(p, 4000.0, 1.0), 4.0);
        const double spinning = turbo.state().shaftOmega;
        run(turbo, at(p, 4000.0, 0.05, false), 0.3);
        return turbo.state().shaftOmega / spinning;
    };
    const double dump = lift(TurboRelease::DumpValve);
    const double flutter = lift(TurboRelease::Flutter);
    MESSAGE("après 0,3 s pied levé : " << dump << " (décharge), " << flutter << " (flutter)");
    CHECK(dump > 0.3);
    CHECK(flutter > dump);
    CHECK(flutter > 0.75);
}

TEST_CASE("reprendre juste après un lever de pied va plus vite") {
    for (const TurboRelease release : {TurboRelease::DumpValve, TurboRelease::Flutter}) {
        Powertrain p = preset("hot_hatch_turbo");
        p.engine.turboRelease = release;
        const TurboSpec spec = turboSpecOf(p);
        const double rpm = 3500.0;
        TurboCharger turbo;
        turbo.setSpec(spec);
        run(turbo, at(p, rpm, 1.0), 4.0);
        run(turbo, at(p, rpm, 0.05, false), 0.25);
        const double wanted = 0.9 * turboSteady(spec, rpm, breathingOf(p, rpm)).boost;
        double again = 30.0;
        for (int i = 0; i < 30000; ++i) {
            turbo.step(kStep, at(p, rpm, 1.0));
            if (turbo.boost() >= wanted) {
                again = static_cast<double>(i) * kStep;
                break;
            }
        }
        const double fromCruise = stabSeconds(p, rpm);
        MESSAGE("90 % : " << fromCruise << " s depuis une conduite tranquille, " << again << " s après un lever de 0,25 s");
        CHECK(again < 0.75 * fromCruise);
    }
}

TEST_CASE("la soupape de décharge tient la pression demandée, et une cible plus haute arrive plus tard") {
    for (const float bar : {0.8f, 1.3f, 2.0f}) {
        Powertrain p = preset("hot_hatch_turbo");
        p.engine.boostBar = bar;
        for (const double rpm : {3500.0, 5000.0, 6000.0}) {
            TurboCharger turbo;
            turbo.setSpec(turboSpecOf(p));
            run(turbo, at(p, rpm, 1.0), 6.0);
            INFO(bar << " bar à " << rpm);
            double low = 1e9, high = -1e9;
            for (int i = 0; i < 2000; ++i) {
                turbo.step(kStep, at(p, rpm, 1.0));
                low = std::min(low, turbo.boost());
                high = std::max(high, turbo.boost());
            }
            // Une soupape à ressort tient à quelques pour cent près : plus haut en régime, plus il y a à jeter.
            CHECK(low > 0.9);
            CHECK(high < 1.13);
            CHECK(high - low < 0.23);
            CHECK(turbo.state().wastegate > 0.05);
        }
    }
    // À cylindrée égale, plus de pression demande un plus gros compresseur : il tarde davantage.
    Powertrain mild = preset("hot_hatch_turbo");
    mild.engine.boostBar = 0.7f;
    Powertrain hard = mild;
    hard.engine.boostBar = 2.2f;
    CHECK(stabSeconds(hard, 3500.0) > stabSeconds(mild, 3500.0));
    CHECK(turboSpecOf(hard).wheelDiameterM > turboSpecOf(mild).wheelDiameterM);
}

TEST_CASE("le point d'équilibre calculé retrouve celui que la simulation atteint") {
    for (const char* id : {"hot_hatch_turbo", "three_cyl_turbo", "five_cyl_turbo"}) {
        const Powertrain p = preset(id);
        const TurboSpec spec = turboSpecOf(p);
        for (const double rpm : {2500.0, 3500.0, 5000.0}) {
            INFO(id << " à " << rpm);
            TurboCharger turbo;
            turbo.setSpec(spec);
            run(turbo, at(p, rpm, 1.0), 12.0);
            const TurboSteady steady = turboSteady(spec, rpm, breathingOf(p, rpm));
            CHECK(turbo.boost() == doctest::Approx(steady.boost).epsilon(0.06));
            CHECK(turbo.state().shaftOmega * 60.0 / (2.0 * kPi) == doctest::Approx(steady.shaftRpm).epsilon(0.13));
        }
    }
    // Sous le seuil aussi, là où l'énergie ou le pompage bornent la pression : c'est le bilan qui la fixe, pas la soupape.
    // Fusion des finitions : à 2500 tr/min, le gros turbo pompe. La simulation passe la limite de pompage, s'effondre, et remonte,
    // en un cycle de 2 à 5 s ; l'équilibre calculé, que cette limite borne (`kHoldX`), est la pression où elle retombe. On compare
    // donc au plus bas des 10 dernières secondes, qui ne dépend pas de la phase du cycle : la pression à la 20e seconde en
    // dépendait, et la masse du faisceau ε-NTU, qui allonge le cycle, l'avait fait passer de 0,55 à 0,91.
    const Powertrain large = sized("hot_hatch_turbo", TurboSize::Large);
    const TurboSpec spec = turboSpecOf(large);
    for (const double rpm : {2000.0, 2500.0}) {
        INFO("gros turbo à " << rpm);
        TurboCharger turbo;
        turbo.setSpec(spec);
        const TurboInput in = at(large, rpm, 1.0);
        run(turbo, in, 10.0);
        double lowest = turbo.boost();
        for (int i = 0; i < 10000; ++i) {
            turbo.step(kStep, in);
            lowest = std::min(lowest, turbo.boost());
        }
        const TurboSteady steady = turboSteady(spec, rpm, breathingOf(large, rpm));
        MESSAGE("gros turbo à " << rpm << " tr/min : " << steady.boost << " à l'équilibre calculé, " << lowest << " au plus bas, " << turbo.boost() << " à la fin");
        CHECK(steady.boost < 0.5);
        CHECK(lowest == doctest::Approx(steady.boost).epsilon(0.25));
    }
}

TEST_CASE("un petit turbo se réveille tôt et s'essouffle, un gros tarde et donne plus") {
    const Powertrain small = sized("hot_hatch_turbo", TurboSize::Small);
    const Powertrain medium = sized("hot_hatch_turbo", TurboSize::Medium);
    const Powertrain large = sized("hot_hatch_turbo", TurboSize::Large);
    CHECK(turboSpecOf(small).inertia < turboSpecOf(medium).inertia);
    CHECK(turboSpecOf(large).inertia > 2.0 * turboSpecOf(medium).inertia);
    // Retard, régime tenu.
    CHECK(stabSeconds(small, 3000.0) < stabSeconds(medium, 3000.0));
    CHECK(stabSeconds(large, 3000.0) > 1.5 * stabSeconds(medium, 3000.0));
    // Le couple en bas, la puissance en haut.
    CHECK(torqueCurveNm(small, 1800.0) > 1.3 * torqueCurveNm(medium, 1800.0));
    CHECK(torqueCurveNm(large, 2200.0) < 0.8 * torqueCurveNm(medium, 2200.0));
    const double top = powerSummary(medium).peakPowerKw;
    CHECK(powerSummary(small).peakPowerKw < top);
    CHECK(powerSummary(large).peakPowerKw > top);
}

TEST_CASE("les roues ne dépassent jamais la vitesse qu'elles supportent") {
    // Un petit turbo, à qui l'on demande beaucoup : à bout de débit, il tourne de plus en plus vite pour rien.
    Powertrain small = sized("hot_hatch_turbo", TurboSize::Small);
    small.engine.boostBar = 2.0f;
    const TurboSpec spec = turboSpecOf(small);
    TurboCharger turbo;
    turbo.setSpec(spec);
    double top = 0.0;
    for (int i = 0; i < 8000; ++i) {
        turbo.step(kStep, at(small, 6400.0, 1.0));
        top = std::max(top, turbo.state().shaftOmega * 0.5 * spec.wheelDiameterM);
    }
    CHECK(top < spec.tipLimit * 1.12);
}

TEST_CASE("les arbres tournent aux vitesses de vrais turbos") {
    for (const char* id : {"hot_hatch_turbo", "three_cyl_turbo", "five_cyl_turbo", "boxer_four_turbo", "diesel_four"}) {
        const Powertrain p = preset(id);
        const double redline = static_cast<double>(p.engine.redlineRpm);
        const TurboSteady steady = turboSteady(turboSpecOf(p), 0.85 * redline, breathingOf(p, 0.85 * redline));
        CAPTURE(steady.shaftRpm);
        CAPTURE(id);
        CHECK(steady.shaftRpm > 100000.0);
        CHECK(steady.shaftRpm < 300000.0);
    }
}

TEST_CASE("le couple suit la vraie pression, pas la pédale") {
    Powertrain p = preset("hot_hatch_turbo");
    DrivingModel model(p);
    model.setManual(true);
    model.requestShift(1);
    while (model.gear() < 4) {
        model.stepPedal(0.01, 0.6, 0.0);
        if (!model.shifting() && model.rpm() > 3000.0 && model.gear() < 4) model.requestShift(1);
    }
    // On laisse le régime redescendre vers 2500, pied léger.
    for (int i = 0; i < 2000 && model.rpm() > 2500.0; ++i) model.stepPedal(0.01, 0.0, 0.1);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, model.rpm() < 2500.0 ? 0.2 : 0.0, 0.0);
    const double start = model.rpm();
    REQUIRE(model.boost() < 0.2);
    // 0,25 s plus tard, le papillon est grand ouvert : le moteur tire son couple atmosphérique, pas plus.
    for (int i = 0; i < 25; ++i) model.stepPedal(0.01, 1.0, 0.0);
    const double earlyTorque = model.torqueNm();
    const double earlyBoost = model.boost();
    CHECK(earlyBoost < 0.3);
    double lateTorque = 0.0;
    for (int i = 0; i < 300; ++i) {
        model.stepPedal(0.01, 1.0, 0.0);
        lateTorque = std::max(lateTorque, model.torqueNm());
    }
    MESSAGE("à " << start << " tr/min : " << earlyTorque << " Nm à " << earlyBoost << " de pression, puis " << lateTorque << " Nm à " << model.boost());
    CHECK(model.boost() > 0.9);
    CHECK(lateTorque > 1.6 * earlyTorque);
    // Le couple atteint au final, c'est celui de la fiche.
    CHECK(lateTorque == doctest::Approx(torqueCurveNm(p, model.rpm())).epsilon(0.1));
}

TEST_CASE("les performances restent plausibles, retard compris") {
    struct Row {
        const char* id;
        double from, to;
    };
    for (const Row& row : {Row{"three_cyl_turbo", 8.5, 12.0}, Row{"hot_hatch_turbo", 5.5, 8.0}, Row{"five_cyl_turbo", 4.2, 6.2}, Row{"diesel_four", 11.0, 16.0}}) {
        const Performance perf = measurePerformance(preset(row.id));
        INFO(row.id << " : " << perf.zeroTo100S << " s, " << perf.topSpeedKmh << " km/h");
        CHECK(perf.zeroTo100S > row.from);
        CHECK(perf.zeroTo100S < row.to);
    }
}

TEST_CASE("le soufflage de décharge vient de la pression, et rien ne s'ouvre à pleine charge") {
    Powertrain p = preset("hot_hatch_turbo");
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    run(turbo, at(p, 4500.0, 1.0), 4.0);
    CHECK(turbo.state().blowOff == doctest::Approx(0.0));
    // Papillon fermé, sous pression : elle s'ouvre, et le conduit se vide en une fraction de seconde.
    run(turbo, at(p, 4500.0, 0.05, false), 0.05);
    CHECK(turbo.state().blowOff > 0.8);
    CHECK(turbo.state().blowOffFlow > 0.5 * turbo.spec().ratedFlow);
    CHECK(turbo.state().compressorFlow > 0.0);
    run(turbo, at(p, 4500.0, 0.05, false), 0.8);
    CHECK(turbo.boost() < 0.15);
    run(turbo, at(p, 4500.0, 0.05, false), 4.0);
    CHECK(turbo.state().blowOff < 0.1);
    // Pas de pression à vider : elle reste fermée, même sous le vide du ralenti.
    TurboCharger idle;
    idle.setSpec(turboSpecOf(p));
    run(idle, at(p, 850.0, 0.15), 4.0);
    CHECK(idle.state().blowOff == doctest::Approx(0.0));
}

TEST_CASE("sans soupape, l'air revient par le compresseur, par à-coups : le flutter") {
    Powertrain p = preset("hot_hatch_turbo");
    p.engine.turboRelease = TurboRelease::Flutter;
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    run(turbo, at(p, 4500.0, 1.0), 4.0);
    int reversals = 0;
    double back = 0.0;
    bool wasForward = true;
    constexpr double kSeconds = 1.5;
    for (int i = 0; i < static_cast<int>(kSeconds / kStep); ++i) {
        turbo.step(kStep, at(p, 4500.0, 0.05, false));
        const double flow = turbo.state().compressorFlow;
        back = std::min(back, flow);
        const bool forward = flow > 0.0;
        if (forward && !wasForward) ++reversals;
        wasForward = forward;
        REQUIRE(turbo.state().blowOff == 0.0);
    }
    const double hz = static_cast<double>(reversals) / kSeconds;
    MESSAGE("pompage : " << hz << " Hz, " << back << " kg/s au plus fort du retour");
    CHECK(back < -0.1 * turbo.spec().ratedFlow);
    // Mesuré sur de vrais turbos : de 5 à 10 Hz (pompage léger), selon la tuyauterie ; un peu plus sur un volume plus petit.
    CHECK(hz > 3.0);
    CHECK(hz < 30.0);
}

TEST_CASE("le rupteur n'ouvre aucune soupape") {
    DrivingModel model(preset("hot_hatch_turbo"));
    for (int i = 0; i < 400; ++i) model.stepNeutral(0.01, 1.0);
    for (int i = 0; i < 1000; ++i) {
        model.stepNeutral(0.001, 1.0);
        REQUIRE(model.turbo().state().blowOff < 0.02);
    }
    for (int i = 0; i < 100; ++i) model.stepNeutral(0.01, 0.0);
    CHECK(model.turbo().state().blowOffFlow >= 0.0);
}

TEST_CASE("le compresseur chauffe l'air, l'échangeur le refroidit, et la pression s'y perd") {
    const Powertrain p = preset("hot_hatch_turbo");
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    run(turbo, at(p, 5500.0, 1.0), 10.0);
    const TurboState& s = turbo.state();
    const double outC = s.compressorOutK - 273.15;
    const double coolC = s.coolerOutK - 273.15;
    MESSAGE("sortie du compresseur " << outC << " °C, de l'échangeur " << coolC << " °C ; " << (s.outletPa - s.chargePa) / 1e5 << " bar perdus");
    // Sur la route : de 110 à 170 °C, de 35 à 60 °C, de 0,1 à 0,2 bar.
    CHECK(outC > 100.0);
    CHECK(outC < 185.0);
    CHECK(coolC < outC - 50.0);
    CHECK(coolC < 70.0);
    CHECK((s.outletPa - s.chargePa) / 1e5 > 0.05);
    CHECK((s.outletPa - s.chargePa) / 1e5 < 0.25);
}

TEST_CASE("sans turbo, rien de tout cela n'existe") {
    for (const char* id : {"city_14", "v8_cross_plane", "electric"}) {
        const Powertrain p = preset(id);
        DrivingModel model(p);
        for (int i = 0; i < 500; ++i) model.stepPedal(0.01, 1.0, 0.0);
        CHECK(steadyBoost(p, 4000.0) == 0.0);
        CHECK(model.boost() == 0.0);
        CHECK(model.turbo().state().shaftOmega == 0.0);
        CHECK(model.turboDrive().shaftHz == 0.0f);
    }
}

TEST_CASE("le démarreur, le calage et la voiture électrique ne sont pas touchés") {
    Powertrain p = preset("hot_hatch_turbo");
    DrivingModel model(p);
    model.halt();
    // Le démarreur fait son travail comme sur n'importe quel moteur.
    model.crank();
    double top = 0.0;
    for (int i = 0; i < 400; ++i) {
        model.stepNeutral(0.01, 0.0);
        top = std::max(top, model.rpm());
    }
    CHECK(top > model.idleRpm() * 0.9);
    CHECK_FALSE(model.cranking());
    // Calé en manuelle, l'arbre continue un moment, et la pression retombe.
    DrivingModel stall(p);
    stall.setManual(true);
    stall.setClutchAssist(false);
    for (int i = 0; i < 200; ++i) stall.stepNeutral(0.01, 1.0);
    const double spinning = stall.turbo().state().shaftOmega;
    stall.halt();
    for (int i = 0; i < 100; ++i) stall.stepOff(0.01);
    CHECK(stall.turbo().state().shaftOmega > 0.0);
    CHECK(stall.turbo().state().shaftOmega < spinning);
}

TEST_CASE("le pas fixe ne dépend pas de la durée demandée") {
    const Powertrain p = preset("hot_hatch_turbo");
    DrivingModel coarse(p);
    DrivingModel fine(p);
    for (int i = 0; i < 400; ++i) coarse.stepPedal(0.008, 1.0, 0.0);
    for (int i = 0; i < 1600; ++i) fine.stepPedal(0.002, 1.0, 0.0);
    // À un pas de 1 ms près, selon comment les durées s'additionnent en virgule flottante.
    CHECK(coarse.boost() == doctest::Approx(fine.boost()).epsilon(1e-3));
    CHECK(coarse.turbo().state().shaftOmega == doctest::Approx(fine.turbo().state().shaftOmega).epsilon(1e-3));
    CHECK(coarse.turbo().state().chargePa == doctest::Approx(fine.turbo().state().chargePa).epsilon(1e-3));
}

namespace {

/** Le son d'un turbo décrit : tout passe par `setTurbo`, par tranches de 256 échantillons. */
Samples renderTurbo(const EngineConfig& config, int block, const std::function<TurboDrive(int)>& drive, int updates = 400) {
    EngineSound core(kRate);
    core.setProfile(config.toProfile());
    core.setVolume(0.7f);
    core.setListener(Listener::Front);
    Samples out(static_cast<std::size_t>(updates) * 256);
    for (int u = 0; u < updates; ++u) {
        core.setTargetRpm(4000.0f);
        core.setDrive(0.9f, 0.9f, false, false, 0.9f);
        core.setTurbo(drive(u));
        for (int done = 0; done < 256; done += block) core.render(out.data() + static_cast<std::size_t>(u) * 256 + done, block);
    }
    return out;
}

TurboDrive spinning(float shaftHz) {
    TurboDrive d;
    d.shaftHz = shaftHz;
    d.shaftShare = shaftHz / 2500.0f;
    d.flowShare = 0.8f;
    return d;
}

EngineConfig turboConfig() {
    EngineConfig c;
    c.aspiration = Aspiration::Turbo;
    c.boostBar = 1.3f;
    return c;
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

double band(const Samples& x, double from, double to) {
    double sum = 0.0;
    int count = 0;
    for (double hz = from; hz <= to; hz += 15.0, ++count) sum += powerAt(x, hz);
    return sum / count;
}

}  // namespace

TEST_CASE("le son ne dépend pas de la taille des blocs, turbo compris") {
    const auto drive = [](int u) {
        TurboDrive d = spinning(1200.0f + 5.0f * static_cast<float>(u));
        d.blowOffShare = u > 200 && u < 260 ? 0.8f : 0.0f;
        d.wastegate = u < 150 ? 0.3f : 0.0f;
        d.wastegateFlowShare = u < 150 ? 0.2f : 0.0f;
        return d;
    };
    const Samples a = renderTurbo(turboConfig(), 256, drive);
    const Samples b = renderTurbo(turboConfig(), 64, drive);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
}

TEST_CASE("le rendu d'un turbo n'alloue rien") {
    EngineSound core(kRate);
    core.setProfile(turboConfig().toProfile());
    core.setVolume(0.7f);
    Samples out(4096);
    core.render(out.data(), 2048);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) {
        core.setTargetRpm(4000.0f);
        core.setDrive(0.9f, 0.9f, false, false, 0.9f);
        TurboDrive d = spinning(2000.0f);
        d.blowOffShare = i % 3 == 0 ? 1.0f : 0.0f;
        core.setTurbo(d);
        core.render(out.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}

TEST_CASE("le sifflement est à la vitesse de l'arbre") {
    // Ce qui s'entend d'un arbre : son double, et un peu lui-même. Deux vitesses, deux raies.
    const auto level = [](float shaftHz, double centre) {
        const Samples out = renderTurbo(turboConfig(), 256, [shaftHz](int) { return spinning(shaftHz); });
        const Samples last = slice(out, out.size() - 16384, out.size());
        return band(last, centre - 80.0, centre + 80.0) / band(last, centre + 700.0, centre + 1100.0);
    };
    CHECK(level(1500.0f, 3000.0) > 1.5 * level(2500.0f, 3000.0));
    CHECK(level(2500.0f, 5000.0) > 1.5 * level(1500.0f, 5000.0));
}

TEST_CASE("arbre arrêté, ni sifflement ni décharge") {
    const Samples spun = renderTurbo(turboConfig(), 256, [](int) { return spinning(2200.0f); });
    // Même fréquence demandée, mais un arbre qui ne tourne plus : c'est sa vitesse qui fait le niveau.
    const Samples still = renderTurbo(turboConfig(), 256, [](int) {
        TurboDrive d = spinning(2200.0f);
        d.shaftShare = 0.0f;
        return d;
    });
    const Samples lastSpun = slice(spun, spun.size() - 16384, spun.size());
    const Samples lastStill = slice(still, still.size() - 16384, still.size());
    CHECK(band(lastSpun, 4300.0, 4500.0) > 2.0 * band(lastStill, 4300.0, 4500.0));
}

TEST_CASE("la décharge suit le débit : plus de souffle, plus de bruit") {
    const auto hiss = [](float share) {
        const Samples out = renderTurbo(turboConfig(), 256, [share](int) {
            TurboDrive d = spinning(1500.0f);
            d.blowOffShare = share;
            return d;
        });
        // Le niveau de sortie se resserre : c'est la part de la bande du souffle qui le dit.
        return band(slice(out, out.size() - 8192, out.size()), 1800.0, 4000.0);
    };
    const double none = hiss(0.0f);
    const double half = hiss(0.5f);
    const double full = hiss(1.0f);
    CHECK(full > 1.1 * half);
    CHECK(half > 1.1 * none);
}
