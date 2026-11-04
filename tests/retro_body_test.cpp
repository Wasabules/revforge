#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"
#include "revforge/sound_sheet.hpp"

using namespace revforge;

// La carrosserie des années 80 : voir « La carrosserie des années 80 » dans docs/modele.md.

namespace {

Powertrain retroCoupe() { return findPreset("retro_coupe")->powertrain(); }

/** Le même moteur et les mêmes réglages de voiture, dans une caisse de compacte : seule la carrosserie diffère. */
Powertrain sameInCompact() {
    Powertrain p = retroCoupe();
    p.car.body = Body::Compact;
    return p;
}

std::vector<float> rendered(const Powertrain& p) {
    EngineSound sound(44100);
    sound.setProfile(p.profile());
    sound.setTargetRpm(4000.0f);
    sound.setDrive(1.0f, 0.0f, false, false);
    std::vector<float> out(44100);
    sound.render(out.data(), static_cast<int>(out.size()));
    return out;
}

}  // namespace

TEST_CASE("carrosserie années 80 : ajoutée en fin de liste, le rang est envoyé par le site") {
    CHECK(static_cast<int>(Body::Van) == 4);
    CHECK(static_cast<int>(Body::Retro) == 5);
    // Elle a sa propre fiche : le `switch` n'est pas retombé sur la citadine.
    CHECK(&bodySpec(Body::Retro) != &bodySpec(Body::City));
}

TEST_CASE("carrosserie années 80 : des ordres de grandeur de l'époque") {
    const BodySpec& retro = bodySpec(Body::Retro);
    const BodySpec& compact = bodySpec(Body::Compact);
    // Cx de 0,38 à 0,42 sur 1,75 à 2 m².
    CHECK(retro.dragArea >= 0.38 * 1.75);
    CHECK(retro.dragArea <= 0.42 * 2.0);
    CHECK(retro.dragArea > compact.dragArea);
    CHECK(retro.gears == 5);
    CHECK(retro.wheelbaseM < compact.wheelbaseM);
    CHECK(retro.lengthM < compact.lengthM);
    CHECK(retro.widthM < compact.widthM);
    // Un coffre marqué : le porte-à-faux arrière est celui d'une trois-volumes.
    CHECK(retro.rearAxleM > compact.rearAxleM);
    CHECK(retro.naturalPlacement == EnginePlacement::Front);
}

TEST_CASE("le préréglage du coupé est bien monté, et sa boîte vient de la carrosserie") {
    const Preset* preset = findPreset("retro_coupe");
    REQUIRE(preset != nullptr);
    const Powertrain p = preset->powertrain();
    CHECK(p.car.body == Body::Retro);
    CHECK(p.car.massKg >= 1000);
    CHECK(p.car.massKg <= 1150);
    CHECK(p.car.rimInch == 14);
    CHECK(p.car.drive == Drive::Rear);
    CHECK(p.car.insulation == Insulation::Light);
    CHECK(gearingOf(p).gears == 5);
    CHECK(gearingOf(sameInCompact()).gears == bodySpec(Body::Compact).gears);
}

// Un 2,5 L en ligne de 171 ch, 1145 kg, Cx 0,35 à 0,38 : 8,3 à 10 s selon la boîte, 206 à 217 km/h (voir la doc). Rien n'est réglé pour y coller.
TEST_CASE("le coupé marche comme ses contemporains, à 30 % près") {
    const Powertrain p = retroCoupe();
    const double hp = powerSummary(p).peakPowerKw * kKwToHp;
    CHECK(hp > 171.0 * 0.7);
    CHECK(hp < 171.0 * 1.3);
    const Performance measured = measurePerformance(p);
    CHECK(measured.zeroTo100S > 8.3 * 0.7);
    CHECK(measured.zeroTo100S < 10.0 * 1.3);
    CHECK(measured.topSpeedKmh > 206.0 * 0.7);
    CHECK(measured.topSpeedKmh < 217.0 * 1.3);
}

