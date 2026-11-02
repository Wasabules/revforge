#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "dsp.hpp"
#include "intake_path.hpp"
#include "revforge/air_filter.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/sound_sheet.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

Powertrain natural(AirFilter filter = AirFilter::Stock, int redline = 6800) {
    Powertrain p;
    p.engine.cylinders = 4;
    p.engine.displacementCc = 2000;
    p.engine.boreMm = 84;
    p.engine.redlineRpm = redline;
    p.car.body = Body::Compact;
    p.car.massKg = 1300;
    p.car.rimInch = 17;
    p.engine.airFilter = filter;
    return p;
}

Powertrain charged(AirFilter filter = AirFilter::Stock) {
    Powertrain p = natural(filter);
    p.engine.aspiration = Aspiration::Turbo;
    p.engine.boostBar = 1.3f;
    return p;
}

Powertrain withFilter(Powertrain p, AirFilter filter) {
    p.engine.airFilter = filter;
    return p;
}

IntakeGeometry geometryOf(const Powertrain& p) {
    IntakeGeometry g;
    g.displacementM3 = static_cast<double>(p.engine.displacementCc) * 1e-6;
    g.stackM = stackLengthM(p.engine);
    g.stackRadiusM = stackRadiusM(p.engine);
    return g;
}

/** Énergie d'une bande, sur un moteur tenu à ce régime et cette charge. */
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
            const double hz = static_cast<double>(k) * kRate / kSize;
            if (hz < fromHz || hz >= toHz) continue;
            sum += static_cast<double>(re[static_cast<std::size_t>(k)]) * re[static_cast<std::size_t>(k)] + static_cast<double>(im[static_cast<std::size_t>(k)]) * im[static_cast<std::size_t>(k)];
        }
    }
    return sum;
}

Samples held(const Powertrain& p, Listener listener, float rpm, float load, float seconds = 2.0f) {
    EngineSound sound(kRate);
    sound.setListener(listener);
    sound.setProfile(p.profile());
    sound.setVolume(0.7f);
    sound.setTargetRpm(rpm);
    sound.setDrive(load, 0.0f, false, false);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    sound.render(out.data(), static_cast<int>(out.size()));
    return slice(out, static_cast<std::size_t>(0.8f * kRate), out.size());
}

}  // namespace

TEST_CASE("la perte du filtre suit le carré du débit, et se classe du papier aux cornets") {
    const Powertrain paper = natural();
    const double top = powerSummary(paper).peakPowerRpm;
    // Ordre de grandeur : quelques kPa au nominal sur un filtre d'origine propre.
    CHECK(intakeDropPa(paper, top) > 1500.0);
    CHECK(intakeDropPa(paper, top) < 4000.0);
    CHECK(intakeDropPa(withFilter(paper, AirFilter::Panel), top) < intakeDropPa(paper, top));
    CHECK(intakeDropPa(withFilter(paper, AirFilter::Cone), top) < intakeDropPa(withFilter(paper, AirFilter::Panel), top));
    CHECK(intakeDropPa(withFilter(paper, AirFilter::Stacks), top) < 0.25 * intakeDropPa(withFilter(paper, AirFilter::Cone), top));
    // Le débit double à peu près de 2500 à 5000 tr/min, à respiration voisine : la perte quadruple.
    const double ratio = intakeDropPa(paper, 5000.0) / intakeDropPa(paper, 2500.0);
    CHECK(ratio > 3.5);
    CHECK(ratio < 6.0);
    // Une grosse cylindrée qui tourne haut y perd plus : sa boîte ne grandit pas autant que son débit.
    Powertrain big = paper;
    big.engine.cylinders = 8;
    big.engine.layout = Layout::CrossPlane;
    big.engine.displacementCc = 5000;
    big.engine.boreMm = 92;
    CHECK(intakeDropPa(big, powerSummary(big).peakPowerRpm) > 1.5 * intakeDropPa(paper, top));
}

