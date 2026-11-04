#include "doctest.h"

#include <cmath>
#include <vector>

#include "revforge/atmosphere.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

Powertrain presetAt(const char* id, int altitudeM, float celsius = 20.0f) {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.altitudeM = altitudeM;
    p.engine.ambientC = celsius;
    return p;
}

/** SAE J1349 (2004), allumage commandé : CF = 1,18·(99/Pd)·√((T+273)/298) − 0,18. La puissance à (p, T), rapportée à 101,325 kPa et 20 °C. */
double j1349Share(double kpa, double celsius) {
    const auto cf = [](double p, double c) { return 1.18 * (99.0 / p) * std::sqrt((c + 273.0) / 298.0) - 0.18; };
    return cf(101.325, 20.0) / cf(kpa, celsius);
}

/** Le temps qu'il faut au turbo pour atteindre 90 % de la pression demandée, régime tenu, du pied léger au plancher. */
double turboRiseSeconds(const TurboSpec& spec, double rpm, double breathing) {
    TurboCharger turbo;
    turbo.setSpec(spec);
    TurboInput in;
    in.rpm = rpm;
    in.breathing = breathing;
    in.burning = true;
    in.fill = 0.2;
    for (int i = 0; i < 20000; ++i) turbo.step(0.001, in);
    in.fill = 1.0;
    for (int i = 0; i < 20000; ++i) {
        turbo.step(0.001, in);
        if (turbo.boost() >= 0.9) return (i + 1) * 0.001;
    }
    return 99.0;
}

float levelAt(const Powertrain& p) {
    EngineSound sound(44100);
    sound.setProfile(p.profile());
    sound.setVolume(0.7f);
    sound.setTargetRpm(3000.0f);
    std::vector<float> block(1024);
    for (int i = 0; i < 44100 * 3 / 1024; ++i) sound.render(block.data(), static_cast<int>(block.size()));
    return sound.levelDbA();
}

}  // namespace

TEST_CASE("dans l'air de référence, chaque grandeur vaut exactement l'ancienne constante") {
    const AmbientAir air = ambientAir(20.0, 0.0);
    CHECK(air.reference());
    CHECK(air.pressurePa == 101325.0);
    CHECK(air.temperatureK == 293.0);
    CHECK(air.pressureRatio == 1.0);
    CHECK(air.temperatureRatio == 1.0);
    CHECK(air.densityRatio == 1.0);
    CHECK(air.soundSpeedRatio == 1.0);
    CHECK(air.soundSpeedMs == 343.0);
    CHECK(air.densityKgM3 == 101325.0 / (287.0 * 293.0));
    // Le son reçoit exactement l'air par défaut : 101 325 / 1000 est bien le 101,325 écrit dans les formules.
    CHECK(airAcousticsOf(air) == AirAcoustics{});
    CHECK(referenceAirOf(EngineConfig{}));
    CHECK(airDensityRatioOf(EngineConfig{}) == 1.0);

    // Rien de ce qui dépend de l'air ne bouge : le turbo, le compresseur, la traînée, l'air piégé.
    const Powertrain turbo = findPreset("hot_hatch_turbo")->powertrain();
    const TurboSpec spec = turboSpecOf(turbo);
    CHECK(spec.ambientPa == 101325.0);
    CHECK(spec.ambientK == 293.0);
    CHECK(spec.pipe.ambientK == 293.0);
    const SuperchargerSpec charger = superchargerSpecOf(findPreset("v8_supercharged")->powertrain());
    CHECK(charger.ambientPa == 101325.0);
    CHECK(charger.ambientK == 293.0);
    for (double rpm : {1500.0, 4000.0, 6500.0}) {
        CHECK(ambientChargeShare(turbo, rpm, 0.7) == 1.0);
        CHECK(ambientChargeShare(findPreset("hot_hatch_na")->powertrain(), rpm, 0.0) == 1.0);
    }
    const Powertrain city = findPreset("city_14")->powertrain();
    const double v = 30.0;
    CHECK(roadLoadN(city, v) == 0.5 * 1.2 * bodySpec(city.car.body).dragArea * v * v + 0.0095 * static_cast<double>(city.car.massKg) * 9.81);
    CHECK(carburetorSpecOf(findPreset("carb_twin_cam")->config).airDensityShare == 1.0);
}

TEST_CASE("la pression suit l'atmosphère type de l'OACI") {
    // Atmosphère type internationale (ISA), Pa : 89 874,6 à 1000 m, 79 495,2 à 2000 m, 70 108,5 à 3000 m, 57 728,3 à 4500 m.
    const double table[][2] = {{1000, 89874.6}, {2000, 79495.2}, {3000, 70108.5}, {4500, 57728.3}};
    for (const auto& row : table) {
        INFO(row[0]);
        CHECK(isaPressurePa(row[0]) == doctest::Approx(row[1]).epsilon(2e-5));
    }
    CHECK(isaPressurePa(0.0) == 101325.0);
}

