#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "revforge/boost_control.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"
#include "revforge/turbo.hpp"

using namespace revforge;

namespace {

constexpr double kStep = 0.001;
constexpr double kAtmosphere = 101325.0;
// `kTyreGrip` : celui de DrivingModel, le sommet du pneu (tyre.hpp).

Powertrain controlled(const char* id, BoostControl kind) {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.boostControl = kind;
    return p;
}

BoostControlSpec specOf(const Powertrain& p) { return boostControlSpecFor(p, gearingOf(p), kTyreGrip); }

TurboInput fullLoad(const Powertrain& p, double rpm, double fill = 1.0) {
    TurboInput in;
    in.rpm = rpm;
    in.fill = fill;
    in.breathing = breathingOf(p, rpm);
    in.burning = true;
    return in;
}

/** Un turbo seul, régime tenu, avec ou sans régulation : la pression à chaque pas après le coup de gaz. */
struct Bench {
    TurboCharger turbo;
    BoostController controller;
    const BoostControlSpec* spec = nullptr;
    int gear = 4;

    void step(const Powertrain& p, double rpm, double fill) {
        TurboInput in = fullLoad(p, rpm, fill);
        if (spec != nullptr) {
            BoostControlInput control;
            control.rpm = rpm;
            control.gear = gear;
            control.chargePa = turbo.state().chargePa;
            in.gateDemand = controller.step(*spec, kStep, control);
        }
        turbo.step(kStep, in);
    }
};

struct TipIn {
    double finalPa = 0.0;
    double peakPa = 0.0;
    double t90 = 0.0;
};

/** Huit secondes à mi-charge, puis plein gaz : la crête et la pression établie. */
TipIn tipIn(const Powertrain& p, const BoostControlSpec* spec, double rpm, double seconds = 8.0) {
    Bench bench;
    bench.turbo.setSpec(turboSpecOf(p));
    bench.spec = spec;
    for (int i = 0; i < 8000; ++i) bench.step(p, rpm, 0.2);
    TipIn out;
    const double start = bench.turbo.state().chargePa;
    const int steps = static_cast<int>(seconds / kStep);
    double sum = 0.0;
    int count = 0;
    std::vector<double> trace;
    for (int i = 0; i < steps; ++i) {
        bench.step(p, rpm, 1.0);
        const double pa = bench.turbo.state().chargePa;
        trace.push_back(pa);
        out.peakPa = std::max(out.peakPa, pa);
        if (i >= steps - 1000) {
            sum += pa;
            ++count;
        }
    }
    out.finalPa = sum / count;
    for (int i = 0; i < steps; ++i) {
        if (trace[static_cast<std::size_t>(i)] - start >= 0.9 * (out.finalPa - start)) {
            out.t90 = static_cast<double>(i) * kStep;
            break;
        }
    }
    return out;
}

double gauge(double pa) { return pa - kAtmosphere; }

/** La boucle ouverte linéarisée : banc, électrovanne (retards de la tâche et du découpage), tuyau, PID à dérivée filtrée. */
std::complex<double> openLoop(const BoostPlant& plant, const BoostPid& pid, const BoostActuatorSpec& actuator, double w) {
    const std::complex<double> s(0.0, w);
    const double delay = boostLoopDelayS(plant, actuator);
    const std::complex<double> g = plant.gainPa * std::exp(-delay * s) / (plant.tauS * s + 1.0) / (actuator.chamberS * s + 1.0);
    const std::complex<double> c = pid.kc * (1.0 + 1.0 / (pid.tauI * s) + pid.tauD * s / (pid.tauD / 10.0 * s + 1.0));
    return c * g;
}

}  // namespace

