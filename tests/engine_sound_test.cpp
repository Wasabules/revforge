#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "revforge/driving_model.hpp"
#include "revforge/presets.hpp"
#include "test_support.hpp"

using namespace revforge;
using namespace test;

extern std::atomic<long> g_allocations;

namespace {

constexpr double kPi = 3.141592653589793;

std::vector<const Preset*> allPresets() {
    std::size_t count = 0;
    const Preset* all = presets(count);
    std::vector<const Preset*> list;
    for (std::size_t i = 0; i < count; ++i) list.push_back(&all[i]);
    return list;
}

EngineProfile profileOf(const char* id) { return findPreset(id)->profile(); }

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

/** Énergie au-dessus de `hz`, sur des raies réparties, en dB sous le signal. */
double highBandDb(const Samples& x, double hz) {
    const Samples head = slice(x, 0, 8192);
    double band = 0.0, full = 0.0;
    for (int i = 0; i < 24; ++i) band += powerAt(head, hz + i * 400.0);
    for (int i = 0; i < 240; ++i) full += powerAt(head, 40.0 + i * 90.0);
    return 10.0 * std::log10(band / full);
}

/** Autocorrélation maximale sur les périodes de 15 à 400 Hz, signal vu au-dessus de ~500 Hz. */
double highBandPeriodicity(const Samples& x) {
    Samples d(x.size(), 0.0f);
    for (std::size_t i = 2; i < x.size(); ++i) d[i] = x[i] - 2.0f * x[i - 1] + x[i - 2];
    const int n = std::min(static_cast<int>(d.size()), kRate) - kRate / 15;
    double energy = 0.0;
    for (int i = 0; i < n; ++i) energy += static_cast<double>(d[i]) * d[i];
    double best = 0.0;
    for (int lag = kRate / 400; lag <= kRate / 15; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += static_cast<double>(d[i]) * d[i + lag];
        best = std::max(best, sum / energy);
    }
    return best;
}

}  // namespace

TEST_CASE("le résultat ne dépend pas de la taille des blocs") {
    const EngineProfile p = profileOf("four_cyl_sport");
    const Samples a = sweep(p, 64);
    const Samples b = sweep(p, 1024);
    CHECK(a == b);
}

TEST_CASE("n'écrête jamais à plein volume") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        CHECK(peak(sweep(preset->profile())) < 0.99f);
    }
}

TEST_CASE("pas de clic quand le régime bouge") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        // Détonations et décharge du turbo sont voulues : on cherche ici les clics parasites.
        EngineConfig clean = preset->config;
        clean.pops = PopLevel::Off;
        clean.aspiration = Aspiration::Natural;
        const Samples x = sweep(preset->electric ? preset->profile() : clean.toProfile());
        std::vector<float> jumps;
        jumps.reserve(x.size());
        for (std::size_t i = 1; i < x.size(); ++i) jumps.push_back(std::fabs(x[i] - x[i - 1]));
        std::sort(jumps.begin(), jumps.end());
        // Un clic est un saut hors de proportion avec le signal qui l'entoure.
        CHECK(jumps.back() < jumps[static_cast<std::size_t>(static_cast<double>(jumps.size()) * 0.99)] * 6.0f);
        CHECK(jumps.back() < 0.55f);
    }
}

// Ils ne jouent plus au même niveau : une citadine est bien plus discrète qu'une ligne libre. Mais aucun n'est
// inaudible, et la correspondance vers la sortie les garde tous sous le seuil du limiteur.
TEST_CASE("aucun moteur n'est inaudible, aucun ne sature") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        const double level = rms(steady(preset->profile(), 3000.0f));
        CHECK(level >= (preset->electric ? 0.002 : 0.01));
        CHECK(level <= 0.45);
    }
}

TEST_CASE("le limiteur reste à l'écart à volume normal") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        CHECK(peak(steady(preset->profile(), 3000.0f, 3.0f, 0.6f)) < 0.9f);
    }
}

TEST_CASE("la cadence d'allumage s'entend") {
    for (const char* id : {"four_cyl_sport", "v6_gt", "diesel_four"}) {
        INFO(id);
        const EngineProfile p = profileOf(id);
        const double firingHz = 3000.0 / 120.0 * p.firingCount;
        const Samples x = steady(p, 3000.0f);
        CHECK(powerAt(x, firingHz) > powerAt(x, firingHz * 1.5) * 4.0);
    }
}

TEST_CASE("les explosions continuent dans la durée") {
    for (const Preset* preset : allPresets()) {
        if (preset->electric) continue;
        INFO(preset->id);
        const EngineProfile p = preset->profile();
        const double firingHz = 3000.0 / 120.0 * p.firingCount;
        const Samples x = steady(p, 3000.0f, 12.0f);
        const Samples last = slice(x, x.size() - kRate, x.size());
        // Au-delà de 1 kHz, la turbulence a remplacé les raies : c'est voulu.
        double harmonics = 0.0;
        for (int k = 2; k <= 6 && firingHz * k < 1000.0; ++k) harmonics += powerAt(last, firingHz * k);
        CHECK(10.0 * std::log10(harmonics / powerAt(last, firingHz)) > -30.0);
    }
}

