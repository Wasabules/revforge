#include "doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "gearbox_sound.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kPi = 3.141592653589793;
constexpr int kRate = 44100;
constexpr int kTick = 128;

Powertrain preset(const char* id) { return findPreset(id)->powertrain(); }

Powertrain withBox(const char* id, GearboxKind kind, ToothForm form, float limitNm = 0.0f) {
    Powertrain p = preset(id);
    p.gearbox.kind = kind;
    p.gearbox.form = form;
    p.gearbox.limitNm = limitNm;
    return p;
}

/** Puissance à cette fréquence, fenêtre de Hann, par une transformée directe. */
double powerAt(const std::vector<float>& x, double hz, std::size_t from = 0, std::size_t count = 0) {
    if (count == 0) count = x.size() - from;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double a = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(count));
        re += x[from + i] * w * std::cos(a);
        im += x[from + i] * w * std::sin(a);
    }
    return re * re + im * im;
}

double db(double power) { return 10.0 * std::log10(power + 1e-30); }

double rmsOf(const std::vector<float>& x, std::size_t from = 0, std::size_t count = 0) {
    if (count == 0) count = x.size() - from;
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) sum += static_cast<double>(x[from + i]) * x[from + i];
    return std::sqrt(sum / static_cast<double>(count));
}

/** Passe-haut du second ordre, pour regarder le métal sans le grave. */
std::vector<float> above(const std::vector<float>& x, float hz) {
    dsp::Svf filter;
    filter.set(hz, 0.707f, kRate);
    std::vector<float> out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        filter.process(x[i]);
        out[i] = x[i] - filter.lp - filter.k * filter.bp;
    }
    return out;
}

/** Un état fixe de la boîte, directement : pour mesurer le son d'une cause, et d'une seule. */
GearboxDrive steadyDrive(int gear, double inputRpm, const GearboxSpec& spec, double torqueNm) {
    GearboxDrive d;
    const double input = inputRpm * 2.0 * kPi / 60.0;
    d.inputOmega = static_cast<float>(input);
    d.outputOmega = gear > 0 ? static_cast<float>(input / spec.boxRatio[static_cast<std::size_t>(gear - 1)]) : 0.0f;
    d.torqueNm = static_cast<float>(torqueNm);
    d.gear = gear;
    d.running = true;
    return d;
}

std::vector<float> renderBox(const GearboxSpec& spec, const GearboxDrive& drive, double seconds, int blockSize = kTick) {
    GearboxSound sound(kRate);
    sound.setup(spec);
    sound.setDrive(drive);
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
    float phase = 0.0f;
    const float inc = 3000.0f / 120.0f / kRate;
    int left = blockSize;
    for (float& sample : out) {
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        sample = sound.sample(phase);
        if (--left == 0) {
            sound.setDrive(drive);
            left = blockSize;
        }
    }
    return out;
}

/** Le bruit de boîte seul, le modèle de conduite aux commandes. */
template <typename Drive>
std::vector<float> driveBox(DrivingModel& model, double seconds, Drive drive) {
    GearboxSound sound(kRate);
    sound.setup(model.box());
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
    float phase = 0.0f;
    const double dt = static_cast<double>(kTick) / kRate;
    std::size_t done = 0;
    double t = 0.0;
    while (done < out.size()) {
        drive(t, dt);
        t += dt;
        sound.setDrive(model.gearboxDrive());
        const float inc = static_cast<float>(model.rpm()) / 120.0f / kRate;
        const std::size_t n = std::min<std::size_t>(kTick, out.size() - done);
        for (std::size_t i = 0; i < n; ++i) {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            out[done + i] = sound.sample(phase);
        }
        done += n;
    }
    return out;
}

/** Monte en manuelle jusqu'au rapport voulu, à mi-gaz. */
DrivingModel inGear(const Powertrain& p, int gear) {
    DrivingModel model(p);
    model.setManual(true);
    for (int i = 0; i < 40000; ++i) {
        if (model.gear() < gear && !model.shifting() && (model.gear() == 0 || model.rpm() > model.redlineRpm() * 0.6)) model.requestShift(1);
        model.stepPedal(0.001, 0.7, 0.0);
        if (model.gear() == gear && model.meshed() && !model.shifting() && model.rpm() > model.redlineRpm() * 0.45) break;
    }
    return model;
}

int gcd(int a, int b) {
    while (b != 0) {
        const int r = a % b;
        a = b;
        b = r;
    }
    return a;
}

}  // namespace

// ── La pièce ──

TEST_CASE("le rapport global vient de nombres de dents entiers") {
    for (const char* id : {"city_14", "hot_hatch_na", "v8_cross_plane", "v12"}) {
        INFO(id);
        const Gearing g = gearingOf(preset(id));
        const GearboxSpec& box = g.box;
        for (int i = 0; i < g.gears; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            const GearPair& pair = box.pairs[k];
            double expected = static_cast<double>(pair.driven) / pair.driver;
            if (box.threeShafts && i + 1 != box.directGear) expected *= static_cast<double>(box.inputPair.driven) / box.inputPair.driver;
            if (i + 1 == box.directGear) expected = 1.0;
            CHECK(box.boxRatio[k] == doctest::Approx(expected));
            expected *= static_cast<double>(box.finalPair.driven) / box.finalPair.driver;
            CHECK(g.overall[k] == doctest::Approx(expected));
        }
        // Le pont aussi.
        CHECK(box.finalDrive == doctest::Approx(static_cast<double>(box.finalPair.driven) / box.finalPair.driver));
    }
}

TEST_CASE("les dents sont celles d'une vraie boîte : pas de trop petit pignon, deux dents se retrouvent rarement") {
    const Gearing g = gearingOf(preset("hot_hatch_na"));
    for (int i = 0; i < g.gears; ++i) {
        const GearPair& pair = g.box.pairs[static_cast<std::size_t>(i)];
        CHECK(pair.driver >= 10);
        CHECK(pair.driven >= 10);
        CHECK(gcd(pair.driver, pair.driven) == 1);
    }
    // Les rapports réels restent proches de la table demandée, à quelques pourcents près.
    constexpr double wanted[6] = {3.55, 2.05, 1.45, 1.12, 0.89, 0.73};
    for (int i = 0; i < 6; ++i) CHECK(g.box.boxRatio[static_cast<std::size_t>(i)] == doctest::Approx(wanted[i]).epsilon(0.025));
}

TEST_CASE("sur mesure : chaque vitesse garde le rapport demandé, à une dent près") {
    Powertrain p = preset("hot_hatch_na");
    p.gearbox.family = GearFamily::Custom;
    p.gearbox.gears = 5;
    p.gearbox.ratios = {3.1f, 2.0f, 1.45f, 1.1f, 0.85f};
    p.gearbox.finalDrive = 4.1f;
    const Gearing g = gearingOf(p);
    CHECK(g.gears == 5);
    for (int i = 0; i < 5; ++i) CHECK(g.box.boxRatio[static_cast<std::size_t>(i)] == doctest::Approx(p.gearbox.ratios[static_cast<std::size_t>(i)]).epsilon(0.03));
    CHECK(g.box.finalDrive == doctest::Approx(4.1).epsilon(0.03));
    // Un rapport ne peut pas en doubler un autre.
    p.gearbox.ratios = {3.1f, 1.0f, 1.45f, 1.1f, 0.85f};
    const Gearing h = gearingOf(p);
    for (int i = 1; i < 5; ++i) CHECK(h.box.boxRatio[static_cast<std::size_t>(i - 1)] > h.box.boxRatio[static_cast<std::size_t>(i)]);
}

