#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "dsp.hpp"
#include "exhaust_line.hpp"
#include "injector_sound.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/injection.hpp"
#include "revforge/particulate_filter.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using test::Samples;

extern std::atomic<long> g_allocations;

namespace {

Powertrain petrol2(FuelSystem system = FuelSystem::Injection) {
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

Powertrain dieselVan(DieselInjection injection = DieselInjection::Mechanical) {
    Powertrain p = findPreset("diesel_four")->powertrain();
    p.engine.dieselInjection = injection;
    return p;
}

double bandEnergy(const Samples& x, double fromHz, double toHz) {
    constexpr int kSize = 8192;
    dsp::Fft fft;
    fft.init(kSize);
    std::vector<float> re(kSize), im(kSize);
    double sum = 0.0;
    for (std::size_t start = 0; start + kSize <= x.size(); start += kSize / 2) {
        for (int i = 0; i < kSize; ++i) {
            const double window = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * i / kSize);
            re[static_cast<std::size_t>(i)] = static_cast<float>(window * x[start + static_cast<std::size_t>(i)]);
            im[static_cast<std::size_t>(i)] = 0.0f;
        }
        fft.transform(re.data(), im.data(), false);
        for (int k = 1; k < kSize / 2; ++k) {
            const double hz = static_cast<double>(k) * test::kRate / kSize;
            if (hz < fromHz || hz >= toHz) continue;
            sum += static_cast<double>(re[static_cast<std::size_t>(k)]) * re[static_cast<std::size_t>(k)] + static_cast<double>(im[static_cast<std::size_t>(k)]) * im[static_cast<std::size_t>(k)];
        }
    }
    return sum;
}

double dB(double ratio) { return 10.0 * std::log10(ratio); }

/** Le son d'un moteur tenu à ce régime et à cette charge, depuis cette écoute. `level` reçoit le dB(A) estimé à la fin. */
Samples held(const Powertrain& p, float rpm, float load, Listener where, float* level = nullptr, float seconds = 4.0f) {
    EngineSound sound(test::kRate);
    sound.setProfile(p.profile());
    sound.setListener(where);
    sound.setVolume(0.7f);
    sound.setTargetRpm(rpm);
    sound.setDrive(load, 0.0f, false, false, load);
    Samples out(static_cast<std::size_t>(seconds * test::kRate));
    sound.render(out.data(), static_cast<int>(out.size()));
    if (level) *level = sound.levelDbA();
    return test::slice(out, test::kRate, out.size());
}

/**
 * L'écart d'une bande entre deux rendus, rapporté aux graves (63 à 400 Hz) : le niveau de sortie suit le niveau entendu, et
 * le tic n'a rien sous 400 Hz.
 */
double bandRise(const Samples& before, const Samples& after, double fromHz, double toHz) {
    return dB(bandEnergy(after, fromHz, toHz) / bandEnergy(before, fromHz, toHz)) - dB(bandEnergy(after, 63.0, 400.0) / bandEnergy(before, 63.0, 400.0));
}

/** Tenu en troisième à 1500 tr/min, pied léger, puis au plancher : le couple moyen de la première demi-seconde (comme carburetor_test). */
double tipInNm(DrivingModel& m, double* leanest = nullptr) {
    constexpr double kDt = 128.0 / 44100.0;
    constexpr double kFrom = 1500.0;
    m.setManual(true);
    m.requestShift(1);
    double held = 0.1;
    for (int i = 0; i < static_cast<int>(12.0 / kDt); ++i) {
        const int now = m.gear();
        if (now >= 1 && now < 3 && !m.shifting()) {
            const double next = m.rpm() * m.gearing().overall[static_cast<std::size_t>(now)] / m.gearing().overall[static_cast<std::size_t>(now - 1)];
            if (next > std::max(kFrom, m.idleRpm() * 1.4) || m.rpm() > m.redlineRpm() * 0.85) m.requestShift(1);
        }
        const double error = (kFrom - m.rpm()) / 1000.0;
        held = std::clamp(held + 0.5 * error * kDt, 0.02, 1.0);
        m.stepPedal(kDt, now < 3 ? 0.5 : std::clamp(held + error, 0.02, 1.0), now == 3 && error < -0.1 ? std::min(-error, 0.5) : 0.0);
    }
    double sum = 0.0;
    int count = 0;
    double lean = 1.0;
    for (int i = 0; i < static_cast<int>(0.5 / kDt); ++i) {
        m.stepPedal(kDt, 1.0, 0.0);
        sum += m.torqueNm();
        lean = std::min(lean, m.portFilm().richnessShare());
        ++count;
    }
    if (leanest) *leanest = lean;
    return sum / count;
}

}  // namespace

