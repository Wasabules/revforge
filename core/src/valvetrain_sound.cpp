#include "valvetrain_sound.hpp"

namespace revforge {

namespace {

// La forme du culbuteur d'un simple arbre (`kRockerQ`, `kRockerDecayS` du son) : le niveau des poussoirs se compare au sien.
constexpr float kPushrodQ = 1.2f;
constexpr float kPushrodDecayS = 0.0007f;
// Le siège : la forme du tic d'origine (`valveBand`, `valveBandHigh`, 0,9 ms).
constexpr float kSeatQ = 1.1f;
constexpr float kSeatHighQ = 1.4f;
constexpr float kSeatDecayS = 0.0009f;
constexpr float kSeatHighShare = 0.35f;
// Le rebond garde la moitié de la vitesse (comme `kRestitution`, valve_float.cpp), jusqu'à ce qu'il ne reste presque rien.
constexpr float kRestitution = 0.5f;
constexpr float kLastBounce = 0.05f;
constexpr float kAttackS = 0.0005f;

float draw(std::uint32_t& state) {
    std::uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
}

}  // namespace

ValvetrainSound::ValvetrainSound(float sampleRate) : sr_(sampleRate) {
    pushrodDecay_ = km::exp(-1.0f / (kPushrodDecayS * sr_));
    seatDecay_ = km::exp(-1.0f / (kSeatDecayS * sr_));
    pushrodBand_.set(2000.0f, kPushrodQ, sr_);
    seatBand_.set(3600.0f, kSeatQ, sr_);
    seatBandHigh_.set(7400.0f, kSeatHighQ, sr_);
}

void ValvetrainSound::setup(const ValvetrainProfile& valves, float seatHz, float seatHighHz) {
    const float top = sr_ * 0.45f;
    if (valves.pushrodHz > 0.0) pushrodBand_.set(km::coerceIn(static_cast<float>(valves.pushrodHz), 300.0f, top), kPushrodQ, sr_);
    seatBand_.set(km::min(seatHz, top), kSeatQ, sr_);
    seatBandHigh_.set(km::min(seatHighHz, top), kSeatHighQ, sr_);
}

float ValvetrainSound::pushrodRandom() { return draw(pushrodRng_); }

float ValvetrainSound::seatRandom() { return draw(seatRng_); }

float ValvetrainSound::pushrodSample(bool hit) {
    const float noise = pushrodRandom();
    if (hit) {
        // Jamais deux chocs identiques : le plongeur n'a pas toujours fui de la même course.
        pushrodEnv_ = 0.5f + 0.25f * (noise + 1.0f);
        pushrodRise_ = 0.0f;
    }
    pushrodRise_ = km::min(pushrodRise_ + 1.0f / (kAttackS * sr_), 1.0f);
    pushrodBand_.process(noise * pushrodEnv_ * pushrodRise_);
    pushrodEnv_ *= pushrodDecay_;
    return pushrodBand_.bp;
}

float ValvetrainSound::cycleShare(float lost) { return 1.0f + lost * seatRandom(); }

float ValvetrainSound::seatSample(bool closing, float landing, float bounceS) {
    const float noise = seatRandom();
    if (closing && landing > 0.0f) {
        seatEnv_ = km::max(seatEnv_, landing * (0.5f + 0.25f * (noise + 1.0f)));
        seatRise_ = 0.0f;
        bounceAmp_ = landing * kRestitution;
        bounceFloor_ = landing * kLastBounce;
        bounceGap_ = bounceS * sr_ * kRestitution;
        bounceLeft_ = bounceS * sr_;
    } else if (bounceAmp_ > 0.0f) {
        bounceLeft_ -= 1.0f;
        if (bounceLeft_ <= 0.0f) {
            // Le rebond suivant : plus faible, plus tôt.
            seatEnv_ = km::max(seatEnv_, bounceAmp_ * (0.5f + 0.25f * (noise + 1.0f)));
            seatRise_ = 0.0f;
            bounceLeft_ = bounceGap_;
            bounceGap_ *= kRestitution;
            bounceAmp_ *= kRestitution;
            if (bounceAmp_ < bounceFloor_ || bounceLeft_ < 1.0f) bounceAmp_ = 0.0f;
        }
    }
    seatRise_ = km::min(seatRise_ + 1.0f / (kAttackS * sr_), 1.0f);
    const float tick = noise * seatEnv_ * seatRise_;
    seatBand_.process(tick);
    seatBandHigh_.process(tick);
    seatEnv_ *= seatDecay_;
    return seatBand_.bp + kSeatHighShare * seatBandHigh_.bp;
}

}  // namespace revforge