TEST_CASE("la même boîte sur deux moteurs a les mêmes rapports : elle ne dépend pas d'eux") {
    GearboxConfig box;
    box.family = GearFamily::Close;
    box.gears = 6;
    box.finalDrive = 4.1f;
    box.limitNm = 500.0f;
    Powertrain small = preset("city_14");
    Powertrain big = preset("v8_cross_plane");
    big.car = small.car;
    small.gearbox = box;
    big.gearbox = box;
    const Gearing a = gearingOf(small);
    const Gearing b = gearingOf(big);
    for (int i = 0; i < 6; ++i) CHECK(a.overall[static_cast<std::size_t>(i)] == doctest::Approx(b.overall[static_cast<std::size_t>(i)]));
    CHECK(a.box.limitNm == doctest::Approx(b.box.limitNm));
    // Contre-épreuve : sans pont choisi, il s'adapte au moteur.
    small.gearbox.finalDrive = 0.0f;
    big.gearbox.finalDrive = 0.0f;
    CHECK(gearingOf(small).overall[0] != doctest::Approx(gearingOf(big).overall[0]).epsilon(0.05));
}

TEST_CASE("la vitesse sur chaque rapport au régime maximal vient de la démultiplication") {
    const Powertrain p = preset("hot_hatch_na");
    const Gearing g = gearingOf(p);
    const double radius = wheelRadiusM(p.car);
    const double redline = redlineRpmOf(p);
    for (int gear = 1; gear <= 3; ++gear) {
        INFO(gear);
        DrivingModel model(p);
        model.setManual(true);
        double top = 0.0;
        for (int i = 0; i < 40000; ++i) {
            if (model.gear() < gear && !model.shifting() && (model.gear() == 0 || model.rpm() > redline * 0.9)) model.requestShift(1);
            model.stepPedal(0.001, 1.0, 0.0);
            if (model.gear() == gear && model.meshed() && !model.shifting()) top = std::max(top, model.speedKmh());
        }
        const double analytic = redline * 2.0 * kPi / 60.0 * radius / g.overall[static_cast<std::size_t>(gear - 1)] * 3.6;
        CHECK(top == doctest::Approx(analytic).epsilon(0.05));
    }
}

TEST_CASE("une boîte trop courte plafonne la vitesse, une trop longue se traîne") {
    const Powertrain base = preset("hot_hatch_na");
    const double radius = wheelRadiusM(base.car);
    Powertrain shortBox = base;
    shortBox.gearbox.finalDrive = 8.0f;
    const Performance tooShort = measurePerformance(shortBox);
    const Gearing g = gearingOf(shortBox);
    // Le dernier rapport tape dans le rupteur bien avant que l'air n'arrête la voiture.
    const double atRedline = redlineRpmOf(base) * 2.0 * kPi / 60.0 * radius / g.overall[static_cast<std::size_t>(g.gears - 1)] * 3.6;
    CHECK(tooShort.topSpeedKmh == doctest::Approx(atRedline).epsilon(0.03));
    CHECK(tooShort.topSpeedKmh < measurePerformance(base).topSpeedKmh * 0.8);

    Powertrain longBox = base;
    longBox.gearbox.finalDrive = 1.6f;
    const Performance tooLong = measurePerformance(longBox);
    const Performance reference = measurePerformance(base);
    CHECK((tooLong.zeroTo100S == 0.0 || tooLong.zeroTo100S > reference.zeroTo100S * 1.3));
    CHECK(tooLong.topSpeedKmh < reference.topSpeedKmh);
}

TEST_CASE("en manuelle, une boîte très longue fait caler au démarrage ou en reprise") {
    Powertrain p = preset("city_14");
    p.gearbox.finalDrive = 1.3f;
    DrivingModel model = inGear(p, 2);
    bool stalled = model.stalled();
    for (int i = 0; i < 60 * 100 && !stalled; ++i) {
        model.stepPedal(0.01, 0.0, 0.3);
        stalled = model.stalled();
    }
    CHECK(stalled);
}

TEST_CASE("couple admissible dépassé : la boîte s'use, puis casse") {
    // Un V8 de 500 Nm sur une boîte de citadine, 170 Nm.
    Powertrain weak = preset("v8_cross_plane");
    weak.gearbox.limitNm = 170.0f;
    DrivingModel model(weak);
    model.setManual(true);
    model.requestShift(1);
    double most = 0.0, broke = -1.0;
    for (int i = 0; i < 20 * 1000 && broke < 0.0; ++i) {
        if (model.gear() < 3 && !model.shifting() && model.rpm() > 5000.0) model.requestShift(1);
        model.stepPedal(0.001, 1.0, 0.0);
        most = std::max(most, model.overload());
        if (model.broken()) broke = i * 0.001;
    }
    MESSAGE("surcharge " << most << " ; cassée après " << broke << " s");
    CHECK(most > 1.2);
    CHECK(broke > 0.0);
    CHECK(broke < 15.0);
    // Plus rien ne passe, on roule sur l'élan.
    const double speed = model.speedKmh();
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 1.0, 0.0);
    CHECK(model.speedKmh() < speed);
    CHECK_FALSE(model.meshed());
    model.requestShift(1);
    CHECK(model.gear() == 0);
    model.repairGearbox();
    CHECK_FALSE(model.broken());

    // Contre-épreuve : la boîte dimensionnée pour ce moteur tient.
    DrivingModel fine(preset("v8_cross_plane"));
    for (int i = 0; i < 25 * 100; ++i) fine.stepPedal(0.01, 1.0, 0.0);
    CHECK_FALSE(fine.broken());
    CHECK(fine.wear() < 0.05);
}

TEST_CASE("un module trop petit réduit ce que la boîte admet : la dent est plus faible") {
    Powertrain p = preset("hot_hatch_na");
    p.gearbox.limitNm = 400.0f;
    p.gearbox.moduleMm = 3.2f;
    const double solid = gearingOf(p).box.limitNm;
    p.gearbox.moduleMm = 1.2f;
    const double weak = gearingOf(p).box.limitNm;
    CHECK(solid == doctest::Approx(400.0));
    CHECK(weak < solid * 0.75);
    // Plus de dents pour la même taille : le sifflement monte d'autant.
    CHECK(gearingOf(p).box.pairs[0].driver >= 18);
}

