#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Les culbuteurs et l'affolement des soupapes : les pièces de chaque distribution, ce qu'elles pèsent ramenées à la
 * soupape, le ressort qui les tient sur la came, et ce qui se passe quand il ne suffit plus. Fonctions pures : la came,
 * le calculateur et le son les lisent. Voir « Les culbuteurs et l'affolement » dans docs/modele.md.
 */

/** Deux soupapes par cylindre : un simple arbre, ou des culbuteurs. Le double arbre, modèle d'origine, en a quatre. */
inline bool twoValves(Camshaft valvetrain) { return valvetrain != Camshaft::Dohc; }

/** Des soupapes, donc un arbre à cames : ni un rotatif, ni un deux-temps. */
bool hasValves(const EngineConfig& e);

/** La came vue de la soupape : levée (mm) et durée d'ouverture (degrés de vilebrequin). */
struct CamLobe {
    double liftMm = 0.0;
    double durationDeg = 0.0;
};

/** La came d'origine : 0,12 fois l'alésage, comme `valvetrain.cpp`, sur 240°, comme le son (`kEvo` à `kEvc`). */
CamLobe stockCamLobe(double boreMm);
/** La came de ce moteur : `camLiftMm` et `camDurationDeg` s'ils sont réglés, celle d'origine sinon (0). */
CamLobe camLobeOf(const EngineConfig& e);

/** Les pièces d'une soupape d'admission et de sa commande, et leur masse ramenée à la soupape. */
struct ValvetrainParts {
    double headMm = 0.0;
    double valveKg = 0.0;
    double retainerKg = 0.0;
    double springKg = 0.0;
    /** Poussoir à godet du double arbre ; 0 ailleurs. */
    double bucketKg = 0.0;
    /** Le culbuteur entier, et ce qu'il pèse vu de la soupape : I / L². */
    double rockerKg = 0.0;
    double rockerAtValveKg = 0.0;
    /** Culbuteurs seulement : poussoir hydraulique et tige, et la tige (longueur, diamètre extérieur, m). */
    double lifterKg = 0.0;
    double pushrodKg = 0.0;
    double pushrodLengthM = 0.0;
    double pushrodOuterM = 0.0;
    /** Rapport du culbuteur : levée à la soupape sur levée à la came. 1 en attaque directe. */
    double ratio = 1.0;
    /** Masse équivalente ramenée à la soupape (kg), raideur de la chaîne vue de la soupape (N/m), et sa fréquence propre (Hz). */
    double effectiveKg = 0.0;
    double stiffnessNPerM = 0.0;
    double naturalHz = 0.0;
};

ValvetrainParts valvetrainParts(Camshaft valvetrain, double boreMm, double strokeMm);

/** Un ressort de soupape : force posée (soupape fermée) et raideur. */
struct ValveSpring {
    double preloadN = 0.0;
    double rateNPerMm = 0.0;
    /** La force à cette levée. */
    double forceN(double liftMm) const { return preloadN + rateNPerMm * liftMm; }
};

/** La décélération de la soupape au nez de came (m/s²), proportionnelle au carré du régime. */
double noseDecelerationMs2(const CamLobe& cam, double rpm);
/** Ce que la souplesse de la chaîne ajoute à cette décélération : (f_d / f_n)², au premier ordre. */
double valvetrainStiffening(const ValvetrainParts& parts, const CamLobe& cam, double rpm);

/** Le ressort d'origine : dimensionné pour la came d'origine à la zone rouge, avec la marge du constructeur. */
ValveSpring stockValveSpring(const ValvetrainParts& parts, const CamLobe& stock, double redlineRpm);
/** Le ressort de ce moteur : d'origine, ou raide (`ValveSprings::Stiff`). */
ValveSpring valveSpringOf(const EngineConfig& e);

/** Le régime où l'inertie, amplifiée par la souplesse de la chaîne, égale le ressort au nez : la soupape quitte la came. */
double valveFloatRpm(const ValvetrainParts& parts, const CamLobe& cam, const ValveSpring& spring);
/** Le régime d'affolement de ce moteur (tr/min) ; 0 sans soupapes. C'est lui que lisent le rupteur et la came. */
double valveFloatRpm(const EngineConfig& e);

/** Tout ce que le son et le couple lisent de la distribution. */
ValvetrainProfile valvetrainProfileOf(const EngineConfig& e);

/** La force que demande la soupape, rapportée à celle du ressort au nez : 1 à l'affolement, 0 sous lui (ou sans soupapes). */
double floatOverload(const ValvetrainProfile& v, double rpm);
/** De combien l'admission se ferme en retard, la soupape ayant quitté la came (degrés de vilebrequin). */
double floatLateDeg(const ValvetrainProfile& v, double rpm);
/** La part de la charge que garde le cylindre : 1 sous l'affolement. */
double floatTrappedShare(const ValvetrainProfile& v, double rpm);
/** La vitesse de la soupape quand elle retombe (m/s), au-delà de celle de la rampe : 0 sous l'affolement. */
double floatLandingMs(const ValvetrainProfile& v, double rpm);
/** La part du couple que laisse l'affolement à ce régime : exactement 1 en dessous. */
double valveFloatShare(const EngineConfig& e, double rpm);

/** Le frottement came-poussoir de cette distribution (bar de pression moyenne) : suit la charge du ressort sur la came. */
double followerFrictionBar(const EngineConfig& e, double rpm);
/** Ce qu'ajoutent les culbuteurs au frottement du double arbre (bar). */
double ohvFrictionDeltaBar(const EngineConfig& e, double rpm);
/** Ce qu'ajoutent des ressorts raides (bar) : la même part de plus sur le frottement de la came. 0 sur des ressorts d'origine. */
double springFrictionDeltaBar(const EngineConfig& e, double rpm);
/**
 * Ce que ces frottements en plus retirent au couple au frein : la courbe à pleine charge est un couple au frein, et
 * `frictionTorqueNm` ne fait qu'y ajouter ce que la combustion doit fournir. Comme `sohcBreathingRatio`, rapporté à 12 bar de
 * pression moyenne indiquée ; celui-ci compte déjà le culbuteur d'un simple arbre. Exactement 1 sans rien de plus.
 */
double valvetrainBrakeShare(const EngineConfig& e, double rpm);
/** Un seul arbre dans le bloc pour tout le moteur, au lieu de deux par rangée (kg·m², ramené au vilebrequin). */
double ohvInertiaDelta(const EngineConfig& e);

}  // namespace revforge
