#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"

extern std::atomic<long> g_allocations;

using namespace revforge;

namespace {

constexpr double kMs = 0.001;
constexpr ShiftMode kAutoModes[] = {ShiftMode::Eco, ShiftMode::Normal, ShiftMode::Sport, ShiftMode::Track};
constexpr GearboxKind kBoxes[] = {GearboxKind::Synchro, GearboxKind::Dog, GearboxKind::Sequential, GearboxKind::DualClutch, GearboxKind::Converter};
// Des moteurs de toutes sortes : un petit, un turbo, un qui tourne haut, un diesel au rupteur bas, un gros V.
const char* const kEngines[] = {"city_14", "hot_hatch_turbo", "inline_six", "diesel_four", "v10_open"};

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

Powertrain withBox(const char* id, GearboxKind kind) {
    Powertrain p = preset(id);
    p.gearbox.kind = kind;
    p.gearbox.form = kind == GearboxKind::Dog || kind == GearboxKind::Sequential ? ToothForm::Spur : ToothForm::Helical;
    return p;
}

/** Pied constant depuis l'arrêt : le régime au moment où part le passage de ce rapport au suivant. 0 s'il n'a pas lieu. */
double upshiftRpmAt(const Powertrain& p, ShiftMode mode, double pedal, int fromGear, double seconds = 40.0) {
    DrivingModel model(p);
    model.setShiftMode(mode);
    bool was = false;
    for (double t = 0.0; t < seconds; t += kMs) {
        model.stepPedal(kMs, pedal, 0.0);
        const bool now = model.shifting();
        if (now && !was && model.pendingGear() == fromGear + 1) return model.rpm();
        was = now;
    }
    return 0.0;
}

double zeroTo100(const Powertrain& p, ShiftMode mode) {
    DrivingModel model(p);
    model.setShiftMode(mode);
    for (double t = 0.0; t < 60.0; t += kMs) {
        model.stepPedal(kMs, 1.0, 0.0);
        if (model.speedKmh() >= 100.0) return t;
    }
    return 1e9;
}

/** Un pied qui cherche à tenir la vitesse, comme un régulateur. */
double holdPedal(const DrivingModel& model, double targetKmh) { return std::clamp(0.15 + (targetKmh - model.speedKmh()) * 0.08, 0.0, 1.0); }

/** Mène la voiture à cette vitesse, stabilisée. */
void settleAt(DrivingModel& model, double targetKmh, double seconds = 40.0) {
    for (double t = 0.0; t < seconds; t += kMs) model.stepPedal(kMs, holdPedal(model, targetKmh), 0.0);
}

/** Un trajet joué en entier, comme le fait le site : par tranches de 50 ms. */
struct Lap {
    bool finite = true;
    bool stalled = false;
    bool broken = false;
    double maxRpm = 0.0;
    double maxSpeed = 0.0;
    double underpoweredSec = 0.0;
    int shifts = 0;
    std::vector<double> legTop;   // vitesse la plus haute de chaque étape
    std::vector<double> legEnd;   // vitesse à la fin de chaque étape
    std::vector<double> legRise;  // ce qu'elle gagnait encore, sur les 2 dernières secondes de l'étape
};

Lap playScenario(const Powertrain& p, DriveScenario scenario, ShiftMode mode = ShiftMode::Normal) {
    const Scenario& s = scenarioOf(scenario);
    DrivingModel model(p);
    model.setShiftMode(mode);
    model.setDriveScenario(scenario);
    Lap lap;
    lap.legTop.assign(static_cast<std::size_t>(s.legCount), 0.0);
    lap.legEnd.assign(static_cast<std::size_t>(s.legCount), 0.0);
    lap.legRise.assign(static_cast<std::size_t>(s.legCount), 0.0);
    double past[40] = {};
    int tick = 0;
    constexpr double kDt = 0.05;
    bool was = false;
    for (double t = 0.0; t < s.loopSec - kDt; t += kDt) {
        model.step(kDt);
        lap.finite = lap.finite && std::isfinite(model.rpm()) && std::isfinite(model.speedKmh()) && std::isfinite(model.torqueNm());
        lap.stalled = lap.stalled || model.stalled();
        lap.broken = lap.broken || model.broken();
        lap.maxRpm = std::max(lap.maxRpm, model.rpm());
        lap.maxSpeed = std::max(lap.maxSpeed, model.speedKmh());
        if (model.underpowered()) lap.underpoweredSec += kDt;
        if (model.shifting() && !was && model.gear() != 0) ++lap.shifts;
        was = model.shifting();
        const auto leg = static_cast<std::size_t>(model.legIndex());
        lap.legTop[leg] = std::max(lap.legTop[leg], model.speedKmh());
        lap.legEnd[leg] = model.speedKmh();
        lap.legRise[leg] = model.speedKmh() - past[tick % 40];
        past[tick++ % 40] = model.speedKmh();
    }
    return lap;
}

}  // namespace

// ── Les points de passage ──

