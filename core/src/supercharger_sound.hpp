#pragma once

#include <array>
#include <cstdint>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"
#include "revforge/supercharger.hpp"

namespace revforge {

/**
 * Le son du compresseur mécanique, à part du moteur : il reçoit l'état de l'air et la vitesse de l'arbre.
 *
 * Un volumétrique vide ses poches une à une. Quand une poche s'ouvre sur la sortie, l'air comprimé y rentre d'un coup : c'est
 * une pulsation de débit, à (poches par tour) × (tours par seconde), d'autant plus forte que l'écart de pression est grand.
 * Une vis comprime déjà en elle : seul l'écart entre sa pression interne et la sortie reste. Un centrifuge n'a pas de
 * poches : la raie vient du passage des aubes, et son niveau de la vitesse de bout de pale.
 *
 * Tout tourne avec l'arbre, donc avec le vilebrequin, sans lissage : l'acyclisme s'y retrouve. Son propre générateur : il
 * ne décale aucun autre tirage.
 */
class SuperchargerSound {
public:
    explicit SuperchargerSound(float sampleRate);

    /** Ne touche pas aux ondes en cours : changer de pression en marche ne coupe rien. */
    void setup(const EngineProfile& profile);
    /** Tous les quelques échantillons : l'état du compresseur. */
    void control(const SuperchargerDrive& drive);
    /** `cycleIncrement` : tours du cycle 4 temps par échantillon. `firing` : phase des explosions, en tours. En pascals à 1 m. */
    void sample(float cycleIncrement, float firing, float& casing, float& inlet);

private:
    float random();
    void setBands(float hz);

    float sr_;
    std::uint32_t rng_ = 0x2545F491;
    SuperchargerKind kind_ = SuperchargerKind::Roots;
    int pulses_ = 6;
    float pocketM3_ = 0.0f;
    float innerPressure_ = 1.0f;
    float lastHz_ = 0.0f;

    float ratio_ = 0.0f;
    float rotorPhase_ = 0.0f;
    float jitter_ = 0.0f;
    float jitterCoef_ = 0.0f;
    float level_ = 0.0f;
    float levelGoal_ = 0.0f;
    float levelCoef_ = 0.0f;
    float rush_ = 0.0f;
    float rushGoal_ = 0.0f;
    std::array<float, 4> weights_{};
    float noiseShare_ = 0.3f;
    float rushTop_ = 0.0f;
    float rushTopCoef_ = 0.0f;
    dsp::Svf band_, bandLow_, shell_, rushBand_;
};

}  // namespace revforge
