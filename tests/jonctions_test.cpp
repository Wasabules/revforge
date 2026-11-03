// Là où deux réglages se rencontrent. Chaque réglage a ses tests ; ici, seulement ce qui n'existe que réuni.
// Voir « Quand les réglages se rencontrent » dans docs/modele.md.
#include <algorithm>
#include <cmath>
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

TEST_CASE("jonction carburateur et anti-lag : le mélange qui brûle encore ne réécrit pas la pression d'ouverture") {
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
    // Les deux gardent la chaleur du retard : même pression à l'ouverture, à ce qu'apporte le souffle près.
    const double carb = liftedSound(antiLagged(AntiLag::Retard, FuelSystem::Carburetor));
    const double injected = liftedSound(antiLagged(AntiLag::Retard));
    const double off = liftedSound(antiLagged(AntiLag::Off, FuelSystem::Carburetor));
    MESSAGE("médiums pied levé : retard sur carburateur " << carb << " dB, retard en injection " << injected << " dB, carburateur sans anti-lag " << off << " dB");
    CHECK(carb == doctest::Approx(injected).epsilon(0.05));
    CHECK(carb > off + 3.0);
}
