// Là où deux réglages se rencontrent. Chaque réglage a ses tests ; ici, seulement ce qui n'existe que réuni.
// Voir « Quand les réglages se rencontrent » dans docs/modele.md.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "doctest.h"
#include "revforge/air_filter.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/turbo.hpp"
#include "test_support.hpp"

using namespace revforge;

namespace {

Powertrain charged(AirFilter filter, SuperchargerKind kind = SuperchargerKind::Roots) {
    Powertrain p = findPreset("v8_supercharged")->powertrain();
    p.engine.superchargerKind = kind;
    p.engine.airFilter = filter;
    return p;
}

}  // namespace

TEST_CASE("jonction filtre et compresseur : l'air passe par le filtre avant le compresseur") {
    const double rated = 0.88 * 6400.0;
    for (const SuperchargerKind kind : {SuperchargerKind::Roots, SuperchargerKind::TwinScrew, SuperchargerKind::Centrifugal}) {
        const Powertrain paper = charged(AirFilter::Stock, kind);
        const Powertrain cone = charged(AirFilter::Cone, kind);
        const Powertrain stacks = charged(AirFilter::Stacks, kind);
        const SuperchargerSpec paperSpec = superchargerSpecOf(paper);
        const SuperchargerSpec coneSpec = superchargerSpecOf(cone);
        // Le compresseur reste le même : seul change l'air qu'il reçoit.
        CHECK(coneSpec.rotorVolumeM3 == paperSpec.rotorVolumeM3);
        CHECK(coneSpec.driveRatio == paperSpec.driveRatio);
        CHECK(coneSpec.filterDrop == doctest::Approx(paperSpec.filterDrop * airFilterDropShare(AirFilter::Cone)));
        // Moins de perte devant lui, mais de l'air pris sous le capot : il sort plus chaud.
        CHECK(intakeDropPa(cone, rated) < 0.5 * intakeDropPa(paper, rated));
        CHECK(intakeDropPa(stacks, rated) < intakeDropPa(cone, rated));
        const double shape = breathingOf(paper, rated);
        const SuperchargerSteady coneHere = superchargerSteady(coneSpec, rated, shape);
        const SuperchargerSteady paperHere = superchargerSteady(paperSpec, rated, shape);
        CHECK(coneHere.coolK > paperHere.coolK + 1.0);
        CHECK(coneHere.outK > paperHere.outK + 5.0);
        // La densité perdue vient du calcul du compresseur. L'estimation du turbo, à même rapport de pression, doit la recouper.
        const double lost = superchargerAirShare(coneSpec, rated, shape, 1.0) / superchargerAirShare(paperSpec, rated, shape, 1.0);
        const double rise = intakeAirK(cone, rated) - intakeAirK(paper, rated);
        CHECK(rise > 5.0);
        CHECK(lost < 1.0);
        CHECK(lost == doctest::Approx(turboChargeDensityShare(static_cast<double>(cone.engine.boostBar), 1.0, rise, 1.0)).epsilon(0.01));
        CHECK(fullLoadTorqueNm(cone, rated, 1.0) < fullLoadTorqueNm(paper, rated, 1.0));
    }
    // Un panneau, c'est le même air, un peu moins de perte : un peu moins de travail à l'arbre, un peu plus de couple.
    for (const SuperchargerKind kind : {SuperchargerKind::Roots, SuperchargerKind::TwinScrew}) {
        const Powertrain paper = charged(AirFilter::Stock, kind);
        const Powertrain panel = charged(AirFilter::Panel, kind);
        CHECK(superchargerDriveNm(panel, rated, 1.0) < superchargerDriveNm(paper, rated, 1.0));
        CHECK(powerSummary(panel).peakPowerKw > powerSummary(paper).peakPowerKw);
    }
}

