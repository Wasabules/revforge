#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"
#include "revforge/valve_float.hpp"
#include "revforge/valvetrain.hpp"
#include "test_support.hpp"

using namespace revforge;
using test::kRate;
using test::Samples;

extern std::atomic<long> g_allocations;

namespace {

Powertrain withCam(Powertrain p, Camshaft camshaft) {
    p.engine.camshaft = camshaft;
    return p;
}

/** Un moteur à la façon d'un V8 américain : alésage, course et zone rouge donnés. */
EngineConfig vee(double bore, double stroke, int redline, Camshaft camshaft) {
    EngineConfig e;
    e.cylinders = 8;
    e.layout = Layout::CrossPlane;
    e.boreMm = static_cast<int>(std::lround(bore));
    e.displacementCc = displacementForStroke(e.boreMm, 8, stroke);
    e.redlineRpm = redline;
    e.camshaft = camshaft;
    return e;
}

/** Le relief d'une bande : le haut des fenêtres de 1 ms face à la médiane, en dB (comme valvetrain_test.cpp). */
double relief(const Samples& x, double fromHz, double toHz) {
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

/** L'énergie d'une bande (dB), deux résonateurs en cascade. */
double bandDb(const Samples& x, double fromHz, double toHz) {
    const double center = std::sqrt(fromHz * toHz);
    const double w = 2.0 * 3.141592653589793 * center / kRate;
    const double r = std::exp(-3.141592653589793 * (toHz - fromHz) / kRate);
    const double a1 = 2.0 * r * std::cos(w);
    const double a2 = -r * r;
    double y1 = 0.0, y2 = 0.0, z1 = 0.0, z2 = 0.0, sum = 0.0;
    for (float s : x) {
        const double y = static_cast<double>(s) + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        const double z = y + a1 * z1 + a2 * z2;
        z2 = z1;
        z1 = z;
        sum += z * z;
    }
    return 10.0 * std::log10(sum / static_cast<double>(x.size()) + 1e-30);
}

struct Heard {
    Samples samples;
    double dbA = 0.0;
};

Heard heard(const EngineProfile& p, float rpm, Listener listener, float seconds = 3.0f) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setListener(listener);
    core.setVolume(0.7f);
    core.setTargetRpm(rpm);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    core.render(out.data(), static_cast<int>(out.size()));
    return {test::slice(out, kRate, out.size()), static_cast<double>(core.levelDbA())};
}

}  // namespace

// ── L'affolement ──

TEST_CASE("affolement : chaque préréglage, ressorts d'origine, flotte au-dessus de sa zone rouge") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    int checked = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const Powertrain p = all[n].powertrain();
        INFO(std::string(all[n].id));
        // Un moteur électrique n'a pas de soupapes, un rotatif non plus : ni affolement, ni rien de ce qui suit.
        if (p.electric || isRotary(p.engine)) {
            if (!p.electric) CHECK(valveFloatRpm(p.engine) == 0.0);
            CHECK(p.profile().valves.floatRpm == 0.0);
            continue;
        }
        CHECK(p.engine.valveSprings == ValveSprings::Stock);
        const double floatRpm = valveFloatRpm(p.engine);
        const double redline = static_cast<double>(p.engine.redlineRpm);
        MESSAGE(all[n].id << " : affolement à " << floatRpm << " tr/min, zone rouge " << redline);
        // La marge du constructeur, moins ce que la souplesse de la chaîne lui prend : de 5 à 15 % au-dessus du rupteur.
        CHECK(floatRpm > 1.05 * redline);
        CHECK(floatRpm < 1.15 * redline);
        // Le rupteur coupe à la zone rouge : la soupape n'atteint jamais l'affolement, et rien ne change au défaut.
        CHECK(valveFloatShare(p.engine, redline * 1.04) == 1.0);
        CHECK(p.profile().valves.floatRpm == doctest::Approx(floatRpm));
        ++checked;
    }
    CHECK(checked >= 17);
}

