#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "revforge/combustion.hpp"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/two_stroke.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kDt = 128.0 / 44100.0;

Powertrain presetPowertrain(const char* id) {
    const Preset* preset = findPreset(id);
    REQUIRE(preset != nullptr);
    return preset->powertrain();
}

/** En quatrième vers 1800 tr/min, puis pied au plancher pendant `seconds` : là où le cliquetis se montre. */
struct Pull {
    double maxRetard = 0.0;
    double meanShare = 0.0;
    double meanTorque = 0.0;
};

Pull pull(const Powertrain& p, double seconds = 3.0) {
    DrivingModel m(p);
    m.setManual(true);
    m.requestShift(1);
    for (int i = 0; i < static_cast<int>(12.0 / kDt); ++i) {
        const int now = m.gear();
        if (now >= 1 && now < 4 && !m.shifting() && m.rpm() > 2600.0) m.requestShift(1);
        const double error = (1800.0 - m.rpm()) / 1000.0;
        m.stepPedal(kDt, now < 4 ? 0.5 : std::clamp(0.2 + error, 0.02, 1.0), now == 4 && error < -0.1 ? 0.3 : 0.0);
    }
    Pull out;
    int count = 0;
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        m.stepPedal(kDt, 1.0, 0.0);
        out.maxRetard = std::max(out.maxRetard, m.knockRetardDeg());
        out.meanShare += m.knockShare();
        out.meanTorque += m.torqueNm();
        ++count;
    }
    out.meanShare /= count;
    out.meanTorque /= count;
    return out;
}

/** Le son seul, avec ce que le modèle dirait du cliquetis : le bloc, écouté sous le capot. */
test::Samples knockSound(const EngineProfile& p, const KnockDrive* knock, float seconds = 2.0f, int block = 128) {
    EngineSound sound(test::kRate);
    sound.setProfile(p);
    sound.setListener(Listener::EngineBay);
    sound.setTargetRpm(2500.0f);
    sound.setDrive(1.0f, 0.0f, false, false, 1.0f);
    if (knock != nullptr) sound.setKnock(*knock);
    test::Samples out(static_cast<std::size_t>(seconds * test::kRate));
    for (std::size_t at = 0; at < out.size(); at += static_cast<std::size_t>(block)) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), out.size() - at));
        sound.render(out.data() + at, n);
    }
    return test::slice(out, test::kRate / 2, out.size());
}

/** Énergie dans une bande, par une transformée de Fourier discrète sur des fenêtres courtes. */
double band(const test::Samples& x, double fromHz, double toHz) {
    constexpr std::size_t kWindow = 512;
    constexpr double kPi = 3.141592653589793;
    double energy = 0.0;
    int windows = 0;
    for (std::size_t at = 0; at + kWindow <= x.size() && windows < 60; at += kWindow, ++windows) {
        for (double hz = fromHz; hz < toHz; hz += static_cast<double>(test::kRate) / kWindow) {
            double re = 0.0, im = 0.0;
            for (std::size_t n = 0; n < kWindow; ++n) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) / kWindow);
                const double phase = 2.0 * kPi * hz * static_cast<double>(n) / test::kRate;
                re += x[at + n] * w * std::cos(phase);
                im += x[at + n] * w * std::sin(phase);
            }
            energy += re * re + im * im;
        }
    }
    return energy / std::max(windows, 1);
}

double dB(double ratio) { return 10.0 * std::log10(ratio); }

}  // namespace