TEST_CASE("à même pédale, Éco passe avant Normal, Normal avant Sport, Sport avant Piste") {
    for (const char* id : kEngines) {
        const Powertrain p = preset(id);
        for (double pedal : {0.35, 0.6, 1.0}) {
            INFO(std::string(id) << " pédale " << pedal);
            const double eco = upshiftRpmAt(p, ShiftMode::Eco, pedal, 1);
            const double normal = upshiftRpmAt(p, ShiftMode::Normal, pedal, 1);
            const double sport = upshiftRpmAt(p, ShiftMode::Sport, pedal, 1);
            const double track = upshiftRpmAt(p, ShiftMode::Track, pedal, 1);
            REQUIRE(eco > 0.0);
            REQUIRE(normal > 0.0);
            CHECK(eco < normal);
            REQUIRE(sport > 0.0);
            CHECK(normal < sport);
            // Un petit moteur à mi-pédale peut mettre longtemps à atteindre le point du mode piste : ne pas passer, c'est tenir.
            if (track > 0.0) CHECK(sport <= track + 5.0);
            CHECK(eco > idleRpmOf(p));
            CHECK(std::max({eco, normal, sport, track}) <= redlineRpmOf(p) * 1.005);
        }
    }
}

TEST_CASE("les points de passage restent ordonnés à toute demande, sur tous les rapports et tous les moteurs") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        if (p.electric) continue;
        const Gearing gearing = gearingOf(p);
        const ShiftMap map = buildShiftMap(p, gearing);
        for (int gear = 1; gear < gearing.gears; ++gear) {
            for (double demand = 0.0; demand <= 1.0; demand += 0.05) {
                INFO(std::string(all[i].id) << " rapport " << gear << " demande " << demand);
                const double eco = upshiftPoint(map, ShiftMode::Eco, gear, demand);
                const double normal = upshiftPoint(map, ShiftMode::Normal, gear, demand);
                const double sport = upshiftPoint(map, ShiftMode::Sport, gear, demand);
                const double track = upshiftPoint(map, ShiftMode::Track, gear, demand);
                CHECK(eco < normal);
                CHECK(normal < sport);
                CHECK(sport <= track);
                CHECK(track < map.redline);
                // Monter doit laisser le moteur au-dessus de ce qui le ferait redescendre aussitôt : c'est l'hystérésis.
                CHECK(sport / map.stepAt(gear) > downshiftFloor(map, ShiftMode::Sport));
                CHECK(track / map.stepAt(gear) > downshiftFloor(map, ShiftMode::Track));
            }
        }
    }
}

TEST_CASE("les points viennent des courbes et des rapports, pas d'une table") {
    Powertrain natural = preset("hot_hatch_turbo");
    natural.engine.aspiration = Aspiration::Natural;
    const Powertrain turbo = preset("hot_hatch_turbo");
    const ShiftMap a = buildShiftMap(natural, gearingOf(natural));
    const ShiftMap b = buildShiftMap(turbo, gearingOf(turbo));
    REQUIRE(a.redline == doctest::Approx(b.redline));
    // Même régime maximal, autre courbe : le point à fond de l'éco suit le couple.
    CHECK(std::fabs(upshiftPoint(a, ShiftMode::Eco, 2, 1.0) - upshiftPoint(b, ShiftMode::Eco, 2, 1.0)) > 100.0);

    // Un écart de rapports plus grand fait monter le point doux du sport : il ne faut pas retomber sous le plancher.
    Powertrain close = preset("city_14");
    close.gearbox.family = GearFamily::Close;
    Powertrain wide = preset("city_14");
    wide.gearbox.family = GearFamily::Custom;
    wide.gearbox.gears = 5;
    wide.gearbox.ratios = {4.2f, 2.0f, 1.3f, 0.95f, 0.75f};
    const ShiftMap c = buildShiftMap(close, gearingOf(close));
    const ShiftMap w = buildShiftMap(wide, gearingOf(wide));
    REQUIRE(w.stepAt(1) > c.stepAt(1));
    CHECK(upshiftPoint(w, ShiftMode::Sport, 1, 0.0) > upshiftPoint(c, ShiftMode::Sport, 1, 0.0) + 100.0);
}

TEST_CASE("le point où le rapport suivant tire autant vient vraiment de la courbe de couple") {
    const Powertrain p = preset("v10_open");
    const Gearing gearing = gearingOf(p);
    const ShiftMap map = buildShiftMap(p, gearing);
    int crossings = 0;
    for (int gear = 1; gear < gearing.gears; ++gear) {
        INFO("rapport " << gear);
        const double at = map.crossing[static_cast<std::size_t>(gear - 1)];
        const double here = gearing.overall[static_cast<std::size_t>(gear - 1)];
        const double next = gearing.overall[static_cast<std::size_t>(gear)];
        const double step = map.stepAt(gear);
        if (at < map.redline) {
            CHECK(map.torqueAt(at / step) * next >= map.torqueAt(at) * here * 0.99);
            ++crossings;
        }
        // Avant ce point, le rapport engagé tire plus fort : pas question de monter plus tôt.
        CHECK(map.torqueAt((at - 100.0) / step) * next <= map.torqueAt(at - 100.0) * here * 1.01);
    }
    // Sur ses hauts rapports serrés, ce moteur a intérêt à passer avant le rupteur : le croisement existe.
    CHECK(crossings > 0);
}

// ── Pas d'hésitation ──

TEST_CASE("à vitesse tenue, aucune hésitation entre deux rapports") {
    for (const char* id : kEngines) {
        const Powertrain p = preset(id);
        for (ShiftMode mode : kAutoModes) {
            for (double kmh : {50.0, 90.0, 120.0}) {
                INFO(std::string(id) << " mode " << static_cast<int>(mode) << " à " << kmh << " km/h");
                DrivingModel model(p);
                model.setShiftMode(mode);
                settleAt(model, kmh);
                int shifts = 0;
                bool was = false;
                for (double t = 0.0; t < 60.0; t += kMs) {
                    model.stepPedal(kMs, holdPedal(model, kmh), 0.0);
                    if (model.shifting() && !was) ++shifts;
                    was = model.shifting();
                }
                CHECK(shifts <= 1);
                CHECK(model.speedKmh() == doctest::Approx(kmh).epsilon(0.12));
            }
        }
    }
}