TEST_CASE("affolement : la décélération au nez suit le carré du régime, la levée et l'inverse du carré de la durée") {
    const CamLobe stock = stockCamLobe(86.0);
    CHECK(stock.liftMm == doctest::Approx(10.32));
    CHECK(stock.durationDeg == 240.0);
    const double base = noseDecelerationMs2(stock, 6000.0);
    CHECK(noseDecelerationMs2(stock, 12000.0) == doctest::Approx(4.0 * base));
    CHECK(noseDecelerationMs2(CamLobe{2.0 * stock.liftMm, stock.durationDeg}, 6000.0) == doctest::Approx(2.0 * base));
    CHECK(noseDecelerationMs2(CamLobe{stock.liftMm, 120.0}, 6000.0) == doctest::Approx(4.0 * base));
    // L'ordre de grandeur d'une came de route : quelques centaines de g au nez, près de la zone rouge.
    CHECK(base / 9.81 > 300.0);
    CHECK(base / 9.81 < 900.0);
}

TEST_CASE("affolement : des ressorts raides le repoussent, et coûtent du frottement") {
    for (const char* id : {"hot_hatch_na", "v8_cross_plane", "retro_coupe", "diesel_four"}) {
        INFO(std::string(id));
        Powertrain stock = findPreset(id)->powertrain();
        Powertrain stiff = stock;
        stiff.engine.valveSprings = ValveSprings::Stiff;
        const double a = valveFloatRpm(stock.engine);
        const double b = valveFloatRpm(stiff.engine);
        // 30 % de force en plus : à peu près √1,3 de régime, un peu moins car la chaîne amplifie davantage plus haut.
        CHECK(b > a * 1.12);
        CHECK(b < a * std::sqrt(1.3) + 1.0);
        const ValveSpring s = valveSpringOf(stock.engine);
        const ValveSpring t = valveSpringOf(stiff.engine);
        CHECK(t.preloadN == doctest::Approx(1.3 * s.preloadN));
        CHECK(t.rateNPerMm == doctest::Approx(1.3 * s.rateNPerMm));
        for (double rpm : {1000.0, 4000.0}) {
            CHECK(frictionTorqueNm(stiff, rpm) > frictionTorqueNm(stock, rpm));
            // Le contact came-poussoir : une part du frottement total, plus grande au ralenti, de 1 à 6 % ici.
            CHECK(frictionTorqueNm(stiff, rpm) < frictionTorqueNm(stock, rpm) * 1.06);
        }
        CHECK(springFrictionDeltaBar(stock.engine, 3000.0) == 0.0);
        // Ce frottement est pris au couple au frein : la courbe baisse d'autant, à la pression de référence près.
        if (stock.engine.aspiration == Aspiration::Natural) {
            const double rpm = 0.8 * static_cast<double>(stock.engine.redlineRpm);
            const double lost = fullLoadTorqueNm(stock, rpm, 0.0) - fullLoadTorqueNm(stiff, rpm, 0.0);
            const double rubbed = frictionTorqueNm(stiff, rpm) - frictionTorqueNm(stock, rpm);
            CHECK(lost > 0.5 * rubbed);
            CHECK(lost < 1.5 * rubbed);
        }
    }
}

TEST_CASE("affolement : une came plus pointue l'abaisse, ressorts d'origine") {
    EngineConfig e = findPreset("hot_hatch_na")->powertrain().engine;
    const double stock = valveFloatRpm(e);
    e.camLiftMm = 12.5f;
    const double lifted = valveFloatRpm(e);
    CHECK(lifted < stock);
    e.camDurationDeg = 200;
    const double pointed = valveFloatRpm(e);
    CHECK(pointed < lifted);
    MESSAGE("came d'origine " << stock << " tr/min, levée 12,5 mm " << lifted << ", sur 200° " << pointed << " ; zone rouge " << e.redlineRpm);
    // Au-delà de la zone rouge, avec cette came : c'est ce que liront le rupteur et la came réglable.
    CHECK(pointed < static_cast<double>(e.redlineRpm));
}

