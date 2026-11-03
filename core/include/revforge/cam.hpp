#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * La came : sa loi de levée, son calage, et ce qu'elle fait au remplissage. Voir « La came » dans docs/modele.md.
 *
 * Les angles sont en degrés de vilebrequin, sur le cycle de 720° du son : 0 au point mort haut d'allumage, 360 au point
 * mort haut d'échange. Les durées et les points d'ouverture et de fermeture se lisent à 1 mm de levée, comme les fiches
 * européennes ; la came d'origine du simulateur y fait 240°, à 110° entre cames, et ouvre l'échappement à 130°, le ferme
 * à 370°, ouvre l'admission à 350° et la ferme à 590° : ce sont les `kEvo`… d'`engine_sound.cpp`.
 *
 * Tout ce qui suit est pur, sauf les tables du remplissage, calculées une fois par moteur et gardées (voir `camTables`).
 */
struct CamProfile {
    /** Durée à 1 mm de levée, admission comme échappement : la came est symétrique. */
    double durationDeg = 240.0;
    /** Levée à la soupape (mm). */
    double liftMm = 10.0;
    double lobeSeparationDeg = 110.0;
    /** Axe de la came d'admission après le point mort haut d'échange : l'angle entre cames, plus ou moins le déphaseur. */
    double intakeCenterDeg = 110.0;
    double exhaustCenterDeg = 110.0;
    /** Demi-durée siège à siège : la levée y est nulle, et ses trois premières dérivées aussi. */
    double halfSpanDeg = 150.0;
    /** Ouvertures et fermetures à 1 mm, dans le cycle de 720°. */
    double ivoDeg = 350.0, ivcDeg = 590.0, evoDeg = 130.0, evcDeg = 370.0;
};

/** La came d'origine : 240° à 1 mm, 110° entre cames, une levée de 0,12 fois l'alésage (celle de `valvetrain.cpp`). */
inline constexpr double kCamStockDurationDeg = 240.0;
inline constexpr double kCamStockLobeSeparationDeg = 110.0;
/** Les points d'un profil se lisent à cette levée (mm). */
inline constexpr double kCamReferenceLiftMm = 1.0;
/** Ce que le déphaseur d'admission peut décaler, de part et d'autre du calage de base (degrés de vilebrequin). */
inline constexpr double kCamPhaserAuthorityDeg = 20.0;
/**
 * Une came à deux profils : un doux et un pointu, de part et d'autre du profil réglé, sur le même axe. Tant de durée, et
 * tant de la levée, de chaque côté (voir docs/modele.md).
 */
inline constexpr double kCamTwoStepSpreadDeg = 25.0;
inline constexpr double kCamTwoStepLiftSpread = 0.15;
/**
 * Bascule de profil : de part et d'autre du régime de croisement, en part de ce régime ; la charge qu'il faut, et ce qu'elle
 * peut perdre avant de le faire ressortir (supposés, de l'ordre d'un VTEC).
 */
inline constexpr double kCamSwitchHysteresis = 0.02;
inline constexpr double kCamSwitchLoad = 0.6;
inline constexpr double kCamSwitchLoadHysteresis = 0.1;
/** Le déphaseur : sous ce remplissage, il reste en butée retard ; il tourne à cette vitesse (degrés de vilebrequin par seconde). */
inline constexpr double kCamPhaserPartLoad = 0.3;
inline constexpr double kCamPhaserDegPerS = 150.0;

/**
 * La loi de levée, de 1 au sommet (x = 0) à 0 aux sièges (x = ±1) : (1 − x²)²(1 − x⁴) = 1 − 2x² + 2x⁶ − x⁸. Levée, vitesse
 * et accélération sont nulles aux sièges, donc continues avec le cercle de base. C'est le polynôme 3-4-5-6 d'une came à
 * simple arrêt, (1 − x²)³, rempli par (1 + x²).
 */
double camLiftShare(double x);
/** Sa dérivée seconde au sommet : −4. Sert à la décélération au nez. */
inline constexpr double kCamNoseCurvature = -4.0;

/** Un profil, depuis sa durée à 1 mm, sa levée, l'angle entre cames et l'axe d'admission. */
CamProfile makeCamProfile(double durationDeg, double liftMm, double lobeSeparationDeg, double intakeCenterDeg);
/** Le même profil, la came d'admission avancée de tant de degrés (négatif : retardée). L'échappement ne bouge pas. */
CamProfile advancedIntake(const CamProfile& base, double advanceDeg);

/** Le moteur a-t-il des soupapes ? Ni un rotatif, ni un deux-temps. */
bool camHasValves(const EngineConfig& e);
/** La came et son calage sont ceux d'origine : rien de ce fichier ne s'exécute alors ailleurs, au bit près. */
bool camIsStock(const EngineConfig& e);
bool camPhased(const EngineConfig& e);
bool camTwoStep(const EngineConfig& e);

