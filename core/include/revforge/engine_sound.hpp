#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Simulation acoustique d'un moteur.
 *
 * Un vrai moteur n'a rien de périodique : chaque explosion diffère de la précédente
 * et l'échappement est turbulent. On suit donc le trajet du gaz : chaque cylindre
 * garde sa propre pression, qui se vide à l'ouverture de la soupape (durée fixée
 * par l'alésage et la course), remonte son collecteur, rejoint le tube puis le
 * silencieux. S'y ajoutent l'admission et les bruits mécaniques.
 *
 * Tout évolue échantillon par échantillon : le résultat ne dépend pas de la taille
 * des blocs. `render` n'alloue jamais de mémoire et ne prend aucun verrou.
 *
 * `setProfile` et `render` s'appellent depuis le même thread ; régime et volume
 * peuvent être changés de n'importe où.
 */
class EngineSound {
public:
    explicit EngineSound(int sampleRate, std::int32_t seed = 0x2F6E2B1);
    ~EngineSound();
    EngineSound(const EngineSound&) = delete;
    EngineSound& operator=(const EngineSound&) = delete;

    /** Le passage d'un moteur à l'autre se fait après un bref fondu au silence. */
    void setProfile(const EngineProfile& profile);
    void setTargetRpm(float rpm) { targetRpm_.store(rpm, std::memory_order_relaxed); }
    void setVolume(float volume) { volume_.store(volume, std::memory_order_relaxed); }

    float targetRpm() const { return targetRpm_.load(std::memory_order_relaxed); }

    /** Vrai quand la sortie est muette et le restera : la piste peut être mise en pause. */
    bool isSilent() const;

    void render(float* out, int count);

    /** Pour l'affichage : charge estimée (0-1), pression du turbo (0-1). */
    float load() const;
    float boost() const;

private:
    struct State;
    std::unique_ptr<State> s_;
    std::atomic<float> targetRpm_{0.0f};
    std::atomic<float> volume_{0.7f};
};

}  // namespace revforge
