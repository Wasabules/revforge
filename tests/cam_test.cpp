#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "doctest.h"
#include "revforge/cam.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using test::kRate;
using test::Samples;

extern std::atomic<long> g_allocations;

namespace {

Powertrain twoLitre() {
    Powertrain p = referencePowertrain();
    p.engine.cylinders = 4;
    p.engine.displacementCc = 1998;
    p.engine.boreMm = 86;
    p.engine.redlineRpm = 6800;
    p.car.body = Body::Compact;
    p.car.massKg = 1200;
    return p;
}

Powertrain withProfile(Powertrain p, int duration, float lift, int separation) {
    p.engine.camDurationDeg = duration;
    p.engine.camLiftMm = lift;
    p.engine.lobeSeparationDeg = separation;
    return p;
}

Powertrain withTiming(Powertrain p, ValveTiming timing) {
    p.engine.valveTiming = timing;
    return p;
}

double derivative(double (*f)(double), double x, int order) {
    const double h = 1e-4;
    if (order == 1) return (f(x + h) - f(x - h)) / (2.0 * h);
    return (f(x + h) - 2.0 * f(x) + f(x - h)) / (h * h);
}

/** Le ralenti, régime tenu et charge donnée, comme le modèle de conduite le donnerait. */
Samples idle(const EngineProfile& p, float seconds = 6.0f) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setTargetRpm(p.idleRpm);
    core.setDrive(0.13f, 0.0f, false, false, 0.0f);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    core.render(out.data(), static_cast<int>(out.size()));
    return test::slice(out, kRate, out.size());
}

/** La dispersion d'une bouffée à l'autre : la crête de l'enveloppe dans chaque intervalle d'allumage, et son écart-type relatif. */
double crestCov(const Samples& x, double firingHz) {
    // Un passe-bande de 40 à 1200 Hz, deux résonateurs en cascade, puis une enveloppe sur 3 ms.
    const double center = std::sqrt(40.0 * 1200.0);
    const double w = 2.0 * 3.141592653589793 * center / kRate;
    const double r = std::exp(-3.141592653589793 * (1200.0 - 40.0) / kRate);
    const double a1 = 2.0 * r * std::cos(w);
    const double a2 = -r * r;
    double y1 = 0.0, y2 = 0.0;
    const std::size_t window = kRate * 3 / 1000;
    const std::size_t period = static_cast<std::size_t>(kRate / firingHz);
    std::vector<double> energy(x.size(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double y = static_cast<double>(x[i]) + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        energy[i] = y * y;
    }
    std::vector<double> crests;
    double running = 0.0;
    double top = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        running += energy[i] - (i >= window ? energy[i - window] : 0.0);
        top = std::max(top, running);
        if ((i + 1) % period == 0) {
            crests.push_back(std::sqrt(top));
            top = 0.0;
        }
    }
    double mean = 0.0;
    for (double c : crests) mean += c;
    mean /= static_cast<double>(crests.size());
    double spread = 0.0;
    for (double c : crests) spread += (c - mean) * (c - mean);
    return std::sqrt(spread / static_cast<double>(crests.size())) / mean;
}

double bandEnergy(const Samples& x, double fromHz, double toHz) {
    const double center = std::sqrt(fromHz * toHz);
    const double w = 2.0 * 3.141592653589793 * center / kRate;
    const double r = std::exp(-3.141592653589793 * (toHz - fromHz) / kRate);
    const double a1 = 2.0 * r * std::cos(w);
    const double a2 = -r * r;
    double y1 = 0.0, y2 = 0.0, sum = 0.0;
    for (float s : x) {
        const double y = static_cast<double>(s) + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        sum += y * y;
    }
    return sum / static_cast<double>(x.size());
}

}  // namespace