TEST_CASE("affolement : au-delà, la charge chute et la soupape retombe vite ; en dessous, rien ne change") {
    const EngineConfig e = findPreset("hot_hatch_na")->powertrain().engine;
    const ValvetrainProfile v = valvetrainProfileOf(e);
    REQUIRE(v.floatRpm > 0.0);
    CHECK(floatOverload(v, v.floatRpm * 0.99) == 0.0);
    CHECK(floatTrappedShare(v, v.floatRpm * 0.99) == 1.0);
    CHECK(floatLandingMs(v, v.floatRpm) == 0.0);
    CHECK(valveFloatShare(e, v.floatRpm * 0.999) == 1.0);
    double previous = 1.0;
    for (double over : {1.02, 1.05, 1.1, 1.2}) {
        INFO(over);
        const double rpm = v.floatRpm * over;
        CHECK(floatOverload(v, rpm) > 1.0);
        const double kept = floatTrappedShare(v, rpm);
        CHECK(kept < previous);
        previous = kept;
        CHECK(floatLandingMs(v, rpm) > 0.0);
    }
    // 10 % au-dessus de l'affolement : l'admission ferme une vingtaine de degrés trop tard, un sixième de la charge repart.
    const double late = floatLateDeg(v, v.floatRpm * 1.1);
    MESSAGE("10 % au-delà : " << late << "° de retard, charge gardée " << floatTrappedShare(v, v.floatRpm * 1.1)
            << ", arrivée au siège " << floatLandingMs(v, v.floatRpm * 1.1) << " m/s pour " << v.rampMsPerRpm * v.floatRpm * 1.1 << " m/s sur la rampe");
    CHECK(late > 10.0);
    CHECK(late < 45.0);
    CHECK(floatTrappedShare(v, v.floatRpm * 1.1) > 0.7);
    CHECK(floatTrappedShare(v, v.floatRpm * 1.1) < 0.95);
    // Le retard est borné par le flanc de fermeture : la charge ne tombe pas à rien.
    CHECK(floatTrappedShare(v, v.floatRpm * 3.0) > 0.5);
    // Le couple suit : face au même moteur aux ressorts raides, qui ne s'affole pas encore à ce régime, il perd la charge repoussée,
    // à ce que les ressorts raides frottent de plus près.
    const Powertrain p = findPreset("hot_hatch_na")->powertrain();
    Powertrain held = p;
    held.engine.valveSprings = ValveSprings::Stiff;
    REQUIRE(valveFloatRpm(held.engine) > v.floatRpm * 1.1);
    const double kept = floatTrappedShare(v, v.floatRpm * 1.1);
    CHECK(breathingOf(p, v.floatRpm * 1.1) / breathingOf(held, v.floatRpm * 1.1) == doctest::Approx(kept).epsilon(0.01));
    CHECK(breathingOf(p, v.floatRpm * 0.99) / breathingOf(held, v.floatRpm * 0.99) > 0.99);
}

// ── Les culbuteurs ──

TEST_CASE("culbuteurs : deux soupapes en ligne, de la taille de celles d'un simple arbre") {
    for (double bore : {70.0, 86.0, 101.6}) {
        INFO(bore);
        const ValveTrainGeometry ohv = valveTrainOf(Camshaft::Ohv, bore);
        const ValveTrainGeometry sohc = valveTrainOf(Camshaft::Sohc, bore);
        CHECK(ohv.perCylinder == 2);
        CHECK(ohv.intakeMm == sohc.intakeMm);
        CHECK(ohv.intakeAreaMm2 == sohc.intakeAreaMm2);
    }
    EngineConfig e;
    e.camshaft = Camshaft::Ohv;
    CHECK(e.valvetrain() == Camshaft::Ohv);
    CHECK(twoValves(e.valvetrain()));
    CHECK(e.toProfile().camshaft == Camshaft::Ohv);
    e.layout = Layout::Rotary;
    CHECK(e.valvetrain() == Camshaft::Dohc);
    CHECK(e.toProfile().valves.floatRpm == 0.0);
    // Un deux-temps n'a pas de soupapes : rien de tout cela ne s'y applique.
    EngineConfig two;
    two.camshaft = Camshaft::Ohv;
    two.cycle = EngineCycle::TwoStroke;
    CHECK(valveFloatRpm(two) == 0.0);
    CHECK(ohvFrictionDeltaBar(two, 3000.0) == 0.0);
    CHECK(ohvInertiaDelta(two) == 0.0);
}

