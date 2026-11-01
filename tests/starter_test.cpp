#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "dsp.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"
#include "starter_sound.hpp"
#include "test_support.hpp"

// Ces mesures décrivent ce que les enregistrements ont montré (tools/analyze_starter.py, docs/modele.md).
// Elles protègent contre le retour de défauts connus ; elles ne disent pas que le son est juste : l'écoute
// tranche. Chaque test a été contre-éprouvé en retirant ce qu'il protège.

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr int kBlock = 128;

struct Take {
    Samples sound;
    std::vector<float> load;  // couple transmis, une valeur par tranche
    std::size_t endOfCranking = 0;
};

/** Du contact à la prise, sur le même chemin que le son : DrivingModel, puis StarterSound. */
void crankOnce(StarterSound& starter, DrivingModel& model, Take& take) {
    model.halt();
    model.crank();
    while (model.cranking()) {
        model.stepNeutral(static_cast<double>(kBlock) / kRate, 0.0);
        starter.setState(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()));
        const auto rpm = static_cast<float>(model.rpm());
        const bool cranking = model.cranking();
        for (int i = 0; i < kBlock; ++i) take.sound.push_back(starter.sample(rpm, cranking, cranking ? 1.0f : 0.0f));
        take.load.push_back(static_cast<float>(model.starterLoad()));
    }
    take.endOfCranking = take.sound.size();
}

Take crankOf(const char* id) {
    const Powertrain powertrain = findPreset(id)->powertrain();
    DrivingModel model(powertrain);
    auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
    starter->setup(powertrain.profile().starter, 1.0f);
    Take take;
    crankOnce(*starter, model, take);
    return take;
}

/** Le lancement établi, sans l'engagement ni la prise. */
Samples established(const Take& take) { return slice(take.sound, kRate / 4, std::min(take.endOfCranking, static_cast<std::size_t>(kRate) * 85 / 100)); }

/** Deux filtres à la suite : un seul laisse les médiums, bien plus forts, déborder sur les aigus. */
Samples bandPassed(const Samples& x, float hz, float q) {
    dsp::Svf first, second;
    first.set(hz, q, static_cast<float>(kRate));
    second.set(hz, q, static_cast<float>(kRate));
    Samples out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        first.process(x[i]);
        second.process(first.bp);
        out[i] = second.bp;
    }
    return out;
}

/** Niveau en dB, par tranches de `window` échantillons. */
std::vector<double> levelsDb(const Samples& x, std::size_t window) {
    std::vector<double> levels;
    for (std::size_t at = 0; at + window <= x.size(); at += window) levels.push_back(20.0 * std::log10(rms(slice(x, at, at + window)) + 1e-9));
    return levels;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ab += (a[i] - ma) * (b[i] - mb);
        aa += (a[i] - ma) * (a[i] - ma);
        bb += (b[i] - mb) * (b[i] - mb);
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

/** Spectre moyen en dB, par tranches de `window` points recouvrant à moitié. */
std::vector<double> meanSpectrumDb(const Samples& x, int window) {
    dsp::Fft fft;
    fft.init(window);
    std::vector<float> re(static_cast<std::size_t>(window)), im(static_cast<std::size_t>(window));
    std::vector<double> power(static_cast<std::size_t>(window / 2), 0.0);
    for (std::size_t at = 0; at + static_cast<std::size_t>(window) <= x.size(); at += static_cast<std::size_t>(window) / 2) {
        for (int i = 0; i < window; ++i) {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * i / window);
            re[static_cast<std::size_t>(i)] = static_cast<float>(x[at + static_cast<std::size_t>(i)] * hann);
            im[static_cast<std::size_t>(i)] = 0.0f;
        }
        fft.transform(re.data(), im.data(), false);
        for (std::size_t k = 0; k < power.size(); ++k) power[k] += static_cast<double>(re[k]) * re[k] + static_cast<double>(im[k]) * im[k];
    }
    for (double& p : power) p = 10.0 * std::log10(p + 1e-12);
    return power;
}