TEST_CASE("masse volumique et vitesse du son retrouvent les valeurs publiées") {
    // Air sec à 101,325 kPa : 1,2922 kg/m³ et 331,3 m/s à 0 °C, 1,2041 et 343,2 à 20 °C, 1,1455 et 351,9 à 35 °C.
    const double table[][3] = {{0.0, 1.2922, 331.3}, {20.0, 1.2041, 343.2}, {35.0, 1.1455, 351.9}, {-30.0, 1.4518, 312.6}};
    for (const auto& row : table) {
        INFO(row[0]);
        const AmbientAir air = ambientAir(row[0], 0.0);
        // Le kelvin du modèle est décalé de 0,15 K : 0,1 % au plus.
        CHECK(air.densityKgM3 == doctest::Approx(row[1]).epsilon(0.002));
        CHECK(air.soundSpeedMs == doctest::Approx(row[2]).epsilon(0.001));
    }
    // En altitude, à température égale, la masse volumique suit la pression, la vitesse du son ne bouge pas.
    const AmbientAir high = ambientAir(20.0, 3000.0);
    CHECK(high.densityRatio == doctest::Approx(70108.5 / 101325.0).epsilon(1e-4));
    CHECK(high.soundSpeedMs == 343.0);
}

TEST_CASE("le couple d'un atmosphérique suit la correction SAE J1349") {
    // Seule la pression change en altitude, seule la température au niveau de la mer : J1349 vaut ici à 1,5 % près.
    // Au-delà de ce qu'elle couvre (environ 90 à 105 kPa), on s'écarte un peu plus : les frottements pèsent plus que ses 15 %.
    const PowerSummary reference = powerSummary(findPreset("hot_hatch_na")->powertrain());
    for (int altitude : {1000, 2000, 3000}) {
        INFO(altitude);
        const PowerSummary here = powerSummary(presetAt("hot_hatch_na", altitude));
        const double expected = j1349Share(isaPressurePa(altitude) / 1000.0, 20.0);
        CHECK(here.peakTorqueNm / reference.peakTorqueNm == doctest::Approx(expected).epsilon(altitude == 3000 ? 0.025 : 0.015));
    }
    for (float celsius : {-30.0f, 0.0f, 35.0f, 50.0f}) {
        INFO(celsius);
        const PowerSummary here = powerSummary(presetAt("hot_hatch_na", 0, celsius));
        CHECK(here.peakTorqueNm / reference.peakTorqueNm == doctest::Approx(j1349Share(101.325, celsius)).epsilon(0.012));
    }
}

TEST_CASE("un turbo à soupape à ressort tient sa pression relative : plus de rapport en altitude, jusqu'à la survitesse") {
    const Powertrain sea = findPreset("hot_hatch_turbo")->powertrain();
    const TurboSpec low = turboSpecOf(sea);
    const TurboSpec high = turboSpecOf(presetAt("hot_hatch_turbo", 3000));
    // Le même turbo : seul l'air change.
    CHECK(high.wheelDiameterM == low.wheelDiameterM);
    CHECK(high.inertia == low.inertia);
    CHECK(high.ambientPa == doctest::Approx(isaPressurePa(3000.0)));
    const double rpm = 4000.0;
    const TurboSteady a = turboSteady(low, rpm, breathingOf(sea, rpm));
    const TurboSteady b = turboSteady(high, rpm, breathingOf(sea, rpm));
    CHECK(a.boost == doctest::Approx(1.0));
    CHECK(b.boost == doctest::Approx(1.0));
    // La pression relative est la même : la charge absolue perd ce que l'atmosphère a perdu.
    CHECK(b.chargePa - high.ambientPa == doctest::Approx(a.chargePa - low.ambientPa).epsilon(1e-3));
    // Le rapport de pression monte : il faut un arbre plus rapide, de 15 % environ.
    CHECK(b.chargePa / high.ambientPa > 1.2 * a.chargePa / low.ambientPa);
    CHECK(b.shaftRpm > 1.1 * a.shaftRpm);
    // Plus haut, en haut du compte-tours, la roue atteint sa vitesse limite : le calculateur ouvre, la pression demandée n'est plus tenue.
    const TurboSpec summit = turboSpecOf(presetAt("hot_hatch_turbo", 4500));
    const TurboSteady c = turboSteady(summit, 5500.0, breathingOf(sea, 5500.0));
    CHECK(c.boost < 0.9);
    CHECK(c.shaftRpm * 2.0 * 3.141592653589793 / 60.0 * 0.5 * summit.wheelDiameterM == doctest::Approx(summit.tipLimit).epsilon(0.01));
    // Le couple à pleine charge perd moins qu'un atmosphérique : la pression relative revient.
    const double turboShare = powerSummary(presetAt("hot_hatch_turbo", 3000)).peakTorqueNm / powerSummary(sea).peakTorqueNm;
    const double naturalShare = powerSummary(presetAt("hot_hatch_na", 3000)).peakTorqueNm / powerSummary(findPreset("hot_hatch_na")->powertrain()).peakTorqueNm;
    CHECK(turboShare > naturalShare + 0.1);
}

