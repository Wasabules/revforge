#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

#include "acoustics.hpp"
#include "exhaust_line.hpp"
#include "revforge/atmosphere.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"
#include "revforge/sound_sheet.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace revforge::acoustics;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kTwoPi = 2.0 * 3.141592653589793;

double db(double ratio) { return 20.0 * std::log10(ratio); }

/** Une voiture sans rien autour : une seule source, en haut d'un mât, loin de tout sol et de toute caisse. */
CarAcoustics emptyCar(float height = 1000.0f) {
    CarAcoustics car;
    car.lengthM = 0.01f;
    car.widthM = 0.01f;
    car.heightM = 0.01f;
    for (SourcePlace& place : car.sources) place = SourcePlace{{0.0f, 0.0f, height}, 0.0f, 1.0f};
    car.engine = {0.0f, 0.0f, height};
    car.seat = {0.0f, 0.0f, -100.0f};
    return car;
}

/** Fait entrer `tone` par la source Bay et renvoie l'amplitude à l'oreille, une fois tout établi. */
struct FieldProbe {
    SoundField field;
    explicit FieldProbe(const CarAcoustics& car, bool stereo = false) : field(kRate) {
        field.setCar(car, true);
        field.setStereo(stereo);
        field.setAmbient(0.0f);
    }
    void at(float x, float y, float z, float facing = std::numeric_limits<float>::quiet_NaN()) {
        field.setTarget({x, y, z}, facing, 1000.0f);
        // La tête glisse : on la laisse arriver.
        const std::array<float, kSources> silence{};
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kRate * 2; ++i) {
            if (i % 32 == 0) field.control(32);
            field.process(silence, l, r);
        }
    }

    /** Amplitude d'une sinusoïde à l'oreille gauche et droite. */
    std::pair<double, double> tone(double hz, double seconds = 0.5, Source source = Source::Underfloor) {
        const int settle = kRate / 4 + static_cast<int>(seconds * kRate);
        double reL = 0.0, imL = 0.0, reR = 0.0, imR = 0.0;
        int counted = 0;
        for (int i = 0; i < settle; ++i) {
            if (i % 32 == 0) field.control(32);
            std::array<float, kSources> in{};
            const double a = kTwoPi * hz * i / kRate;
            in[static_cast<std::size_t>(source)] = static_cast<float>(std::sin(a));
            float l = 0.0f, r = 0.0f;
            field.process(in, l, r);
            if (i >= kRate / 4) {
                reL += l * std::cos(a);
                imL += l * std::sin(a);
                reR += r * std::cos(a);
                imR += r * std::sin(a);
                ++counted;
            }
        }
        const double scale = 2.0 / counted;
        return {scale * std::hypot(reL, imL), scale * std::hypot(reR, imR)};
    }

    /** Réponse à une impulsion : renvoie les deux oreilles. */
    std::pair<Samples, Samples> impulse(int length) {
        Samples left(static_cast<std::size_t>(length)), right(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i) {
            if (i % 32 == 0) field.control(32);
            std::array<float, kSources> in{};
            if (i == 0) in[static_cast<std::size_t>(Source::Underfloor)] = 1.0f;
            field.process(in, left[static_cast<std::size_t>(i)], right[static_cast<std::size_t>(i)]);
        }
        return {left, right};
    }
};

std::size_t arrivalOf(const Samples& x) {
    std::size_t best = 0;
    float top = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::fabs(x[i]) > top) {
            top = std::fabs(x[i]);
            best = i;
        }
    }
    return best;
}

}  // namespace

// ── Formules, face aux valeurs publiées ──

TEST_CASE("la pondération A retrouve la table de la norme IEC 61672") {
    struct Row {
        double hz, value;
    };
    const Row table[] = {{31.5, -39.4}, {63, -26.2}, {125, -16.1}, {250, -8.6}, {500, -3.2}, {1000, 0.0},
                         {2000, 1.2},   {4000, 1.0}, {8000, -1.1}, {10000, -2.5}, {16000, -6.6}};
    for (const Row& row : table) {
        INFO(row.hz);
        // La table arrondit au dixième, et donne ses valeurs aux fréquences exactes (31,62 Hz pour le nominal 31,5).
        CHECK(std::fabs(aWeightingDb(row.hz) - row.value) < 0.15);
    }
}

TEST_CASE("le filtre numérique de pondération A reste dans la classe 1, à 44,1 et 48 kHz") {
    for (float rate : {44100.0f, 48000.0f}) {
        AWeighting filter;
        filter.init(rate);
        CHECK(std::fabs(filter.responseDb(1000.0)) < 0.01);
        // Tolérances de classe 1 de la norme. Les moteurs n'ont presque plus d'énergie au-delà.
        struct Row {
            double hz, plus, minus;
        };
        for (const Row& row : {Row{31.5, 1.5, 1.5}, Row{63, 1.0, 1.0}, Row{125, 1.0, 1.0}, Row{500, 1.0, 1.0}, Row{2000, 1.0, 1.0}, Row{4000, 1.0, 1.0},
                               Row{8000, 1.5, 2.5}, Row{10000, 2.0, 3.0}, Row{12500, 2.5, 5.0}, Row{16000, 3.0, 16.0}}) {
            INFO(rate, " Hz, ", row.hz);
            const double error = filter.responseDb(row.hz) - aWeightingDb(row.hz);
            CHECK(error < row.plus);
            CHECK(error > -row.minus);
        }
    }
}

TEST_CASE("le filtre numérique de pondération A pèse vraiment le signal") {
    AWeighting filter;
    filter.init(kRate);
    for (double hz : {63.0, 1000.0, 4000.0}) {
        double in = 0.0, out = 0.0;
        for (int i = 0; i < kRate * 2; ++i) {
            const double x = std::sin(kTwoPi * hz * i / kRate);
            const double y = filter.process(x);
            if (i > kRate) {
                in += x * x;
                out += y * y;
            }
        }
        CHECK(std::fabs(10.0 * std::log10(out / in) - aWeightingDb(hz)) < 0.5);
    }
}

TEST_CASE("le sonomètre lit 94 dB pour 1 Pa efficace à 1 kHz, à la constante de temps fast") {
    LevelMeter meter;
    meter.init(kRate);
    const float amplitude = std::sqrt(2.0f);
    float at125ms = 0.0f;
    for (int i = 0; i < kRate * 2; ++i) {
        meter.addMono(amplitude * static_cast<float>(std::sin(kTwoPi * 1000.0 * i / kRate)));
        if (i == kRate / 8) at125ms = meter.db();
    }
    CHECK(meter.db() == doctest::Approx(94.0).epsilon(0.002));
    // Au bout d'une constante de temps : 63 % de l'énergie finale, soit -2 dB.
    CHECK(std::fabs(at125ms - (94.0f - 2.0f)) < 0.3f);
}

TEST_CASE("l'absorption de l'air retrouve les tables de l'ISO 9613") {
    // Tables de l'ISO 9613-2, dB/km : 20 °C et 70 % d'humidité, puis 10 °C et 70 %.
    const double warm[][2] = {{63, 0.1}, {125, 0.3}, {250, 1.1}, {500, 2.8}, {1000, 5.0}, {2000, 9.0}, {4000, 22.9}, {8000, 76.6}};
    for (const auto& row : warm) {
        INFO(row[0]);
        // La table arrondit au dixième, 4 kHz et plus à 1 % près.
        CHECK(std::fabs(airAbsorptionDbPerM(row[0], 20.0, 70.0) * 1000.0 - row[1]) < std::max(0.06, row[1] * 0.015));
    }
    const double cool[][2] = {{500, 1.9}, {1000, 3.7}, {2000, 9.7}, {4000, 32.8}, {8000, 117.0}};
    for (const auto& row : cool) {
        INFO(row[0]);
        CHECK(std::fabs(airAbsorptionDbPerM(row[0], 10.0, 70.0) * 1000.0 - row[1]) < std::max(0.06, row[1] * 0.012));
    }
}