TEST_CASE("jonction filtre et compresseur : le papier d'origine ne change pas le compresseur") {
    const Powertrain paper = charged(AirFilter::Stock);
    const SuperchargerSpec spec = superchargerSpecOf(paper);
    CHECK(spec.inletFilter == AirFilter::Stock);
    const double ambient = superchargerSteady(spec, 5000.0, breathingOf(paper, 5000.0)).outK;
    SuperchargerSpec same = spec;
    same.inletPlacement = EnginePlacement::Mid;
    CHECK(superchargerSteady(same, 5000.0, breathingOf(paper, 5000.0)).outK == ambient);
}

namespace {

constexpr double kPi = 3.141592653589793;

Powertrain carbAndFilter(FuelSystem system, AirFilter filter) {
    Powertrain p = findPreset("hot_hatch_na")->powertrain();
    p.engine.fuelSystem = system;
    p.engine.airFilter = filter;
    return p;
}

test::Samples heardHeld(const Powertrain& p, float rpm, Listener where = Listener::Front) {
    EngineSound sound(test::kRate);
    sound.setProfile(p.profile());
    sound.setListener(where);
    sound.setTargetRpm(rpm);
    sound.setDrive(1.0f, 0.0f, false, false, 1.0f);
    test::Samples out(static_cast<std::size_t>(3.0f * test::kRate));
    sound.render(out.data(), static_cast<int>(out.size()));
    return test::slice(out, test::kRate, out.size());
}

/** Le niveau dans cette bande (dB), sur des fenêtres courtes. */
double bandDb(const test::Samples& x, double fromHz, double toHz) {
    constexpr std::size_t kWindow = 2048;
    double energy = 0.0;
    int windows = 0;
    for (std::size_t at = 0; at + kWindow <= x.size() && windows < 20; at += kWindow * 2, ++windows) {
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
    return 10.0 * std::log10(energy / std::max(windows, 1) + 1e-20);
}

/** Ce qu'ajoute ce carburateur à ce filtre, à l'avant de la voiture, à pleine charge (dB). */
double carbAddsDb(FuelSystem system, AirFilter filter, float rpm, double fromHz, double toHz) {
    return bandDb(heardHeld(carbAndFilter(system, filter), rpm), fromHz, toHz) - bandDb(heardHeld(carbAndFilter(FuelSystem::Injection, filter), rpm), fromHz, toHz);
}

}  // namespace

TEST_CASE("jonction carburateur et filtre : la boîte à air ne se retire qu'une fois") {
    // Un corps par cylindre et le papier d'origine : c'est le carburateur qui retire la boîte.
    const double onPaper = carbAddsDb(FuelSystem::MultiCarb, AirFilter::Stock, 5500.0f, 100.0, 800.0);
    MESSAGE("corps multiples, en dB dans les médiums : papier " << onPaper);
    CHECK(onPaper > 1.0);
    // Des cônes ou des cornets l'ont déjà retirée : les corps n'y ajoutent presque rien.
    for (const AirFilter filter : {AirFilter::Cone, AirFilter::Stacks}) {
        const double low = carbAddsDb(FuelSystem::MultiCarb, filter, 3000.0f, 100.0, 800.0);
        MESSAGE("à 3000 tr/min, sans boîte : " << low);
        CHECK(std::fabs(low) < 2.0);
    }
}

TEST_CASE("jonction carburateur et filtre : le souffle d'un col unique remonte le chemin de l'admission") {
    // Ce qui sort sans boîte est plus fort : c'est le filtre, pas un réglage du souffle.
    const double behindStacks = carbAddsDb(FuelSystem::Carburetor, AirFilter::Stacks, 5500.0f, 1000.0, 4000.0);
    const double behindPaper = carbAddsDb(FuelSystem::Carburetor, AirFilter::Stock, 5500.0f, 1000.0, 4000.0);
    MESSAGE("souffle, en dB : derrière le papier " << behindPaper << ", derrière des cornets " << behindStacks);
    CHECK(behindStacks > behindPaper + 0.5);
    CHECK(behindStacks < 6.0);
}

TEST_CASE("jonction carburateur et suralimentation : une seule perte, la même part") {
    const auto engine = [](Aspiration aspiration, FuelSystem system) {
        Powertrain p = findPreset("v8_supercharged")->powertrain();
        p.engine.aspiration = aspiration;
        p.engine.fuelSystem = system;
        return p;
    };
    for (const FuelSystem system : {FuelSystem::Carburetor, FuelSystem::MultiCarb}) {
        const double natural = torqueCurveNm(engine(Aspiration::Natural, system), 5000.0) / torqueCurveNm(engine(Aspiration::Natural, FuelSystem::Injection), 5000.0);
        for (const Aspiration aspiration : {Aspiration::Turbo, Aspiration::Supercharger}) {
            const double blown = torqueCurveNm(engine(aspiration, system), 5000.0) / torqueCurveNm(engine(aspiration, FuelSystem::Injection), 5000.0);
            MESSAGE("part du couple à 5000 tr/min : atmosphérique " << natural << ", suralimenté " << blown);
            CHECK(blown == doctest::Approx(natural).epsilon(0.015));
            // Comptée deux fois, elle tomberait au carré.
            CHECK(blown > natural * natural + 0.01);
        }
    }
    // Le compresseur est un moteur suralimenté comme un autre : même mélange de pleine charge qu'un turbo.
    CHECK(carburetorSpecOf(engine(Aspiration::Supercharger, FuelSystem::Carburetor).engine).fullLambda == carburetorSpecOf(engine(Aspiration::Turbo, FuelSystem::Carburetor).engine).fullLambda);
    CHECK(carburetorSpecOf(engine(Aspiration::Natural, FuelSystem::Carburetor).engine).fullLambda > carburetorSpecOf(engine(Aspiration::Turbo, FuelSystem::Carburetor).engine).fullLambda);
    // Soufflé, le col voit de l'air dense : même vitesse pour plus de masse, donc d'autant plus de film.
    const auto filmAt4000 = [](const Powertrain& p) {
        DrivingModel m(p);
        for (int i = 0; i < 4000 && m.rpm() < 4000.0; ++i) m.stepPedal(0.01, 1.0, 0.0);
        return m.carburetor().filmG();
    };
    const double ratio = filmAt4000(engine(Aspiration::Supercharger, FuelSystem::Carburetor)) / filmAt4000(engine(Aspiration::Natural, FuelSystem::Carburetor));
    MESSAGE("film soufflé / atmosphérique : " << ratio);
    CHECK(ratio > 1.3);
}

namespace {

double shaftRpmOf(const DrivingModel& model) { return model.turbo().state().shaftOmega * 60.0 / (2.0 * kPi); }

/** En quatrième, plein gaz le temps que la pression s'établisse. */
void pullInFourth(DrivingModel& model) {
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 6000 && (model.gear() < 4 || model.shifting()); ++i) {
        model.stepPedal(0.01, 0.6, 0.0);
        if (!model.shifting() && model.rpm() > 3500.0 && model.gear() < 4) model.requestShift(1);
    }
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 1.0, 0.0);
}

