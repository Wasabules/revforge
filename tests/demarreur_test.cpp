#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "doctest.h"
#include "revforge/cold_start.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

Powertrain presetCold(const char* id, double ambientC) {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.startTemp = StartTemp::Cold;
    p.engine.ambientC = static_cast<float>(ambientC);
    return p;
}

/** Le relevé d'un démarrage : la clé tenue jusqu'à la prise, au plus `seconds`. */
struct Crank {
    StartStats stats;
    bool running = false;
    bool failed = false;
    double secondsToRun = -1.0;
};

Crank crankFor(const Powertrain& p, double seconds) {
    DrivingModel model(p);
    model.halt();
    model.crank();
    Crank out;
    const int steps = static_cast<int>(seconds * 1000.0);
    for (int i = 0; i < steps; ++i) {
        if (i == 10000) model.releaseKey();
        model.stepNeutral(0.001, 0.0);
        if (out.secondsToRun < 0.0 && !model.cranking() && model.preheatLeft() <= 0.0 && model.rpm() > 0.8 * model.idleRpm()) out.secondsToRun = (i + 1) * 0.001;
    }
    out.stats = model.start().stats();
    out.running = !model.stalled() && model.rpm() > 0.8 * model.idleRpm();
    out.failed = model.startFailed();
    return out;
}

}  // namespace

TEST_CASE("démarreur : moteur chaud par 20 °C, la batterie de référence ne change rien, au bit près") {
    // Le démarreur d'origine est calé sur sa batterie de référence : moteur chaud par 20 °C, pleine. Ici, la batterie réelle
    // est cette référence, et l'écart de son courant (ce qui change le couple et le son) vaut exactement zéro à chaque pas.
    for (const char* id : {"city_14", "v8_cross_plane", "diesel_four", "carb_twin_cam", "twin_rotor_13", "two_stroke_triple"}) {
        CAPTURE(std::string(id));
        DrivingModel model(findPreset(id)->powertrain());
        model.halt();
        model.crank();
        bool exact = true;
        double lowest = 99.0;
        for (int i = 0; i < 2000; ++i) {
            model.stepNeutral(0.001, 0.0);
            exact = exact && model.starterSupply() == 0.0;
            if (model.cranking()) lowest = std::min(lowest, model.batteryVolts());
        }
        CHECK(exact);
        // Une batterie saine sous le démarreur, moteur chaud : de 9 à 11 V (essai de charge : au moins 9,6 V à mi-courant nominal).
        CHECK(lowest > 8.5);
        CHECK(lowest < 11.5);
        // Et le moteur tourne : rien du démarrage d'origine n'a changé.
        CHECK(model.rpm() > model.idleRpm() * 0.9);
        CHECK(model.start().stats().misfires == 0);
    }
}

TEST_CASE("batterie : l'essai de démarrage à froid de l'EN 50342-1, à -18 °C") {
    // 7,5 V après 10 s au courant nominal ; puis, après 10 s de repos, 6 V atteints au bout de 73 s à 60 % de ce courant.
    EngineConfig e = findPreset("city_14")->powertrain().engine;
    const BatterySpec spec = batterySpecOf(e);
    BatteryState state;
    state.celsius = -18.0;
    for (int i = 0; i < 1000; ++i) stepBattery(spec, state, spec.coldCrankingA, 0.01);
    CHECK(batteryVolts(spec, state, spec.coldCrankingA) == doctest::Approx(7.5).epsilon(0.01));
    for (int i = 0; i < 1000; ++i) stepBattery(spec, state, 0.0, 0.01);
    double seconds = 0.0;
    while (batteryVolts(spec, state, 0.6 * spec.coldCrankingA) > 6.0 && seconds < 300.0) {
        stepBattery(spec, state, 0.6 * spec.coldCrankingA, 0.01);
        seconds += 0.01;
    }
    CHECK(seconds == doctest::Approx(73.0).epsilon(0.03));
    // Au chaud, la même batterie tient bien mieux : 30 % de résistance en moins à 25 °C qu'à -18 °C (CA = 1,2·CCA à 0 °C).
    CHECK(batteryOhms(spec, 25.0, 1.0) < 0.75 * batteryOhms(spec, -18.0, 1.0));
    // À moitié vide, par -30 °C, l'électrolyte gèle (-24 °C à mi-charge) : la batterie ne débite presque plus.
    CHECK(electrolyteFreezeC(0.5) > -30.0);
    CHECK(batteryOhms(spec, -30.0, 0.5) > 10.0 * batteryOhms(spec, -30.0, 1.0));
}