TEST_CASE("un enrobé renvoie presque tout, une pelouse beaucoup moins sous une incidence rasante") {
    double hard = 0.0, soft = 0.0, phase = 0.0;
    groundReflection(1000.0, 2e7, 0.1, hard, phase);
    CHECK(hard > 0.9);
    groundReflection(1000.0, 2e5, 0.1, soft, phase);
    CHECK(soft < 0.75);
    // Delany et Bazley à X = f/σ = 5 (σ = 200 kPa·s/m²) : Z = 1 + 9,08·X^-0,75 - j·11,9·X^-0,73.
    const double x = 5.0;
    const double zr = 1.0 + 9.08 * std::pow(x, -0.75), zi = -11.9 * std::pow(x, -0.73);
    const double cosine = 1.0;
    const double top = std::hypot(zr * cosine - 1.0, zi * cosine), bottom = std::hypot(zr * cosine + 1.0, zi * cosine);
    groundReflection(1000.0, 2e5, cosine, soft, phase);
    CHECK(soft == doctest::Approx(top / bottom).epsilon(0.001));
}

TEST_CASE("un obstacle suit la loi de Maekawa") {
    CHECK(barrierLossDb(0.0) == 0.0);
    // Maekawa : A = 10·log10(3 + 20·N), au moins à N >= 1. Nous partons de zéro pour rester continus.
    for (double n : {1.0, 3.0, 10.0}) CHECK(std::fabs(barrierLossDb(n) - 10.0 * std::log10(3.0 + 20.0 * n)) < 0.7);
    CHECK(barrierLossDb(1000.0) <= 24.0);
}

TEST_CASE("le chemin autour de la caisse suit la géométrie") {
    CarAcoustics car;
    car.lengthM = 4.0f;
    car.widthM = 1.8f;
    car.heightM = 1.4f;
    // Sans rien entre les deux : aucun excès.
    CHECK(pathExcessM({-2.1f, 0.0f, 0.3f}, {-7.0f, 0.0f, 1.6f}, car) == 0.0f);
    CHECK(pathExcessM({-2.1f, 0.0f, 0.3f}, {-2.1f, 5.0f, 1.6f}, car) == 0.0f);
    // Le toit, calculé à la main : de la sortie au bord arrière du toit, le long du toit, puis jusqu'à la tête.
    const Point3 s{-2.1f, 0.0f, 0.3f}, r{5.0f, 0.0f, 1.6f};
    const double expected = std::hypot(0.1, 1.1) + 4.0 + std::hypot(3.0, 0.2) - std::hypot(7.1, 1.3);
    CHECK(pathExcessM(s, r, car) == doctest::Approx(expected).epsilon(0.01));
    // Par le côté, bien plus court ici : la tête est à hauteur de la sortie, et la caisse est étroite.
    const Point3 low{5.0f, 0.0f, 0.3f};
    const double around = 2.0 * std::hypot(0.1, 0.9) + 4.0 + 0.0 * 1.0;
    const double side = std::hypot(0.1, 0.9) + 4.0 + std::hypot(3.0, 0.9) - 7.1;
    CHECK(side < around);
    CHECK(pathExcessM({-2.1f, 0.0f, 0.3f}, low, car) == doctest::Approx(side).epsilon(0.01));
}

TEST_CASE("le masquage ne saute pas quand on contourne la voiture") {
    CarAcoustics car;
    float last = pathExcessM({-2.3f, -0.4f, 0.3f}, {-12.0f, 0.0f, 1.6f}, car);
    for (int i = 1; i <= 720; ++i) {
        const double a = 3.141592653589793 + kTwoPi * i / 720.0;  // un demi-degré à 12 m : 10 cm
        const float now = pathExcessM({-2.3f, -0.4f, 0.3f}, {static_cast<float>(12.0 * std::cos(a)), static_cast<float>(12.0 * std::sin(a)), 1.6f}, car);
        CHECK(std::fabs(now - last) < 0.07f);
        last = now;
    }
}

TEST_CASE("la bouche du tube suit la directivité de Levine et Schwinger") {
    // Gain 10·log10(G) calculé d'après les intégrales de Levine et Schwinger (1948) : ka, puis 0°, 45°, 90°, 135°, 180°.
    const double table[][6] = {{0.5, 1.05, 0.59, -0.25, -0.45, 0.10},
                               {1.0, 2.87, 1.62, -0.76, -1.67, -0.29},
                               {2.0, 6.57, 3.23, -3.08, -5.63, -2.64},
                               {3.0, 9.65, 3.44, -8.37, -11.36, -6.58}};
    const double angles[] = {0.0, 45.0, 90.0, 135.0, 180.0};
    double worst = 0.0;
    for (const auto& row : table) {
        for (int i = 0; i < 5; ++i) {
            const double error = std::fabs(outletFilterDb(row[0], angles[i] * 3.141592653589793 / 180.0) - row[i + 1]);
            INFO("ka ", row[0], ", ", angles[i], "°, écart ", error);
            CHECK(error < 2.5);
            worst = std::max(worst, error);
        }
    }
    MESSAGE("écart maximal à Levine et Schwinger : " << worst << " dB");
}

TEST_CASE("la loi de masse donne les valeurs attendues, et une fuite la borne") {
    // 20·log10(f·m) - 47 : 1 kHz, 10 kg/m² : 32,8 dB.
    CHECK(panelLossDb(1000.0, 10.0, 15000.0, 6.0, 1e9, 0.0, 0.0) == doctest::Approx(32.8).epsilon(0.01));
    // +6 dB par doublement de la masse ou de la fréquence.
    CHECK(panelLossDb(2000.0, 10.0, 15000.0, 6.0, 1e9, 0.0, 0.0) - panelLossDb(1000.0, 10.0, 15000.0, 6.0, 1e9, 0.0, 0.0) == doctest::Approx(6.02).epsilon(0.01));
    CHECK(panelLossDb(1000.0, 20.0, 15000.0, 6.0, 1e9, 0.0, 0.0) - panelLossDb(1000.0, 10.0, 15000.0, 6.0, 1e9, 0.0, 0.0) == doctest::Approx(6.02).epsilon(0.01));
    // Une fuite de 0,1 % de la surface plafonne la paroi autour de 30 dB, quelle que soit sa masse.
    CHECK(panelLossDb(4000.0, 100.0, 1e6, 6.0, 1e9, 0.0, 1e-3) < 30.1);
    CHECK(panelLossDb(4000.0, 100.0, 1e6, 6.0, 1e9, 0.0, 1e-3) > 29.5);
}

TEST_CASE("une vitre perd du terrain autour de sa fréquence de coïncidence") {
    // Vitre de 4 mm : coïncidence vers 3,2 kHz. Une octave plus bas, elle ne gagne plus 6 dB par octave.
    const auto glass = [](double hz) { return panelLossDb(hz, 10.0, 3200.0, 3.0, 1e9, 0.0, 0.0); };
    CHECK(glass(1600.0) - glass(800.0) > 1.0);
    CHECK(glass(3100.0) - glass(1700.0) < 2.0);
    CHECK(glass(6400.0) - glass(3200.0) > 6.0);
}

