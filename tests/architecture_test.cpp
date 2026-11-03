#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>

#include "revforge/architecture.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kPi = 3.141592653589793;

EngineConfig engine(Layout layout, int cylinders, int cc = 0, int bore = 84) {
    EngineConfig c;
    c.layout = layout;
    c.cylinders = cylinders;
    c.displacementCc = cc > 0 ? cc : 500 * cylinders;
    c.boreMm = bore;
    c.redlineRpm = 6500;
    c.exhaust = Exhaust::Sport;
    return c;
}

/** Puissance du signal à cette fréquence, par une transformée de Fourier fenêtrée. */
double powerAt(const Samples& x, double hz) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double a = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(x.size()));
        re += x[i] * w * std::cos(a);
        im += x[i] * w * std::sin(a);
    }
    return re * re + im * im;
}

/** Puissance moyenne autour d'un ordre du vilebrequin (± 4 %), à ce régime. */
double orderPower(const Samples& x, double rpm, double order) {
    const double hz = rpm / 60.0 * order;
    double sum = 0.0;
    for (double f = hz * 0.96; f <= hz * 1.04; f += hz * 0.01) sum += powerAt(x, f);
    return sum;
}

/** Le son d'une seule ligne, au régime tenu : la culasse 0 ou 1, l'autre réduite au silence. */
Samples oneLine(EngineProfile p, int head, float rpm) {
    for (int c = 0; c < p.firingCount; ++c) {
        if (p.bank[static_cast<std::size_t>(c)] != head) p.firingGain[static_cast<std::size_t>(c)] = 0.0f;
    }
    return steady(p, rpm, 2.5f);
}

}  // namespace

TEST_CASE("chaque ordre d'allumage passe une fois par chaque cylindre, et chaque culasse a sa moitié") {
    const std::pair<Layout, int> engines[] = {{Layout::Vr, 5}, {Layout::Vr, 6}, {Layout::W, 8}, {Layout::W, 12}, {Layout::W, 16}, {Layout::V, 14}, {Layout::V, 16},
                                              {Layout::V, 6}, {Layout::V, 8}, {Layout::CrossPlane, 8}, {Layout::V, 10}, {Layout::V, 12}, {Layout::Inline, 4}, {Layout::Boxer, 6}};
    for (const auto& [layout, n] : engines) {
        INFO(static_cast<int>(layout), " ", n);
        const Architecture a = architectureOf(layout, n);
        CHECK(a.layout == layout);
        std::set<int> seen(a.order.begin(), a.order.begin() + n);
        CHECK(seen.size() == static_cast<std::size_t>(n));
        CHECK(*seen.begin() == 1);
        CHECK(*seen.rbegin() == n);
        // Les allumages croissent, sous 720°.
        CHECK(a.firingDeg[0] == 0.0);
        for (int s = 1; s < n; ++s) CHECK(a.firingDeg[static_cast<std::size_t>(s)] > a.firingDeg[static_cast<std::size_t>(s - 1)]);
        CHECK(a.firingDeg[static_cast<std::size_t>(n - 1)] < 720.0);
        // Deux culasses : chacune a la moitié des cylindres, et chaque rang le long du vilebrequin porte au plus deux bielles.
        if (a.heads == 2 && n % 2 == 0) CHECK(a.headShare(0) == doctest::Approx(0.5));
        std::set<std::pair<int, int>> places;
        for (int k = 0; k < n; ++k) places.insert({a.head[static_cast<std::size_t>(k)], a.position[static_cast<std::size_t>(k)]});
        CHECK(places.size() == static_cast<std::size_t>(n));
    }
}