// Le défaut « moustique » : des raies trop pures en haut du spectre. De vraies voitures
// mesurées en charge donnent 0,25 à 0,35 ; l'ancien modèle à raies donnait 0,5 à 0,7.
TEST_CASE("ne siffle pas comme un moustique") {
    for (const Preset* preset : allPresets()) {
        if (preset->electric) continue;
        INFO(preset->id);
        const EngineProfile p = preset->profile();
        const Samples x = steady(p, p.idleRpm + (p.redlineRpm - p.idleRpm) * 0.5f, 2.5f);
        CHECK(highBandPeriodicity(x) < 0.45);
    }
}

TEST_CASE("deux cycles ne sont jamais identiques") {
    const Samples x = steady(profileOf("city_14"), 2000.0f);
    const std::size_t cycle = static_cast<std::size_t>(kRate) * 120 / 2000;
    const Samples a = slice(x, kRate, kRate + cycle);
    const Samples b = slice(x, kRate + cycle, kRate + 2 * cycle);
    Samples difference(cycle);
    for (std::size_t i = 0; i < cycle; ++i) difference[i] = a[i] - b[i];
    // Sans aucun aléa, cet écart tombe à 0,04.
    CHECK(rms(difference) > rms(a) * 0.1);
}

// Le défaut « étouffé » : il faut de l'énergie jusque dans les aigus, comme en réel. Dix voitures
// mesurées vont de -47 à -3 dB, médiane -32.
TEST_CASE("garde son grain dans les aigus") {
    for (const Preset* preset : allPresets()) {
        if (preset->electric) continue;
        INFO(preset->id);
        const EngineProfile p = preset->profile();
        const Samples x = steady(p, p.idleRpm + (p.redlineRpm - p.idleRpm) * 0.6f);
        Samples d(x.size(), 0.0f);
        for (std::size_t i = 3; i < x.size(); ++i) d[i] = x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3];
        // Le sol double la pression dans les graves : 6 dB de moins sur ce rapport qu'avant la position d'écoute.
        const double grain = 20.0 * std::log10(rms(d) / rms(x));
        MESSAGE(preset->id << " : grain " << grain << " dB");
        CHECK(grain > -40.0);
    }
}

TEST_CASE("un moteur irrégulier a des sous-harmoniques") {
    const double cycleHz = 2000.0 / 120.0;
    const Samples x = steady(profileOf("v8_cross_plane"), 2000.0f);
    double between = 0.0;
    for (int k = 2; k <= 7; ++k) between = std::max(between, powerAt(x, cycleHz * k));
    CHECK(between / powerAt(x, cycleHz * 8) > 0.01);
}

TEST_CASE("pas de repliement en haut du spectre") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        const EngineProfile p = preset->profile();
        CHECK(highBandDb(steady(p, p.redlineRpm), 14000.0) < -25.0);
    }
}

TEST_CASE("moteur coupé : silence après un fondu") {
    EngineSound core(kRate);
    core.setVolume(0.8f);
    core.setTargetRpm(3000.0f);
    Samples running(kRate);
    core.render(running.data(), kRate);
    core.setTargetRpm(0.0f);
    Samples fading(kRate);
    core.render(fading.data(), kRate);
    CHECK(core.isSilent());
    CHECK(worstJump(fading) < 0.2f);
    CHECK(peak(slice(fading, fading.size() - 100, fading.size())) <= 1e-3f);
}

TEST_CASE("le son reprend après un moteur à l'arrêt") {
    EngineSound core(48000);
    core.setVolume(0.7f);
    Samples idle(48000 * 2);
    core.setTargetRpm(0.0f);
    core.render(idle.data(), static_cast<int>(idle.size()));
    CHECK(core.isSilent());
    CHECK(peak(idle) == 0.0f);
    core.setTargetRpm(2000.0f);
    CHECK_FALSE(core.isSilent());
    Samples running(48000);
    core.render(running.data(), 48000);
    CHECK(rms(running) > 0.05);
}

TEST_CASE("changer de moteur ne fait pas de clic") {
    EngineSound core(kRate);
    core.setVolume(0.8f);
    core.setTargetRpm(3000.0f);
    Samples first(kRate);
    core.render(first.data(), kRate);
    core.setProfile(profileOf("v8_cross_plane"));
    Samples switching(kRate);
    core.render(switching.data(), kRate);
    CHECK(worstJump(switching) < 0.2f);
    CHECK(peak(slice(switching, switching.size() - kRate / 4, switching.size())) > 0.01f);
}

TEST_CASE("fonctionne au taux natif des navigateurs") {
    for (int rate : {48000, 96000}) {
        for (const Preset* preset : allPresets()) {
            INFO(preset->id);
            EngineSound core(rate);
            core.setProfile(preset->profile());
            core.setVolume(0.7f);
            core.setTargetRpm(3000.0f);
            Samples out(static_cast<std::size_t>(rate) * 3);
            core.render(out.data(), static_cast<int>(out.size()));
            const Samples played = slice(out, rate / 2, out.size());
            CHECK(std::none_of(played.begin(), played.end(), [](float s) { return std::isnan(s); }));
            CHECK(peak(played) < 0.99f);
            CHECK(rms(played) > 0.002);
        }
    }
}

