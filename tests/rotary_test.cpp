#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "doctest.h"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

constexpr double kPi = 3.141592653589793;

EngineConfig rotary(int rotors = 2, int cc = 1308, int redline = 9000) {
    EngineConfig e;
    e.cylinders = rotors;
    e.layout = Layout::Rotary;
    e.displacementCc = cc;
    e.redlineRpm = redline;
    return e;
}

/** Le moteur à pistons qui avale autant d'air : deux fois la cylindrée, à course assez courte pour ne pas en souffrir. */
EngineConfig sameAir(const EngineConfig& r) {
    EngineConfig e;
    e.cylinders = 2 * r.cylinders;
    e.layout = Layout::Inline;
    e.displacementCc = 2 * r.displacementCc;
    e.boreMm = 110;
    e.redlineRpm = r.redlineRpm;
    e.exhaust = r.exhaust;
    e.aspiration = r.aspiration;
    return e;
}

Powertrain car(const EngineConfig& e) {
    Powertrain p;
    p.engine = e;
    p.car.body = Body::Coupe;
    p.car.massKg = 1300.0f;
    p.car.rimInch = 18;
    p.car.drive = Drive::Rear;
    return p;
}

/** Puissance moyenne autour de chaque multiple de 1/6 d'ordre, à régime tenu. */
struct Orders {
    std::vector<double> power;
    double at(int sixths) const { return power[static_cast<std::size_t>(sixths)]; }
};

Orders measureOrders(const EngineProfile& profile, float rpm, int topSixths) {
    constexpr int kRate = 44100;
    EngineSound sound(kRate);
    sound.setProfile(profile);
    sound.setVolume(0.7f);
    sound.setTargetRpm(rpm);
    sound.setDrive(0.7f, 0.0f, false, false, 0.7f);
    std::vector<float> pcm(static_cast<std::size_t>(kRate) * 4);
    sound.render(pcm.data(), static_cast<int>(pcm.size()));
    // Sur de courtes fenêtres : le moteur dérive, et une longue étalerait les raies aiguës.
    constexpr int kWindow = kRate / 2;
    Orders out;
    out.power.assign(static_cast<std::size_t>(topSixths) + 1, 0.0);
    const double shaft = static_cast<double>(rpm) / 60.0;
    for (int start = kRate; start + kWindow <= static_cast<int>(pcm.size()); start += kWindow / 2) {
        for (int s = 1; s <= topSixths; ++s) {
            // Le régime dérive de 1 à 2 % : on prend le plus fort de cinq fréquences autour du rang.
            double best = 0.0;
            for (int offset = -2; offset <= 2; ++offset) {
                double re = 0.0, im = 0.0;
                const double omega = 2.0 * kPi * shaft * s / 6.0 * (1.0 + 0.006 * offset) / kRate;
                for (int n = 0; n < kWindow; n += 2) {
                    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * n / kWindow);
                    re += w * static_cast<double>(pcm[static_cast<std::size_t>(start + n)]) * std::cos(omega * n);
                    im += w * static_cast<double>(pcm[static_cast<std::size_t>(start + n)]) * std::sin(omega * n);
                }
                best = std::max(best, re * re + im * im);
            }
            out.power[static_cast<std::size_t>(s)] += best;
        }
    }
    return out;
}

double db(double power) { return 10.0 * std::log10(power + 1e-20); }

}  // namespace

TEST_CASE("le rotatif : R 105, e 15, b 80 redonnent 654 cm3 par chambre") {
    const double volume = 3.0 * std::sqrt(3.0) * 105.0 * 15.0 * 80.0 / 1000.0;
    CHECK(volume == doctest::Approx(654.7).epsilon(0.001));
    const RotaryGeometry g = rotaryGeometry(volume);
    CHECK(g.radiusMm == doctest::Approx(105.0).epsilon(1e-6));
    CHECK(g.eccentricityMm == doctest::Approx(15.0).epsilon(1e-6));
    CHECK(g.widthMm == doctest::Approx(80.0).epsilon(1e-6));
    // Le double du volume : les longueurs suivent en racine cubique.
    CHECK(rotaryGeometry(2.0 * volume).radiusMm == doctest::Approx(105.0 * std::cbrt(2.0)).epsilon(1e-6));
}