// Small Block Chevrolet 350 (101,6 × 88,4, 1,5), LS1 (99 × 92, 1,7), Windsor 302 (101,6 × 76,2, 1,6) : masses ramenées de
// 200 à 300 g et fréquences propres de 500 à 900 Hz publiées pour ces distributions (ordres de grandeur, de mémoire).
TEST_CASE("culbuteurs : masse ramenée, rapport, tige et fréquence propre de vrais moteurs") {
    for (const auto [bore, stroke] : {std::pair{101.6, 88.4}, {99.0, 92.0}, {101.6, 76.2}}) {
        INFO(bore << " x " << stroke);
        const ValvetrainParts ohv = valvetrainParts(Camshaft::Ohv, bore, stroke);
        const ValvetrainParts dohc = valvetrainParts(Camshaft::Dohc, bore, stroke);
        CHECK(ohv.ratio == doctest::Approx(1.6));
        CHECK(ohv.effectiveKg > 0.18);
        CHECK(ohv.effectiveKg < 0.3);
        CHECK(ohv.naturalHz > 500.0);
        CHECK(ohv.naturalHz < 900.0);
        CHECK(ohv.pushrodLengthM > 0.15);
        CHECK(ohv.pushrodLengthM < 0.2);
        // La soupape d'un V8 : 90 à 120 g ; une tige d'acier de 7,9 mm : 35 à 60 g.
        CHECK(ohv.valveKg > 0.09);
        CHECK(ohv.valveKg < 0.12);
        CHECK(ohv.pushrodKg > 0.03);
        CHECK(ohv.pushrodKg < 0.06);
        // Un double arbre attaque directement : plus léger, plus raide, de 1,5 à 3 kHz.
        CHECK(dohc.effectiveKg < 0.7 * ohv.effectiveKg);
        CHECK(dohc.naturalHz > 2.0 * ohv.naturalHz);
    }
    // Le LS1 : ressorts d'origine donnés jusqu'à 6300 à 6500 tr/min environ pour un rupteur à 6000.
    const double ls1 = valveFloatRpm(vee(99.0, 92.0, 6000, Camshaft::Ohv));
    MESSAGE("LS1 : affolement à " << ls1 << " tr/min");
    CHECK(ls1 > 6200.0);
    CHECK(ls1 < 6800.0);
}

TEST_CASE("culbuteurs : la chaîne souple prend une part de la marge, plus à haut régime") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t n = 0; n < count; ++n) {
        const Powertrain p = all[n].powertrain();
        if (p.electric || isRotary(p.engine)) continue;
        INFO(std::string(all[n].id));
        const double twin = valveFloatRpm(withCam(p, Camshaft::Dohc).engine);
        const double pushrods = valveFloatRpm(withCam(p, Camshaft::Ohv).engine);
        CHECK(pushrods < twin);
        // Même dessinés pour la zone rouge, les culbuteurs y gardent leur marge sur ces moteurs, au prix de ressorts bien plus forts.
        CHECK(pushrods > static_cast<double>(p.engine.redlineRpm));
        CHECK(valveSpringOf(withCam(p, Camshaft::Ohv).engine).preloadN > 1.3 * valveSpringOf(withCam(p, Camshaft::Dohc).engine).preloadN);
    }
}

TEST_CASE("culbuteurs : plus de frottement que le simple arbre, un seul arbre à lancer") {
    for (const char* id : {"hot_hatch_na", "v8_cross_plane", "v6_gt"}) {
        INFO(std::string(id));
        const Powertrain twin = withCam(findPreset(id)->powertrain(), Camshaft::Dohc);
        const Powertrain single = withCam(findPreset(id)->powertrain(), Camshaft::Sohc);
        const Powertrain pushrods = withCam(findPreset(id)->powertrain(), Camshaft::Ohv);
        for (double rpm : {1000.0, 3000.0, 6000.0}) {
            CHECK(frictionTorqueNm(pushrods, rpm) > frictionTorqueNm(single, rpm));
            // Poussoirs plats et ressorts plus forts : jusqu'à 15 % du frottement total au ralenti, où la came pèse le plus.
            CHECK(frictionTorqueNm(pushrods, rpm) < frictionTorqueNm(twin, rpm) * 1.15);
        }
        CHECK(engineInertia(pushrods) < engineInertia(twin));
        CHECK(engineInertia(pushrods) > engineInertia(twin) * 0.99);
        if (twin.engine.layout != Layout::Inline) CHECK(engineInertia(pushrods) < engineInertia(single));
    }
}