TEST_CASE("le rendu n'alloue jamais de mémoire") {
    // Le plus chargé : tout activé, avec changement de moteur en plein rendu.
    EngineConfig config;
    config.cylinders = 12;
    config.layout = Layout::V;
    config.exhaust = Exhaust::Straight;
    config.aspiration = Aspiration::Turbo;
    config.pops = PopLevel::Strong;
    const EngineProfile big = config.toProfile();
    const EngineProfile other = profileOf("diesel_four");
    EngineSound core(kRate);
    core.setProfile(big);
    core.setVolume(0.8f);
    Samples out(kRate);
    const long before = g_allocations.load();
    for (int i = 0; i < 40; ++i) {
        core.setTargetRpm(i % 2 == 0 ? 7000.0f : 1200.0f);
        if (i == 20) core.setProfile(other);
        core.render(out.data(), 4096);
    }
    CHECK(g_allocations.load() == before);
}

TEST_CASE("assez léger pour un autoradio") {
    EngineSound core(kRate);
    core.setTargetRpm(4000.0f);
    Samples out(static_cast<std::size_t>(kRate) * 10);
    const auto started = std::chrono::steady_clock::now();
    core.render(out.data(), static_cast<int>(out.size()));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    CHECK(seconds < 2.0);
    MESSAGE("Coût : " << seconds / 10.0 * 100.0 << " % d'un cœur");
}

namespace {

constexpr double kNoiseCombDb = 8.0;

/** Moteur tenu à ce régime et ce remplissage, sans la première seconde. */
Samples held(const EngineProfile& p, float rpm, float load, Listener listener = Listener::Outside, float seconds = 4.0f) {
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setListener(listener);
    core.setTargetRpm(rpm);
    core.setDrive(load, 0.0f, false, false);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    core.render(out.data(), static_cast<int>(out.size()));
    return slice(out, kRate, out.size());
}

/**
 * Hauteur des raies au-dessus du fond (dB) entre deux fréquences, sur des fenêtres courtes : la
 * lente dérive du régime étalerait les raies sur une longue fenêtre et les cacherait.
 * Un simple bruit donne environ 8 dB.
 */
double combDb(const Samples& x, double fromHz, double toHz) {
    constexpr std::size_t kWindow = 8192;
    const double resolution = static_cast<double>(kRate) / static_cast<double>(kWindow);
    std::vector<double> scores;
    for (std::size_t start = 0; start + kWindow <= x.size() && scores.size() < 6; start += kWindow * 2) {
        const Samples part = slice(x, start, start + kWindow);
        std::vector<double> bins;
        for (double hz = fromHz; hz < toHz; hz += resolution) bins.push_back(powerAt(part, hz));
        std::sort(bins.begin(), bins.end());
        const double top = bins[static_cast<std::size_t>(static_cast<double>(bins.size()) * 0.98)];
        scores.push_back(10.0 * std::log10(top / bins[bins.size() / 2]));
    }
    std::sort(scores.begin(), scores.end());
    return scores[scores.size() / 2];
}

}  // namespace

// Le défaut « fermeture éclair » : des raies aussi nettes à 3 kHz qu'à 300 Hz. Le défaut inverse,
// trouvé ensuite par un audit : plus aucune raie au-dessus de 800 Hz. Mesuré sur de vrais moteurs entre
// 1,5 et 3,5 kHz : de 8,7 à 17,1 dB (GT500, Cobra, MX-5, 106, S550, GTV6, RC F, S2000).
TEST_CASE("les raies s'effacent progressivement vers les aigus, sans disparaître") {
    EngineConfig big;
    big.cylinders = 8;
    big.layout = Layout::CrossPlane;
    big.displacementCc = 7000;
    big.boreMm = 99;
    big.redlineRpm = 6000;
    big.exhaust = Exhaust::Sport;
    for (float rpm : {2500.0f, 4500.0f}) {
        INFO(rpm);
        const Samples x = held(big.toProfile(), rpm, 1.0f);
        const double low = combDb(x, 100.0, 800.0);
        const double high = combDb(x, 1500.0, 3500.0);
        MESSAGE("raies à " << rpm << " tr/min : " << low << " dB en bas, " << high << " dB en haut");
        CHECK(high > 8.5);
        CHECK(high < 18.0);
        CHECK(low > high + 6.0);
    }
}