TEST_CASE("dans une longue côte, les modes n'hésitent pas : le rapport suivant doit pouvoir tenir") {
    for (const char* id : {"city_14", "three_cyl_turbo", "inline_six", "diesel_four"}) {
        const Powertrain p = preset(id);
        for (ShiftMode mode : kAutoModes) {
            INFO(std::string(id) << " mode " << static_cast<int>(mode));
            DrivingModel model(p);
            model.setShiftMode(mode);
            model.setRoadGrade(8.0);
            settleAt(model, 70.0);
            int shifts = 0;
            bool was = false;
            for (double t = 0.0; t < 40.0; t += kMs) {
                model.stepPedal(kMs, holdPedal(model, 70.0), 0.0);
                if (model.shifting() && !was) ++shifts;
                was = model.shifting();
            }
            CHECK(shifts <= 2);
        }
    }
}

// ── Rétrograder ──

TEST_CASE("le kick-down fait descendre un rapport, s'il ne fait pas sur-régimer") {
    int dropped = 0;
    for (const char* id : kEngines) {
        const Powertrain p = preset(id);
        const Gearing gearing = gearingOf(p);
        const ShiftMap map = buildShiftMap(p, gearing);
        for (ShiftMode mode : kAutoModes) {
            INFO(std::string(id) << " mode " << static_cast<int>(mode));
            DrivingModel model(p);
            model.setShiftMode(mode);
            settleAt(model, 70.0, 50.0);
            const int before = model.gear();
            if (before < 2) continue;
            const double below = model.rpm() * map.stepAt(before - 1);
            // Là où le rapport du dessous a de la marge, il doit y descendre. Le mode normal garde sa règle d'origine.
            const bool room = below < upshiftPoint(map, mode, before - 1, 1.0) * 0.8 && (mode != ShiftMode::Normal || model.rpm() < map.redline * 0.4);
            int lowest = before;
            double top = 0.0;
            for (double t = 0.0; t < 3.0; t += kMs) {
                model.stepPedal(kMs, 1.0, 0.0);
                lowest = std::min(lowest, model.gear());
                top = std::max(top, model.rpm());
            }
            if (room) {
                CHECK(lowest < before);
                ++dropped;
            }
            CHECK(top <= map.redline * 1.03);
        }
    }
    CHECK(dropped >= 8);
    // À mi-course, le sport rétrograde ; l'éco, lui, attend le plancher.
    const Powertrain p = preset("inline_six");
    const auto lowestAt = [&p](ShiftMode mode, double pedal) {
        DrivingModel model(p);
        model.setShiftMode(mode);
        settleAt(model, 70.0, 50.0);
        const int before = model.gear();
        int lowest = before;
        for (double t = 0.0; t < 3.0; t += kMs) {
            model.stepPedal(kMs, pedal, 0.0);
            lowest = std::min(lowest, model.gear());
        }
        return before - lowest;
    };
    CHECK(lowestAt(ShiftMode::Sport, 0.72) >= 1);
    CHECK(lowestAt(ShiftMode::Eco, 0.72) == 0);
}