TEST_CASE("la wastegate à ressort reste celle de TurboCharger, au bit près") {
    const Powertrain p = findPreset("hot_hatch_turbo")->powertrain();
    const TurboSpec spec = turboSpecOf(p);
    TurboCharger turbo;
    turbo.setSpec(spec);
    int checked = 0;
    for (int i = 0; i < 6000; ++i) {
        const double before = turbo.state().chargePa;
        const double gateBefore = turbo.state().wastegate;
        turbo.step(kStep, fullLoad(p, 4500.0));
        const TurboState& s = turbo.state();
        // La même membrane, la même anticipation, la même inertie de 50 ms : la fonction pure refait exactement le pas.
        const double wanted = springGateDemand(spec.targetBar, s.chargePa, (s.chargePa - before) / kStep, kAtmosphere);
        const double expected = gateBefore + (wanted - gateBefore) * kStep / 0.05;
        REQUIRE(s.wastegate == expected);
        ++checked;
    }
    CHECK(turbo.state().wastegate > 0.1);
    CHECK(checked == 6000);
    // Sur un ressort, le modèle de conduite ne pilote rien : ni rapport cyclique, ni cible qui bouge.
    DrivingModel model(p);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 1.0, 0.0);
    CHECK(model.boostDuty() == -1.0);
    CHECK(model.boostTargetPa() == doctest::Approx(kAtmosphere + static_cast<double>(p.engine.boostBar) * 1e5));
}

TEST_CASE("le ressort dérive en haut, la régulation électronique tient sa cible") {
    for (const char* id : {"hot_hatch_turbo", "three_cyl_turbo", "five_cyl_turbo"}) {
        const Powertrain spring = findPreset(id)->powertrain();
        const Powertrain electronic = controlled(id, BoostControl::Electronic);
        const BoostControlSpec spec = specOf(electronic);
        const double redline = static_cast<double>(spring.engine.redlineRpm);
        for (const double share : {0.55, 0.75, 0.92}) {
            const double rpm = share * redline;
            INFO(std::string(id) << " à " << rpm << " tr/min");
            const double target = boostTargetPa(spec, rpm, 4, kAtmosphere);
            REQUIRE(target == doctest::Approx(kAtmosphere + static_cast<double>(spring.engine.boostBar) * 1e5));
            const TipIn a = tipIn(spring, nullptr, rpm);
            const TipIn b = tipIn(electronic, &spec, rpm);
            const double springError = (a.finalPa - target) / gauge(target);
            const double electronicError = (b.finalPa - target) / gauge(target);
            MESSAGE(std::string(id) << " à " << rpm << " tr/min : erreur établie " << springError * 100 << " % (ressort), " << electronicError * 100 << " % (électronique) ; 90 % en "
                       << a.t90 << " s et " << b.t90 << " s ; dépassement " << (a.peakPa - a.finalPa) / gauge(a.finalPa) * 100 << " % et "
                       << (b.peakPa - b.finalPa) / gauge(b.finalPa) * 100 << " %");
            // L'intégrale annule l'écart : il ne reste que le bruit du découpage.
            CHECK(std::fabs(electronicError) < 0.003);
            // Le dépassement existe, borné.
            CHECK((b.peakPa - b.finalPa) / gauge(b.finalPa) < 0.12);
            if (share > 0.7) {
                // En haut, la wastegate à ressort doit s'ouvrir davantage, donc la membrane voir plus de pression : elle dérive.
                CHECK(springError > 0.01);
            }
        }
    }
}

TEST_CASE("sans anti-emballement, l'intégrale grossit pendant que le turbo se lance, et la pression dépasse") {
    const Powertrain p = controlled("hot_hatch_turbo", BoostControl::Electronic);
    const BoostControlSpec spec = specOf(p);
    BoostControlSpec loose = spec;
    loose.antiWindup = false;
    for (const double rpm : {2500.0, 3500.0}) {
        // Huit secondes à mi-charge : la pression n'atteint pas la cible, l'écart reste positif, et l'électrovanne en butée.
        const double target = boostTargetPa(spec, rpm, 4, kAtmosphere);
        const TipIn held = tipIn(p, &spec, rpm, 12.0);
        const TipIn wound = tipIn(p, &loose, rpm, 12.0);
        const double heldOver = (held.peakPa - target) / gauge(target);
        const double woundOver = (wound.peakPa - target) / gauge(target);
        MESSAGE("à " << rpm << " tr/min : crête " << heldOver * 100 << " % au-dessus de la cible avec, " << woundOver * 100 << " % sans anti-emballement ; encore "
                   << (wound.finalPa - target) / gauge(target) * 100 << " % au bout de 12 s");
        CHECK(heldOver < 0.12);
        CHECK(woundOver > 0.3);
        CHECK(std::fabs(held.finalPa - target) < 0.003 * gauge(target));
    }
}

