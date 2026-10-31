#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

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

TEST_CASE("les moteurs jouent à des niveaux comparables") {
    for (const Preset* preset : allPresets()) {
        INFO(preset->id);
        const double level = rms(steady(preset->profile(), 3000.0f));
        CHECK(level >= 0.06);
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
        double harmonics = 0.0;
        for (int k = 3; k <= 6; ++k) harmonics += powerAt(last, firingHz * k);
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
    CHECK(rms(difference) > rms(a) * 0.2);
}

// Le défaut « étouffé » : il faut de l'énergie jusque dans les aigus, comme en réel.
TEST_CASE("garde son grain dans les aigus") {
    for (const Preset* preset : allPresets()) {
        if (preset->electric) continue;
        INFO(preset->id);
        const EngineProfile p = preset->profile();
        const Samples x = steady(p, p.idleRpm + (p.redlineRpm - p.idleRpm) * 0.6f);
        Samples d(x.size(), 0.0f);
        for (std::size_t i = 3; i < x.size(); ++i) d[i] = x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3];
        CHECK(20.0 * std::log10(rms(d) / rms(x)) > -28.0);
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
            CHECK(rms(played) > 0.05);
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
