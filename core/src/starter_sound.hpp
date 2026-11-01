#pragma once

#include <array>
#include <cstdint>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le son du démarreur, à part du moteur : il reçoit l'état mécanique, et rend un échantillon.
 *
 * Mesuré sur de vrais démarrages : un bruit localement gaussien, sans raie au-dessus de 800 Hz, dont
 * le niveau suit lentement le couple, passé par une structure dense et fixe. Le son vient donc de
 * forces (contact des dents, balais, chocs) qui excitent une seule réponse : celle du bloc et du carter.
 */
class StarterSound {
public:
    explicit StarterSound(float sampleRate);

    void setup(const StarterSpec& spec, float sizeScale);
    /** Alimenté ; pignon engagé dans la couronne ; couple transmis par les dents, rapporté au calage. */
    void setState(bool powered, bool meshed, float load);
    /** `cranking` : le moteur ne brûle encore rien. `rpm` : régime du vilebrequin. */
    float sample(float rpm, bool cranking, float level);

private:
    static constexpr int kBodyPartition = 128;
    static constexpr int kBodyPartitions = 32;
    static constexpr int kBodyCapacity = kBodyPartition * kBodyPartitions;

    void buildBody(float scale);
    void strikeWith(float force);

    float sr_;
    dsp::Noise noise_;
    StarterSpec spec_;
    bool powered_ = false;
    bool meshed_ = false;
    bool wasPowered_ = false;
    bool wasMeshed_ = false;
    double pinionOmega_ = 0.0;
    double ringTurns_ = 0.0;
    double pinionTurns_ = 0.0;
    double motorTurns_ = 0.0;
    float load_ = 0.0f;
    float force_ = 0.0f;
    float forceCoef_ = 0.0f;
    float forceMean_ = 0.2f;
    float forceMeanCoef_ = 0.0f;
    bool wasRising_ = false;
    float burst_ = 1.0f;
    float burstTarget_ = 1.0f;
    float burstCoef_ = 0.0f;
    float pitchFast_ = 0.0f;
    float pitchSlow_ = 0.0f;
    float pitchFastCoef_ = 0.0f;
    float pitchSlowCoef_ = 0.0f;
    float currentHeld_ = 0.0f;
    float currentCoef_ = 0.0f;
    std::array<float, 3> wander_{};
    std::array<float, 3> wanderSlow_{};
    float wanderCoef_ = 0.0f;
    float wanderSlowCoef_ = 0.0f;
    double overrun_ = 0.0;
    bool inContact_ = false;
    double pinionFreeOmega_ = 400.0;
    double pinionStall_ = 10.0;
    double pinionInertia_ = 0.003;
    float toothRough_ = 0.5f;
    float meshLevel_ = 0.0f;
    float torqueScale_ = 1.0f;
    float brushWeight_ = 0.5f;
    float contactFloor_ = 0.1f;
    std::array<float, 180> ringErrors_{};
    std::array<float, 16> pinionErrors_{};
    std::array<float, 40> barErrors_{};
    std::array<float, 16> sunErrors_{};
    dsp::Svf contactTone_, contactToneSteep_, contactSplit_, brushBand_, hitBand_;
    // La séquence d'engagement, en échantillons depuis le contact.
    int sinceContact_ = -1;
    int plungerTravel_ = 1;
    std::array<int, 4> plungerStops_{};
    std::array<float, 4> plungerForces_{};
    float impact_ = 0.0f;
    float impactDecay_ = 0.0f;
    float click_ = 0.0f;
    float clickDecay_ = 0.0f;
    float impactHeld_ = 0.0f;
    float impactRiseCoef_ = 0.0f;
    float brushRolloff_ = 0.0f;
    float brushRolloffCoef_ = 0.0f;
    float shock_ = 0.0f;
    float shockDecay_ = 0.0f;
    // La réponse du bloc et du carter : calculée à part, puis figée.
    dsp::Convolver body_;
    std::array<float, kBodyCapacity> bodyImpulse_{};
    std::array<float, kBodyCapacity> bodyScratch_{};
    int bodyLength_ = 0;
    int silentFor_ = 0;
};

}  // namespace revforge