TEST_CASE("le silencieux ne suit pas le régime : l'énergie reste dans les mêmes bosses") {
    // Un timbre qui monte avec la hauteur, c'est un jouet : ici le centre de gravité monte peu.
    // Tout près de la sortie, pour n'entendre qu'elle : le bloc, lui, monte vraiment avec le régime.
    const EngineProfile p = profileOf("v8_cross_plane");
    const Point3& outlet = p.car.sources[static_cast<std::size_t>(Source::ExhaustA)].at;
    const auto heard = [&p, &outlet](float rpm) {
        EngineSound core(kRate);
        core.setProfile(p);
        core.setVolume(0.7f);
        core.setListenerPosition(outlet.x - 0.7f, outlet.y + 0.7f, outlet.z + 0.1f);
        core.setTargetRpm(rpm);
        core.setDrive(1.0f, 0.0f, false, false);
        Samples out(kRate * 4);
        core.render(out.data(), kRate * 4);
        return slice(out, kRate, out.size());
    };
    const Samples slow = heard(2000.0f);
    const Samples fast = heard(4000.0f);
    const double low = slopeRms(slow) / rms(slow);
    const double high = slopeRms(fast) / rms(fast);
    MESSAGE("brillance près de la sortie : " << low << " à 2000 tr/min, " << high << " à 4000 tr/min");
    CHECK(high / low < 1.8);
}

TEST_CASE("plein gaz, le moteur est bien plus fort que pied léger, même dans la durée") {
    const EngineProfile p = profileOf("four_cyl_sport");
    EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(0.7f);
    core.setTargetRpm(3500.0f);
    Samples out(kRate * 8);
    core.setDrive(1.0f, 0.0f, false, false);
    core.render(out.data(), static_cast<int>(out.size()));
    const double full = rms(slice(out, out.size() - kRate, out.size()));
    core.setDrive(0.2f, 0.0f, false, false);
    core.render(out.data(), static_cast<int>(out.size()));
    const double light = rms(slice(out, out.size() - kRate, out.size()));
    CHECK(full > light * 1.6);
}

TEST_CASE("régler la cylindrée pendant que le moteur tourne ne coupe pas le son") {
    EngineConfig c;
    EngineSound core(kRate);
    core.setProfile(c.toProfile());
    core.setVolume(0.7f);
    core.setTargetRpm(3000.0f);
    Samples out(kRate);
    core.render(out.data(), kRate);
    Samples block(1024);
    float lowest = 1.0f;
    for (int cc = 2000; cc < 3000; cc += 50) {
        c.displacementCc = cc;
        core.setProfile(c.toProfile());
        core.render(block.data(), 1024);
        lowest = std::min(lowest, peak(block));
        CHECK(worstJump(block) < 0.3f);
    }
    CHECK(lowest > 0.02f);
}

TEST_CASE("chaque position d'écoute s'entend") {
    const EngineProfile p = profileOf("four_cyl_sport");
    const auto brightnessAt = [&p](Listener listener) {
        const Samples x = held(p, 4000.0f, 1.0f, listener);
        return slopeRms(x) / rms(x);
    };
    const double outside = brightnessAt(Listener::Outside);
    CHECK(brightnessAt(Listener::Cabin) < outside);
    CHECK(brightnessAt(Listener::EngineBay) > outside);
}

TEST_CASE("le vent et les pneus montent avec la vitesse") {
    const auto noiseAt = [](float speed) {
        EngineSound core(kRate);
        core.setVolume(0.7f);
        core.setTargetRpm(1500.0f);
        core.setDrive(0.3f, 0.0f, false, false);
        core.setRoad(speed, 0.0f);
        Samples out(kRate * 2);
        core.render(out.data(), static_cast<int>(out.size()));
        return rms(slice(out, kRate, out.size()));
    };
    CHECK(noiseAt(50.0f) > noiseAt(0.0f));
}

TEST_CASE("le démarreur s'entend sans explosions") {
    EngineSound core(kRate);
    core.setVolume(0.7f);
    core.setTargetRpm(240.0f);
    core.setDrive(0.0f, 0.0f, true, true);
    core.setStarter(true, true);
    Samples out(kRate);
    core.render(out.data(), kRate);
    CHECK(rms(slice(out, kRate / 4, out.size())) > 0.01);
    CHECK(core.pops() == 0u);
}

TEST_CASE("le rendu reste léger, même à douze cylindres") {
    EngineConfig c;
    c.cylinders = 12;
    c.layout = Layout::V;
    c.exhaust = Exhaust::Straight;
    c.aspiration = Aspiration::Turbo;
    c.pops = PopLevel::Strong;
    EngineSound core(kRate);
    core.setProfile(c.toProfile());
    core.setTargetRpm(7000.0f);
    core.setDrive(1.0f, 1.0f, false, false);
    core.setRoad(60.0f, 0.0f);
    core.setListener(Listener::Cabin);
    Samples out(static_cast<std::size_t>(kRate) * 5);
    const auto started = std::chrono::steady_clock::now();
    core.render(out.data(), static_cast<int>(out.size()));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    CHECK(seconds < 1.0);
    MESSAGE("Coût à 12 cylindres : " << seconds / 5.0 * 100.0 << " % d'un cœur");
}

namespace {

/** Niveau sur des tranches de 4 ms. */
std::vector<double> envelope(const Samples& x) {
    constexpr std::size_t kFrame = 176;
    std::vector<double> levels;
    for (std::size_t at = 0; at + kFrame <= x.size(); at += kFrame) levels.push_back(rms(slice(x, at, at + kFrame)));
    return levels;
}

double percentile(std::vector<double> values, double share) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(static_cast<double>(values.size() - 1) * share)];
}