TEST_CASE("aucun rétrogradage ne fait sur-régimer le moteur") {
    for (const char* id : kEngines) {
        const Powertrain p = preset(id);
        const double redline = redlineRpmOf(p);
        for (ShiftMode mode : kAutoModes) {
            INFO(std::string(id) << " mode " << static_cast<int>(mode));
            DrivingModel model(p);
            model.setShiftMode(mode);
            for (double t = 0.0; t < 30.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
            double top = model.rpm();
            // Gros freinage, reprise à fond, freinage : ce qui pousse le plus à rétrograder trop tôt.
            for (double t = 0.0; t < 3.0; t += kMs) {
                model.stepPedal(kMs, 0.0, 1.0);
                top = std::max(top, model.rpm());
            }
            for (double t = 0.0; t < 2.0; t += kMs) {
                model.stepPedal(kMs, 1.0, 0.0);
                top = std::max(top, model.rpm());
            }
            for (double t = 0.0; t < 4.0; t += kMs) {
                model.stepPedal(kMs, 0.0, 0.8);
                top = std::max(top, model.rpm());
            }
            CHECK(top <= redline * 1.03);
        }
    }
}

TEST_CASE("au freinage, le sport rétrograde tôt et monte le régime ; le normal attend") {
    const Powertrain p = preset("inline_six");
    const auto brakeFrom = [&p](ShiftMode mode, double& highest) {
        DrivingModel model(p);
        model.setShiftMode(mode);
        settleAt(model, 130.0);
        const int before = model.gear();
        int lowest = before;
        highest = 0.0;
        for (double t = 0.0; t < 2.5; t += kMs) {
            model.stepPedal(kMs, 0.0, 0.5);
            lowest = std::min(lowest, model.gear());
            highest = std::max(highest, model.rpm());
        }
        return before - lowest;
    };
    double sportRpm = 0.0, normalRpm = 0.0;
    const int sport = brakeFrom(ShiftMode::Sport, sportRpm);
    const int normal = brakeFrom(ShiftMode::Normal, normalRpm);
    CHECK(sport >= 2);
    CHECK(normal == 0);
    CHECK(sportRpm > normalRpm + 1000.0);
}

TEST_CASE("les modes sportifs tiennent leur rapport en virage, les autres non") {
    const Powertrain p = preset("inline_six");
    const auto gearAfter = [&p](ShiftMode mode, double radius) {
        DrivingModel model(p);
        model.setShiftMode(mode);
        model.setCornerRadius(radius);
        for (double t = 0.0; t < 3.0; t += kMs) model.stepPedal(kMs, 0.25, 0.0);
        const int before = model.gear();
        // Une sortie de virage à pied léger : assez pour dépasser le point d'un mode normal, pas celui d'une pleine charge.
        for (double t = 0.0; t < 20.0; t += kMs) model.stepPedal(kMs, 0.25, 0.0);
        return model.gear() - before;
    };
    CHECK(gearAfter(ShiftMode::Sport, 60.0) == 0);
    CHECK(gearAfter(ShiftMode::Track, 60.0) == 0);
    CHECK(gearAfter(ShiftMode::Sport, 0.0) > 0);
    CHECK(gearAfter(ShiftMode::Normal, 60.0) > 0);
}

TEST_CASE("lever le pied ne fait pas monter en sport ni en piste, mais l'éco en profite") {
    const Powertrain p = preset("inline_six");
    const auto afterLift = [&p](ShiftMode mode) {
        DrivingModel model(p);
        model.setShiftMode(mode);
        for (double t = 0.0; t < 40.0 && model.speedKmh() < 150.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        while (model.shifting()) model.stepPedal(kMs, 1.0, 0.0);
        const int before = model.gear();
        // Plus longtemps que la mémoire du pied, et le moteur reste haut : seul le pied levé retient la montée.
        for (double t = 0.0; t < 12.0; t += kMs) model.stepPedal(kMs, 0.0, 0.0);
        return model.gear() - before;
    };
    CHECK(afterLift(ShiftMode::Sport) <= 0);
    CHECK(afterLift(ShiftMode::Track) <= 0);
    CHECK(afterLift(ShiftMode::Eco) > 0);
}

// ── Les conséquences ──

TEST_CASE("le 0 à 100 en sport n'est jamais plus lent qu'en éco, sur tous les moteurs et toutes les boîtes") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (all[i].powertrain().electric) continue;
        for (GearboxKind kind : kBoxes) {
            INFO(std::string(all[i].id) << " boîte " << static_cast<int>(kind));
            const Powertrain p = withBox(all[i].id, kind);
            const double eco = zeroTo100(p, ShiftMode::Eco);
            const double sport = zeroTo100(p, ShiftMode::Sport);
            CHECK(sport <= eco + 0.05);
        }
    }
}

TEST_CASE("l'éco consomme moins que le sport sur le même trajet") {
    for (const char* id : {"city_14", "inline_six", "diesel_four"}) {
        for (DriveScenario scenario : {DriveScenario::City, DriveScenario::Country}) {
            INFO(std::string(id) << " trajet " << scenarioOf(scenario).id);
            const auto litres = [&](ShiftMode mode) {
                DrivingModel model(preset(id));
                model.setShiftMode(mode);
                model.setDriveScenario(scenario);
                for (double t = 0.0; t < scenarioOf(scenario).loopSec; t += 0.05) model.step(0.05);
                return model.consumption().fuelAvgLper100km();
            };
            const double eco = litres(ShiftMode::Eco);
            const double sport = litres(ShiftMode::Sport);
            CHECK(eco > 0.0);
            CHECK(eco < sport);
        }
    }
}

TEST_CASE("le convertisseur se ponte plus tôt en éco, jusqu'à pleine charge en piste") {
    const Powertrain p = withBox("inline_six", GearboxKind::Converter);
    const auto pontedShare = [&p](ShiftMode mode, double pedal) {
        DrivingModel model(p);
        model.setShiftMode(mode);
        double ponted = 0.0, total = 0.0;
        for (double t = 0.0; t < 30.0; t += kMs) {
            model.stepPedal(kMs, pedal, 0.0);
            if (model.gear() >= 2) {
                total += kMs;
                if (model.clutch() > 0.9) ponted += kMs;
            }
        }
        return ponted / std::max(total, 1.0);
    };
    // Le normal ouvre le pontage à partir de 85 % de pédale, l'éco à 90 %.
    CHECK(pontedShare(ShiftMode::Normal, 0.87) == 0.0);
    CHECK(pontedShare(ShiftMode::Eco, 0.87) > 0.5);
    CHECK(pontedShare(ShiftMode::Normal, 1.0) == 0.0);
    CHECK(pontedShare(ShiftMode::Track, 1.0) > 0.3);
}

