#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/twin_turbo.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kStep = 0.001;
constexpr double kPi = 3.141592653589793;

/** Un six en ligne de 3 L, 1 bar : la base des comparaisons, turbo simple au défaut. */
Powertrain six(TurboLayout layout, bool vanes = false) {
    Powertrain p = findPreset("inline_six")->powertrain();
    EngineConfig& e = p.engine;
    e.cylinders = 6;
    e.layout = Layout::Inline;
    e.displacementCc = 3000;
    e.boreMm = 86;
    e.redlineRpm = 6800;
    e.aspiration = Aspiration::Turbo;
    e.boostBar = 1.0f;
    e.turboLayout = layout;
    e.variableGeometry = vanes;
    return p;
}

/** Le 2JZ-GTE : 2997 cm³, alésage 86, 0,8 bar, deux turbos identiques en séquence. */
Powertrain twoJz() {
    Powertrain p = six(TurboLayout::TwinSequential);
    p.engine.displacementCc = 2997;
    p.engine.boostBar = 0.8f;
    return p;
}

/** Le 1.4 TSI à double suralimentation : 1390 cm³, alésage 76,5, 1,5 bar (2,5 bar absolus), un Roots et un turbo. */
Powertrain tsi() {
    Powertrain p = findPreset("hot_hatch_turbo")->powertrain();
    EngineConfig& e = p.engine;
    e.cylinders = 4;
    e.layout = Layout::Inline;
    e.displacementCc = 1390;
    e.boreMm = 77;
    e.redlineRpm = 7000;
    e.boostBar = 1.5f;
    e.turboLayout = TurboLayout::Twincharger;
    e.superchargerKind = SuperchargerKind::Roots;
    return p;
}

TwinTurboInput at(const Powertrain& p, double rpm, double fill, bool burning = true) {
    TwinTurboInput in;
    in.rpm = rpm;
    in.fill = fill;
    in.breathing = breathingOf(p, rpm);
    in.burning = burning;
    return in;
}

void run(TwinTurbo& twin, const TwinTurboInput& in, double seconds) {
    const int steps = static_cast<int>(seconds / kStep);
    for (int i = 0; i < steps; ++i) twin.step(kStep, in);
}

/** Un coup de gaz à régime tenu, depuis une conduite tranquille : le temps pour atteindre 90 % de la pression demandée. */
double stabSeconds(const Powertrain& p, double rpm, const TwinTurboSpec* given = nullptr) {
    const TwinTurboSpec spec = given != nullptr ? *given : twinTurboSpecOf(p);
    TwinTurbo twin;
    twin.setSpec(spec);
    run(twin, at(p, rpm, 0.2), 8.0);
    for (int i = 0; i < 30000; ++i) {
        twin.step(kStep, at(p, rpm, 1.0));
        if (twin.boost() >= 0.9) return static_cast<double>(i) * kStep;
    }
    return 30.0;
}

/** Le même coup de gaz pour le turbo simple, par `TurboCharger`. */
double singleStabSeconds(const Powertrain& p, double rpm) {
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    TurboInput in;
    in.rpm = rpm;
    in.fill = 0.2;
    in.breathing = breathingOf(p, rpm);
    in.burning = true;
    for (int i = 0; i < 8000; ++i) turbo.step(kStep, in);
    in.fill = 1.0;
    for (int i = 0; i < 30000; ++i) {
        turbo.step(kStep, in);
        if (turbo.boost() >= 0.9) return static_cast<double>(i) * kStep;
    }
    return 30.0;
}

double shaftRpm(const TurboState& s) { return s.shaftOmega * 60.0 / (2.0 * kPi); }

