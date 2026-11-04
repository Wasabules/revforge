#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/** Le taux géométrique d'origine d'un deux-temps (préréglage, Saab 96) : celui que remplace `compressionRatio`. */
inline constexpr double kTwoStrokeGeometricCompression = 7.5;

/**
 * Le deux-temps : des lumières dans la chemise au lieu des soupapes, un carter qui aspire puis refoule le mélange frais dans le
 * cylindre, et une seule explosion par tour. Voir « Le deux-temps » dans docs/modele.md.
 *
 * Tout ce qui suit ne s'exécute que sur `isTwoStroke(config)` : un quatre-temps ne passe par rien d'ici.
 */

/**
 * Les lumières, par leur hauteur. L'échappement et le transfert sont découverts par le haut du piston qui descend ; leur hauteur
 * se compte en part de la course, depuis le haut du piston au point mort bas. L'admission dans le carter est découverte par la
 * jupe qui monte : `inletTravel` est la course restant jusqu'au point mort haut quand elle s'ouvre. Les angles s'en déduisent par
 * la bielle et la manivelle (`pistonTravelShare`), symétriques autour du point mort bas pour les deux premières, du haut pour
 * l'admission.
 */
struct TwoStrokePorts {
    double exhaustHeight = 0.0;
    double transferHeight = 0.0;
    double inletTravel = 0.0;
    /** Longueur de bielle sur course. */
    double rodOverStroke = 2.0;
    /** Largeurs, en part de l'alésage : un pont ou deux entre plusieurs fenêtres, pour retenir le segment. */
    double exhaustWidth = 0.0;
    double transferWidth = 0.0;
    double inletWidth = 0.0;
    /** Angles (° après le point mort haut) : ouverture de l'échappement, du transfert ; ouverture de l'admission avant le point mort haut. */
    double exhaustOpenDeg = 0.0;
    double transferOpenDeg = 0.0;
    double inletOpenBtdcDeg = 0.0;
    /** Taux géométrique, taux réel (compté depuis la fermeture de l'échappement) et taux du carter. */
    double geometricCompression = 0.0;
    double trappedCompression = 0.0;
    double crankcaseCompression = 0.0;
};

/** Course du piston depuis le point mort haut, en part de la course, à cet angle de vilebrequin (°). */
double pistonTravelShare(double crankDeg, double rodOverStroke);
/** L'angle (° de 0 à 180) où le piston qui descend a parcouru cette part de la course. */
double crankAngleForTravel(double share, double rodOverStroke);

TwoStrokePorts twoStrokePortsOf(const EngineConfig& e);

/** La chambre de détente accordée sur ce moteur, pour un cylindre. */
ExpansionChamber expansionChamberFor(const EngineConfig& e);
/** La vitesse du son dans le gaz de la chambre (m/s) : gaz brûlé à `kChamberGasK`. */
double chamberSoundMs();

/** Ce que fait le carter à pleine charge et ce que le cylindre en garde, rapporté à ρ·V d'un cylindre. */
struct TwoStrokeCharge {
    /** Rapport de débit : l'air entré dans le carter, sur celui qui remplirait la cylindrée. */
    double delivery = 0.0;
    /** Rendement de piégeage : l'air frais gardé, sur celui que les transferts ont fait entrer. */
    double trapping = 0.0;
    /** Rendement de balayage : la part d'air frais dans la charge enfermée. */
    double scavenging = 0.0;
    /** Rendement de remplissage : l'air frais enfermé, sur celui qui remplirait la cylindrée. */
    double charging = 0.0;
    /** Pression dans le cylindre quand la lumière d'échappement s'ouvre (Pa) : ce que la bouffée emporte. */
    double openPa = 0.0;
};

/** Les régimes de la table de débit : du dixième du régime maximal à 1,2 fois. */
double twoStrokeTableRpm(const EngineConfig& e, int point);
/**
 * Le cycle à pleine charge, intégré pas à pas sur quelques tours : carter, transferts, cylindre, et l'onde dans la ligne qui revient
 * à la lumière. Calculé une fois par moteur (gardé en mémoire), puis lu entre deux points de la table.
 */
TwoStrokeCharge twoStrokeFullLoad(const EngineConfig& e, double rpm);
/** Le même calcul, sans table ni mémoire : un seul régime. Pour les tests et les mesures. */
TwoStrokeCharge twoStrokeCycle(const EngineConfig& e, double rpm);

/** Balayage de Benson et Brandham : déplacement parfait jusqu'à `displacement`, puis mélange parfait, et `shortCircuit` du débit qui file droit. */
double scavengingEfficiency(double scavengeRatio, double displacement, double shortCircuit);

/** Les paramètres de balayage retenus (boucle de Schnürle) : voir docs/modele.md. */
inline constexpr double kScavengeDisplacement = 0.5;
inline constexpr double kScavengeShortCircuit = 0.12;
/** La pureté sous laquelle l'étincelle n'allume plus, et la dispersion d'un cycle à l'autre (supposées). */
inline constexpr double kPurityMin = 0.30;
inline constexpr double kPuritySpread = 0.12;
/** L'huile mélangée à l'essence, en volume : 1 pour 50 (Trabant 601). */
inline constexpr double kPremixRatio = 50.0;

/** La probabilité qu'une charge de cette pureté s'allume : dispersion triangulaire de demi-largeur `kPuritySpread`. */
double ignitionChance(double purity);

/**
 * Les ratés à faible charge. Après un raté, le cylindre garde son mélange : la pureté monte au cycle suivant, qui part. Ce que cela
 * donne en moyenne, pour cette pureté de balayage : la part des tours qui allument, et la part de l'air frais gardé qui brûle.
 */
struct TwoStrokeFiring {
    double firing = 1.0;
    double burnt = 1.0;
};
TwoStrokeFiring twoStrokeFiring(double scavenging);

/**
 * À charge partielle : le papillon ne laisse passer que `fill` du débit de pleine charge. Part du couple indiqué de pleine charge
 * (balayage meilleur à faible débit, ratés), et ce qu'il faut de carburant en plus de celui qui brûle (court-circuit, ratés).
 */
struct TwoStrokePartLoad {
    double torqueShare = 1.0;
    double trapping = 1.0;
    double firing = 1.0;
    double burnt = 1.0;
    double scavenging = 0.0;
};
TwoStrokePartLoad twoStrokePartLoad(const EngineConfig& e, double rpm, double fill);

/**
 * La pression à l'ouverture de la lumière à pleine charge, en part de celle d'un quatre-temps atmosphérique à l'ouverture de sa soupape
 * (10,5 de compression, remplissage de 0,92, ouverture à 0,821 de la course, même chaleur par kilo d'air que le cycle) : la force des
 * bouffées dans le son.
 */
double twoStrokeReleaseShare(const EngineConfig& e);

/** Rendement indiqué, en part de celui d'un quatre-temps atmosphérique : le taux réel, compté depuis la fermeture de l'échappement. */
double twoStrokeEfficiencyShare(const EngineConfig& e);
/** Pression moyenne indiquée à pleine charge (bar), sur un tour : l'air frais enfermé et le rendement du cycle. */
double twoStrokeIndicatedBar(const EngineConfig& e, double rpm);

/**
 * Ramène à ce qui existe un réglage sans sens sur un deux-temps (diesel, turbo, compresseur, vilebrequin à plat croisé, trop de
 * cylindres), et la chambre de détente d'un quatre-temps à la ligne directe. Vrai si quelque chose a changé. La CLI et le WASM
 * l'appellent ; le site passe par `coherentConfig`.
 */
bool coherentTwoStroke(EngineConfig& e);

}  // namespace revforge
