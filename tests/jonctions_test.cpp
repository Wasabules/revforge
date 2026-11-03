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