/** Plein gaz en deuxième depuis 1500 tr/min : chaque pas passe à `watch`. */
void pullInSecond(DrivingModel& model, const std::function<void(const DrivingModel&)>& watch, double seconds = 6.0) {
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 3000 && (model.gear() < 2 || model.shifting()); ++i) {
        model.stepPedal(0.01, 0.3, 0.0);
        if (!model.shifting() && model.rpm() > 2500.0 && model.gear() < 2) model.requestShift(1);
    }
    for (int i = 0; i < 2000 && model.rpm() > 1500.0; ++i) model.stepPedal(0.01, 0.0, 0.4);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.15, 0.0);
    const int steps = static_cast<int>(seconds / 0.01);
    for (int i = 0; i < steps && model.rpm() < 0.97 * model.redlineRpm(); ++i) {
        model.stepPedal(0.01, 1.0, 0.0);
        watch(model);
    }
}

}  // namespace

TEST_CASE("au défaut, rien du biturbo ne s'exécute") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    int twins = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        // Le W12 et le W16 (architectures) choisissent leurs deux turbos : une turbine par culasse. Ils sont le biturbo voulu.
        if (p.engine.turboLayout != TurboLayout::Single || p.engine.variableGeometry) {
            ++twins;
            CHECK(usesTwinTurbo(p.engine));
            continue;
        }
        CHECK_FALSE(usesTwinTurbo(p.engine));
        if (p.electric) continue;
        DrivingModel model(p);
        CHECK_FALSE(model.twinTurbo());
    }
    // Un turbo simple à soupape reste `TurboCharger` ; les aubes seules suffisent à passer par le nouveau modèle.
    CHECK(twins == 2);
    CHECK_FALSE(usesTwinTurbo(six(TurboLayout::Single).engine));
    CHECK(usesTwinTurbo(six(TurboLayout::Single, true).engine));
    Powertrain natural = six(TurboLayout::TwinParallel);
    natural.engine.aspiration = Aspiration::Natural;
    CHECK_FALSE(usesTwinTurbo(natural.engine));
}

TEST_CASE("un turbo parmi deux : moitié du débit, roue √2 plus petite, inertie en puissance cinquième") {
    const TwinTurboSpec spec = twinTurboSpecOf(six(TurboLayout::TwinParallel));
    const TurboSpec& unit = spec.unit[0];
    const TurboSpec& single = spec.reference;
    MESSAGE("roue " << single.wheelDiameterM * 1000.0 << " mm, " << single.inertia << " kg·m² seule ; " << unit.wheelDiameterM * 1000.0 << " mm, " << unit.inertia << " kg·m² à deux");
    CHECK(unit.ratedFlow == doctest::Approx(0.5 * single.ratedFlow).epsilon(0.01));
    CHECK(unit.wheelDiameterM / single.wheelDiameterM == doctest::Approx(1.0 / std::sqrt(2.0)).epsilon(0.01));
    // Par similitude : la masse en D³, le rayon de giration en D, l'inertie en D⁵.
    CHECK(unit.inertia / single.inertia == doctest::Approx(std::pow(2.0, -2.5)).epsilon(0.02));
    // Les deux arbres, ensemble, ont encore trois fois moins d'inertie que le seul.
    CHECK(2.0 * unit.inertia < 0.4 * single.inertia);
    // Plus petits, ils tournent plus vite : √2.
    CHECK(unit.referenceOmega / single.referenceOmega == doctest::Approx(std::sqrt(2.0)).epsilon(0.01));
}

TEST_CASE("chaque arbre intègre son propre bilan de puissance") {
    const Powertrain p = six(TurboLayout::TwinParallel);
    TwinTurbo twin;
    twin.setSpec(twinTurboSpecOf(p));
    run(twin, at(p, 3500.0, 1.0), 0.35);
    for (int i = 0; i < 300; ++i) {
        std::array<double, 2> before{twin.state().unit[0].shaftOmega, twin.state().unit[1].shaftOmega};
        twin.step(kStep, at(p, 3500.0, 1.0));
        for (std::size_t u = 0; u < 2; ++u) {
            const TurboSpec& spec = twin.spec().unit[u];
            const TurboState& s = twin.state().unit[u];
            const double friction = spec.frictionW * (before[u] / spec.referenceOmega) * (before[u] / spec.referenceOmega);
            const double wanted = before[u] * before[u] + 2.0 * (s.turbinePowerW - s.compressorPowerW - friction) / spec.inertia * kStep;
            REQUIRE(s.shaftOmega * s.shaftOmega == doctest::Approx(wanted).epsilon(1e-9));
        }
    }
}