// ── Le champ extérieur ──

TEST_CASE("la pression suit 1/r, quelle que soit la fréquence : un monopôle n'a pas de terme de champ proche") {
    // Pour un monopôle, p = ρ·Q'(t - r/c)/(4π·r) : même forme à toute distance. C'est sa vitesse, et un dipôle, qui en ont un.
    FieldProbe probe(emptyCar());
    for (double hz : {60.0, 250.0, 2000.0}) {
        double reference = 0.0;
        for (float r : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f}) {
            probe.at(r, 0.0f, 1000.0f);
            probe.tone(hz, 0.05);
            const double level = probe.tone(hz).first;
            INFO(hz, " Hz, ", r, " m");
            CHECK(level * r == doctest::Approx(1.0).epsilon(0.03));
            reference = level;
        }
        (void)reference;
    }
}

TEST_CASE("la sortie du tube plafonne : sa pression ne diverge pas au plan de sortie") {
    // Tube non bafflé, zone incompressible : |φ| = 0,71·a·U au centre de la bouche, soit un monopôle à 0,35 a.
    constexpr float kRadius = 0.03f;
    CarAcoustics car = emptyCar();
    car.sources[static_cast<std::size_t>(Source::Underfloor)].extentM = 0.35f * kRadius;
    FieldProbe probe(car);
    probe.at(0.001f, 0.0f, 1000.0f);
    probe.tone(200.0, 0.05);
    const double atMouth = probe.tone(200.0).first;
    CHECK(atMouth == doctest::Approx(1.0 / (0.35 * kRadius)).epsilon(0.03));
    // Déjà à 3 rayons, on retrouve la loi à moins de 1 dB (table de Wiener-Hopf du rapport de recherche).
    probe.at(3.0f * kRadius, 0.0f, 1000.0f);
    probe.tone(200.0, 0.05);
    const double atThree = probe.tone(200.0).first;
    CHECK(std::fabs(db(atThree * 3.0 * kRadius)) < 1.0);
}

TEST_CASE("le son met le temps qu'il faut pour arriver") {
    FieldProbe probe(emptyCar());
    probe.at(17.15f, 0.0f, 1000.0f);
    const auto [left, right] = probe.impulse(kRate / 5);
    (void)right;
    // 17,15 m à 343 m/s : 50 ms.
    CHECK(static_cast<double>(arrivalOf(left)) == doctest::Approx(kRate * 0.05).epsilon(0.002));
}

TEST_CASE("le sol creuse le spectre à la fréquence prévue par la source image") {
    // Source à 0,3 m, oreille à 1,6 m, à 5 m : l'écho arrive avec un excès de 18 cm, soit un creux près de 900 Hz.
    CarAcoustics car = emptyCar(0.3f);
    FieldProbe probe(car);
    probe.at(5.0f, 0.0f, 1.6f);
    probe.tone(100.0, 0.05);
    const double direct = std::hypot(5.0, 1.3);
    const double mirror = std::hypot(5.0, 1.9);
    const double predicted = 343.0 / (2.0 * (mirror - direct));
    double bestHz = 0.0, lowest = 1e9;
    double top = 0.0;
    for (double hz = predicted * 0.6; hz < predicted * 1.4; hz += 15.0) {
        const double level = probe.tone(hz, 0.08).first;
        if (level < lowest) {
            lowest = level;
            bestHz = hz;
        }
        top = std::max(top, level);
    }
    MESSAGE("creux de sol à " << bestHz << " Hz, prévu à " << predicted << " Hz, profondeur " << db(top / lowest) << " dB");
    CHECK(bestHz == doctest::Approx(predicted).epsilon(0.05));
    CHECK(db(top / lowest) > 12.0);
    // Bien en dessous du creux, les deux trajets s'ajoutent : presque 6 dB de plus que le trajet direct seul.
    const double low = probe.tone(80.0).first * direct;
    CHECK(db(low) > 4.0);
}

TEST_CASE("les oreilles entendent la bonne différence de temps") {
    // Woodworth : ITD = (a/c)·(θ + sin θ). À 90° et 10 m : 0,656 ms pour une tête de 8,75 cm.
    FieldProbe probe(emptyCar(), true);
    probe.at(0.0f, -10.0f, 1000.0f, 0.0f);  // la source à gauche : la tête regarde vers +x, et la source est en +y
    const auto [left, right] = probe.impulse(kRate / 5);
    const double itd = (static_cast<double>(arrivalOf(right)) - static_cast<double>(arrivalOf(left))) / kRate;
    const double expected = 0.0875 / 343.0 * (3.141592653589793 / 2.0 + 1.0);
    MESSAGE("écart de temps : " << itd * 1000.0 << " ms, attendu " << expected * 1000.0 << " ms");
    CHECK(itd == doctest::Approx(expected).epsilon(0.1));
}

TEST_CASE("la tête fait de l'ombre aux aigus, pas aux graves (Brown et Duda)") {
    FieldProbe probe(emptyCar(), true);
    probe.at(0.0f, -10.0f, 1000.0f, 0.0f);
    probe.tone(100.0, 0.05);
    const auto low = probe.tone(100.0);
    const auto high = probe.tone(8000.0);
    const double ildLow = db(low.first / low.second);
    const double ildHigh = db(high.first / high.second);
    MESSAGE("différence de niveau : " << ildLow << " dB à 100 Hz, " << ildHigh << " dB à 8 kHz");
    CHECK(std::fabs(ildLow) < 1.0);
    CHECK(ildHigh > 10.0);
    // α varie de 2 (face à la source, +6 dB) à 0,1 vers 150° : au plus 26 dB dans les aigus.
    CHECK(ildHigh < 26.0);
    CHECK(headShadowAlpha(0.0f) == doctest::Approx(2.0f));
    CHECK(headShadowAlpha(150.0f / 180.0f * 3.1415927f) == doctest::Approx(0.1f));
}

TEST_CASE("le trajet jusqu'à l'oreille contourne la tête") {
    constexpr float a = kHeadRadius;
    // En face : la distance moins le rayon.
    CHECK(earPathM(1.0f, 0.0f) == doctest::Approx(1.0f - a).epsilon(0.001));
    // À l'opposé, loin : le rayon de plus, et un quart de tour autour de la tête.
    CHECK(earPathM(100.0f, 3.14159265f) == doctest::Approx(100.0f + a * 3.14159265f / 2.0f).epsilon(0.001));
    // Continu au point tangent.
    const float tangent = std::acos(a / 0.5f);
    CHECK(earPathM(0.5f, tangent - 0.001f) == doctest::Approx(earPathM(0.5f, tangent + 0.001f)).epsilon(0.001));
}

TEST_CASE("l'air retire les aigus à grande distance, sans toucher aux graves") {
    FieldProbe probe(emptyCar());
    probe.at(60.0f, 0.0f, 1000.0f);
    probe.tone(100.0, 0.05);
    const double low = probe.tone(100.0).first;
    const double high = probe.tone(8000.0).first;
    // ISO 9613-1, 20 °C et 50 % : environ 115 dB/km à 8 kHz, soit 7 dB à 60 m.
    const double expected = airAbsorptionDbPerM(8000.0) * 60.0;
    MESSAGE("air à 60 m : " << db(low / high) << " dB à 8 kHz, attendu " << expected);
    CHECK(db(low / high) == doctest::Approx(expected).epsilon(0.2));
}

