#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/turbo.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kPi = 3.141592653589793;
constexpr AntiLag kSystems[] = {AntiLag::Retard, AntiLag::AirInjection, AntiLag::CylinderCut};

Powertrain withAntiLag(AntiLag kind, const char* id = "hot_hatch_turbo") {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.antiLag = kind;
    return p;
}

double shaftRpm(const DrivingModel& model) { return model.turbo().state().shaftOmega * 60.0 / (2.0 * kPi); }

/** En quatrième, plein gaz le temps que la pression s'établisse. */
void pull(DrivingModel& model) {
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 6000 && (model.gear() < 4 || model.shifting()); ++i) {
        model.stepPedal(0.01, 0.6, 0.0);
        if (!model.shifting() && model.rpm() > 3500.0 && model.gear() < 4) model.requestShift(1);
    }
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 1.0, 0.0);
}

struct Lift {
    double shaftAt1s = 0.0, shaftAt3s = 0.0, boostAt3s = 0.0;
    double hottest = 0.0, torque = 0.0, lost = 0.0, fuelGps = 0.0, air = 0.0;
    double lambda = 0.0, reprise = 30.0, rpmAtEnd = 0.0;
    bool active = true;
    double blowOff = 0.0;
};

/** Un lever de pied de 3 s en quatrième, puis la reprise : ce que chaque système doit changer. */
Lift liftAndCatch(AntiLag kind) {
    DrivingModel model(withAntiLag(kind));
    pull(model);
    Lift out;
    int counted = 0;
    for (int i = 0; i < 300; ++i) {
        model.stepPedal(0.01, 0.0, 0.0);
        if (i == 99) out.shaftAt1s = shaftRpm(model);
        if (i < 30) continue;
        ++counted;
        out.hottest = std::max(out.hottest, model.turbo().state().turbineInK);
        out.torque += model.torqueNm();
        out.lost += model.antiLagLostNm();
        out.fuelGps += model.consumption().fuelGps();
        out.air += model.turbo().antiLag().airFlow;
        out.active = out.active && model.antiLagActive();
        out.blowOff = std::max(out.blowOff, model.turbo().state().blowOff);
    }
    out.torque /= counted;
    out.lost /= counted;
    out.fuelGps /= counted;
    out.air /= counted;
    out.shaftAt3s = shaftRpm(model);
    out.boostAt3s = model.boost();
    out.lambda = model.consumption().lambdaNow();
    out.rpmAtEnd = model.rpm();
    for (int i = 0; i < 1500; ++i) {
        model.stepPedal(0.01, 1.0, 0.0);
        if (model.boost() >= 0.9) {
            out.reprise = static_cast<double>(i) * 0.01;
            break;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("pied levé, chaque anti-lag garde l'arbre plus haut et raccourcit vraiment la reprise") {
    const Lift off = liftAndCatch(AntiLag::Off);
    MESSAGE("sans : arbre " << off.shaftAt1s << " puis " << off.shaftAt3s << " tr/min, reprise " << off.reprise << " s");
    CHECK_FALSE(off.active);
    for (const AntiLag kind : kSystems) {
        const Lift with = liftAndCatch(kind);
        MESSAGE(static_cast<int>(kind) << " : arbre " << with.shaftAt1s << " puis " << with.shaftAt3s << " tr/min, pression " << with.boostAt3s << ", reprise " << with.reprise << " s, " << with.hottest - 273.15 << " °C, " << with.fuelGps << " g/s, " << with.lost << " Nm perdus, " << with.air * 1000.0 << " g/s d'air");
        CHECK(with.active);
        CHECK(with.shaftAt1s > 1.5 * off.shaftAt1s);
        CHECK(with.shaftAt3s > 2.0 * off.shaftAt3s);
        CHECK(with.reprise < 0.7 * off.reprise);
        // Il brûle, là où un lever de pied ordinaire coupe l'injection.
        CHECK(with.fuelGps > 0.5);
        CHECK(off.fuelGps == 0.0);
        // Ce couple-là ne vient pas d'un gain saisi : la soupape reste fermée, sans quoi l'air s'en irait.
        CHECK(with.blowOff < 0.02);
        CHECK(off.blowOff > 0.5);
        // Le moteur freine encore, mais moins qu'une coupure : il ne s'emballe pas.
        CHECK(with.torque < 0.0);
        CHECK(with.torque > off.torque);
    }
}

TEST_CASE("les différences entre systèmes sortent de leur mécanisme") {
    const Lift off = liftAndCatch(AntiLag::Off);
    const Lift retard = liftAndCatch(AntiLag::Retard);
    const Lift air = liftAndCatch(AntiLag::AirInjection);
    const Lift cut = liftAndCatch(AntiLag::CylinderCut);
    // Le retard réchauffe sans dépendre de rien : tout le travail perdu part dans le gaz. Une poche imbrûlée, elle, ne brûle qu'en partie.
    CHECK(retard.hottest > cut.hottest + 50.0);
    CHECK(retard.hottest > off.hottest + 150.0);
    CHECK(retard.hottest - 273.15 > 900.0);
    CHECK(retard.hottest - 273.15 < 1250.0);
    // Seule l'injection d'air fait passer de l'air à côté des cylindres.
    CHECK(air.air > 0.005);
    CHECK(retard.air == 0.0);
    CHECK(cut.air == 0.0);
    // Elle retire moins de couple au moteur, mais elle paie son air en essence : c'est celle qui brûle le plus, et qui lance le plus vite.
    CHECK(air.lost < 0.7 * retard.lost);
    CHECK(air.lost < 0.7 * cut.lost);
    CHECK(air.fuelGps > 1.2 * retard.fuelGps);
    CHECK(air.fuelGps > 1.2 * cut.fuelGps);
    CHECK(air.shaftAt1s > 1.2 * retard.shaftAt1s);
    CHECK(air.shaftAt1s > 1.2 * cut.shaftAt1s);
    // Le lambda suit : le retard noie le cylindre, la coupure ne change rien au mélange, l'injection d'air apporte l'oxygène de son surplus.
    CHECK(retard.lambda < 0.95 * cut.lambda);
    CHECK(air.lambda > retard.lambda);
}

TEST_CASE("un cylindre sur deux ou trois n'allume pas, dans l'ordre d'allumage") {
    int fired = 0;
    for (std::uint32_t event = 0; event < 1000; ++event) fired += antiLagFires(event) ? 1 : 0;
    CHECK(static_cast<double>(fired) / 1000.0 == doctest::Approx(kAntiLagFiredShare));
    CHECK(antiLagTorqueShare(AntiLag::CylinderCut) == doctest::Approx(kAntiLagFiredShare));
    // Le retard ne touche pas au carburant du cylindre, mais retire le couple.
    CHECK(antiLagTorqueShare(AntiLag::Retard) < 0.5);
    CHECK(antiLagTorqueShare(AntiLag::Retard) > 0.2);
    CHECK(antiLagTorqueShare(AntiLag::Off) == 1.0);
    // Un mélange ne s'enflamme pas sur des parois froides.
    CHECK(antiLagIgnition(600.0) == 0.0);
    CHECK(antiLagIgnition(1300.0) == 1.0);
}

TEST_CASE("la vanne d'air ne s'ouvre que si le compresseur pousse plus fort que le collecteur") {
    const Powertrain p = withAntiLag(AntiLag::AirInjection);
    TurboCharger turbo;
    turbo.setSpec(turboSpecOf(p));
    TurboInput in;
    in.rpm = 4000.0;
    in.fill = antiLagOpening(AntiLag::AirInjection);
    in.breathing = breathingOf(p, 4000.0);
    in.burning = true;
    in.antiLag.kind = AntiLag::AirInjection;
    in.antiLag.active = true;
    // Rien en réserve : l'arbre arrêté ne fait aucune pression, le clapet reste fermé.
    for (int i = 0; i < 200; ++i) turbo.step(0.001, in);
    CHECK(turbo.state().chargePa < turbo.state().turbineInPa);
    CHECK(turbo.antiLag().airFlow == 0.0);
    // Le lancer plein gaz : la vanne fermée, la pression monte.
    TurboInput full = in;
    full.fill = 1.0;
    full.antiLag.active = false;
    for (int i = 0; i < 4000; ++i) turbo.step(0.001, full);
    CHECK(turbo.antiLag().airFlow == 0.0);
    // Pied levé : elle s'ouvre et l'air passe. Il peut pomper à quelques hertz avec le compresseur, jamais battre d'un pas à l'autre.
    for (int i = 0; i < 500; ++i) turbo.step(0.001, in);
    double low = 1e9, high = 0.0, jump = 0.0, last = turbo.antiLag().airFlow;
    for (int i = 0; i < 500; ++i) {
        turbo.step(0.001, in);
        low = std::min(low, turbo.antiLag().airFlow);
        high = std::max(high, turbo.antiLag().airFlow);
        jump = std::max(jump, std::fabs(turbo.antiLag().airFlow - last));
        last = turbo.antiLag().airFlow;
    }
    CHECK(low > 0.005);
    CHECK(jump < 0.1 * high);
    CHECK(turbo.antiLag().collectorHeatW > 5000.0);
    CHECK(turbo.state().chargePa > turbo.state().turbineInPa);
    // Sans carburant en surplus, plus de chaleur : c'est lui qu'il brûle, et l'air seul ne ferait que refroidir.
    CHECK(turbo.antiLag().surplusFuel > 0.0);
}

TEST_CASE("le réglage retiré en marche, la vanne d'air ne vide plus la pression") {
    Powertrain p = withAntiLag(AntiLag::AirInjection);
    DrivingModel model(p);
    pull(model);
    for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.0, 0.0);
    REQUIRE(model.turbo().antiLag().airFlow > 0.0);
    p.engine.antiLag = AntiLag::Off;
    model.setPowertrain(p);
    model.stepPedal(0.01, 0.0, 0.0);
    CHECK_FALSE(model.antiLagActive());
    CHECK(model.turbo().antiLag().airFlow == 0.0);
    CHECK(model.turbo().antiLag().collectorHeatW == 0.0);
    CHECK(model.turbo().antiLag().surplusFuel == 0.0);
}

TEST_CASE("le pied posé, ou trop bas dans les tours, l'anti-lag ne fait rien") {
    for (const AntiLag kind : kSystems) {
        DrivingModel model(withAntiLag(kind));
        pull(model);
        CHECK_FALSE(model.antiLagActive());
        CHECK(model.antiLagLostNm() == 0.0);
        // Ralenti : pied levé, mais rien à protéger, et le moteur ne doit pas caler.
        DrivingModel idle(withAntiLag(kind));
        for (int i = 0; i < 400; ++i) idle.stepNeutral(0.01, 0.0);
        CHECK_FALSE(idle.antiLagActive());
        CHECK(idle.rpm() == doctest::Approx(idle.idleRpm()).epsilon(0.1));
        // Un coup de gaz au point mort : le moteur redescend de lui-même, l'anti-lag s'arrête, et le ralenti reprend sans caler.
        for (int i = 0; i < 80; ++i) idle.stepNeutral(0.01, 1.0);
        double top = 0.0;
        bool wasActive = false;
        for (int i = 0; i < 1500; ++i) {
            idle.stepNeutral(0.01, 0.0);
            top = std::max(top, idle.rpm());
            wasActive = wasActive || idle.antiLagActive();
        }
        CAPTURE(static_cast<int>(kind));
        CHECK(wasActive);
        CHECK_FALSE(idle.stalled());
        CHECK_FALSE(idle.antiLagActive());
        CHECK(idle.rpm() < 1.3 * idle.idleRpm());
        CHECK(top < idle.redlineRpm());
    }
}

TEST_CASE("pied levé au point mort, aucun moteur ne reste accroché en haut") {
    for (const char* id : {"hot_hatch_turbo", "three_cyl_turbo", "five_cyl_turbo", "boxer_four_turbo"}) {
        for (const AntiLag kind : kSystems) {
            DrivingModel model(withAntiLag(kind, id));
            for (int i = 0; i < 300; ++i) model.stepNeutral(0.01, 1.0);
            double last = model.rpm();
            for (int i = 0; i < 2000; ++i) {
                model.stepNeutral(0.01, 0.0);
                if (model.antiLagActive()) last = model.rpm();
            }
            CAPTURE(id);
            CAPTURE(static_cast<int>(kind));
            CHECK_FALSE(model.antiLagActive());
            CHECK(model.rpm() < 1.3 * model.idleRpm());
            CHECK(last < 0.5 * model.redlineRpm());
        }
    }
}

TEST_CASE("la consommation compte ce qui brûle hors du cylindre") {
    const auto trip = [](AntiLag kind) {
        DrivingModel model(withAntiLag(kind));
        pull(model);
        // Le pied met un instant à se lever : le compteur part une fois le pied levé.
        for (int i = 0; i < 20; ++i) model.stepPedal(0.01, 0.0, 0.0);
        model.resetTrip();
        for (int i = 0; i < 500; ++i) model.stepPedal(0.01, 0.0, 0.0);
        return model.consumption().fuelTripL();
    };
    const double off = trip(AntiLag::Off);
    CHECK(off < 1e-3);
    for (const AntiLag kind : kSystems) CHECK(trip(kind) > 0.005);
    // Le surplus de l'injection d'air s'ajoute à ce que demande le cylindre, avec l'air qu'il trouve : le CO₂ suit le carburant.
    DrivingModel model(withAntiLag(AntiLag::AirInjection));
    pull(model);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.0, 0.0);
    CHECK(model.turbo().antiLag().surplusFuel > 0.0);
    CHECK(model.consumption().lambdaNow() > 0.5);
    CHECK(model.consumption().lambdaNow() < 1.6);
    CHECK(model.consumption().fuelGps() > model.turbo().antiLag().surplusFuel * 1000.0);
}

TEST_CASE("sur un diesel ou sans turbo, le réglage n'existe pas") {
    for (const char* id : {"diesel_four", "city_14", "hot_hatch_na"}) {
        for (const AntiLag kind : kSystems) {
            DrivingModel with(withAntiLag(kind, id));
            DrivingModel without(withAntiLag(AntiLag::Off, id));
            for (int i = 0; i < 1500; ++i) {
                const double pedal = i < 600 ? 1.0 : 0.0;
                with.stepPedal(0.01, pedal, 0.0);
                without.stepPedal(0.01, pedal, 0.0);
                REQUIRE_FALSE(with.antiLagActive());
            }
            CHECK(with.rpm() == without.rpm());
            CHECK(with.torqueNm() == without.torqueNm());
            CHECK(with.consumption().fuelTripL() == without.consumption().fuelTripL());
            CHECK(with.turbo().state().shaftOmega == without.turbo().state().shaftOmega);
        }
    }
}

TEST_CASE("réglage sans effet sur les courbes de la fiche : il ne travaille que pied levé") {
    const Powertrain off = withAntiLag(AntiLag::Off);
    const Performance plain = measurePerformance(off);
    MESSAGE("0-100 km/h sans anti-lag : " << plain.zeroTo100S << " s");
    for (const AntiLag kind : kSystems) {
        const Powertrain with = withAntiLag(kind);
        for (const double rpm : {2000.0, 3500.0, 5500.0}) CHECK(torqueCurveNm(with, rpm) == torqueCurveNm(off, rpm));
        CHECK(powerSummary(with).peakPowerKw == powerSummary(off).peakPowerKw);
        // Pied au plancher, seuls les passages lèvent le pied : la fiche ne bouge guère, et ne peut que s'améliorer.
        const Performance perf = measurePerformance(with);
        MESSAGE(static_cast<int>(kind) << " : " << perf.zeroTo100S << " s");
        CHECK(perf.zeroTo100S <= plain.zeroTo100S + 0.05);
        CHECK(perf.zeroTo100S > plain.zeroTo100S - 1.0);
    }
}

namespace {

struct Heard {
    Samples samples;
    std::uint32_t pops = 0;
};

/** Le long d'un lever de pied maintenu, par le vrai modèle : le son qu'il donne, et ses détonations. */
Heard renderLift(AntiLag kind, int block = 128, const char* id = "hot_hatch_turbo") {
    const Powertrain p = withAntiLag(kind, id);
    DrivingModel model(p);
    pull(model);
    EngineSound sound(kRate);
    sound.setProfile(p.profile());
    sound.setVolume(0.7f);
    sound.setListener(Listener::Outside);
    constexpr int kSlice = 512;
    constexpr int kSlices = 240;
    Heard out;
    out.samples.resize(static_cast<std::size_t>(kSlice) * kSlices);
    for (int u = 0; u < kSlices; ++u) {
        model.stepPedal(static_cast<double>(kSlice) / kRate, 0.0, 0.0);
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking(), static_cast<float>(model.throttle()));
        sound.setTurbo(model.turboDrive());
        for (int done = 0; done < kSlice; done += block) sound.render(out.samples.data() + static_cast<std::size_t>(u) * kSlice + done, block);
    }
    out.pops = sound.pops();
    return out;
}

/** Énergie à une fréquence (Goertzel), sur la seconde moitié. */
double powerNear(const Samples& all, double hz, double span) {
    const Samples x = slice(all, all.size() / 2, all.size());
    double sum = 0.0;
    for (double f = hz - span; f <= hz + span; f += 0.5) {
        const double w = 2.0 * kPi * f / kRate;
        const double coeff = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (float sample : x) {
            const double s0 = static_cast<double>(sample) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        sum += s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
    return sum;
}

}  // namespace

TEST_CASE("le son de chaque système vient de ce qu'il fait au gaz") {
    const Heard off = renderLift(AntiLag::Off);
    const Heard retard = renderLift(AntiLag::Retard);
    const Heard air = renderLift(AntiLag::AirInjection);
    const Heard cut = renderLift(AntiLag::CylinderCut);
    MESSAGE("efficace : sans " << rms(off.samples) << ", retard " << rms(retard.samples) << ", air " << rms(air.samples) << ", coupure " << rms(cut.samples) << " ; détonations " << off.pops << ", " << retard.pops << ", " << air.pops << ", " << cut.pops);
    // Il brûle : plus fort qu'une coupure d'injection.
    CHECK(rms(retard.samples) > 1.5 * rms(off.samples));
    CHECK(rms(air.samples) > 1.3 * rms(off.samples));
    CHECK(rms(cut.samples) > 1.3 * rms(off.samples));
    // Les détonations : d'autant plus que ce qui arrive au collecteur arrive imbrûlé. Ni réglage de détonations, ni coupure d'injection.
    CHECK(cut.pops * 2 > 3 * retard.pops);
    CHECK(retard.pops > air.pops);
    CHECK(cut.pops > 8);
}

TEST_CASE("la coupure de cylindres hache le son, au rythme de l'ordre d'allumage") {
    // Quatre cylindres : le cycle fait 4 explosions, et la coupure a un rythme de 5. Ses raies tombent entre celles du moteur.
    const Powertrain p = withAntiLag(AntiLag::CylinderCut);
    DrivingModel probe(p);
    pull(probe);
    for (int i = 0; i < 200; ++i) probe.stepPedal(0.01, 0.0, 0.0);
    const auto rhythm = [&](AntiLag kind) {
        // À régime fixe et état figé, pour lire les raies : seule la coupure diffère.
        EngineSound sound(kRate);
        sound.setProfile(p.profile());
        sound.setVolume(0.7f);
        TurboDrive d = probe.turboDrive();
        d.antiLag.kind = kind;
        d.antiLag.active = kind != AntiLag::Off;
        d.antiLag.heatW = 0.0f;
        d.antiLag.heatShare = 0.0f;
        d.antiLag.ignition = 0.0f;
        Samples out(256 * 512);
        for (std::size_t at = 0; at < out.size(); at += 256) {
            sound.setTargetRpm(3600.0f);
            sound.setDrive(0.35f, 0.3f, false, false, 0.0f);
            sound.setTurbo(d);
            sound.render(out.data() + at, 256);
        }
        return out;
    };
    const Samples full = rhythm(AntiLag::Retard);
    const Samples chopped = rhythm(AntiLag::CylinderCut);
    // 3600 tr/min : 120 explosions par seconde, 24 Hz pour un motif de 5. On regarde ses raies, 72 et 96 Hz, là où un moteur rond n'a rien.
    const double firedLine = powerNear(chopped, 120.0, 2.0);
    const double between = powerNear(chopped, 96.0, 2.0) + powerNear(chopped, 72.0, 2.0);
    const double roundBetween = powerNear(full, 96.0, 2.0) + powerNear(full, 72.0, 2.0);
    MESSAGE("raies du motif : " << between << " contre " << roundBetween << " pour un moteur rond ; raie d'allumage " << firedLine << " contre " << powerNear(full, 120.0, 2.0));
    CHECK(between > 4.0 * roundBetween);
    CHECK(firedLine < powerNear(full, 120.0, 2.0));
}

TEST_CASE("le son de l'anti-lag ne dépend pas de la taille des blocs, et n'alloue rien") {
    const Heard a = renderLift(AntiLag::CylinderCut, 512);
    const Heard b = renderLift(AntiLag::CylinderCut, 64);
    REQUIRE(a.samples.size() == b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i) REQUIRE(a.samples[i] == b.samples[i]);

    const Powertrain p = withAntiLag(AntiLag::AirInjection);
    DrivingModel model(p);
    pull(model);
    for (int i = 0; i < 100; ++i) model.stepPedal(0.01, 0.0, 0.0);
    REQUIRE(model.antiLagActive());
    EngineSound sound(kRate);
    sound.setProfile(p.profile());
    sound.setVolume(0.7f);
    Samples out(4096);
    sound.render(out.data(), 2048);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) {
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), false, false, 0.0f);
        sound.setTurbo(model.turboDrive());
        sound.render(out.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}
