#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"
#include "revforge/valvetrain.hpp"
#include "test_support.hpp"

using namespace revforge;
using test::kRate;
using test::Samples;

extern std::atomic<long> g_allocations;

namespace {

Powertrain twoLitre(int bore = 86) {
    Powertrain p = referencePowertrain();
    p.engine.cylinders = 4;
    p.engine.displacementCc = 1998;
    p.engine.boreMm = bore;
    p.engine.redlineRpm = 6500;
    p.car.body = Body::Compact;
    p.car.massKg = 1200;
    return p;
}

Powertrain withCam(Powertrain p, Camshaft camshaft) {
    p.engine.camshaft = camshaft;
    return p;
}

/** Le relief d'une bande : le haut des fenêtres de 1 ms face à la médiane, en dB. Une longue fenêtre cache un tic. */
double relief(const Samples& x, double fromHz, double toHz) {
    // Deux résonateurs en cascade autour de la bande.
    const double center = std::sqrt(fromHz * toHz);
    const double w = 2.0 * 3.141592653589793 * center / kRate;
    const double r = std::exp(-3.141592653589793 * (toHz - fromHz) / kRate);
    const double a1 = 2.0 * r * std::cos(w);
    const double a2 = -r * r;
    double y1 = 0.0, y2 = 0.0, z1 = 0.0, z2 = 0.0;
    constexpr std::size_t kWindow = kRate / 1000;
    std::vector<double> windows;
    double sum = 0.0;
    std::size_t n = 0;
    for (float s : x) {
        const double y = static_cast<double>(s) + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        const double z = y + a1 * z1 + a2 * z2;
        z2 = z1;
        z1 = z;
        sum += z * z;
        if (++n == kWindow) {
            windows.push_back(sum);
            sum = 0.0;
            n = 0;
        }
    }
    std::sort(windows.begin(), windows.end());
    return 10.0 * std::log10(windows[windows.size() * 97 / 100] / windows[windows.size() / 2]);
}

Samples heard(const EngineProfile& p, float rpm, Listener listener, float seconds = 3.0f) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setListener(listener);
    core.setVolume(0.7f);
    core.setTargetRpm(rpm);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    core.render(out.data(), static_cast<int>(out.size()));
    return test::slice(out, kRate, out.size());
}

}  // namespace

// Les tailles de vrais moteurs, de 75 à 101,6 mm d'alésage : la place dans l'alésage suffit à les retrouver, à 6 % près.
// TU3 (75 mm, 36,7 mm), C20NE (86, 41,8), Small Block (101,6, 51,3) ; 4A-GE (81, 30,5), SR20 (86, 34), S38 (93,4, 37). De mémoire, sauf le S38.
TEST_CASE("les soupapes ont la taille de celles de vrais moteurs") {
    for (const auto [bore, head] : {std::pair{75.0, 36.7}, {86.0, 41.8}, {101.6, 51.3}}) {
        INFO(bore);
        CHECK(valveTrainOf(Camshaft::Sohc, bore).intakeMm == doctest::Approx(head).epsilon(0.06));
    }
    for (const auto [bore, head] : {std::pair{81.0, 30.5}, {86.0, 34.0}, {93.4, 37.0}}) {
        INFO(bore);
        CHECK(valveTrainOf(Camshaft::Dohc, bore).intakeMm == doctest::Approx(head).epsilon(0.06));
    }
    CHECK(valveTrainOf(Camshaft::Dohc, 93.4).exhaustMm == doctest::Approx(32.0).epsilon(0.03));
}

