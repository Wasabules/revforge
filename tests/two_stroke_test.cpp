#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/consumption.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/two_stroke.hpp"

using namespace revforge;

namespace {

constexpr double kPi = 3.141592653589793;

/** Le trois cylindres de la Saab 96 : 841 cm³, 70 × 73 mm. */
EngineConfig triple(Exhaust exhaust = Exhaust::Stock, int redline = 5000) {
    EngineConfig e;
    e.cylinders = 3;
    e.displacementCc = 841;
    e.boreMm = 70;
    e.redlineRpm = redline;
    e.exhaust = exhaust;
    e.fuelSystem = FuelSystem::Carburetor;
    e.cycle = EngineCycle::TwoStroke;
    return e;
}

Powertrain saab(const EngineConfig& e) {
    Powertrain p;
    p.engine = e;
    p.car.body = Body::Retro;
    p.car.massKg = 813;
    p.car.rimInch = 15;
    p.car.drive = Drive::Front;
    return p;
}

/** Puissance moyenne d'un rendu à régime et charge tenus, à cette fréquence (Hz) : le plus fort de cinq voisines. */
double powerAt(const std::vector<float>& pcm, int rate, double hz) {
    const int window = rate / 2;
    double total = 0.0;
    for (int start = rate; start + window <= static_cast<int>(pcm.size()); start += window / 2) {
        double best = 0.0;
        for (int offset = -2; offset <= 2; ++offset) {
            double re = 0.0, im = 0.0;
            const double omega = 2.0 * kPi * hz * (1.0 + 0.006 * offset) / rate;
            for (int n = 0; n < window; n += 2) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * n / window);
                re += w * static_cast<double>(pcm[static_cast<std::size_t>(start + n)]) * std::cos(omega * n);
                im += w * static_cast<double>(pcm[static_cast<std::size_t>(start + n)]) * std::sin(omega * n);
            }
            best = std::max(best, re * re + im * im);
        }
        total += best;
    }
    return total;
}

struct Held {
    std::vector<float> pcm;
    float misfires = 0.0f;
};

Held hold(const EngineProfile& profile, float rpm, float load, int seconds = 5) {
    constexpr int kRate = 44100;
    EngineSound sound(kRate);
    sound.setProfile(profile);
    sound.setVolume(0.7f);
    sound.setTargetRpm(rpm);
    Held out;
    out.pcm.assign(static_cast<std::size_t>(kRate) * seconds, 0.0f);
    for (std::size_t i = 0; i < out.pcm.size(); i += 128) {
        sound.setDrive(load, 0.0f, false, false, load);
        sound.render(out.pcm.data() + i, static_cast<int>(std::min<std::size_t>(128, out.pcm.size() - i)));
    }
    out.misfires = sound.misfireShare();
    return out;
}

}  // namespace

TEST_CASE("deux-temps : les lumieres se calent par leur hauteur, par la bielle et la manivelle") {
    const TwoStrokePorts ports = twoStrokePortsOf(triple());
    // La lumière s'ouvre quand le haut du piston a parcouru la course moins sa hauteur.
    CHECK(pistonTravelShare(ports.exhaustOpenDeg, ports.rodOverStroke) == doctest::Approx(1.0 - ports.exhaustHeight).epsilon(1e-6));
    CHECK(pistonTravelShare(ports.transferOpenDeg, ports.rodOverStroke) == doctest::Approx(1.0 - ports.transferHeight).epsilon(1e-6));
    // Symétrique autour du point mort bas : la bielle et la manivelle repassent par la même course.
    CHECK(pistonTravelShare(360.0 - ports.exhaustOpenDeg, ports.rodOverStroke) == doctest::Approx(1.0 - ports.exhaustHeight).epsilon(1e-6));
    // L'échappement avant le transfert : la bouffée vide le cylindre avant que le carter ne pousse.
    CHECK(ports.exhaustOpenDeg < ports.transferOpenDeg);
    // Un moteur de tourisme : de 140 à 150° d'échappement, de 105 à 120° de transfert.
    CHECK(360.0 - 2.0 * ports.exhaustOpenDeg == doctest::Approx(145.6).epsilon(0.02));
    CHECK(360.0 - 2.0 * ports.transferOpenDeg == doctest::Approx(112.0).epsilon(0.03));
    // Le cylindre ne comprime que ce qu'enferme la lumière : 1 + (1 - h)(r - 1).
    CHECK(ports.trappedCompression == doctest::Approx(1.0 + (1.0 - ports.exhaustHeight) * 6.5).epsilon(1e-9));
    // Un moteur qui tourne vite a des lumières plus hautes, ouvertes plus tôt.
    CHECK(twoStrokePortsOf(triple(Exhaust::Stock, 9500)).exhaustOpenDeg < ports.exhaustOpenDeg - 10.0);
}