TEST_CASE("le mode piste raccourcit les passages, sauf sur une séquentielle : c'est son barillet qui décide") {
    const auto shiftSeconds = [](GearboxKind kind, ShiftMode mode) {
        const Powertrain p = withBox("inline_six", kind);
        DrivingModel model(p);
        model.setShiftMode(mode);
        double held = 0.0;
        bool was = false;
        int seen = 0;
        for (double t = 0.0; t < 30.0 && seen < 2; t += kMs) {
            model.stepPedal(kMs, 1.0, 0.0);
            const bool now = model.shifting() && model.gear() >= 1 && model.pendingGear() >= 3;
            if (now) held += kMs;
            if (was && !now) ++seen;
            was = now;
        }
        return held;
    };
    for (GearboxKind kind : {GearboxKind::Synchro, GearboxKind::DualClutch, GearboxKind::Converter}) {
        INFO("boîte " << static_cast<int>(kind));
        CHECK(shiftSeconds(kind, ShiftMode::Track) < shiftSeconds(kind, ShiftMode::Normal));
    }
    CHECK(shiftSeconds(GearboxKind::Sequential, ShiftMode::Track) == doctest::Approx(shiftSeconds(GearboxKind::Sequential, ShiftMode::Normal)).epsilon(0.3));
}

// ── Les palettes ──

TEST_CASE("aux palettes, le conducteur décide, et l'automatique ne fait que protéger le moteur") {
    for (GearboxKind kind : kBoxes) {
        INFO("boîte " << static_cast<int>(kind));
        const Powertrain p = withBox("inline_six", kind);
        const double redline = redlineRpmOf(p);
        DrivingModel model(p);
        model.setShiftMode(ShiftMode::Paddles);
        // Pied léger : il n'en bouge pas, là où les autres modes seraient montés depuis longtemps.
        for (double t = 0.0; t < 3.0; t += kMs) model.stepPedal(kMs, 0.3, 0.0);
        CHECK(model.gear() == 1);
        model.requestShift(1);
        for (double t = 0.0; t < 2.0; t += kMs) model.stepPedal(kMs, 0.3, 0.0);
        CHECK(model.gear() == 2);
        // Au plancher, elle monte d'elle-même, au rupteur et pas avant.
        double highest = 0.0;
        int gear = model.gear();
        double rpmAtUp = 0.0;
        for (double t = 0.0; t < 30.0 && rpmAtUp == 0.0; t += kMs) {
            const double rpm = model.rpm();
            model.stepPedal(kMs, 1.0, 0.0);
            highest = std::max(highest, model.rpm());
            if (model.gear() > gear) rpmAtUp = rpm;
            gear = model.gear();
        }
        CHECK(rpmAtUp > redline * 0.9);
        CHECK(highest <= redline * 1.03);
        // Trop vite pour le rapport du dessous : refusé.
        for (double t = 0.0; t < 2.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        for (int i = 0; i < 6; ++i) {
            model.requestShift(-1);
            for (double t = 0.0; t < 0.6; t += kMs) {
                model.stepPedal(kMs, 1.0, 0.0);
                CHECK(model.rpm() <= redline * 1.03);
            }
        }
        // Au frein, elle descend d'elle-même bien avant l'arrêt, et le moteur ne cale jamais.
        int gearAt15 = 99;
        for (double t = 0.0; t < 15.0; t += kMs) {
            model.stepPedal(kMs, 0.0, 0.3);
            CHECK(!model.stalled());
            if (gearAt15 == 99 && model.speedKmh() < 15.0) gearAt15 = model.gear();
        }
        CHECK(gearAt15 <= 2);
        CHECK(model.gear() <= 1);
        // Ni point mort ni marche arrière aux palettes : le rapport reste, et la voiture continue d'avancer.
        DrivingModel crawling(p);
        crawling.setShiftMode(ShiftMode::Paddles);
        for (double t = 0.0; t < 1.0; t += kMs) crawling.stepPedal(kMs, 0.3, 0.0);
        REQUIRE(crawling.gear() == 1);
        crawling.requestShift(-1);
        for (double t = 0.0; t < 0.3; t += kMs) {
            crawling.stepPedal(kMs, 0.3, 0.0);
            CHECK(crawling.gear() == 1);
        }
    }
}

TEST_CASE("le pilote automatique ne se sert pas des palettes : il conduit en normal") {
    const Powertrain p = preset("v8_cross_plane");
    DrivingModel paddles(p), normal(p);
    paddles.setShiftMode(ShiftMode::Paddles);
    for (double t = 0.0; t < 60.0; t += 0.05) {
        paddles.step(0.05);
        normal.step(0.05);
        REQUIRE(paddles.rpm() == normal.rpm());
    }
}

// ── La boîte manuelle et l'électrique ──

TEST_CASE("le mode n'a aucun effet sur une boîte manuelle ni sur une voiture électrique") {
    const Powertrain p = preset("inline_six");
    DrivingModel manualA(p), manualB(p);
    manualA.setManual(true);
    manualB.setManual(true);
    manualB.setShiftMode(ShiftMode::Track);
    manualA.requestShift(1);
    manualB.requestShift(1);
    const Powertrain electric = preset("electric");
    DrivingModel electricA(electric), electricB(electric);
    electricB.setShiftMode(ShiftMode::Eco);
    for (double t = 0.0; t < 20.0; t += kMs) {
        manualA.stepPedal(kMs, 0.6, 0.0);
        manualB.stepPedal(kMs, 0.6, 0.0);
        electricA.stepPedal(kMs, 0.6, 0.0);
        electricB.stepPedal(kMs, 0.6, 0.0);
    }
    CHECK(manualA.rpm() == manualB.rpm());
    CHECK(manualA.speedKmh() == manualB.speedKmh());
    CHECK(electricA.rpm() == electricB.rpm());
}

// ── Les trajets ──

TEST_CASE("chaque trajet se tient : étapes dans l'ordre, dans la boucle, reprise sur une étape qui existe") {
    for (int i = 0; i < kDriveScenarios; ++i) {
        const Scenario& s = scenarioOf(static_cast<DriveScenario>(i));
        INFO(std::string(s.id));
        CHECK(s.legCount > 2);
        CHECK(s.legs[0].fromSec == 0.0);
        CHECK(s.loopBackSec < s.loopSec);
        for (int leg = 1; leg < s.legCount; ++leg) {
            CHECK(s.legs[leg].fromSec > s.legs[leg - 1].fromSec);
            CHECK(s.legs[leg].fromSec < s.loopSec);
        }
    }
}

TEST_CASE("la boucle d'origine n'a pas bougé") {
    const Scenario& s = scenarioOf(DriveScenario::City);
    REQUIRE(s.legCount == 8);
    CHECK(s.loopSec == DrivingModel::kLoopSec);
    const double from[] = {0.0, 5.0, 20.0, 26.0, 36.0, 62.0, 76.0, 92.0};
    const double target[] = {0.0, 50.0, 50.0, 0.0, 112.0, 90.0, 128.0, 0.0};
    const double eager[] = {0.0, 0.55, 0.4, 0.0, 1.0, 0.35, 1.0, 0.0};
    for (int leg = 0; leg < 8; ++leg) {
        CHECK(s.legs[leg].fromSec == from[leg]);
        CHECK(s.legs[leg].targetKmh == target[leg]);
        CHECK(s.legs[leg].eagerness == eager[leg]);
        CHECK(s.legs[leg].gradePct == 0.0);
        CHECK(s.legs[leg].cornerRadiusM == 0.0);
    }
    CHECK(s.style.anticipateDecel == 0.0);
}

TEST_CASE("sans rien choisir, on a le mode normal et la boucle d'origine, au bit près") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        INFO(std::string(all[i].id));
        const Powertrain p = all[i].powertrain();
        DrivingModel untouched(p), chosen(p);
        chosen.setShiftMode(ShiftMode::Normal);
        chosen.setDriveScenario(DriveScenario::City);
        for (double t = 0.0; t < 40.0; t += 0.05) {
            untouched.step(0.05);
            chosen.step(0.05);
            REQUIRE(untouched.rpm() == chosen.rpm());
            REQUIRE(untouched.speedKmh() == chosen.speedKmh());
            REQUIRE(untouched.torqueNm() == chosen.torqueNm());
        }
    }
}