TEST_CASE("démarreur : par -30 °C, une 5W-30 lance le moteur dans la plage publiée, et il peine") {
    const Crank warm = crankFor(presetCold("city_14", 20.0), 12.0);
    const Crank cold = crankFor(presetCold("city_14", -30.0), 12.0);
    // 60 à 150 tr/min publiés pour un entraînement à froid ; 150 à 300 moteur à 20 °C.
    CHECK(cold.stats.meanCrankRpm > 60.0);
    CHECK(cold.stats.meanCrankRpm < 170.0);
    CHECK(warm.stats.meanCrankRpm > 200.0);
    // Il prend quand même, mais en plus de temps, et la batterie descend plus bas.
    CHECK(cold.running);
    CHECK(cold.secondsToRun > warm.secondsToRun + 0.5);
    CHECK(cold.stats.minVolts < warm.stats.minVolts - 1.0);
}

TEST_CASE("huile : une monograde d'été empêche la Cadillac de démarrer par -20 °C, une 0W-20 la fait partir") {
    Powertrain summer = presetCold("v16_cadillac", -20.0);
    REQUIRE(summer.engine.oilGrade == OilGrade::Sae30);
    Powertrain winter = summer;
    winter.engine.oilGrade = OilGrade::Sae0W20;
    const Crank thick = crankFor(summer, 14.0);
    const Crank thin = crankFor(winter, 14.0);
    // La SAE 30 à -20 °C : plus de 10 Pa·s, cent fois une 5W-30 à -20 °C (1,7 Pa·s).
    CHECK(oilViscosityPaS(OilGrade::Sae30, -20.0) > 10.0);
    CHECK(oilViscosityPaS(OilGrade::Sae5W30, -20.0) < 2.0);
    CHECK(thick.stats.meanCrankRpm < 0.6 * thin.stats.meanCrankRpm);
    CHECK_FALSE(thick.running);
    CHECK(thick.failed);
    CHECK(thin.running);
}

TEST_CASE("huile : au-dessus de 80 °C, chaque grade frotte comme la référence ; sa classe J300 se lit au froid") {
    for (OilGrade grade : {OilGrade::Sae0W20, OilGrade::Sae10W40, OilGrade::Sae15W40, OilGrade::Sae20W50, OilGrade::Sae30}) {
        CAPTURE(static_cast<int>(grade));
        for (double oil : {80.0, 95.0, 100.0, 112.0}) {
            CHECK(runningViscosityExcess(grade, oil) == coldViscosityExcess(oil));
            CHECK(crankingViscosityExcess(grade, oil) == coldViscosityExcess(oil));
        }
    }
    // Chaque classe W passe la limite CCS de sa température à la marge de la référence (4250 mPa·s pour 6600 à -30 °C).
    const double margin = 4250.0 / 6600.0;
    CHECK(oilViscosityPaS(OilGrade::Sae0W20, -35.0) == doctest::Approx(6.2 * margin).epsilon(0.001));
    CHECK(oilViscosityPaS(OilGrade::Sae10W40, -25.0) == doctest::Approx(7.0 * margin).epsilon(0.001));
    CHECK(oilViscosityPaS(OilGrade::Sae15W40, -20.0) == doctest::Approx(7.0 * margin).epsilon(0.001));
    CHECK(oilViscosityPaS(OilGrade::Sae20W50, -15.0) == doctest::Approx(9.5 * margin).epsilon(0.001));
    // Au lancement, l'huile froide pèse plus qu'en tournant : l'exposant 0,86 de la table d'Exide, contre la racine du moteur qui tourne.
    CHECK(crankingViscosityExcess(OilGrade::Sae5W30, -30.0) > 5.0 * runningViscosityExcess(OilGrade::Sae5W30, -30.0));
}

TEST_CASE("essence : ce qui se vaporise d'une essence d'hiver, et l'E85 qui ne se vaporise plus") {
    // À 20 °C, presque tout ce qui arrive à la stœchiométrie ; par -30 °C, les seules fractions légères.
    CHECK(vaporizedShare(FuelGrade::Ron95, 1.0, 20.0, 1.013) > 0.8);
    const double cold = vaporizedShare(FuelGrade::Ron95, 4.0, -30.0, 1.013);
    CHECK(cold > 0.12);
    CHECK(cold < 0.3);
    // Par -30 °C, le calculateur doit injecter près de vingt fois la stœchiométrie pour une vapeur riche (λ 0,77) ; à 20 °C, 1,5 fois.
    CHECK(startFuelShare(FuelGrade::Ron95, 20.0, 1.013, 1.3) < 2.0);
    CHECK(startFuelShare(FuelGrade::Ron95, -30.0, 1.013, 1.3) > 12.0);
    // L'E85 par -20 °C : même au plus fort de l'injection, la vapeur reste sous la limite pauvre d'allumage.
    const double e85 = startFuelShare(FuelGrade::E85, -20.0, 1.013, 1.3);
    CHECK(e85 * vaporizedShare(FuelGrade::E85, e85, -20.0, 1.013) < kLeanFiringPhi);
    CHECK(petrolFiringChance(0.4) == 0.0);
    CHECK(petrolFiringChance(1.3) == 1.0);
}