/** Ôte la tendance sur `span` raies : il ne reste que la structure fine. */
std::vector<double> fineStructure(const std::vector<double>& spectrum, std::size_t from, std::size_t to, std::size_t span) {
    std::vector<double> fine;
    for (std::size_t k = from; k < to; ++k) {
        double mean = 0.0;
        for (std::size_t j = k - span / 2; j <= k + span / 2; ++j) mean += spectrum[j];
        fine.push_back(spectrum[k] - mean / static_cast<double>(span + 1));
    }
    return fine;
}

/** De combien le niveau de la bande monte quand le couple passe, en dB : tiers haut contre tiers bas du couple. */
double swingWithLoad(const Take& take, float hz, float q) {
    const std::size_t from = kRate / 4 / kBlock;
    const std::size_t to = std::min(take.load.size(), static_cast<std::size_t>(kRate) * 85 / 100 / kBlock);
    std::vector<float> sorted(take.load.begin() + static_cast<std::ptrdiff_t>(from), take.load.begin() + static_cast<std::ptrdiff_t>(to));
    std::sort(sorted.begin(), sorted.end());
    const float low = sorted[sorted.size() / 3];
    const float high = sorted[sorted.size() * 2 / 3];
    const Samples band = bandPassed(take.sound, hz, q);
    double energyHigh = 1e-12, energyLow = 1e-12;
    for (std::size_t b = from; b < to; ++b) {
        double energy = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i) energy += static_cast<double>(band[b * kBlock + i]) * band[b * kBlock + i];
        if (take.load[b] >= high) energyHigh += energy;
        if (take.load[b] <= low) energyLow += energy;
    }
    return 10.0 * std::log10(energyHigh / energyLow);
}

}  // namespace

// Mesuré : deux moitiés d'une même prise gardent la même structure fine (corrélation de 0,5 à 0,97), de 40 à
// 100 Hz de large. Treize résonateurs n'en ont pas : c'était la trace des défauts « ballon » et « vaisseau ».
TEST_CASE("la fine structure du spectre reste fixe pendant le lancement") {
    for (const char* id : {"city_14", "four_cyl_sport", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const Samples cranking = established(crankOf(id));
        const std::size_t half = cranking.size() / 2;
        const auto a = meanSpectrumDb(slice(cranking, 0, half), 2048);
        const auto b = meanSpectrumDb(slice(cranking, half, cranking.size()), 2048);
        const double hzPerBin = static_cast<double>(kRate) / 2048.0;
        const auto from = static_cast<std::size_t>(1500.0 / hzPerBin), to = static_cast<std::size_t>(4000.0 / hzPerBin);
        const auto span = static_cast<std::size_t>(400.0 / hzPerBin);
        const double fixedPart = correlation(fineStructure(a, from, to, span), fineStructure(b, from, to, span));
        MESSAGE(std::string(id) << " : corrélation entre moitiés " << fixedPart);
        CHECK(fixedPart > 0.45);
    }
}

// Mesuré : chaque moteur garde ses bosses d'une prise à l'autre (l'Iveco, à 1 dB près), et elles diffèrent
// d'un moteur à l'autre.
TEST_CASE("la structure appartient au moteur") {
    const Powertrain powertrain = findPreset("city_14")->powertrain();
    DrivingModel model(powertrain);
    auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
    starter->setup(powertrain.profile().starter, 1.0f);
    Take first, second;
    crankOnce(*starter, model, first);
    crankOnce(*starter, model, second);
    const auto one = meanSpectrumDb(established(first), 1024);
    const auto two = meanSpectrumDb(established(second), 1024);
    const auto other = meanSpectrumDb(established(crankOf("v8_cross_plane")), 1024);
    const auto from = static_cast<std::size_t>(500.0 * 1024.0 / kRate), to = static_cast<std::size_t>(5000.0 * 1024.0 / kRate);
    const double sameEngine = correlation(fineStructure(one, from, to, 12), fineStructure(two, from, to, 12));
    const double otherEngine = correlation(fineStructure(one, from, to, 12), fineStructure(other, from, to, 12));
    MESSAGE("même moteur " << sameEngine << ", autre moteur " << otherEngine);
    CHECK(sameEngine > otherEngine + 0.2);
}

// « Aspirateur » : plus aucune trace des dents. « DJ » et « vaisseau » : une note qui domine. À régime et couple
// tenus, l'engrènement tombe sur une fréquence exacte : mesurer autrement, c'est se faire tromper par la structure
// fixe (une ressemblance à l'échelle des dents ne bougeait pas, avec ou sans raie).
TEST_CASE("l'engrènement sort du bruit sans le dominer") {
    for (const char* id : {"city_14", "four_cyl_sport", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const StarterSpec spec = findPreset(id)->profile().starter;
        auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
        starter->setup(spec, 1.0f);
        constexpr float kRpm = 240.0f;
        starter->setState(true, true, 0.3f);
        Samples held(kRate * 2);
        for (float& s : held) s = starter->sample(kRpm, true, 1.0f);
        const Samples steady = slice(held, kRate / 2, held.size());
        const double meshHz = static_cast<double>(kRpm) / 60.0 * spec.ringTeeth;
        const auto powerAt = [&steady](double hz) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < steady.size(); ++i) {
                const double phase = 2.0 * 3.141592653589793 * hz * static_cast<double>(i) / kRate;
                const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(steady.size()));
                re += steady[i] * hann * std::cos(phase);
                im += steady[i] * hann * std::sin(phase);
            }
            return re * re + im * im;
        };
        std::vector<double> beside;
        for (double offset : {-90.0, -70.0, -50.0, -30.0, 30.0, 50.0, 70.0, 90.0}) beside.push_back(powerAt(meshHz + offset));
        std::sort(beside.begin(), beside.end());
        const double height = 10.0 * std::log10(powerAt(meshHz) / beside[beside.size() / 2]);
        // Les défauts de dents étalent la raie autour d'elle : on compare toute cette zone aux médiums, loin d'elle.
        double around = 0.0, far = 0.0;
        for (double offset = -120.0; offset <= 120.0; offset += 7.0) around += powerAt(meshHz + offset);
        for (double offset = -120.0; offset <= 120.0; offset += 7.0) far += powerAt(1250.0 + offset);
        const double dominance = 10.0 * std::log10(around / far);
        MESSAGE(std::string(id) << " : engrènement à " << meshHz << " Hz, " << height << " dB au-dessus du bruit voisin, " << dominance << " dB face aux médiums");
        CHECK(height > 10.0);
        CHECK(dominance < 6.5);
    }
}