TEST_CASE("les intervalles : égaux sur le VR6, le W12, le W16 et le V16, inégaux sur le V14 à 45°") {
    auto intervals = [](Layout layout, int n) {
        const Architecture a = architectureOf(layout, n);
        std::set<long> out;
        for (int s = 0; s < n; ++s) out.insert(std::lround(a.intervalAfterDeg(s) * 100.0));
        return out;
    };
    CHECK(intervals(Layout::Vr, 6) == std::set<long>{12000});
    CHECK(intervals(Layout::Vr, 5) == std::set<long>{14400});
    CHECK(intervals(Layout::W, 12) == std::set<long>{6000});
    CHECK(intervals(Layout::W, 16) == std::set<long>{4500});
    CHECK(intervals(Layout::V, 16) == std::set<long>{4500});
    // Sept manetons communs, 720/7 = 102,86° entre deux cylindres de gauche : le droit allume 45° après, d'où 45 et 57,86.
    CHECK(intervals(Layout::V, 14) == std::set<long>{4500, 5786});
    // Le W8 : égal en tout, mais pas sur chaque culasse (SSP 248), 180, 270, 180 et 90°.
    CHECK(intervals(Layout::W, 8) == std::set<long>{9000});
    CHECK(architectureOf(Layout::V, 14).uneven());
    CHECK_FALSE(architectureOf(Layout::W, 12).uneven());
}

TEST_CASE("un VR et un W qui n'existent pas se construisent en V, et plus de douze cylindres seulement en V ou en W") {
    CHECK(builtLayout(Layout::Vr, 6) == Layout::Vr);
    CHECK(builtLayout(Layout::Vr, 8) == Layout::V);
    CHECK(builtLayout(Layout::W, 12) == Layout::W);
    CHECK(builtLayout(Layout::W, 10) == Layout::V);
    CHECK(builtLayout(Layout::Inline, 14) == Layout::V);
    CHECK(builtLayout(Layout::Rotary, 3) == Layout::Rotary);
    CHECK(builtLayout(Layout::CrossPlane, 8) == Layout::CrossPlane);
    // Aucun moteur d'avant n'y passe : leur son reste celui d'origine.
    CHECK_FALSE(ownArchitecture(engine(Layout::V, 12)));
    CHECK_FALSE(ownArchitecture(engine(Layout::CrossPlane, 8)));
    CHECK(ownArchitecture(engine(Layout::V, 16)));
    CHECK(ownArchitecture(engine(Layout::Vr, 6)));
}

TEST_CASE("le profil du son suit l'architecture : instants, culasses et lignes") {
    // VR6 : une seule culasse, donc une seule ligne quelle que soit la sortie, et un allumage tous les 120°.
    EngineConfig vr6 = engine(Layout::Vr, 6, 2792, 81);
    vr6.routing = ExhaustRouting::Dual;
    const EngineProfile p = vr6.toProfile();
    CHECK(p.firingCount == 6);
    CHECK_FALSE(p.twoBanks);
    for (int c = 0; c < 6; ++c) {
        CHECK(p.bank[static_cast<std::size_t>(c)] == 0);
        CHECK(p.firing[static_cast<std::size_t>(c)] == doctest::Approx(c / 6.0).epsilon(1e-6));
    }
    // W12 en deux lignes : la culasse de chaque allumage, dans l'ordre réel.
    EngineConfig w12 = engine(Layout::W, 12, 5998, 84);
    w12.routing = ExhaustRouting::Dual;
    const EngineProfile q = w12.toProfile();
    const Architecture a = architectureOf(w12);
    CHECK(q.twoBanks);
    for (int c = 0; c < 12; ++c) {
        const int slot = c == 0 ? 0 : 12 - c;
        CHECK(q.bank[static_cast<std::size_t>(c)] == a.head[static_cast<std::size_t>(a.order[static_cast<std::size_t>(slot)] - 1)]);
    }
    // V14 : les instants inégaux arrivent jusqu'au son.
    const EngineProfile v14 = engine(Layout::V, 14, 7000, 86).toProfile();
    CHECK(v14.firingCount == 14);
    // L'indice c allume au rang 14 − c : le rang 1 (le premier cylindre de droite, 45° après) est à l'indice 13.
    CHECK(v14.firing[13] == doctest::Approx((720.0 - 45.0) / 720.0).epsilon(1e-6));
    CHECK(v14.firing[12] == doctest::Approx((720.0 - 720.0 / 7.0) / 720.0).epsilon(1e-6));
}

