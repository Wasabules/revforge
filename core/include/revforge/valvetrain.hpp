#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Les soupapes d'un cylindre, déduites de ce qui tient dans l'alésage. Le modèle d'origine du simulateur est un double
 * arbre : `Camshaft::Dohc` n'y ajoute rien, et ces fonctions ne servent qu'à dire ce qu'un simple arbre a de moins.
 * Voir « Le simple arbre » dans docs/modele.md, et tools/valve_filling.py pour les constantes.
 */
struct ValveTrainGeometry {
    int perCylinder = 4;
    /** Diamètre de tête de chaque soupape (mm). */
    double intakeMm = 0.0;
    double exhaustMm = 0.0;
    /** Section efficace moyenne pendant l'ouverture, toutes soupapes du cylindre (mm²) : Cd × section. */
    double intakeAreaMm2 = 0.0;
    double exhaustAreaMm2 = 0.0;
};

ValveTrainGeometry valveTrainOf(Camshaft camshaft, double boreMm);

/** L'indice de Mach de Taylor : vitesse du gaz à travers la section efficace, rapportée à celle du son. */
double machIndex(double areaMm2, double boreMm, double strokeMm, double rpm, double soundMs);

/** La part de la cylindrée qu'une section laisse entrer à cet indice : 1 à bas régime, 0,76 / Z une fois étranglée. */
double valveFilling(double z);

/** Le travail perdu à vider le cylindre à cet indice (bar de pression moyenne), à comparer d'un indice à l'autre. */
double exhaustLossBar(double z);

/** Ce que les deux soupapes d'un simple arbre laissent entrer, par rapport aux quatre d'un double arbre : au plus 1. */
double sohcFillingRatio(double boreMm, double strokeMm, double rpm);

/** Le couple au frein qui en reste, le remplissage et le travail perdu à l'échappement compris. */
double sohcBreathingRatio(double boreMm, double strokeMm, double rpm);

/** Moins de section, le gaz va plus vite : l'accord de l'admission descend comme la racine de la section (Engelman). */
double sohcTuningRatio(double boreMm);

/** La bouffée d'échappement dure d'autant plus que la section est petite : τ = V / (Cd·A·a). */
double sohcBlowdownStretch(double boreMm);

/** Ce que change le frottement de distribution (bar de pression moyenne), selon la course et le régime. */
double sohcFrictionDeltaBar(double strokeMm, double rpm);

/** Un arbre à cames de moins par rangée, ramené au vilebrequin (kg·m²) : il tourne à demi-vitesse. */
double sohcInertiaDelta(const EngineConfig& e);

}  // namespace revforge