// Mesuré : les médiums suivent le contact des dents, les aigus suivent le courant. Quand tout venait d'une seule
// source, les bandes bougeaient d'un bloc.
TEST_CASE("les aigus ne bougent pas d'un bloc avec les médiums") {
    for (const char* id : {"city_14", "four_cyl_sport", "diesel_four"}) {
        INFO(id);
        const Samples cranking = established(crankOf(id));
        const auto mid = levelsDb(bandPassed(cranking, 1200.0f, 0.8f), 220);
        const auto top = levelsDb(bandPassed(bandPassed(cranking, 6000.0f, 1.0f), 6000.0f, 1.0f), 220);
        const double together = correlation(mid, top);
        MESSAGE(std::string(id) << " : médiums et aigus " << together);
        CHECK(together < 0.65);
    }
}

// Mesuré : 12 à 19 dB dans les médiums d'un diesel, 4 à 9 dB en essence, et 3 à 7 dB dans les aigus d'un diesel.
TEST_CASE("un diesel se tait entre deux compressions, pas une essence") {
    const Take diesel = crankOf("diesel_four");
    const Take petrol = crankOf("four_cyl_sport");
    const double dieselMid = swingWithLoad(diesel, 1400.0f, 0.8f);
    const double petrolMid = swingWithLoad(petrol, 1400.0f, 0.8f);
    const double dieselTop = swingWithLoad(diesel, 6000.0f, 0.8f);
    MESSAGE("médiums : diesel " << dieselMid << " dB, essence " << petrolMid << " dB ; aigus du diesel " << dieselTop << " dB");
    CHECK(dieselMid > petrolMid + 2.0);
    CHECK(dieselTop < dieselMid - 3.0);
}

