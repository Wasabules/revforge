#pragma once

#include <cstdint>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Deux bruits de la distribution, à part du moteur : chacun a son propre générateur, et aucun ne tourne sans sa cause.
 *
 * Les poussoirs (culbuteurs seulement) : à chaque ouverture, le corps du poussoir hydraulique, poussé par la came sur sa
 * rampe, rattrape la course de fuite du plongeur et frappe la tige, le culbuteur et la soupape. Un choc dont l'impulsion
 * vient des masses (`ValvetrainProfile::pushrodTick`), qui fait sonner la tige en flexion.
 *
 * Le siège (toute distribution) : au-delà de l'affolement, la soupape a quitté la came et retombe sans sa rampe, plus vite,
 * puis rebondit. Chaque rebond garde `e` de la vitesse, et revient d'autant plus tôt.
 *
 * Voir « Les culbuteurs et l'affolement » dans docs/modele.md.
 */
class ValvetrainSound {
public:
    explicit ValvetrainSound(float sampleRate);

    /** Ne touche pas aux ondes en cours. `seatHz`, `seatHighHz` : les résonances du siège, celles du tic d'origine. */
    void setup(const ValvetrainProfile& valves, float seatHz, float seatHighHz);
    /** Le choc au poussoir, à chaque ouverture (`hit`). Même forme que le culbuteur d'un simple arbre : son niveau est relatif. */
    float pushrodSample(bool hit);
    /** Un cycle de plus au-delà de l'affolement : la charge qu'il garde, autour de 1, à ± `lost`. Le rebond est chaotique. */
    float cycleShare(float lost);
    /**
     * Le siège qui claque. `closing` : une soupape se ferme ; `landing` : sa vitesse d'arrivée sur celle de la rampe (0 sous
     * l'affolement) ; `bounceS` : le temps avant le premier rebond. Même échelle que le tic d'origine du siège.
     */
    float seatSample(bool closing, float landing, float bounceS);

private:
    float pushrodRandom();
    float seatRandom();

    float sr_;
    std::uint32_t pushrodRng_ = 0x6C8E9CF5u;
    std::uint32_t seatRng_ = 0x2F7A3D91u;
    dsp::Svf pushrodBand_;
    float pushrodEnv_ = 0.0f;
    float pushrodRise_ = 1.0f;
    float pushrodDecay_ = 0.0f;
    dsp::Svf seatBand_, seatBandHigh_;
    float seatEnv_ = 0.0f;
    float seatRise_ = 1.0f;
    float seatDecay_ = 0.0f;
    // Les rebonds à venir : force du prochain, et dans combien d'échantillons.
    float bounceAmp_ = 0.0f;
    float bounceFloor_ = 0.0f;
    float bounceLeft_ = 0.0f;
    float bounceGap_ = 0.0f;
};

}  // namespace revforge