/** Le moteur conduit par le modèle, pied au plancher, au point mort : il finit sur le rupteur. */
Samples onLimiter(const Powertrain& powertrain, unsigned* pops = nullptr) {
    DrivingModel model(powertrain);
    EngineSound core(kRate);
    core.setProfile(powertrain.profile());
    core.setVolume(0.7f);
    Samples out(static_cast<std::size_t>(kRate) * 6);
    constexpr int kBlock = 128;
    const double dt = static_cast<double>(kBlock) / kRate;
    for (std::size_t done = 0; done + kBlock <= out.size(); done += kBlock) {
        model.stepNeutral(dt, 1.0);
        core.setTargetRpm(static_cast<float>(model.rpm()));
        core.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), false);
        core.render(out.data() + done, kBlock);
    }
    if (pops != nullptr) *pops = core.pops();
    return slice(out, out.size() - kRate * 3, out.size());
}

/** Part du sifflement du turbo dans ce qu'on entend (dB) : la bande où il se trouve, par rapport à tout le signal. */
double whistleLevel(Listener listener, float bar, double fromHz = 4000.0, double toHz = 13000.0) {
    EngineConfig c;
    c.aspiration = Aspiration::Turbo;
    c.boostBar = bar;
    EngineSound core(kRate);
    core.setProfile(c.toProfile());
    core.setVolume(0.7f);
    core.setListener(listener);
    core.setTargetRpm(4500.0f);
    core.setDrive(1.0f, 1.0f, false, false);
    Samples out(kRate * 5);
    core.render(out.data(), static_cast<int>(out.size()));
    const Samples last = slice(slice(out, out.size() - kRate, out.size()), 0, 8192);
    // Le sifflement monte avec la turbine : énergie dans la bande où il se trouve.
    double energy = 0.0;
    for (double hz = fromHz; hz <= toHz; hz += 40.0) energy += powerAt(last, hz);
    double all = 0.0;
    for (std::size_t i = 0; i < last.size(); ++i) all += static_cast<double>(last[i]) * last[i];
    return 10.0 * std::log10(energy / all);
}

}  // namespace

// Le défaut du rupteur « pas réaliste » : une note tenue, à peine agitée. Un vrai rupteur coupe
// franchement : le son tombe et revient, une dizaine de fois par seconde.
TEST_CASE("le rupteur s'entend : le son tombe et revient") {
    for (const char* id : {"four_cyl_sport", "v8_cross_plane"}) {
        INFO(id);
        const std::vector<double> levels = envelope(onLimiter(findPreset(id)->powertrain()));
        const double contrast = 20.0 * std::log10(percentile(levels, 0.9) / percentile(levels, 0.1));
        MESSAGE(id << " : contraste au rupteur " << contrast << " dB");
        CHECK(contrast > 8.0);
    }
}

TEST_CASE("le rupteur ne déclenche pas la décharge du turbo") {
    // Le papillon reste ouvert : aucune raison de décharger, seul un vrai lever de pied le fait.
    const Powertrain p = findPreset("hot_hatch_turbo")->powertrain();
    EngineSound core(kRate);
    core.setProfile(p.profile());
    core.setVolume(0.7f);
    core.setTargetRpm(6000.0f);
    core.setDrive(1.0f, 1.0f, false, false);
    Samples block(kRate);
    core.render(block.data(), kRate);
    core.render(block.data(), kRate);
    for (int i = 0; i < 20; ++i) {
        core.setDrive(1.0f, 1.0f, i % 2 == 0, false);
        core.render(block.data(), 2205);
        REQUIRE(core.turboRelease() == 0.0f);
    }
    core.setDrive(0.0f, 1.0f, false, false);
    core.render(block.data(), 4410);
    CHECK(core.turboRelease() > 0.05f);
}

TEST_CASE("la détonation à la reprise du rupteur dépend du réglage") {
    Powertrain p = findPreset("v8_cross_plane")->powertrain();
    p.engine.pops = PopLevel::Off;
    unsigned none = 0, strong = 0;
    onLimiter(p, &none);
    p.engine.pops = PopLevel::Strong;
    onLimiter(p, &strong);
    CHECK(none == 0u);
    CHECK(strong > 3u);
}

TEST_CASE("le sifflement du turbo dépend d'où l'on écoute") {
    // À pleine pression, la turbine siffle vers 7,1 kHz : ce que la raie dépasse de ses voisines.
    const auto prominence = [](Listener listener) {
        EngineConfig c;
        c.aspiration = Aspiration::Turbo;
        c.boostBar = 1.0f;
        EngineSound core(kRate);
        core.setProfile(c.toProfile());
        core.setVolume(0.7f);
        core.setListener(listener);
        core.setTargetRpm(4500.0f);
        core.setDrive(1.0f, 1.0f, false, false);
        Samples out(kRate * 6);
        core.render(out.data(), static_cast<int>(out.size()));
        const Samples last = slice(out, out.size() - 16384, out.size());
        const auto band = [&last](double from, double to) {
            double sum = 0.0;
            int count = 0;
            for (double hz = from; hz <= to; hz += 10.0, ++count) sum += powerAt(last, hz);
            return sum / count;
        };
        return 10.0 * std::log10(band(6900.0, 7300.0) / (0.5 * (band(5600.0, 6400.0) + band(7800.0, 8600.0))));
    };
    const double cabin = prominence(Listener::Cabin);
    const double front = prominence(Listener::Front);
    MESSAGE("sifflement : devant " << front << " dB, habitacle " << cabin << " dB au-dessus du fond");
    // Il sort par l'admission, à l'avant, et l'habitacle retient ces aigus.
    CHECK(front > cabin + 4.0);
}