struct Lifted {
    double fuelGps = 0.0, surplusGps = 0.0, shaftAt3s = 0.0, hottest = 0.0, reprise = 30.0;
    bool active = false;
};

/** Trois secondes pied levé en quatrième, puis la reprise. */
Lifted liftOf(const Powertrain& p) {
    DrivingModel model(p);
    pullInFourth(model);
    Lifted out;
    int counted = 0;
    for (int i = 0; i < 300; ++i) {
        model.stepPedal(0.01, 0.0, 0.0);
        if (i < 30) continue;
        ++counted;
        out.fuelGps += model.consumption().fuelGps();
        out.surplusGps += model.turbo().antiLag().surplusFuel * 1000.0;
        out.hottest = std::max(out.hottest, model.turbo().state().turbineInK);
        out.active = model.antiLagActive();
    }
    out.fuelGps /= counted;
    out.surplusGps /= counted;
    out.shaftAt3s = shaftRpmOf(model);
    for (int i = 0; i < 1500; ++i) {
        model.stepPedal(0.01, 1.0, 0.0);
        if (model.boost() >= 0.9) {
            out.reprise = static_cast<double>(i) * 0.01;
            break;
        }
    }
    return out;
}

Powertrain antiLagged(AntiLag kind, FuelSystem system = FuelSystem::Injection, int pipeMm = 400) {
    Powertrain p = findPreset("hot_hatch_turbo")->powertrain();
    p.engine.antiLag = kind;
    p.engine.fuelSystem = system;
    p.engine.turboPipeMm = pipeMm;
    return p;
}

