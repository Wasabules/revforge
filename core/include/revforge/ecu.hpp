#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le calculateur : la carte d'avance et de richesse, le rupteur, le launch control, le passage à fond, l'antipatinage.
 * Voir « Le calculateur » dans docs/modele.md.
 *
 * La carte se lit en fonctions pures, que d'autres sujets lisent aussi (combustion, consommation, son). Ce que le
 * calculateur fait à chaque pas (retarder, couper) se décide dans `DrivingModel` et sort par `EcuAction` ; le son le
 * reçoit par `EcuDrive`. Au défaut, rien de tout cela ne s'exécute : aucun calcul ne change.
 *
 * Pas de bibliothèque mathématique en dehors de `sqrt` : ces fonctions servent au pas de `DrivingModel`.
 */

// ── La carte : avance et richesse ──

/**
 * Le couple indiqué, en part de celui du meilleur couple (MBT), pour une étincelle à `fromMbtDeg` du MBT (positif :
 * plus tard). Une parabole `1 − (Δ/68°)²`, à peu près symétrique près du MBT : la forme des tables « torque ratio »
 * des calculateurs, la même que l'anti-lag. Jamais négatif.
 */
double sparkTorqueShare(double fromMbtDeg);
/**
 * La chaleur que l'étincelle tardive laisse au gaz d'échappement, en part de l'énergie du carburant : le travail que le
 * piston n'a pas reçu, moins 20 % dans les parois. Le même bilan que l'anti-lag. 0 du côté avancé : le travail perdu
 * y part dans les parois, pas dans le gaz. `indicatedEfficiency` : le rendement du cylindre, celui de `ConsumptionModel`.
 */
double sparkExhaustGain(double fromMbtDeg, double indicatedEfficiency = 0.375);
/**
 * L'avance du meilleur couple (° avant le point mort haut) : celle qui place la moitié de la combustion vers 8° après le
 * point mort haut. La durée jusque-là croît avec le régime (en degrés, la turbulence suit le piston, pas tout à fait le
 * temps) et à faible remplissage (gaz résiduels, mélange moins dense). Ordres de grandeur de Heywood, chap. 9, supposés.
 */
double ecuMbtAdvanceDeg(double rpm, double load);

/**
 * Le couple indiqué par kilogramme d'air à cette richesse, en part de celui de λ = 1. Pauvre, il manque du carburant ;
 * riche, l'air est tout brûlé, et le surplus ajoute des moles et refroidit la charge. La courbe que lit déjà le
 * carburateur (`torqueOfRichness`, d'après Heywood, chap. 5) : un sommet plat vers λ 0,87, 7 % au-dessus de λ 1.
 */
double lambdaTorqueShare(double lambda);
/**
 * L'échauffement des gaz brûlés à cette richesse, en part de celui de λ = 1 : la température de flamme adiabatique d'un
 * hydrocarbure dans l'air, qui culmine un peu au-delà de la stœchiométrie (φ ≈ 1,05). Tables usuelles, ordre de grandeur.
 */
double lambdaHeatShare(double lambda);

/** Le λ de la carte d'origine à pleine charge : 0,88, 0,82 suralimenté, celui de `ConsumptionModel`. 1 pour un diesel. */
double stockFullLoadLambda(const EngineConfig& e);
/** Le λ demandé à pleine charge : le réglage, ou celui d'origine. Un carburateur ou un diesel garde le sien. */
double fullLoadLambdaOf(const EngineConfig& e);
/**
 * L'enrichissement de mise en température, en richesse (1 : aucun) : le carburant qui se dépose sur des parois froides
 * ne brûle pas, le calculateur en ajoute. 1,3 à −20 °C, 1,18 à 20 °C, rien au-delà de 80 °C (ordre de grandeur des
 * calculateurs de série, supposé). `coolantC` se lit de la thermique : 90 °C tant qu'elle n'est pas branchée.
 */
double warmupRichness(double coolantC);
/**
 * Le λ demandé à ce remplissage : 1 en boucle fermée, la pleine charge à partir de 85 % de remplissage, avec la même
 * rampe que la consommation (`kEnrichFromShare`), sans sa part liée à la puissance ; et l'enrichissement à froid.
 */
