#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le filtre à air, du point de vue de l'air qui entre : ce qu'il coûte en pression, et où il va chercher son air.
 * Tout est écrit par rapport au papier d'origine, sur lequel les couples de référence sont calibrés : avec
 * `AirFilter::Stock`, aucun de ces calculs ne s'exécute. Voir « Le filtre à air » dans docs/modele.md.
 */
struct AirFilterSpec {
    /** Perte de tout le trajet jusqu'au conduit, au débit où le papier d'origine perd 3000 Pa. */
    double designDropPa;
    /** Ce qui, dans cette perte, se fait dans l'élément lui-même ; le reste est dans la boîte et sa prise d'air. */
    double elementDropPa;
    /** Part de l'échauffement du compartiment moteur que voit la prise d'air : 0, elle va chercher l'air dehors. */
    double bayAirShare;
    /** Hors de toute boîte : il respire et rayonne dans le compartiment moteur. */
    bool underBonnet;
};

const AirFilterSpec& airFilterSpec(AirFilter filter);

/** La perte à débit égal, rapportée au papier d'origine. */
double airFilterDropShare(AirFilter filter);

/** Ce qu'il y a derrière le filtre : à l'entrée d'un moteur ou d'un compresseur. */
struct AirInlet {
    double pressurePa = 101325.0;
    double temperatureK = 293.0;
};

/**
 * L'air derrière ce filtre, pour ce débit : ΔP = k·ṁ², où `k` donne 3 kPa au débit nominal sur le papier d'origine, et
 * la température de `airFilterRiseK`. À appeler pour toute entrée de compresseur : le dimensionnement fixe le débit
 * nominal, et `regime` est le régime rapporté à celui du point nominal. `ambientPa`, `ambientK` : l'air du jour, celui de
 * référence par défaut (atmosphere.hpp).
 */
AirInlet airInletBehind(AirFilter filter, EnginePlacement placement, double flowKgS, double ratedFlowKgS, double regime, double ambientPa = 101325.0,
                        double ambientK = 293.0);

/**
 * Ce que l'air gagne en température avant d'atteindre le filtre, à pleine charge (K). Bilan du compartiment : ce qui y est
 * rejeté suit la puissance, donc le régime ; ce qui le ventile, un ventilateur et la vitesse. `regime` est rapporté au
 * point nominal, et la voiture supposée rouler assez vite pour tenir ce régime sur un rapport intermédiaire.
 */
double airFilterRiseK(AirFilter filter, EnginePlacement placement, double regime);

/** Coefficient de perte du papier d'origine d'un moteur atmosphérique (Pa par (kg/s)²), selon la taille de sa boîte. */
double naturalFilterCoefficient(double displacementM3, double ratedFlowKgS);

/** La longueur des cornets, de la soupape à la bouche (m), telle qu'on la choisit : accordée à 72 % du régime maximal. */
double stackLengthM(const EngineConfig& engine);
/** Le rayon de la bouche d'un cornet (m). */
double stackRadiusM(const EngineConfig& engine);
/**
 * Le quart d'onde de ce tube ouvert à un bout, correction de bout comprise (Hz). Le tube est coupé une fois pour toutes dans
 * l'air de référence ; sa résonance suit la vitesse du son de l'air du jour (`ambientC`).
 */
double stackResonanceHz(const EngineConfig& engine);
/**
 * Gain de remplissage par l'onde qui revient dans le cornet, à ce régime (Heywood, § 7.6). L'aspiration lance une
 * dépression à mi-course, le tube la renvoie, et ce que trouve la soupape à sa fermeture ajoute ou retire de l'air.
 */
double stackRamGain(const EngineConfig& engine, double rpm);

}  // namespace revforge