TEST_CASE("chaque trajet va au bout sur chaque préréglage, sans erreur ni calage") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        for (int s = 0; s < kDriveScenarios; ++s) {
            const DriveScenario scenario = static_cast<DriveScenario>(s);
            for (ShiftMode mode : kAutoModes) {
                INFO(std::string(all[i].id) << " " << scenarioOf(scenario).id << " mode " << static_cast<int>(mode));
                const Lap lap = playScenario(p, scenario, mode);
                CHECK(lap.finite);
                CHECK(!lap.stalled);
                CHECK(!lap.broken);
                // Le rupteur reprend le moteur, et une descente peut le pousser un instant au-delà.
                CHECK(lap.maxRpm <= redlineRpmOf(p) * 1.03);
                CHECK(lap.maxSpeed > 15.0);
            }
        }
    }
}

TEST_CASE("chaque trajet atteint ses vitesses, ou dit franchement que la voiture n'en a pas la force") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        for (int si = 0; si < kDriveScenarios; ++si) {
            const DriveScenario scenario = static_cast<DriveScenario>(si);
            const Scenario& s = scenarioOf(scenario);
            const Lap lap = playScenario(p, scenario);
            for (int leg = 0; leg < s.legCount; ++leg) {
                const ScenarioLeg& l = s.legs[leg];
                const double target = legTargetKmh(s, l);
                const double end = (leg + 1 < s.legCount ? s.legs[leg + 1].fromSec : s.loopSec) - l.fromSec;
                // Seules comptent les étapes assez longues pour y arriver, et qui ne sont pas « à fond ».
                if (target >= kFlatOutKmh || target <= 0.0 || end < 15.0) continue;
                INFO(std::string(all[i].id) << " " << std::string(s.id) << " étape " << leg << " : " << target << " km/h, atteint " << lap.legTop[static_cast<std::size_t>(leg)]);
                const bool reached = lap.legTop[static_cast<std::size_t>(leg)] >= target * 0.85;
                // Encore en train d'accélérer quand l'étape se termine : le temps a manqué, pas la force.
                const bool stillRising = lap.legRise[static_cast<std::size_t>(leg)] > 0.5;
                CHECK((reached || stillRising || lap.underpoweredSec > 0.0 || l.eagerness < 0.7));
            }
        }
    }
    // Une voiture lourde et faible le dit ; un gros moteur n'a jamais à le dire.
    Powertrain weak = preset("city_14");
    weak.car.massKg = 1500;
    CHECK(playScenario(weak, DriveScenario::Towing).underpoweredSec > 1.0);
    CHECK(playScenario(preset("v12"), DriveScenario::Towing).underpoweredSec == 0.0);
}

TEST_CASE("les scénarios conduisent vraiment différemment") {
    const Powertrain p = preset("inline_six");
    const Lap jam = playScenario(p, DriveScenario::StopAndGo);
    const Lap motorway = playScenario(p, DriveScenario::Motorway);
    const Lap track = playScenario(p, DriveScenario::Track);
    CHECK(jam.maxSpeed < 50.0);
    CHECK(motorway.maxSpeed > 140.0);
    CHECK(track.maxSpeed > motorway.maxSpeed);
    CHECK(jam.maxRpm < track.maxRpm * 0.75);
}