TEST_CASE("le rendement décroît avec les étages : traction, propulsion, intégrale") {
    Powertrain front = preset("hot_hatch_na");
    front.car.drive = Drive::Front;
    Powertrain rear = front;
    rear.car.drive = Drive::Rear;
    Powertrain all = front;
    all.car.drive = Drive::All;
    const double f = gearingOf(front).box.meshEfficiency;
    const double r = gearingOf(rear).box.meshEfficiency;
    const double a = gearingOf(all).box.meshEfficiency;
    MESSAGE("rendements " << f << " " << r << " " << a);
    CHECK(f > r);
    CHECK(r > a);
    // Boîte manuelle barbotée : de 92 à 97 % (Joachim et al.) ; nos ponts ajoutent leur part.
    CHECK(f > 0.9);
    CHECK(a > 0.82);
}

// ── Les passages ──

TEST_CASE("le temps de synchronisation suit l'écart de vitesses et l'inertie") {
    // Mesuré sur le passage réel : même régime, deux écarts différents.
    const auto syncTime = [](int fromGear, double atRpm) {
        DrivingModel model(preset("hot_hatch_na"));
        model.setManual(true);
        for (int i = 0; i < 60000; ++i) {
            if (model.gear() < fromGear && !model.shifting() && (model.gear() == 0 || model.rpm() > 5000.0)) model.requestShift(1);
            model.stepPedal(0.001, model.gear() == fromGear ? 0.5 : 0.8, 0.0);
            // Embrayage collé : le primaire tourne bien à ce régime, pas seulement le moteur.
            if (model.gear() == fromGear && !model.shifting() && model.meshed() && model.inputRpm() > atRpm) break;
        }
        model.requestShift(1);
        double inSync = 0.0;
        for (int i = 0; i < 3000 && model.shifting(); ++i) {
            model.stepPedal(0.001, 0.5, 0.0);
            if (model.shiftStep() == 4) inSync += 0.001;
        }
        return inSync;
    };
    const double twoToThree = syncTime(2, 5000.0);
    const double fourToFive = syncTime(4, 5000.0);
    MESSAGE("synchronisation 2-3 : " << twoToThree << " s ; 4-5 : " << fourToFive << " s");
    CHECK(twoToThree > 0.03);
    CHECK(twoToThree < 0.5);
    // Même cône : écart de 29 % contre 22 % du régime, et même inertie. Le temps suit J·Δω / T.
    CHECK(twoToThree > fourToFive * 1.1);
    const double oneToFour = syncTime(3, 3000.0);
    CHECK(oneToFour < fourToFive * 1.5);
}

TEST_CASE("rétrograder sans débrayer gratte ; en débrayant, ça passe sans bruit") {
    const auto attempt = [](bool useClutch) {
        DrivingModel model = inGear(preset("hot_hatch_na"), 3);
        model.setClutchAssist(false);
        for (int i = 0; i < 300; ++i) model.stepPedal(0.001, 0.0, 0.0);
        model.setClutchPedal(useClutch ? 1.0 : 0.0);
        for (int i = 0; i < 300; ++i) model.stepPedal(0.001, 0.0, 0.0);
        model.requestShift(-1);
        double worst = 0.0;
        bool engaged = false;
        for (int i = 0; i < 1500; ++i) {
            model.stepPedal(0.001, 0.0, 0.0);
            worst = std::max(worst, model.grinding());
            if (model.meshed() && model.gear() == 2) engaged = true;
        }
        return std::make_pair(worst, engaged);
    };
    const auto [without, enteredWithout] = attempt(false);
    const auto [with, enteredWith] = attempt(true);
    MESSAGE("écart sous le baladeur : sans débrayer " << without << " rad/s ; en débrayant " << with);
    CHECK(without > 50.0);
    CHECK_FALSE(enteredWithout);
    CHECK(with == 0.0);
    CHECK(enteredWith);
}

TEST_CASE("à la montée, sans débrayer, il suffit d'attendre que le régime tombe : le rapport entre") {
    DrivingModel model = inGear(preset("hot_hatch_na"), 2);
    model.setClutchAssist(false);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.001, 0.0, 0.0);
    model.requestShift(1);
    bool engaged = false;
    for (int i = 0; i < 1500; ++i) {
        model.stepPedal(0.001, 0.0, 0.0);
        if (model.meshed() && model.gear() == 3) engaged = true;
    }
    CHECK(engaged);
}

TEST_CASE("rétrograder sans débrayer, mais en montant le régime au point mort : le rapport entre sans gratter") {
    DrivingModel model = inGear(preset("hot_hatch_na"), 3);
    model.setClutchAssist(false);
    for (int i = 0; i < 500; ++i) model.stepPedal(0.001, 0.15, 0.0);
    model.requestShift(-1);
    // Le moteur doit monter jusqu'au régime du nouveau rapport, pendant que le levier est au point mort.
    double worstGrind = 0.0;
    for (int i = 0; i < 1600 && !(model.meshed() && model.gear() == 2 && !model.shifting()); ++i) {
        const double wanted = model.outputRpm() * model.box().boxRatio[1];
        model.stepPedal(0.001, model.gear() == 2 && model.rpm() < wanted - 40.0 ? 1.0 : 0.0, 0.0);
        worstGrind = std::max(worstGrind, model.grinding());
    }
    CHECK(model.gear() == 2);
    CHECK(model.meshed());
    MESSAGE("grattement le plus fort : " << worstGrind << " rad/s");
    // Le cône seul ne peut pas freiner le moteur : on l'entend s'il dure, mais l'engagement finit sans abandon.
    CHECK(worstGrind < 250.0);
}

TEST_CASE("un rapport sous charge ne sort pas sans lever le pied") {
    DrivingModel model = inGear(preset("hot_hatch_na"), 2);
    model.setClutchAssist(false);
    model.requestShift(1);
    bool sawNeutral = false;
    for (int i = 0; i < 300; ++i) {
        model.stepPedal(0.001, 1.0, 0.0);
        sawNeutral = sawNeutral || !model.meshed();
    }
    CHECK_FALSE(sawNeutral);
}

TEST_CASE("une séquentielle passe en moins de 100 ms ; une manuelle prend son temps") {
    const auto duration = [](GearboxKind kind) {
        DrivingModel model = inGear(withBox("hot_hatch_na", kind, ToothForm::Spur, 400.0f), 3);
        for (int i = 0; i < 200; ++i) model.stepPedal(0.001, 1.0, 0.0);
        model.requestShift(1);
        int steps = 0;
        while (model.shifting() && steps < 3000) {
            model.stepPedal(0.001, 1.0, 0.0);
            ++steps;
        }
        return steps * 0.001;
    };
    const double sequential = duration(GearboxKind::Sequential);
    const double manual = duration(GearboxKind::Synchro);
    MESSAGE("séquentielle " << sequential << " s ; manuelle " << manual << " s");
    CHECK(sequential > 0.02);
    CHECK(sequential < 0.1);
    CHECK(manual > 0.15);
    CHECK(manual > sequential * 3.0);
}