constexpr AntiLag kAntiLags[] = {AntiLag::Retard, AntiLag::AirInjection, AntiLag::CylinderCut};

}  // namespace

TEST_CASE("jonction carburateur et anti-lag : le surplus de carburant n'est payé qu'une fois") {
    const Lifted plainCarb = liftOf(antiLagged(AntiLag::Off, FuelSystem::Carburetor));
    CHECK_FALSE(plainCarb.active);
    for (const AntiLag kind : {AntiLag::Retard, AntiLag::AirInjection}) {
        const Lifted injected = liftOf(antiLagged(kind));
        const Lifted carb = liftOf(antiLagged(kind, FuelSystem::Carburetor));
        MESSAGE(static_cast<int>(kind) << " : carburant pied levé " << injected.fuelGps << " g/s en injection, " << carb.fuelGps << " sur carburateur ; surplus " << injected.surplusGps << " et " << carb.surplusGps);
        CHECK(carb.active);
        // Le mélange du carburateur (λ 0,93) est moins riche que ce que commande le calculateur : payé deux fois, il passerait devant.
        CHECK(carb.surplusGps < injected.surplusGps);
        CHECK(carb.fuelGps < injected.fuelGps);
        const double richness = 1.0 / carburetorSpecOf(antiLagged(kind, FuelSystem::Carburetor).engine).partLambda;
        CHECK(carb.fuelGps < (injected.fuelGps - injected.surplusGps) * richness);
        // Le circuit de ralenti reste un plancher : il ne s'ajoute pas à ce que brûle l'anti-lag.
        CHECK(carb.fuelGps > plainCarb.fuelGps);
    }
    // Sans dosage sur commande, l'injection d'air n'a presque rien à brûler dans le collecteur : c'est la cause, pas un malus.
    const Lifted airInjected = liftOf(antiLagged(AntiLag::AirInjection));
    const Lifted airCarb = liftOf(antiLagged(AntiLag::AirInjection, FuelSystem::Carburetor));
    CHECK(airCarb.surplusGps < 0.6 * airInjected.surplusGps);
    CHECK(airCarb.hottest < airInjected.hottest);
    // La coupure ne demande aucun surplus : elle marche pareil.
    CHECK(liftOf(antiLagged(AntiLag::CylinderCut, FuelSystem::Carburetor)).surplusGps == 0.0);
}