TEST_CASE("came : la loi de levée est continue en accélération, nulle aux sièges") {
    CHECK(camLiftShare(0.0) == doctest::Approx(1.0));
    CHECK(camLiftShare(1.0) == 0.0);
    CHECK(camLiftShare(-1.0) == 0.0);
    CHECK(camLiftShare(1.5) == 0.0);
    // Levée, vitesse, accélération : nulles au siège, donc raccordées au cercle de base.
    for (const double edge : {-0.9999, 0.9999}) {
        CHECK(std::fabs(camLiftShare(edge)) < 1e-10);
        CHECK(std::fabs(derivative(camLiftShare, edge, 1)) < 1e-5);
        CHECK(std::fabs(derivative(camLiftShare, edge, 2)) < 1e-2);
    }
    // Au nez, la décélération du polynôme : −4.
    CHECK(derivative(camLiftShare, 0.0, 2) == doctest::Approx(kCamNoseCurvature).epsilon(1e-3));
    // Monotone du nez au siège : une came n'a qu'un sommet.
    double previous = 1.0;
    for (double x = 0.01; x < 1.0; x += 0.01) {
        CHECK(camLiftShare(x) <= previous);
        previous = camLiftShare(x);
    }
}

TEST_CASE("came : le profil d'origine retrouve le calage du son, à 1 mm") {
    const Powertrain p = twoLitre();
    const CamProfile stock = stockCamProfile(p.engine);
    // Ce sont les kEvo, kEvc, kIvo, kIvc d'engine_sound.cpp.
    CHECK(stock.evoDeg == 130.0);
    CHECK(stock.evcDeg == 370.0);
    CHECK(stock.ivoDeg == 350.0);
    CHECK(stock.ivcDeg == 590.0);
    CHECK(stock.liftMm == doctest::Approx(0.12 * 86.0));
    CHECK(camValveLiftMm(stock, stock.ivoDeg, true) == doctest::Approx(1.0).epsilon(1e-6));
    CHECK(camValveLiftMm(stock, stock.evcDeg, false) == doctest::Approx(1.0).epsilon(1e-6));
    CHECK(camValveLiftMm(stock, 360.0 + 110.0, true) == doctest::Approx(stock.liftMm));
    CHECK(camDurationAtLiftDeg(stock, 1.0) == doctest::Approx(240.0).epsilon(1e-6));
    CHECK(valveOverlapDeg(stock) == doctest::Approx(20.0));
    CHECK(intakeClosingAbdcDeg(stock) == doctest::Approx(50.0));
    // Siège à siège, la came tient plus longtemps : de l'ordre de 300° pour 240° à 1 mm.
    CHECK(2.0 * stock.halfSpanDeg > 280.0);
    CHECK(2.0 * stock.halfSpanDeg < 320.0);
    // Le défaut ne passe par aucun de ces calculs.
    CHECK(camIsStock(p.engine));
    CHECK_FALSE(p.profile().camTuned);
}

TEST_CASE("came : la même came saisie à la main redonne le moteur d'origine") {
    const Powertrain stock = twoLitre();
    // La levée saisie est un float : à un arrondi près, la même came.
    const Powertrain same = withProfile(stock, 240, static_cast<float>(0.12 * 86.0), 110);
    REQUIRE_FALSE(camIsStock(same.engine));
    for (double rpm = 800.0; rpm <= 7200.0; rpm += 400.0) {
        CHECK(camTorqueRatio(same.engine, rpm) == doctest::Approx(1.0).epsilon(1e-5));
        CHECK(torqueCurveNm(same, rpm) == doctest::Approx(torqueCurveNm(stock, rpm)).epsilon(1e-5));
    }
}

TEST_CASE("came : la fermeture de l'admission place le meilleur remplissage, et règle la compression effective") {
    const Powertrain stock = twoLitre();
    const Powertrain race = withProfile(stock, 290, 12.5f, 106);
    const Powertrain mild = withProfile(stock, 216, 9.0f, 112);
    const PowerSummary a = powerSummary(stock);
    const PowerSummary b = powerSummary(race);
    const PowerSummary c = powerSummary(mild);
    // Une came longue ferme tard : le couple monte plus haut, et se perd en bas.
    CHECK(b.peakTorqueRpm > a.peakTorqueRpm);
    CHECK(c.peakTorqueRpm < a.peakTorqueRpm);
    CHECK(torqueCurveNm(race, 1500.0) < 0.85 * torqueCurveNm(stock, 1500.0));
    CHECK(torqueCurveNm(mild, 1500.0) > torqueCurveNm(stock, 1500.0));
    // Au-delà du régime maximal d'origine, elle respire mieux.
    CHECK(camTorqueRatio(race.engine, 1.2 * 6800.0) > 1.0);
    CHECK(camTorqueRatio(mild.engine, 1.2 * 6800.0) < 1.0);
    // Compression effective : volume à la fermeture sur volume mort. Plus tard, moins de compression.
    const CamProfile s = camProfileOf(stock.engine);
    const CamProfile r = camProfileOf(race.engine);
    CHECK(effectiveCompressionRatio(10.5, intakeClosingAbdcDeg(s)) == doctest::Approx(9.25).epsilon(0.01));
    CHECK(effectiveCompressionRatio(10.5, intakeClosingAbdcDeg(r)) < effectiveCompressionRatio(10.5, intakeClosingAbdcDeg(s)) - 0.8);
    // Au point mort bas, la compression effective est la compression géométrique.
    CHECK(effectiveCompressionRatio(10.5, 0.0) == doctest::Approx(10.5));
}

