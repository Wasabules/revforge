#pragma once

#include <vector>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * La ligne d'échappement vue comme un filtre : calculée depuis sa géométrie, par matrices de transfert
 * acoustiques (tubes, catalyseur, résonateur, silencieux, sortie à l'air libre). Tout ce qui sort du
 * collecteur la traverse, raies comme souffle : c'est elle qui donne ses résonances et ses creux au son.
 * Le calcul est long, mais n'alloue rien : à faire pendant un silence.
 */
class ExhaustLine {
public:
    static constexpr int kImpulseLength = 4096;

    explicit ExhaustLine(float sampleRate);

    /**
     * Calcule la réponse de cette ligne, pour cette cylindrée (m³) et cette longueur de voiture : de ce qui entre
     * (un débit en m³/s) à la dérivée du débit qui sort (m³/s²). La ligne garde ainsi son vrai niveau : une ligne
     * directe laisse tout passer, un silencieux retient 30 dB. La pression à 1 m en découle (rayonnement d'un monopôle).
     */
    const std::vector<float>& compute(Exhaust exhaust, float scale, double displacementM3, int cylinders = 4);

    /** Énergie de la dernière réponse (racine de la somme des carrés) : sert à ce qui doit suivre son niveau. */
    float gain() const { return gain_; }
    /** Atténuation (dB) par rapport au tube seul, moyennée dans cette bande : ce que la ligne retire. */
    double lossDb(double fromHz, double toHz) const;

    /**
     * Perte par transmission (dB) d'une chambre d'expansion seule, sans pertes : le contrôle du calcul
     * face à la formule TL = 10·log10(1 + ¼(m - 1/m)²·sin²(kL)).
     */
    static double chamberLossDb(double hz, double length, double areaRatio);
    /** La bouche du tube : module de la réflexion et correction de bout (l/a), pour ce ka. */
    static double outletReflectionMagnitude(double ka);
    static double outletEndCorrection(double ka);

private:
    static constexpr int kFftSize = kImpulseLength * 2;
    float sr_;
    dsp::Fft fft_;
    std::vector<float> re_, im_, impulse_;
    std::vector<float> outlet_;  // |débit de sortie / débit d'entrée| de la dernière ligne, par case de FFT
    float gain_ = 1.0f;
};

}  // namespace revforge
