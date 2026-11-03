#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "dsp.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "test_support.hpp"

using namespace revforge;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kDt = 128.0 / 44100.0;

Powertrain fourLitre2(FuelSystem system = FuelSystem::Injection) {
    Powertrain p = referencePowertrain();
    p.engine.cylinders = 4;
    p.engine.displacementCc = 2000;
    p.engine.boreMm = 84;
    p.engine.redlineRpm = 6800;
    p.engine.fuelSystem = system;
    p.car.body = Body::Compact;
    p.car.massKg = 1200;
    p.car.rimInch = 16;
    return p;
}

/** Tenu en troisième à ce régime, puis pied au plancher : le couple et le mélange de la première seconde. */
struct TipIn {
    double meanNm = 0.0;
    double dip = 0.0;
    double leanest = 0.0;
};

TipIn tipIn(const Powertrain& p, double fromRpm) {
    DrivingModel m(p);
    m.setManual(true);
    m.requestShift(1);
    double held = 0.1;
    for (int i = 0; i < static_cast<int>(12.0 / kDt); ++i) {
        const int now = m.gear();
        if (now >= 1 && now < 3 && !m.shifting()) {
            const double next = m.rpm() * m.gearing().overall[static_cast<std::size_t>(now)] / m.gearing().overall[static_cast<std::size_t>(now - 1)];
            if (next > std::max(fromRpm, m.idleRpm() * 1.4) || m.rpm() > m.redlineRpm() * 0.85) m.requestShift(1);
        }
        const double error = (fromRpm - m.rpm()) / 1000.0;
        held = std::clamp(held + 0.5 * error * kDt, 0.02, 1.0);
        m.stepPedal(kDt, now < 3 ? 0.5 : std::clamp(held + error, 0.02, 1.0), now == 3 && error < -0.1 ? std::min(-error, 0.5) : 0.0);
    }
    TipIn out;
    int count = 0;
    double worst = 1.0;
    for (int i = 0; i < static_cast<int>(1.0 / kDt); ++i) {
        m.stepPedal(kDt, 1.0, 0.0);
        out.meanNm += m.torqueNm();
        ++count;
        if (m.carbureted()) {
            worst = std::min(worst, m.carburetor().torqueShare());
            out.leanest = std::max(out.leanest, m.carburetor().lambda());
        }
    }
    out.meanNm /= count;
    out.dip = 1.0 - worst;
    return out;
}

double wltcLikeFuel(const Powertrain& p) {
    DrivingModel m(p);
    for (int i = 0; i < static_cast<int>(120.0 / kDt); ++i) m.step(kDt);
    return m.consumption().fuelTripL();
}

/** Le moteur tenu à ce régime et à cette charge : le son seul, sans modèle de conduite. */
test::Samples drivenSound(const EngineProfile& p, float rpm, float load, float seconds = 3.0f, Listener where = Listener::Front) {
    EngineSound sound(test::kRate);
    sound.setProfile(p);
    sound.setListener(where);
    sound.setTargetRpm(rpm);
    sound.setDrive(load, 0.0f, false, false, load);
    test::Samples out(static_cast<std::size_t>(seconds * test::kRate));
    sound.render(out.data(), static_cast<int>(out.size()));
    return test::slice(out, test::kRate, out.size());
}