TEST_CASE("le rotatif : une explosion par rotor et par tour d'arbre") {
    for (int rotors : {2, 3, 4}) {
        const EngineProfile p = rotary(rotors, 654 * rotors).toProfile();
        CHECK(p.rotary);
        CHECK(p.rotors == rotors);
        // Sur le cycle de deux tours : deux explosions par rotor, à 360°/rotors d'arbre l'une de l'autre.
        REQUIRE(p.firingCount == 2 * rotors);
        REQUIRE(p.firingCount <= kMaxCylinders);
        for (int i = 0; i < p.firingCount; ++i) {
            const float shaftDeg = p.firing[static_cast<std::size_t>(i)] * 720.0f;
            CHECK(shaftDeg == doctest::Approx(i * 360.0f / rotors).epsilon(1e-5));
        }
        // Le volume équivalent de chaque explosion est bien celui d'une chambre : le débit sur deux tours tombe juste.
        const double swept = 0.7854 * p.boreM * p.boreM * p.strokeM * p.cylinders * 1e6;
        CHECK(swept == doctest::Approx(2.0 * 654.0 * rotors).epsilon(0.002));
    }
    // Un réglage hors de portée ne déborde pas des tableaux.
    CHECK(rotary(9, 5000).toProfile().firingCount <= kMaxCylinders);
}

TEST_CASE("le rotatif : le couple suit le cycle d'un tour, pas un coefficient") {
    const Powertrain r = car(rotary());
    const Powertrain same = car(sameAir(rotary()));
    const double share = rotaryEfficiencyShare(Aspiration::Natural);
    // 2π au lieu de 4π : le couple d'une même pression est celui d'un quatre temps de deux fois la cylindrée.
    for (double rpm : {3000.0, 5500.0, 8000.0}) {
        CHECK(fullLoadTorqueNm(r, rpm, 0.0) == doctest::Approx(fullLoadTorqueNm(same, rpm, 0.0) * share).epsilon(0.02));
        // Les frottements en suivent la même loi, à la part sans distribution près.
        CHECK(frictionTorqueNm(r, rpm) == doctest::Approx(0.75 * frictionTorqueNm(same, rpm)).epsilon(1e-9));
    }
    // Moins bon à volume égal : la chambre est allongée, et le taux plus bas. Ce n'est pas réglé, cela découle de la forme.
    CHECK(share < 0.9);
    CHECK(share > 0.7);
    CHECK(rotaryWallRatio(9.4f) > 1.5);
    // Le rapport de forme ne dépend pas de la cylindrée choisie : une chambre allongée reste allongée.
    const double peak = powerSummary(r).peakTorqueNm;
    CHECK(peak > 180.0);
    CHECK(peak < 260.0);
}

TEST_CASE("le rotatif : débit d'air et débit d'huile suivent le rotor, le carburant son rendement") {
    const Powertrain r = car(rotary());
    const Powertrain same = car(sameAir(rotary()));
    ConsumptionModel a, b;
    a.configure(r);
    b.configure(same);
    const double rpm = 4000.0;
    // La même charge d'air par tour que le quatre temps de deux fois sa cylindrée.
    const double full = fullLoadTorqueNm(r, rpm, 0.0) + frictionTorqueNm(r, rpm);
    const double fullSame = fullLoadTorqueNm(same, rpm, 0.0) + frictionTorqueNm(same, rpm);
    CHECK(a.airGps(rpm, 0.5, full) == doctest::Approx(b.airGps(rpm, 0.5, fullSame)).epsilon(0.05));
    // Même couple au frein : plus de carburant.
    const double fuelRotary = brakeSpecificFuel(r, rpm, 80.0);
    const double fuelPiston = brakeSpecificFuel(same, rpm, 80.0);
    CHECK(fuelRotary > fuelPiston * 1.05);
    // L'huile dosée : bien plus qu'un moteur à pistons de même souffle, et une fois encore à un facteur trois près.
    CHECK(a.oilGps(rpm, 80.0) > 2.0 * b.oilGps(rpm, 80.0));
    const double gramsPerHour = a.oilGps(2000.0, 0.0) * 3600.0;
    CHECK(gramsPerHour > 7.0);
    CHECK(gramsPerHour < 60.0);
}

TEST_CASE("le rotatif ne brûle que de l'essence") {
    EngineConfig petrol = rotary();
    EngineConfig diesel = rotary();
    diesel.fuel = Fuel::Diesel;
    CHECK_FALSE(diesel.burnsDiesel());
    CHECK(diesel.toProfile() == petrol.toProfile());
    const Powertrain a = car(petrol), b = car(diesel);
    CHECK(fullLoadTorqueNm(a, 5000.0, 0.0) == fullLoadTorqueNm(b, 5000.0, 0.0));
    CHECK(frictionTorqueNm(a, 5000.0) == frictionTorqueNm(b, 5000.0));
    CHECK(brakeSpecificFuel(a, 3000.0, 80.0) == brakeSpecificFuel(b, 3000.0, 80.0));
    // Sur un moteur à pistons, le diesel reste un diesel.
    EngineConfig piston;
    piston.fuel = Fuel::Diesel;
    CHECK(piston.burnsDiesel());
}