TEST_CASE("en quatrième, après des montées et des levers de pied, la reprise ne garde pas l'intégrale d'avant") {
    // Le scénario `turbo` de la CLI : les rapports à mi-pédale, une conduite tranquille vers 1800 tr/min, puis plein gaz.
    for (const char* id : {"three_cyl_turbo", "hot_hatch_turbo"}) {
        DrivingModel model(controlled(id, BoostControl::Electronic));
        model.setManual(true);
        model.requestShift(1);
        int phase = 0;
        double since = 0.0, crest = 0.0, last = 0.0, target = 0.0;
        const double dt = 128.0 / 44100.0;
        for (int step = 0; step < static_cast<int>(28.0 / dt) && phase < 3; ++step) {
            const double t = step * dt;
            const double rpm = model.rpm();
            double pedal = 0.0, brake = 0.0;
            if (phase == 0) {
                pedal = t > 0.3 ? 0.625 : 0.0;
                if (model.gear() < 4 && !model.shifting() && rpm > 3000.0) model.requestShift(1);
                if (model.gear() == 4 && !model.shifting()) phase = 1, since = t;
            } else if (phase == 1) {
                pedal = rpm < 1800.0 ? 0.25 : 0.0;
                brake = rpm > 2000.0 ? 0.125 : 0.0;
                if (t - since > 5.0) phase = 2;
            } else {
                pedal = 1.0;
                if (rpm > 3600.0) phase = 3;
            }
            model.stepPedal(dt, pedal, brake);
            if (phase == 2) {
                crest = std::max(crest, model.turbo().state().chargePa);
                last = model.turbo().state().chargePa;
                target = model.boostTargetPa();
            }
        }
        REQUIRE(phase == 3);
        const double over = (crest - target) / gauge(target);
        MESSAGE(std::string(id) << " : crête " << over * 100 << " % au-dessus de la cible, " << (last - target) / gauge(target) * 100 << " % à 3600 tr/min");
        CHECK(over < 0.12);
        CHECK(std::fabs(last - target) < 0.01 * gauge(target));
    }
}

TEST_CASE("les gains SIMC laissent au moins 45° de marge de phase sur le modèle mesuré") {
    for (const char* id : {"hot_hatch_turbo", "three_cyl_turbo", "five_cyl_turbo", "boxer_four_turbo", "diesel_four"}) {
        const BoostControlSpec spec = specOf(controlled(id, BoostControl::Electronic));
        REQUIRE(spec.plantCount > 0);
        for (int k = 0; k < spec.plantCount; ++k) {
            const BoostPlant& plant = spec.plants[static_cast<std::size_t>(k)];
            const BoostPid pid = simcPid(plant, spec.actuator);
            // La pulsation de coupure, où |L| = 1, par dichotomie en logarithme.
            double low = 0.01, high = 1000.0;
            for (int i = 0; i < 80; ++i) {
                const double middle = std::sqrt(low * high);
                (std::abs(openLoop(plant, pid, spec.actuator, middle)) > 1.0 ? low : high) = middle;
            }
            const double wc = std::sqrt(low * high);
            const double phase = std::arg(openLoop(plant, pid, spec.actuator, wc)) * 180.0 / 3.141592653589793;
            const double margin = 180.0 + (phase > 0.0 ? phase - 360.0 : phase);
            // La marge de gain : là où la phase passe −180°.
            double lo = wc, hi = 2000.0;
            for (int i = 0; i < 80; ++i) {
                const double middle = std::sqrt(lo * hi);
                const double ph = std::arg(openLoop(plant, pid, spec.actuator, middle));
                (ph < 0.0 ? lo : hi) = middle;
            }
            const double gainMargin = 1.0 / std::abs(openLoop(plant, pid, spec.actuator, std::sqrt(lo * hi)));
            MESSAGE(std::string(id) << " à " << plant.rpm << " tr/min : K " << plant.gainPa / 1e5 << " bar par ouverture, τ " << plant.tauS << " s, θ " << plant.deadS << " s ; Kc "
                       << pid.kc * 1e5 << " par bar, τI " << pid.tauI << " s, τD " << pid.tauD << " s ; coupure " << wc << " rad/s, marge de phase " << margin
                       << "°, de gain " << gainMargin);
            CHECK(margin > 45.0);
            CHECK(margin < 80.0);
            CHECK(gainMargin > 2.0);
        }
    }
}