TEST_CASE("les carburants : l'E85 et le méthanol sortent de leurs composants publiés, l'essence garde ses chiffres") {
    const FuelProperties& petrol = fuelProperties(FuelGrade::Ron95);
    const FuelSpec& counted = fuelSpecOf(Fuel::Petrol);
    // Le 95 est exactement l'essence de la consommation d'origine.
    CHECK(petrol.lowerHeatingJPerG == counted.lowerHeatingJPerG);
    CHECK(petrol.densityKgPerL == counted.densityKgPerL);
    CHECK(petrol.stoichiometricAfr == counted.stoichiometricAfr);
    const FuelProperties& e85 = fuelProperties(FuelGrade::E85);
    MESSAGE("E85 : " << e85.lowerHeatingJPerG / 1000.0 << " MJ/kg, " << e85.stoichiometricAfr << ":1, " << e85.densityKgPerL << " kg/L, " << e85.vaporizationJPerG << " kJ/kg");
    // Valeurs publiées pour un E85 d'été : 29 à 30 MJ/kg, 9,7 à 9,9 de pouvoir comburivore.
    CHECK(e85.lowerHeatingJPerG == doctest::Approx(29300.0).epsilon(0.02));
    CHECK(e85.stoichiometricAfr == doctest::Approx(9.8).epsilon(0.02));
    CHECK(e85.densityKgPerL == doctest::Approx(0.78).epsilon(0.01));
    const FuelProperties& methanol = fuelProperties(FuelGrade::Methanol);
    // Par kilogramme d'air, les alcools apportent un peu plus d'énergie : leur oxygène.
    const double e85Energy = energyPerAirJPerKg(e85) / energyPerAirJPerKg(petrol);
    const double methanolEnergy = energyPerAirJPerKg(methanol) / energyPerAirJPerKg(petrol);
    MESSAGE("énergie par kg d'air : E85 x" << e85Energy << ", méthanol x" << methanolEnergy);
    CHECK(e85Energy > 1.01);
    CHECK(e85Energy < 1.05);
    CHECK(methanolEnergy > e85Energy);
    CHECK(methanolEnergy < 1.08);
    // La vaporisation refroidit la charge, bien plus avec un alcool, et la vapeur du méthanol finit par saturer.
    const double petrolCool = evaporationCoolingK(petrol, 1.0, 1.0, 320.0, 101325.0);
    const double e85Cool = evaporationCoolingK(e85, 1.0, 1.0, 320.0, 101325.0);
    const double methanolCool = evaporationCoolingK(methanol, 1.0, 1.0, 320.0, 101325.0);
    MESSAGE("tout vaporisé dans l'air à 320 K : essence " << petrolCool << " K, E85 " << e85Cool << " K, méthanol " << methanolCool << " K");
    CHECK(petrolCool == doctest::Approx(350.0 / 14.7 / 1.005).epsilon(0.01));
    // Tout vaporisé dans l'air, un alcool sature : sa vapeur ne dépasse pas sa tension à la température atteinte.
    CHECK(e85Cool < e85.vaporizationJPerG / e85.stoichiometricAfr / 1.005);
    CHECK(methanolCool < methanol.vaporizationJPerG / methanol.stoichiometricAfr / 1.005);
    // La part qui se vaporise dans le conduit ne sature pas : l'E85 y refroidit trois fois plus que l'essence.
    const double portPetrol = evaporationCoolingK(petrol, 1.0, kPortEvaporatedShare, 320.0, 101325.0);
    const double portE85 = evaporationCoolingK(e85, 1.0, kPortEvaporatedShare, 320.0, 101325.0);
    MESSAGE("dans le conduit : essence " << portPetrol << " K, E85 " << portE85 << " K");
    CHECK(portPetrol == doctest::Approx(kPortEvaporatedShare * petrolCool).epsilon(1e-9));
    CHECK(portE85 > 3.0 * portPetrol);
    // Un diesel ignore le carburant choisi.
    EngineConfig diesel;
    diesel.fuel = Fuel::Diesel;
    diesel.fuelGrade = FuelGrade::E85;
    CHECK(&fuelPropertiesOf(diesel) == &dieselFuelProperties());
    CHECK(&fuelSpecOf(diesel) == &fuelSpecOf(Fuel::Diesel));
    CHECK(combustionStock(diesel));
}

TEST_CASE("au défaut, la combustion garde son chemin d'origine : aucune carte de cliquetis, aucun couple changé") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        const std::string id = all[i].id;
        CAPTURE(id);
        // Intégration : la Cadillac prend son taux publié (5,35, Old Cars Weekly) ; sa combustion n'est plus au défaut. Elle seule.
        if (p.engine.compressionRatio != 0.0f) {
            CHECK(id == "v16_cadillac");
            continue;
        }
        CHECK(combustionStock(p.engine));
        CHECK(knockMapOf(p) == nullptr);
        CHECK(p.profile().dieselKnockScale == 1.0f);
        if (p.electric) continue;
        // Un deux-temps comprime au démarreur son taux réel, celui que laisse la lumière d'échappement (deux-temps).
        if (p.engine.twoStroke()) CHECK(p.profile().starter.compressionRatio == static_cast<float>(twoStrokePortsOf(p.engine).trappedCompression));
        else CHECK(p.profile().starter.compressionRatio == stockCompressionRatio(p.engine));
        // Le même taux, écrit à la main : le calcul passe par la combustion, et retombe sur le même couple.
        Powertrain same = p;
        same.engine.compressionRatio = stockCompressionRatio(p.engine);
        for (double rpm : {1500.0, 3000.0, 5000.0}) CHECK(fullLoadTorqueNm(same, rpm, 0.0) == doctest::Approx(fullLoadTorqueNm(p, rpm, 0.0)).epsilon(1e-9));
    }
}