TEST_CASE("le rotatif : le préréglage roule, démarre, et atteint son régime") {
    const Preset* preset = findPreset("twin_rotor_13");
    REQUIRE(preset != nullptr);
    const Powertrain p = preset->powertrain();
    CHECK(isRotary(p.engine));
    CHECK(p.engine.cylinders == 2);
    const Performance perf = measurePerformance(p);
    CHECK(perf.zeroTo100S > 4.0);
    CHECK(perf.zeroTo100S < 11.0);
    CHECK(perf.topSpeedKmh > 180.0);

    // Le démarreur vainc la compression, malgré deux fois moins de cylindrée affichée.
    DrivingModel model(p);
    model.crank();
    double peak = 0.0;
    for (int i = 0; i < 500; ++i) {
        model.stepPedal(0.01, 0.0, 0.0);
        peak = std::max(peak, model.rpm());
    }
    CHECK(peak > 300.0);
    CHECK(model.rpm() == doctest::Approx(p.profile().idleRpm).epsilon(0.25));
    CHECK_FALSE(model.stalled());

    // Pied au plancher au point mort : jusqu'au rupteur, sans jamais le dépasser de beaucoup.
    DrivingModel free(p);
    double top = 0.0;
    for (int i = 0; i < 400; ++i) {
        free.stepPedal(0.01, 1.0, 0.0);
        top = std::max(top, free.rpm());
    }
    CHECK(top > 8000.0);
}

TEST_CASE("le rotatif : la charge de compression au démarrage est moindre que le quatre temps de même souffle") {
    // Le régime moyen pendant le lancement : les fuites aux sommets le rendent plus facile, malgré autant de volume comprimé.
    auto cranking = [](const Powertrain& p) {
        DrivingModel m(p);
        m.crank();
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < 80; ++i) {
            m.stepPedal(0.01, 0.0, 0.0);
            if (i > 35 && m.cranking()) {
                sum += m.rpm();
                ++count;
            }
        }
        REQUIRE(count > 5);
        return sum / count;
    };
    const double r = cranking(car(rotary()));
    const double same = cranking(car(sameAir(rotary())));
    MESSAGE("lancement : rotatif ", r, " tr/min, quatre temps ", same, " tr/min");
    CHECK(r > same);
    CHECK(r > 150.0);
}

TEST_CASE("le rotatif : ses rangs ne sont ni ceux d'un 2 cylindres, ni ceux d'un 4 cylindres") {
    constexpr float kRpm = 4000.0f;
    constexpr int kTop = 8 * 6;
    EngineConfig fourConfig;
    fourConfig.cylinders = 4;
    fourConfig.displacementCc = 2000;
    fourConfig.redlineRpm = 9000;
    fourConfig.exhaust = Exhaust::Sport;
    EngineConfig rotaryConfig = rotary();
    rotaryConfig.exhaust = Exhaust::Sport;
    const Orders r = measureOrders(rotaryConfig.toProfile(), kRpm, kTop);
    const Orders four = measureOrders(fourConfig.toProfile(), kRpm, kTop);

    const auto family = [](const Orders& o, int denominator) {
        // Les ordres k/denominator irréductibles, de 2 à 8 : là où la ligne passe tout. Rapportés au rang de la cadence.
        double sum = 0.0;
        int count = 0;
        const int step = 6 / denominator;
        for (int s = 2 * 6; s <= kTop; s += step) {
            if (s % 6 == 0) continue;
            if (denominator == 3 && s % 3 == 0) continue;
            sum += db(o.at(s));
            ++count;
        }
        return sum / count - db(o.at(2 * 6));
    };
    // La période d'un rotatif est de trois tours par chambre, celle d'un quatre temps de deux tours : tiers d'ordre d'un côté, demi-ordres de l'autre.
    const double rotaryThirds = family(r, 3);
    const double rotaryHalves = family(r, 2);
    const double fourThirds = family(four, 3);
    const double fourHalves = family(four, 2);
    MESSAGE("rotatif : tiers ", rotaryThirds, " demis ", rotaryHalves, " ; quatre cylindres : tiers ", fourThirds, " demis ", fourHalves);
    CHECK(rotaryThirds - rotaryHalves > 3.0);
    CHECK(fourHalves - fourThirds > 3.0);
    // La cadence reste 2 par tour : c'est le même rang pair qui domine sur les deux, pas le premier d'un 2 cylindres.
    CHECK(db(r.at(2 * 6)) > db(r.at(1 * 6)));
}

TEST_CASE("le rotatif : le son ne dépend pas de la taille des blocs, et n'alloue pas") {
    const EngineProfile profile = rotary().toProfile();
    auto render = [&profile](int block) {
        EngineSound sound(44100);
        sound.setProfile(profile);
        sound.setTargetRpm(3000.0f);
        std::vector<float> pcm(44100);
        for (int at = 0; at < 44100; at += block) sound.render(pcm.data() + at, std::min(block, 44100 - at));
        return pcm;
    };
    CHECK(render(64) == render(4096));
}
