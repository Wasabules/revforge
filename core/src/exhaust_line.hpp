#pragma once

#include <vector>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/** Un tronçon de la ligne, pour la réponse : voir exhaust_line.cpp. */
struct ExhaustSection;

/** Un tronçon de ligne vu par une onde qui la parcourt dans le temps : longueur (m), section (m²), pertes (néper/m, vers 300 Hz). */
struct LineStretch {
    double lengthM = 0.0;
    double areaM2 = 0.0;
    double lossPerM = 0.0;
};

/**
 * La même ligne que la réponse de `ExhaustLine::compute`, tronçon par tronçon, de l'entrée à la bouche : pour le cycle du deux-temps,
 * qui suit l'onde dans le temps. Les résonateurs greffés sur le côté n'y sont pas. `chambers` chambres de détente en parallèle.
 * Renvoie le nombre de tronçons écrits.
 */
int lineStretches(Exhaust exhaust, float scale, double displacementM3, const ExpansionChamber& chamber, int chambers, LineStretch* out, int capacity);

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
    /**
     * `filterResistance` : un filtre à particules après le catalyseur, de cette résistance rapportée à ρc/S du tube (voir
     * `filterLineResistance`). 0 : pas de filtre, et rien ne change.
     */
    const std::vector<float>& compute(Exhaust exhaust, float scale, double displacementM3, int cylinders = 4, double filterResistance = 0.0);
    /**
     * La chambre de détente : diffuseur, ventre et cône de retour par marches, puis la pointe, un petit silencieux garni et la sortie.
     * `chambers` chambres en parallèle ne font qu'un tube de `chambers` fois leur section : mêmes longueurs, donc même accord.
     */
    const std::vector<float>& computeExpansion(const ExpansionChamber& chamber, int chambers, float scale, double displacementM3);

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
    /** Des tronçons à la réponse : commun à toutes les lignes. */
    /** `filterResistance` et `filterAfter` : le filtre à particules après ce tronçon (0 : pas de filtre, voir `compute`). */
    const std::vector<float>& respond(const ExhaustSection* sections, std::size_t count, float scale, double baseArea, double branchOmega,
                                      double filterResistance, std::size_t filterAfter);
    static constexpr int kFftSize = kImpulseLength * 2;
    float sr_;
    dsp::Fft fft_;
    std::vector<float> re_, im_, impulse_;
    std::vector<float> outlet_;  // |débit de sortie / débit d'entrée| de la dernière ligne, par case de FFT
    float gain_ = 1.0f;
};

}  // namespace revforge