TEST_CASE("un panneau sport gagne peu, et d'autant plus que le débit est grand") {
    const Powertrain paper = natural();
    const Powertrain panel = withFilter(paper, AirFilter::Panel);
    const double low = torqueCurveNm(panel, 2000.0) / torqueCurveNm(paper, 2000.0) - 1.0;
    const double high = torqueCurveNm(panel, 6000.0) / torqueCurveNm(paper, 6000.0) - 1.0;
    CHECK(low >= 0.0);
    CHECK(high > low);
    CHECK(high < 0.015);  // pas de miracle : moins de 1,5 %
    // Sur un moteur qui tourne haut, le même panneau rapporte plus.
    const Powertrain revvy = natural(AirFilter::Stock, 9000);
    const double revvyHigh = torqueCurveNm(withFilter(revvy, AirFilter::Panel), 8000.0) / torqueCurveNm(revvy, 8000.0) - 1.0;
    CHECK(revvyHigh > high);
}

TEST_CASE("le cône perd de la densité : son gain n'est pas gratuit") {
    const Powertrain paper = natural();
    const Powertrain cone = withFilter(paper, AirFilter::Cone);
    CHECK(intakeAirK(paper, 5000.0) == doctest::Approx(293.0));
    CHECK(intakeAirK(cone, 5000.0) > 298.0);
    // Plus chaud en haut : la chaleur rejetée suit la puissance plus vite que la ventilation.
    CHECK(intakeAirK(cone, 6000.0) > intakeAirK(cone, 2000.0));
    // Plus chaud au centre : l'échappement y est enfermé.
    Powertrain middle = cone;
    middle.car.placement = EnginePlacement::Mid;
    CHECK(intakeAirK(middle, 5000.0) > intakeAirK(cone, 5000.0));
    // À un régime où il ne gagne presque rien sur la perte, il perd sur la densité.
    CHECK(torqueCurveNm(cone, 2500.0) < torqueCurveNm(paper, 2500.0));
    // Un effet réaliste reste petit.
    const double top = powerSummary(cone).peakPowerKw / powerSummary(paper).peakPowerKw;
    CHECK(top > 0.93);
    CHECK(top < 1.03);
    // Et sans la température, il gagnerait : c'est bien elle qui coûte.
    const double gainWithoutHeat = (101325.0 - intakeDropPa(cone, 6000.0)) / (101325.0 - intakeDropPa(paper, 6000.0));
    CHECK(gainWithoutHeat > 1.0);
}

TEST_CASE("le cornet a sa résonance de quart d'onde, et accorde le remplissage") {
    const Powertrain p = natural(AirFilter::Stacks);
    const IntakeGeometry g = geometryOf(p);
    const double expected = stackResonanceHz(p.engine);
    CHECK(expected == doctest::Approx(343.0 / (4.0 * (g.stackM + 0.61 * g.stackRadiusM))));
    double bestHz = 0.0, best = -1000.0;
    for (double hz = 100.0; hz < 500.0; hz += 2.0) {
        const double db = IntakePath::pathDb(AirFilter::Stacks, g, hz);
        if (db > best) {
            best = db;
            bestHz = hz;
        }
    }
    CHECK(bestHz > 0.85 * expected);
    CHECK(bestHz < 1.15 * expected);
    // Plus long, plus grave.
    IntakeGeometry longer = g;
    longer.stackM = 1.5 * g.stackM;
    double longBestHz = 0.0;
    best = -1000.0;
    for (double hz = 100.0; hz < 500.0; hz += 2.0) {
        const double db = IntakePath::pathDb(AirFilter::Stacks, longer, hz);
        if (db > best) {
            best = db;
            longBestHz = hz;
        }
    }
    CHECK(longBestHz < 0.8 * bestHz);
    // Le remplissage culmine là où on a accordé le tube, à 72 % du régime maximal.
    const double tuned = 0.72 * static_cast<double>(p.engine.redlineRpm);
    CHECK(stackRamGain(p.engine, tuned) > 0.025);
    CHECK(stackRamGain(p.engine, 1.4 * tuned) < 0.0);
}

