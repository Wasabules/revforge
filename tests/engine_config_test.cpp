#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "exhaust_line.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

namespace {

double brightness(const EngineConfig& c) {
    EngineSound core(kRate);
    core.setProfile(c.toProfile());
    core.setVolume(0.7f);
    core.setTargetRpm(3000.0f);
    Samples x(kRate * 2);
    core.render(x.data(), static_cast<int>(x.size()));
    const Samples played = slice(x, kRate / 2, x.size());
    return slopeRms(played) / rms(played);
}

double powerAt(const Samples& x, double hz) {
    constexpr double kPi = 3.141592653589793;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double a = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(x.size()));
        re += x[i] * w * std::cos(a);
        im += x[i] * w * std::sin(a);
    }
    return re * re + im * im;
}

/** Ce qu'il y a au-dessus de 2 kHz, par rapport au dessous, à 3000 tr/min et à cette charge. */
double airiness(const EngineConfig& c, float load) {
    EngineSound core(kRate);
    core.setProfile(c.toProfile());
    core.setVolume(0.7f);
    // Sur le côté : pile dans l'axe, un gros tube concentre ses aigus vers l'auditeur, et ce n'est pas ce qu'on mesure ici.
    core.setListener(Listener::Side);
    core.setTargetRpm(3000.0f);
    core.setDrive(load, 0.0f, false, false);
    Samples x(kRate * 2);
    core.render(x.data(), static_cast<int>(x.size()));
    const Samples played = slice(x, kRate, kRate + 16384);
    const auto bandPower = [&played](double from, double to) {
        double sum = 0.0;
        for (double hz = from; hz < to; hz += 50.0) sum += powerAt(played, hz);
        return sum;
    };
    return bandPower(2000.0, 8000.0) / bandPower(100.0, 2000.0);
}

/** Monte, tient, puis lève le pied d'un coup. Renvoie (pied sur l'accélérateur, lever de pied). */
std::pair<Samples, Samples> blip(const EngineConfig& c, float volume = 0.7f, Listener listener = Listener::Outside) {
    const EngineProfile p = c.toProfile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(volume);
    core.setListener(listener);
    auto play = [&core](float seconds, const std::function<float(float)>& rpm) {
        Samples out(static_cast<std::size_t>(seconds * kRate));
        std::size_t done = 0;
        while (done < out.size()) {
            core.setTargetRpm(rpm(static_cast<float>(done) / static_cast<float>(out.size())));
            const int n = static_cast<int>(std::min<std::size_t>(2205, out.size() - done));
            core.render(out.data() + done, n);
            done += n;
        }
        return out;
    };
    play(1.0f, [&p](float) { return p.idleRpm; });
    const float top = p.idleRpm + (p.redlineRpm - p.idleRpm) * 0.85f;
    Samples up = play(1.5f, [&](float t) { return p.idleRpm + (top - p.idleRpm) * t; });
    Samples lift = play(1.5f, [&](float t) { return p.idleRpm + (top - p.idleRpm) * (1.0f - t) * (1.0f - t); });
    return {std::move(up), std::move(lift)};
}

/** Autour de la plus forte détonation : (crête, brillance) sur 40 ms. */
std::pair<float, double> loudestPop(const EngineConfig& c, float volume = 0.7f) {
    const Samples lift = blip(c, volume).second;
    // Pas le premier quart de seconde : le niveau suit maintenant la physique, et le plein gaz n'y est pas encore retombé.
    const auto top = std::max_element(lift.begin() + kRate / 4, lift.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    const std::size_t at = static_cast<std::size_t>(top - lift.begin());
    const std::size_t from = at > 882 ? at - 882 : 0;
    const Samples window = slice(lift, from, std::min(from + 1764, lift.size()));
    return {peak(window), slopeRms(window) / rms(window)};
}

}  // namespace

TEST_CASE("la course découle de la cylindrée et de l'alésage") {
    EngineConfig c;
    c.displacementCc = 2000;
    c.boreMm = 84;
    CHECK(c.strokeMm() == doctest::Approx(90.2f).epsilon(0.002));
}

TEST_CASE("de gros cylindres sonnent plus grave") {
    EngineConfig big, small;
    big.displacementCc = 6000;
    small.displacementCc = 1000;
    CHECK(big.toProfile().strokeM > small.toProfile().strokeM);
    // À toute charge : un cylindre plus gros se vide plus lentement, et sa ligne garde moins d'aigus.
    for (float load : {0.1f, 0.3f, 0.6f, 1.0f}) {
        INFO(load);
        CHECK(airiness(big, load) < airiness(small, load) * 0.85);
    }
}