TEST_CASE("le banc retrouve la pente de l'arbre que donne l'équilibre") {
    const Powertrain p = controlled("hot_hatch_turbo", BoostControl::Electronic);
    const BoostControlSpec spec = specOf(p);
    const TurboSpec turbo = turboSpecOf(p);
    for (int k = 0; k < spec.plantCount; ++k) {
        const BoostPlant& plant = spec.plants[static_cast<std::size_t>(k)];
        // À l'équilibre, deux pressions voisines demandent deux ouvertures : leur rapport est le gain statique.
        TurboSpec low = turbo, high = turbo;
        low.targetBar -= 0.04;
        high.targetBar += 0.04;
        const double breathing = breathingOf(p, plant.rpm);
        const double gateLow = turboSteady(low, plant.rpm, breathing).wastegate;
        const double gateHigh = turboSteady(high, plant.rpm, breathing).wastegate;
        const double steadyGain = 0.08e5 / (gateLow - gateHigh);
        MESSAGE("à " << plant.rpm << " tr/min : " << plant.gainPa / 1e5 << " bar par ouverture au banc, " << steadyGain / 1e5 << " à l'équilibre");
        CHECK(plant.gainPa == doctest::Approx(steadyGain).epsilon(0.3));
        CHECK(plant.tauS > 0.2);
        CHECK(plant.tauS < 2.0);
    }
}

TEST_CASE("une cible absolue rend la régulation sensible à l'altitude, pas le ressort") {
    const Powertrain p = controlled("hot_hatch_turbo", BoostControl::Electronic);
    const BoostControlSpec electronic = specOf(p);
    const BoostControlSpec spring = specOf(findPreset("hot_hatch_turbo")->powertrain());
    const double bar = static_cast<double>(p.engine.boostBar);
    // L'atmosphère standard à 2000 m.
    const double high = 79495.0;
    CHECK(boostTargetGaugeBar(electronic, 4000.0, 4, kAtmosphere) == doctest::Approx(bar));
    CHECK(boostTargetGaugeBar(electronic, 4000.0, 4, high) == doctest::Approx(bar + (kAtmosphere - high) / 1e5));
    CHECK(boostTargetPa(electronic, 4000.0, 4, high) == doctest::Approx(boostTargetPa(electronic, 4000.0, 4, kAtmosphere)));
    // Le ressort tient sa pression au-dessus de l'air ambiant : en altitude, moins d'air dans le moteur.
    CHECK(boostTargetGaugeBar(spring, 4000.0, 4, high) == doctest::Approx(bar));
    CHECK(boostTargetPa(spring, 4000.0, 4, high) < boostTargetPa(spring, 4000.0, 4, kAtmosphere) - 20000.0);
    // Là où les roues protègent le turbo, c'est un rapport de pression : en altitude, la cible absolue baisse.
    Powertrain small = controlled("hot_hatch_turbo", BoostControl::Electronic);
    small.engine.turboSize = TurboSize::Small;
    small.engine.boostBar = 2.0f;
    const BoostControlSpec limited = specOf(small);
    const double redline = static_cast<double>(small.engine.redlineRpm);
    const double atSea = boostTargetPa(limited, redline, 4, kAtmosphere);
    CHECK(atSea < limited.calibratedPa);
    CHECK(boostTargetPa(limited, redline, 4, high) < atSea);
}