/** Énergie dans une bande, par une transformée de Fourier discrète sur des fenêtres courtes. */
double band(const test::Samples& x, double fromHz, double toHz) {
    constexpr std::size_t kWindow = 1024;
    constexpr double kPi = 3.141592653589793;
    double energy = 0.0;
    int windows = 0;
    for (std::size_t at = 0; at + kWindow <= x.size() && windows < 40; at += kWindow * 2, ++windows) {
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

TEST_CASE("un diesel et une voiture électrique ignorent le carburateur") {
    Powertrain diesel = fourLitre2();
    diesel.engine.fuel = Fuel::Diesel;
    Powertrain withCarb = diesel;
    withCarb.engine.fuelSystem = FuelSystem::Carburetor;
    CHECK(fuelSystemOf(withCarb.engine) == FuelSystem::Injection);
    CHECK(withCarb.profile() == diesel.profile());
    CHECK(torqueCurveNm(withCarb, 3500.0) == torqueCurveNm(diesel, 3500.0));
    DrivingModel m(withCarb);
    CHECK_FALSE(m.carbureted());

    Powertrain electric = fourLitre2(FuelSystem::MultiCarb);
    electric.electric = true;
    DrivingModel e(electric);
    CHECK_FALSE(e.carbureted());
}

TEST_CASE("le venturi : la perte suit le carré du débit et l'inverse du carré de sa section") {
    const Powertrain p = fourLitre2(FuelSystem::Carburetor);
    CarburetorSpec spec = carburetorSpecOf(p.engine);
    CHECK(spec.throats == 1);
    // 10 √(2 × 6) : autour des 32 à 37 mm des 2 L de série.
    CHECK(spec.throatM * 1000.0 == doctest::Approx(34.5).epsilon(0.03));
    const double base = carburetorDropPa(spec, 2e-3, 3000.0, 0.8);
    const double doubled = carburetorDropPa(spec, 2e-3, 6000.0, 0.8);
    // Le carré, et un peu plus : la compressibilité.
    CHECK(doubled / base > 4.0);
    CHECK(doubled / base < 4.5);
    const CarburetorSpec narrow = [&] {
        CarburetorSpec s = spec;
        s.throatM *= std::sqrt(0.5);
        s.pipeM *= std::sqrt(0.5);
        return s;
    }();
    CHECK(carburetorDropPa(narrow, 2e-3, 3000.0, 0.8) / base > 4.0);
    // L'ordre de grandeur des fiches : 5 à 10 kPa (1,5 à 3 inHg) à la puissance maximale.
    const double rated = carburetorDropPa(spec, 2e-3, 0.88 * 6800.0, 0.8);
    CHECK(rated > 5000.0);
    CHECK(rated < 10000.0);
    // Un corps par cylindre : la même section sur chaque bouffée, et plus de pipe commune à traverser.
    const CarburetorSpec multi = carburetorSpecOf(fourLitre2(FuelSystem::MultiCarb).engine);
    CHECK(multi.throats == 4);
    CHECK(carburetorDropPa(multi, 2e-3, 0.88 * 6800.0, 0.8) < rated * 0.6);
    // L'à-coup : la perte pèse bien plus sur un col à une seule bouffée que sur un col à quatre.
    CHECK(throatPulsation(1) > 5.0 * throatPulsation(4));
    CHECK(throatPulsation(8) < 1.1);
}

TEST_CASE("le carburateur prend du couple en haut, plus qu'en bas, et davantage s'il est unique") {
    for (const FuelSystem system : {FuelSystem::Carburetor, FuelSystem::MultiCarb}) {
        const double low = torqueCurveNm(fourLitre2(system), 1500.0) / torqueCurveNm(fourLitre2(), 1500.0);
        const double high = torqueCurveNm(fourLitre2(system), 6500.0) / torqueCurveNm(fourLitre2(), 6500.0);
        CHECK(low > 0.985);
        CHECK(high < low - 0.02);
    }
    CHECK(torqueCurveNm(fourLitre2(FuelSystem::Carburetor), 6500.0) < torqueCurveNm(fourLitre2(FuelSystem::MultiCarb), 6500.0));
    CHECK(torqueCurveNm(fourLitre2(FuelSystem::MultiCarb), 6500.0) < torqueCurveNm(fourLitre2(), 6500.0));
    const double injected = powerSummary(fourLitre2()).peakPowerKw;
    const double single = powerSummary(fourLitre2(FuelSystem::Carburetor)).peakPowerKw;
    const double multi = powerSummary(fourLitre2(FuelSystem::MultiCarb)).peakPowerKw;
    MESSAGE("puissance : injection " << injected << " kW, multiples " << multi << " kW, unique " << single << " kW");
    CHECK(multi < injected);
    CHECK(single < multi);
    // De l'ordre de ce qui sépare les BMW 2002 à un et à deux carburateurs, à compression près : une dizaine de pour cent.
    CHECK(single > injected * 0.85);
    CHECK(measurePerformance(fourLitre2(FuelSystem::Carburetor)).zeroTo100S > measurePerformance(fourLitre2()).zeroTo100S);
    CHECK(measurePerformance(fourLitre2(FuelSystem::Carburetor)).topSpeedKmh < measurePerformance(fourLitre2()).topSpeedKmh);
}

TEST_CASE("suralimenté, le montage soufflé perd la même part qu'à l'atmosphère") {
    Powertrain turbo = fourLitre2(FuelSystem::Carburetor);
    turbo.engine.aspiration = Aspiration::Turbo;
    turbo.engine.boostBar = 0.6f;
    Powertrain plain = turbo;
    plain.engine.fuelSystem = FuelSystem::Injection;
    CHECK(breathingOf(turbo, 6000.0) < breathingOf(plain, 6000.0));
    CHECK(torqueCurveNm(turbo, 6500.0) < torqueCurveNm(plain, 6500.0));
    const double share = breathingOf(turbo, 6000.0) / breathingOf(plain, 6000.0);
    const double natural = breathingOf(fourLitre2(FuelSystem::Carburetor), 6000.0) / breathingOf(fourLitre2(), 6000.0);
    CHECK(share == doctest::Approx(natural).epsilon(0.01));
    // Il roule, et le mélange reste compté à sa pression.
    DrivingModel m(turbo);
    for (int i = 0; i < static_cast<int>(10.0 / kDt); ++i) m.stepPedal(kDt, 1.0, 0.0);
    CHECK(m.speedKmh() > 30.0);
    CHECK_FALSE(m.stalled());
}

TEST_CASE("le creux de reprise : plus marqué à bas régime, sur un carburateur unique, et pas sur une injection") {
    const TipIn injectedLow = tipIn(fourLitre2(), 1500.0);
    const TipIn singleLow = tipIn(fourLitre2(FuelSystem::Carburetor), 1500.0);
    const TipIn singleHigh = tipIn(fourLitre2(FuelSystem::Carburetor), 4000.0);
    const TipIn multiLow = tipIn(fourLitre2(FuelSystem::MultiCarb), 1500.0);
    MESSAGE("creux : unique " << singleLow.dip * 100.0 << " % à 1500, " << singleHigh.dip * 100.0 << " % à 4000 ; multiples " << multiLow.dip * 100.0 << " % à 1500");
    CHECK(injectedLow.dip == 0.0);
    CHECK(singleLow.dip > 0.1);
    CHECK(singleLow.dip < 0.45);
    CHECK(singleLow.dip > singleHigh.dip + 0.03);
    CHECK(singleLow.dip > multiLow.dip);
    CHECK(singleLow.leanest > 1.1);
    CHECK(singleLow.meanNm < injectedLow.meanNm - 3.0);
}

TEST_CASE("le film à l'équilibre ne change rien au couple, et la pompe rattrape une part du creux") {
    const auto dipOf = [](CarburetorSpec spec) {
        Carburetor carb;
        carb.configure(spec, 2e-3);
        CarburetorInput in;
        in.rpm = 1500.0;
        in.throttle = 0.1;
        in.fill = 0.3;
        in.airGps = 27.0 * in.fill;
        // Vingt secondes établies : le mélange doit être exactement celui du gicleur.
        for (int i = 0; i < 20000; ++i) carb.step(in, 0.001);
        CHECK(carb.torqueShare() == doctest::Approx(1.0).epsilon(1e-6));
        if (spec.wallShare > 0.0) CHECK(carb.filmG() > 0.0);
        double worst = 1.0;
        const double film = carb.filmG();
        for (int i = 0; i < 2000; ++i) {
            const double before = in.fill;
            in.throttle = std::min(in.throttle + 0.006, 1.0);
            in.fill += (1.0 - in.fill) * 0.01;
            in.fillRate = (in.fill - before) / 0.001;
            in.airGps = 27.0 * in.fill;
            carb.step(in, 0.001);
            worst = std::min(worst, carb.torqueShare());
        }
        if (spec.wallShare > 0.0) CHECK(carb.filmG() > film);
        return 1.0 - worst;
    };
    const CarburetorSpec spec = carburetorSpecOf(fourLitre2(FuelSystem::Carburetor).engine);
    CarburetorSpec noPump = spec;
    noPump.pumpG = 0.0;
    CarburetorSpec dry = spec;
    dry.wallShare = 0.0;
    dry.pumpG = 0.0;
    const double with = dipOf(spec);
    const double without = dipOf(noPump);
    MESSAGE("creux avec pompe " << with * 100.0 << " %, sans " << without * 100.0 << " %");
    CHECK(without > with * 1.3);
    CHECK(with > 0.02);
    // Sans parois, il ne reste que le retard du carburant : le film est bien ce qui creuse.
    CHECK(dipOf(dry) < without);
}

TEST_CASE("pied levé, un carburateur consomme encore, et plus riche en charge") {
    const auto liftOff = [](const Powertrain& p) {
        DrivingModel m(p);
        for (int i = 0; i < static_cast<int>(12.0 / kDt); ++i) m.stepPedal(kDt, 1.0, 0.0);
        for (int i = 0; i < static_cast<int>(1.0 / kDt); ++i) m.stepPedal(kDt, 0.0, 0.0);
        return m.consumption().fuelGps();
    };
    CHECK(liftOff(fourLitre2()) == 0.0);
    CHECK(liftOff(fourLitre2(FuelSystem::Carburetor)) > 0.05);
    CHECK(liftOff(fourLitre2(FuelSystem::MultiCarb)) > 0.05);

    // Plus riche à charge partielle, à cause de l'écart entre cylindres ; encore plus à pleine charge.
    const double part = brakeSpecificFuel(fourLitre2(FuelSystem::Carburetor), 2500.0, 50.0) / brakeSpecificFuel(fourLitre2(), 2500.0, 50.0);
    CHECK(part > 1.04);
    CHECK(part < 1.2);
    CHECK(fuelMapSummary(fourLitre2(FuelSystem::Carburetor)).peakPowerGPerKwh > fuelMapSummary(fourLitre2()).peakPowerGPerKwh * 1.05);

    const double injected = wltcLikeFuel(fourLitre2());
    const double single = wltcLikeFuel(fourLitre2(FuelSystem::Carburetor));
    const double multi = wltcLikeFuel(fourLitre2(FuelSystem::MultiCarb));
    MESSAGE("trajet en ville : injection " << injected << " L, multiples " << multi << " L, unique " << single << " L");
    CHECK(single > injected * 1.05);
    CHECK(multi > injected);
    CHECK(single > multi);
}

TEST_CASE("au rupteur, le carburateur continue de débiter") {
    const auto atLimiter = [](const Powertrain& p) {
        DrivingModel m(p);
        double fuel = 0.0;
        bool wasCut = false;
        for (int i = 0; i < static_cast<int>(6.0 / kDt); ++i) {
            m.stepNeutral(kDt, 1.0);
            if (m.cut()) {
                wasCut = true;
                fuel = std::max(fuel, m.consumption().fuelGps());
            }
        }
        CHECK(wasCut);
        return fuel;
    };
    CHECK(atLimiter(fourLitre2()) >= 0.0);
    CHECK(atLimiter(fourLitre2(FuelSystem::Carburetor)) > atLimiter(fourLitre2()) * 0.5);
}

TEST_CASE("un carburateur tient le ralenti et ne cale pas au lever de pied") {
    for (const FuelSystem system : {FuelSystem::Carburetor, FuelSystem::MultiCarb}) {
        DrivingModel m(fourLitre2(system));
        for (int i = 0; i < static_cast<int>(5.0 / kDt); ++i) m.stepNeutral(kDt, 0.0);
        const double idle = m.idleRpm();
        double low = 1e9, high = 0.0;
        for (int i = 0; i < static_cast<int>(10.0 / kDt); ++i) {
            m.stepNeutral(kDt, 0.0);
            low = std::min(low, m.rpm());
            high = std::max(high, m.rpm());
        }
        CHECK(low > idle * 0.9);
        CHECK(high < idle * 1.1);
        // Coups de gaz au point mort : le film suit sans jamais éteindre le moteur.
        for (int blip = 0; blip < 4; ++blip) {
            for (int i = 0; i < static_cast<int>(0.5 / kDt); ++i) m.stepNeutral(kDt, 1.0);
            for (int i = 0; i < static_cast<int>(1.5 / kDt); ++i) m.stepNeutral(kDt, 0.0);
            CHECK_FALSE(m.stalled());
            CHECK(m.rpm() > idle * 0.7);
        }
    }
}

TEST_CASE("le partage d'un carburateur donne des explosions inégales, pas un corps par cylindre") {
    const EngineProfile injected = fourLitre2().profile();
    const EngineProfile single = fourLitre2(FuelSystem::Carburetor).profile();
    const EngineProfile multi = fourLitre2(FuelSystem::MultiCarb).profile();
    CHECK(multi.firingGain == injected.firingGain);
    CHECK_FALSE(single.firingGain == injected.firingGain);
    const double even = static_cast<double>(single.firingGain[0] / injected.firingGain[0]);
    const double odd = static_cast<double>(single.firingGain[1] / injected.firingGain[1]);
    CHECK(even < 1.0);
    CHECK(odd > even);
    // Une légère irrégularité : quelques pour cent.
    CHECK(odd - even < 0.08);
    CHECK(odd - even > 0.01);
}

TEST_CASE("le souffle du venturi suit la vitesse de l'air, sans passer devant le moteur à bas régime") {
    const EngineProfile injected = fourLitre2().profile();
    const EngineProfile single = fourLitre2(FuelSystem::Carburetor).profile();
    const EngineProfile multi = fourLitre2(FuelSystem::MultiCarb).profile();
    const auto hiss = [](const EngineProfile& p, float rpm, float load) { return band(drivenSound(p, rpm, load), 1000.0, 4000.0); };
    const double injectedHigh = hiss(injected, 6000.0f, 1.0f);
    const double singleHigh = hiss(single, 6000.0f, 1.0f);
    const double multiHigh = hiss(multi, 6000.0f, 1.0f);
    MESSAGE("1-4 kHz à 6000 tr/min, devant la voiture : unique +" << dB(singleHigh / injectedHigh) << " dB, multiples +" << dB(multiHigh / injectedHigh) << " dB");
    CHECK(singleHigh > injectedHigh);
    CHECK(multiHigh > singleHigh);
    CHECK(dB(multiHigh / injectedHigh) < 12.0);

    // « Tout ce qui ne vient pas de l'échappement garde sa place à bas régime », et pas de bruit blanc derrière le moteur.
    for (const EngineProfile* p : {&single, &multi}) {
        const test::Samples mine = drivenSound(*p, 1500.0f, 1.0f, 3.0f, Listener::Outside);
        const test::Samples theirs = drivenSound(injected, 1500.0f, 1.0f, 3.0f, Listener::Outside);
        const double airy = dB(band(mine, 8000.0, 16000.0) / band(mine, 500.0, 2000.0));
        const double before = dB(band(theirs, 8000.0, 16000.0) / band(theirs, 500.0, 2000.0));
        MESSAGE("8-16 kHz face à 500-2000 Hz, à 1500 tr/min pleine charge : " << airy << " dB, injection " << before << " dB");
        CHECK(airy < before + 2.0);
        // Ce premier contrôle ne voit pas un souffle à niveau fixe, qui reste sous 6 kHz : la contre-épreuve l'a montré. Devant la voiture, tout près de lui, si.
        const double frontDb = dB(band(drivenSound(*p, 1500.0f, 1.0f), 500.0, 4000.0) / band(drivenSound(injected, 1500.0f, 1.0f), 500.0, 4000.0));
        MESSAGE("500-4000 Hz devant la voiture, à 1500 tr/min pleine charge : +" << frontDb << " dB");
        CHECK(frontDb < 2.0);
    }
}

TEST_CASE("sur un carburateur, le lever de pied peut détoner sans réglage, et le son ne dépend pas de la taille des blocs") {
    const auto popsOf = [](Powertrain p) {
        p.engine.exhaust = Exhaust::Sport;
        DrivingModel m(p);
        EngineSound sound(44100);
        sound.setProfile(p.profile());
        std::vector<float> block(128);
        for (int i = 0; i < static_cast<int>(40.0 / kDt); ++i) {
            const double t = static_cast<double>(i) * kDt;
            m.stepPedal(kDt, std::fmod(t, 6.0) < 4.0 ? 1.0 : 0.0, 0.0);
            sound.setTargetRpm(static_cast<float>(m.rpm()));
            sound.setDrive(static_cast<float>(m.load()), static_cast<float>(m.boost()), m.cut(), m.cranking(), static_cast<float>(m.throttle()));
            sound.render(block.data(), 128);
        }
        return sound.pops();
    };
    const auto fourSport = [](FuelSystem system) { return fourLitre2(system); };
    const unsigned injected = popsOf(fourSport(FuelSystem::Injection));
    const unsigned single = popsOf(fourSport(FuelSystem::Carburetor));
    MESSAGE("détonations en 40 s, sans réglage : injection " << injected << ", carburateur " << single);
    CHECK(injected == 0);
    CHECK(single > 0);

    for (const FuelSystem system : {FuelSystem::Carburetor, FuelSystem::MultiCarb}) {
        const EngineProfile p = fourLitre2(system).profile();
        const auto rendered = [&p](int blockSize) {
            EngineSound sound(44100);
            sound.setProfile(p);
            sound.setTargetRpm(4000.0f);
            sound.setDrive(1.0f, 0.0f, false, false, 1.0f);
            std::vector<float> all;
            std::vector<float> block(static_cast<std::size_t>(blockSize));
            while (all.size() < 44100) {
                sound.render(block.data(), blockSize);
                all.insert(all.end(), block.begin(), block.end());
            }
            all.resize(44100);
            return all;
        };
        CHECK(rendered(128) == rendered(517));
        EngineSound sound(44100);
        sound.setProfile(p);
        sound.setTargetRpm(4000.0f);
        std::vector<float> block(512);
        sound.render(block.data(), 512);
        const long before = g_allocations.load();
        sound.render(block.data(), 512);
        CHECK(g_allocations.load() == before);
    }
}