double stockCamLiftMm(double boreMm);
CamProfile stockCamProfile(const EngineConfig& e);
/**
 * Le profil réglé (0 : d'origine). Sur une came à deux profils, `camProfileOf` donne le doux, celui du bas, et
 * `highCamProfileOf` le pointu ; sinon, tous deux donnent le profil réglé.
 */
CamProfile camProfileOf(const EngineConfig& e);
CamProfile highCamProfileOf(const EngineConfig& e);

/** Levée de la soupape (mm) à cet angle du cycle. */
double camValveLiftMm(const CamProfile& cam, double cycleDeg, bool intake);
/** Fermeture de l'admission après le point mort bas (degrés). */
double intakeClosingAbdcDeg(const CamProfile& cam);
/** Croisement à 1 mm : l'admission ouverte avant que l'échappement soit fermé (degrés). */
double valveOverlapDeg(const CamProfile& cam);
/** Durée pendant laquelle la levée dépasse `liftMm` (degrés). */
double camDurationAtLiftDeg(const CamProfile& cam, double liftMm);

/**
 * Compression effective : volume à la fermeture de l'admission sur volume mort. `rodToCrank` : bielle sur rayon de
 * manivelle (3,2 dans tout le simulateur).
 */
double effectiveCompressionRatio(double geometricRatio, double ivcAbdcDeg, double rodToCrank = 3.2);

/** Décélération de la soupape au nez de came (m/s², positive), à ce régime : en carré du régime. */
double camNoseDecelerationMs2(const CamProfile& cam, double rpm);

/**
 * Le facteur de croisement de Fox, Cheng et Heywood (SAE 931025) : (1,45/B)(107 + 7,8Δθ + Δθ²)(Lv·Dv/B²), alésage, levée
 * et diamètre en mm, Δθ le croisement en degrés. `intakeValvesMm` : la somme des diamètres des soupapes d'admission.
 */
double overlapFactor(const CamProfile& cam, double boreMm, double intakeValvesMm);
/**
 * La fraction de gaz résiduels, d'après la même corrélation :
 * x = 1,266 (OF/N)(pi/pe)^−0,87 √|pe − pi| + 0,632 (φ/ε)(pi/pe)^−0,74, pressions en kPa.
 */
double residualGasFraction(double overlapFactor, double rpm, double intakeKpa, double exhaustKpa, double compressionRatio, double equivalenceRatio = 1.0);
/**
 * La dispersion de la pression moyenne indiquée d'un cycle à l'autre (COV, en part), selon les gaz résiduels : la flamme
 * laminaire ralentit en 1 − 2,06 x^0,77 (Metghalchi et Keck), et la dispersion suit son inverse à la puissance 1,62.
 * N'a de sens que sur un moteur à étincelle.
 */
double combustionCov(double residualFraction);

/** Ce que la came change, régime par régime, à pleine charge, face à la came d'origine. 1 partout sur une came d'origine. */
struct CamEffect {
    /** Pression moyenne indiquée nette (remplissage, détente perdue, pompage) : c'est le couple. */
    double torque = 1.0;
    /** Air enfermé. */
    double filling = 1.0;
    /** Pression dans le cylindre à l'ouverture de l'échappement : c'est la bouffée du son. */
    double opening = 1.0;
};
/** À ce régime, sur le profil bas ou haut, le déphaseur à son meilleur calage s'il y en a un. */
CamEffect camEffect(const EngineConfig& e, double rpm, bool high);
/** Le couple de la fiche : le meilleur des deux profils, s'il y en a deux. 1 sur une came d'origine. */
double camTorqueRatio(const EngineConfig& e, double rpm);
/** Le calage que vise le déphaseur à pleine charge (degrés d'avance de l'admission), à ce régime et sur ce profil. */
double camPhaserTargetDeg(const EngineConfig& e, double rpm, bool high);
/**
 * Le régime où les deux courbes de couple se croisent : au-delà, le second profil donne plus. 0 s'il n'y en a pas, ou s'il
 * tombe au-delà de `floatRpm`, le régime d'affolement des soupapes (0 : aucune limite).
 */
double camSwitchRpm(const EngineConfig& e, double floatRpm = 0.0);

/** Le ralenti : le papillon qu'il faut pour tenir le régime, et ce que la came y laisse de gaz brûlés. */
struct CamIdle {
    double rpm = 0.0;
    double manifoldKpa = 0.0;
    double residual = 0.0;
    double cov = 0.0;
};
CamIdle camIdle(const EngineConfig& e);
CamIdle stockIdle(const EngineConfig& e);

/** L'état d'une distribution variable, pour le son et l'affichage. */
struct CamDrive {
    /** Le second profil est en prise. */
    bool high = false;
    /** Avance de l'admission par le déphaseur (degrés de vilebrequin). */
    float phaserDeg = 0.0f;
};

/** Remplit ce que le son doit savoir de la came. Ne touche à rien sur une came d'origine. */
void applyCam(const EngineConfig& e, EngineProfile& p);

}  // namespace revforge