double demandedLambda(const EngineConfig& e, double load, double coolantC = 90.0);
/**
 * Le régime où coupe le rupteur : le régime maximal, ou plus bas si les soupapes s'affolent avant (2 % de marge, supposée).
 * `valveFloatRpm` se lit des culbuteurs : 0 tant qu'ils ne sont pas branchés, et le rupteur reste au régime maximal.
 */
double limiterRpmFor(double redlineRpm, double valveFloatRpm);
/**
 * L'avance demandée (° avant le point mort haut) : la carte d'origine, supposée au meilleur couple, plus son décalage,
 * moins ce que le capteur de cliquetis retire (`knockRetardDeg`, lu de la combustion : 0 tant qu'elle n'est pas branchée).
 */
double demandedAdvanceDeg(const EngineConfig& e, double rpm, double load, double knockRetardDeg = 0.0);

/** La carte s'écarte-t-elle de l'origine ? Faux : aucun calcul ne change. Sans effet sur un diesel. */
bool ecuMapTuned(const EngineConfig& e);
/** L'écart au meilleur couple de la carte réglée (positif : plus tard), cliquetis lu compris. */
/** Le décalage d'avance est porté par la carte du cliquetis (`knockMapOf`) : tout quatre-temps à étincelle. */
inline bool ecuAdvanceInKnockMap(const EngineConfig& e) { return !e.burnsDiesel() && !e.twoStroke() && e.ignitionOffsetDeg != 0; }
double mapRetardDeg(const EngineConfig& e, double knockRetardDeg = 0.0);
/** Le rapport du couple indiqué de pleine charge à celui de la richesse d'origine. 1 sans réglage. */
double lambdaFullLoadRatio(const EngineConfig& e);
/** Le même, à ce remplissage : il ne joue qu'à partir de 85 %, là où la carte enrichit. */
double lambdaLoadRatio(const EngineConfig& e, double load);
/** Le couple indiqué de pleine charge face à la carte d'origine : avance (cliquetis lu) et richesse. 1 sans réglage. */
double ecuFullLoadShare(const EngineConfig& e, double knockRetardDeg = 0.0);

// ── Ce que le calculateur fait à chaque pas ──

/** Ce que le calculateur décide à ce pas, au-delà de sa carte. Tout au neutre : il ne fait rien. */
struct EcuAction {
    double retardDeg = 0.0;   // étincelle retardée en plus de la carte (°)
    double firedShare = 1.0;  // part des explosions qui ont lieu : la coupure partielle
    bool fuelOnCut = false;   // les cylindres coupés reçoivent encore leur carburant : coupure d'allumage
    bool limiter = false;     // le rupteur agit
    bool launch = false;      // le launch control tient le régime
    bool flat = false;        // le passage à fond coupe
    double traction = 0.0;    // part du couple que retire l'antipatinage, 0 à 1
};

/** La part du couple qui reste, face à la carte (`mapRetard`, son écart au meilleur couple) : le retard, puis les cylindres coupés. */
double ecuActionShare(const EcuAction& action, double mapRetard = 0.0);

/** Ce que le calculateur change au gaz qui part vers la ligne ou la turbine. */
struct EcuExhaust {
    double mapRetardDeg = 0.0;          // la carte, face au meilleur couple (positif : plus tard), à tout remplissage
    double fullLoadHeatRatio = 1.0;     // échauffement à pleine charge, face à la richesse d'origine
    double retardDeg = 0.0;             // retard du pas
    double firedShare = 1.0;
    bool fuelOnCut = false;
    double efficiency = 0.375;          // rendement du cylindre, pour le travail que le retard lui retire
    bool active() const { return mapRetardDeg != 0.0 || fullLoadHeatRatio != 1.0 || retardDeg != 0.0 || firedShare != 1.0; }
};

/** La carte d'un moteur, sans action de pas : ce que voient les courbes à l'équilibre. */
EcuExhaust ecuMapExhaust(const EngineConfig& e);
/**
 * Le gaz à la sortie des cylindres à pleine charge et à ce régime, à l'air de référence (K) : avec la carte réglée, ou
 * celle d'origine. Le modèle de `TurboCharger`, sans tube ni turbine. Pour la fiche, et pour qui en a besoin.
 */