TEST_CASE("deux embrayages : le couple ne s'interrompt pas pendant le passage") {
    const auto worstDecel = [](GearboxKind kind) {
        Powertrain p = withBox("v8_cross_plane", kind, ToothForm::Helical);
        p.car.drive = Drive::All;
        DrivingModel model = inGear(p, 2);
        for (int i = 0; i < 400; ++i) model.stepPedal(0.001, 1.0, 0.0);
        model.requestShift(1);
        double previous = model.speedKmh();
        double lost = 0.0;
        for (int i = 0; i < 1000; ++i) {
            model.stepPedal(0.001, 1.0, 0.0);
            if (model.shifting()) lost = std::max(lost, previous - model.speedKmh());
            previous = std::max(previous, model.speedKmh());
        }
        return std::make_pair(lost, previous);
    };
    const auto manual = worstDecel(GearboxKind::Synchro);
    const auto dual = worstDecel(GearboxKind::DualClutch);
    MESSAGE("vitesse perdue pendant le passage : manuelle " << manual.first << " km/h, deux embrayages " << dual.first);
    CHECK(dual.first < 0.05);
    CHECK(manual.first > dual.first);
    // Le passage sous couple arrive plus vite une seconde plus tard.
    CHECK(dual.second > manual.second);
}

TEST_CASE("le convertisseur fait décoller sans caler, en glissant, puis se ponte") {
    Powertrain p = withBox("hot_hatch_na", GearboxKind::Converter, ToothForm::Helical);
    DrivingModel model(p);
    double slipMax = 0.0;
    for (int i = 0; i < 3 * 1000; ++i) {
        model.stepPedal(0.001, 1.0, 0.0);
        if (model.meshed()) slipMax = std::max(slipMax, model.rpm() - model.inputRpm());
        REQUIRE_FALSE(model.stalled());
    }
    CHECK(slipMax > 1000.0);
    CHECK(model.speedKmh() > 15.0);
    for (int i = 0; i < 25 * 1000; ++i) model.stepPedal(0.001, 0.5, 0.0);
    CHECK(model.speedKmh() > 90.0);
    CHECK(std::fabs(model.rpm() - model.inputRpm()) < 150.0);
    CHECK(model.clutch() > 0.8);
    for (int i = 0; i < 20 * 1000; ++i) {
        model.stepPedal(0.001, 0.0, 0.5);
        REQUIRE_FALSE(model.stalled());
    }
    CHECK(model.rpm() > model.idleRpm() * 0.8);
}

TEST_CASE("le couple brusque fait osciller la chaîne : de 2 à 10 Hz selon le rapport") {
    const auto modeOf = [](int gear) {
        DrivingModel model = inGear(preset("hot_hatch_na"), gear);
        // On stabilise à bas régime, puis un coup de gaz franc.
        const double goal = 2000.0;
        for (int i = 0; i < 6000; ++i) {
            const double error = goal - model.rpm();
            model.stepPedal(0.001, std::clamp(0.1 + error * 0.002, 0.0, 0.5), error < -300.0 ? 0.2 : 0.0);
        }
        std::vector<double> rpm;
        for (int i = 0; i < 2500; ++i) {
            model.stepPedal(0.001, 1.0, 0.0);
            rpm.push_back(model.rpm());
        }
        // Tendance lente retirée, on compte les passages à zéro.
        std::vector<double> detrended(rpm.size(), 0.0);
        constexpr int kHalf = 150;
        for (std::size_t i = kHalf; i + kHalf < rpm.size(); ++i) {
            double sum = 0.0;
            for (int k = -kHalf; k <= kHalf; ++k) sum += rpm[static_cast<std::size_t>(static_cast<long>(i) + k)];
            detrended[i] = rpm[i] - sum / (2 * kHalf + 1);
        }
        int crossings = 0;
        std::size_t first = 0, last = 0;
        double amplitude = 0.0;
        for (std::size_t i = kHalf + 1; i + kHalf < rpm.size(); ++i) {
            if ((detrended[i - 1] < 0.0) != (detrended[i] < 0.0)) {
                if (crossings == 0) first = i;
                last = i;
                ++crossings;
            }
            amplitude = std::max(amplitude, std::fabs(detrended[i]));
        }
        const double hz = crossings > 1 ? (crossings - 1) / 2.0 / (static_cast<double>(last - first) * 0.001) : 0.0;
        return std::make_pair(hz, amplitude);
    };
    const auto second = modeOf(2);
    const auto third = modeOf(3);
    MESSAGE("mode : 2e " << second.first << " Hz (±" << second.second << " tr/min) ; 3e " << third.first << " Hz (±" << third.second << " tr/min)");
    // Une inertie vue par le rapport, un ressort : √(K / (J g²)) / 2π.
    const Powertrain p = preset("hot_hatch_na");
    const Gearing g = gearingOf(p);
    const double inertia = engineInertia(p) + g.box.inputInertia;
    const auto analytic = [&](int gear) {
        const double overall = g.overall[static_cast<std::size_t>(gear - 1)];
        return std::sqrt(6000.0 / (inertia * overall * overall * g.box.meshEfficiency)) / (2.0 * kPi);
    };
    MESSAGE("attendu : 2e " << analytic(2) << " Hz, 3e " << analytic(3) << " Hz");
    CHECK(second.first == doctest::Approx(analytic(2)).epsilon(0.35));
    CHECK(third.first == doctest::Approx(analytic(3)).epsilon(0.35));
    CHECK(third.first > second.first);
    CHECK(second.first > 2.0);
    CHECK(second.first < 8.0);
    CHECK(second.second > 15.0);
}

TEST_CASE("marche arrière : elle s'engage à l'arrêt et fait reculer ; refusée en roulant") {
    DrivingModel model(preset("city_14"));
    model.setManual(true);
    model.requestShift(-1);
    for (int i = 0; i < 500; ++i) model.stepPedal(0.001, 0.0, 0.0);
    CHECK(model.gear() == -1);
    CHECK(model.meshed());
    for (int i = 0; i < 4000; ++i) model.stepPedal(0.001, 0.3, 0.0);
    CHECK(model.speedKmh() > 5.0);
    CHECK_FALSE(model.stalled());
    // Elle n'est pas plus longue que la première : son rapport vient de ses dents.
    const Gearing g = gearingOf(preset("city_14"));
    CHECK(g.reverseOverall == doctest::Approx(g.box.reverseRatio * g.box.finalDrive));
    CHECK(g.box.reverseRatio > 2.5);
    CHECK(g.box.reverseRatio < 4.5);

    DrivingModel moving = inGear(preset("city_14"), 1);
    moving.requestShift(-1);
    for (int i = 0; i < 300; ++i) moving.stepPedal(0.001, 0.2, 0.0);
    moving.requestShift(-1);
    for (int i = 0; i < 300; ++i) moving.stepPedal(0.001, 0.2, 0.0);
    CHECK(moving.gear() >= 0);
}

TEST_CASE("toutes les boîtes font rouler tous les moteurs, sans erreur") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (GearboxKind kind : {GearboxKind::Synchro, GearboxKind::Dog, GearboxKind::Sequential, GearboxKind::DualClutch, GearboxKind::Converter}) {
            INFO(std::string(all[i].id) << " " << static_cast<int>(kind));
            Powertrain p = all[i].powertrain();
            p.gearbox.kind = kind;
            p.gearbox.form = kind == GearboxKind::Dog || kind == GearboxKind::Sequential ? ToothForm::Spur : ToothForm::Helical;
            DrivingModel model(p);
            double top = 0.0;
            for (int step = 0; step < 60 * 20; ++step) {
                model.step(0.05);
                REQUIRE(std::isfinite(model.rpm()));
                REQUIRE(std::isfinite(model.speedKmh()));
                REQUIRE(std::isfinite(model.inputRpm()));
                top = std::max(top, model.speedKmh());
            }
            CHECK(top > 80.0);
            CHECK(model.rpm() <= model.redlineRpm() + 300.0);
        }
    }
}