TEST_CASE("deux turbos en parallèle se lancent plus vite qu'un seul, à même pression") {
    const Powertrain single = six(TurboLayout::Single);
    const Powertrain parallel = six(TurboLayout::TwinParallel);
    const double one = singleStabSeconds(single, 2500.0);
    const double two = stabSeconds(parallel, 2500.0);
    MESSAGE("90 % de la pression à 2500 tr/min : " << one << " s seul, " << two << " s à deux");
    CHECK(two < 0.85 * one);
    // Contre-épreuve : à inertie égale à celle du turbo simple, le gain disparaît. Il vient de l'inertie, pas d'ailleurs.
    TwinTurboSpec heavy = twinTurboSpecOf(parallel);
    heavy.unit[0].inertia = heavy.reference.inertia;
    heavy.unit[1].inertia = heavy.reference.inertia;
    CHECK(stabSeconds(parallel, 2500.0, &heavy) > one);
}

TEST_CASE("en parallèle, la pression établie est celle du turbo simple") {
    const Powertrain single = six(TurboLayout::Single);
    const Powertrain parallel = six(TurboLayout::TwinParallel);
    for (double rpm : {3000.0, 4500.0, 6000.0}) {
        CHECK(steadyBoost(parallel, rpm) == doctest::Approx(steadyBoost(single, rpm)).epsilon(0.03));
        CHECK(torqueCurveNm(parallel, rpm) == doctest::Approx(torqueCurveNm(single, rpm)).epsilon(0.03));
    }
    // L'équilibre du modèle à pas fixes retrouve celui de la fiche.
    TwinTurbo twin;
    twin.setSpec(twinTurboSpecOf(parallel));
    run(twin, at(parallel, 4500.0, 1.0), 10.0);
    CHECK(twin.boost() == doctest::Approx(steadyBoost(parallel, 4500.0)).epsilon(0.03));
}

TEST_CASE("les arbres du biturbo lisent l'air du jour : en altitude, plus de rapport et des arbres plus rapides") {
    // Finitions : comme le turbo simple (atmosphère), chaque arbre aspire l'air du jour et la soupape à ressort tient une pression
    // relative. Dans l'air de référence, rien ne change au bit près (les empreintes du biturbo n'en bougent pas).
    for (const TurboLayout layout : {TurboLayout::TwinParallel, TurboLayout::TwinSequential, TurboLayout::Twincharger}) {
        CAPTURE(static_cast<int>(layout));
        const Powertrain sea = layout == TurboLayout::Twincharger ? tsi() : six(layout);
        Powertrain high = sea;
        high.engine.altitudeM = 3000;
        const double rpm = 5000.0;
        const TwinTurboSpec low = twinTurboSpecOf(sea);
        const TwinTurboSpec up = twinTurboSpecOf(high);
        CHECK(up.unit[0].ambientPa < 0.75 * low.unit[0].ambientPa);
        const TwinTurboSteady a = twinTurboSteady(low, rpm, breathingOf(sea, rpm));
        const TwinTurboSteady b = twinTurboSteady(up, rpm, breathingOf(high, rpm));
        MESSAGE("à 5000 tr/min, arbre " << a.shaftRpm[0] << " tr/min au niveau de la mer, " << b.shaftRpm[0] << " à 3000 m ; charge " << a.chargePa / 1e5 << " / "
                                        << b.chargePa / 1e5 << " bar");
        CHECK(b.shaftRpm[0] > 1.05 * a.shaftRpm[0]);
        CHECK(b.chargePa < a.chargePa);
        // Le modèle à pas fixes : la pression relative tenue, au-dessus de l'atmosphère de 3000 m.
        TwinTurbo twin;
        twin.setSpec(up);
        run(twin, at(high, rpm, 1.0), 10.0);
        TwinTurbo seaTwin;
        seaTwin.setSpec(low);
        run(seaTwin, at(sea, rpm, 1.0), 10.0);
        MESSAGE("pas fixes : " << (seaTwin.state().unit[0].chargePa - low.reference.ambientPa) / 1e5 << " bar au niveau de la mer, "
                               << (twin.state().unit[0].chargePa - up.reference.ambientPa) / 1e5 << " bar à 3000 m");
        const double gauge = twin.state().unit[0].chargePa - up.reference.ambientPa;
        if (layout != TurboLayout::Twincharger) {
            CHECK(gauge == doctest::Approx(b.chargePa - up.reference.ambientPa).epsilon(0.05));
        } else {
            // L'équilibre du twincharger ignore la perte du volet, que le turbo seul traverse encore : à 3000 m, près de la vitesse
            // limite de ses roues, le modèle à pas fixes tient un peu moins (approximation déjà notée dans twin_turbo.cpp).
            CHECK(gauge < 1.02 * (b.chargePa - up.reference.ambientPa));
            CHECK(gauge > 0.8 * (b.chargePa - up.reference.ambientPa));
        }
    }
}