TEST_CASE("derrière la voiture, le moteur avant est masqué par la caisse") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    const auto levelFrom = [&p](float x, float y, double hz) {
        FieldProbe probe(p.car);
        probe.at(x, y, 1.6f);
        probe.tone(hz, 0.05, Source::Bay);
        return probe.tone(hz, 0.5, Source::Bay).first;
    };
    const float reach = p.car.lengthM / 2.0f + 5.0f;
    // Même distance de la voiture : devant, on voit l'ouverture ; derrière, on est de l'autre côté de la caisse.
    const double front = levelFrom(reach, 0.0f, 2000.0);
    const double back = levelFrom(-reach, 0.0f, 2000.0);
    CHECK(db(front / back) > 8.0);
    // Les graves contournent : l'écart se réduit presque à la distance.
    const double frontLow = levelFrom(reach, 0.0f, 80.0);
    const double backLow = levelFrom(-reach, 0.0f, 80.0);
    CHECK(db(frontLow / backLow) < db(front / back));
}

// ── Le son complet ──

namespace {

struct Heard {
    double db = 0.0;
    double rms = 0.0;
    Samples left, right;
};

/** Moteur tenu à ce régime et cette charge, écouté à cette position. Niveau moyen sur la dernière seconde. */
Heard heard(const EngineProfile& p, float rpm, float load, Listener listener, bool stereo = false) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListener(listener);
    core.setTargetRpm(rpm);
    core.setDrive(load, 0.0f, false, false);
    Samples l(kRate * 2), r(kRate * 2);
    if (stereo) core.renderStereo(l.data(), r.data(), kRate * 2);
    else core.render(l.data(), kRate * 2);
    Heard out;
    double energy = 0.0;
    out.left.resize(kRate);
    out.right.resize(kRate);
    for (int b = 0; b < 100; ++b) {
        if (stereo) core.renderStereo(out.left.data() + b * 441, out.right.data() + b * 441, 441);
        else core.render(out.left.data() + b * 441, 441);
        energy += std::pow(10.0, static_cast<double>(core.levelDbA()) / 10.0);
    }
    out.db = 10.0 * std::log10(energy / 100.0);
    out.rms = rms(out.left);
    return out;
}

Heard heardAt(const EngineProfile& p, float rpm, float load, float x, float y, float z) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListenerPosition(x, y, z);
    core.setTargetRpm(rpm);
    core.setDrive(load, 0.0f, false, false);
    Samples l(kRate * 2);
    core.render(l.data(), kRate * 2);
    Heard out;
    double energy = 0.0;
    out.left.resize(kRate);
    for (int b = 0; b < 100; ++b) {
        core.render(out.left.data() + b * 441, 441);
        energy += std::pow(10.0, static_cast<double>(core.levelDbA()) / 10.0);
    }
    out.db = 10.0 * std::log10(energy / 100.0);
    out.rms = rms(out.left);
    return out;
}

}  // namespace

TEST_CASE("le résultat ne dépend pas de la taille des blocs, en stéréo et en se déplaçant") {
    const EngineProfile p = findPreset("v8_cross_plane")->profile();
    const auto play = [&p](int block) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setTargetRpm(3000.0f);
        core.setDrive(0.6f, 0.0f, false, false);
        Samples left(kRate * 3), right(kRate * 3);
        int done = 0;
        int nextMove = 0;
        while (done < kRate * 3) {
            // La tête bouge à des instants fixes, pas à chaque bloc.
            if (done >= nextMove) {
                const float t = static_cast<float>(done) / kRate;
                core.setListenerPosition(-6.0f + 4.0f * t, -3.0f, 1.6f);
                nextMove += 4096;
            }
            const int n = std::min({block, kRate * 3 - done, nextMove - done});
            core.renderStereo(left.data() + done, right.data() + done, n);
            done += n;
        }
        return std::make_pair(left, right);
    };
    CHECK(play(64) == play(1024));
}

TEST_CASE("le rendu stéréo n'alloue jamais de mémoire, même en se déplaçant") {
    const EngineProfile p = findPreset("v12")->profile();
    const EngineProfile other = findPreset("diesel_four")->profile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.8f);
    Samples left(4096), right(4096);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) {
        core.setTargetRpm(i % 2 == 0 ? 7000.0f : 1200.0f);
        core.setListenerPosition(-8.0f + 0.5f * static_cast<float>(i), -3.0f, 1.6f);
        if (i == 10) core.setListener(Listener::Cabin);
        if (i == 15) core.setListener(Listener::Pipe);
        if (i == 20) core.setProfile(other);
        core.renderStereo(left.data(), right.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}

TEST_CASE("traverser la voiture en se déplaçant ne fait ni clic ni saut") {
    for (const char* id : {"four_cyl_sport", "v8_cross_plane"}) {
        INFO(id);
        const EngineProfile p = findPreset(id)->profile();
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setTargetRpm(2500.0f);
        core.setDrive(0.4f, 0.0f, false, false);
        Samples settle(kRate);
        core.setListenerPosition(-8.0f, 0.3f, 1.2f);
        core.render(settle.data(), kRate);

        // À 3 m/s, à travers l'habitacle : de derrière à devant.
        Samples moving(kRate * 6);
        constexpr int kBlock = 128;
        for (int done = 0; done + kBlock <= kRate * 6; done += kBlock) {
            const float t = static_cast<float>(done) / kRate;
            core.setListenerPosition(-8.0f + 3.0f * t, 0.3f, 1.2f);
            core.render(moving.data() + done, kBlock);
        }
        std::vector<float> jumps;
        jumps.reserve(moving.size());
        for (std::size_t i = 1; i < moving.size(); ++i) jumps.push_back(std::fabs(moving[i] - moving[i - 1]));
        std::sort(jumps.begin(), jumps.end());
        const float typical = jumps[static_cast<std::size_t>(static_cast<double>(jumps.size()) * 0.99)];
        MESSAGE(id << " : plus grand saut " << jumps.back() << ", 99e centile " << typical);
        CHECK(jumps.back() < typical * 6.0f);
        CHECK(jumps.back() < 0.3f);
    }
}

TEST_CASE("sauter d'une position à l'autre glisse, sans clic") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setTargetRpm(2500.0f);
    core.setDrive(0.4f, 0.0f, false, false);
    Samples out(kRate * 4);
    core.setListener(Listener::Outside);
    core.render(out.data(), kRate * 4);
    float before = worstJump(slice(out, kRate * 3, kRate * 4));
    for (Listener next : {Listener::Front, Listener::Cabin, Listener::Pipe, Listener::EngineBay, Listener::Side, Listener::Outside}) {
        INFO(static_cast<int>(next));
        core.setListener(next);
        core.render(out.data(), kRate * 4);
        // Un clic dépasserait ce que le son faisait au départ, et ce qu'il fait une fois arrivé.
        const float during = worstJump(slice(out, 0, kRate * 2));
        const float arrived = worstJump(slice(out, kRate * 3, kRate * 4));
        MESSAGE("vers " << static_cast<int>(next) << " : plus grand saut " << during << ", avant " << before << ", après " << arrived);
        CHECK(during < std::max({before * 3.0f, arrived * 4.0f, 0.1f}));
        before = arrived;
    }
}

// ── Les niveaux absolus, face aux valeurs publiées (voir docs/modele.md) ──