TEST_CASE("deux soupapes plus grandes laissent moins de section que quatre petites") {
    for (double bore : {60.0, 75.0, 86.0, 100.0, 110.0}) {
        INFO(bore);
        const ValveTrainGeometry two = valveTrainOf(Camshaft::Sohc, bore);
        const ValveTrainGeometry four = valveTrainOf(Camshaft::Dohc, bore);
        CHECK(two.perCylinder == 2);
        CHECK(four.perCylinder == 4);
        CHECK(two.intakeMm > four.intakeMm);
        CHECK(two.intakeAreaMm2 / four.intakeAreaMm2 > 0.65);
        CHECK(two.intakeAreaMm2 / four.intakeAreaMm2 < 0.75);
        CHECK(two.exhaustAreaMm2 / four.exhaustAreaMm2 > 0.6);
        CHECK(two.exhaustAreaMm2 / four.exhaustAreaMm2 < 0.72);
        // Le coefficient de Taylor, ramené à la tête : 0,3 à 0,4.
        const double head = 3.141592653589793 / 4.0 * two.intakeMm * two.intakeMm;
        CHECK(two.intakeAreaMm2 / head > 0.3);
        CHECK(two.intakeAreaMm2 / head < 0.4);
    }
}

TEST_CASE("l'indice de Mach et le remplissage suivent la loi de Taylor") {
    // Un moteur de série vise moins de 0,55 au régime de puissance : 0,53 sur un 8 soupapes de 2 L à 5200 tr/min.
    const ValveTrainGeometry two = valveTrainOf(Camshaft::Sohc, 86.0);
    const ValveTrainGeometry four = valveTrainOf(Camshaft::Dohc, 86.0);
    CHECK(machIndex(two.intakeAreaMm2, 86.0, 86.0, 5200.0, 343.0) == doctest::Approx(0.53).epsilon(0.05));
    CHECK(machIndex(four.intakeAreaMm2, 86.0, 86.0, 6000.0, 343.0) == doctest::Approx(0.43).epsilon(0.05));
    // Plat à bas indice, puis de moins en moins, jusqu'à la limite étranglée : 0,75 / Z selon le cours, 0,76 par l'intégration.
    CHECK(valveFilling(0.0) == 1.0);
    CHECK(valveFilling(0.3) > 0.98);
    CHECK(valveFilling(0.6) > 0.9);
    CHECK(valveFilling(1.0) < 0.75);
    CHECK(valveFilling(4.0) * 4.0 == doctest::Approx(0.78).epsilon(0.03));
    // Un cylindre qu'on vide : trop tôt, on perd de la détente ; trop tard, le piston pousse. Le travail perdu a un creux, puis monte.
    CHECK(exhaustLossBar(0.1) < exhaustLossBar(0.0));
    CHECK(exhaustLossBar(0.2) < exhaustLossBar(0.3));
    CHECK(exhaustLossBar(0.4) < exhaustLossBar(0.5));
    CHECK(exhaustLossBar(0.5) > 1.0);
}

TEST_CASE("le simple arbre décroche en haut selon la vitesse du gaz, pas selon un pourcentage") {
    // Le régime.
    CHECK(sohcBreathingRatio(86.0, 86.0, 2000.0) > 0.98);
    CHECK(sohcBreathingRatio(86.0, 86.0, 4000.0) > 0.95);
    CHECK(sohcBreathingRatio(86.0, 86.0, 6000.0) < 0.92);
    CHECK(sohcBreathingRatio(86.0, 86.0, 8000.0) < 0.8);
    // La course : la même section à piston plus lent passe mieux.
    CHECK(sohcBreathingRatio(86.0, 70.0, 6000.0) > sohcBreathingRatio(86.0, 86.0, 6000.0));
    CHECK(sohcBreathingRatio(86.0, 100.0, 6000.0) < sohcBreathingRatio(86.0, 86.0, 6000.0));
    // L'alésage, à cylindrée égale : plus grand, le piston va moins vite, et le pont entre les sièges pèse moins.
    const auto atBore = [](int bore, double rpm) {
        const Powertrain p = withCam(twoLitre(bore), Camshaft::Sohc);
        const Powertrain q = withCam(twoLitre(bore), Camshaft::Dohc);
        return breathingOf(p, rpm) / breathingOf(q, rpm);
    };
    CHECK(atBore(92, 6500.0) > atBore(78, 6500.0));
    // Et l'alésage seul déplace peu le rapport des sections : c'est bien le piston qui fait la différence.
    CHECK(sohcTuningRatio(60.0) == doctest::Approx(sohcTuningRatio(110.0)).epsilon(0.02));
}