TEST_CASE("un échappement libre est plus brillant") {
    EngineConfig stock, straight;
    straight.exhaust = Exhaust::Straight;
    CHECK(brightness(straight) > brightness(stock));
}

TEST_CASE("toutes les combinaisons jouent proprement") {
    for (int n : {2, 3, 4, 5, 6, 8, 10, 12}) {
        for (Layout layout : {Layout::Inline, Layout::V, Layout::CrossPlane, Layout::Boxer}) {
            for (Exhaust exhaust : {Exhaust::Stock, Exhaust::Sport, Exhaust::Straight}) {
                for (Aspiration aspiration : {Aspiration::Natural, Aspiration::Turbo}) {
                    for (Fuel fuel : {Fuel::Petrol, Fuel::Diesel}) {
                        EngineConfig c;
                        c.cylinders = n;
                        c.layout = layout;
                        c.displacementCc = 600 + n * 400;
                        c.boreMm = 60 + n * 4;
                        c.redlineRpm = 6000;
                        c.exhaust = exhaust;
                        c.aspiration = aspiration;
                        c.fuel = fuel;
                        const EngineProfile p = c.toProfile();
                        for (int i = 1; i < n; ++i) REQUIRE(p.firing[i] > p.firing[i - 1]);
                        REQUIRE(p.firing[n - 1] < 1.0f);
                        EngineSound core(kRate);
                        core.setProfile(p);
                        core.setVolume(1.0f);
                        Samples out(8192);
                        for (int step = 0; step < 3; ++step) {
                            core.setTargetRpm(1500.0f + static_cast<float>(step) * 2000.0f);
                            core.render(out.data(), 8192);
                        }
                        REQUIRE(std::none_of(out.begin(), out.end(), [](float s) { return std::isnan(s); }));
                        REQUIRE(peak(out) < 0.99f);
                        REQUIRE(rms(out) > 0.01);
                    }
                }
            }
        }
    }
}

TEST_CASE("le turbo ne siffle que sous charge") {
    auto whistle = [](bool load) {
        EngineConfig c;
        c.aspiration = Aspiration::Turbo;
        EngineSound core(kRate);
        core.setProfile(c.toProfile());
        core.setVolume(0.7f);
        core.setTargetRpm(2000.0f);
        Samples idle(kRate);
        core.render(idle.data(), kRate);
        Samples out(kRate * 2);
        if (load) {
            for (int done = 0; done < static_cast<int>(out.size()); done += 441) {
                core.setTargetRpm(2000.0f + static_cast<float>(done) * 0.06f);
                core.render(out.data() + done, std::min(441, static_cast<int>(out.size()) - done));
            }
        } else {
            core.render(out.data(), static_cast<int>(out.size()));
        }
        return slopeRms(out);
    };
    CHECK(whistle(true) > whistle(false));
}

TEST_CASE("le multiplicateur automatique couvre la plage du moteur") {
    const EngineProfile diesel = findPreset("diesel_four")->profile();
    CHECK(diesel.fromCarRpm(850.0f) == doctest::Approx(diesel.idleRpm));
    CHECK(diesel.fromCarRpm(6200.0f) >= diesel.redlineRpm * 0.93f);
    CHECK(diesel.fromCarRpm(6200.0f) <= diesel.redlineRpm);
    CHECK(diesel.fromCarRpm(0.0f) == 0.0f);
}

TEST_CASE("le multiplicateur amène 4000 tr/min à 9000") {
    EngineConfig c;
    c.redlineRpm = 12000;
    c.revMultiplier = 2.6f;
    CHECK(std::fabs(c.toProfile().fromCarRpm(4000.0f) - 9000.0f) < 200.0f);
}

TEST_CASE("la courbe de régime n'a aucune cassure") {
    for (float multiplier : {0.5f, 1.0f, 2.6f, 4.0f}) {
        EngineConfig c;
        c.redlineRpm = 9000;
        c.revMultiplier = multiplier;
        const EngineProfile p = c.toProfile();
        float previous = p.fromCarRpm(850.0f);
        float previousSlope = 0.0f;
        for (int car = 855; car <= 7000; car += 5) {
            const float now = p.fromCarRpm(static_cast<float>(car));
            const float slope = now - previous;
            REQUIRE(slope >= 0.0f);
            REQUIRE(now <= 9000.0f);
            if (car > 855) REQUIRE(std::fabs(slope - previousSlope) < 0.05f + multiplier * 5.0f * 0.02f);
            previous = now;
            previousSlope = slope;
        }
    }
}