namespace {

Powertrain withExhaust(const char* id, Exhaust exhaust) {
    Powertrain p = findPreset(id)->powertrain();
    p.engine.exhaust = exhaust;
    return p;
}

/** Niveau à la position de l'essai à l'arrêt : 50 cm de la sortie, 45° de son axe. */
double atStandstillPosition(const Powertrain& powertrain, float rpm, float load) {
    const EngineProfile p = powertrain.profile();
    const Point3& o = p.car.sources[static_cast<std::size_t>(Source::ExhaustA)].at;
    const float side = o.y >= 0.0f ? 1.0f : -1.0f;
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListenerPosition(o.x - 0.354f, o.y + side * 0.354f, std::max(o.z, 0.2f));
    core.setTargetRpm(rpm);
    core.setDrive(load, 0.0f, false, false, load);
    Samples out(kRate * 3 / 2);
    core.render(out.data(), kRate);
    double energy = 0.0;
    for (int b = 0; b < 20; ++b) {
        core.render(out.data(), kRate / 40);
        energy += std::pow(10.0, static_cast<double>(core.levelDbA()) / 10.0);
    }
    return 10.0 * std::log10(energy / 20.0);
}

}  // namespace

// Cartes grises néerlandaises (RDW, U.1), essai à l'arrêt : valeurs médianes par catégorie, voir le rapport de recherche.
TEST_CASE("le niveau à l'arrêt retrouve les cartes grises") {
    struct Row {
        const char* id;
        double target, tolerance;
    };
    for (const Row& row : {Row{"city_14", 78.0, 4.0}, Row{"four_cyl_sport", 86.0, 5.0}, Row{"hot_hatch_turbo", 88.0, 6.0}, Row{"v8_cross_plane", 93.0, 5.0},
                           Row{"flat_six", 96.0, 5.0}, Row{"diesel_four", 74.0, 5.0}, Row{"v10_open", 112.0, 7.0}}) {
        INFO(row.id);
        const SoundSheet sheet = soundSheet(findPreset(row.id)->powertrain());
        MESSAGE(row.id << " : " << sheet.standstillDb << " dB(A) à l'arrêt, attendu " << row.target << " ± " << row.tolerance);
        CHECK(std::fabs(static_cast<double>(sheet.standstillDb) - row.target) < row.tolerance);
    }
}

TEST_CASE("chaque échappement a son écart, comme sur de vraies voitures") {
    const double stock = atStandstillPosition(withExhaust("four_cyl_sport", Exhaust::Stock), 3750.0f, 0.2f);
    const auto gain = [stock](Exhaust exhaust) { return atStandstillPosition(withExhaust("four_cyl_sport", exhaust), 3750.0f, 0.2f) - stock; };
    const double sport = gain(Exhaust::Sport), straight = gain(Exhaust::Straight), noMuffler = gain(Exhaust::MufflerDelete), noResonator = gain(Exhaust::ResonatorDelete), quiet = gain(Exhaust::Quiet);
    MESSAGE("par rapport à l'origine : sport " << sport << ", sans silencieux " << noMuffler << ", directe " << straight << ", sans résonateur " << noResonator << ", confort " << quiet << " dB");
    // Lignes homologuées au Japon : +3 à +11 dB. Silencieux supprimé : +9 à +14. Ligne directe : +10 à +25 (faible confiance). Résonateur : 0 à +3.
    CHECK(sport > 3.0);
    CHECK(sport < 11.5);
    CHECK(noMuffler > 8.0);
    CHECK(noMuffler < 15.0);
    CHECK(straight > 12.0);
    CHECK(straight < 26.0);
    CHECK(straight > noMuffler + 2.0);
    CHECK(noResonator > -1.0);
    CHECK(noResonator < 4.0);
    CHECK(quiet < -3.0);
}

TEST_CASE("le niveau suit la charge et le régime comme le montrent les mesures") {
    const Powertrain city = findPreset("city_14")->powertrain();
    const double empty = atStandstillPosition(city, 3750.0f, 0.2f);
    const double full = atStandstillPosition(city, 3750.0f, 1.0f);
    MESSAGE("pleine charge - à vide : " << full - empty << " dB");
    CHECK(full - empty > 8.0);
    CHECK(full - empty < 16.0);
    // À vide, environ 10 dB par doublement du régime, de 7 à 14 : sur plus d'une octave, les résonances se moyennent.
    const double slow = atStandstillPosition(city, 1800.0f, 0.2f);
    const double fast = atStandstillPosition(city, 5400.0f, 0.2f);
    const double slope = (fast - slow) / std::log2(3.0);
    MESSAGE("pente à vide : " << slope << " dB par doublement");
    CHECK(slope > 6.0);
    CHECK(slope < 14.0);
    // Ralenti : 25 dB ± 6 sous l'essai à l'arrêt.
    const EngineProfile p = city.profile();
    const double idle = atStandstillPosition(city, p.idleRpm, 0.12f);
    MESSAGE("ralenti : " << empty - idle << " dB sous l'essai à l'arrêt");
    CHECK(empty - idle > 15.0);
    CHECK(empty - idle < 31.0);
}

TEST_CASE("le bloc suit la loi d'Anderton, la route celle de CNOSSOS") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    const auto heardAtRoad = [&p](float kmh) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListenerPosition(0.0f, -7.5f, 1.2f);
        core.setTargetRpm(800.0f);
        core.setDrive(0.05f, 0.0f, false, false, 0.05f);
        core.setRoad(kmh / 3.6f, 0.0f);
        Samples out(kRate * 2);
        core.render(out.data(), kRate * 2);
        return static_cast<double>(core.levelDbA());
    };
    // 130 km/h : le pneu donne 81 dB(A) à 7,5 m (72 + 34·lg(v/70)), l'air autant, sur revêtement d'essai.
    const double at130 = heardAtRoad(130.0f);
    MESSAGE("route à 130 km/h, à 7,5 m : " << at130 << " dB(A)");
    CHECK(std::fabs(at130 - 83.0) < 4.0);
    CHECK(heardAtRoad(130.0f) - heardAtRoad(65.0f) > 9.0);

    // Au-dessus du bloc, capot ouvert : 50·lg(N) + 60·lg(B) - 203 à 1 m, moteur essence à pleine charge.
    const Point3& e = p.car.engine;
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListenerPosition(e.x, e.y, e.z + 0.6f);
    core.setTargetRpm(5500.0f);
    core.setDrive(1.0f, 0.0f, false, false, 1.0f);
    Samples out(kRate * 2);
    core.render(out.data(), kRate * 2);
    const double expected = 50.0 * std::log10(5500.0) + 60.0 * std::log10(p.boreM * 1000.0) - 203.0;
    MESSAGE("près du bloc : " << core.levelDbA() << " dB(A), loi d'Anderton à 1 m : " << expected);
    // À 0,6 m d'une source de 0,3 m de taille : 3,5 dB de plus qu'à 1 m.
    CHECK(std::fabs(static_cast<double>(core.levelDbA()) - 3.5 - expected) < 4.0);
}

// Essais de presse (auto motor und sport) : citadine 72-76 à pleine charge, sportive 82-98, voiture de course 114 et plus.
TEST_CASE("l'habitacle suit l'isolation de la caisse, et le niveau publié") {
    Powertrain car = findPreset("city_14")->powertrain();
    float last = 1e9f;
    for (Insulation insulation : {Insulation::Stripped, Insulation::Light, Insulation::Standard, Insulation::Comfort, Insulation::Luxury}) {
        car.car.insulation = insulation;
        const float full = soundSheet(car).cabinFullDb;
        MESSAGE("isolation " << static_cast<int>(insulation) << " : " << full << " dB(A) plein gaz");
        CHECK(full < last);
        last = full;
    }
    car.car.insulation = Insulation::Standard;
    const SoundSheet standard = soundSheet(car);
    CHECK(std::fabs(static_cast<double>(standard.cabinFullDb) - 75.0) < 6.0);
    CHECK(standard.cabinIdleDb < 50.0);
}