TEST_CASE("à moteur et masse égaux, cette caisse freine plus dans l'air qu'une compacte, et plafonne plus bas") {
    Powertrain retro = retroCoupe();
    const Powertrain compact = sameInCompact();
    CHECK(roadLoadN(retro, 130.0 / 3.6) > roadLoadN(compact, 130.0 / 3.6));
    // Mêmes pertes au roulement : à l'arrêt, seul l'air diffère.
    CHECK(roadLoadN(retro, 1.0) - roadLoadN(compact, 1.0) < 0.1);
    CHECK(measurePerformance(retro).topSpeedKmh < measurePerformance(compact).topSpeedKmh);
}

TEST_CASE("la légèreté se paie moins cher que la traînée : plus vive qu'une compacte de série au même moteur") {
    Powertrain retro = retroCoupe();
    Powertrain compact = sameInCompact();
    compact.car.massKg = 1350;
    compact.car.rimInch = 18;
    compact.car.drive = Drive::Front;
    // À adhérence tenue : sans antipatinage, la propulsion légère patine au-delà du sommet du pneu (9,75 s contre 10,11), et
    // c'est l'adhérence qui décide, pas la masse ni la traînée.
    retro.engine.tractionControl = TractionControl::Sport;
    compact.engine.tractionControl = TractionControl::Sport;
    CHECK(measurePerformance(retro).zeroTo100S < measurePerformance(compact).zeroTo100S - 0.5);
}

TEST_CASE("les sources sont placées d'après sa géométrie") {
    const Powertrain p = retroCoupe();
    const CarAcoustics car = p.profile().car;
    const BodySpec& body = bodySpec(Body::Retro);
    CHECK(car.lengthM == body.lengthM);
    CHECK(car.heightM == body.heightM);
    CHECK(car.wheelbaseM == doctest::Approx(body.wheelbaseM));
    // La sortie derrière le pare-chocs, le moteur derrière le train avant, la tête sous le toit, entre les essieux.
    const float rearEnd = -body.lengthM / 2.0f;
    const float rearAxle = rearEnd + body.rearAxleM;
    const float frontAxle = rearAxle + static_cast<float>(body.wheelbaseM);
    CHECK(car.sources[static_cast<std::size_t>(Source::ExhaustA)].at.x < rearEnd);
    CHECK(car.engine.x < frontAxle);
    CHECK(car.engine.x > 0.0f);
    CHECK(car.seat.x > rearAxle);
    CHECK(car.seat.x < frontAxle);
    CHECK(car.seat.z < body.heightM);
    // Ligne un peu plus courte qu'une compacte : la voiture l'est.
    CHECK(p.profile().exhaustScale < bodySpec(Body::Compact).exhaustScale);
}

TEST_CASE("la caisse change le son, et le peu d'isolant s'entend dans l'habitacle") {
    Powertrain retro = retroCoupe();
    Powertrain compact = sameInCompact();
    // La même isolation : la ligne et le plan suffisent à changer le rendu.
    CHECK(rendered(retro) != rendered(compact));
    compact.car.insulation = Insulation::Standard;
    const SoundSheet lightly = soundSheet(retro);
    const SoundSheet standard = soundSheet(compact);
    CHECK(lightly.cabinIdleDb > standard.cabinIdleDb + 1.0f);
    CHECK(lightly.cabinFullDb > standard.cabinFullDb + 1.0f);
    // Dehors, l'isolant ne joue pas : l'écart reste celui de deux caisses proches.
    CHECK(std::abs(lightly.outsideFullDb - standard.outsideFullDb) < 3.0f);
}

TEST_CASE("le coupé roule en conduite automatique sur les cinq rapports") {
    DrivingModel model(retroCoupe());
    int top = 0;
    for (int i = 0; i < 60 * 20; ++i) {
        model.stepPedal(0.05, 1.0, 0.0);
        top = std::max(top, model.gear());
    }
    CHECK(top == 5);
    CHECK(model.speedKmh() > 150.0);
}