TEST_CASE("deux-temps : une explosion par tour et par cylindre, deux fois le volume balaye en deux tours") {
    const EngineConfig e = triple();
    CHECK(e.firingsPerTwoTurns() == 6);
    CHECK(e.sweptCcPerTwoTurns() == doctest::Approx(1682.0));
    CHECK(e.valvetrain() == Camshaft::Dohc);
    CHECK(!e.burnsDiesel());
    const EngineProfile p = e.toProfile();
    CHECK(p.twoStroke);
    CHECK(p.pistons == 3);
    CHECK(p.firingCount == 6);
    // L'explosion i et i + 3 sont celles du même cylindre, un tour plus tard : à 360° l'une de l'autre.
    for (int i = 0; i < 3; ++i) {
        CHECK(p.firing[static_cast<std::size_t>(i + 3)] - p.firing[static_cast<std::size_t>(i)] == doctest::Approx(0.5f));
        CHECK(p.headerM[static_cast<std::size_t>(i + 3)] == p.headerM[static_cast<std::size_t>(i)]);
    }
    // Le même moteur en quatre temps n'en sait rien.
    EngineConfig four = e;
    four.cycle = EngineCycle::FourStroke;
    CHECK(four.firingsPerTwoTurns() == 3);
    CHECK(!four.toProfile().twoStroke);
}

TEST_CASE("deux-temps : le balayage de Benson et Brandham") {
    constexpr double x = kScavengeDisplacement;
    constexpr double y = kScavengeShortCircuit;
    // Déplacement parfait : tout ce qui entre (moins le court-circuit) reste.
    CHECK(scavengingEfficiency(0.3, x, y) == doctest::Approx(0.3 * (1.0 - y)));
    // Puis mélange parfait : dSE/dΛ' = 1 - SE, continu au passage.
    const double edge = x / (1.0 - y);
    CHECK(scavengingEfficiency(edge, x, y) == doctest::Approx(x));
    CHECK(scavengingEfficiency(edge + 1e-6, x, y) == doctest::Approx(x).epsilon(1e-5));
    CHECK(scavengingEfficiency(1.0, x, y) == doctest::Approx(1.0 - (1.0 - x) * std::exp(x - (1.0 - y))));
    // Jamais plus que tout, de plus en plus cher : le piégeage baisse avec le débit.
    CHECK(scavengingEfficiency(5.0, x, y) < 1.0);
    CHECK(scavengingEfficiency(1.2, x, y) / 1.2 < scavengingEfficiency(0.6, x, y) / 0.6);
}

TEST_CASE("deux-temps : le cycle integre donne des rendements de moteur reel") {
    const EngineConfig e = triple();
    for (double rpm : {1500.0, 3000.0, 4500.0}) {
        const TwoStrokeCharge c = twoStrokeFullLoad(e, rpm);
        INFO("rpm " << rpm << " DR " << c.delivery << " TE " << c.trapping << " SE " << c.scavenging << " CE " << c.charging);
        // Un carter de tourisme : de 0,4 à 0,9 de rapport de débit ; un piégeage de 0,5 à 0,8 (Heywood, Blair).
        CHECK(c.delivery > 0.4);
        CHECK(c.delivery < 0.95);
        CHECK(c.trapping > 0.45);
        CHECK(c.trapping < 0.85);
        CHECK(c.scavenging > 0.6);
        CHECK(c.scavenging < 0.95);
        // La bouffée s'ouvre sur 2,5 à 5 bar.
        CHECK(c.openPa > 2.5e5);
        CHECK(c.openPa < 5.0e5);
    }
    // La table et le calcul direct disent la même chose, à la continuité près d'un régime au suivant.
    const double rpm = twoStrokeTableRpm(e, 7);
    CHECK(twoStrokeCycle(e, rpm).charging == doctest::Approx(twoStrokeFullLoad(e, rpm).charging).epsilon(0.08));
}