TEST_CASE("le conducteur anticipe : il arrive au virage à la bonne vitesse, sans attendre d'y être") {
    const Powertrain p = preset("inline_six");
    DrivingModel model(p);
    model.setDriveScenario(DriveScenario::Track);
    const Scenario& s = scenarioOf(DriveScenario::Track);
    int corner = 0;
    while (s.legs[corner].cornerRadiusM <= 0.0) ++corner;
    const double target = legTargetKmh(s, s.legs[corner]);
    bool checked = false;
    for (double t = 0.0; t < s.legs[corner].fromSec + 1.0; t += 0.05) {
        model.step(0.05);
        if (!checked && model.legIndex() == corner) {
            checked = true;
            CHECK(model.speedKmh() < target * 1.25);
        }
    }
    REQUIRE(checked);
}

TEST_CASE("un virage serré limite la vitesse selon son rayon : la voiture le sent, ce n'est pas un décor") {
    const Scenario& s = scenarioOf(DriveScenario::Mountain);
    int hairpin = 0;
    while (s.legs[hairpin].kind != LegKind::Hairpin) ++hairpin;
    const double target = legTargetKmh(s, s.legs[hairpin]);
    CHECK(target < s.legs[hairpin].targetKmh * 0.6);
    // v = sqrt(a × R) : quatre fois le rayon, deux fois la vitesse.
    ScenarioLeg wide = s.legs[hairpin];
    wide.cornerRadiusM *= 4.0;
    CHECK(legTargetKmh(s, wide) == doctest::Approx(std::min(wide.targetKmh, target * 2.0)));

    // Et le virage freine : à même pédale, on va moins vite.
    const auto cruise = [](double radius) {
        DrivingModel model(preset("city_14"));
        model.setCornerRadius(radius);
        for (double t = 0.0; t < 40.0; t += kMs) model.stepPedal(kMs, 0.3, 0.0);
        return model.speedKmh();
    };
    CHECK(cruise(40.0) < cruise(0.0) - 3.0);
}

TEST_CASE("changer de trajet en roulant ne fait pas sauter la voiture") {
    const Powertrain p = preset("v6_gt");
    DrivingModel model(p);
    for (double t = 0.0; t < 50.0; t += 0.05) model.step(0.05);
    double before = model.speedKmh();
    REQUIRE(before > 40.0);
    model.setDriveScenario(DriveScenario::Mountain);
    // Elle garde sa vitesse, prend une étape qui roule, et la pente arrive progressivement.
    for (double t = 0.0; t < 3.0; t += 0.05) {
        model.step(0.05);
        CHECK(std::fabs(model.speedKmh() - before) < 4.0);
        CHECK(std::fabs(model.roadGradePct()) <= (t + 0.05) * 2.0 + 0.1);
        CHECK(model.legKind() != LegKind::Stopped);
        before = model.speedKmh();
    }
    CHECK(model.speedKmh() > 20.0);
}

TEST_CASE("un trajet reprend à la fin ; celui qui tourne en rond ne s'arrête qu'une fois") {
    const Scenario& s = scenarioOf(DriveScenario::Track);
    REQUIRE(s.loopBackSec > 0.0);
    CHECK(scenarioCycle(s, 1.0) == doctest::Approx(1.0));
    CHECK(scenarioCycle(s, s.loopSec + 1.0) == doctest::Approx(s.loopBackSec + 1.0));
    CHECK(scenarioCycle(s, s.loopSec * 3.0) >= s.loopBackSec);

    DrivingModel model(preset("v6_gt"));
    model.setDriveScenario(DriveScenario::Track);
    for (double t = 0.0; t < s.loopSec + 5.0; t += 0.05) model.step(0.05);
    CHECK(model.legKind() != LegKind::Stopped);
    CHECK(model.speedKmh() > 30.0);
}

// ── La pente ──