TEST_CASE("culbuteurs : la courbe d'un deux soupapes, un peu plus de frottement, et tout le modèle suit") {
    for (const char* id : {"hot_hatch_na", "v8_cross_plane", "v8_supercharged", "diesel_four", "hot_hatch_turbo"}) {
        INFO(std::string(id));
        const Powertrain twin = withCam(findPreset(id)->powertrain(), Camshaft::Dohc);
        const Powertrain single = withCam(findPreset(id)->powertrain(), Camshaft::Sohc);
        const Powertrain pushrods = withCam(findPreset(id)->powertrain(), Camshaft::Ohv);
        const PowerSummary a = powerSummary(twin);
        const PowerSummary c = powerSummary(pushrods);
        CHECK(c.peakPowerKw < a.peakPowerKw);
        CHECK(c.peakPowerKw < powerSummary(single).peakPowerKw);
        CHECK(c.peakPowerKw > 0.95 * powerSummary(single).peakPowerKw);
        DrivingModel model(pushrods);
        model.crank();
        for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.0, 0.0);
        CHECK(model.rpm() > 300.0);
        const Performance performance = measurePerformance(pushrods);
        CHECK(performance.zeroTo100S > 3.0);
        CHECK(performance.zeroTo100S < 25.0);
    }
}

// ── Le son ──

TEST_CASE("culbuteurs : le choc des poussoirs se détache sous le capot au ralenti, sans passer devant l'échappement") {
    const EngineProfile single = withCam(findPreset("v8_cross_plane")->powertrain(), Camshaft::Sohc).profile();
    EngineProfile pushrods = withCam(findPreset("v8_cross_plane")->powertrain(), Camshaft::Ohv).profile();
    REQUIRE(pushrods.valves.pushrodTick > 1.0);
    REQUIRE(pushrods.valves.pushrodHz > 1000.0);
    MESSAGE("impulsion au poussoir face au culbuteur : " << pushrods.valves.pushrodTick << ", tige à " << pushrods.valves.pushrodHz << " Hz");
    const double low = 0.6 * pushrods.valves.pushrodHz;
    const double high = 1.5 * pushrods.valves.pushrodHz;
    const auto tick = [&](const EngineProfile& p, float rpm, Listener listener) { return relief(heard(p, rpm, listener).samples, low, high); };
    const double bayIdle = tick(pushrods, 800.0f, Listener::EngineBay) - tick(single, 800.0f, Listener::EngineBay);
    const double outsideIdle = tick(pushrods, 800.0f, Listener::Outside) - tick(single, 800.0f, Listener::Outside);
    MESSAGE("relief du choc, face au simple arbre : capot au ralenti " << bayIdle << " dB, dehors " << outsideIdle << " dB");
    CHECK(bayIdle > 1.0);
    CHECK(outsideIdle < 1.0);
    // « Tout ce qui ne vient pas de l'échappement garde sa place à bas régime » : dehors, 8-16 kHz face à 500-2000 Hz ne bouge guère.
    const Samples outPush = heard(pushrods, 800.0f, Listener::Outside).samples;
    const Samples outSingle = heard(single, 800.0f, Listener::Outside).samples;
    const double ratioPush = bandDb(outPush, 8000.0, 16000.0) - bandDb(outPush, 500.0, 2000.0);
    const double ratioSingle = bandDb(outSingle, 8000.0, 16000.0) - bandDb(outSingle, 500.0, 2000.0);
    MESSAGE("dehors au ralenti, 8-16 kHz sur 500-2000 Hz : " << ratioPush << " dB contre " << ratioSingle << " dB");
    CHECK(std::fabs(ratioPush - ratioSingle) < 1.5);
    // Contre-épreuve : sans le choc au poussoir, le relief retombe au niveau du simple arbre ou en dessous.
    pushrods.valves.pushrodTick = 0.0;
    CHECK(tick(pushrods, 800.0f, Listener::EngineBay) - tick(single, 800.0f, Listener::EngineBay) < bayIdle - 1.0);
}