// ── L'injection directe ──

TEST_CASE("injection directe : la vaporisation refroidit la charge, et remplit de 2 à 9 % de plus") {
    // Toute l'essence vaporisée : 18,5 K à λ = 1 (305 kJ/kg, Heywood).
    CHECK(vaporizationDropK(1.0) == doctest::Approx(18.5).epsilon(0.03));
    CHECK(vaporizationDropK(0.88) > vaporizationDropK(1.0));
    const ChargeCooling direct = chargeCooling(FuelSystem::Direct, 3000.0, 1.0, 6800.0, 0.88, 9.0);
    const ChargeCooling port = chargeCooling(FuelSystem::Injection, 3000.0, 1.0, 6800.0, 0.88, 9.0);
    CHECK(direct.atIvcK > 10.0);
    CHECK(direct.atIvcK < 20.0);
    CHECK(direct.atIvcK > 3.0 * port.atIvcK);
    // La compression le grandit : au point mort haut, c'est ce que voit le cliquetis.
    CHECK(direct.atTdcK > 1.8 * direct.atIvcK);
    // Anderson et al. (SAE 962018) : 2 à 3 % en série ; Wyszynski et al. (SAE 2002-01-0839) : 9 % sur un monocylindre.
    const double gain = directFillGain(3000.0, 6800.0, 293.0);
    CHECK(gain > 0.02);
    CHECK(gain < 0.09);
    // Contre-épreuve : des gouttes qui n'ont plus le temps de s'évaporer ne refroidissent plus l'air avant la fermeture.
    CHECK(evaporatedBeforeIvc(3000.0, 200.0) == doctest::Approx(1.0));
    CHECK(evaporatedBeforeIvc(30000.0, 20.0) < 0.6);
}

TEST_CASE("injection directe : plus de couple à pleine charge, une pompe à payer, et pas de carburateur") {
    const Powertrain port = petrol2();
    const Powertrain direct = petrol2(FuelSystem::Direct);
    for (double rpm : {2000.0, 4000.0, 6000.0}) {
        const double share = torqueCurveNm(direct, rpm) / torqueCurveNm(port, rpm);
        CHECK(share > 1.02);
        CHECK(share < 1.06);
    }
    const double pump = directPumpTorqueNm(direct, 4000.0, 1.0, 0.0);
    CHECK(pump > 0.1);
    CHECK(pump < 1.5);
    CHECK(directPumpTorqueNm(port, 4000.0, 1.0, 0.0) == 0.0);
    CHECK_FALSE(hasCarburetor(direct.engine));
    CHECK(fuelSystemOf(direct.engine) == FuelSystem::Direct);
    CHECK(carburetorSpecOf(direct.engine).kind == FuelSystem::Injection);
    CHECK_FALSE(DrivingModel(direct).carbureted());
    CHECK(direct.profile().directInjection);
    CHECK_FALSE(port.profile().directInjection);
    // Un diesel ignore l'injection directe d'essence : il a la sienne.
    Powertrain diesel = dieselVan();
    diesel.engine.fuelSystem = FuelSystem::Direct;
    CHECK_FALSE(directInjected(diesel.engine));
    CHECK(diesel.profile() == dieselVan().profile());
}