TEST_CASE("un régime par marches reste fluide avec un fort multiplicateur") {
    EngineConfig c;
    c.redlineRpm = 12000;
    c.revMultiplier = 3.0f;
    const EngineProfile p = c.toProfile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    Samples out(kRate * 4);
    const int tick = kRate / 25;
    for (int done = 0; done < static_cast<int>(out.size()); done += tick) {
        const float car = 1000.0f + 3000.0f * static_cast<float>(done) / static_cast<float>(out.size());
        core.setTargetRpm(p.fromCarRpm(static_cast<float>(static_cast<int>(car / 8.0f)) * 8.0f));
        core.render(out.data() + done, std::min(tick, static_cast<int>(out.size()) - done));
    }
    std::vector<float> jumps;
    for (std::size_t i = 1; i < out.size(); ++i) jumps.push_back(std::fabs(out[i] - out[i - 1]));
    std::sort(jumps.begin(), jumps.end());
    CHECK(jumps.back() < jumps[static_cast<std::size_t>(static_cast<double>(jumps.size()) * 0.99)] * 6.0f);
}

TEST_CASE("les détonations n'arrivent qu'au lever de pied") {
    EngineConfig quiet;
    quiet.exhaust = Exhaust::Straight;
    EngineConfig loud = quiet;
    loud.pops = PopLevel::Strong;
    const auto [upLoud, liftLoud] = blip(loud);
    const auto [upQuiet, liftQuiet] = blip(quiet);
    // Une fois le moteur redescendu, seules les détonations dépassent.
    const auto after = [](const Samples& x) { return slice(x, x.size() / 4, x.size() * 3 / 4); };
    CHECK(peak(after(liftLoud)) > peak(after(liftQuiet)) * 1.5f);
    CHECK(rms(upLoud) == doctest::Approx(rms(upQuiet)).epsilon(0.05));
}

// Le défaut « caillou » : un claquement sec, sans corps, donc presque tout dans les aigus.
TEST_CASE("une détonation a du corps, ce n'est pas un claquement") {
    for (Exhaust exhaust : {Exhaust::Stock, Exhaust::Sport, Exhaust::Straight}) {
        EngineConfig c;
        c.exhaust = exhaust;
        c.pops = PopLevel::Strong;
        CHECK(loudestPop(c).second < 0.35);
    }
}

TEST_CASE("un échappement libre détone plus fort") {
    EngineConfig stock;
    stock.pops = PopLevel::Strong;
    EngineConfig straight = stock;
    straight.exhaust = Exhaust::Straight;
    // Volume bas : à 0,7, les deux crêtes butent sur le limiteur, et on ne compare plus rien.
    const float loud = loudestPop(straight, 0.1f).first;
    const float tame = loudestPop(stock, 0.1f).first;
    CHECK(tame < 0.8f);
    CHECK(loud > tame);
}

TEST_CASE("l'échappement et le moteur façonnent la détonation") {
    EngineConfig big, small, turbo, diesel;
    big.cylinders = 8;
    big.layout = Layout::V;
    big.displacementCc = 6000;
    small.cylinders = 3;
    small.displacementCc = 1000;
    turbo.aspiration = Aspiration::Turbo;
    diesel.fuel = Fuel::Diesel;
    diesel.pops = PopLevel::Strong;
    CHECK(big.toProfile().popBodyHz < small.toProfile().popBodyHz);
    CHECK(big.toProfile().popForce > small.toProfile().popForce);
    CHECK(turbo.toProfile().popForce < EngineConfig{}.toProfile().popForce);
    CHECK(diesel.toProfile().pops == PopLevel::Off);
}

TEST_CASE("pas de détonation au ralenti") {
    auto idle = [](PopLevel pops) {
        EngineConfig c;
        c.exhaust = Exhaust::Straight;
        c.pops = pops;
        const EngineProfile p = c.toProfile();
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setTargetRpm(p.idleRpm);
        Samples out(kRate * 4);
        core.render(out.data(), static_cast<int>(out.size()));
        return slice(out, kRate, out.size());
    };
    CHECK(idle(PopLevel::Strong) == idle(PopLevel::Off));
}