TEST_CASE("deux-temps : la chambre de detente s'accorde par la vitesse du son du gaz chaud") {
    const ExpansionChamber c = expansionChamberFor(triple(Exhaust::Expansion));
    CHECK(c.soundMs == doctest::Approx(std::sqrt(1.35 * 287.0 * 650.0)).epsilon(1e-6));
    CHECK(c.tunedRpm == doctest::Approx(0.88 * 5000.0));
    // Le ventre plus large que le collecteur, la pointe plus étroite : un diffuseur, puis un cône de retour.
    CHECK(c.bellyDiameterM > 2.5f * c.headerDiameterM);
    CHECK(c.stingerDiameterM < c.headerDiameterM);
    // L'aller et retour jusqu'au milieu du cône de retour, au régime visé, dure le trajet du transfert à la mi-fermeture.
    const TwoStrokePorts ports = twoStrokePortsOf(triple(Exhaust::Expansion));
    const double halfOpen = 360.0 - crankAngleForTravel(1.0 - 0.5 * ports.exhaustHeight, ports.rodOverStroke);
    const double tuned = static_cast<double>(c.headerM + c.diffuserM + c.bellyM + c.baffleM / 2.0f);
    CHECK(2.0 * tuned / c.soundMs == doctest::Approx((halfOpen - ports.transferOpenDeg) / (6.0 * c.tunedRpm)).epsilon(1e-4));
    // Un moteur qui tourne plus vite : une chambre plus courte.
    CHECK(expansionChamberFor(triple(Exhaust::Expansion, 7000)).diffuserM < c.diffuserM);
}

TEST_CASE("deux-temps : la chambre de detente donne une bande de puissance la ou elle est accordee") {
    // Son onde de retour fait entrer plus d'air dans le carter et repousse le mélange frais dans le cylindre : le remplissage monte
    // autour du régime visé. Contre-épreuve : accordée plus haut, sa bande monte avec elle.
    const auto band = [](int redline) {
        const EngineConfig stock = triple(Exhaust::Stock, redline);
        const EngineConfig tuned = triple(Exhaust::Expansion, redline);
        double best = 0.0, at = 0.0;
        for (int k = 0; k < kTwoStrokePoints; ++k) {
            const double rpm = twoStrokeTableRpm(tuned, k);
            const double gain = twoStrokeFullLoad(tuned, rpm).charging - twoStrokeFullLoad(stock, rpm).charging;
            if (gain > best) {
                best = gain;
                at = rpm;
            }
        }
        return std::pair<double, double>{best, at};
    };
    const auto [gain, at] = band(5000);
    INFO("gain " << gain << " at " << at);
    CHECK(gain > 0.05);
    CHECK(at > 0.6 * 4400.0);
    CHECK(at < 1.25 * 4400.0);
    // Le sommet du remplissage avec la chambre, accordée sur 0,88 fois le régime maximal : plus haut sur un moteur qui tourne plus vite.
    const auto summit = [](int redline) {
        const EngineConfig tuned = triple(Exhaust::Expansion, redline);
        double best = 0.0, at = 0.0;
        for (int k = 0; k < kTwoStrokePoints; ++k) {
            const double rpm = twoStrokeTableRpm(tuned, k);
            if (rpm < 0.4 * redline) continue;
            const double charging = twoStrokeFullLoad(tuned, rpm).charging;
            if (charging > best) {
                best = charging;
                at = rpm;
            }
        }
        return at;
    };
    INFO("sommet " << summit(5000) << " puis " << summit(7000));
    CHECK(summit(7000) > summit(5000) + 500.0);
    // Au régime visé, la chambre donne plus de couple que la ligne d'origine.
    CHECK(torqueCurveNm(saab(triple(Exhaust::Expansion)), 4400.0) > 1.1 * torqueCurveNm(saab(triple()), 4400.0));
}