TEST_CASE("injection directe : deux chocs par injection, un par course de pompe, aucun pendant une coupure") {
    const EngineProfile p = petrol2(FuelSystem::Direct).profile();
    constexpr float kRpm = 800.0f;
    constexpr float kSeconds = 3.0f;
    InjectorSound fed(static_cast<float>(test::kRate));
    InjectorSound cut(static_cast<float>(test::kRate));
    fed.setup(p);
    cut.setup(p);
    fed.control(kRpm, 0.15f, 0.1f);
    cut.control(kRpm, 0.15f, 0.1f);
    std::array<float, kMaxCylinders> timing{};
    float phase = 0.0f;
    double energy = 0.0;
    for (int i = 0; i < static_cast<int>(kSeconds * test::kRate); ++i) {
        phase += kRpm / 120.0f / static_cast<float>(test::kRate);
        if (phase >= 1.0f) phase -= 1.0f;
        fed.sample(phase, p.firingCount, p.firing, timing, 1.0f);
        const float silent = cut.sample(phase, p.firingCount, p.firing, timing, 0.0f);
        energy += static_cast<double>(silent) * silent;
    }
    const double cycles = kSeconds * kRpm / 120.0;
    CHECK(std::fabs(static_cast<double>(fed.needleHits()) - 2.0 * 4.0 * cycles) <= 8.0);
    CHECK(std::fabs(static_cast<double>(fed.pumpHits()) - 4.0 * cycles) <= 4.0);
    CHECK(cut.needleHits() == 0u);
    CHECK(cut.pumpHits() == 0u);
    CHECK(energy == 0.0);
    // Au ralenti, une injection dure moins d'une milliseconde, à quelques dizaines de bar.
    CHECK(fed.injectionSeconds() > 0.0003f);
    CHECK(fed.injectionSeconds() < 0.0015f);
    CHECK(fed.railBar() > 40.0f);
    CHECK(fed.railBar() < 100.0f);
    // Pleine charge au régime maximal : 200 bar, de 2 à 5 ms.
    fed.control(6800.0f, 1.0f, 0.1f);
    CHECK(fed.railBar() == doctest::Approx(200.0f));
    CHECK(fed.injectionSeconds() > 0.002f);
    CHECK(fed.injectionSeconds() < 0.005f);
}

TEST_CASE("injection directe : le tic s'entend au ralenti sous le capot, pas de l'extérieur ni à charge") {
    const Powertrain port = findPreset("hot_hatch_na")->powertrain();
    Powertrain direct = port;
    direct.engine.fuelSystem = FuelSystem::Direct;
    const float idle = port.profile().idleRpm;
    const Samples portBay = held(port, idle, 0.15f, Listener::EngineBay);
    const Samples directBay = held(direct, idle, 0.15f, Listener::EngineBay);
    // Calé sur SAE 2011-01-0930 : la pompe domine vers 1,6 kHz de 5,8 à 7,5 dB, les injecteurs au-dessus de 5 kHz de 3 dB.
    const double pump = bandRise(portBay, directBay, 1414.0, 1782.0);
    const double needles = bandRise(portBay, directBay, 5000.0, 16000.0);
    CHECK(pump > 4.5);
    CHECK(pump < 9.0);
    CHECK(needles > 1.5);
    CHECK(needles < 5.0);
    // Dehors, derrière la voiture, l'échappement le couvre ; et les aigus ne passent pas devant les médiums.
    const Samples portOut = held(port, idle, 0.15f, Listener::Outside);
    const Samples directOut = held(direct, idle, 0.15f, Listener::Outside);
    CHECK(std::fabs(bandRise(portOut, directOut, 5000.0, 16000.0)) < 0.5);
    const double topBefore = dB(bandEnergy(portBay, 8000.0, 16000.0) / bandEnergy(portBay, 500.0, 2000.0));
    const double topAfter = dB(bandEnergy(directBay, 8000.0, 16000.0) / bandEnergy(directBay, 500.0, 2000.0));
    CHECK(topAfter - topBefore < 3.0);
    // À 3000 tr/min, le bloc et l'échappement ont grandi : le tic, presque pas.
    const Samples portMid = held(port, 3000.0f, 0.3f, Listener::EngineBay);
    const Samples directMid = held(direct, 3000.0f, 0.3f, Listener::EngineBay);
    CHECK(bandRise(portMid, directMid, 1414.0, 1782.0) < 3.0);
    CHECK(bandRise(portMid, directMid, 5000.0, 16000.0) < 1.5);
}