TEST_CASE("jonction carburateur et anti-lag : pied levé, le son reste celui de l'anti-lag") {
    const auto liftedSound = [](const Powertrain& p) {
        DrivingModel model(p);
        pullInFourth(model);
        for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.0, 0.0);
        EngineSound sound(test::kRate);
        sound.setProfile(p.profile());
        test::Samples out(static_cast<std::size_t>(test::kRate));
        constexpr int kSlice = 256;
        for (std::size_t at = 0; at + kSlice <= out.size(); at += kSlice) {
            model.stepPedal(static_cast<double>(kSlice) / test::kRate, 0.0, 0.0);
            sound.setTargetRpm(static_cast<float>(model.rpm()));
            sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
            sound.setTurbo(model.turboDrive());
            sound.render(out.data() + at, kSlice);
        }
        return bandDb(test::slice(out, out.size() / 2, out.size()), 80.0, 800.0);
    };
    // Le circuit de ralenti ne prend pas la place de l'anti-lag : ce n'est pas un moteur qui retient.
    const double carb = liftedSound(antiLagged(AntiLag::Retard, FuelSystem::Carburetor));
    const double injected = liftedSound(antiLagged(AntiLag::Retard));
    const double off = liftedSound(antiLagged(AntiLag::Off, FuelSystem::Carburetor));
    MESSAGE("médiums pied levé : retard sur carburateur " << carb << " dB, retard en injection " << injected << " dB, carburateur sans anti-lag " << off << " dB");
    CHECK(carb == doctest::Approx(injected).epsilon(0.05));
    CHECK(carb > off + 3.0);
}

TEST_CASE("jonction anti-lag et tube : la chaleur traverse la paroi, et l'anti-lag reste utile, moins sur un tube long") {
    const Lifted offShort = liftOf(antiLagged(AntiLag::Off, FuelSystem::Injection, 400));
    const Lifted offLong = liftOf(antiLagged(AntiLag::Off, FuelSystem::Injection, 3000));
    for (const AntiLag kind : kAntiLags) {
        const Lifted reference = liftOf(antiLagged(kind, FuelSystem::Injection, 400));
        const Lifted remote = liftOf(antiLagged(kind, FuelSystem::Injection, 3000));
        MESSAGE(static_cast<int>(kind) << " : arbre à 3 s " << reference.shaftAt3s << " puis " << remote.shaftAt3s << " tr/min (sans : " << offShort.shaftAt3s << ", " << offLong.shaftAt3s << ") ; reprise "
                                       << reference.reprise << " puis " << remote.reprise << " s (sans : " << offShort.reprise << ", " << offLong.reprise << ") ; T3 " << reference.hottest - 273.15 << " puis " << remote.hottest - 273.15 << " °C");
        // La paroi prend la chaleur de l'anti-lag comme une autre.
        CHECK(remote.hottest < reference.hottest - 200.0);
        // Moins efficace sur un tube long, mais toujours efficace.
        CHECK(remote.shaftAt3s < 0.85 * reference.shaftAt3s);
        CHECK(remote.shaftAt3s > 2.0 * offLong.shaftAt3s);
        CHECK(remote.reprise > reference.reprise);
        CHECK(remote.reprise < 0.8 * offLong.reprise);
    }
}

TEST_CASE("jonction anti-lag et tube : ce qui arrive imbrûlé s'enflamme au collecteur, pas à la turbine") {
    DrivingModel model(antiLagged(AntiLag::CylinderCut, FuelSystem::Injection, 3000));
    pullInFourth(model);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.0, 0.0);
    REQUIRE(model.antiLagActive());
    // Le gaz arrive froid à la turbine, mais le collecteur, lui, est encore chaud.
    CHECK(model.turbo().state().turbineInK < 800.0);
    CHECK(model.turbo().collectorK() > 800.0);
    CHECK(model.turbo().antiLag().collectorHeatW > 1000.0);
}

TEST_CASE("le tube court ne fait pas fondre le gaz d'un moteur entraîné") {
    DrivingModel model(antiLagged(AntiLag::Off, FuelSystem::Injection, 150));
    pullInFourth(model);
    double hottest = 0.0;
    for (int i = 0; i < 300; ++i) {
        model.stepPedal(0.01, 0.0, 0.0);
        hottest = std::max(hottest, model.turbo().state().turbineInK);
    }
    CHECK(hottest < 1200.0);
}