// Le volume battait de 20 dB avec des trous complets : mesuré, le lancement établi ne bat que de 6 à 9 dB en essence.
TEST_CASE("le lancement bat sans s'éteindre") {
    for (const char* id : {"city_14", "four_cyl_sport", "v8_cross_plane"}) {
        INFO(id);
        auto levels = levelsDb(established(crankOf(id)), 220);
        std::sort(levels.begin(), levels.end());
        const double contrast = levels[levels.size() * 9 / 10] - levels[levels.size() / 10];
        MESSAGE(std::string(id) << " : contraste " << contrast << " dB");
        CHECK(contrast > 4.5);
        CHECK(contrast < 12.0);
    }
}

// Mesuré : monte en 10 à 20 ms, culmine de 10 à 15 dB au-dessus du lancement, et bien plus dans l'aigu, puis
// retombe en une cinquantaine de millisecondes. Ce sont les premiers sons qu'on entend.
TEST_CASE("l'engagement monte, frappe, puis retombe") {
    for (const char* id : {"city_14", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const Take take = crankOf(id);
        const double base = 20.0 * std::log10(rms(established(take)));
        const auto levelOf = [&take, base](double from, double to) {
            return 20.0 * std::log10(rms(slice(take.sound, static_cast<std::size_t>(from * kRate), static_cast<std::size_t>(to * kRate))) + 1e-9) - base;
        };
        double top = -100.0;
        for (double t = 0.005; t < 0.06; t += 0.005) top = std::max(top, levelOf(t, t + 0.01));
        const double start = levelOf(0.0, 0.006);
        const double after = levelOf(0.1, 0.14);
        // Les chocs excitent tout : c'est l'aigu, pas le couple, qui les distingue (mesuré : 15 à 25 dB).
        const Samples sharp = bandPassed(take.sound, 7000.0f, 1.0f);
        const double sharpBase = 20.0 * std::log10(rms(established({sharp, {}, take.endOfCranking})));
        double sharpTop = -100.0;
        for (double t = 0.005; t < 0.06; t += 0.005) sharpTop = std::max(sharpTop, 20.0 * std::log10(rms(slice(sharp, static_cast<std::size_t>(t * kRate), static_cast<std::size_t>((t + 0.01) * kRate))) + 1e-9) - sharpBase);
        MESSAGE(std::string(id) << " : départ " << start << " dB, sommet " << top << " dB, aigus " << sharpTop << " dB, ensuite " << after << " dB");
        CHECK(sharpTop > 13.0);
        CHECK(start < top - 8.0);
        CHECK(top > 6.0);
        CHECK(top < 18.0);
        CHECK(after < top - 4.0);
    }
}

// Le brevet US8302497 décrit plusieurs chocs. Un seul, c'est un clic posé devant un bruit.
TEST_CASE("l'engagement est fait de plusieurs chocs") {
    // Le bruit change à chaque démarrage, pas les chocs : à force de moyenner, il ne reste qu'eux.
    constexpr std::size_t kFrames = 44;
    constexpr int kFrame = 33;
    for (const char* id : {"city_14", "v8_cross_plane"}) {
        INFO(id);
        const Powertrain powertrain = findPreset(id)->powertrain();
        DrivingModel model(powertrain);
        auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
        starter->setup(powertrain.profile().starter, 1.0f);
        std::vector<double> energy(kFrames, 0.0);
        for (int take = 0; take < 32; ++take) {
            starter->setState(false, false, 0.0f);
            for (int i = 0; i < kRate / 10; ++i) starter->sample(0.0f, false, 0.0f);
            Take once;
            crankOnce(*starter, model, once);
            const Samples sharp = bandPassed(slice(once.sound, 0, kFrames * kFrame), 7000.0f, 1.0f);
            for (std::size_t f = 0; f < kFrames; ++f) {
                for (int i = 0; i < kFrame; ++i) energy[f] += static_cast<double>(sharp[f * kFrame + static_cast<std::size_t>(i)]) * sharp[f * kFrame + static_cast<std::size_t>(i)];
            }
        }
        int shocks = 0;
        for (std::size_t f = 1; f + 1 < kFrames; ++f) {
            const double before = *std::min_element(energy.begin() + static_cast<std::ptrdiff_t>(f >= 4 ? f - 4 : 0), energy.begin() + static_cast<std::ptrdiff_t>(f));
            if (energy[f] > energy[f - 1] && energy[f] >= energy[f + 1] && energy[f] > before * 2.0) ++shocks;
        }
        MESSAGE(std::string(id) << " : " << shocks << " chocs");
        CHECK(shocks >= 3);
    }
}

// Aucune compression n'est identique ; sinon, c'est un métronome.
TEST_CASE("deux compressions ne se ressemblent pas tout à fait") {
    DrivingModel model(findPreset("four_cyl_sport")->powertrain());
    model.halt();
    model.crank();
    std::vector<double> lows;
    double previous = 0.0, before = 0.0;
    for (double t = 0.0; model.cranking(); t += 0.001) {
        model.stepNeutral(0.001, 0.0);
        const double rpm = model.rpm();
        if (t > 0.25 && previous < before && previous < rpm) lows.push_back(previous);
        before = previous;
        previous = rpm;
    }
    REQUIRE(lows.size() >= 4);
    const auto [least, most] = std::minmax_element(lows.begin(), lows.end());
    MESSAGE("creux de " << *least << " à " << *most << " tr/min");
    CHECK(*most - *least > 12.0);
    // Le son aussi : à couple strictement périodique, l'énergie de chaque bouffée varie encore, bien plus que le
    // bruit seul ne le ferait sur cent millisecondes.
    const StarterSpec spec = findPreset("v8_cross_plane")->profile().starter;
    auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
    starter->setup(spec, 1.0f);
    constexpr int kPeriod = kBlock * 34;
    std::vector<double> bursts;
    Samples cycle(kPeriod);
    for (int n = 0; n < 60; ++n) {
        for (int i = 0; i < kPeriod; ++i) {
            if (i % kBlock == 0) {
                const float phase = static_cast<float>(i) / static_cast<float>(kPeriod);
                starter->setState(true, true, 0.2f + 0.15f * dsp::sinTurns(phase));
            }
            cycle[static_cast<std::size_t>(i)] = starter->sample(250.0f, true, 1.0f);
        }
        if (n >= 10) bursts.push_back(20.0 * std::log10(rms(bandPassed(cycle, 1400.0f, 0.8f))));
    }
    double mean = 0.0, spread = 0.0;
    for (double p : bursts) mean += p;
    mean /= static_cast<double>(bursts.size());
    for (double p : bursts) spread += (p - mean) * (p - mean);
    spread = std::sqrt(spread / static_cast<double>(bursts.size()));
    MESSAGE("bouffées à couple périodique : écart " << spread << " dB");
    CHECK(spread > 1.5);
}

// Un vrai V8 a toujours des compressions en cours : ses dents ne se quittent pas. Un 4 cylindres, si.
TEST_CASE("le vilebrequin d'un 4 cylindres file devant le pignon, pas celui d'un V8") {
    const auto negativeShare = [](const char* id) {
        const Take take = crankOf(id);
        std::size_t negative = 0, counted = 0;
        for (std::size_t b = kRate / 4 / kBlock; b < take.load.size(); ++b, ++counted) negative += take.load[b] < 0.0f ? 1u : 0u;
        return static_cast<double>(negative) / static_cast<double>(counted);
    };
    CHECK(negativeShare("four_cyl_sport") > 0.1);
    CHECK(negativeShare("v8_cross_plane") == doctest::Approx(0.0));
}

TEST_CASE("le démarreur n'alloue rien et ne dépend que de ses entrées") {
    const Powertrain powertrain = findPreset("v8_cross_plane")->powertrain();
    auto a = std::make_unique<StarterSound>(static_cast<float>(kRate));
    auto b = std::make_unique<StarterSound>(static_cast<float>(kRate));
    a->setup(powertrain.profile().starter, 1.0f);
    b->setup(powertrain.profile().starter, 1.0f);
    DrivingModel modelA(powertrain), modelB(powertrain);
    Take takeA, takeB;
    takeA.sound.reserve(kRate * 3);
    takeA.load.reserve(kRate);
    const long before = g_allocations.load();
    crankOnce(*a, modelA, takeA);
    CHECK(g_allocations.load() == before);
    crankOnce(*b, modelB, takeB);
    REQUIRE(takeA.sound.size() == takeB.sound.size());
    bool same = true;
    for (std::size_t i = 0; i < takeA.sound.size(); ++i) same = same && takeA.sound[i] == takeB.sound[i];
    CHECK(same);
}

// Le relais et le plongeur doivent se rejouer, et le pignon repartir de zéro : le filtre du lanceur, resté sur le
// régime du moteur, faisait croire à un pignon rapide, donc sans courant ni balais, pendant une demi-seconde.
TEST_CASE("le deuxième démarrage ressemble au premier") {
    const Powertrain powertrain = findPreset("city_14")->powertrain();
    DrivingModel model(powertrain);
    auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
    starter->setup(powertrain.profile().starter, 1.0f);
    Take first, second;
    crankOnce(*starter, model, first);
    // Le moteur prend et tourne, le pignon sort, le démarreur se tait.
    for (int block = 0; block < 2 * kRate / kBlock; ++block) {
        model.stepNeutral(static_cast<double>(kBlock) / kRate, 0.0);
        starter->setState(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()));
        for (int i = 0; i < kBlock; ++i) starter->sample(static_cast<float>(model.rpm()), false, 0.0f);
    }
    // Il cale : on relance.
    crankOnce(*starter, model, second);
    // Avant que le pignon n'entre : seuls le relais et le plongeur s'entendent.
    const auto attack = [](const Take& take) { return 20.0 * std::log10(rms(slice(take.sound, 0, kRate * 18 / 1000)) + 1e-9); };
    const auto brushes = [](const Take& take) { return 20.0 * std::log10(rms(bandPassed(slice(take.sound, kRate / 10, kRate * 3 / 10), 7000.0f, 1.0f)) + 1e-9); };
    MESSAGE("attaque " << attack(first) << " dB, puis " << attack(second) << " dB ; balais " << brushes(first) << " dB, puis " << brushes(second) << " dB");
    CHECK(std::fabs(attack(first) - attack(second)) < 3.0);
    CHECK(std::fabs(brushes(first) - brushes(second)) < 2.0);
}