// ── Le bruit ──

TEST_CASE("le sifflement sort à dents × vitesse d'arbre, avec ses harmoniques") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    const GearboxSpec box = gearingOf(p).box;
    constexpr int kGear = 2;
    constexpr double kInputRpm = 3000.0;
    const std::vector<float> x = renderBox(box, steadyDrive(kGear, kInputRpm, box, 150.0), 2.0);
    const double mesh = box.pairs[kGear - 1].driver * kInputRpm / 60.0;
    CHECK(box.meshHz(kGear, kInputRpm) == doctest::Approx(mesh));
    const double line = db(powerAt(x, mesh, kRate / 4));
    // Entre deux raies, rien de comparable.
    const double between = 0.5 * (db(powerAt(x, mesh * 0.82, kRate / 4)) + db(powerAt(x, mesh * 1.18, kRate / 4)));
    MESSAGE("raie à " << mesh << " Hz : " << line - between << " dB au-dessus du voisinage");
    CHECK(line - between > 15.0);
    CHECK(db(powerAt(x, 2.0 * mesh, kRate / 4)) - db(powerAt(x, 2.25 * mesh, kRate / 4)) > 10.0);
    // Contre-épreuve : pas de raie là où il n'y a pas de dents.
    CHECK(line - db(powerAt(x, mesh * 0.73, kRate / 4)) > 12.0);
}

TEST_CASE("les défauts de chaque dent font des bandes latérales à la vitesse de l'arbre") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    const GearboxSpec box = gearingOf(p).box;
    constexpr int kGear = 3;
    constexpr double kInputRpm = 3000.0;
    const std::vector<float> x = renderBox(box, steadyDrive(kGear, kInputRpm, box, 150.0), 3.0);
    const double mesh = box.pairs[kGear - 1].driver * kInputRpm / 60.0;
    const double shaft = kInputRpm / 60.0;
    const double side = 0.5 * (db(powerAt(x, mesh + shaft, kRate / 4)) + db(powerAt(x, mesh - shaft, kRate / 4)));
    const double gap = 0.5 * (db(powerAt(x, mesh + shaft * 0.5, kRate / 4)) + db(powerAt(x, mesh - shaft * 0.5, kRate / 4)));
    const double line = db(powerAt(x, mesh, kRate / 4));
    MESSAGE("raie " << line << " dB, bande latérale " << side << " dB, entre les deux " << gap << " dB");
    CHECK(side - gap > 8.0);
    CHECK(side < line);
    CHECK(side > line - 30.0);
}

TEST_CASE("sur une vraie raie, le niveau bat : ce n'est pas une sinusoïde") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    const GearboxSpec box = gearingOf(p).box;
    constexpr int kGear = 3;
    constexpr double kInputRpm = 3000.0;
    const std::vector<float> x = renderBox(box, steadyDrive(kGear, kInputRpm, box, 150.0), 8.0);
    const double mesh = box.pairs[kGear - 1].driver * kInputRpm / 60.0;
    // Mesuré sur de vraies marches arrière : de 2,6 à 4,5 dB d'une fenêtre de 46 ms à l'autre.
    std::vector<double> levels;
    for (std::size_t from = kRate / 2; from + 2048 < x.size(); from += 1024) levels.push_back(db(powerAt(x, mesh, from, 2048)));
    double mean = 0.0, variance = 0.0;
    for (double l : levels) mean += l;
    mean /= static_cast<double>(levels.size());
    for (double l : levels) variance += (l - mean) * (l - mean);
    const double spread = std::sqrt(variance / static_cast<double>(levels.size()));
    MESSAGE("battement de la raie : " << spread << " dB");
    CHECK(spread > 1.5);
    CHECK(spread < 9.0);
}

TEST_CASE("denture droite : nettement plus fort qu'en hélicoïdale, à couple et vitesse égaux") {
    Powertrain helical = withBox("hot_hatch_na", GearboxKind::Synchro, ToothForm::Helical, 400.0f);
    Powertrain spur = helical;
    spur.gearbox.form = ToothForm::Spur;
    const GearboxSpec a = gearingOf(helical).box;
    const GearboxSpec b = gearingOf(spur).box;
    constexpr int kGear = 3;
    const double soft = rmsOf(renderBox(a, steadyDrive(kGear, 3000.0, a, 120.0), 2.0), kRate / 4);
    const double loud = rmsOf(renderBox(b, steadyDrive(kGear, 3000.0, b, 120.0), 2.0), kRate / 4);
    const double gap = 20.0 * std::log10(loud / soft);
    MESSAGE("droit/hélicoïdal : " << gap << " dB");
    // NASA TM-106511 : de 7 à 17 dB, jusqu'à 22 selon le rapport de conduite. Le rapport de conduite total les explique.
    CHECK(gap > 7.0);
    CHECK(gap < 28.0);
    // Contre-épreuve de la cause : la formule de Velex donne le zéro des rapports de conduite entiers.
    const GearPair& pair = a.pairs[kGear - 1];
    CHECK(pair.overlap > 0.8);
    CHECK(b.pairs[kGear - 1].overlap == 0.0f);
    CHECK(pair.profile + pair.overlap > b.pairs[kGear - 1].profile + 0.5f);
}

TEST_CASE("le rapport de conduite décide : un recouvrement entier efface l'engrènement") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Synchro, ToothForm::Helical, 400.0f);
    GearboxSpec box = gearingOf(p).box;
    const double mesh = box.pairs[2].driver * 3000.0 / 60.0;
    // Mêmes dents, même charge : seul le recouvrement change. La formule de Velex donne Sinc(1) = 0, et Sinc(0,5) = 0,64.
    const auto lineWith = [&box, mesh](float overlap) {
        box.pairs[2].overlap = overlap;
        return db(powerAt(renderBox(box, steadyDrive(3, 3000.0, box, 40.0), 3.0), mesh, kRate / 4));
    };
    const double half = lineWith(0.5f);
    const double whole = lineWith(1.0f);
    MESSAGE("εβ = 0,5 : " << half << " dB ; εβ = 1 : " << whole << " dB");
    CHECK(half > whole + 8.0);
    // Mais pas de silence : de vraies dents ont des défauts, qui comblent le zéro.
    CHECK(whole > 0.0);
}

TEST_CASE("plus d'hélice, moins de raie") {
    Powertrain p = withBox("hot_hatch_na", GearboxKind::Synchro, ToothForm::Helical, 400.0f);
    const auto lineFor = [&p](float helix) {
        p.gearbox.helixDeg = helix;
        const GearboxSpec box = gearingOf(p).box;
        const std::vector<float> x = renderBox(box, steadyDrive(3, 3000.0, box, 120.0), 2.0);
        return std::make_pair(db(powerAt(x, box.pairs[2].driver * 50.0, kRate / 4)), box.pairs[2].overlap);
    };
    const auto low = lineFor(8.0f);
    const auto high = lineFor(32.0f);
    MESSAGE("εβ " << low.second << " : " << low.first << " dB ; εβ " << high.second << " : " << high.first << " dB");
    CHECK(low.first > high.first + 5.0);
}

