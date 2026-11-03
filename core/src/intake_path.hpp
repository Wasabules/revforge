#pragma once

#include <vector>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/** Ce que la géométrie de l'admission doit savoir du moteur. */
struct IntakeGeometry {
    double displacementM3 = 0.002;
    double stackM = 0.27;  // longueur d'un cornet, de la soupape à la bouche
    double stackRadiusM = 0.021;
    /** L'air admis : celui du jour, exactement 343 m/s et 1,2 kg/m³ dans l'air de référence (atmosphere.hpp). */
    double soundSpeedMs = 343.0;
    double densityKgM3 = 1.2;
};

/**
 * Ce qui change entre la soupape et l'air libre quand on quitte le papier d'origine, par matrices de transfert, comme
 * `ExhaustLine` : conduit, boîte, élément, prise d'air, bouche. Le niveau du papier d'origine étant calibré, on ne calcule
 * que le rapport des deux trajets. La boîte fuit par ses parois : c'est ce qui borne ce qu'on gagne à la retirer.
 * Le calcul n'alloue rien, mais il est long : à faire pendant un silence.
 */
class IntakePath {
public:
    static constexpr int kLength = 1024;

    explicit IntakePath(float sampleRate);

    /** La réponse, à phase minimale, du rapport entre ce filtre et le papier d'origine. */
    const std::vector<float>& compute(AirFilter filter, const IntakeGeometry& geometry);

    /** Du débit à la soupape au débit rayonné par la bouche (dB). */
    static double pathDb(AirFilter filter, const IntakeGeometry& geometry, double hz);
    /** Ce que ce filtre ajoute au papier d'origine à cette fréquence (dB). */
    static double gainDb(AirFilter filter, const IntakeGeometry& geometry, double hz);

private:
    static constexpr int kFftSize = kLength * 2;
    float sr_;
    dsp::Fft fft_;
    std::vector<float> re_, im_, impulse_;
};

}  // namespace revforge
