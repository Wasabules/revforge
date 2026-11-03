#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le carburateur tel qu'on le monterait sur ce moteur : combien de corps, quelle section, quelle pipe. Tout en découle :
 * la perte de charge, le film sur les parois, le mélange, le bruit. Voir « Le carburateur » dans docs/modele.md.
 */
struct CarburetorSpec {
    FuelSystem kind = FuelSystem::Injection;
    /** Corps, et cylindres servis par chacun d'eux. */
    int throats = 0;
    int sharing = 1;
    /** Diamètre au col du venturi (m). */
    double throatM = 0.0;
    /** Le conduit entre le carburateur et les soupapes : diamètre, longueur, coudes et embranchements (coefficient de perte). */
    double pipeM = 0.0;
    double pipeLengthM = 0.0;
    double pipeBendsK = 0.0;
    /** Volume derrière le papillon, en part de la cylindrée : c'est lui qu'il faut remplir à l'ouverture. */
    double plenumShare = 0.0;
    /** Part du carburant qui se dépose sur les parois, à faible vitesse d'air et à la pression atmosphérique. */
    double wallShare = 0.0;
    /** Écart de mélange d'un cylindre à l'autre : ± cette part. */
    double spread = 0.0;
    /** Richesse visée : λ à charge partielle et à pleine charge, en moyenne sur les cylindres. */
    double partLambda = 1.0;
    double fullLambda = 0.88;
    /** Ce que la pompe de reprise injecte sur une course complète du papillon (g). */
    double pumpG = 0.0;
    double redlineRpm = 6000.0;
};

/** Pas de carburateur sur un diesel : le réglage y est sans effet. */
FuelSystem fuelSystemOf(const EngineConfig& engine);
CarburetorSpec carburetorSpecOf(const EngineConfig& engine);

/** Ce que le débit perd à traverser le venturi et la pipe (Pa), à ce régime et à ce remplissage. */
double carburetorDropPa(const CarburetorSpec& spec, double displacementM3, double rpm, double volumetric);
/**
 * <v³>/<v>³ d'un col que traversent les bouffées de `sharing` cylindres : ce que l'à-coup ajoute à la perte, par rapport au
 * même débit moyen en continu. Environ 10 pour un corps par cylindre, 1,1 pour un col partagé par quatre.
 */
double throatPulsation(int sharing);
/** Richesse (1/λ) moyenne, en régime établi, à cette part de la pleine charge et à ce régime. */
double carburetorRichness(const CarburetorSpec& spec, double share, double rpm);
/** Couple à cette richesse, rapporté au meilleur : plat autour de 1,15, en baisse vers le pauvre, jusqu'aux ratés. */
double torqueOfRichness(double richness);

/** Ce que voit le carburateur à ce pas. */
struct CarburetorInput {
    double rpm = 0.0;
    double airGps = 0.0;    // air qui entre dans les cylindres
    double fill = 0.0;      // remplissage du collecteur, 0 à 1
    double fillRate = 0.0;  // sa dérivée (1/s) : ce qu'on remplit passe aussi par le venturi
    double throttle = 0.0;
    double density = 1.0;   // masse volumique au col, en part de l'atmosphère : plus de 1 sur un montage soufflé
};

/**
 * Le chemin du carburant, à pas fixe : la colonne dans le puits, la pompe de reprise, puis le film sur les parois
 * (x-τ d'Aquino). À l'ouverture, l'air arrive avant le carburant : le mélange s'appauvrit, et le couple avec lui.
 * Pas d'allocation, et pas de bibliothèque mathématique en dehors de `sqrt`.
 */
class Carburetor {
public:
    void configure(const CarburetorSpec& spec, double displacementM3);
    /** Remplit le film à l'équilibre au prochain pas : le moteur est chaud, rien ne simule le démarrage à froid. */
    void prime() { primed_ = false; }
    void step(const CarburetorInput& in, double dt);

    /** Le couple que permet le mélange en ce moment, rapporté à celui du mélange établi. */
    double torqueShare() const { return torqueShare_; }
    /** λ dans les cylindres (0 sans débit), film (g), débit de la pompe (g/s). */
    double lambda() const { return lambda_; }
    double filmG() const { return film_; }
    double pumpGps() const { return pumpGps_; }
    const CarburetorSpec& spec() const { return spec_; }

private:
    CarburetorSpec spec_;
    double throatArea_ = 1e-3;
    double pulsationRoot_ = 1.0;
    double chargeG_ = 0.0;
    bool primed_ = false;
    double metered_ = 0.0;
    double film_ = 0.0;
    double suspended_ = 0.0;
    double pumpLeft_ = 0.0;
    double pumpGps_ = 0.0;
    double pumpRate_ = 0.0;
    double lastThrottle_ = 0.0;
    double torqueShare_ = 1.0;
    double lambda_ = 0.0;
};

}  // namespace revforge