TEST_CASE("les deux arbres d'un V ne tournent pas tout à fait à la même vitesse") {
    Powertrain v = six(TurboLayout::TwinParallel);
    v.engine.layout = Layout::V;
    const TwinTurboSpec spec = twinTurboSpecOf(v);
    // La rangée aux collecteurs plus longs envoie un gaz un peu moins chaud.
    CHECK(spec.heatShare[0] != spec.heatShare[1]);
    TwinTurbo twin;
    twin.setSpec(spec);
    run(twin, at(v, 2500.0, 1.0), 1.0);
    const double a = shaftRpm(twin.state().unit[0]);
    const double b = shaftRpm(twin.state().unit[1]);
    MESSAGE("à 2500 tr/min, une seconde après le coup de gaz : " << a << " et " << b << " tr/min");
    CHECK(std::fabs(a - b) / a > 0.002);
    CHECK(std::fabs(a - b) / a < 0.05);
}

TEST_CASE("le séquentiel du 2JZ-GTE ouvre son second turbo vers 3700 tr/min, avec un creux de pression") {
    DrivingModel model(twoJz());
    double switchRpm = 0.0;
    double preSpoolRpm = 0.0;
    double before = 0.0;
    double dip = 9.0;
    double secondaryAtSwitch = 0.0;
    double switchedAt = -1.0;
    double clock = 0.0;
    pullInSecond(model, [&](const DrivingModel& m) {
        clock += 0.01;
        const TwinTurboState& s = m.twin().state();
        if (preSpoolRpm == 0.0 && s.stage == 1) preSpoolRpm = m.rpm();
        if (switchRpm == 0.0 && s.stage == 2) {
            switchRpm = m.rpm();
            before = m.boost();
            secondaryAtSwitch = s.unit[1].shaftShare;
            switchedAt = clock;
        }
        if (switchedAt >= 0.0 && clock - switchedAt < 0.6) dip = std::min(dip, m.boost());
    });
    MESSAGE("pré-lancement à " << preSpoolRpm << " tr/min, bascule à " << switchRpm << ", pression " << before << " puis creux à " << dip);
    CHECK(preSpoolRpm > 2900.0);
    CHECK(preSpoolRpm < switchRpm);
    CHECK(switchRpm > 3400.0);
    CHECK(switchRpm < 4100.0);
    // Le secondaire n'est pas encore à sa vitesse : le primaire perd la moitié de son gaz avant que l'autre ne prenne le relais.
    CHECK(secondaryAtSwitch < 0.8);
    CHECK(dip < before - 0.02);
    CHECK(dip > 0.5);
}