TEST_CASE("Douaud-Eyzat : le délai suit l'octane, la pression et la température") {
    // 17,68 (ON/100)^3,402 p^-1,7 exp(3800/T) ms : 40 atm et 800 K, à 95.
    const double tau = douaudEyzatDelayS(95.0, 40.0 * 101325.0, 800.0);
    CHECK(tau == doctest::Approx(17.68e-3 * std::pow(0.95, 3.402) * std::pow(40.0, -1.7) * std::exp(3800.0 / 800.0)).epsilon(1e-9));
    CHECK(douaudEyzatDelayS(102.0, 40.0 * 101325.0, 800.0) > tau);
    CHECK(douaudEyzatDelayS(95.0, 60.0 * 101325.0, 800.0) < tau);
    CHECK(douaudEyzatDelayS(95.0, 40.0 * 101325.0, 850.0) < tau);
}

TEST_CASE("le rendement suit le taux de compression : un optimum vers 17 sur un moteur carré, quelques pour cent de 10,5 à 12,5") {
    double best = 0.0, bestRatio = 0.0;
    for (double r = 8.0; r <= 24.0; r += 0.25) {
        const double eta = indicatedEfficiency(r, 1.0, false);
        if (eta > best) {
            best = eta;
            bestRatio = r;
        }
    }
    MESSAGE("optimum à " << bestRatio << ":1");
    CHECK(bestRatio == doctest::Approx(17.0).epsilon(0.03));
    const double gain = indicatedEfficiency(12.5, 1.0, false) / indicatedEfficiency(10.5, 1.0, false);
    MESSAGE("de 10,5 à 12,5 : x" << gain);
    CHECK(gain > 1.015);
    CHECK(gain < 1.04);
    // Le diesel est déjà près de son optimum : y monter de 18 à 22 n'apporte rien, en descendre à 16 coûte peu.
    Powertrain diesel = presetPowertrain("diesel_four");
    diesel.engine.compressionRatio = 16.0f;
    const double sixteen = compressionEfficiencyShare(diesel.engine);
    diesel.engine.compressionRatio = 22.0f;
    const double twentyTwo = compressionEfficiencyShare(diesel.engine);
    MESSAGE("diesel : 16:1 x" << sixteen << ", 22:1 x" << twentyTwo);
    CHECK(sixteen < 1.0);
    CHECK(sixteen > 0.97);
    CHECK(twentyTwo < 1.01);
}

TEST_CASE("aucun préréglage ne cliquette à ses réglages : marges listées en degrés d'avance") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Powertrain p = all[i].powertrain();
        if (p.electric || p.engine.burnsDiesel()) continue;
        const std::string id = all[i].id;
        CAPTURE(id);
        KnockMap map;
        map.build(p);
        REQUIRE(map.valid());
        double margin = 1e9, marginRpm = 0.0, retard = 0.0, retardRpm = 0.0;
        const double idle = idleRpmOf(p);
        const double redline = redlineRpmOf(p);
        for (double rpm = idle; rpm <= redline; rpm += 50.0) {
            const KnockPoint point = map.at(rpm, 1.0);
            // Même le cycle le plus rapide reste sous sa limite : pas un seul cliquetis.
            CHECK(knockingShare(point.stockDeg - point.meanLimitDeg) == 0.0);
            if (point.limitDeg - point.stockDeg < margin) {
                margin = point.limitDeg - point.stockDeg;
                marginRpm = rpm;
            }
            if (point.mbtDeg - point.stockDeg > retard) {
                retard = point.mbtDeg - point.stockDeg;
                retardRpm = rpm;
            }
            CHECK(map.steadyTorqueShare(rpm) == doctest::Approx(1.0).epsilon(1e-12));
        }
        MESSAGE(id << " : marge " << margin << "° à " << marginRpm << " tr/min ; la carte retarde au plus de " << retard << "° face au meilleur couple, à " << retardRpm << " tr/min");
        CHECK(margin >= kCalibrationMarginDeg - 1e-9);
    }
}

