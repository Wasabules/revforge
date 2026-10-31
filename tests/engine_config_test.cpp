#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

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

/** Monte, tient, puis lève le pied d'un coup. Renvoie (pied sur l'accélérateur, lever de pied). */
std::pair<Samples, Samples> blip(const EngineConfig& c) {
    const EngineProfile p = c.toProfile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
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
std::pair<float, double> loudestPop(const EngineConfig& c) {
    const Samples lift = blip(c).second;
    const auto top = std::max_element(lift.begin(), lift.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); });
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
    CHECK(brightness(big) < brightness(small));
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
    CHECK(loudestPop(straight).first > loudestPop(stock).first);
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
    const auto hiss = [](const EngineConfig& c) {
        const Samples lift = blip(c).second;
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