TEST_CASE("les soupapes s'entendent sous le capot, à peine dehors") {
    const EngineProfile p = profileOf("four_cyl_sport");
    const auto hiss = [&p](Listener listener) {
        const Samples x = held(p, p.idleRpm, 0.2f, listener);
        Samples d(x.size(), 0.0f);
        for (std::size_t i = 3; i < x.size(); ++i) d[i] = x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3];
        return rms(d);
    };
    CHECK(hiss(Listener::EngineBay) > hiss(Listener::Outside) * 2.0);
}

TEST_CASE("plus de pression, une turbine plus rapide : le sifflement monte") {
    CHECK(whistleLevel(Listener::EngineBay, 2.5f, 7500.0, 10000.0) > whistleLevel(Listener::EngineBay, 0.5f, 7500.0, 10000.0));
}

// Le défaut « métal qui claque » : des salves de bruit aiguës, à niveau fixe, à chaque mouvement de
// soupape. Quand l'échappement se fait discret à bas régime, elles passaient devant.
TEST_CASE("au ralenti, pas de salves dans les aigus") {
    for (const char* id : {"four_cyl_sport", "city_14", "v8_cross_plane", "v12"}) {
        INFO(id);
        const EngineProfile p = profileOf(id);
        const Samples x = held(p, p.idleRpm, 0.2f);
        Samples hiss(x.size(), 0.0f);
        for (std::size_t i = 3; i < x.size(); ++i) hiss[i] = x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3];
        const std::vector<double> levels = envelope(hiss);
        const double punch = percentile(levels, 0.97) / percentile(levels, 0.4);
        MESSAGE(id << " : salves " << punch);
        CHECK(punch < 1.7);
    }
}

// Le défaut « bruit blanc derrière le moteur » : un souffle à niveau presque fixe, sans rythme, jusqu'en haut du
// spectre. De vrais ralentis ont de -15,5 à -26 dB entre 8-16 kHz et 500-2000 Hz (Saxo, MX-5, 107, RC F, Cobra) ;
// le défaut donnait de -5 à -9 dB. Contre-épreuve : le test échoue sans `kNoiseTopHz`.
TEST_CASE("au ralenti, pas de bruit blanc derrière le moteur") {
    const auto bandPower = [](const Samples& x, double from, double to) {
        double sum = 0.0;
        const Samples head = slice(x, 0, 16384);
        for (double hz = from; hz < to; hz += 50.0) sum += powerAt(head, hz);
        return sum;
    };
    for (const char* id : {"four_cyl_sport", "city_14", "v8_cross_plane", "v12"}) {
        INFO(id);
        const EngineProfile p = profileOf(id);
        const Samples x = held(p, p.idleRpm, 0.1f);
        const double air = 10.0 * std::log10(bandPower(x, 8000.0, 16000.0) / bandPower(x, 500.0, 2000.0));
        MESSAGE(id << " : 8-16 kHz à " << air << " dB sous 500-2000 Hz");
        CHECK(air < -11.0);
    }
}

namespace {

/** Le démarrage tel que le joue le site : contact, démarreur, le moteur qui prend. */
Samples starting(const char* id, Listener listener = Listener::EngineBay) {
    const Powertrain powertrain = findPreset(id)->powertrain();
    DrivingModel model(powertrain);
    model.halt();
    model.crank();
    EngineSound core(kRate);
    core.setProfile(powertrain.profile());
    core.setVolume(0.7f);
    // De près, comme les enregistrements sur lesquels le démarreur est calé.
    core.setListener(listener);
    Samples sound(static_cast<std::size_t>(kRate) * 5);
    constexpr int kBlock = 128;
    const double dt = static_cast<double>(kBlock) / kRate;
    for (std::size_t done = 0; done + kBlock <= sound.size(); done += kBlock) {
        model.stepNeutral(dt, 0.0);
        core.setTargetRpm(static_cast<float>(model.rpm()));
        core.setDrive(static_cast<float>(model.load()), static_cast<float>(model.boost()), model.cut(), model.cranking());
        core.setStarter(model.starterPowered(), model.starterMeshed(), static_cast<float>(model.starterLoad()));
        core.render(sound.data() + done, kBlock);
    }
    return sound;
}

/** Régime moyen pendant le lancement, en laissant passer le premier effort. */
double crankingRpm(const char* id) {
    DrivingModel model(findPreset(id)->powertrain());
    model.halt();
    model.crank();
    double sum = 0.0;
    int samples = 0;
    for (double t = 0.0; model.cranking(); t += 0.001) {
        model.stepNeutral(0.001, 0.0);
        if (t > 0.25) {
            sum += model.rpm();
            ++samples;
        }
    }
    return sum / samples;
}

/** Ressemblance du niveau avec lui-même, décalé de `lag` tranches de 4 ms. */
double selfSimilarity(const std::vector<double>& levels, std::size_t lag) {
    double mean = 0.0;
    for (double level : levels) mean += level;
    mean /= static_cast<double>(levels.size());
    double top = 0.0, bottom = 0.0;
    for (std::size_t i = 0; i < levels.size(); ++i) bottom += (levels[i] - mean) * (levels[i] - mean);
    for (std::size_t i = 0; i + lag < levels.size(); ++i) top += (levels[i] - mean) * (levels[i + lag] - mean);
    return top / bottom;
}

}  // namespace