TEST_CASE("came : le croisement laisse des gaz brûlés, surtout au ralenti, et la flamme en devient irrégulière") {
    const Powertrain stock = twoLitre();
    const Powertrain race = withProfile(stock, 290, 12.5f, 106);
    const CamProfile s = camProfileOf(stock.engine);
    const CamProfile r = camProfileOf(race.engine);
    const double factorStock = overlapFactor(s, 86.0, 2.0 * 33.1);
    const double factorRace = overlapFactor(r, 86.0, 2.0 * 33.1);
    CHECK(factorRace > 5.0 * factorStock);
    // Fox, Cheng et Heywood : de 10 à 20 % au ralenti, moins de 10 % à pleine charge.
    const double atIdle = residualGasFraction(factorStock, 800.0, 38.0, 101.4, 10.5);
    const double atFull = residualGasFraction(factorStock, 4000.0, 100.0, 104.0, 10.5);
    CHECK(atIdle > 0.10);
    CHECK(atIdle < 0.20);
    CHECK(atFull < 0.10);
    CHECK(residualGasFraction(factorRace, 800.0, 38.0, 101.4, 10.5) > atIdle + 0.1);
    // Plus vite, le croisement dure moins longtemps : moins de gaz repasse.
    CHECK(residualGasFraction(factorRace, 2000.0, 38.0, 101.4, 10.5) < residualGasFraction(factorRace, 800.0, 38.0, 101.4, 10.5));
    // La dispersion : quelques pour cent sans gaz brûlés, 10 % (limite de conduite) vers 27 %, des ratés au-delà.
    CHECK(combustionCov(0.0) < 0.015);
    CHECK(combustionCov(0.15) == doctest::Approx(0.03).epsilon(0.15));
    CHECK(combustionCov(0.27) == doctest::Approx(0.10).epsilon(0.1));
    CHECK(combustionCov(0.35) > 0.25);
    // Le ralenti : la came pointue demande plus d'air pour tenir, et brûle bien plus mal.
    const CamIdle idleStock = camIdle(stock.engine);
    const CamIdle idleRace = camIdle(race.engine);
    CHECK(idleStock.manifoldKpa > 30.0);
    CHECK(idleStock.manifoldKpa < 45.0);
    CHECK(idleRace.manifoldKpa > idleStock.manifoldKpa + 5.0);
    CHECK(idleStock.cov < 0.05);
    CHECK(idleRace.cov > 0.10);
}

TEST_CASE("came : le déphaseur avance l'admission en bas, la retarde en haut, et gagne du couple partout") {
    const Powertrain stock = twoLitre();
    const Powertrain phased = withTiming(stock, ValveTiming::Phased);
    CHECK(camPhaserTargetDeg(phased.engine, 1500.0, false) > 10.0);
    CHECK(camPhaserTargetDeg(phased.engine, 7000.0, false) < -5.0);
    CHECK(camPhaserTargetDeg(stock.engine, 1500.0, false) == 0.0);
    for (double rpm = 1000.0; rpm <= 7000.0; rpm += 500.0) CHECK(torqueCurveNm(phased, rpm) >= torqueCurveNm(stock, rpm) - 0.5);
    CHECK(powerSummary(phased).peakTorqueNm > 1.04 * powerSummary(stock).peakTorqueNm);
    // Le modèle de conduite : la palette part en butée retard, et tourne à vitesse finie.
    DrivingModel model(phased);
    CHECK(model.camDrive().phaserDeg == doctest::Approx(-kCamPhaserAuthorityDeg));
    for (int i = 0; i < 2000; ++i) model.stepNeutral(0.001, 0.0);
    CHECK(model.camDrive().phaserDeg == doctest::Approx(-kCamPhaserAuthorityDeg));
    float before = model.camDrive().phaserDeg;
    double fastest = 0.0;
    for (int i = 0; i < 400; ++i) {
        model.stepNeutral(0.001, 1.0);
        fastest = std::max(fastest, std::fabs(static_cast<double>(model.camDrive().phaserDeg - before)) / 0.001);
        before = model.camDrive().phaserDeg;
    }
    CHECK(model.camDrive().phaserDeg > 0.0);
    CHECK(fastest <= kCamPhaserDegPerS * 1.0001);
    // Une came fixe ne bouge jamais.
    DrivingModel fixed(stock);
    for (int i = 0; i < 400; ++i) fixed.stepNeutral(0.001, 1.0);
    CHECK(fixed.camDrive().phaserDeg == 0.0f);
    CHECK_FALSE(fixed.camDrive().high);
}