namespace {

Powertrain rotaryWith(Aspiration aspiration = Aspiration::Natural, FuelSystem system = FuelSystem::Injection, AirFilter filter = AirFilter::Stock) {
    Powertrain p = findPreset("twin_rotor_13")->powertrain();
    p.engine.aspiration = aspiration;
    p.engine.boostBar = 0.8f;
    p.engine.fuelSystem = system;
    p.engine.airFilter = filter;
    return p;
}

/** Le moteur à pistons qui avale le même volume en deux tours, au même régime : le rotatif doit respirer comme lui. */
Powertrain pistonOfSameSwept(const Powertrain& rotary) {
    Powertrain p = rotary;
    p.engine.layout = Layout::Inline;
    p.engine.displacementCc = 2 * rotary.engine.displacementCc;
    // Très super-carré : à ce régime, un piston plus long y perdrait son souffle.
    p.engine.boreMm = 110;
    p.engine.cylinders = 4;
    return p;
}

}  // namespace

TEST_CASE("jonction rotatif : course, alésage et arbre à cames n'y existent pas") {
    Powertrain plain = rotaryWith();
    Powertrain other = plain;
    other.engine.boreMm = 70;
    other.engine.camshaft = Camshaft::Sohc;
    CHECK(other.engine.valvetrain() == Camshaft::Dohc);
    CHECK(other.profile().camshaft == Camshaft::Dohc);
    CHECK(other.engine.strokeMm() == plain.engine.strokeMm());
    CHECK(static_cast<double>(other.engine.strokeMm()) / 1000.0 == doctest::Approx(static_cast<double>(other.profile().strokeM)).epsilon(1e-5));
    for (const double rpm : {2000.0, 5000.0, 8500.0}) {
        CHECK(torqueCurveNm(other, rpm) == torqueCurveNm(plain, rpm));
        CHECK(frictionTorqueNm(other, rpm) == frictionTorqueNm(plain, rpm));
    }
    CHECK(engineInertia(other) == engineInertia(plain));
    // Le son, au bit près.
    const test::Samples a = heardHeld(plain, 5000.0f);
    const test::Samples b = heardHeld(other, 5000.0f);
    REQUIRE(a.size() == b.size());
    bool same = true;
    for (std::size_t i = 0; i < a.size(); ++i) same = same && a[i] == b[i];
    CHECK(same);
    // Les cornets non plus ne lisent pas un alésage qui n'existe pas.
    plain.engine.airFilter = AirFilter::Stacks;
    other.engine.airFilter = AirFilter::Stacks;
    CHECK(other.profile().stackRadiusM == plain.profile().stackRadiusM);
    CHECK(torqueCurveNm(other, 5000.0) == torqueCurveNm(plain, 5000.0));
}