TEST_CASE("une pente pèse ce que dit la physique : m g sin(θ)") {
    const Powertrain p = preset("v6_gt");
    const auto coastFrom = [&p](double percent) {
        DrivingModel model(p);
        // Lancée en haut d'un rapport, puis débrayée : plus rien d'autre que la route.
        model.setManual(true);
        model.requestShift(1);
        for (double t = 0.0; t < 2.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        for (int gear = 2; gear <= 3; ++gear) {
            model.requestShift(1);
            for (double t = 0.0; t < 4.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        }
        model.setRoadGrade(percent);
        model.setClutchPedal(1.0);
        const double start = model.speedKmh() / 3.6;
        constexpr double kSeconds = 2.0;
        for (double t = 0.0; t < kSeconds; t += kMs) model.stepPedal(kMs, 0.0, 0.0);
        return (model.speedKmh() / 3.6 - start) / kSeconds;
    };
    const double flat = coastFrom(0.0);
    const double up = coastFrom(8.0);
    const double down = coastFrom(-8.0);
    const double expected = 9.81 * 0.08 / std::sqrt(1.0 + 0.08 * 0.08);
    // La roue libre garde un peu de traînée et d'embrayage : on compare à plat, à la même vitesse de départ.
    CHECK(flat - up == doctest::Approx(expected).epsilon(0.06));
    CHECK(down - flat == doctest::Approx(expected).epsilon(0.06));
}

TEST_CASE("en côte, il faut plus de charge pour la même vitesse, et la vitesse maximale baisse") {
    const auto holding = [](double percent) {
        DrivingModel model(preset("v6_gt"));
        model.setRoadGrade(percent);
        settleAt(model, 80.0);
        double load = 0.0, count = 0.0;
        for (double t = 0.0; t < 10.0; t += kMs) {
            model.stepPedal(kMs, holdPedal(model, 80.0), 0.0);
            load += model.torqueNm() * model.rpm();
            count += 1.0;
        }
        return load / count;
    };
    // La puissance demandée croît d'environ m g sin(θ) v, quel que soit le rapport que la boîte a choisi.
    const Powertrain p = preset("v6_gt");
    const double extra = (holding(6.0) - holding(0.0)) * 2.0 * 3.141592653589793 / 60.0;
    const double expected = static_cast<double>(p.car.massKg) * 9.81 * 0.06 / std::sqrt(1.0 + 0.06 * 0.06) * 80.0 / 3.6 / gearingOf(p).box.meshEfficiency;
    CHECK(extra == doctest::Approx(expected).epsilon(0.2));

    const auto top = [](double percent) {
        DrivingModel model(preset("city_14"));
        model.setRoadGrade(percent);
        for (double t = 0.0; t < 120.0; t += 0.01) model.stepPedal(0.01, 1.0, 0.0);
        return model.speedKmh();
    };
    CHECK(top(10.0) < top(0.0) * 0.8);
}

TEST_CASE("à l'arrêt, freins serrés, la voiture tient dans la pente, et la remorque se sent") {
    DrivingModel held(preset("city_14"));
    // Les freins d'abord, ils mettent un instant à serrer.
    for (double t = 0.0; t < 1.0; t += kMs) held.stepPedal(kMs, 0.0, 0.3);
    held.setRoadGrade(-12.0);
    for (double t = 0.0; t < 3.0; t += kMs) {
        held.stepPedal(kMs, 0.0, 0.3);
        REQUIRE(held.speedKmh() == 0.0);
    }
    // Sans frein, elle part.
    for (double t = 0.0; t < 3.0; t += kMs) held.stepPedal(kMs, 0.0, 0.0);
    CHECK(held.speedKmh() > 5.0);

    const auto after = [](double trailer) {
        DrivingModel model(preset("v6_gt"));
        model.setTrailer(trailer);
        for (double t = 0.0; t < 8.0; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        return model.speedKmh();
    };
    CHECK(after(1500.0) < after(0.0) * 0.8);
}

TEST_CASE("on démarre en côte, sans caler ni reculer, même à 16 %") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        INFO(std::string(all[i].id));
        DrivingModel model(all[i].powertrain());
        model.setRoadGrade(16.0);
        double top = 0.0;
        for (double t = 0.0; t < 10.0; t += kMs) {
            model.stepPedal(kMs, 0.8, 0.0);
            top = std::max(top, model.speedKmh());
            CHECK(!model.stalled());
        }
        CHECK(top > 15.0);
    }
}

TEST_CASE("une côte charge l'arrière comme une accélération : la limite des pneus n'y change presque pas") {
    // À la limite de l'adhérence, gravité et inertie reviennent au même pour les pneus : on perd g sin(θ), et rien de plus.
    // Sans ce transfert de charge, une propulsion perdrait en plus l'appui de son train arrière.
    const auto launch = [](Drive drive, double percent) {
        Powertrain p = preset("v8_cross_plane");
        p.car.drive = drive;
        DrivingModel model(p);
        model.setManual(true);
        model.setRoadGrade(percent);
        model.requestShift(1);
        for (double t = 0.0; t < 1.2; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        const double from = model.speedKmh() / 3.6;
        constexpr double kSeconds = 0.5;
        for (double t = 0.0; t < kSeconds; t += kMs) model.stepPedal(kMs, 1.0, 0.0);
        REQUIRE(model.wheelSlip() > 3.0);
        return (model.speedKmh() / 3.6 - from) / kSeconds;
    };
    const double expected = 9.81 * 0.2 / std::sqrt(1.0 + 0.2 * 0.2);
    for (Drive drive : {Drive::Rear, Drive::Front}) {
        INFO("transmission " << static_cast<int>(drive));
        CHECK(launch(drive, 0.0) - launch(drive, 20.0) == doctest::Approx(expected).epsilon(0.12));
    }
}

// ── Déterminisme et temps réel ──

TEST_CASE("mêmes réglages, même trajet : au bit près") {
    const Powertrain p = preset("boxer_four_turbo");
    DrivingModel a(p), b(p);
    a.setShiftMode(ShiftMode::Sport);
    b.setShiftMode(ShiftMode::Sport);
    a.setDriveScenario(DriveScenario::Mountain);
    b.setDriveScenario(DriveScenario::Mountain);
    for (double t = 0.0; t < 60.0; t += 0.05) {
        a.step(0.05);
        b.step(0.05);
        REQUIRE(a.rpm() == b.rpm());
        REQUIRE(a.speedKmh() == b.speedKmh());
    }
}

TEST_CASE("les trajets et les modes n'allouent rien") {
    DrivingModel model(preset("inline_six"));
    model.setShiftMode(ShiftMode::Track);
    model.setDriveScenario(DriveScenario::Mountain);
    const long before = g_allocations.load();
    for (double t = 0.0; t < 60.0; t += 0.05) model.step(0.05);
    CHECK(g_allocations.load() == before);
}