TEST_CASE("le turbo décharge sa pression au lever de pied") {
    EngineConfig atmo;
    atmo.exhaust = Exhaust::Sport;
    EngineConfig dump = atmo;
    dump.aspiration = Aspiration::Turbo;
    EngineConfig flutter = dump;
    flutter.turboRelease = TurboRelease::Flutter;
    // La soupape décharge sous le capot : on l'entend depuis l'avant, pas derrière la caisse et la ligne.
    const auto hiss = [](const EngineConfig& c) {
        const Samples lift = blip(c, 0.7f, Listener::Front).second;
        return slopeRms(slice(lift, 0, lift.size() / 2));
    };
    CHECK(hiss(dump) > hiss(atmo) * 1.2);
    CHECK(hiss(flutter) > hiss(atmo) * 1.1);
}

TEST_CASE("les événements n'écrêtent jamais") {
    EngineConfig popping;
    popping.exhaust = Exhaust::Straight;
    popping.pops = PopLevel::Strong;
    EngineConfig v8 = popping;
    v8.cylinders = 8;
    v8.layout = Layout::CrossPlane;
    v8.displacementCc = 5000;
    v8.boreMm = 96;
    v8.redlineRpm = 6000;
    v8.aspiration = Aspiration::Turbo;
    EngineConfig flutter = popping;
    flutter.exhaust = Exhaust::Stock;
    flutter.aspiration = Aspiration::Turbo;
    flutter.turboRelease = TurboRelease::Flutter;
    for (const EngineConfig& c : {popping, v8, flutter}) {
        const auto [up, lift] = blip(c);
        CHECK(peak(up) < 0.99f);
        CHECK(peak(lift) < 0.99f);
    }
}

TEST_CASE("même configuration, pas de fondu inutile") {
    const EngineProfile a = EngineConfig{}.toProfile();
    const EngineProfile b = EngineConfig{}.toProfile();
    EngineConfig changed;
    changed.boreMm = 70;
    CHECK(a == b);
    CHECK_FALSE(a == changed.toProfile());
}

TEST_CASE("un silencieux garni retient les aigus, une ligne directe les laisse passer") {
    // Le niveau sort maintenant de la physique : on compare ce que chaque ligne retient, plus un timbre égalisé.
    ExhaustLine line(kRate);
    const auto heldBack = [&line](Exhaust exhaust) {
        line.compute(exhaust, 1.0f, 0.002);
        return line.lossDb(2000.0, 6000.0);
    };
    MESSAGE("retenu entre 2 et 6 kHz : origine " << heldBack(Exhaust::Stock) << " dB, sport " << heldBack(Exhaust::Sport) << " dB, directe " << heldBack(Exhaust::Straight) << " dB");
    CHECK(heldBack(Exhaust::Straight) < heldBack(Exhaust::Sport) - 6.0);
    CHECK(heldBack(Exhaust::Sport) < heldBack(Exhaust::Stock) - 6.0);
}

// Le calcul de la ligne face à la formule analytique d'une chambre d'expansion.
TEST_CASE("la matrice d'une chambre retrouve la perte par transmission analytique") {
    constexpr double kPiLocal = 3.141592653589793;
    constexpr double kLength = 0.4;
    constexpr double kRatio = 8.0;
    for (double hz : {100.0, 300.0, 625.0, 900.0, 1250.0, 1700.0}) {
        INFO(hz);
        const double k = 2.0 * kPiLocal * hz / 500.0;
        const double m = kRatio - 1.0 / kRatio;
        const double sine = std::sin(k * kLength);
        const double expected = 10.0 * std::log10(1.0 + 0.25 * m * m * sine * sine);
        CHECK(ExhaustLine::chamberLossDb(hz, kLength, kRatio) == doctest::Approx(expected).epsilon(0.001));
    }
    // Aucune perte quand la chambre fait une demi-longueur d'onde : le creux caractéristique.
    CHECK(std::fabs(ExhaustLine::chamberLossDb(625.0, kLength, kRatio)) < 0.01);
}

