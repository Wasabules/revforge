#include <algorithm>
#include <cmath>
#include <string>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/thermal.hpp"

using namespace revforge;

namespace {

Powertrain parked(Powertrain p, StartTemp start, float ambientC) {
    p.engine.startTemp = start;
    p.engine.ambientC = ambientC;
    return p;
}

Powertrain presetById(const char* id) {
    const Preset* found = findPreset(id);
    REQUIRE(found != nullptr);
    return found->powertrain();
}

}  // namespace

TEST_CASE("départ : une demi-heure ou deux heures d'arrêt se calculent, entre le moteur chaud et la nuit") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t k = 0; k < count; ++k) {
        const Powertrain p = all[k].powertrain();
        if (p.electric) continue;
        for (const float ambient : {20.0f, -30.0f}) {
            CAPTURE(std::string(all[k].id));
            CAPTURE(ambient);
            const ThermalNetwork warm = DrivingModel(parked(p, StartTemp::Warm, ambient)).thermal();
            const ThermalNetwork half = DrivingModel(parked(p, StartTemp::HalfHour, ambient)).thermal();
            const ThermalNetwork two = DrivingModel(parked(p, StartTemp::TwoHours, ambient)).thermal();
            const ThermalNetwork night = DrivingModel(parked(p, StartTemp::Cold, ambient)).thermal();
            // Chaud et nuit n'ont pas changé : la référence, et l'air ambiant.
            CHECK(warm.coolantC() == kWarmCoolantC);
            CHECK(warm.oilC() == kWarmOilC);
            CHECK(night.coolantC() == static_cast<double>(ambient));
            CHECK(night.oilC() == static_cast<double>(ambient));
            // Entre les deux, et dans l'ordre du temps passé.
            CHECK(half.coolantC() < warm.coolantC());
            CHECK(two.coolantC() < half.coolantC() - 10.0);
            CHECK(two.coolantC() > night.coolantC() + 10.0);
            CHECK(half.oilC() < warm.oilC());
            CHECK(two.oilC() < half.oilC() - 10.0);
            CHECK(two.oilC() > night.oilC() + 10.0);
        }
    }
}

TEST_CASE("départ : l'arrêt est le refroidissement du réseau lui-même, comme le modèle de conduite moteur coupé") {
    // Le même moteur chaud, coupé 30 min par -30 °C : pas à pas dans le modèle de conduite (1 ms, réseau à 10 ms), ou d'un coup
    // par `soakThermal` (pas de 1 s), ou au départ « une demi-heure ». Rien n'est saisi : tout vient du même réseau.
    const Powertrain p = parked(presetById("city_14"), StartTemp::Warm, -30.0f);
    DrivingModel stepped(p);
    stepped.halt();
    for (int i = 0; i < 1800 * 10; ++i) stepped.stepOff(0.1);
    DrivingModel soaked(p);
    soaked.halt();
    soaked.soakThermal(1800.0);
    const ThermalNetwork half = DrivingModel(parked(presetById("city_14"), StartTemp::HalfHour, -30.0f)).thermal();
    CHECK(std::abs(stepped.thermal().coolantC() - soaked.thermal().coolantC()) < 0.01);
    CHECK(std::abs(stepped.thermal().oilC() - soaked.thermal().oilC()) < 0.01);
    CHECK(std::abs(stepped.thermal().metalC() - soaked.thermal().metalC()) < 0.01);
    CHECK(half.coolantC() == soaked.thermal().coolantC());
    CHECK(half.oilC() == soaked.thermal().oilC());
    // Le modèle relit ses températures : le ralenti visé et l'enrichissement suivent l'eau refroidie.
    CHECK(std::abs(soaked.coldIdleRpm() - stepped.coldIdleRpm()) < 0.05);
    // L'état au départ se relit sans rien changer au modèle : les jauges avant le premier démarrage.
    CHECK(stepped.startThermal().coolantC() == kWarmCoolantC);
    CHECK(DrivingModel(parked(presetById("city_14"), StartTemp::TwoHours, -30.0f)).startThermal().coolantC() ==
          DrivingModel(parked(presetById("city_14"), StartTemp::TwoHours, -30.0f)).thermal().coolantC());
}

TEST_CASE("départ : moteur coupé, l'eau immobile se réchauffe d'abord au métal, puis tout rejoint l'air en une nuit") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t k = 0; k < count; ++k) {
        const Powertrain p = all[k].powertrain();
        if (p.electric) continue;
        CAPTURE(std::string(all[k].id));
        ThermalNetwork net = DrivingModel(parked(p, StartTemp::Warm, 20.0f)).thermal();
        // La pompe arrêtée, la culasse plus chaude que l'eau la réchauffe : la « montée » que lisent les conducteurs à l'arrêt.
        net.soak(120.0, 20.0);
        CHECK(net.coolantC() > kWarmCoolantC + 1.0);
        // Deux heures : une bonne part de l'écart est partie, pas tout (l'essentiel des pertes, les deux premières heures).
        net.soak(7200.0 - 120.0, 20.0);
        CHECK(net.coolantC() - 20.0 < 0.65 * (kWarmCoolantC - 20.0));
        CHECK(net.coolantC() - 20.0 > 0.25 * (kWarmCoolantC - 20.0));
        // Douze heures, une nuit : à 2,5 K de l'air au plus (les essais refroidis de force demandent ±2 K).
        net.soak(10.0 * 3600.0, 20.0);
        CHECK(net.coolantC() - 20.0 < 2.5);
        CHECK(net.oilC() - 20.0 < 2.5);
    }
}

TEST_CASE("départ : posé au ralenti après deux heures d'arrêt, le moteur encore tiède part avec l'air d'un ralenti établi") {
    // Deux heures d'arrêt laissent l'eau sous 70 °C, la fin de la mise en température : comme après une nuit, le modèle posé part
    // avec l'air qui équilibre ses frottements. Parti d'un collecteur vide, comme moteur chaud, le régime creuse au premier
    // dixième (jusqu'à un tiers du ralenti sur les carburateurs par -30 °C).
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t k = 0; k < count; ++k) {
        const Powertrain base = all[k].powertrain();
        if (base.electric) continue;
        for (const float ambient : {20.0f, -30.0f}) {
            CAPTURE(std::string(all[k].id));
            CAPTURE(ambient);
            DrivingModel model(parked(base, StartTemp::TwoHours, ambient));
            REQUIRE(model.thermal().coolantC() < 70.0);
            const double idle = model.rpm();
            double low = idle;
            for (int i = 0; i < 1000; ++i) {
                model.stepNeutral(0.001, 0.0);
                low = std::min(low, model.rpm());
            }
            CHECK(low > 0.97 * idle);
            CHECK_FALSE(model.stalled());
        }
    }
}

TEST_CASE("départ : un modèle reconstruit pour la même voiture reprend les températures de l'ancien") {
    const Powertrain p = parked(presetById("city_14"), StartTemp::Cold, -10.0f);
    DrivingModel first(p);
    for (int i = 0; i < 120000; ++i) first.stepNeutral(0.001, 0.0);
    REQUIRE(first.thermal().coolantC() > -9.0);
    DrivingModel second(p);
    CHECK(second.thermal().coolantC() == -10.0);
    second.carryThermal(first.thermal());
    CHECK(second.thermal().coolantC() == first.thermal().coolantC());
    CHECK(second.thermal().oilC() == first.thermal().oilC());
    CHECK(second.coldIdleRpm() == doctest::Approx(first.coldIdleRpm()));
}