TEST_CASE("injection directe et rampe commune : le son n'alloue pas, et ne dépend pas de la taille des blocs") {
    Powertrain direct = petrol2(FuelSystem::Direct);
    direct.engine.egr = true;
    direct.engine.particulateFilter = true;
    const EngineProfile p = direct.profile();
    const EngineProfile rail = dieselVan(DieselInjection::CommonRail).profile();
    EngineSound core(test::kRate);
    core.setProfile(p);
    core.setVolume(0.8f);
    Samples out(4096);
    core.render(out.data(), 4096);
    const long before = g_allocations.load();
    for (int i = 0; i < 30; ++i) {
        core.setTargetRpm(i % 2 == 0 ? 6000.0f : 900.0f);
        core.render(out.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
    core.setProfile(rail);
    core.render(out.data(), 4096);

    for (const EngineProfile& profile : {p, rail}) {
        EngineSound a(test::kRate), b(test::kRate);
        a.setProfile(profile);
        b.setProfile(profile);
        a.setTargetRpm(2500.0f);
        b.setTargetRpm(2500.0f);
        Samples one(test::kRate), many(test::kRate);
        for (int i = 0; i < test::kRate; ++i) a.render(one.data() + i, 1);
        b.render(many.data(), test::kRate);
        CHECK(one == many);
    }
}

// ── Le film d'une injection indirecte ──

TEST_CASE("film d'une injection indirecte : plus de dépôt, plus lent à repartir, à froid et sous pression") {
    const FilmParameters warm = portFilmParameters(90.0, 1.0, 30.0);
    const FilmParameters cold = portFilmParameters(20.0, 1.0, 30.0);
    const FilmParameters vacuum = portFilmParameters(90.0, 0.3, 30.0);
    // Chaud : X de 0,15 à 0,3 et τ de 0,1 à 0,4 s ; froid : X vers 0,5 et τ au-delà de la seconde (ordres de grandeur publiés).
    CHECK(warm.deposit > 0.15);
    CHECK(warm.deposit < 0.3);
    CHECK(warm.tauS > 0.1);
    CHECK(warm.tauS < 0.4);
    CHECK(cold.deposit > 0.45);
    CHECK(cold.deposit < 0.6);
    CHECK(cold.tauS > 0.8);
    // Sous vide, l'essence distille plus bas : moins de dépôt, et il s'évapore plus vite.
    CHECK(vacuum.deposit < 0.5 * warm.deposit);
    CHECK(vacuum.tauS < warm.tauS);
    // La distillation d'une essence EN 228 : E70 de 22 à 50 %, E100 de 46 à 71 %, E150 au moins 75 %.
    CHECK(distilledShare(70.0) > 0.22);
    CHECK(distilledShare(70.0) < 0.5);
    CHECK(distilledShare(100.0) > 0.46);
    CHECK(distilledShare(150.0) >= 0.75);
    const FilmParameters same = coldFilmShare(90.0, 0.6);
    CHECK(same.deposit == doctest::Approx(1.0));
    CHECK(same.tauS == doctest::Approx(1.0));
}

TEST_CASE("film d'une injection indirecte : compensé à 90 °C, pauvre à l'accélération et riche au lever de pied à froid") {
    constexpr double kStep = 0.001;
    double richestWarm = 1.0;
    for (double coolant : {90.0, 20.0}) {
        PortFuelFilm film;
        PortFilmInput in;
        in.coolantC = coolant;
        in.wantedGps = 0.4;
        in.manifoldShare = 0.35;
        double leanest = 1.0, richest = 1.0;
        for (int i = 0; i < 2000; ++i) film.step(in, kStep);
        CHECK(film.richnessShare() == doctest::Approx(1.0).epsilon(1e-6));
        // Le pied au plancher : le collecteur se remplit en 0,1 s, l'air demande quatre fois plus de carburant.
        for (int i = 0; i < 3000; ++i) {
            const double t = std::min(i * kStep / 0.1, 1.0);
            in.wantedGps = 0.4 + 1.2 * t;
            in.manifoldShare = 0.35 + 0.65 * t;
            film.step(in, kStep);
            leanest = std::min(leanest, film.richnessShare());
        }
        // Le pied levé : le collecteur se vide, et le film s'évapore d'un coup sous le vide.
        for (int i = 0; i < 3000; ++i) {
            const double t = std::min(i * kStep / 0.1, 1.0);
            in.wantedGps = 1.6 - 1.2 * t;
            in.manifoldShare = 1.0 - 0.65 * t;
            film.step(in, kStep);
            richest = std::max(richest, film.richnessShare());
        }
        if (coolant == 90.0) {
            // Le calculateur connaît son film : à l'accélération, il tombe juste. Au lever de pied, il coupe tout, mais ne peut
            // pas retirer ce qui s'évapore de la paroi : un peu de richesse reste.
            CHECK(leanest == doctest::Approx(1.0).epsilon(1e-6));
            richestWarm = richest;
        } else {
            CHECK(leanest < 0.85);
            CHECK(richest > 1.1);
            CHECK(richest > richestWarm);
        }
    }
}

TEST_CASE("film d'une injection indirecte : à 90 °C le modèle de conduite ne change pas d'un bit, à froid la reprise faiblit") {
    const Powertrain p = petrol2();
    // Finitions : le réseau thermique agit toujours ; tenu à la référence, il laisse poser l'eau à la main.
    DrivingModel plain(p);
    plain.holdThermalReference(true);
    DrivingModel warm(p);
    warm.holdThermalReference(true);
    warm.setCoolantC(kWarmCoolantC);
    DrivingModel cold(p);
    cold.holdThermalReference(true);
    cold.setCoolantC(20.0);
    const double plainNm = tipInNm(plain);
    const double warmNm = tipInNm(warm);
    double leanest = 1.0;
    const double coldNm = tipInNm(cold, &leanest);
    CHECK(warmNm == plainNm);
    CHECK(plain.rpm() == warm.rpm());
    CHECK(plain.gear() == 3);
    CHECK(warmNm > 50.0);
    CHECK_FALSE(warm.portFilmActive());
    CHECK(cold.portFilmActive());
    // Contre-épreuve : le froid change le mélange, et le couple de la reprise le suit.
    CHECK(leanest < 0.9);
    CHECK(coldNm < 0.97 * warmNm);
    // L'injection directe n'a pas de film : le froid ne lui fait rien ici.
    DrivingModel direct(petrol2(FuelSystem::Direct));
    direct.holdThermalReference(true);
    direct.setCoolantC(20.0);
    tipInNm(direct);
    CHECK_FALSE(direct.portFilmActive());
}

TEST_CASE("film d'un carburateur : plus gros à froid, inchangé à 90 °C") {
    const CarburetorSpec spec = carburetorSpecOf(petrol2(FuelSystem::Carburetor).engine);
    Carburetor warm, cold, plain;
    for (Carburetor* c : {&warm, &cold, &plain}) c->configure(spec, 0.002);
    CarburetorInput in;
    in.rpm = 2000.0;
    in.airGps = 20.0;
    in.fill = 0.4;
    in.throttle = 0.3;
    for (int i = 0; i < 100; ++i) {
        plain.step(in, 0.001);
        in.coolantC = kWarmCoolantC;
        warm.step(in, 0.001);
        in.coolantC = 20.0;
        cold.step(in, 0.001);
        in.coolantC = kWarmCoolantC;
    }
    CHECK(warm.filmG() == plain.filmG());
    CHECK(warm.torqueShare() == plain.torqueShare());
    CHECK(cold.filmG() > 2.0 * warm.filmG());
}

// ── Le diesel ──

TEST_CASE("rampe commune : moins de claquement au ralenti, une pente de pression de l'ordre publié") {
    const Powertrain mech = dieselVan();
    DieselPoint idle = dieselPointOf(mech, 900.0, 0.12, 0.0);
    const DieselBurn pump = dieselBurn(idle);
    idle.commonRail = true;
    const DieselBurn rail = dieselBurn(idle);
    // Un diesel à injection directe et pompe mécanique : de 5 à 10 bar/° ; avec une injection pilote, de 2 à 5 bar/°.
    CHECK(pump.peakRiseBarPerDeg > 3.0);
    CHECK(pump.peakRiseBarPerDeg < 12.0);
    CHECK(rail.peakRiseBarPerDeg > 1.0);
    CHECK(rail.peakRiseBarPerDeg < 5.0);
    // Au ralenti, la pompe mécanique brûle presque tout en prémélange ; la pilote en laisse peu à la principale.
    CHECK(pump.premixShare > 0.8);
    CHECK(rail.premixShare < pump.premixShare);
    CHECK(rail.delayDeg < 0.5 * pump.delayDeg);
    const double db = 20.0 * std::log10(dieselKnockShare(idle));
    CHECK(db < -3.0);
    CHECK(db > -10.0);
    // La pompe mécanique reste la référence, à l'identique.
    idle.commonRail = false;
    CHECK(dieselKnockShare(idle) == 1.0);
    CHECK_FALSE(mech.profile().commonRail);
    CHECK(dieselVan(DieselInjection::CommonRail).profile().commonRail);
}

TEST_CASE("rampe commune : plus de couple, surtout en bas") {
    const Powertrain mech = dieselVan();
    const Powertrain rail = dieselVan(DieselInjection::CommonRail);
    const double low = torqueCurveNm(rail, 1500.0) / torqueCurveNm(mech, 1500.0);
    const double high = torqueCurveNm(rail, 3800.0) / torqueCurveNm(mech, 3800.0);
    // Denso : +35 % de couple sur un 2 L face à sa pompe ; PSA, du 1.9 TD (196 Nm) au 2.0 HDi 110 (250 Nm) : +27 %.
    CHECK(low > 1.10);
    CHECK(low < 1.35);
    CHECK(high > 1.03);
    CHECK(high < low);
    for (double rpm = 1000.0; rpm <= 4400.0; rpm += 200.0) CHECK(torqueCurveNm(rail, rpm) > torqueCurveNm(mech, rpm));
}

TEST_CASE("rampe commune : le ralenti s'entend moins sous le capot") {
    const Powertrain mech = dieselVan();
    const Powertrain rail = dieselVan(DieselInjection::CommonRail);
    const float idle = mech.profile().idleRpm;
    float pumpDb = 0.0f, railDb = 0.0f;
    held(mech, idle, 0.12f, Listener::EngineBay, &pumpDb);
    held(rail, idle, 0.12f, Listener::EngineBay, &railDb);
    // PSA annonce 3 dB pour la pilote, Denso 6,5 dB au ralenti : le bloc et la distribution restent.
    CHECK(pumpDb - railDb > 2.0);
    CHECK(pumpDb - railDb < 8.0);
}

// ── La recirculation ──

TEST_CASE("recirculation : ni au ralenti d'un essence, ni à pleine charge, jusqu'à 15 et 30 %") {
    CHECK(egrRate(false, 0.1, 800.0, 6800.0) == 0.0);
    CHECK(egrRate(false, 1.0, 2000.0, 6800.0) == 0.0);
    CHECK(egrRate(true, 1.0, 2000.0, 4500.0) == 0.0);
    CHECK(egrRate(false, 0.4, 2000.0, 6800.0) == doctest::Approx(0.15));
    CHECK(egrRate(true, 0.2, 1500.0, 4500.0) == doctest::Approx(0.30));
    CHECK(egrRate(true, 0.2, 4400.0, 4500.0) == 0.0);
    // Metghalchi et Keck : 15 % de gaz brûlés divisent presque par deux la vitesse de flamme laminaire.
    CHECK(egrFlameSpeedShare(0.15) == doctest::Approx(0.52).epsilon(0.02));
    CHECK(egrBurnStretch(0.15) > 1.15);
    CHECK(egrBurnStretch(0.15) < 1.35);
    // Un rotatif n'en a pas.
    Powertrain rotary = petrol2();
    rotary.engine.layout = Layout::Rotary;
    rotary.engine.cylinders = 2;
    rotary.engine.egr = true;
    CHECK_FALSE(recirculated(rotary.engine));
    CHECK_FALSE(rotary.profile().egr);
}

TEST_CASE("recirculation : un essence consomme de 2 à 5 % de moins à charge partielle, un diesel presque autant") {
    const Powertrain petrol = petrol2();
    Powertrain petrolEgr = petrol;
    petrolEgr.engine.egr = true;
    const Powertrain diesel = dieselVan();
    Powertrain dieselEgr = diesel;
    dieselEgr.engine.egr = true;
    const double twoBar = 2.0 * 1e5 * 0.002 / (4.0 * 3.141592653589793);
    const double petrolGain = 1.0 - brakeSpecificFuel(petrolEgr, 2000.0, twoBar) / brakeSpecificFuel(petrol, 2000.0, twoBar);
    CHECK(petrolGain > 0.02);
    CHECK(petrolGain < 0.05);
    const double dieselGain = 1.0 - brakeSpecificFuel(dieselEgr, 2000.0, twoBar) / brakeSpecificFuel(diesel, 2000.0, twoBar);
    CHECK(std::fabs(dieselGain) < 0.02);
    // À pleine charge, rien : la recirculation est fermée.
    CHECK(torqueCurveNm(petrolEgr, 4000.0) == torqueCurveNm(petrol, 4000.0));
    // Une combustion plus lente fait moins de bruit.
    CHECK(egrCombustionNoiseDb(false, 0.15, 1.0) < -1.0);
    CHECK(egrCombustionNoiseDb(true, 0.3, 3.0) < 0.0);
}

// ── Le filtre à particules ──

TEST_CASE("filtre à particules : la contre-pression croît avec la suie, de l'ordre publié") {
    Powertrain p = dieselVan();
    p.engine.particulateFilter = true;
    const ParticulateFilterSpec spec = particulateFilterSpecOf(p.engine);
    const ExhaustState rated = exhaustStateOf(p, 3960.0, 1.0, 1.0);
    const double clean = filterBackPressurePa(spec, 0.0, rated.kgS, rated.kelvin);
    const double full = filterBackPressurePa(spec, spec.startGPerL * spec.volumeL, rated.kgS, rated.kelvin);
    // Propre, de 2 à 10 kPa à la puissance maximale ; plein, de 15 à 40 kPa.
    CHECK(clean > 2000.0);
    CHECK(clean < 10000.0);
    CHECK(full > 15000.0);
    CHECK(full < 40000.0);
    CHECK(filterLineResistance(spec, 10.0) > filterLineResistance(spec, 0.0));
    // Le couple : quelques newton-mètres à pleine charge.
    const double loss = torqueCurveNm(dieselVan(), 3960.0) - torqueCurveNm(p, 3960.0);
    CHECK(loss > 0.5);
    CHECK(loss < 0.05 * torqueCurveNm(dieselVan(), 3960.0));
    CHECK(filterPumpingTorqueNm(dieselVan(), 3960.0, 1.0, 1.0, 10.0) == 0.0);
    // Une injection indirecte d'essence ne fait pas assez de suie : le réglage y reste sans effet.
    Powertrain port = petrol2();
    port.engine.particulateFilter = true;
    CHECK_FALSE(particulateFiltered(port.engine));
    CHECK(port.profile() == petrol2().profile());
    CHECK_FALSE(DrivingModel(port).filtered());
}

TEST_CASE("filtre à particules : la régénération se déclenche au plein, brûle la suie, et se paie en carburant") {
    Powertrain p = dieselVan();
    p.engine.particulateFilter = true;
    ParticulateFilter filter;
    filter.configure(particulateFilterSpecOf(p.engine));
    const double full = filter.spec().startGPerL * filter.spec().volumeL;
    filter.setSootG(full * 1.001);
    const ExhaustState gas = exhaustStateOf(p, 2000.0, 0.35, 0.4);
    FilterInput in;
    in.exhaustKgS = gas.kgS;
    in.exhaustK = gas.kelvin;
    in.fuelGps = gas.fuelGps;
    in.lambda = gas.lambda;
    in.smokeLambda = smokeLambdaOf(p, 2000.0);
    double post = 0.0, hottest = 0.0, seconds = 0.0;
    bool started = false;
    constexpr double kStep = 0.01;
    for (int i = 0; i < static_cast<int>(3600.0 / kStep); ++i) {
        filter.step(in, kStep);
        started = started || filter.regenerating();
        post += filter.extraFuelGps() * kStep;
        hottest = std::max(hottest, filter.temperatureK());
        if (filter.regenerating()) seconds += kStep;
        if (started && !filter.regenerating()) break;
    }
    CHECK(started);
    CHECK_FALSE(filter.regenerating());
    CHECK(filter.loadGPerL() <= filter.spec().endGPerL + 0.01);
    CHECK(hottest > 850.0);
    // De 3 à 30 min, et quelques dizaines de grammes de gazole : de l'ordre publié (10 à 20 min, selon la conduite).
    CHECK(seconds > 180.0);
    CHECK(seconds < 1800.0);
    CHECK(post > 20.0);
    CHECK(post < 400.0);
    CHECK(filter.extraFuelGps() == 0.0);
}

TEST_CASE("filtre à particules : la consommation lit la post-injection, le couple ne la voit pas") {
    Powertrain p = dieselVan();
    p.engine.particulateFilter = true;
    DrivingModel regen(p), plain(p);
    regen.setFilterSootG(regen.particulateFilter().spec().startGPerL * regen.particulateFilter().spec().volumeL * 1.001);
    for (int i = 0; i < 5000; ++i) {
        regen.stepPedal(0.002, 0.25, 0.0);
        plain.stepPedal(0.002, 0.25, 0.0);
    }
    CHECK(regen.particulateFilter().regenerating());
    CHECK_FALSE(plain.particulateFilter().regenerating());
    CHECK(regen.particulateFilter().extraFuelGps() > 0.0);
    CHECK(regen.consumption().fuelGps() > 1.05 * plain.consumption().fuelGps());
}

TEST_CASE("filtre à particules : un essence se régénère seul, en coupure d'injection, sur un filtre chaud") {
    Powertrain p = petrol2(FuelSystem::Direct);
    p.engine.particulateFilter = true;
    ParticulateFilter filter;
    filter.configure(particulateFilterSpecOf(p.engine));
    filter.setSootG(filter.spec().startGPerL * filter.spec().volumeL * 2.0);
    const double before = filter.sootG();
    FilterInput hot;
    hot.exhaustKgS = 0.06;
    hot.exhaustK = 1050.0;
    hot.fuelGps = 4.0;
    hot.lambda = 0.88;
    for (int i = 0; i < 60000; ++i) filter.step(hot, 0.01);
    CHECK_FALSE(filter.regenerating());
    CHECK(filter.extraFuelGps() == 0.0);
    const double afterLoad = filter.sootG();
    // Pied levé : de l'air pur sur un filtre chaud, la suie brûle.
    FilterInput lifted = hot;
    lifted.burning = false;
    lifted.exhaustK = 600.0;
    for (int i = 0; i < 3000; ++i) filter.step(lifted, 0.01);
    CHECK(filter.sootG() < 0.9 * afterLoad);
    CHECK(afterLoad <= before * 1.2);
}

TEST_CASE("filtre à particules : la ligne perd de ses aigus, et le son suit") {
    ExhaustLine without(44100.0f), with(44100.0f);
    without.compute(Exhaust::Stock, 1.0f, 0.002, 4);
    Powertrain p = dieselVan();
    p.engine.particulateFilter = true;
    const float resistance = p.profile().filterResistance;
    CHECK(resistance > 0.05f);
    CHECK(resistance < 0.5f);
    with.compute(Exhaust::Stock, 1.0f, 0.002, 4, resistance);
    const double mid = with.lossDb(500.0, 2000.0) - without.lossDb(500.0, 2000.0);
    const double top = with.lossDb(2000.0, 8000.0) - without.lossDb(2000.0, 8000.0);
    CHECK(mid > 0.5);
    CHECK(mid < 6.0);
    CHECK(top > mid);
    // Sans filtre, la ligne ne change pas d'un bit.
    ExhaustLine again(44100.0f);
    const std::vector<float> plain = again.compute(Exhaust::Stock, 1.0f, 0.002, 4);
    CHECK(plain == without.compute(Exhaust::Stock, 1.0f, 0.002, 4, 0.0));
}

TEST_CASE("injection : au défaut, rien ne s'ajoute") {
    const EngineConfig e;
    CHECK_FALSE(directInjected(e));
    CHECK_FALSE(commonRailed(e));
    CHECK_FALSE(recirculated(e));
    const EngineProfile p = e.toProfile();
    CHECK_FALSE(p.directInjection);
    CHECK_FALSE(p.commonRail);
    CHECK_FALSE(p.egr);
    CHECK(p.filterResistance == 0.0f);
    const DrivingModel m(petrol2());
    CHECK_FALSE(m.filtered());
    CHECK_FALSE(m.portFilmActive());
    CHECK(m.coolantC() == kWarmCoolantC);
    // La fiche : de vrais chiffres là où ils ont un sens, rien ailleurs.
    const InjectionSummary none = injectionSummary(e);
    CHECK(none.coolingK == 0.0);
    CHECK(none.lowEndTorqueShare == 0.0);
    const InjectionSummary rail = injectionSummary(dieselVan(DieselInjection::CommonRail).engine);
    CHECK(rail.railBar > 1000.0);
    CHECK(rail.knockDb < -3.0);
    CHECK(rail.lowEndTorqueShare > 0.1);
}