TEST_CASE("derrière un filtre plus libre, le turbo tourne moins vite pour la même pression, et elle vient plus tôt") {
    const Powertrain paper = charged();
    const Powertrain panel = charged(AirFilter::Panel);
    const double rated = 6000.0;
    const TurboSteady stock = turboSteady(turboSpecOf(paper), rated, breathingOf(paper, rated));
    const TurboSteady freer = turboSteady(turboSpecOf(panel), rated, breathingOf(panel, rated));
    CHECK(freer.chargePa == doctest::Approx(stock.chargePa).epsilon(1e-3));
    CHECK(freer.shaftRpm < stock.shaftRpm);
    // Là où l'énergie manque, moins de perte à l'entrée donne plus de pression.
    const TurboSteady lowStock = turboSteady(turboSpecOf(paper), 2200.0, breathingOf(paper, 2200.0));
    const TurboSteady lowStacks = turboSteady(turboSpecOf(charged(AirFilter::Stacks)), 2200.0, breathingOf(paper, 2200.0));
    CHECK(lowStacks.chargePa >= lowStock.chargePa);
    // De l'air chaud : plus de travail pour le même rapport, donc l'arbre tourne plus vite à perte égale.
    const TurboSpec hot = turboSpecOf(charged(AirFilter::Cone));
    TurboSpec cool = hot;
    cool.inletFilter = AirFilter::Stock;
    CHECK(turboSteady(hot, rated, breathingOf(paper, rated)).shaftRpm > turboSteady(cool, rated, breathingOf(paper, rated)).shaftRpm);
    // Une charge plus chaude, à pression égale : moins de couple en haut.
    CHECK(torqueCurveNm(charged(AirFilter::Cone), rated) < torqueCurveNm(paper, rated));
    CHECK(torqueCurveNm(charged(AirFilter::Cone), rated) > 0.95 * torqueCurveNm(paper, rated));
}

TEST_CASE("changer de filtre ne change pas la boîte") {
    const Powertrain paper = natural();
    const Gearing a = gearingOf(paper);
    const Gearing b = gearingOf(withFilter(paper, AirFilter::Stacks));
    CHECK(a.box.finalDrive == b.box.finalDrive);
    CHECK(a.overall == b.overall);
}

TEST_CASE("le filtre se voit au chrono et à la consommation, sans tricher") {
    const Performance paper = measurePerformance(natural());
    const Performance cone = measurePerformance(natural(AirFilter::Cone));
    CHECK(cone.zeroTo100S > paper.zeroTo100S);
    CHECK(cone.zeroTo100S < paper.zeroTo100S + 0.6);
    // Un moteur électrique n'a pas de filtre à air : rien ne change.
    Powertrain ev = natural();
    ev.electric = true;
    const double torque = torqueCurveNm(ev, 3000.0);
    ev.engine.airFilter = AirFilter::Cone;
    CHECK(torqueCurveNm(ev, 3000.0) == torque);
    CHECK(intakeAirK(ev, 3000.0) == doctest::Approx(293.0));
    CHECK(ev.profile().car == withFilter(ev, AirFilter::Stock).profile().car);
}

TEST_CASE("sans boîte, l'admission s'entend à forte charge, depuis l'habitacle et devant la voiture") {
    const Powertrain paper = natural();
    const Powertrain panel = withFilter(paper, AirFilter::Panel);
    const Powertrain cone = withFilter(paper, AirFilter::Cone);
    const Powertrain stacks = withFilter(paper, AirFilter::Stacks);
    // Le niveau, moyenné sur quelques régimes : à un seul, un mode de caisse peut tout couvrir.
    const auto mean = [](const Powertrain& p, Listener where, float load) {
        float sum = 0.0f;
        const float regimes[] = {3000.0f, 4000.0f, 6500.0f};
        for (float rpm : regimes) {
            EngineSound sound(kRate);
            sound.setListener(where);
            sound.setProfile(p.profile());
            sound.setVolume(0.7f);
            sound.setTargetRpm(rpm);
            sound.setDrive(load, 0.0f, false, false);
            Samples out(kRate * 7 / 10);
            sound.render(out.data(), static_cast<int>(out.size()));
            sum += sound.levelDbA();
        }
        return sum / 3.0f;
    };
    const float stock = mean(paper, Listener::Cabin, 1.0f);
    // Un panneau dans la même boîte : presque rien, et c'est bien normal.
    CHECK(std::abs(mean(panel, Listener::Cabin, 1.0f) - stock) < 1.5f);
    const float coneFull = mean(cone, Listener::Cabin, 1.0f);
    const float stacksFull = mean(stacks, Listener::Cabin, 1.0f);
    CHECK(coneFull > stock + 2.0f);
    CHECK(stacksFull > coneFull);
    CHECK(stacksFull < stock + 14.0f);
    // Plus à forte charge qu'à charge légère : il suit l'air qui passe, sans fond à niveau fixe.
    const float coneLight = mean(cone, Listener::Cabin, 0.3f) - mean(paper, Listener::Cabin, 0.3f);
    CHECK(coneLight < coneFull - stock);
    CHECK(mean(stacks, Listener::Front, 1.0f) > mean(paper, Listener::Front, 1.0f) + 2.0f);
    // L'échappement reste maître derrière la voiture.
    CHECK(std::abs(mean(cone, Listener::Outside, 1.0f) - mean(paper, Listener::Outside, 1.0f)) < 1.5f);
}