TEST_CASE("diesel : lancé trop lentement par -30 °C, il rate ; la bougie chaude en allume une part ; plus vite, il part") {
    const EngineConfig e = findPreset("diesel_four")->powertrain().engine;
    const double stroke = static_cast<double>(e.strokeMm());
    // L'essai de compression : 25 à 31 bar sur un diesel de série, moteur chaud, à 250 tr/min.
    const CrankCompression test = crankCompression(250.0, 18.0, e.effectiveBoreMm(), stroke, 20.0, 80.0, 1.013, 30.0);
    CHECK(test.tdcBar > 25.0);
    CHECK(test.tdcBar < 34.0);
    // Lancé lentement, le gaz fuit et perd sa chaleur aux parois froides : l'exposant tombe, et la pression avec lui.
    const CrankCompression slow = crankCompression(90.0, 18.0, e.effectiveBoreMm(), stroke, -30.0, -30.0, 1.013, 30.0);
    CHECK(slow.polytropic < test.polytropic - 0.05);
    CHECK(slow.tdcBar < 17.0);
    const double piston = 2.0 * stroke / 1000.0 * 90.0 / 60.0;
    const double bare = crankIgnitionDelayDeg(slow, piston, 0.0);
    const double glowing = crankIgnitionDelayDeg(slow, piston, slow.tdcK + 0.25 * (glowPlugC(-30.0, 10.0) + 273.15 - slow.tdcK));
    CHECK(dieselFiringChance(bare) == 0.0);
    CHECK(dieselFiringChance(glowing) > dieselFiringChance(bare) + 0.2);
    // À 150 tr/min, la pression suffit : le délai de Hardenberg et Hase, calé moteur tournant, pèse surtout par elle.
    const CrankCompression brisk = crankCompression(150.0, 18.0, e.effectiveBoreMm(), stroke, -30.0, -30.0, 1.013, 30.0);
    CHECK(dieselFiringChance(crankIgnitionDelayDeg(brisk, 2.0 * stroke / 1000.0 * 150.0 / 60.0, 0.0)) == 1.0);
    // Le préchauffage ne sert que moteur froid.
    CHECK(glowPreheatS(90.0) == 0.0);
    CHECK(glowPreheatS(-20.0) > 2.0);
}

TEST_CASE("démarrage raté : la clé lâchée, le calculateur renonce, et l'on peut réessayer") {
    // La Cadillac en huile d'été par -30 °C ne prend pas : le calculateur tient le démarreur 10 s, puis le démarrage est raté.
    DrivingModel model(presetCold("v16_cadillac", -30.0));
    model.halt();
    model.crank();
    model.releaseKey();
    for (int i = 0; i < 9000; ++i) model.stepNeutral(0.001, 0.0);
    CHECK(model.cranking());
    CHECK_FALSE(model.startFailed());
    for (int i = 0; i < 2000; ++i) model.stepNeutral(0.001, 0.0);
    CHECK_FALSE(model.cranking());
    CHECK(model.startFailed());
    CHECK(model.stalled());
    // Et la batterie, qui s'est vidée de ses plaques, remonte au repos.
    const double tired = model.batteryVolts();
    for (int i = 0; i < 30000; ++i) model.stepNeutral(0.001, 0.0);
    CHECK(model.batteryVolts() > tired + 0.02);
    model.crank();
    CHECK(model.cranking());
    CHECK_FALSE(model.startFailed());
}

TEST_CASE("son du lancement : les explosions isolées s'entendent, de leur propre tirage") {
    const Powertrain p = findPreset("city_14")->powertrain();
    const auto render = [&](const StartDrive& drive) {
        EngineSound sound(44100);
        sound.setProfile(p.profile());
        std::vector<float> out(44100);
        for (int block = 0; block < 44100 / 128; ++block) {
            sound.setTargetRpm(150.0f);
            sound.setDrive(0.0f, 0.0f, true, true, 0.0f);
            // Sans le démarreur lui-même : seules les compressions et les explosions s'entendent.
            sound.setStarter(false, false, 0.0f);
            sound.setStart(drive);
            sound.render(out.data() + block * 128, 128);
        }
        double energy = 0.0;
        for (float s : out) energy += static_cast<double>(s) * s;
        return std::vector<double>{energy, static_cast<double>(out[30000])};
    };
    StartDrive off;
    StartDrive firing;
    firing.active = true;
    firing.firing = 1.0f;
    firing.fill = 0.85f;
    StartDrive missing = firing;
    missing.firing = 0.0f;
    const std::vector<double> silent = render(off);
    const std::vector<double> fired = render(firing);
    const std::vector<double> missed = render(missing);
    // Les explosions ajoutent nettement d'énergie aux compressions seules ; les ratés n'en ajoutent pas.
    CHECK(fired[0] > 2.0 * silent[0]);
    CHECK(missed[0] < 1.2 * silent[0]);
}