TEST_CASE("plus de compression ou un air plus chaud rapprochent la limite, plus d'octane l'éloigne") {
    const Powertrain city = presetPowertrain("city_14");
    const auto limitAt = [](Powertrain p, double rpm) {
        KnockMap map;
        map.build(p);
        return map.at(rpm, 1.0).limitDeg;
    };
    const double stock = limitAt(city, 1500.0);
    Powertrain higher = city;
    higher.engine.compressionRatio = 11.5f;
    Powertrain premium = city;
    premium.engine.fuelGrade = FuelGrade::Ron98;
    Powertrain race = city;
    race.engine.fuelGrade = FuelGrade::Race102;
    Powertrain ethanol = city;
    ethanol.engine.fuelGrade = FuelGrade::E85;
    const double perRatio = limitAt(higher, 1500.0) - stock;
    const double perOctane = (limitAt(premium, 1500.0) - stock) / 3.0;
    KnockMap::Inputs hot;
    hot.ambientK = 303.15;
    KnockMap warm;
    warm.build(city, hot);
    const double perTenKelvin = warm.at(1500.0, 1.0).limitDeg - stock;
    MESSAGE("à 1500 tr/min pleine charge : " << perRatio << "° par point de compression, " << perOctane << "° par point d'octane, " << perTenKelvin << "° pour 10 °C d'air en plus");
    CHECK(perRatio < -2.0);
    CHECK(perRatio > -7.0);
    CHECK(perOctane > 0.3);
    CHECK(perOctane < 2.0);
    CHECK(perTenKelvin < 0.0);
    CHECK(perTenKelvin > -2.5);
    CHECK(limitAt(race, 1500.0) > limitAt(premium, 1500.0));
    CHECK(limitAt(ethanol, 1500.0) > limitAt(race, 1500.0));
}

TEST_CASE("le capteur retire l'avance et le couple qui va avec ; un carburateur, sans capteur, cliquette") {
    Powertrain injected = presetPowertrain("city_14");
    injected.engine.compressionRatio = 13.0f;
    const Pull sensed = pull(injected);
    MESSAGE("1.4 à 13:1, plein gaz en quatrième depuis 1800 tr/min : retard " << sensed.maxRetard << "°, cycles qui cliquettent " << sensed.meanShare * 100.0 << " %");
    CHECK(sensed.maxRetard > 3.0);
    CHECK(sensed.meanShare < 0.15);

    Powertrain carb = presetPowertrain("carb_twin_cam");
    carb.engine.compressionRatio = 12.0f;
    const Pull unsensed = pull(carb);
    MESSAGE("carburateurs à 12:1 : retard " << unsensed.maxRetard << "°, cycles qui cliquettent " << unsensed.meanShare * 100.0 << " %");
    CHECK(unsensed.maxRetard == 0.0);
    CHECK(unsensed.meanShare > 0.3);

    // Le même moteur d'origine ne cliquette jamais, et son modèle ne calcule rien.
    const Pull stock = pull(presetPowertrain("carb_twin_cam"));
    CHECK(stock.meanShare == 0.0);
    CHECK(stock.maxRetard == 0.0);
}