TEST_CASE("le démarreur se tait et ne coûte plus rien après son dernier écho") {
    const Powertrain powertrain = findPreset("city_14")->powertrain();
    DrivingModel model(powertrain);
    auto starter = std::make_unique<StarterSound>(static_cast<float>(kRate));
    starter->setup(powertrain.profile().starter, 1.0f);
    Take take;
    crankOnce(*starter, model, take);
    starter->setState(false, false, 0.0f);
    Samples tail(kRate);
    for (float& s : tail) s = starter->sample(0.0f, false, 0.0f);
    CHECK(rms(slice(tail, kRate / 2, kRate)) == 0.0);
}

TEST_CASE("le démarreur reste dans ses limites à tous les taux d'échantillonnage") {
    const Powertrain powertrain = findPreset("v8_cross_plane")->powertrain();
    for (const float rate : {22050.0f, 44100.0f, 48000.0f, 96000.0f}) {
        INFO(rate);
        DrivingModel model(powertrain);
        auto starter = std::make_unique<StarterSound>(rate);
        starter->setup(powertrain.profile().starter, 1.0f);
        model.halt();
        model.crank();
        const int block = static_cast<int>(rate * 0.003f);
        float top = 0.0f;
        bool finite = true;
        while (model.cranking()) {
            model.stepNeutral(static_cast<double>(block) / rate, 0.0);
            starter->setState(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()));
            for (int i = 0; i < block; ++i) {
                const float s = starter->sample(static_cast<float>(model.rpm()), true, 1.0f);
                finite = finite && std::isfinite(s);
                top = std::max(top, std::fabs(s));
            }
        }
        CHECK(finite);
        CHECK(top < 2.0f);
        CHECK(top > 0.02f);
    }
}
