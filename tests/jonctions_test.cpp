// Là où deux réglages se rencontrent. Chaque réglage a ses tests ; ici, seulement ce qui n'existe que réuni.
// Voir « Quand les réglages se rencontrent » dans docs/modele.md.
#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/air_filter.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/turbo.hpp"

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
