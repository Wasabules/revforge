#pragma once

#include <array>
#include <cstdint>

#include "dsp.hpp"
#include "revforge/combustion.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le cliquetis, tel que l'oreille le reçoit du bloc. Quand les gaz frais en bout de chambre s'enflamment d'eux-mêmes, la pression
 * saute d'un coup et la chambre résonne sur ses modes acoustiques : le premier mode circonférentiel (Draper, 1938) vers 6 kHz, puis
 * le deuxième et le premier radial. La fréquence suit la vitesse du son du gaz brûlé, qui baisse à mesure qu'il se détend.
 *
 * Chaque combustion tire, de son propre générateur, sa phase dans la dispersion d'un cycle à l'autre : elle cliquette si elle passe
 * la limite, d'autant plus fort qu'elle la dépasse. Rien ne tourne tant que le modèle de conduite ne dit pas que le moteur peut cliqueter.
 */
class KnockSound {
public:
    explicit KnockSound(float sampleRate);

    /** La chambre : alésage et taux de compression, ou la lame d'un rotatif. Ne coupe aucune résonance en cours. */
    void setup(const EngineProfile& profile);
    /** Tous les quelques échantillons : où en est le cycle moyen de sa limite, et le régime. */
    void control(const KnockDrive& drive, float rpm);
    /** Chaque cylindre, à chaque échantillon : `theta`, sa phase dans le cycle de 720°, 0 à l'explosion. */
    void watch(int cylinder, float theta);
    bool ringing() const { return voices_[0].on || voices_[1].on; }
    /** Ce que le bloc rayonne, en pascals à 1 m. */
    float sample();
    /** Nombre de combustions qui ont cliqueté depuis le début, pour les tests et l'affichage. */
    std::uint32_t knocks() const { return knocks_; }

private:
    struct Voice {
        bool on = false;
        float level = 0.0f;
        float angleDeg = 0.0f;
        float startVolume = 1.0f;
        std::array<float, 3> phase{};
    };

    float random();
    void fire();
    float volumeAt(float angleDeg) const;

    float sr_;
    std::uint32_t rng_ = 0x6C8E9CF5u;
    std::uint32_t knocks_ = 0;
    std::array<float, 3> modeRatio_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> modeWeight_{0.0f, 0.0f, 0.0f};
    float boreM_ = 0.086f;
    float chamberM_ = 0.0f;
    bool rotary_ = false;
    float clearance_ = 0.1f;
    float cycleShare_ = 1.0f;
    float decay_ = 0.0f;
    float baseHz_ = 6000.0f;
    float degPerSample_ = 0.0f;
    float excessDeg_ = -100.0f;
    float spreadDeg_ = 1.5f;
    bool active_ = false;
    int nextVoice_ = 0;
    std::array<bool, kMaxCylinders> armed_{};
    std::array<Voice, 2> voices_{};
};

}  // namespace revforge