TEST_CASE("un moteur derrière les sièges s'entend bien plus dans l'habitacle") {
    // Avec une ligne silencieuse, pour entendre le bloc plutôt que la sortie.
    Powertrain car = withExhaust("city_14", Exhaust::Quiet);
    car.car.placement = EnginePlacement::Front;
    const float front = soundSheet(car).cabinFullDb;
    car.car.placement = EnginePlacement::Mid;
    const float mid = soundSheet(car).cabinFullDb;
    MESSAGE("habitacle plein gaz : moteur avant " << front << ", central " << mid << " dB(A)");
    CHECK(mid > front + 3.0f);
    // La masse suit : moins de poids sur l'avant, comme une vraie voiture à moteur central (de 40 à 45 %) ou arrière (38 à 40 %).
    CHECK(frontShareOf(car.car) < 0.48);
    car.car.placement = EnginePlacement::Rear;
    CHECK(frontShareOf(car.car) < 0.43);
    // Et la ligne raccourcit.
    CHECK(exhaustScaleOf(car.car) < 0.6f);
}

TEST_CASE("le bloc est là où on l'a mis, et se fait entendre à travers la cloison la plus proche") {
    Powertrain car = withExhaust("city_14", Exhaust::Quiet);
    car.car.placement = EnginePlacement::Front;
    const CarAcoustics front = car.profile().car;
    car.car.placement = EnginePlacement::Mid;
    const CarAcoustics mid = car.profile().car;
    car.car.placement = EnginePlacement::Rear;
    const CarAcoustics rear = car.profile().car;
    CHECK(front.engine.x > front.seat.x + 0.5f);
    CHECK(mid.engine.x < mid.seat.x - 0.5f);
    CHECK(rear.engine.x < mid.engine.x);
    // Le siège recule ou avance, mais le bloc reste près d'une cloison : à 500 Hz, plus fort s'il est juste derrière la tête.
    const double frontDb = 20.0 * std::log10(acoustics::cabinTransfer(500.0, front, Source::Bay));
    const double midDb = 20.0 * std::log10(acoustics::cabinTransfer(500.0, mid, Source::Bay));
    MESSAGE("bloc vers l'habitacle, 500 Hz : avant " << frontDb << " dB, central " << midDb << " dB");
    CHECK(midDb > frontDb + 1.0);
}

// Le défaut « bruit blanc » : un fond continu et large, sous le capot, de 4 à 8 dB au-dessus de vrais compartiments, de 2 à 8 kHz.
TEST_CASE("sous le capot, le bloc perd ses aigus, comme de vrais compartiments") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    EngineSound sound(kRate);
    sound.setListener(Listener::EngineBay);
    sound.setProfile(p);
    sound.setTargetRpm(p.idleRpm);
    sound.setDrive(0.12f, 0.0f, false, false);
    Samples warm(kRate * 3 / 2);
    sound.render(warm.data(), static_cast<int>(warm.size()));
    Samples x(16384);
    sound.render(x.data(), static_cast<int>(x.size()));
    const auto density = [&x](double from, double to) {
        double sum = 0.0;
        int count = 0;
        for (double hz = from; hz <= to; hz += (to - from) / 40.0, ++count) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i) {
                const double a = 2.0 * 3.141592653589793 * hz * static_cast<double>(i) / kRate;
                const double w = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(x.size()));
                re += x[i] * w * std::cos(a);
                im += x[i] * w * std::sin(a);
            }
            sum += re * re + im * im;
        }
        return sum / count;
    };
    // Vrais compartiments, en densité par hertz : -18 dB de 1 à 8 kHz (octaves à -9 et -18, et 9 dB de plus pour leur largeur).
    const double slope = 10.0 * std::log10(density(7000.0, 9000.0) / density(900.0, 1100.0));
    MESSAGE("de 1 à 8 kHz : " << slope << " dB (réel : -18, défaut : -11,6)");
    CHECK(slope < -15.5);
    CHECK(slope > -24.0);
}

TEST_CASE("des vitres ouvertes font entrer le dehors") {
    Powertrain car = findPreset("city_14")->powertrain();
    const auto cruising = [&car] {
        EngineSound core(kRate);
        core.setProfile(car.profile());
        core.setVolume(0.7f);
        core.setListener(Listener::Cabin);
        core.setTargetRpm(2500.0f);
        core.setDrive(0.3f, 0.0f, false, false, 0.3f);
        core.setRoad(25.0f, 0.0f);
        Samples out(kRate * 2);
        core.render(out.data(), kRate * 2);
        return static_cast<double>(core.levelDbA());
    };
    const double closed = cruising();
    car.car.windowsOpen = true;
    const double open = cruising();
    MESSAGE("habitacle en route : " << closed << " dB(A) fermé, " << open << " dB(A) ouvert");
    // Mesuré dans une 206 : +5 dB(A) en route.
    CHECK(open > closed + 3.0);
    CHECK(open < closed + 20.0);
}

// ── Position : ce qu'on doit entendre ──

TEST_CASE("le niveau ne dépend pas du volume, la sortie si") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    const auto run = [&p](float volume, double& level) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(volume);
        core.setTargetRpm(3000.0f);
        core.setDrive(0.5f, 0.0f, false, false, 0.5f);
        Samples out(kRate * 2);
        core.render(out.data(), kRate * 2);
        level = static_cast<double>(core.levelDbA());
        return rms(slice(out, kRate, out.size()));
    };
    double loud = 0.0, quiet = 0.0;
    const double high = run(1.0f, loud);
    const double low = run(0.25f, quiet);
    CHECK(loud == doctest::Approx(quiet).epsilon(0.001));
    CHECK(high / low == doctest::Approx(4.0).epsilon(0.05));
}

TEST_CASE("une berline au ralenti reste bien plus basse qu'une ligne directe à plein gaz, sans saturer") {
    Powertrain limo = findPreset("v6_gt")->powertrain();
    limo.engine.exhaust = Exhaust::Quiet;
    limo.engine.routing = ExhaustRouting::Single;
    limo.car.insulation = Insulation::Luxury;
    const auto output = [](const Powertrain& powertrain, Listener listener, float rpmShare, float load, double* limiter) {
        const EngineProfile p = powertrain.profile();
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListener(listener);
        core.setTargetRpm(load > 0.5f ? p.redlineRpm * rpmShare : p.idleRpm);
        core.setDrive(load, 0.0f, false, false, load);
        Samples out(kRate * 8);
        core.render(out.data(), kRate * 6);
        double gain = 0.0;
        for (int i = 0; i < 40; ++i) {
            core.render(out.data() + kRate * 6 + i * kRate / 20, kRate / 20);
            gain += static_cast<double>(core.limiterGain());
        }
        if (limiter != nullptr) *limiter = gain / 40.0;
        return slice(out, kRate * 6, out.size());
    };
    double limiter = 0.0;
    const Samples quiet = output(limo, Listener::Cabin, 0.0f, 0.1f, nullptr);
    const Samples loud = output(findPreset("v10_open")->powertrain(), Listener::Outside, 0.75f, 1.0f, &limiter);
    // Pondéré A comme le sonomètre : sur un haut-parleur, les graves du ralenti ne font pas le niveau.
    const auto weighted = [](const Samples& x) {
        AWeighting filter;
        filter.init(kRate);
        double energy = 0.0;
        for (float sample : x) {
            const double y = filter.process(static_cast<double>(sample));
            energy += y * y;
        }
        return std::sqrt(energy / static_cast<double>(x.size()));
    };
    const double contrast = 20.0 * std::log10(weighted(loud) / weighted(quiet));
    MESSAGE("écart à la sortie : " << contrast << " dB, gain moyen du limiteur à plein gaz : " << limiter);
    CHECK(contrast > 20.0);
    CHECK(peak(loud) < 0.99f);
    // Le limiteur garde la forme d'onde : il retire les crêtes, sans travailler en continu.
    CHECK(limiter > 0.7);
}