TEST_CASE("jonction rotatif : tout débit passe par le volume balayé en deux tours") {
    const Powertrain charged = rotaryWith(Aspiration::Supercharger);
    const SuperchargerSpec chargedSpec = superchargerSpecOf(charged);
    CHECK(chargedSpec.displacementM3 == doctest::Approx(2.0 * static_cast<double>(charged.engine.displacementCc) * 1e-6));
    const SuperchargerSpec pistonSpec = superchargerSpecOf(pistonOfSameSwept(charged));
    MESSAGE("compresseur : " << chargedSpec.ratedFlow << " kg/s sur le rotatif, " << pistonSpec.ratedFlow << " sur le piston ; rotor " << chargedSpec.rotorVolumeM3 << " et " << pistonSpec.rotorVolumeM3);
    CHECK(chargedSpec.ratedFlow / pistonSpec.ratedFlow > 0.9);
    CHECK(chargedSpec.ratedFlow / pistonSpec.ratedFlow < 1.1);
    CHECK(chargedSpec.rotorVolumeM3 / pistonSpec.rotorVolumeM3 > 0.9);
    CHECK(chargedSpec.rotorVolumeM3 / pistonSpec.rotorVolumeM3 < 1.1);

    const Powertrain turbo = rotaryWith(Aspiration::Turbo);
    CHECK(turboSpecOf(turbo).displacementM3 == doctest::Approx(chargedSpec.displacementM3));
    Powertrain piped = turbo;
    piped.engine.turboPipeMm = 2000;
    CHECK(turboSpecOf(piped).pipe.cylinders == 4);
    CHECK(turboSpecOf(piped).pipe.volumeM3 == doctest::Approx(turboSpecOf(pistonOfSameSwept(piped)).pipe.volumeM3));

    // Le carburateur : le col d'un corps se dimensionne sur ce que ce corps avale, deux chambres en deux tours.
    const CarburetorSpec multi = carburetorSpecOf(rotaryWith(Aspiration::Natural, FuelSystem::MultiCarb).engine);
    Powertrain twoCylinders = pistonOfSameSwept(rotaryWith());
    twoCylinders.engine.cylinders = 2;
    twoCylinders.engine.fuelSystem = FuelSystem::MultiCarb;
    CHECK(multi.throats == 2);
    CHECK(multi.sharing == 2);
    CHECK(multi.throatM == doctest::Approx(carburetorSpecOf(twoCylinders.engine).throatM));
    const CarburetorSpec single = carburetorSpecOf(rotaryWith(Aspiration::Natural, FuelSystem::Carburetor).engine);
    CHECK(single.sharing == 4);
    const Powertrain singleCarb = rotaryWith(Aspiration::Natural, FuelSystem::Carburetor);
    CHECK(torqueCurveNm(singleCarb, 7500.0) < torqueCurveNm(rotaryWith(), 7500.0));
    CHECK(torqueCurveNm(singleCarb, 7500.0) > 0.8 * torqueCurveNm(rotaryWith(), 7500.0));

    // Le filtre : la perte suit ce débit-là, pas la moitié.
    const Powertrain paper = rotaryWith();
    CHECK(intakeDropPa(paper, 7500.0) / intakeDropPa(pistonOfSameSwept(paper), 7500.0) > 0.8);
    CHECK(intakeDropPa(paper, 7500.0) / intakeDropPa(pistonOfSameSwept(paper), 7500.0) < 1.2);
    const Powertrain chargedCone = rotaryWith(Aspiration::Supercharger, FuelSystem::Injection, AirFilter::Cone);
    CHECK(intakeDropPa(chargedCone, 7500.0) < 0.6 * intakeDropPa(charged, 7500.0));
    CHECK(intakeDropPa(charged, 7500.0) > 1500.0);
}