// Le défaut « aspirateur » : une nappe de souffle régulière. Un vrai démarreur bat au rythme des
// compressions : 8,3 Hz mesurés sur un 4 cylindres de 1,8 L, 11,9 Hz sur un V8 de 5,4 L.
TEST_CASE("le démarreur bat au rythme des compressions") {
    for (const char* id : {"four_cyl_sport", "inline_six", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const double rpm = crankingRpm(id);
        const double beatHz = rpm / 120.0 * findPreset(id)->config.cylinders;
        const Samples cranking = slice(starting(id), kRate / 10, kRate * 65 / 100);
        const std::vector<double> levels = envelope(cranking);
        const std::size_t lag = static_cast<std::size_t>(1.0 / beatHz / 0.004 + 0.5);
        const double onBeat = selfSimilarity(levels, lag);
        const double offBeat = selfSimilarity(levels, lag / 2);
        MESSAGE(id << " : " << rpm << " tr/min, " << beatHz << " Hz, ressemblance " << onBeat << " sur le rythme, " << offBeat << " à contretemps");
        CHECK(onBeat > offBeat + 0.1);
    }
}

// Le régime n'est pas imposé : il vient du moteur électrique et des compressions, et il rejoint
// ce qu'on mesure sur de vrais démarrages (250 tr/min pour un 1.8, 180 pour un 5.4).
TEST_CASE("le régime de lancement vient de la mécanique") {
    const double small = crankingRpm("four_cyl_sport");
    CHECK(small > 220.0);
    CHECK(small < 310.0);
    CHECK(crankingRpm("v8_cross_plane") < small);
    CHECK(crankingRpm("v8_cross_plane") > 150.0);
}

TEST_CASE("chaque moteur a son démarreur") {
    const StarterSpec small = findPreset("city_14")->profile().starter;
    const StarterSpec big = findPreset("v8_cross_plane")->profile().starter;
    CHECK(small.kind == StarterKind::Reduction);
    CHECK(big.kind == StarterKind::Direct);
    CHECK(big.powerKw > small.powerKw);
    CHECK(big.ringTeeth > small.ringTeeth);
    CHECK(small.helixDeg > big.helixDeg);
    CHECK(findPreset("electric")->profile().starter.powerKw == 0.0f);
}

TEST_CASE("tous les moteurs démarrent") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (all[i].electric) continue;
        INFO(all[i].id);
        DrivingModel model(all[i].powertrain());
        model.halt();
        model.crank();
        double top = 0.0;
        for (int step = 0; step < 300; ++step) {
            model.stepNeutral(0.01, 0.0);
            top = std::max(top, model.rpm());
        }
        CHECK_FALSE(model.cranking());
        CHECK(top > model.idleRpm());
        CHECK(model.rpm() > model.idleRpm() * 0.9);
    }
}

TEST_CASE("le solénoïde claque avant que le démarreur ne tourne") {
    const Samples sound = starting("four_cyl_sport");
    // Sur de vrais démarrages, le premier choc arrive 8 à 18 ms après la mise sous tension.
    const float clack = peak(slice(sound, 0, kRate * 25 / 1000));
    const float cranking = peak(slice(sound, kRate / 5, kRate / 2));
    MESSAGE("claquement " << clack << ", démarreur " << cranking);
    CHECK(clack > 0.15f);
    CHECK(rms(slice(sound, 0, kRate * 25 / 1000)) > 0.03);
}

TEST_CASE("le moteur démarre plus fort que son ralenti, puis redescend") {
    for (const char* id : {"four_cyl_sport", "v8_cross_plane"}) {
        INFO(id);
        // Derrière la voiture : c'est l'échappement qui monte, le bloc s'entend à peine plus.
        const Samples sound = starting(id, Listener::Outside);
        const Samples flare = slice(sound, kRate * 9 / 10, kRate * 14 / 10);
        const Samples idle = slice(sound, kRate * 4, kRate * 5);
        CHECK(rms(flare) > rms(idle) * 1.4);
    }
}

TEST_CASE("le démarreur dépend d'où l'on écoute") {
    const auto starterAt = [](Listener listener) {
        EngineSound core(kRate);
        core.setVolume(0.7f);
        core.setListener(listener);
        core.setTargetRpm(250.0f);
        core.setDrive(0.0f, 0.0f, true, true);
        core.setStarter(true, true);
        Samples out(kRate);
        core.render(out.data(), kRate);
        return rms(slice(out, kRate / 2, out.size()));
    };
    CHECK(starterAt(Listener::EngineBay) > starterAt(Listener::Outside) * 1.5);
}