TEST_CASE("le même calage de bascule tombe juste sur le 959 et le 13B-REW") {
    const auto switchOf = [](Powertrain p) {
        DrivingModel model(p);
        double rpm = 0.0;
        pullInSecond(model, [&rpm](const DrivingModel& m) {
            if (rpm == 0.0 && m.twin().state().stage == 2) rpm = m.rpm();
        });
        return rpm;
    };
    // Porsche 959 : six à plat de 2849 cm³, 1 bar, le second turbo vers 4000 tr/min.
    Powertrain p959 = six(TurboLayout::TwinSequential);
    p959.engine.layout = Layout::Boxer;
    p959.engine.displacementCc = 2849;
    p959.engine.boreMm = 95;
    p959.engine.redlineRpm = 7600;
    // Mazda 13B-REW : deux rotors de 654 cm³, 0,7 bar, le second turbo entre 4000 et 4500 tr/min.
    Powertrain rew = six(TurboLayout::TwinSequential);
    rew.engine.layout = Layout::Rotary;
    rew.engine.cylinders = 2;
    rew.engine.displacementCc = 1308;
    rew.engine.redlineRpm = 8000;
    rew.engine.boostBar = 0.7f;
    const double porsche = switchOf(p959);
    const double mazda = switchOf(rew);
    MESSAGE("bascule : " << porsche << " tr/min sur le 959, " << mazda << " sur le 13B-REW");
    CHECK(porsche > 3700.0);
    CHECK(porsche < 4600.0);
    CHECK(mazda > 3800.0);
    CHECK(mazda < 4900.0);
}

TEST_CASE("à bas régime, le séquentiel répond comme un petit turbo") {
    const double sequential = stabSeconds(six(TurboLayout::TwinSequential), 2500.0);
    const double parallel = stabSeconds(six(TurboLayout::TwinParallel), 2500.0);
    const double single = singleStabSeconds(six(TurboLayout::Single), 2500.0);
    MESSAGE("90 % à 2500 tr/min : " << sequential << " s en séquence, " << parallel << " s en parallèle, " << single << " s seul");
    CHECK(sequential < 0.6 * parallel);
    CHECK(parallel < single);
}

TEST_CASE("le twincharger embraye son compresseur en bas et le coupe au-delà de son régime") {
    const Powertrain p = tsi();
    const TwinTurboSpec spec = twinTurboSpecOf(p);
    MESSAGE("le turbo tient seul dès " << spec.turboAloneRpm << " tr/min ; coupure à " << spec.clutchOffRpm << " ; rapport " << spec.charger.driveRatio << ", " << spec.charger.rotorVolumeM3 * 1000.0 << " L par tour");
    // Ordres de grandeur du 1.4 TSI : 2400 et 3500 tr/min, un Roots de l'ordre d'un demi-litre.
    CHECK(spec.turboAloneRpm > 1700.0);
    CHECK(spec.turboAloneRpm < 3000.0);
    CHECK(spec.clutchOffRpm > 2500.0);
    CHECK(spec.clutchOffRpm < 4200.0);
    CHECK(spec.charger.rotorVolumeM3 > 0.2e-3);
    CHECK(spec.charger.rotorVolumeM3 < 1.0e-3);
    DrivingModel model(p);
    bool engagedLow = false;
    bool engagedHigh = false;
    double earlyBoost = 0.0;
    double earlyRpm = 0.0;
    double clock = 0.0;
    pullInSecond(model, [&](const DrivingModel& m) {
        const TwinTurboState& s = m.twin().state();
        clock += 0.01;
        if (m.rpm() < 1800.0 && s.clutch) engagedLow = true;
        if (m.rpm() > spec.clutchOffRpm * 1.02 && s.clutch) engagedHigh = true;
        if (clock <= 0.6 && m.boost() > earlyBoost) {
            earlyBoost = m.boost();
            earlyRpm = m.rpm();
        }
    });
    CHECK(engagedLow);
    CHECK_FALSE(engagedHigh);
    // Pied au plancher depuis 1500 tr/min en deuxième : la pression est là en moins de 0,6 s.
    MESSAGE("pression atteinte en 0,6 s : " << earlyBoost << ", à " << earlyRpm << " tr/min");
    CHECK(earlyBoost > 0.85);
}