// Le défaut « canette » : une ligne calculée en ondes planes forme un peigne de bosses régulières et fixes dans les
// médiums, qui reviennent tous les 600 Hz environ. De vrais moteurs ont un relief de 1,3 à 3,5 dB, sans cette régularité.
TEST_CASE("la ligne ne forme pas de peigne régulier dans les médiums") {
    constexpr double kPiLocal = 3.141592653589793;
    constexpr double kStepHz = 20.0;
    constexpr int kFirstHz = 800;
    ExhaustLine line(kRate);
    for (const Exhaust exhaust : {Exhaust::Stock, Exhaust::Sport, Exhaust::Straight, Exhaust::MufflerDelete, Exhaust::ResonatorDelete, Exhaust::Quiet}) {
        INFO(static_cast<int>(exhaust));
        const std::vector<float>& impulse = line.compute(exhaust, 1.0f, 0.002);
        std::vector<double> level;
        for (double hz = kFirstHz; hz <= 6000.0; hz += kStepHz) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < impulse.size(); ++i) {
                const double a = 2.0 * kPiLocal * hz * static_cast<double>(i) / kRate;
                re += impulse[i] * std::cos(a);
                im += impulse[i] * std::sin(a);
            }
            level.push_back(10.0 * std::log10(re * re + im * im + 1e-20));
        }
        // On retire la pente, sur une octave autour de chaque point.
        const int count = static_cast<int>(level.size());
        std::vector<double> relief(level.size());
        double mean = 0.0;
        for (int i = 0; i < count; ++i) {
            const double hz = kFirstHz + kStepHz * i;
            double sum = 0.0;
            int inside = 0;
            for (int j = 0; j < count; ++j) {
                const double other = kFirstHz + kStepHz * j;
                if (other > hz / std::sqrt(2.0) && other < hz * std::sqrt(2.0)) {
                    sum += level[static_cast<std::size_t>(j)];
                    ++inside;
                }
            }
            relief[static_cast<std::size_t>(i)] = level[static_cast<std::size_t>(i)] - sum / inside;
            mean += relief[static_cast<std::size_t>(i)];
        }
        mean /= count;
        double variance = 0.0;
        for (double& value : relief) {
            value -= mean;
            variance += value * value;
        }
        // La régularité : le relief ressemble-t-il à lui-même, décalé de 400 à 1500 Hz ?
        double regular = 0.0;
        for (int lag = 20; lag < 75; ++lag) {
            double same = 0.0;
            for (int i = 0; i + lag < count; ++i) same += relief[static_cast<std::size_t>(i)] * relief[static_cast<std::size_t>(i + lag)];
            regular = std::max(regular, same / variance);
        }
        MESSAGE("relief " << std::sqrt(variance / count) << " dB, régularité " << regular);
        CHECK(regular < 0.53);
    }
}

TEST_CASE("la course se traduit en une cylindrée entière, et revient d'où elle vient") {
    EngineConfig c;
    c.cylinders = 4;
    c.boreMm = 86;
    c.displacementCc = displacementForStroke(c.boreMm, c.cylinders, 86.0);
    CHECK(c.displacementCc == 1998);
    CHECK(c.strokeMm() == doctest::Approx(86.0f).epsilon(0.001));
    // Le plus petit cylindre du site : l'arrondi à l'entier ne déplace la course que de moins de 0,09 mm.
    c.cylinders = 2;
    c.boreMm = 60;
    for (double stroke = 106.5; stroke <= 130.0; stroke += 0.5) {
        c.displacementCc = displacementForStroke(c.boreMm, c.cylinders, stroke);
        CHECK(std::abs(static_cast<double>(c.strokeMm()) - stroke) < 0.09);
    }
}

TEST_CASE("à cylindrée constante, alésage et course se partagent le son, sans changer le débit") {
    const auto render = [](int bore) {
        EngineConfig c;
        c.cylinders = 4;
        c.displacementCc = 2000;
        c.boreMm = bore;
        EngineSound sound(kRate);
        sound.setProfile(c.toProfile());
        sound.setVolume(0.7f);
        sound.setTargetRpm(4000.0f);
        std::vector<float> out(static_cast<std::size_t>(kRate));
        sound.render(out.data(), kRate);
        double energy = 0.0;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i) energy += static_cast<double>(out[i]) * out[i];
        return energy;
    };
    // La bouffée dure √course : plus courte, elle rend le même volume plus vite, donc plus fort.
    CHECK(render(96) > render(86) * 1.02);
    CHECK(render(86) > render(76) * 1.02);
}