TEST_CASE("le bruit de bouche garde des aigus qui s'effacent : pas de bruit blanc") {
    const Powertrain paper = natural();
    for (AirFilter filter : {AirFilter::Cone, AirFilter::Stacks}) {
        const Powertrain open = withFilter(paper, filter);
        // Le pire endroit : la tête au-dessus du moteur, et le moteur qui tourne doucement. L'échappement n'y est plus.
        const Samples idle = held(open, Listener::EngineBay, 900.0f, 0.12f);
        const Samples stockIdle = held(paper, Listener::EngineBay, 900.0f, 0.12f);
        const double top = bandEnergy(idle, 8000.0, 16000.0) / bandEnergy(idle, 500.0, 2000.0);
        const double stockTop = bandEnergy(stockIdle, 8000.0, 16000.0) / bandEnergy(stockIdle, 500.0, 2000.0);
        CHECK(top <= 1.2 * stockTop);
        // En face du bloc, elle reste audible à bas régime sans passer devant.
        CHECK(bandEnergy(idle, 500.0, 2000.0) < 8.0 * bandEnergy(stockIdle, 500.0, 2000.0));
    }
}

TEST_CASE("la réponse de l'admission reste bornée et n'ajoute rien dans les graves") {
    const Powertrain p = natural(AirFilter::Cone);
    const IntakeGeometry g = geometryOf(p);
    CHECK(IntakePath::gainDb(AirFilter::Stock, g, 500.0) == 0.0);
    // La boîte résonne sur sa prise d'air : retirée, ce grave s'efface. Les médiums montent.
    CHECK(IntakePath::gainDb(AirFilter::Cone, g, 60.0) < 0.0);
    CHECK(IntakePath::gainDb(AirFilter::Cone, g, 400.0) > 8.0);
    // Dans la même boîte, le panneau ne bouge presque rien.
    for (double hz : {60.0, 200.0, 500.0, 1000.0, 3000.0}) CHECK(std::abs(IntakePath::gainDb(AirFilter::Panel, g, hz)) < 3.5);
    for (double hz = 20.0; hz < 20000.0; hz *= 1.2) {
        CHECK(IntakePath::gainDb(AirFilter::Stacks, g, hz) < 30.0);
        CHECK(IntakePath::gainDb(AirFilter::Cone, g, hz) < 30.0);
    }
}

TEST_CASE("sur un autre filtre, le rendu n'alloue pas et ne dépend pas des blocs") {
    const EngineProfile profile = natural(AirFilter::Stacks).profile();
    EngineSound whole(kRate);
    EngineSound pieces(kRate);
    for (EngineSound* sound : {&whole, &pieces}) {
        sound->setProfile(profile);
        sound->setTargetRpm(4000.0f);
        sound->setDrive(1.0f, 0.0f, false, false);
    }
    Samples a(kRate), b(kRate);
    const long before = g_allocations.load();
    whole.render(a.data(), kRate);
    for (int done = 0; done < kRate;) {
        const int block = std::min(37 + done % 251, kRate - done);
        pieces.render(b.data() + done, block);
        done += block;
    }
    CHECK(g_allocations.load() == before);
    CHECK(a == b);
}