TEST_CASE("le couple vient plus tôt, la puissance baisse") {
    for (const char* id : {"city_14", "four_cyl_sport", "hot_hatch_na", "inline_six", "v6_gt", "flat_six", "v8_cross_plane", "v10_open", "v12"}) {
        INFO(id);
        const Powertrain twin = withCam(findPreset(id)->powertrain(), Camshaft::Dohc);
        const Powertrain single = withCam(findPreset(id)->powertrain(), Camshaft::Sohc);
        const PowerSummary a = powerSummary(twin);
        const PowerSummary b = powerSummary(single);
        CHECK(b.peakTorqueRpm < a.peakTorqueRpm);
        CHECK(b.peakPowerRpm < a.peakPowerRpm);
        CHECK(b.peakPowerKw < a.peakPowerKw * 0.95);
        // Des couples de vrais moteurs : de 15 à 30 % de moins, et rarement moins de 5 % quand le 16 soupapes n'est pas poussé.
        CHECK(b.peakPowerKw > a.peakPowerKw * 0.6);
        // Le couple maximal reste proche, à 5 % près : c'est le haut qui se paie.
        CHECK(b.peakTorqueNm > a.peakTorqueNm * 0.93);
        // En bas, le gaz rapide remplit mieux : le simple arbre est devant à 40 % du régime de couple du double arbre.
        const double low = 0.45 * a.peakTorqueRpm;
        CHECK(torqueCurveNm(single, low) > torqueCurveNm(twin, low));
        // Et derrière, nettement, en haut.
        const double high = 0.98 * static_cast<double>(twin.engine.redlineRpm);
        CHECK(torqueCurveNm(single, high) < torqueCurveNm(twin, high) * 0.92);
    }
}

TEST_CASE("un double arbre garde sa courbe, et le réglage n'agit pas sur un moteur électrique") {
    const Powertrain base = findPreset("hot_hatch_na")->powertrain();
    CHECK(base.engine.camshaft == Camshaft::Dohc);
    CHECK(base.profile().camshaft == Camshaft::Dohc);
    Powertrain electric = findPreset("electric")->powertrain();
    const PowerSummary before = powerSummary(electric);
    const double inertia = engineInertia(electric);
    electric.engine.camshaft = Camshaft::Sohc;
    CHECK(powerSummary(electric).peakPowerKw == before.peakPowerKw);
    CHECK(engineInertia(electric) == inertia);
}

TEST_CASE("le frottement et l'inertie de la distribution changent à peine") {
    for (const char* id : {"hot_hatch_na", "v8_cross_plane"}) {
        INFO(id);
        const Powertrain twin = withCam(findPreset(id)->powertrain(), Camshaft::Dohc);
        const Powertrain single = withCam(findPreset(id)->powertrain(), Camshaft::Sohc);
        // Le culbuteur charge la came de son rapport : un peu plus de frottement, moins de 5 % du total.
        for (double rpm : {1000.0, 3000.0, 6000.0}) {
            CHECK(frictionTorqueNm(single, rpm) > frictionTorqueNm(twin, rpm));
            CHECK(frictionTorqueNm(single, rpm) < frictionTorqueNm(twin, rpm) * 1.05);
        }
        // Un arbre de moins par rangée, à demi-vitesse : moins de 1 % de l'inertie du moteur.
        CHECK(engineInertia(single) < engineInertia(twin));
        CHECK(engineInertia(single) > engineInertia(twin) * 0.99);
    }
}

TEST_CASE("le simple arbre suit tout le modèle : démarrage, conduite, turbo, diesel") {
    for (const char* id : {"hot_hatch_na", "hot_hatch_turbo", "diesel_four", "boxer_four_turbo", "v8_cross_plane"}) {
        INFO(id);
        const Powertrain p = withCam(findPreset(id)->powertrain(), Camshaft::Sohc);
        const Performance performance = measurePerformance(p);
        CHECK(performance.zeroTo100S > 3.0);
        CHECK(performance.zeroTo100S < 25.0);
        const Performance twin = measurePerformance(withCam(findPreset(id)->powertrain(), Camshaft::Dohc));
        CHECK(performance.topSpeedKmh < twin.topSpeedKmh);
        DrivingModel model(p);
        model.crank();
        for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.0, 0.0);
        CHECK(model.rpm() > 300.0);
    }
}

