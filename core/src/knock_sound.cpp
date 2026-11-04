#include "knock_sound.hpp"

#include "kotlin_math.hpp"

namespace revforge {

namespace {

// Les modes d'une chambre cylindrique (Draper, 1938) : zéros de la dérivée des fonctions de Bessel, 1,841 pour le premier
// circonférentiel, 3,054 pour le deuxième, 3,832 pour le premier radial.
constexpr std::array<float, 3> kCylinderModes{1.0f, 3.054f / 1.841f, 3.832f / 1.841f};
// Ce que chaque mode prend de l'oscillation : le bout de chambre détone contre la paroi, le premier domine (supposé).
constexpr std::array<float, 3> kModeWeights{0.6f, 0.25f, 0.15f};
// La crête de l'oscillation (MAPO) : 0,3 bar à la limite, multipliée par e tous les 3° de plus, 10 bar au plus (supposé).
constexpr float kMapoAtLimitPa = 0.3e5f;
constexpr float kMapoGrowthDeg = 3.0f;
constexpr float kMapoMaxPa = 10e5f;
// De la chambre à 1 m du bloc : l'atténuation de la structure, de l'ordre de 100 dB vers 6 kHz (courbes de Priede, supposé).
constexpr float kStructure = 1e-5f;
// L'oscillation s'éteint en une milliseconde environ : parois, couches limites, piston qui descend (supposé).
constexpr float kRingS = 0.001f;
// Le bout de chambre détone vers 15° après le point mort haut, quand la flamme approche de la paroi.
constexpr float kOnsetDeg = 15.0f;
constexpr float kOnset = kOnsetDeg / 720.0f;
constexpr float kSilentPa = 1e-6f;

}  // namespace

KnockSound::KnockSound(float sampleRate) : sr_(sampleRate) { decay_ = km::exp(-1.0f / (kRingS * sr_)); }

void KnockSound::setup(const EngineProfile& p) {
    rotary_ = p.rotary;
    modeWeight_ = kModeWeights;
    if (rotary_) {
        // Au point mort haut, la chambre d'un rotatif est une lame : la corde entre deux sommets, sur la largeur du rotor. Ses modes sont
        // ceux d'une boîte plate : en longueur, en largeur, puis le deuxième en longueur.
        const double chamberCc = 0.7854 * static_cast<double>(p.boreM) * static_cast<double>(p.boreM) * static_cast<double>(p.strokeM) * 1e6;
        const RotaryGeometry shape = rotaryGeometry(chamberCc);
        const float length = static_cast<float>(1.7320508 * shape.radiusMm / 1000.0);
        const float width = static_cast<float>(shape.widthMm / 1000.0);
        chamberM_ = length;
        modeRatio_ = {1.0f, length / width, 2.0f};
        cycleShare_ = 2.0f / 3.0f;
    } else {
        boreM_ = p.boreM;
        modeRatio_ = kCylinderModes;
        cycleShare_ = 1.0f;
    }
    clearance_ = 1.0f / km::max(p.starter.compressionRatio - 1.0f, 1.0f);
}

void KnockSound::control(const KnockDrive& drive, float rpm) {
    active_ = drive.active;
    excessDeg_ = drive.excessDeg;
    spreadDeg_ = drive.spreadDeg;
    const float sound = static_cast<float>(burnedSoundMs(static_cast<double>(drive.gasK)));
    baseHz_ = rotary_ ? sound / (2.0f * chamberM_) : 1.841f * sound / (static_cast<float>(km::kPi) * boreM_);
    degPerSample_ = km::max(rpm, 0.0f) * 6.0f / sr_ * cycleShare_;
}

float KnockSound::random() {
    std::uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
}

void KnockSound::watch(int cylinder, float theta) {
    bool& armed = armed_[static_cast<std::size_t>(cylinder)];
    if (theta >= kOnset && theta < kOnset + 0.25f) {
        if (armed) {
            armed = false;
            fire();
        }
    } else if (theta >= 0.5f) {
        armed = true;
    }
}

void KnockSound::fire() {
    // Trois tirages uniformes : une cloche d'écart type 1, bornée à trois. Cette combustion est en avance ou en retard sur le cycle moyen.
    const float draw = random() + random() + random();
    const float excess = excessDeg_ + spreadDeg_ * draw;
    if (!active_ || excess <= 0.0f) return;
    const float mapo = km::min(kMapoAtLimitPa * km::exp(excess / kMapoGrowthDeg), kMapoMaxPa);
    Voice& voice = voices_[static_cast<std::size_t>(nextVoice_)];
    nextVoice_ = 1 - nextVoice_;
    voice.on = true;
    voice.level = mapo * kStructure;
    voice.angleDeg = kOnsetDeg * cycleShare_;
    voice.startVolume = volumeAt(voice.angleDeg);
    voice.phase = {0.0f, 0.0f, 0.0f};
    ++knocks_;
}

float KnockSound::volumeAt(float angleDeg) const {
    // La loi en cosinus suffit ici : seul le rapport de deux volumes proches compte.
    return clearance_ + 0.5f * (1.0f - dsp::sinTurns(angleDeg / 360.0f + 0.25f));
}

float KnockSound::sample() {
    float out = 0.0f;
    for (Voice& voice : voices_) {
        if (!voice.on) continue;
        // Le gaz se détend pendant que la chambre sonne : c baisse comme T^½, soit V^-(γ−1)/2 = V^-1/8.
        const float glide = km::sqrt(km::sqrt(km::sqrt(voice.startVolume / volumeAt(voice.angleDeg))));
        for (std::size_t m = 0; m < voice.phase.size(); ++m) {
            const float hz = baseHz_ * modeRatio_[m] * glide;
            if (hz >= 0.45f * sr_) continue;
            out += modeWeight_[m] * voice.level * dsp::sinTurns(voice.phase[m]);
            voice.phase[m] += hz / sr_;
            if (voice.phase[m] >= 1.0f) voice.phase[m] -= 1.0f;
        }
        voice.level *= decay_;
        voice.angleDeg += degPerSample_;
        if (voice.level < kSilentPa) voice.on = false;
    }
    return out;
}

}  // namespace revforge