TEST_CASE("le couple ne fait pas tout : un minimum vers le couple de conception, plus fort à côté") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    const GearboxSpec box = gearingOf(p).box;
    const double mesh = box.pairs[2].driver * 3000.0 / 60.0;
    // Sur plusieurs secondes : la raie bat, sa moyenne seule compte ici.
    const auto level = [&box, mesh](double torque) { return db(powerAt(renderBox(box, steadyDrive(3, 3000.0, box, torque), 4.0), mesh, kRate / 4)); };
    const double light = level(40.0);
    const double design = level(200.0);
    const double heavy = level(380.0);
    MESSAGE("raie : 40 Nm " << light << " dB, 200 Nm " << design << " dB, 380 Nm " << heavy << " dB");
    // NASA TM-105331 : pas de tendance générale, un minimum près du couple pour lequel le profil est corrigé.
    CHECK(design < light);
    CHECK(design < heavy);
    // Au repos, plus rien : les dents ne s'appuient plus.
    CHECK(level(0.0) < light - 20.0);
}

TEST_CASE("débrayé, un coup de gaz ne fait pas monter le sifflement : il suit l'arbre, pas le moteur") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Synchro, ToothForm::Spur, 400.0f);
    DrivingModel model = inGear(p, 3);
    model.setClutchAssist(false);
    model.setClutchPedal(1.0);
    for (int i = 0; i < 500; ++i) model.stepPedal(0.001, 0.0, 0.0);
    const double inputBefore = model.inputRpm();
    const double engineBefore = model.rpm();
    double engineTop = 0.0, inputTop = 0.0;
    for (int i = 0; i < 400; ++i) {
        model.stepPedal(0.001, 1.0, 0.0);
        engineTop = std::max(engineTop, model.rpm());
        inputTop = std::max(inputTop, model.inputRpm());
    }
    MESSAGE("moteur " << engineBefore << " → " << engineTop << " tr/min ; primaire " << inputBefore << " → " << inputTop);
    CHECK(engineTop > engineBefore * 1.5);
    // Le primaire ne suit que les roues, à quelques tours près.
    CHECK(inputTop < inputBefore * 1.02);

    // Le bruit ne dépend pas du moteur : débrayé, le niveau reste le même, coup de gaz ou non.
    const auto level = [&p](bool blip, bool clutchOpen) {
        DrivingModel car = inGear(p, 3);
        car.setClutchAssist(false);
        car.setClutchPedal(clutchOpen ? 1.0 : 0.0);
        for (int i = 0; i < 400; ++i) car.stepPedal(0.001, clutchOpen ? 0.0 : 0.3, 0.0);
        const std::vector<float> x = driveBox(car, 1.0, [&car, blip](double, double dt) { car.stepPedal(dt, blip ? 1.0 : 0.0, 0.0); });
        return rmsOf(x, kRate / 4);
    };
    const double still = level(false, true);
    const double blipped = level(true, true);
    const double engaged = level(true, false);
    MESSAGE("débrayé " << still << " ; débrayé, coup de gaz " << blipped << " ; embrayé, pied au plancher " << engaged);
    CHECK(blipped == doctest::Approx(still).epsilon(0.2));
    // Contre-épreuve : embrayé, ce même coup de gaz charge les dents, et ça s'entend.
    CHECK(engaged > still * 4.0);
}

TEST_CASE("quand l'embrayage patine, la raie suit l'arbre primaire, pas le moteur") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Synchro, ToothForm::Spur, 400.0f);
    DrivingModel model(p);
    model.setManual(true);
    model.requestShift(1);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.001, 0.0, 0.0);
    // Un démarrage : le moteur prend ses tours, le primaire part de zéro avec les roues.
    for (int i = 0; i < 600; ++i) model.stepPedal(0.001, 1.0, 0.0);
    const double engineRpm = model.rpm();
    const double inputRpm = model.inputRpm();
    REQUIRE(engineRpm > inputRpm * 1.5);
    const GearboxSpec box = model.box();
    GearboxDrive drive = model.gearboxDrive();
    // On fige l'instant : mêmes vitesses, même couple, on écoute où est l'énergie.
    drive.acyclism = 0.0f;
    const std::vector<float> x = renderBox(box, drive, 1.5);
    const double atInput = db(powerAt(x, box.meshHz(1, inputRpm), kRate / 4));
    const double atEngine = db(powerAt(x, box.meshHz(1, engineRpm), kRate / 4));
    MESSAGE("moteur " << engineRpm << " tr/min, primaire " << inputRpm << " tr/min : raie " << atInput << " dB, à la place du moteur " << atEngine << " dB");
    CHECK(atInput > atEngine + 15.0);
}

TEST_CASE("embrayé au point mort, ça claque ; débrayé, ça se tait") {
    const Powertrain p = preset("city_14");
    DrivingModel model(p);
    for (int i = 0; i < 1000; ++i) model.stepNeutral(0.001, 0.0);
    const std::vector<float> x = driveBox(model, 9.0, [&model](double t, double dt) {
        model.setClutchPedal(t > 3.0 && t < 6.0 ? 1.0 : 0.0);
        model.stepNeutral(dt, 0.0);
    });
    const std::vector<float> high = above(x, 1000.0f);
    const double engaged = rmsOf(high, kRate * 1, kRate * 2);
    const double released = rmsOf(high, kRate * 4, kRate * 2);
    const double back = rmsOf(high, kRate * 7, kRate * 2);
    MESSAGE("au-dessus de 1 kHz : embrayé " << 20.0 * std::log10(engaged) << " dB, débrayé " << 20.0 * std::log10(released) << " dB, de nouveau embrayé " << 20.0 * std::log10(back) << " dB");
    CHECK(engaged > released * 10.0);
    CHECK(back > released * 10.0);
    CHECK(back == doctest::Approx(engaged).epsilon(0.5));
}