TEST_CASE("on entend de quel côté est la voiture") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    const auto balance = [&p](float y) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListenerPosition(0.0f, y, 1.6f);
        core.setTargetRpm(3000.0f);
        core.setDrive(0.6f, 0.0f, false, false, 0.6f);
        Samples left(kRate * 2), right(kRate * 2);
        core.renderStereo(left.data(), right.data(), kRate * 2);
        return std::make_pair(left, right);
    };
    // Deux oreilles qui ne s'accordent pas : ce n'est plus un seul canal recopié.
    const auto [left, right] = balance(-5.0f);
    double difference = 0.0;
    for (std::size_t i = kRate; i < left.size(); ++i) difference += static_cast<double>(left[i] - right[i]) * (left[i] - right[i]);
    CHECK(std::sqrt(difference / static_cast<double>(kRate)) > rms(left) * 0.2);
}

TEST_CASE("l'oreille dans le pot : près de la sortie, une oreille prend tout") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListener(Listener::Pipe);
    core.setTargetRpm(3000.0f);
    core.setDrive(0.6f, 0.0f, false, false, 0.6f);
    Samples left(kRate * 3), right(kRate * 3);
    core.renderStereo(left.data(), right.data(), kRate * 3);
    const double near = rms(slice(left, kRate * 2, left.size()));
    const double far = rms(slice(right, kRate * 2, right.size()));
    MESSAGE("oreille dans le pot : " << 20.0 * std::log10(near / far) << " dB d'écart entre les oreilles, " << core.levelDbA() << " dB(A)");
    CHECK(near > far * 2.0);
    // Une mesure à 10 cm d'une sortie de série dépasse 110 dB(A) sous charge (calcul à partir de l'essai à l'arrêt, pas de mesure directe).
    CHECK(core.levelDbA() > 100.0f);
}

TEST_CASE("dans le jet, l'oreille reçoit du vent : bien plus fort et bien plus grave qu'à côté") {
    const EngineProfile p = findPreset("four_cyl_sport")->profile();
    const Point3& o = p.car.sources[static_cast<std::size_t>(Source::ExhaustA)].at;
    const auto heard = [&p](float x, float y, float z, double& lowShare) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListenerPosition(x, y, z);
        core.setTargetRpm(5000.0f);
        core.setDrive(1.0f, 0.0f, false, false, 1.0f);
        Samples out(kRate * 2);
        core.render(out.data(), kRate * 2);
        lowShare = 0.0;
        return static_cast<double>(core.levelDbA());
    };
    double unused = 0.0;
    const double inJet = heard(o.x - 0.3f, o.y, o.z, unused);
    const double beside = heard(o.x - 0.21f, o.y + 0.21f, o.z, unused);
    MESSAGE("à 30 cm de la sortie : " << inJet << " dB(A) dans le jet, " << beside << " dB(A) à 45°");
    CHECK(inJet > beside + 3.0);
}

TEST_CASE("l'écoulement porte le son vers l'aval") {
    // Masovic et al. (2017) : p_d = p_m·M·cos θ. À M = 0,3, 3,3 dB de plus vers l'aval que sans lui, et 3,1 dB de moins vers l'amont, à faible ka.
    CarAcoustics car = emptyCar();
    FieldProbe probe(car);
    probe.at(5.0f, 0.0f, 1000.0f);
    const double hz = 100.0;
    probe.field.setJetSpeed(0.0f);
    const auto level = [&probe, hz](float x, float speed) {
        // L'axe de sortie pointe vers l'arrière : la tête en -x est en aval.
        probe.at(x, 0.0f, 1000.0f);
        probe.field.setJetSpeed(speed);
        return probe.tone(hz, 0.4, Source::ExhaustA).first;
    };
    const double still = level(-5.0f, 0.0f);
    const double downstream = level(-5.0f, 343.0f * 0.3f);
    const double upstream = level(5.0f, 343.0f * 0.3f);
    CHECK(db(downstream / still) == doctest::Approx(db(1.3)).epsilon(0.1));
    CHECK(db(upstream / still) == doctest::Approx(db(0.7)).epsilon(0.1));
}

TEST_CASE("quand la tête défile devant la voiture, la hauteur suit l'effet Doppler") {
    // 85 km/h : rapport de 1,14 en approche ; mesuré de 1,12 à 1,15 sur de vrais passages.
    const double v = 85.0 / 3.6;
    FieldProbe probe(emptyCar());
    const double hz = 1000.0;
    float x = 300.0f;
    probe.field.setTarget({45.0f, 0.0f, 1000.0f}, std::numeric_limits<float>::quiet_NaN(), 1000.0f);
    const std::array<float, kSources> silence{};
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < kRate; ++i) {
        if (i % 32 == 0) probe.field.control(32);
        probe.field.process(silence, a, b);
    }
    // À distance fixe d'abord, puis la tête approche à v : on compte les passages par zéro, une fois sa vitesse établie.
    Samples out(kRate);
    x = 45.0f;
    for (int i = 0; i < kRate; ++i) {
        if (i % 32 == 0) {
            x -= static_cast<float>(v) * 32.0f / kRate;
            probe.field.setTarget({x, 0.0f, 1000.0f}, std::numeric_limits<float>::quiet_NaN(), 500.0f);
            probe.field.control(32);
        }
        std::array<float, kSources> in{};
        in[static_cast<std::size_t>(Source::Underfloor)] = static_cast<float>(std::sin(kTwoPi * hz * i / kRate));
        probe.field.process(in, out[static_cast<std::size_t>(i)], b);
    }
    int crossings = 0;
    for (int i = kRate / 2; i < kRate - 1; ++i) {
        if (out[static_cast<std::size_t>(i)] <= 0.0f && out[static_cast<std::size_t>(i + 1)] > 0.0f) ++crossings;
    }
    const double measured = crossings * 2.0;
    const double expected = hz / (1.0 - v / 343.0);
    MESSAGE("Doppler : " << measured << " Hz, attendu " << expected << " Hz");
    CHECK(measured == doctest::Approx(expected).epsilon(0.01));
}

TEST_CASE("sur un V8 à deux lignes, près d'une sortie, le rythme irrégulier de la rangée s'entend") {
    Powertrain powertrain = findPreset("v8_cross_plane")->powertrain();
    const auto orders = [](const EngineProfile& p, const Point3& at, double& odd, double& firing) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListenerPosition(at.x, at.y, at.z);
        core.setTargetRpm(2000.0f);
        Samples out(kRate * 3);
        core.render(out.data(), kRate * 3);
        const Samples part = slice(out, kRate, out.size());
        const auto power = [&part](double hz) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < part.size(); ++i) {
                const double a = kTwoPi * hz * static_cast<double>(i) / kRate;
                const double w = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(part.size()));
                re += part[i] * w * std::cos(a);
                im += part[i] * w * std::sin(a);
            }
            return re * re + im * im;
        };
        const double cycle = 2000.0 / 120.0;
        odd = power(cycle * 3.0) + power(cycle * 5.0);
        firing = power(cycle * 8.0);
    };
    powertrain.engine.routing = ExhaustRouting::Dual;
    const EngineProfile dual = powertrain.profile();
    const Point3 nearA{dual.car.sources[0].at.x - 0.7f, dual.car.sources[0].at.y, dual.car.sources[0].at.z};
    double odd = 0.0, firing = 0.0;
    orders(dual, nearA, odd, firing);
    const double dualNear = 10.0 * std::log10(odd / firing);
    powertrain.engine.routing = ExhaustRouting::Single;
    const EngineProfile single = powertrain.profile();
    const Point3 nearOne{single.car.sources[0].at.x - 0.7f, single.car.sources[0].at.y, single.car.sources[0].at.z};
    orders(single, nearOne, odd, firing);
    const double singleNear = 10.0 * std::log10(odd / firing);
    MESSAGE("ordres impairs par rapport à l'allumage, près d'une sortie : deux lignes " << dualNear << " dB, une ligne " << singleNear << " dB");
    CHECK(dualNear > singleNear + 6.0);
}