TEST_CASE("deux-temps : les rates a faible charge, en moyenne") {
    // Pleine charge : la charge est pure, tout part.
    const TwoStrokeFiring full = twoStrokeFiring(0.75);
    CHECK(full.firing == doctest::Approx(1.0));
    CHECK(full.burnt == doctest::Approx(1.0));
    // Faible charge : une explosion sur deux, le « quatre-temps ».
    const TwoStrokeFiring light = twoStrokeFiring(0.27);
    CHECK(light.firing > 0.45);
    CHECK(light.firing < 0.75);
    // Au ralenti : un tour sur deux ou trois, le « ring-ding ».
    const TwoStrokeFiring idle = twoStrokeFiring(0.15);
    CHECK(idle.firing > 0.25);
    CHECK(idle.firing < 0.5);
    CHECK(idle.burnt < 1.0);
    // Ce qui brûle ne dépasse jamais ce qui est enfermé.
    for (double se = 0.05; se < 1.0; se += 0.05) CHECK(twoStrokeFiring(se).burnt <= 1.0);
}

TEST_CASE("deux-temps : le ralenti rate un tour sur deux ou trois, pas la pleine charge") {
    const EngineProfile profile = triple().toProfile();
    const Held idle = hold(profile, 830.0f, 0.15f);
    const Held full = hold(profile, 3000.0f, 1.0f);
    INFO("ralenti " << idle.misfires << ", pleine charge " << full.misfires);
    CHECK(idle.misfires > 0.4f);
    CHECK(idle.misfires < 0.8f);
    CHECK(full.misfires < 0.02f);
    // Les ratés font des sous-ordres : à 830 tr/min, une explosion tous les tiers de tour (41,5 Hz), et entre deux, des raies que la
    // pleine charge n'a pas. Contre-épreuve : sans ratés (charge 0,6), la raie de demi-cadence retombe.
    const Held clean = hold(profile, 830.0f, 0.6f);
    const double firing = 830.0 / 60.0 * 3.0;
    const double halfIdle = powerAt(idle.pcm, 44100, firing / 2.0) / powerAt(idle.pcm, 44100, firing);
    const double halfClean = powerAt(clean.pcm, 44100, firing / 2.0) / powerAt(clean.pcm, 44100, firing);
    INFO("demi-cadence : ralenti " << 10.0 * std::log10(halfIdle) << " dB, sans ratés " << 10.0 * std::log10(halfClean) << " dB");
    CHECK(halfIdle > 3.0 * halfClean);
}

TEST_CASE("deux-temps : une octave au-dessus du quatre-temps de meme cylindree") {
    // Deux fois plus d'explosions : la cadence d'un trois cylindres deux-temps est celle d'un six cylindres quatre-temps.
    const EngineConfig two = triple();
    EngineConfig four = two;
    four.cycle = EngineCycle::FourStroke;
    const Held a = hold(two.toProfile(), 3000.0f, 1.0f, 4);
    const Held b = hold(four.toProfile(), 3000.0f, 1.0f, 4);
    const double fourFiring = 3000.0 / 60.0 * 1.5;
    CHECK(powerAt(a.pcm, 44100, 2.0 * fourFiring) > 10.0 * powerAt(a.pcm, 44100, fourFiring));
    CHECK(powerAt(b.pcm, 44100, fourFiring) > 0.1 * powerAt(b.pcm, 44100, 2.0 * fourFiring));
}

