#include "injector_sound.hpp"

#include <algorithm>
#include <cmath>

#include "kotlin_math.hpp"
#include "revforge/injection.hpp"

namespace revforge {

namespace {

// Début d'injection : 60° après le point mort haut d'admission, soit 420° après l'explosion, dans le cycle de 720°.
constexpr float kStartShare = 420.0f / 720.0f;
// La came de la pompe finit sa course 30° avant que l'injecteur ne s'ouvre.
constexpr float kPumpLeadShare = 30.0f / 720.0f;
// Une injection ne dure jamais moins que l'ouverture balistique de l'aiguille, ni plus que ce que le cycle laisse.
constexpr float kMinInjectionS = 0.0003f;
constexpr float kMaxSpanShare = 0.35f;
// Le choc : 0,1 ms de contact (une bille d'acier sur son siège), puis les modes sonnent. Au-dessus de 5 kHz pour l'injecteur et sa
// culasse, vers 1,6 kHz pour la pompe et sa rampe : les bandes que des contre-mesures ont fait baisser sur un moteur réel
// (SAE 2011-01-0930 : 3 dB au-dessus de 5 kHz en suspendant les injecteurs, de 5,8 à 7,5 dB(A) vers 1,6 kHz pour la pompe).
constexpr float kContactS = 0.0001f;
// La soupape de la pompe se pose plus mollement, sur un film de carburant : un demi-sinus de 0,3 ms, dont le spectre retombe
// au-delà de 5 kHz. Sans cela, le claquement de la pompe débordait sur la bande des injecteurs.
constexpr float kPumpContactS = 0.0003f;
constexpr float kNeedleHz = 6300.0f;
constexpr float kNeedleQ = 6.0f;
constexpr float kNeedleHighHz = 9800.0f;
constexpr float kNeedleHighQ = 5.0f;
constexpr float kPumpHz = 1600.0f;
constexpr float kPumpQ = 5.0f;
constexpr float kPumpHighHz = 3400.0f;
constexpr float kPumpHighQ = 4.0f;
// L'aiguille qui se ferme frappe plus fort que l'induit qui s'ouvre : le ressort et la pression la poussent.
constexpr float kOpeningShare = 0.6f;
// Niveaux face au plancher du bloc au ralenti : calés pour que, au ralenti et sous le capot, les chocs ajoutent ce que ces
// contre-mesures ont retiré (voir docs/modele.md, « L'injection »).
constexpr float kNeedleShare = 3.6f;
constexpr float kPumpShare = 8.8f;
constexpr float kRailReferenceBar = 200.0f;

}  // namespace

InjectorSound::InjectorSound(float sampleRate) : sr_(sampleRate) {}

void InjectorSound::setup(const EngineProfile& p) {
    cylinderM3_ = 0.7854f * p.boreM * p.boreM * p.strokeM;
    redlineRpm_ = p.redlineRpm;
    nozzleM2_ = static_cast<float>(directNozzleAreaM2(static_cast<double>(cylinderM3_)));
    // Une came à autant de bossages que de cylindres jusqu'à quatre ; trois sur un six cylindres ; quatre au-delà.
    const int firings = std::max(p.firingCount, 1);
    strokes_ = firings <= 4 ? firings : (firings <= 6 ? 3 : 4);
    burst_ = std::max(static_cast<int>(kContactS * sr_ + 0.5f), 1);
    pumpBurst_ = std::max(static_cast<int>(kPumpContactS * sr_ + 0.5f), 2);
    const float top = sr_ * 0.45f;
    needle_.set(km::min(kNeedleHz, top), kNeedleQ, sr_);
    needleHigh_.set(km::min(kNeedleHighHz, top), kNeedleHighQ, sr_);
    pump_.set(kPumpHz, kPumpQ, sr_);
    pumpHigh_.set(kPumpHighHz, kPumpHighQ, sr_);
    soi_ = kStartShare;
}

void InjectorSound::control(float rpm, float load, float floorPa) {
    const float speed = km::max(rpm, 100.0f);
    const float share = km::coerceIn(load, 0.0f, 1.0f);
    railBar_ = static_cast<float>(directRailBar(static_cast<double>(share), static_cast<double>(speed), static_cast<double>(redlineRpm_)));
    // Le carburant d'une injection : l'air du cylindre à ce remplissage, à richesse 1, plus riche à fond.
    const float air = 1.2f * cylinderM3_ * 0.9f * (0.1f + 0.9f * share);
    const float lambda = share > 0.85f ? 0.88f : 1.0f;
    const float fuel = air / (14.7f * lambda);
    injectionS_ = km::max(static_cast<float>(revforge::injectionSeconds(static_cast<double>(fuel), static_cast<double>(railBar_), static_cast<double>(nozzleM2_), 745.0)), kMinInjectionS);
    span_ = km::min(injectionS_ * speed / 120.0f, kMaxSpanShare);
    // L'aiguille se ferme sous la pression : l'énergie du choc la suit, l'amplitude sa racine.
    const float hit = km::sqrt(railBar_ / kRailReferenceBar);
    needleLevel_ = floorPa * kNeedleShare * hit;
    // La pompe : le même claquement de soupape, et une onde d'autant plus forte qu'elle refoule plus.
    pumpLevel_ = floorPa * kPumpShare * hit * (0.5f + 0.5f * share);
}

float InjectorSound::random() {
    std::uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
}

void InjectorSound::strike(float& amplitude, int& left, float level) {
    // Jamais deux chocs identiques : ± 15 %.
    amplitude = level * (1.0f + 0.15f * random());
    left = burst_;
}

float InjectorSound::sample(float phase, int cylinders, const std::array<float, kMaxCylinders>& firing, const std::array<float, kMaxCylinders>& timing, float fuelGate) {
    const bool fuelled = fuelGate > 0.5f;
    for (int c = 0; c < cylinders; ++c) {
        float theta = phase + firing[static_cast<std::size_t>(c)] + timing[static_cast<std::size_t>(c)];
        if (theta >= 1.0f) theta -= 1.0f;
        if (theta < 0.0f) theta += 1.0f;
        const bool now = theta >= soi_ && theta < soi_ + span_;
        bool& was = injecting_[static_cast<std::size_t>(c)];
        if (now != was) {
            // L'ouverture, puis la fermeture : deux chocs par injection, et aucun pendant une coupure.
            if (fuelled) {
                strike(needleAmp_, needleLeft_, now ? kOpeningShare * needleLevel_ : needleLevel_);
                ++needleHits_;
            }
            was = now;
        }
    }
    float pumpAt = phase - soi_ + kPumpLeadShare;
    if (pumpAt < 0.0f) pumpAt += 1.0f;
    const int index = static_cast<int>(pumpAt * static_cast<float>(strokes_)) % strokes_;
    if (index != pumpIndex_) {
        pumpIndex_ = index;
        if (fuelled) {
            strike(pumpAmp_, pumpLeft_, pumpLevel_);
            pumpLeft_ = pumpBurst_;
            ++pumpHits_;
        }
    }
    float needleIn = 0.0f;
    if (needleLeft_ > 0) {
        needleIn = random() * needleAmp_;
        --needleLeft_;
    }
    float pumpIn = 0.0f;
    if (pumpLeft_ > 0) {
        // Un demi-sinus, de la pose au rebond.
        pumpIn = pumpAmp_ * dsp::sinTurns(0.5f * static_cast<float>(pumpBurst_ - pumpLeft_ + 1) / static_cast<float>(pumpBurst_ + 1));
        --pumpLeft_;
    }
    needle_.process(needleIn);
    needleHigh_.process(needleIn);
    pump_.process(pumpIn);
    pumpHigh_.process(pumpIn);
    // Le passe-bande d'état a un gain Q à sa fréquence : on le ramène à 1.
    return needle_.bp / kNeedleQ + 0.6f * needleHigh_.bp / kNeedleHighQ + pump_.bp / kPumpQ + 0.5f * pumpHigh_.bp / kPumpHighQ;
}

}  // namespace revforge