TEST_CASE("le retard du turbo s'allonge en altitude") {
    const Powertrain sea = findPreset("hot_hatch_turbo")->powertrain();
    const double breathing = breathingOf(sea, 2500.0);
    const double atSea = turboRiseSeconds(turboSpecOf(sea), 2500.0, breathing);
    const double at3000 = turboRiseSeconds(turboSpecOf(presetAt("hot_hatch_turbo", 3000)), 2500.0, breathing);
    MESSAGE("90 % de la pression à 2500 tr/min : " << atSea << " s au niveau de la mer, " << at3000 << " s à 3000 m");
    CHECK(atSea < 2.0);
    CHECK(at3000 > 1.4 * atSea);
}

TEST_CASE("la traînée suit la masse volumique, et la boîte reste celle de l'air de référence") {
    const Powertrain sea = findPreset("electric")->powertrain();
    const Powertrain high = presetAt("electric", 3000);
    const double v = 36.0;
    const double rolling = roadLoadN(sea, 0.11) - 0.5 * 1.2 * bodySpec(sea.car.body).dragArea * 0.11 * 0.11;
    CHECK((roadLoadN(high, v) - rolling) / (roadLoadN(sea, v) - rolling) == doctest::Approx(ambientAir(20.0, 3000.0).densityRatio).epsilon(1e-9));
    // Une voiture qu'on monte à 3000 m garde sa boîte, son pont et ses rapports.
    const Powertrain na = findPreset("hot_hatch_na")->powertrain();
    const Gearing a = gearingOf(na);
    const Gearing b = gearingOf(presetAt("hot_hatch_na", 3000));
    CHECK(a.gears == b.gears);
    for (int g = 0; g < a.gears; ++g) CHECK(a.overall[static_cast<std::size_t>(g)] == b.overall[static_cast<std::size_t>(g)]);
    // Par grand froid, l'air plus dense freine davantage.
    CHECK(roadLoadN(presetAt("electric", 0, -30.0f), v) > roadLoadN(sea, v));
}

TEST_CASE("en altitude, un carburateur s'enrichit comme 1/√σ, et le papillon pompe moins") {
    const Powertrain sea = findPreset("carb_twin_cam")->powertrain();
    const Powertrain high = presetAt("carb_twin_cam", 3000);
    const CarburetorSpec a = carburetorSpecOf(sea.engine);
    const CarburetorSpec b = carburetorSpecOf(high.engine);
    const double sigma = ambientAir(20.0, 3000.0).densityRatio;
    for (double share : {0.3, 1.0}) {
        INFO(share);
        CHECK(carburetorRichness(b, share, 4000.0) / carburetorRichness(a, share, 4000.0) == doctest::Approx(1.0 / std::sqrt(sigma)).epsilon(1e-9));
    }
    // À charge partielle, le papillon retient moins d'atmosphère : chaque kWh coûte moins de carburant.
    const Powertrain city = findPreset("city_14")->powertrain();
    CHECK(brakeSpecificFuel(presetAt("city_14", 2000), 2000.0, 30.0) < brakeSpecificFuel(city, 2000.0, 30.0));
}

TEST_CASE("lancer un moteur en altitude demande moins de couple : chaque compression freine moins le démarreur") {
    // La détente rend presque tout, la vitesse moyenne change peu : c'est le creux de chaque compression qui remonte.
    const auto slowest = [](const Powertrain& p) {
        DrivingModel model(p);
        model.halt();
        model.crank();
        double low = 1e9;
        for (int i = 0; i < 700; ++i) {
            model.stepNeutral(0.001, 0.0);
            if (i > 150 && model.cranking()) low = std::fmin(low, model.rpm());
        }
        return low;
    };
    const double sea = slowest(findPreset("diesel_four")->powertrain());
    const double high = slowest(presetAt("diesel_four", 3000));
    MESSAGE("creux du lancement : " << sea << " tr/min au niveau de la mer, " << high << " à 3000 m");
    CHECK(high > sea + 4.0);
}

TEST_CASE("en altitude, tout rayonne moins : le niveau baisse comme ρ") {
    const float sea = levelAt(findPreset("hot_hatch_na")->powertrain());
    const float high = levelAt(presetAt("hot_hatch_na", 3000));
    const double expected = 20.0 * std::log10(ambientAir(20.0, 3000.0).densityRatio);
    MESSAGE("niveau : " << sea << " dB(A) au niveau de la mer, " << high << " à 3000 m, attendu " << expected << " dB");
    CHECK(high - sea == doctest::Approx(expected).epsilon(0.15));
}