TEST_CASE("came : deux profils basculent là où leurs courbes de couple se croisent, avec une hystérésis") {
    const Powertrain stock = twoLitre();
    const Powertrain twin = withTiming(stock, ValveTiming::TwoStep);
    const double at = camSwitchRpm(twin.engine);
    REQUIRE(at > 3000.0);
    REQUIRE(at < 6800.0);
    // Au croisement, les deux profils donnent le même couple : la bascule ne se sent pas.
    CHECK(camEffect(twin.engine, at, false).torque == doctest::Approx(camEffect(twin.engine, at, true).torque).epsilon(0.01));
    CHECK(camEffect(twin.engine, 0.6 * at, false).torque > camEffect(twin.engine, 0.6 * at, true).torque);
    CHECK(camEffect(twin.engine, 1.2 * at, true).torque > camEffect(twin.engine, 1.2 * at, false).torque);
    // Un régime d'affolement plus bas que la bascule : le second profil ne servirait à rien.
    CHECK(camSwitchRpm(twin.engine, at - 100.0) == 0.0);
    CHECK(camSwitchRpm(twin.engine, at + 100.0) == doctest::Approx(at));
    // Le couple de la fiche est le meilleur des deux.
    CHECK(powerSummary(twin).peakTorqueNm > powerSummary(stock).peakTorqueNm);
    // Pied au plancher, au point mort : le second profil entre au-delà du croisement, et ressort en dessous moins l'hystérésis.
    DrivingModel model(twin);
    bool entered = false;
    double enteredAt = 0.0;
    for (int i = 0; i < 4000 && !entered; ++i) {
        model.stepNeutral(0.001, 1.0);
        if (model.camDrive().high) {
            entered = true;
            enteredAt = model.rpm();
        }
    }
    REQUIRE(entered);
    CHECK(enteredAt > at * (1.0 + kCamSwitchHysteresis) - 50.0);
    CHECK(enteredAt < at * (1.0 + kCamSwitchHysteresis) + 300.0);
    bool left = false;
    double leftAt = 0.0;
    for (int i = 0; i < 6000 && !left; ++i) {
        model.stepNeutral(0.001, 0.7);
        if (!model.camDrive().high) {
            left = true;
            leftAt = model.rpm();
        }
    }
    if (left) CHECK(leftAt < at * (1.0 - kCamSwitchHysteresis) + 50.0);
    // Pied levé : le profil doux revient, quel que soit le régime.
    DrivingModel lifted(twin);
    for (int i = 0; i < 3000; ++i) lifted.stepNeutral(0.001, 1.0);
    REQUIRE(lifted.camDrive().high);
    for (int i = 0; i < 300; ++i) lifted.stepNeutral(0.001, 0.0);
    CHECK_FALSE(lifted.camDrive().high);
}

TEST_CASE("came : la décélération au nez croît comme le carré du régime") {
    const CamProfile stock = stockCamProfile(twoLitre().engine);
    const double at3500 = camNoseDecelerationMs2(stock, 3500.0);
    const double at7000 = camNoseDecelerationMs2(stock, 7000.0);
    CHECK(at7000 == doctest::Approx(4.0 * at3500));
    // De l'ordre de 300 g à 7000 tr/min sur une came de route.
    CHECK(at7000 > 1500.0);
    CHECK(at7000 < 6000.0);
}