TEST_CASE("deux-temps : couple et puissance face a la Saab 96") {
    const Powertrain p = saab(triple());
    const PowerSummary s = powerSummary(p);
    INFO("couple " << s.peakTorqueNm << " Nm à " << s.peakTorqueRpm << ", puissance " << s.peakPowerKw << " kW à " << s.peakPowerRpm);
    // Saab 96 (1960-1966, musée Saab) : 79 Nm à 3000 tr/min, 38 ch à 4250. Le modèle lit plus bas : l'écart reste dans 35 %.
    CHECK(s.peakTorqueNm > 0.65 * 79.0);
    CHECK(s.peakTorqueNm < 1.2 * 79.0);
    CHECK(s.peakTorqueRpm > 2000.0);
    CHECK(s.peakTorqueRpm < 4000.0);
    CHECK(s.peakPowerKw > 0.65 * 28.0);
    CHECK(s.peakPowerRpm < 5000.0);
    // Les frottements d'un tour : sans distribution, moins qu'un quatre-temps de même cylindrée.
    Powertrain four = p;
    four.engine.cycle = EngineCycle::FourStroke;
    CHECK(frictionTorqueNm(p, 3000.0) < frictionTorqueNm(four, 3000.0));
}

TEST_CASE("deux-temps : l'huile part avec l'essence, et l'essence perdue par court-circuit se paie") {
    const Powertrain p = saab(triple());
    ConsumptionModel model;
    model.configure(p);
    CHECK(model.twoStroke());
    // 1 volume d'huile pour 50 d'essence.
    CHECK(model.premixOilGps(1.0) == doctest::Approx(1.0 / 50.0 * 0.85 / 0.745));
    // Pour le même couple, le carburateur et l'injection directe : celle-ci n'en perd pas par court-circuit.
    Powertrain direct = p;
    direct.engine.fuelSystem = FuelSystem::Direct;
    ConsumptionModel injected;
    injected.configure(direct);
    const double friction = frictionTorqueNm(direct, 3000.0);
    const double full = fullLoadTorqueNm(direct, 3000.0, 0.0) + friction;
    const double withPort = model.fuelGps(3000.0, 30.0, friction, full);
    const double withDirect = injected.fuelGps(3000.0, 30.0, friction, full);
    CHECK(withPort > 1.15 * withDirect);
    // Sur un trajet : l'huile suit l'essence, au mélange près.
    DrivingModel car(p);
    car.crank();
    for (int i = 0; i < 3000; ++i) car.stepPedal(0.001, 0.0, 0.0);
    for (int i = 0; i < 20000; ++i) car.stepPedal(0.001, 0.5, 0.0);
    const ConsumptionMeter& meter = car.consumption();
    CHECK(meter.oilTripMl() > 0.0);
    CHECK(meter.oilTripMl() / 1000.0 == doctest::Approx(meter.fuelTripL() / 50.0).epsilon(1e-6));
}

TEST_CASE("deux-temps : ce qui n'existe pas revient a ce qui existe") {
    EngineConfig e = triple();
    e.fuel = Fuel::Diesel;
    e.aspiration = Aspiration::Turbo;
    e.cylinders = 8;
    e.layout = Layout::CrossPlane;
    CHECK(coherentTwoStroke(e));
    CHECK(e.fuel == Fuel::Petrol);
    CHECK(e.aspiration == Aspiration::Natural);
    CHECK(e.cylinders == kMaxCylinders / 2);
    CHECK(e.layout == Layout::V);
    CHECK(!coherentTwoStroke(e));
    // Un quatre-temps n'est jamais touché.
    EngineConfig four;
    four.aspiration = Aspiration::Turbo;
    four.fuel = Fuel::Diesel;
    CHECK(!coherentTwoStroke(four));
}

TEST_CASE("deux-temps : le demarreur ne comprime que ce que la lumiere enferme, a chaque tour") {
    DrivingModel model(saab(triple()));
    const EngineProfile profile = saab(triple()).profile();
    CHECK(profile.starter.compressionRatio == doctest::Approx(twoStrokePortsOf(triple()).trappedCompression).epsilon(1e-5));
    // Le préréglage se lance et tient son ralenti.
    const Preset* preset = findPreset("two_stroke_triple");
    REQUIRE(preset != nullptr);
    DrivingModel car(preset->powertrain());
    car.crank();
    for (int i = 0; i < 6000; ++i) car.stepPedal(0.001, 0.0, 0.0);
    CHECK(car.rpm() > 0.8 * idleRpmOf(preset->powertrain()));
    CHECK(car.rpm() < 1.5 * idleRpmOf(preset->powertrain()));
    (void)model;
}