// ── La ligne, par morceaux ──

TEST_CASE("la bouche du tube retrouve Levine et Schwinger, par Silva et al.") {
    // |R| et l/a exacts, intégrales de Levine et Schwinger (1948) évaluées numériquement.
    const double table[][3] = {{0.2, 0.9809, 0.6064}, {0.5, 0.8964, 0.5818}, {1.0, 0.6951, 0.5274}, {2.0, 0.3462, 0.4169}, {3.0, 0.1543, 0.3083}};
    for (const auto& row : table) {
        INFO(row[0]);
        CHECK(ExhaustLine::outletReflectionMagnitude(row[0]) == doctest::Approx(row[1]).epsilon(0.02));
        CHECK(ExhaustLine::outletEndCorrection(row[0]) == doctest::Approx(row[2]).epsilon(0.03));
    }
}

TEST_CASE("un résonateur vise le bourdonnement : le retirer le fait revenir") {
    // Quatre cylindres : allumage à 83 Hz à 2500 tr/min, là où il est accordé.
    ExhaustLine line(kRate);
    line.compute(Exhaust::Stock, 1.0f, 0.002, 4);
    const double with = line.lossDb(75.0, 92.0);
    const double wide = line.lossDb(200.0, 800.0);
    line.compute(Exhaust::ResonatorDelete, 1.0f, 0.002, 4);
    const double without = line.lossDb(75.0, 92.0);
    const double wideWithout = line.lossDb(200.0, 800.0);
    MESSAGE("autour de 83 Hz : " << with << " dB retenus avec, " << without << " dB sans ; de 200 à 800 Hz : " << wide << " et " << wideWithout);
    CHECK(with > without + 4.0);
}

// Le défaut « vent ou pluie qui couvre tout dès que la voiture roule » : à grande vitesse, le bruit de roulement et le vent
// remplissaient le haut du spectre, jusqu'à 13 dB de plus entre 4 et 16 kHz.
TEST_CASE("rouler vite n'ajoute presque rien tout en haut du spectre") {
    const auto topBand = [](float speedMs) {
        const Powertrain powertrain = findPreset("four_cyl_sport")->powertrain();
        EngineSound core(48000);
        core.setProfile(powertrain.profile());
        core.setVolume(0.7f);
        core.setTargetRpm(5500.0f);
        core.setDrive(1.0f, 0.0f, false, false);
        core.setRoad(speedMs, 0.0f);
        std::vector<float> out(48000 * 4);
        core.render(out.data(), static_cast<int>(out.size()));
        // La différence troisième ne laisse passer que le haut du spectre.
        double energy = 0.0;
        for (std::size_t i = 48000 * 2 + 3; i < out.size(); ++i) {
            const double d = out[i] - 3.0 * out[i - 1] + 3.0 * out[i - 2] - out[i - 3];
            energy += d * d;
        }
        return 10.0 * std::log10(energy);
    };
    const double parked = topBand(0.0f);
    const double fast = topBand(47.0f);
    MESSAGE("à l'arrêt " << parked << " dB, à 170 km/h " << fast << " dB");
    CHECK(fast - parked < 3.0);
}

// ── L'air du jour (voir « L'air ambiant » dans docs/modele.md) ──

TEST_CASE("l'absorption suit la température et l'humidité des tables de l'ISO 9613-2") {
    // ISO 9613-2, tableau 2, dB/km : 30 °C et 70 %, puis 15 °C à 20, 50 et 80 %.
    struct Row {
        double celsius, humidity, hz, value;
    };
    const Row rows[] = {
        {30.0, 70.0, 1000, 7.4}, {30.0, 70.0, 2000, 12.7}, {30.0, 70.0, 4000, 23.1}, {30.0, 70.0, 8000, 59.3},
        {15.0, 20.0, 1000, 8.2}, {15.0, 20.0, 2000, 28.2}, {15.0, 20.0, 4000, 88.8}, {15.0, 20.0, 8000, 202.0},
        {15.0, 50.0, 1000, 4.2}, {15.0, 50.0, 2000, 10.8}, {15.0, 50.0, 4000, 36.2}, {15.0, 50.0, 8000, 129.0},
        {15.0, 80.0, 1000, 4.1}, {15.0, 80.0, 2000, 8.3}, {15.0, 80.0, 4000, 23.7}, {15.0, 80.0, 8000, 82.8},
    };
    for (const Row& row : rows) {
        INFO(row.celsius << " °C, " << row.humidity << " %, " << row.hz << " Hz");
        CHECK(std::fabs(airAbsorptionDbPerM(row.hz, row.celsius, row.humidity) * 1000.0 - row.value) < std::max(0.06, row.value * 0.015));
    }
    // L'air du jour passe bien par là, à l'humidité de référence : 50 %.
    CHECK(ambientAbsorptionDbPerM(ambientAir(15.0, 0.0), 4000.0) == airAbsorptionDbPerM(4000.0, 15.0, 50.0, 101.325));
}

TEST_CASE("dans l'air froid, le son arrive plus tard, et l'air du jour fait son absorption") {
    const auto arrival = [](const CarAcoustics& car) {
        FieldProbe probe(car);
        probe.at(30.0f, 0.0f, 1000.0f);
        return static_cast<double>(arrivalOf(probe.impulse(kRate / 5).first));
    };
    const CarAcoustics warm = emptyCar();
    CarAcoustics cold = emptyCar();
    cold.air = airAcousticsOf(ambientAir(-30.0, 0.0));
    const double ratio = arrival(cold) / arrival(warm);
    MESSAGE("arrivée à 30 m : " << ratio << " fois plus tard à -30 °C");
    CHECK(ratio == doctest::Approx(343.0 / static_cast<double>(cold.air.soundSpeedMs)).epsilon(0.01));

    // À 0 °C et 50 %, l'air retire bien plus de 8 kHz qu'à 20 °C : ISO 9613-1, sur 60 m.
    CarAcoustics chilly = emptyCar();
    const AmbientAir zero = ambientAir(0.0, 0.0);
    chilly.air = airAcousticsOf(zero);
    FieldProbe probe(chilly);
    probe.at(60.0f, 0.0f, 1000.0f);
    probe.tone(100.0, 0.05);
    const double low = probe.tone(100.0).first;
    const double high = probe.tone(8000.0).first;
    const double expected = ambientAbsorptionDbPerM(zero, 8000.0) * 60.0;
    MESSAGE("air à 60 m, 0 °C : " << db(low / high) << " dB à 8 kHz, attendu " << expected);
    CHECK(db(low / high) == doctest::Approx(expected).epsilon(0.2));
    CHECK(expected > airAbsorptionDbPerM(8000.0) * 60.0 + 2.0);
}