TEST_CASE("le twincharger ôte le retard, et son compresseur se paie au vilebrequin") {
    const Powertrain p = tsi();
    Powertrain single = p;
    single.engine.turboLayout = TurboLayout::Single;
    const double charged = stabSeconds(p, 1600.0);
    const double alone = singleStabSeconds(single, 1600.0);
    MESSAGE("90 % à 1600 tr/min : " << charged << " s avec le compresseur, " << alone << " s turbo seul");
    CHECK(charged < 0.3 * alone);
    // À bas régime, le compresseur donne la pression que le turbo n'a pas : plus de couple, moins ce qu'il coûte.
    CHECK(torqueCurveNm(p, 1500.0) > 1.2 * torqueCurveNm(single, 1500.0));
    const TwinTurboSteady low = twinTurboSteady(twinTurboSpecOf(p), 1500.0, breathingOf(p, 1500.0));
    CHECK(low.clutch);
    CHECK(low.chargerNm > 1.0);
    // Au-delà de la coupure, c'est le turbo simple.
    CHECK(torqueCurveNm(p, 5500.0) == doctest::Approx(torqueCurveNm(single, 5500.0)).epsilon(0.02));
}

TEST_CASE("sur un diesel, les aubes divisent le retard ; elles tiennent la pression sans soupape") {
    Powertrain diesel = findPreset("diesel_four")->powertrain();
    Powertrain vanes = diesel;
    vanes.engine.variableGeometry = true;
    const double fixed = singleStabSeconds(diesel, 2000.0);
    const double variable = stabSeconds(vanes, 2000.0);
    MESSAGE("90 % à 2000 tr/min : " << fixed << " s turbine fixe, " << variable << " s à aubes");
    CHECK(variable < 0.7 * fixed);
    TwinTurbo twin;
    twin.setSpec(twinTurboSpecOf(vanes));
    run(twin, at(vanes, 3500.0, 1.0), 6.0);
    CHECK(twin.boost() == doctest::Approx(1.0).epsilon(0.08));
    CHECK(twin.state().unit[0].wastegate == 0.0);
    // Les aubes ont rouvert pour ne pas dépasser : plus que fermées.
    CHECK(twin.state().vane[0] > twin.spec().vaneMin * 1.2);
}

TEST_CASE("sur une essence, sans soupape, les aubes ne gagnent qu'en haut") {
    // Les gaz d'une essence sont plus chauds et son débit plus étendu : grandes ouvertes au régime maximal, les aubes fermées
    // restent aussi grandes que la petite turbine fixe, et moins efficaces. Elles gagnent en haut, par la contre-pression.
    const Powertrain fixed = six(TurboLayout::Single);
    const Powertrain vanes = six(TurboLayout::Single, true);
    const double fixedLag = singleStabSeconds(fixed, 2500.0);
    const double vaneLag = stabSeconds(vanes, 2500.0);
    Powertrain diesel = findPreset("diesel_four")->powertrain();
    Powertrain dieselVanes = diesel;
    dieselVanes.engine.variableGeometry = true;
    const double dieselGain = stabSeconds(dieselVanes, 2000.0) / singleStabSeconds(diesel, 2000.0);
    const TwinTurboSpec petrol = twinTurboSpecOf(vanes);
    const TwinTurboSpec oil = twinTurboSpecOf(dieselVanes);
    MESSAGE("essence : " << fixedLag << " s fixe, " << vaneLag << " s à aubes ; diesel : rapport " << dieselGain << " ; aubes fermées : "
                         << petrol.vaneMin << " de la turbine fixe (essence), " << oil.vaneMin << " (diesel)");
    CHECK(vaneLag / fixedLag > dieselGain);
    CHECK(petrol.vaneMin > 0.9);
    CHECK(oil.vaneMin < 0.7);
    CHECK(torqueCurveNm(vanes, 6000.0) > torqueCurveNm(fixed, 6000.0));
}