TEST_CASE("le claquement a ses chocs en phase avec les explosions") {
    const Powertrain p = preset("city_14");
    DrivingModel model(p);
    for (int i = 0; i < 1000; ++i) model.stepNeutral(0.001, 0.0);
    // Le son reçoit la position dans le cycle : on replie son enveloppe sur cette position, explosion par explosion.
    GearboxSound sound(kRate);
    sound.setup(model.box());
    const int firings = model.powertrainCylinders();
    constexpr int kBins = 16;
    std::array<double, kBins> folded{};
    std::array<int, kBins> counts{};
    dsp::Svf high;
    high.set(1500.0f, 0.707f, kRate);
    float phase = 0.0f, follow = 0.0f;
    const double dt = static_cast<double>(kTick) / kRate;
    for (int block = 0; block < 4 * kRate / kTick; ++block) {
        model.stepNeutral(dt, 0.0);
        sound.setDrive(model.gearboxDrive());
        const float inc = static_cast<float>(model.rpm()) / 120.0f / kRate;
        for (int i = 0; i < kTick; ++i) {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            const float y = sound.sample(phase);
            high.process(y);
            follow += (std::fabs(y - high.lp - high.k * high.bp) - follow) * 0.02f;
            if (block > kRate / kTick) {
                const float within = phase * static_cast<float>(firings);
                const int bin = static_cast<int>((within - std::floor(within)) * kBins) % kBins;
                folded[static_cast<std::size_t>(bin)] += follow;
                ++counts[static_cast<std::size_t>(bin)];
            }
        }
    }
    double mean = 0.0, top = 0.0;
    for (int i = 0; i < kBins; ++i) {
        const double value = folded[static_cast<std::size_t>(i)] / std::max(counts[static_cast<std::size_t>(i)], 1);
        mean += value / kBins;
        top = std::max(top, value);
    }
    MESSAGE("sommet de l'enveloppe, replié sur les explosions : " << top / mean << " fois sa moyenne");
    // Rust : les chocs tombent là où l'accélération du primaire est la plus forte. Sans cette phase, l'enveloppe repliée est plate.
    CHECK(top > mean * 1.15);
}

TEST_CASE("le claquement obéit au seuil de Rust : ce qu'il faut, c'est de l'acyclisme") {
    Powertrain p = preset("city_14");
    const auto metal = [&p](Flywheel flywheel, double rpmPedal) {
        p.gearbox.flywheel = flywheel;
        DrivingModel model(p);
        for (int i = 0; i < 1000; ++i) model.stepNeutral(0.001, rpmPedal);
        const std::vector<float> x = driveBox(model, 2.0, [&model, rpmPedal](double, double dt) { model.stepNeutral(dt, rpmPedal); });
        return rmsOf(above(x, 1000.0f), kRate / 2);
    };
    const double single = metal(Flywheel::Single, 0.0);
    const double dual = metal(Flywheel::DualMass, 0.0);
    MESSAGE("volant simple " << 20.0 * std::log10(single) << " dB, double volant " << 20.0 * std::log10(dual) << " dB");
    // Schaeffler : avec un double volant, plus de claquement.
    CHECK(single > dual * 8.0);
    // Un V8 tourne trop rond au ralenti pour secouer les roues folles, un diesel à volant simple claque.
    const auto rattleOf = [](const char* id, Flywheel flywheel) {
        Powertrain q = preset(id);
        q.gearbox.flywheel = flywheel;
        DrivingModel model(q);
        for (int i = 0; i < 1000; ++i) model.stepNeutral(0.001, 0.0);
        return std::make_pair(model.gearboxDrive().acyclism, rmsOf(above(driveBox(model, 2.0, [&model](double, double dt) { model.stepNeutral(dt, 0.0); }), 1000.0f), kRate / 2));
    };
    const auto diesel = rattleOf("diesel_four", Flywheel::Single);
    const auto v12 = rattleOf("v12", Flywheel::Single);
    MESSAGE("acyclisme : diesel " << diesel.first << ", V12 " << v12.first << " rad/s²");
    CHECK(diesel.first > v12.first * 5.0f);
    CHECK(diesel.second > v12.second * 3.0);
}

TEST_CASE("une boîte à convertisseur ne claque pas : le fluide isole") {
    Powertrain p = withBox("city_14", GearboxKind::Converter, ToothForm::Helical);
    DrivingModel model(p);
    for (int i = 0; i < 1000; ++i) model.stepNeutral(0.001, 0.0);
    CHECK(model.gearboxDrive().acyclism == 0.0f);
}

TEST_CASE("le grattement sort à l'écart de vitesses et s'arrête avec lui") {
    Powertrain p = preset("hot_hatch_na");
    const GearboxSpec box = gearingOf(p).box;
    GearboxDrive grind = steadyDrive(0, 3000.0, box, 0.0);
    grind.outputOmega = 200.0f;
    const std::vector<float> silent = renderBox(box, grind, 1.5);
    grind.grind = 150.0f;
    const std::vector<float> loud = renderBox(box, grind, 1.5);
    const std::vector<float> hi = above(loud, 800.0f);
    const std::vector<float> lo = above(silent, 800.0f);
    const double gain = 20.0 * std::log10(rmsOf(hi, kRate / 4) / rmsOf(lo, kRate / 4));
    MESSAGE("grattement : +" << gain << " dB");
    CHECK(gain > 15.0);
}

TEST_CASE("un choc d'engagement de crabots s'entend bien plus qu'un engagement synchronisé") {
    const auto loudest = [](GearboxKind kind) {
        DrivingModel model = inGear(withBox("hot_hatch_na", kind, ToothForm::Spur, 400.0f), 3);
        for (int i = 0; i < 300; ++i) model.stepPedal(0.001, 1.0, 0.0);
        double before = 0.0;
        model.requestShift(1);
        for (int i = 0; i < 1500; ++i) model.stepPedal(0.001, 1.0, 0.0);
        before = model.gearboxDrive().impacts;
        return before;
    };
    const double dog = loudest(GearboxKind::Sequential);
    const double synchro = loudest(GearboxKind::Synchro);
    MESSAGE("énergie des chocs : crabots " << dog << " J ; synchro " << synchro << " J");
    CHECK(dog > synchro * 10.0);
}

TEST_CASE("la voiture électrique siffle aussi : deux étages hélicoïdaux") {
    const Powertrain p = preset("electric");
    const Gearing g = gearingOf(p);
    CHECK(g.gears == 1);
    CHECK(g.box.kind == GearboxKind::Reducer);
    CHECK(g.overall[0] == doctest::Approx(g.box.boxRatio[0] * g.box.finalDrive));
    DrivingModel model(p);
    for (int i = 0; i < 300; ++i) model.stepPedal(0.01, 0.8, 0.0);
    const std::vector<float> x = driveBox(model, 1.0, [&model](double, double dt) { model.stepPedal(dt, 0.8, 0.0); });
    const double inputRpm = model.inputRpm();
    CHECK(inputRpm > 2000.0);
    CHECK(rmsOf(x, kRate / 4) > 1e-5);
}

// ── Les contraintes du son ──

TEST_CASE("sans setGearbox, la boîte est muette et le son du moteur inchangé") {
    const Powertrain p = preset("city_14");
    const auto render = [&p](bool withBoxCall) {
        EngineSound sound(kRate);
        sound.setProfile(p.profile());
        sound.setVolume(0.2f);
        GearboxDrive still;
        const GearboxSpec box = gearingOf(p).box;
        sound.setTargetRpm(3000.0f);
        std::vector<float> out(kRate);
        for (std::size_t done = 0; done < out.size(); done += kTick) {
            if (withBoxCall) sound.setGearbox(box, still);
            sound.render(out.data() + done, static_cast<int>(std::min<std::size_t>(kTick, out.size() - done)));
        }
        return out;
    };
    const std::vector<float> a = render(false);
    const std::vector<float> b = render(true);
    // Rien qui tourne, rien qui frappe : le silence exact, et aucun tirage en commun avec le moteur.
    int different = 0;
    for (std::size_t i = 0; i < a.size(); ++i) different += a[i] != b[i] ? 1 : 0;
    CHECK(different == 0);
    GearboxSound lone(kRate);
    lone.setup(gearingOf(p).box);
    for (int i = 0; i < 1000; ++i) REQUIRE(lone.sample(0.5f) == 0.0f);
}