TEST_CASE("selon le rapport, la cible baisse là où les pneus ne passeraient pas le couple") {
    const Powertrain p = controlled("three_cyl_turbo", BoostControl::ByGear);
    const Gearing gearing = gearingOf(p);
    const BoostControlSpec spec = specOf(p);
    // Une traction de 1150 kg : la première ne passe pas tout le couple du moteur.
    const double firstCap = gearTorqueCapNm(p, gearing, 1, kTyreGrip);
    CHECK(firstCap < powerSummary(p).peakTorqueNm);
    CHECK(gearTorqueCapNm(p, gearing, 2, kTyreGrip) > firstCap);
    for (const double rpm : {2500.0, 3500.0, 4500.0}) {
        const double first = boostTargetPa(spec, rpm, 1, kAtmosphere);
        CHECK(first < boostTargetPa(spec, rpm, 3, kAtmosphere) - 10000.0);
        // Au point mort, le calculateur ne sait pas ce qui passera : la première.
        CHECK(boostTargetPa(spec, rpm, 0, kAtmosphere) == first);
        // La cible est la pression qui donne ce couple : le modèle de remplissage, à l'envers.
        const double share = (first - kAtmosphere) / (static_cast<double>(p.engine.boostBar) * 1e5);
        CHECK(fullLoadTorqueNm(p, rpm, share) == doctest::Approx(firstCap).epsilon(0.01));
    }
    // L'électronique seule n'a pas ce plafond.
    const BoostControlSpec plain = specOf(controlled("three_cyl_turbo", BoostControl::Electronic));
    CHECK(boostTargetPa(plain, 3500.0, 1, kAtmosphere) == doctest::Approx(plain.calibratedPa));

    // Dans la voiture : un départ arrêté, la première patine moins.
    const auto launch = [](const Powertrain& car) {
        DrivingModel model(car);
        double slip = 0.0, target = 1e9;
        for (int i = 0; i < 100 && !model.meshed(); ++i) model.stepPedal(0.01, 1.0, 0.0);
        for (int i = 0; i < 600; ++i) {
            model.stepPedal(0.01, 1.0, 0.0);
            if (model.gear() == 1) {
                slip = std::max(slip, model.wheelSlip());
                target = std::min(target, model.boostTargetPa());
            }
        }
        return std::make_pair(slip, target);
    };
    const auto byGear = launch(p);
    const auto electronic = launch(controlled("three_cyl_turbo", BoostControl::Electronic));
    MESSAGE("première, glissement maximal : " << byGear.first << " m/s selon le rapport, " << electronic.first << " m/s sans");
    CHECK(byGear.second < electronic.second);
    CHECK(byGear.first < electronic.first);
}

TEST_CASE("l'électrovanne a ses zones mortes, et la précommande en tient compte") {
    const BoostActuatorSpec actuator;
    // Une impulsion plus courte que l'ouverture ne fait rien ; une pause plus courte que la fermeture non plus.
    CHECK(effectiveDuty(actuator, 0.1) == 0.0);
    CHECK(effectiveDuty(actuator, 0.95) == 1.0);
    CHECK(effectiveDuty(actuator, 0.5) == doctest::Approx(0.5 - (actuator.openS - actuator.closeS) * actuator.pwmHz));
    for (const double gate : {0.2, 0.4, 0.6, 0.8}) CHECK(gateForVacuum(effectiveDuty(actuator, dutyForGate(actuator, gate))) == doctest::Approx(gate));
    CHECK(dutyForGate(actuator, 1.0) == 0.0);
    CHECK(dutyForGate(actuator, 0.0) == 1.0);
}

TEST_CASE("la régulation ne dépend pas de la durée demandée, et le compresseur mécanique l'ignore") {
    const Powertrain p = controlled("hot_hatch_turbo", BoostControl::ByGear);
    DrivingModel coarse(p);
    DrivingModel fine(p);
    for (int i = 0; i < 500; ++i) coarse.stepPedal(0.008, 1.0, 0.0);
    for (int i = 0; i < 2000; ++i) fine.stepPedal(0.002, 1.0, 0.0);
    CHECK(coarse.turbo().state().chargePa == doctest::Approx(fine.turbo().state().chargePa).epsilon(1e-3));
    CHECK(coarse.boostDuty() >= 0.0);
    CHECK(coarse.boostDuty() <= 1.0);
    // Un compresseur n'a pas de wastegate : le réglage ne change rien, au bit près.
    Powertrain charged = findPreset("v8_supercharged")->powertrain();
    DrivingModel plain(charged);
    charged.engine.boostControl = BoostControl::Electronic;
    DrivingModel ignored(charged);
    for (int i = 0; i < 500; ++i) {
        plain.stepPedal(0.01, 1.0, 0.0);
        ignored.stepPedal(0.01, 1.0, 0.0);
        REQUIRE(plain.boost() == ignored.boost());
        REQUIRE(plain.rpm() == ignored.rpm());
    }
    CHECK(ignored.boostDuty() == -1.0);
}