TEST_CASE("Draper face aux mesures : un alésage de 126 mm résonne entre 4,3 et 5,4 kHz, un de 86 mm vers 6,6 kHz") {
    // Les bandes mesurées sur un moteur au gaz de 126 mm d'alésage (Physics, 2021, vu cité) : 4,3-5,4, 6,4-7,7, 8,9-9,6, 10,3-11,4 et
    // 12,2-13,1 kHz. Les modes de Draper, au gaz brûlé que donne le modèle : 1,841, 3,054, 3,832, 4,201 et 5,318.
    const double gasK = 2580.0;
    const double first = draperHz(0.126, gasK);
    MESSAGE("126 mm : " << first << " Hz, 86 mm : " << draperHz(0.086, gasK) << " Hz");
    CHECK(first > 4300.0);
    CHECK(first < 5400.0);
    const double modes[] = {3.054, 3.832, 4.201, 5.318};
    const double low[] = {6400.0, 8900.0, 10300.0, 12200.0};
    const double high[] = {7700.0, 9600.0, 11400.0, 13100.0};
    for (int i = 0; i < 4; ++i) {
        const double hz = first * modes[i] / 1.841;
        CHECK(hz > low[i] - 100.0);
        CHECK(hz < high[i] + 100.0);
    }
    // La règle des préparateurs, 1800/(π·alésage en mm) kHz, donne 6,66 kHz pour 86 mm.
    CHECK(draperHz(0.086, gasK) == doctest::Approx(1800.0 / (3.141592653589793 * 86.0) * 1000.0).epsilon(0.03));
}

TEST_CASE("le son du cliquetis : à la fréquence de Draper, seulement quand il cliquette, sans allouer ni dépendre des blocs") {
    const EngineProfile p = presetPowertrain("four_cyl_sport").profile();
    const test::Samples plain = knockSound(p, nullptr);
    KnockDrive quiet;
    quiet.active = true;
    quiet.excessDeg = -10.0f;
    quiet.gasK = 2580.0f;
    // Sous la limite de trois écarts types : pas un tirage ne cliquette, et le reste du son n'a pas bougé d'un bit.
    CHECK(knockSound(p, &quiet) == plain);
    KnockDrive inactive = quiet;
    inactive.active = false;
    inactive.excessDeg = 5.0f;
    CHECK(knockSound(p, &inactive) == plain);

    KnockDrive knocking = quiet;
    knocking.excessDeg = 3.0f;
    const test::Samples heard = knockSound(p, &knocking);
    const double draper = draperHz(0.086, 2580.0);
    const double near = band(heard, draper - 600.0, draper + 300.0) / band(plain, draper - 600.0, draper + 300.0);
    const double low = band(heard, 500.0, 2000.0) / band(plain, 500.0, 2000.0);
    MESSAGE("sous le capot, autour de " << draper << " Hz : +" << dB(near) << " dB ; 500-2000 Hz : " << dB(low) << " dB");
    // Le niveau réel monte : la correspondance vers la sortie resserre un peu tout le reste (`toneGain`), d'où quelques dixièmes en bas.
    CHECK(dB(near) - dB(low) > 6.0);
    CHECK(std::fabs(dB(low)) < 2.0);
    // Au-dessus du premier mode et sous le deuxième, la résonance ne met presque rien.
    const double between = band(heard, draper * 1.25, draper * 1.45) / band(plain, draper * 1.25, draper * 1.45);
    CHECK(dB(between) < dB(near) - 3.0);

    CHECK(knockSound(p, &knocking, 1.0f, 128) == knockSound(p, &knocking, 1.0f, 517));
    EngineSound sound(test::kRate);
    sound.setProfile(p);
    sound.setTargetRpm(3000.0f);
    sound.setDrive(1.0f, 0.0f, false, false, 1.0f);
    sound.setKnock(knocking);
    std::vector<float> block(512);
    sound.render(block.data(), 512);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) sound.render(block.data(), 512);
    CHECK(g_allocations.load() == before);
    CHECK(sound.knocks() > 0);
}