TEST_CASE("came : ni rotatif ni deux-temps n'en ont, et leur son ne bouge pas") {
    Powertrain rotary = findPreset("twin_rotor_13")->powertrain();
    const EngineProfile before = rotary.profile();
    rotary.engine.camDurationDeg = 300;
    rotary.engine.valveTiming = ValveTiming::PhasedTwoStep;
    CHECK(camIsStock(rotary.engine));
    CHECK(rotary.profile() == before);
    Powertrain two = twoLitre();
    two.engine.cycle = EngineCycle::TwoStroke;
    const double torque = torqueCurveNm(two, 4000.0);
    two.engine.camDurationDeg = 300;
    CHECK(torqueCurveNm(two, 4000.0) == torque);
}

TEST_CASE("came : la came pointue fait galoper le ralenti, à son propre tirage") {
    const Powertrain base = findPreset("hot_hatch_na")->powertrain();
    Powertrain race = base;
    race.engine.camDurationDeg = 300;
    race.engine.camLiftMm = 13.0f;
    race.engine.lobeSeparationDeg = 104;
    const EngineProfile stockProfile = base.profile();
    const EngineProfile raceProfile = race.profile();
    REQUIRE(raceProfile.camTuned);
    const double firing = static_cast<double>(raceProfile.idleRpm) / 120.0 * raceProfile.cylinders;
    const double stockCov = crestCov(idle(stockProfile), firing);
    const double raceCov = crestCov(idle(raceProfile), firing);
    CHECK(raceCov > stockCov * 1.25);
    // Contre-épreuve : sans la dispersion des combustions, la came pointue garde à peu près celle d'origine.
    EngineProfile calm = raceProfile;
    calm.camDispersion = false;
    const double calmCov = crestCov(idle(calm), firing);
    CHECK(calmCov < raceCov * 0.85);
}

TEST_CASE("came : la bascule s'entend, cylindre par cylindre, sans rien changer sans elle") {
    const Powertrain twin = withTiming(findPreset("hot_hatch_na")->powertrain(), ValveTiming::TwoStep);
    const EngineProfile p = twin.profile();
    REQUIRE(p.camTwoStep);
    // Deux profils identiques : la bascule ne change alors rien au gaz, et seul le verrou des culbuteurs reste. Le même moteur
    // sans deux profils sert de témoin : la goupille a son propre tirage, donc tout le reste est identique au bit près.
    EngineProfile same = p;
    same.camHighTiming = p.camTiming;
    same.camOpeningHigh = p.camOpening;
    same.camBlowStretchHigh = p.camBlowStretch;
    same.camOverlapFactorHigh = p.camOverlapFactor;
    same.camIdleKpaHigh = p.camIdleKpa;
    EngineProfile witness = same;
    witness.camTwoStep = false;
    const auto render = [](const EngineProfile& profile) {
        EngineSound core(kRate);
        core.setProfile(profile);
        core.setVolume(0.7f);
        core.setListener(Listener::EngineBay);
        core.setTargetRpm(1500.0f);
        core.setDrive(0.3f, 0.0f, false, false, 0.3f);
        Samples out(kRate + kRate / 10);
        core.render(out.data(), kRate);
        CamDrive drive;
        drive.high = true;
        core.setCam(drive);
        core.render(out.data() + kRate, kRate / 10);
        return out;
    };
    const Samples switched = render(same);
    const Samples quiet = render(witness);
    double before = 0.0;
    for (int i = 0; i < kRate; ++i) before += std::fabs(static_cast<double>(switched[static_cast<std::size_t>(i)] - quiet[static_cast<std::size_t>(i)]));
    CHECK(before == 0.0);
    // Après la bascule : une goupille par cylindre, chacune à sa fermeture d'admission, donc en un cycle (80 ms à 1500 tr/min).
    Samples click(kRate / 10);
    for (std::size_t i = 0; i < click.size(); ++i) click[i] = switched[kRate + i] - quiet[kRate + i];
    int onsets = 0;
    bool loud = false;
    const float threshold = 0.2f * test::peak(click);
    REQUIRE(threshold > 0.0f);
    int quietRun = 0;
    for (float s : click) {
        const bool now = std::fabs(s) > threshold;
        if (now && !loud) ++onsets;
        quietRun = now ? 0 : quietRun + 1;
        loud = now || (loud && quietRun < kRate / 500);
    }
    // Le reste de l'écart n'est que le gain de sortie, qui suit le niveau : il ne fait pas d'autres fronts.
    CHECK(onsets == p.cylinders);
}