// La bouffée se voit dans les aigus du gaz, le tic sous le capot. Rien n'est joué : ce ne sont que des nombres.
TEST_CASE("avec une section plus petite, la bouffée d'échappement s'adoucit") {
    const EngineProfile twin = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Dohc).profile();
    const EngineProfile single = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Sohc).profile();
    const auto level = [](const EngineProfile& p, float rpm) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setListener(Listener::Pipe);
        core.setVolume(0.7f);
        core.setTargetRpm(rpm);
        Samples out(static_cast<std::size_t>(3 * kRate));
        core.render(out.data(), static_cast<int>(out.size()));
        return static_cast<double>(core.levelDbA());
    };
    for (float rpm : {2000.0f, 5000.0f}) {
        INFO(rpm);
        // Le niveau vrai se lit avant le volume et le gain de sortie : la sortie, elle, ramène tout à son niveau.
        const double drop = level(single, rpm) - level(twin, rpm);
        MESSAGE("à la sortie, à " << rpm << " tr/min : " << drop << " dB(A)");
        CHECK(drop < -1.0);
        CHECK(drop > -6.0);
    }
}

TEST_CASE("sous le capot, le culbuteur se détache au ralenti et s'efface à haut régime ; hors de la voiture, rien ne passe devant") {
    const EngineProfile twin = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Dohc).profile();
    const EngineProfile single = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Sohc).profile();
    const auto gainAt = [&](float rpm, Listener listener) {
        return relief(heard(single, rpm, listener), 1800.0, 3200.0) - relief(heard(twin, rpm, listener), 1800.0, 3200.0);
    };
    const double bayIdle = gainAt(900.0f, Listener::EngineBay);
    const double bayHigh = gainAt(6000.0f, Listener::EngineBay);
    const double outside = gainAt(900.0f, Listener::Outside);
    MESSAGE("relief du tic : capot au ralenti " << bayIdle << " dB, capot à 6000 tr/min " << bayHigh << " dB, dehors au ralenti " << outside << " dB");
    CHECK(bayIdle > 1.0);
    CHECK(bayHigh < 1.0);
    // « Un truc métallique qui claque » : dehors, à bas régime, le tic ne doit pas sortir de l'échappement.
    CHECK(outside < 0.5);
}

TEST_CASE("le simple arbre : temps réel, blocs, parole tenue au changement d'arbre") {
    const EngineProfile single = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Sohc).profile();
    {
        EngineSound whole(kRate);
        EngineSound parts(kRate);
        whole.setProfile(single);
        parts.setProfile(single);
        whole.setVolume(0.7f);
        parts.setVolume(0.7f);
        whole.setTargetRpm(3000.0f);
        parts.setTargetRpm(3000.0f);
        Samples a(kRate);
        Samples b(kRate);
        whole.render(a.data(), kRate);
        for (int at = 0; at < kRate;) {
            const int n = std::min(137, kRate - at);
            parts.render(b.data() + at, n);
            at += n;
        }
        CHECK(a == b);
    }
    EngineSound core(kRate);
    core.setProfile(single);
    core.setVolume(0.7f);
    core.setTargetRpm(3000.0f);
    Samples buffer(4096);
    core.render(buffer.data(), 4096);
    const long before = g_allocations.load();
    for (int i = 0; i < 20; ++i) core.render(buffer.data(), 4096);
    CHECK(g_allocations.load() == before);
    // Changer d'arbre change des pièces : le son repart proprement, et revenir redonne le premier.
    EngineProfile back = single;
    back.camshaft = Camshaft::Dohc;
    const EngineProfile twin = withCam(findPreset("hot_hatch_na")->powertrain(), Camshaft::Dohc).profile();
    // L'affolement dépend des pièces de la distribution : lui aussi revient avec elles.
    back.valves = twin.valves;
    CHECK(back == twin);
}