TEST_CASE("jonction rotatif : le motif de coupure reste un motif sur deux rotors") {
    // Cinq est impair : sur deux rotors, le motif passe de l'un à l'autre, comme d'un cylindre à l'autre sur un 4 ou un 6.
    int fired[2] = {0, 0};
    for (std::uint32_t event = 0; event < 40; ++event) {
        if (antiLagFires(event)) ++fired[event % 2];
    }
    CHECK(fired[0] == 8);
    CHECK(fired[1] == 8);

    Powertrain p = rotaryWith(Aspiration::Turbo);
    p.engine.antiLag = AntiLag::CylinderCut;
    DrivingModel model(p);
    pullInFourth(model);
    const double before = shaftRpmOf(model);
    for (int i = 0; i < 200; ++i) model.stepPedal(0.01, 0.0, 0.0);
    CHECK(model.antiLagActive());
    Powertrain plain = p;
    plain.engine.antiLag = AntiLag::Off;
    DrivingModel without(plain);
    pullInFourth(without);
    for (int i = 0; i < 200; ++i) without.stepPedal(0.01, 0.0, 0.0);
    MESSAGE("birotor turbo, arbre : " << before << " en pleine charge, " << shaftRpmOf(model) << " après 2 s en coupure, " << shaftRpmOf(without) << " sans");
    CHECK(shaftRpmOf(model) > 1.5 * shaftRpmOf(without));

    // À 3600 tr/min, 120 explosions par seconde, comme un 4 cylindres : un motif de 5 fait 24 Hz, et ses raies à 72 et 96 Hz.
    const auto rhythm = [&](AntiLag kind) {
        EngineSound sound(test::kRate);
        sound.setProfile(p.profile());
        TurboDrive d = model.turboDrive();
        d.antiLag.kind = kind;
        d.antiLag.active = true;
        d.antiLag.heatW = 0.0f;
        d.antiLag.heatShare = 0.0f;
        d.antiLag.ignition = 0.0f;
        test::Samples out(256 * 512);
        for (std::size_t at = 0; at < out.size(); at += 256) {
            sound.setTargetRpm(3600.0f);
            sound.setDrive(0.35f, 0.3f, false, false, 0.0f);
            sound.setTurbo(d);
            sound.render(out.data() + at, 256);
        }
        return test::slice(out, out.size() / 2, out.size());
    };
    const test::Samples round = rhythm(AntiLag::Retard);
    const test::Samples chopped = rhythm(AntiLag::CylinderCut);
    const double gained = std::max(bandDb(chopped, 66.0, 78.0) - bandDb(round, 66.0, 78.0), bandDb(chopped, 90.0, 102.0) - bandDb(round, 90.0, 102.0));
    MESSAGE("raies du motif, en plus du moteur rond : " << gained << " dB");
    CHECK(gained > 3.0);
}

TEST_CASE("toutes les rencontres tournent, sans NaN ni calage") {
    const std::vector<std::pair<Layout, int>> engines = {{Layout::Inline, 4}, {Layout::Boxer, 4}, {Layout::CrossPlane, 8}, {Layout::Rotary, 2}};
    // Un tirage déterministe sur le produit des réglages : le tout prendrait des heures.
    std::uint32_t seed = 12345;
    const auto pickOf = [&seed](std::uint32_t count) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 16) % count;
    };
    for (int round = 0; round < 60; ++round) {
        Powertrain p = findPreset("hot_hatch_turbo")->powertrain();
        const auto& engine = engines[pickOf(4)];
        p.engine.layout = engine.first;
        p.engine.cylinders = engine.second;
        p.engine.displacementCc = engine.first == Layout::Rotary ? 1308 : (engine.second == 8 ? 5000 : 2000);
        p.engine.boreMm = engine.second == 8 ? 92 : 84;
        p.engine.aspiration = static_cast<Aspiration>(pickOf(3));
        p.engine.superchargerKind = static_cast<SuperchargerKind>(pickOf(3));
        p.engine.airFilter = static_cast<AirFilter>(pickOf(4));
        p.engine.fuelSystem = static_cast<FuelSystem>(pickOf(3));
        p.engine.camshaft = static_cast<Camshaft>(pickOf(2));
        p.engine.antiLag = static_cast<AntiLag>(pickOf(4));
        p.engine.turboPipeMm = pickOf(2) ? 400 : 3000;
        CAPTURE(round);
        DrivingModel model(p);
        for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 1.0, 0.0);
        const double spinning = model.rpm();
        for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.0, 0.0);
        CHECK(std::isfinite(model.rpm()));
        CHECK(std::isfinite(model.torqueNm()));
        CHECK(std::isfinite(model.consumption().fuelTripL()));
        CHECK(spinning > 1000.0);
        CHECK_FALSE(model.stalled());
        EngineSound sound(test::kRate);
        sound.setProfile(p.profile());
        test::Samples out(2048);
        sound.setTargetRpm(static_cast<float>(spinning));
        sound.setDrive(1.0f, 0.5f, false, false, 1.0f);
        if (p.engine.aspiration == Aspiration::Turbo) sound.setTurbo(model.turboDrive());
        sound.render(out.data(), static_cast<int>(out.size()));
        CHECK(std::isfinite(test::rms(out)));
    }
}