// Valeurs mesurées sur neuf vrais démarrages (tools/analyze_starter.py). Hauteur des raies, sur des
// fenêtres courtes : une longue étale une raie qui ondule et la fait passer pour faible.
double shortWindowLinesDb(const Samples& cranking) {
    constexpr std::size_t kWindow = 1024;
    std::vector<double> heights;
    for (std::size_t at = 0; at + kWindow <= cranking.size(); at += kWindow / 2) {
        const Samples part = slice(cranking, at, at + kWindow);
        std::vector<double> bins;
        for (double hz = 400.0; hz < 3000.0; hz += static_cast<double>(kRate) / kWindow) bins.push_back(powerAt(part, hz));
        std::sort(bins.begin(), bins.end());
        heights.push_back(10.0 * std::log10(bins[bins.size() * 98 / 100] / bins[bins.size() / 2]));
    }
    std::sort(heights.begin(), heights.end());
    return heights[heights.size() / 2];
}

double bandEnergy(const Samples& x, double fromHz, double toHz) {
    double energy = 0.0;
    constexpr std::size_t kWindow = 4096;
    for (std::size_t at = 0; at + kWindow <= x.size(); at += kWindow) {
        const Samples part = slice(x, at, at + kWindow);
        for (double hz = fromHz; hz < toHz; hz += static_cast<double>(kRate) / kWindow) energy += powerAt(part, hz);
    }
    return energy;
}

// « Un vaisseau extraterrestre » : des raies trop pures. « Un ballon en latex » : rien que du frottement,
// sans raie. Un vrai démarreur est entre les deux : de 10 à 16 dB.
TEST_CASE("le démarreur a des raies, ni trop pures ni noyées") {
    for (const char* id : {"city_14", "four_cyl_sport", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const double lines = shortWindowLinesDb(slice(starting(id), kRate / 10, kRate * 65 / 100));
        MESSAGE(id << " : raies du démarreur " << lines << " dB");
        CHECK(lines > 9.5);
        CHECK(lines < 18.0);
    }
}

// Mesuré : -5 à -30 dB sous 250 Hz, -6 à -17 dB au-dessus de 4 kHz, par rapport à 500-2000 Hz.
TEST_CASE("un démarreur chante dans les médiums, sans graves") {
    for (const char* id : {"four_cyl_sport", "v8_cross_plane"}) {
        INFO(id);
        const Samples cranking = slice(starting(id), kRate / 10, kRate * 65 / 100);
        const double mid = bandEnergy(cranking, 500.0, 2000.0) / 1500.0;
        const double low = 10.0 * std::log10(bandEnergy(cranking, 40.0, 250.0) / 210.0 / mid);
        const double top = 10.0 * std::log10(bandEnergy(cranking, 4000.0, 9000.0) / 5000.0 / mid);
        MESSAGE(id << " : graves " << low << " dB, aigus " << top << " dB (par hertz)");
        CHECK(low < -8.0);
        CHECK(top < 0.0);
    }
}

// C'est ce qui le fait « s'exprimer » : la hauteur des raies balance à chaque compression.
TEST_CASE("le lancement balance à chaque compression") {
    DrivingModel model(findPreset("four_cyl_sport")->powertrain());
    model.halt();
    model.crank();
    double low = 1e9, high = 0.0;
    for (double t = 0.0; model.cranking(); t += 0.001) {
        model.stepNeutral(0.001, 0.0);
        if (t > 0.2) {
            low = std::min(low, model.rpm());
            high = std::max(high, model.rpm());
        }
    }
    MESSAGE("régime de " << low << " à " << high << " tr/min");
    CHECK((high - low) / high > 0.25);
}

// Le défaut « un DJ qui fait drifter un CD » : un volume constant et une seule note, forte, qui glisse.
// Dans de vrais démarreurs, le volume bat de 6 à 13 dB à chaque compression, et l'énergie est dans
// les harmoniques vers 1 à 1,5 kHz, plus que dans la raie fondamentale.
TEST_CASE("le démarreur bat en volume, sur des harmoniques") {
    for (const char* id : {"city_14", "four_cyl_sport", "v8_cross_plane", "diesel_four"}) {
        INFO(id);
        const Samples cranking = slice(starting(id), kRate / 10, kRate * 65 / 100);
        const std::vector<double> levels = envelope(cranking);
        const double contrast = 20.0 * std::log10(percentile(levels, 0.9) / percentile(levels, 0.1));
        const double fundamental = bandEnergy(cranking, 400.0, 800.0) / 400.0;
        const double harmonics = bandEnergy(cranking, 900.0, 1600.0) / 700.0;
        MESSAGE(id << " : contraste " << contrast << " dB, fondamentale/harmoniques " << 10.0 * std::log10(fundamental / harmonics) << " dB");
        CHECK(contrast > 7.0);
        CHECK(contrast < 24.0);
        CHECK(fundamental < harmonics * 1.6);
    }
}