TEST_CASE("le modèle à deux turbos ne dépend pas de la durée des pas demandés") {
    const Powertrain p = six(TurboLayout::TwinSequential);
    DrivingModel coarse(p);
    DrivingModel fine(p);
    for (DrivingModel* m : {&coarse, &fine}) {
        m->setManual(true);
        m->requestShift(1);
    }
    for (int i = 0; i < 400; ++i) coarse.stepPedal(0.01, 1.0, 0.0);
    for (int i = 0; i < 1600; ++i) fine.stepPedal(0.0025, 1.0, 0.0);
    CHECK(coarse.turboState().shaftOmega == doctest::Approx(fine.turboState().shaftOmega).epsilon(1e-3));
    CHECK(coarse.turboStateB().shaftOmega == doctest::Approx(fine.turboStateB().shaftOmega).epsilon(1e-3));
}

namespace {

/** Le son de deux arbres décrits : `setTurbo` et `setTurboB`, par tranches de 256 échantillons. */
Samples renderTwin(int block, float hzA, float hzB, bool second, int updates = 400) {
    EngineConfig config;
    config.aspiration = Aspiration::Turbo;
    config.boostBar = 1.0f;
    EngineSound core(kRate);
    core.setProfile(config.toProfile());
    core.setVolume(0.7f);
    core.setListener(Listener::Front);
    Samples out(static_cast<std::size_t>(updates) * 256);
    for (int u = 0; u < updates; ++u) {
        core.setTargetRpm(4000.0f);
        core.setDrive(0.9f, 0.9f, false, false, 0.9f);
        TurboDrive a;
        a.shaftHz = hzA;
        a.shaftShare = 1.0f;
        a.flowShare = 0.8f;
        a.levelShare = 0.71f;
        core.setTurbo(a);
        if (second) {
            TurboDrive b = a;
            b.shaftHz = hzB;
            core.setTurboB(b);
        }
        for (int done = 0; done < 256; done += block) core.render(out.data() + static_cast<std::size_t>(u) * 256 + done, block);
    }
    return out;
}

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

TEST_CASE("deux arbres, deux sifflements, chacun à sa vitesse") {
    const Samples one = renderTwin(256, 1500.0f, 2300.0f, false);
    const Samples two = renderTwin(256, 1500.0f, 2300.0f, true);
    const Samples a = slice(one, one.size() - 16384, one.size());
    const Samples b = slice(two, two.size() - 16384, two.size());
    // L'ordre 2 du second arbre, à 4600 Hz : présent seulement s'il existe.
    CHECK(band(b, 4520.0, 4680.0) > 3.0 * band(a, 4520.0, 4680.0));
    // Le premier reste où il est.
    CHECK(band(b, 2920.0, 3080.0) > 0.5 * band(a, 2920.0, 3080.0));
}

TEST_CASE("le second arbre a son propre tirage, et ne dépend pas de la taille des blocs") {
    const Samples a = renderTwin(256, 1500.0f, 1530.0f, true);
    const Samples b = renderTwin(64, 1500.0f, 1530.0f, true);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
}

TEST_CASE("le rendu de deux turbos n'alloue rien") {
    EngineConfig config;
    config.aspiration = Aspiration::Turbo;
    EngineSound core(kRate);
    core.setProfile(config.toProfile());
    core.setVolume(0.7f);
    Samples out(4096);
    core.render(out.data(), 2048);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) {
        core.setTargetRpm(4000.0f);
        core.setDrive(0.9f, 0.9f, false, false, 0.9f);
        TurboDrive d;
        d.shaftHz = 2000.0f;
        d.shaftShare = 0.9f;
        d.flowShare = 0.7f;
        core.setTurbo(d);
        d.shaftHz = 2040.0f;
        d.wastegateFlowShare = i % 2 == 0 ? 0.3f : 0.0f;
        core.setTurboB(d);
        core.render(out.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}
