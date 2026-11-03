#include "supercharger_sound.hpp"

#include "kotlin_math.hpp"

namespace revforge {

namespace {

constexpr float kTwoPi = 2.0f * static_cast<float>(km::kPi);
constexpr float kAirDensity = 1.2f;
// Le Roots : la pulsation interne est énorme (de l'ordre de 125 dB à 1 m si elle sortait à l'air libre). Le pas des rotors
// en hélice, le carter et le conduit en retiennent presque tout : 36 dB. Supposé, sur l'ordre de grandeur de ce qui s'entend
// d'un Roots à pleine charge, au niveau du bloc. L'écoute tranche.
constexpr float kRootsTransmission = 0.016f;
// Une vis n'a pas le déphasage des trois lobes d'un Roots moderne en hélice, et réputée plus sifflante : 5 dB de plus (supposé).
constexpr float kScrewTransmission = 0.028f;
// Même une vis bien adaptée garde une pulsation : la poche ne s'ouvre pas d'un coup, et ses fuites varient. Supposé.
constexpr float kScrewResidualShare = 0.2f;
// La roue d'un centrifuge, à sa vitesse nominale et à plein débit (Pa à 1 m, supposé) : de l'ordre de 85 dB.
constexpr float kWheelPa = 0.35f;
constexpr float kShaftShare = 0.6f;
constexpr float kBladeShare = 0.25f;
constexpr float kBladeFromHz = 4000.0f;
constexpr float kBladeToHz = 12000.0f;
// Le souffle à l'entrée, au débit nominal : il suit le carré du débit. Il ne monte pas haut, comme tout souffle (`kNoiseTopHz`).
constexpr float kRushPa = 0.5f;
constexpr float kRushHz = 1400.0f;
constexpr float kRushTopHz = 3000.0f;
// Le carter a ses modes, fixes : ce sont les raies qui défilent devant.
constexpr float kShellHz = 1700.0f;
constexpr float kShellQ = 1.1f;
// Un écart de quelques pour cent de l'arbre d'une poche à l'autre : les raies d'ordre rotor autour de la raie principale.
constexpr float kRotorOrder = 0.12f;
constexpr float kRotorSecondOrder = 0.06f;
// La sortie voit les bouffées d'admission : la pression, donc la pulsation, varie à chaque explosion.
constexpr float kFiringRipple = 0.18f;
// La raie n'est pas pure : le jeu entre rotors et l'air qui bouge déplacent chaque poche d'un peu (en part de poche).
constexpr float kPhaseJitter = 0.04f;
constexpr float kJitterHz = 250.0f;
constexpr float kLevelTauS = 0.006f;
constexpr float kFadeFromShare = 0.32f;
constexpr float kFadeToShare = 0.45f;

}  // namespace

SuperchargerSound::SuperchargerSound(float sampleRate) : sr_(sampleRate) {
    jitterCoef_ = 1.0f - km::exp(-kTwoPi * kJitterHz / sr_);
    levelCoef_ = 1.0f - km::exp(-1.0f / (kLevelTauS * sr_));
    rushTopCoef_ = 1.0f - km::exp(-kTwoPi * kRushTopHz / sr_);
    shell_.set(kShellHz, kShellQ, sr_);
    rushBand_.set(kRushHz, 0.6f, sr_);
    setBands(1000.0f);
}

void SuperchargerSound::setup(const EngineProfile& profile) {
    kind_ = profile.superchargerKind;
    pocketM3_ = profile.superchargerPocketM3;
    innerPressure_ = profile.superchargerInnerPressure;
    switch (kind_) {
        case SuperchargerKind::Roots:
            pulses_ = 6;
            weights_ = {1.0f, 0.45f, 0.2f, 0.1f};
            noiseShare_ = 0.3f;
            break;
        case SuperchargerKind::TwinScrew:
            pulses_ = 3;
            weights_ = {1.0f, 0.6f, 0.35f, 0.2f};
            noiseShare_ = 0.2f;
            break;
        case SuperchargerKind::Centrifugal:
            // Six aubes : voir `sample`.
            pulses_ = 6;
            noiseShare_ = 1.5f;
            break;
    }
}

float SuperchargerSound::random() {
    std::uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
}

void SuperchargerSound::setBands(float hz) {
    const float top = 0.45f * sr_;
    const float at = km::coerceIn(hz, 40.0f, top);
    band_.set(at, 12.0f, sr_);
    bandLow_.set(km::coerceIn(at / static_cast<float>(pulses_) * 2.0f, 40.0f, top), 12.0f, sr_);
    lastHz_ = hz;
}

void SuperchargerSound::control(const SuperchargerDrive& drive) {
    ratio_ = drive.ratio;
    const float speed = km::max(drive.speedShare, 0.0f);
    const float flow = km::coerceIn(drive.flowShare, 0.0f, 1.5f);
    rushGoal_ = kRushPa * flow * flow;
    if (kind_ == SuperchargerKind::Centrifugal) {
        // Un bruit aérodynamique monte vite avec la vitesse de bout de pale ; il tient au débit qui passe sur les aubes.
        levelGoal_ = kWheelPa * speed * speed * speed * (0.25f + 0.75f * km::min(flow, 1.0f));
        return;
    }
    const float inletPa = km::max(drive.inletPa, 3e4f);
    const float outletPa = inletPa + km::max(drive.liftPa, 0.0f);
    float share;
    float transmission;
    if (kind_ == SuperchargerKind::TwinScrew) {
        const float inner = inletPa * innerPressure_;
        // Ouverte sur moins que sa pression, la poche se vide vers la sortie ; sur plus, elle se remplit : même pulsation.
        share = km::abs(outletPa - inner) / km::max(outletPa, inner) + kScrewResidualShare * (outletPa - inletPa) / outletPa;
        transmission = kScrewTransmission;
    } else {
        share = (outletPa - inletPa) / outletPa;
        transmission = kRootsTransmission;
    }
    // Le volume rendu par poche, Q = V·part·f, en monopôle à 1 m : ρ/(4π)·2πf·Q. Le carré de la fréquence se multiplie à chaque échantillon.
    levelGoal_ = km::coerceIn(share, 0.0f, 1.0f) * pocketM3_ * kAirDensity * 0.5f * transmission;
}

void SuperchargerSound::sample(float cycleIncrement, float firing, float& casing, float& inlet) {
    const float rotorIncrement = 2.0f * ratio_ * cycleIncrement;
    rotorPhase_ += rotorIncrement;
    if (rotorPhase_ >= 1.0f) rotorPhase_ -= km::floor(rotorPhase_);
    const float toneHz = rotorIncrement * sr_ * static_cast<float>(pulses_);
    // Les filtres suivent la raie, mais sans se recalculer à chaque échantillon : au moindre écart sensible.
    if (km::abs(toneHz - lastHz_) > 0.02f * km::max(lastHz_, 40.0f)) setBands(toneHz);

    const bool wheel = kind_ == SuperchargerKind::Centrifugal;
    const float pressure = wheel ? levelGoal_ : levelGoal_ * toneHz * toneHz;
    level_ += (pressure - level_) * levelCoef_;
    rush_ += (rushGoal_ - rush_) * levelCoef_;
    casing = 0.0f;
    inlet = 0.0f;

    const float noise = random();
    const float rushDraw = random();
    rushBand_.process(rushDraw);
    rushTop_ += (rushBand_.bp - rushTop_) * rushTopCoef_;
    inlet = rushTop_ * rush_;
    if (level_ < 1e-5f) {
        // Rien à entendre, mais les filtres restent vivants.
        band_.process(noise);
        bandLow_.process(noise);
        shell_.process(0.0f);
        return;
    }

    jitter_ += (random() * kPhaseJitter - jitter_) * jitterCoef_;
    if (wheel) {
        // La roue rayonne vers son entrée, et sort par le conduit d'admission, comme un turbo : l'ordre 2 de l'arbre, du balourd et des
        // jeux, dans son souffle étroit, et le passage des aubes, que le conduit et l'air éteignent peu à peu vers les aigus.
        band_.process(noise);
        bandLow_.process(noise);
        const float blade = 1.0f - km::coerceIn((toneHz - kBladeFromHz) / (kBladeToHz - kBladeFromHz), 0.0f, 1.0f);
        const float passing = blade > 0.0f ? kBladeShare * blade * dsp::sinTurns(rotorPhase_ * static_cast<float>(pulses_) + jitter_) : 0.0f;
        inlet += (kShaftShare * dsp::sinTurns(2.0f * rotorPhase_ + jitter_) + noiseShare_ * bandLow_.bp + passing) * level_;
        return;
    }
    const float pocket = rotorPhase_ * static_cast<float>(pulses_) + jitter_;
    float tone = 0.0f;
    const float top = kFadeToShare * sr_;
    const float from = kFadeFromShare * sr_;
    for (int k = 0; k < 4; ++k) {
        const float weight = weights_[static_cast<std::size_t>(k)];
        if (weight <= 0.0f) continue;
        const float order = static_cast<float>(k + 1);
        const float hz = toneHz * order;
        if (hz >= top) break;
        const float fade = hz > from ? (top - hz) / (top - from) : 1.0f;
        tone += weight * fade * dsp::sinTurns(pocket * order);
    }
    band_.process(noise);
    bandLow_.process(noise);
    // D'une poche à l'autre et d'une explosion à l'autre, la pulsation varie : ce sont ces raies voisines qui font un moteur, pas une sirène.
    const float rotorOrder = 1.0f + kRotorOrder * dsp::sinTurns(rotorPhase_) + kRotorSecondOrder * dsp::sinTurns(2.0f * rotorPhase_ + 0.3f);
    const float ripple = 1.0f + kFiringRipple * dsp::sinTurns(firing);
    const float pulse = (tone + noiseShare_ * band_.bp) * rotorOrder * ripple;
    shell_.process(pulse);
    casing = (0.5f * pulse + 0.7f * shell_.bp) * level_;
}

}  // namespace revforge