TEST_CASE("le bruit de boîte a ses propres tirages : ceux du moteur ne bougent pas") {
    const Powertrain p = preset("city_14");
    const auto render = [&p](bool withBox) {
        EngineSound sound(kRate);
        sound.setProfile(p.profile());
        // Assez bas pour que le limiteur reste à l'écart : les sources s'additionnent alors exactement.
        sound.setVolume(0.05f);
        const GearboxSpec box = gearingOf(p).box;
        const GearboxDrive drive = steadyDrive(3, 3000.0, box, 80.0);
        sound.setTargetRpm(3000.0f);
        std::vector<float> out(kRate * 2);
        for (std::size_t done = 0; done < out.size(); done += kTick) {
            if (withBox) sound.setGearbox(box, drive);
            sound.render(out.data() + done, static_cast<int>(std::min<std::size_t>(kTick, out.size() - done)));
        }
        return out;
    };
    const std::vector<float> without = render(false);
    const std::vector<float> with = render(true);
    std::vector<float> difference(with.size());
    for (std::size_t i = 0; i < with.size(); ++i) difference[i] = with[i] - without[i];
    // Si les tirages du moteur avaient bougé, la différence serait de l'ordre du moteur lui-même.
    const double engine = rmsOf(without, kRate);
    const double extra = rmsOf(difference, kRate);
    MESSAGE("moteur " << engine << ", boîte " << extra);
    CHECK(extra > 0.0);
    CHECK(extra < engine);
    // Le moteur sans boîte reste une part fidèle : corrélation quasi nulle avec la boîte.
    double cross = 0.0, a2 = 0.0, b2 = 0.0;
    for (std::size_t i = kRate; i < with.size(); ++i) {
        cross += static_cast<double>(without[i]) * difference[i];
        a2 += static_cast<double>(without[i]) * without[i];
        b2 += static_cast<double>(difference[i]) * difference[i];
    }
    CHECK(std::fabs(cross) / std::sqrt(a2 * b2) < 0.1);
}

TEST_CASE("le résultat de la boîte ne dépend pas de la taille des blocs") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    const GearboxSpec box = gearingOf(p).box;
    GearboxDrive drive = steadyDrive(2, 2500.0, box, 100.0);
    drive.acyclism = 1200.0f;
    drive.impacts = 3.0;
    drive.grind = 80.0f;
    drive.coneSlip = 20.0f;
    drive.coneTorque = 10.0f;
    // Pas de multiples du pas de contrôle interne (32 échantillons) : c'est là qu'une dépendance se cacherait.
    const std::vector<float> small = renderBox(box, drive, 0.5, 47);
    const std::vector<float> large = renderBox(box, drive, 0.5, 512);
    int different = 0;
    for (std::size_t i = 0; i < small.size(); ++i) different += small[i] != large[i] ? 1 : 0;
    CHECK(different == 0);
}

TEST_CASE("le bruit de boîte n'alloue pas, et ne casse pas le temps réel") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Sequential, ToothForm::Spur, 400.0f);
    DrivingModel model(p);
    EngineSound sound(kRate);
    sound.setProfile(p.profile());
    sound.setTargetRpm(3000.0f);
    std::vector<float> buffer(kTick);
    for (int i = 0; i < 200; ++i) {
        model.stepPedal(0.003, 1.0, 0.0);
        sound.setGearbox(model.box(), model.gearboxDrive());
        sound.render(buffer.data(), kTick);
    }
    const long before = g_allocations.load();
    for (int i = 0; i < 400; ++i) {
        sound.setGearbox(model.box(), model.gearboxDrive());
        sound.render(buffer.data(), kTick);
    }
    CHECK(g_allocations.load() == before);
}

TEST_CASE("deux rendus de la boîte sont identiques : déterminisme") {
    const Powertrain p = withBox("hot_hatch_na", GearboxKind::Dog, ToothForm::Spur, 400.0f);
    const auto render = [&p] {
        DrivingModel model(p);
        return driveBox(model, 3.0, [&model](double t, double dt) { model.stepPedal(dt, t > 0.2 ? 1.0 : 0.0, 0.0); });
    };
    const std::vector<float> a = render();
    const std::vector<float> b = render();
    int different = 0;
    for (std::size_t i = 0; i < a.size(); ++i) different += a[i] != b[i] ? 1 : 0;
    CHECK(different == 0);
}

// Une roue posée sur son flanc par la traînée ne fait aucun choc. Compter ce contact au repos comme des chocs
// laissait un claquement constant jusqu'au rupteur, à l'oreille et sur la vue de la boîte.
TEST_CASE("le claquement retombe à haut régime et à pleine charge") {
    constexpr int kBlock = 128;
    constexpr int kSampleRate = 48000;
    const double dt = static_cast<double>(kBlock) / kSampleRate;
    const Powertrain powertrain = findPreset("four_cyl_sport")->powertrain();
    std::vector<float> block(kBlock);
    const auto feed = [&](DrivingModel& model, EngineSound& sound) {
        sound.setTargetRpm(static_cast<float>(model.rpm()));
        sound.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking());
        sound.setRoad(static_cast<float>(model.speedKmh() / 3.6), static_cast<float>(model.wheelSlip()));
        sound.setGearbox(model.box(), model.gearboxDrive());
        sound.render(block.data(), kBlock);
    };
    DrivingModel idling(powertrain);
    EngineSound idleSound(kSampleRate);
    idleSound.setProfile(powertrain.profile());
    idleSound.setVolume(0.7f);
    double idleStrike = 0.0;
    int idleCount = 0;
    for (int i = 0; i < kSampleRate * 8 / kBlock; ++i) {
        idling.stepNeutral(dt, 0.0);
        feed(idling, idleSound);
        if (i > kSampleRate * 3 / kBlock) {
            idleStrike += idleSound.gearboxStrike();
            ++idleCount;
        }
    }
    idleStrike /= idleCount;

    DrivingModel pulling(powertrain);
    EngineSound pullSound(kSampleRate);
    pullSound.setProfile(powertrain.profile());
    pullSound.setVolume(0.7f);
    double highStrike = 0.0;
    int highCount = 0;
    for (int i = 0; i < kSampleRate * 25 / kBlock; ++i) {
        pulling.stepPedal(dt, 1.0, 0.0);
        feed(pulling, pullSound);
        if (pulling.gear() > 0 && pulling.rpm() > 5500.0 && !pulling.shifting()) {
            highStrike += pullSound.gearboxStrike();
            ++highCount;
        }
    }
    REQUIRE(highCount > 0);
    highStrike /= highCount;
    MESSAGE("ralenti " << idleStrike << ", plein gaz à haut régime " << highStrike);
    CHECK(idleStrike > 5.0);
    CHECK(highStrike < idleStrike * 0.2);
}