TEST_CASE("un VR6 sonne au rang 3 du vilebrequin, un V16 au rang 8") {
    // Trois explosions par tour pour un six, huit pour un seize : c'est là que se tient la note.
    for (const auto& [layout, n, order] : {std::tuple{Layout::Vr, 6, 3.0}, std::tuple{Layout::V, 16, 8.0}, std::tuple{Layout::W, 12, 6.0}}) {
        INFO(n);
        const EngineProfile p = engine(layout, n, 400 * n, 82).toProfile();
        const float rpm = 2400.0f;
        const Samples x = steady(p, rpm, 2.5f);
        const double firing = orderPower(x, rpm, order);
        // Entre deux rangs de l'allumage : rien d'aussi fort.
        CHECK(firing > 4.0 * orderPower(x, rpm, order * 0.5 + 0.25));
    }
}

TEST_CASE("le V14 à 45° fait entendre la demi-fréquence d'allumage, que n'a pas un V14 à intervalles égaux") {
    // Les intervalles alternent 45 et 57,86° : une explosion sur deux arrive un peu tôt, d'où le rang 3,5 (la moitié de 7).
    // Pour n'entendre que les instants, les deux culasses reçoivent la même force et les mêmes collecteurs.
    EngineProfile uneven = engine(Layout::V, 14, 7000, 86).toProfile();
    for (int k = 0; k < 14; ++k) {
        uneven.firingGain[static_cast<std::size_t>(k)] = 1.0f;
        uneven.headerM[static_cast<std::size_t>(k)] = 0.6f;
    }
    EngineProfile even = uneven;
    for (int k = 0; k < 14; ++k) even.firing[static_cast<std::size_t>(k)] = static_cast<float>(k) / 14.0f;
    const float rpm = 3000.0f;
    const Samples a = steady(uneven, rpm, 2.5f);
    const Samples b = steady(even, rpm, 2.5f);
    MESSAGE("rang 3,5 : inégal ", orderPower(a, rpm, 3.5), ", égal ", orderPower(b, rpm, 3.5));
    CHECK(orderPower(a, rpm, 3.5) > 2.0 * orderPower(b, rpm, 3.5));
}

TEST_CASE("seize cylindres : le rendu n'alloue pas, et tient le temps réel") {
    for (const Layout layout : {Layout::V, Layout::W}) {
        EngineConfig c = engine(layout, 16, 7993, 86);
        c.routing = ExhaustRouting::Dual;
        c.aspiration = Aspiration::Turbo;
        EngineSound core(48000);
        core.setProfile(c.toProfile());
        core.setVolume(0.7f);
        std::vector<float> left(4096), right(4096);
        core.setTargetRpm(1000.0f);
        core.renderStereo(left.data(), right.data(), 4096);
        const long before = g_allocations.load();
        const auto start = std::chrono::steady_clock::now();
        int frames = 0;
        for (int i = 0; i < 48; ++i) {
            core.setTargetRpm(i % 2 == 0 ? 6500.0f : 1500.0f);
            core.renderStereo(left.data(), right.data(), 4096);
            frames += 4096;
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(g_allocations.load() == before);
        // 4 s de son : il en faut bien moins pour les calculer, même sous la charge d'autres compilations.
        const double audio = static_cast<double>(frames) / 48000.0;
        MESSAGE("16 cylindres : ", audio, " s de son en ", seconds, " s");
        CHECK(seconds < audio);
    }
}

TEST_CASE("au démarreur, un V14 et un W16 se lancent et partent, leurs compressions aux vrais intervalles") {
    for (const auto& [layout, n] : {std::pair{Layout::V, 14}, std::pair{Layout::W, 16}}) {
        INFO(n);
        Powertrain p;
        p.engine = engine(layout, n, 7000, 86);
        DrivingModel model(p);
        model.halt();
        model.crank();
        double peakLoad = 0.0;
        for (int i = 0; i < 3000; ++i) {
            model.stepNeutral(0.001, 0.0);
            peakLoad = std::max(peakLoad, std::fabs(model.starterLoad()));
        }
        CHECK(std::isfinite(peakLoad));
        CHECK(peakLoad > 0.0);
        CHECK(model.rpm() > 400.0);
    }
}