/** L'écart des énergies d'un cycle à l'autre (écart type sur moyenne), par fenêtres d'un cycle de quatre temps. */
double cycleSpread(const Samples& x, int window) {
    std::vector<double> energy;
    for (std::size_t i = 0; i + static_cast<std::size_t>(window) <= x.size(); i += static_cast<std::size_t>(window)) {
        double sum = 0.0;
        for (int k = 0; k < window; ++k) sum += static_cast<double>(x[i + static_cast<std::size_t>(k)]) * x[i + static_cast<std::size_t>(k)];
        energy.push_back(sum);
    }
    double mean = 0.0, variance = 0.0;
    for (double e : energy) mean += e;
    mean /= static_cast<double>(energy.size());
    for (double e : energy) variance += (e - mean) * (e - mean);
    return std::sqrt(variance / static_cast<double>(energy.size())) / mean;
}

TEST_CASE("affolement : sous lui, le son ne change pas d'un bit ; au-delà, la charge chute, les cycles se dispersent, le siège claque") {
    const EngineProfile real = findPreset("hot_hatch_na")->powertrain().profile();
    // Contre-épreuve : le même moteur, dont les soupapes ne s'affoleraient jamais.
    EngineProfile never = real;
    never.valves.floatRpm = 0.0;
    const float below = static_cast<float>(real.valves.floatRpm * 0.95);
    CHECK(heard(real, below, Listener::EngineBay, 1.5f).samples == heard(never, below, Listener::EngineBay, 1.5f).samples);
    // Un peu au-delà : moins de charge, des bouffées plus faibles, et plus inégales d'un cycle à l'autre.
    const float above = static_cast<float>(real.valves.floatRpm * 1.15);
    const Heard outFloating = heard(real, above, Listener::Outside);
    const Heard outHeld = heard(never, above, Listener::Outside);
    const int cycle = static_cast<int>(static_cast<float>(kRate) * 120.0f / above);
    const double spreadFloating = cycleSpread(outFloating.samples, cycle);
    const double spreadHeld = cycleSpread(outHeld.samples, cycle);
    MESSAGE("à " << above << " tr/min, dehors : " << outFloating.dbA - outHeld.dbA << " dB(A), écart d'un cycle à l'autre " << spreadFloating << " contre " << spreadHeld);
    CHECK(outFloating.dbA < outHeld.dbA - 1.0);
    CHECK(spreadFloating > spreadHeld * 1.3);
    // Bien au-delà, la soupape retombe vite sur son siège et rebondit : sous le capot, au-dessus de ce que rayonne le bloc.
    const float far = static_cast<float>(real.valves.floatRpm * 1.25);
    const Samples floating = heard(real, far, Listener::EngineBay).samples;
    const Samples held = heard(never, far, Listener::EngineBay).samples;
    const double seat = bandDb(floating, 6000.0, 12000.0) - bandDb(held, 6000.0, 12000.0);
    MESSAGE("à " << far << " tr/min, sous le capot : 6-12 kHz " << seat << " dB");
    CHECK(seat > 1.0);
}

TEST_CASE("culbuteurs et affolement : temps réel, blocs") {
    EngineProfile p = withCam(findPreset("v8_cross_plane")->powertrain(), Camshaft::Ohv).profile();
    const float rpm = static_cast<float>(p.valves.floatRpm * 1.1);
    EngineSound whole(kRate);
    EngineSound parts(kRate);
    whole.setProfile(p);
    parts.setProfile(p);
    whole.setVolume(0.7f);
    parts.setVolume(0.7f);
    whole.setTargetRpm(rpm);
    parts.setTargetRpm(rpm);
    Samples a(kRate);
    Samples b(kRate);
    whole.render(a.data(), kRate);
    for (int at = 0; at < kRate;) {
        const int n = std::min(137, kRate - at);
        parts.render(b.data() + at, n);
        at += n;
    }
    CHECK(a == b);
    Samples buffer(4096);
    const long before = g_allocations.load();
    for (int i = 0; i < 10; ++i) whole.render(buffer.data(), 4096);
    CHECK(g_allocations.load() == before);
}