TEST_CASE("came : temps réel et blocs, came pointue, deux profils et déphaseur") {
    Powertrain both = withTiming(findPreset("hot_hatch_na")->powertrain(), ValveTiming::PhasedTwoStep);
    both.engine.camDurationDeg = 280;
    both.engine.camLiftMm = 12.0f;
    const EngineProfile p = both.profile();
    const auto drive = [](EngineSound& core, int at) {
        CamDrive cam;
        cam.high = (at / 4000) % 2 == 1;
        cam.phaserDeg = static_cast<float>((at % 9000) / 450 - 10);
        core.setCam(cam);
        core.setDrive(0.4f, 0.0f, false, false, 0.4f);
    };
    EngineSound whole(kRate);
    EngineSound parts(kRate);
    for (EngineSound* core : {&whole, &parts}) {
        core->setProfile(p);
        core->setVolume(0.7f);
        core->setTargetRpm(1200.0f);
    }
    Samples a(kRate);
    Samples b(kRate);
    for (int at = 0; at < kRate; at += 1024) {
        drive(whole, at);
        whole.render(a.data() + at, std::min(1024, kRate - at));
    }
    for (int at = 0; at < kRate;) {
        const int block = std::min({137, kRate - at, 1024 - at % 1024});
        if (at % 1024 == 0) drive(parts, at);
        parts.render(b.data() + at, block);
        at += block;
    }
    CHECK(a == b);
    Samples buffer(4096);
    const long before = g_allocations.load();
    for (int i = 0; i < 20; ++i) {
        drive(whole, i * 4096);
        whole.render(buffer.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}

TEST_CASE("came : figures de la documentation (affichage)" * doctest::skip()) {
    const Powertrain p = twoLitre();
    const auto show = [](const char* name, const Powertrain& q) {
        const auto start = std::chrono::steady_clock::now();
        const CamEffect first = camEffect(q.engine, 3000.0, false);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s (%.1f ms) premier %.3f\n", name, ms, first.torque);
        for (double rpm = 1000.0; rpm <= 8500.0; rpm += 500.0) {
            const CamEffect low = camEffect(q.engine, rpm, false);
            const CamEffect high = camEffect(q.engine, rpm, true);
            std::printf("  %5.0f  couple %.3f rempl %.3f ouv %.3f  | haut %.3f  phase %.1f  C %.1f Nm\n", rpm, low.torque, low.filling, low.opening, high.torque,
                        camPhaserTargetDeg(q.engine, rpm, false), torqueCurveNm(q, rpm));
        }
        const PowerSummary s = powerSummary(q);
        const Performance perf = measurePerformance(q);
        std::printf("  pic %.1f Nm à %.0f, %.1f kW à %.0f, 0-100 %.2f s, bascule %.0f\n", s.peakTorqueNm, s.peakTorqueRpm, s.peakPowerKw, s.peakPowerRpm, perf.zeroTo100S,
                    camSwitchRpm(q.engine));
        const CamIdle idle = camIdle(q.engine);
        const CamProfile cam = camProfileOf(q.engine);
        std::printf("  ralenti %.0f tr/min, %.1f kPa, résiduels %.3f, COV %.3f ; croisement %.0f°, IVC %.0f° après PMB, compression effective %.2f\n", idle.rpm,
                    idle.manifoldKpa, idle.residual, idle.cov, valveOverlapDeg(cam), intakeClosingAbdcDeg(cam), effectiveCompressionRatio(10.5, intakeClosingAbdcDeg(cam)));
    };
    show("origine", p);
    show("route rapide 265/11/110", withProfile(p, 265, 11.0f, 110));
    show("course 280/12/106", withProfile(p, 280, 12.0f, 106));
    show("origine, rupteur 8000", [&] {
        Powertrain q = p;
        q.engine.redlineRpm = 8000;
        return q;
    }());
    show("course, rupteur 8000", [&] {
        Powertrain q = withProfile(p, 280, 12.0f, 106);
        q.engine.redlineRpm = 8000;
        return q;
    }());
    show("déphaseur", withTiming(p, ValveTiming::Phased));
    show("deux profils", withTiming(p, ValveTiming::TwoStep));
    show("les deux", withTiming(p, ValveTiming::PhasedTwoStep));
}