TEST_CASE("le diesel : le taux change son rendement, son démarreur et son claquement, pas son carburant") {
    const Powertrain stock = presetPowertrain("diesel_four");
    Powertrain low = stock;
    low.engine.compressionRatio = 15.0f;
    CHECK(!combustionStock(low.engine));
    CHECK(knockMapOf(low) == nullptr);
    CHECK(low.profile().starter.compressionRatio == 15.0f);
    CHECK(low.profile().dieselKnockScale > 1.05f);
    Powertrain high = stock;
    high.engine.compressionRatio = 22.0f;
    CHECK(high.profile().dieselKnockScale < 0.97f);
    // Sous 14:1, un diesel ne s'allume plus : le réglage s'arrête là.
    Powertrain tooLow = stock;
    tooLow.engine.compressionRatio = 8.0f;
    CHECK(compressionRatioOf(tooLow.engine) == 14.0f);
    // Le démarreur lance plus vite un moteur qui comprime moins.
    const auto crankRpm = [](const Powertrain& p) {
        DrivingModel m(p);
        m.halt();
        m.crank();
        // La vitesse moyenne une fois lancé : les compressions la freinent, les détentes la relancent presque autant.
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < 2000 && m.cranking(); ++i) {
            m.stepNeutral(0.001, 0.0);
            if (i >= 200) {
                sum += m.rpm();
                ++count;
            }
        }
        return sum / std::max(count, 1);
    };
    const double easy = crankRpm(low);
    const double usual = crankRpm(stock);
    MESSAGE("lancement du diesel : " << easy << " tr/min à 15:1, " << usual << " à 18:1");
    CHECK(easy > usual);
    // Au ralenti, sous le capot : un délai plus long, plus de gazole brûlé d'un coup, une combustion plus bruyante.
    const auto idling = [](const EngineProfile& p) {
        EngineSound sound(test::kRate);
        sound.setProfile(p);
        sound.setListener(Listener::EngineBay);
        sound.setTargetRpm(p.idleRpm);
        sound.setDrive(0.1f, 0.0f, false, false, 0.0f);
        test::Samples out(static_cast<std::size_t>(3 * test::kRate));
        sound.render(out.data(), static_cast<int>(out.size()));
        return test::slice(out, test::kRate, out.size());
    };
    const double louder = dB(band(idling(low.profile()), 500.0, 4000.0) / band(idling(stock.profile()), 500.0, 4000.0));
    MESSAGE("ralenti du diesel sous le capot, 500-4000 Hz, à 15:1 : +" << louder << " dB");
    CHECK(louder > 0.4);
    CHECK(louder < 3.0);
}

TEST_CASE("le rotatif : son taux est borné par son rotor, son rendement suit sa chambre, et il peut cliqueter") {
    Powertrain rotary = presetPowertrain("twin_rotor_13");
    rotary.engine.compressionRatio = 20.0f;
    CHECK(compressionRatioOf(rotary.engine) == doctest::Approx(11.9f));
    rotary.engine.compressionRatio = 10.5f;
    CHECK(compressionEfficiencyShare(rotary.engine) > 1.0);
    CHECK(rotary.profile().starter.compressionRatio == 10.5f);
    const KnockMap* map = knockMapOf(rotary);
    REQUIRE(map != nullptr);
    CHECK(map->sensor());
    // Plus de compression, plus de cliquetis : la limite descend sous celle d'origine.
    KnockMap stock;
    stock.build(presetPowertrain("twin_rotor_13"));
    CHECK(map->at(2000.0, 1.0).limitDeg < stock.at(2000.0, 1.0).limitDeg);
}

TEST_CASE("la consommation en litres suit le carburant, et un taux plus haut dépense moins") {
    const auto litres = [](const Powertrain& p) {
        DrivingModel m(p);
        for (int i = 0; i < static_cast<int>(120.0 / kDt); ++i) m.step(kDt);
        return m.consumption().fuelTripL();
    };
    const Powertrain city = presetPowertrain("city_14");
    const double petrol = litres(city);
    Powertrain e85 = city;
    e85.engine.fuelGrade = FuelGrade::E85;
    Powertrain methanol = city;
    methanol.engine.fuelGrade = FuelGrade::Methanol;
    Powertrain higher = city;
    higher.engine.compressionRatio = 12.5f;
    const double ethanolL = litres(e85);
    const double methanolL = litres(methanol);
    const double higherL = litres(higher);
    MESSAGE("deux minutes en ville : essence " << petrol << " L, E85 " << ethanolL << " L (x" << ethanolL / petrol << "), méthanol " << methanolL << " L (x" << methanolL / petrol << "), 12,5:1 " << higherL << " L");
    // L'E85 : de l'ordre de 40 % de litres en plus, le méthanol plus du double (rapport des énergies par litre).
    CHECK(ethanolL / petrol > 1.3);
    CHECK(ethanolL / petrol < 1.5);
    CHECK(methanolL / petrol > 1.9);
    CHECK(methanolL / petrol < 2.3);
    CHECK(higherL < petrol);
    CHECK(higherL > 0.95 * petrol);
}