double fullLoadExhaustK(const EngineConfig& e, double rpm, bool stockMap = false);

/** Ce qui brûle au collecteur, d'un cylindre dont on n'a coupé que l'étincelle, s'il y fait assez chaud : celle de l'anti-lag. */
inline constexpr double kUnburntCollectorShare = 0.45;

/**
 * La température du gaz qui part vers la ligne, quand le calculateur s'en mêle. `normalK` est celle d'un allumage normal à la
 * richesse d'origine, `heatRiseK` l'échauffement si toute l'énergie du carburant lui revenait. Les cylindres allumés
 * gagnent la chaleur du retard ; les coupés sans carburant n'apportent que de l'air ; avec carburant, une part brûle au
 * collecteur, selon `ignition` (`antiLagIgnition`).
 */
double ecuExhaustK(const EcuExhaust& ecu, double intakeK, double normalK, double heatRiseK, double fill, double ignition);

/**
 * La pression à l'ouverture de la soupape d'échappement d'un cylindre allumé, rapportée à un allumage normal : ce que le
 * retard laisse de chaleur en plus au gaz, et ce que la richesse en retire. `exhaustShare` : la part de l'énergie du
 * carburant que le gaz emporte déjà.
 */
double ecuEvoRatio(const EcuExhaust& ecu, double exhaustShare, double fill);

/**
 * Le son, quand le calculateur agit : rien n'y est deviné. Sans lui, ou inactif, le son reste celui d'origine au bit près.
 */
struct EcuDrive {
    bool active = false;
    float evoRatio = 1.0f;       // pression à l'ouverture des cylindres allumés, retard compris
    float firedShare = 1.0f;     // part des explosions, hors coupure franche (`setDrive`)
    bool fuelOnCut = false;      // un cylindre coupé envoie son mélange à la ligne
    float unburntJoules = 0.0f;  // ce qui peut brûler au collecteur, par cylindre coupé (J)
    float ignition = 0.0f;       // 0 à 1 : le collecteur est-il assez chaud pour l'enflammer ?
};

// ── Les réglages du pas : constantes, supposées et citées dans docs/modele.md ──

/** Rupteur progressif : le retard monte sur les derniers 3 % avant le régime maximal, puis la coupure sur 2 % au-delà. */
inline constexpr double kSoftRetardBand = 0.03;
inline constexpr double kSoftRetardDeg = 15.0;
inline constexpr double kSoftCutBand = 0.02;
/** Launch control : 25° de retard, l'ordre de grandeur des calculateurs de préparation (supposé). */
inline constexpr double kLaunchRetardDeg = 25.0;
/** Le départ d'un essai avec launch control : frein serré et pied au plancher pendant ce temps, avant de lâcher le frein. */
inline constexpr double kLaunchStageS = 3.0;
/** Antipatinage : la part de l'adhérence que chaque mode accepte de laisser (voir `tractionSlipTarget`). */
inline constexpr double kTractionSportGrip = 0.9;
inline constexpr double kTractionFullGrip = 0.8;
/** Antipatinage : retard au plus, avant de couper des cylindres (supposé, l'ordre de grandeur d'un ESP de série). */
inline constexpr double kTractionMaxRetardDeg = 25.0;

/**
 * Le glissement (m/s) que l'antipatinage vise. Le pneu du modèle, `F = μ·N·s/√(1+s²)` avec s en m/s, n'a pas de sommet :
 * plus il glisse, plus il pousse. La cible se lit donc sur sa courbe, comme la part de l'adhérence que le mode accepte de
 * laisser pour garder de la marge en virage : 90 % en sport (2,1 m/s), 80 % en complet (1,3 m/s). 0 : aucun.
 */
double tractionSlipTarget(TractionControl mode);

/** Le retard et la part des cylindres qui donnent une part de couple voulue : le retard d'abord, jusqu'à `maxRetardDeg`. */
void tractionActuation(double torqueShare, double maxRetardDeg, double& retardDeg, double& firedShare);

}  // namespace revforge
