#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * L'air de charge, du compresseur à la soupape d'admission : l'échangeur (air-air, air-eau ou aucun), l'injection d'eau et le
 * protoxyde d'azote. Tout est en fonctions pures, sauf l'état de conduite (`ChargeAir`), qui avance avec `DrivingModel` et
 * passe par `revforge::portable` : la boucle d'eau et la bouteille s'intègrent pas à pas. Voir « L'air de charge » dans
 * docs/modele.md.
 */

// ── Ce que calculent d'autres sujets, lu ici comme paramètres ──

/** L'air autour de la voiture (sujet atmosphère) et l'eau du moteur (sujet thermique). Valeurs de référence par défaut. */
struct ChargeConditions {
    double ambientK = 293.0;
    double ambientPa = 101325.0;
    double engineCoolantK = 363.0;  // moteur chaud, eau à 90 °C
};

/** Le carburant (sujet combustion) et la richesse à pleine charge (sujet calculateur). Essence RON 95 par défaut. */
struct ChargeFuel {
    double latentJPerKg = 350e3;        // chaleur de vaporisation : 300 à 400 kJ/kg pour une essence
    double stoichiometricAfr = 14.7;    // comme `kPetrol` de consumption.cpp
    double lowerHeatingJPerKg = 42.9e6;
    double fullLoadLambda = 0.88;       // comme `kFullLoadLambda` de consumption.cpp
};

// ── Les échangeurs, par la méthode ε-NTU ──

/** Efficacité d'un échangeur à courants croisés, les deux fluides non brassés (approximation d'Incropera, à 1 % près). */
double crossflowEffectiveness(double ntu, double capacityRatio);
/** L'air de route qui traverse une face à la calandre (kg/s) : une part de la vitesse, plus le ventilateur à l'arrêt. */
double roadAirFlowKgS(double faceM2, double speedMs, double ambientK, double ambientPa);
/**
 * La vitesse à laquelle les courbes de la fiche supposent l'échangeur : 100 km/h. La fiche se mesure au banc, devant une
 * soufflerie qui donne au faisceau l'air de cette vitesse : sur un banc à rouleaux, le ventilateur suit la vitesse des rouleaux
 * et peut plafonner à 70 mi/h, 113 km/h (40 CFR 1066.105(c)(2)). Voir « L'air de charge » dans docs/modele.md.
 */
inline constexpr double kChargeReferenceSpeedMs = 100.0 / 3.6;
/** Le faisceau d'un air-air, en tranches le long du trajet de la charge : chacune a sa paroi et l'air de route qui la traverse. */
inline constexpr int kCoolerSlices = 4;

/** La sortie de l'échangeur d'origine (air-air), telle que `coolerOutlet` la calcule dans turbo.cpp et supercharger.cpp. */
double stockCoolerOutletK(double hotK, double flowShare, double ambientK = 293.0);

/**
 * L'échangeur, dimensionné sur le point nominal du compresseur : à la vitesse de référence et au débit nominal, il rend la
 * même température que l'échangeur d'origine. C'est le même échangeur, autrement refroidi.
 */
struct ChargeCoolerSpec {
    Intercooler kind = Intercooler::AirToAir;
    double ratedFlow = 0.2;    // kg/s d'air de charge au point nominal
    double ratedHotK = 400.0;  // sortie du compresseur à ce point
    // Côté air de charge : conductance au débit nominal (W/K), qui suit ṁ^0,8 (Dittus-Boelter).
    double chargeHaW = 0.0;
    // Air-air : face à la calandre, conductance côté route à la vitesse de référence (W/K), qui suit ṁ^0,5 (ailettes à persiennes).
    double faceM2 = 0.0;
    double roadHaW = 0.0;
    // Air-eau : la brique (côté eau fixe, pompe électrique), le radiateur basse température, la masse de la boucle, et ce que le
    // compartiment lui rend.
    double coolantCapacityWK = 0.0;  // débit de la pompe × capacité thermique du liquide
    double brickWaterHaW = 0.0;
    double radiatorFaceM2 = 0.0;
    double radiatorAirHaW = 0.0;     // à la vitesse de référence
    double radiatorWaterHaW = 0.0;
    double loopHeatJK = 0.0;         // liquide et aluminium de la boucle
    double soakWK = 0.0;             // conductance du compartiment moteur vers la boucle
    double coreJK = 0.0;             // air-air : l'aluminium du faisceau (J/K)
};

/**
 * Air-air par ε-NTU, avec l'air de route, l'échangeur de toutes les suralimentations par défaut (finitions). Le faisceau est
 * découpé en `kCoolerSlices` tranches le long du trajet de la charge ; dans chacune, la charge et l'air de route échangent avec la
 * paroi, chacun par ε = 1 − e^−NTU (une paroi à une seule température), et l'air de route ne traverse qu'une tranche : un échangeur
 * à courants croisés en différences finies. Dimensionné pour rendre l'échangeur d'origine au débit nominal, à la vitesse de référence.
 */
ChargeCoolerSpec airToAirCoolerFor(double ratedFlow, double ratedHotK, const ChargeConditions& conditions = {});
/** Sortie de l'air-air à l'équilibre pour ce débit de charge et cette vitesse de la voiture : la forme fermée des tranches. */
double airToAirOutletK(const ChargeCoolerSpec& spec, double hotK, double flow, double speedMs, const ChargeConditions& conditions = {});
/** L'air-air à l'équilibre sur le banc de la fiche : la soufflerie à la vitesse de référence. */
double benchCoolerOutletK(const ChargeCoolerSpec& spec, double hotK, double flow, const ChargeConditions& conditions = {});

/**
 * Le faisceau en conduite : la paroi de chaque tranche a sa masse, et garde la chaleur d'une charge chaude passée (l'échauffement
 * à l'arrêt, « heat soak »). Ce qui sort est la charge qui a traversé les tranches ; à débit nul, l'air qui y dort a la
 * température de la dernière paroi. Pas d'allocation, et `revforge::portable` : il avance avec `DrivingModel`.
 */
struct AirCoolerCore {
    bool ready = false;
    double wallK[kCoolerSlices] = {};
    /** Les parois à cette température : l'air ambiant, au premier pas. */
    void settle(double kelvin);
    /** Un pas : rend la sortie de l'échangeur. `hotK` et `flow` : la charge qui y entre ; `speedMs` : la voiture. */
    double step(const ChargeCoolerSpec& spec, double dt, double hotK, double flow, double speedMs, const ChargeConditions& conditions);
};

/** L'échangeur choisi : l'air-air ε-NTU, l'air-eau dimensionné comme ci-dessus, ou aucun. */
ChargeCoolerSpec chargeCoolerFor(Intercooler kind, double ratedFlow, double ratedHotK, const ChargeConditions& conditions = {});

/** Ce que la brique air-eau retire à l'air de charge, rapporté à ṁ·cp·(T2 − Teau). */
double brickEffectiveness(const ChargeCoolerSpec& spec, double flow);
/** Ce que le radiateur basse température rejette par kelvin d'écart entre l'eau et l'air extérieur (W/K). */
double radiatorConductanceWK(const ChargeCoolerSpec& spec, double speedMs, const ChargeConditions& conditions = {});
/** Le compartiment moteur, qui chauffe à l'arrêt : la moitié de l'écart entre l'eau du moteur et l'air, moins en roulant. */
double bayAirK(double speedMs, const ChargeConditions& conditions = {});
/** La boucle d'eau à l'équilibre pour cette charge et cette vitesse (K). */
double waterLoopSteadyK(const ChargeCoolerSpec& spec, double hotK, double flow, double speedMs, const ChargeConditions& conditions = {});
/**
 * La sortie de l'échangeur à l'équilibre, à la vitesse de référence : c'est ce que voient les courbes de la fiche. Aucun
 * échangeur : la sortie du compresseur. Air-air : le faisceau ε-NTU devant la soufflerie du banc (l'ancienne loi d'origine
 * s'il n'est pas dimensionné).
 */
double chargeCoolerSteadyK(const ChargeCoolerSpec& spec, double hotK, double flow, const ChargeConditions& conditions = {});

/** Le chemin de l'air entre compresseur et papillon pour cet échangeur, dimensionné pour 2 L (recopié par `fluidGas.ts`). */
struct ChargePathPart {
    double lengthM;
    double areaShare;  // rapportée au conduit
    int bends;         // coudes à 90°
    bool core;         // le faisceau, ou la brique
};
struct ChargePath {
    ChargePathPart hot;
    ChargePathPart core;
    ChargePathPart cold;
    double coreDropShare;  // perte du faisceau, rapportée à celle de l'air-air d'origine
};
const ChargePath& chargePathOf(Intercooler kind, bool turbo);
/**
 * Change le volume et la perte entre compresseur et papillon, d'un échangeur d'origine à celui-ci : `volumeM3` et
 * `coolerDrop` (Pa·(kg/m³)/(kg/s)², comme dans `TurboSpec`) sont ceux de l'air-air d'origine, `ductAreaM2` la section du conduit.
 */
void applyChargePath(Intercooler kind, bool turbo, double ductAreaM2, double& volumeM3, double& coolerDrop);

// ── L'injection d'eau ──

/** Pression de vapeur saturante de l'eau (Pa) : Clausius-Clapeyron intégrée avec une chaleur latente linéaire en T (Kirchhoff). */
double waterSaturationPa(double kelvin);
/** Chaleur de vaporisation de l'eau (J/kg) à cette température. */
double waterLatentJPerKg(double kelvin);

struct WaterSpray {
    double outK = 293.0;          // la charge après évaporation
    double vapourPerAir = 0.0;    // eau évaporée avant la soupape (kg/kg d'air)
    double liquidPerAir = 0.0;    // ce qui arrive encore liquide dans le cylindre
    double vapourPa = 0.0;        // pression partielle de la vapeur
    double densityShare = 1.0;    // masse d'air admise, rapportée à celle de la charge sèche avant l'injection
};
/**
 * De l'eau à `waterK` pulvérisée dans la charge : elle s'évapore jusqu'à saturer l'air, ou jusqu'à la dernière goutte.
 * Bilan d'enthalpie à pression constante, résolu par dichotomie sur la température de sortie.
 */
WaterSpray waterSpray(double chargeK, double chargePa, double waterPerAir, double waterK);
/** L'eau injectée, en part du carburant, et la charge à partir de laquelle la pompe la débite. */
inline constexpr double kWaterPerFuel = 0.3;
/** Eau par kilo d'air pour ce remplissage (0 sous le seuil, tout au-delà). */
double waterPerAirAt(double fill, const ChargeFuel& fuel = {});

// ── Le protoxyde d'azote ──

/** Le protoxyde saturé, selon les corrélations ESDU 91022 (de 183 à 309 K). */
struct NitrousSaturation {
    double pressurePa = 0.0;
    double liquidKgM3 = 0.0;
    double vapourKgM3 = 0.0;
    double latentJPerKg = 0.0;
};
NitrousSaturation nitrousSaturation(double kelvin);

/** Le kit : gicleurs de protoxyde et d'essence, bouteille, fenêtre d'activation. Tout découle de la puissance annoncée. */
struct NitrousKit {
    double ratedKw = 0.0;
    double nitrousKgS = 0.0;     // au point de calcul : bouteille pleine à l'air ambiant, collecteur à la pression nominale
    double fuelKgS = 0.0;        // le gicleur d'essence, à pression régulée : il ne suit pas la bouteille
    double jetCdAM2 = 0.0;       // le gicleur de protoxyde, coefficient de décharge compris
    double ratedManifoldPa = 101325.0;
    double activationRpm = 3000.0;
    double cutoffRpm = 6000.0;
    double bottleM3 = 0.0;
    double fillKg = 0.0;
    double shellJK = 0.0;        // la bouteille vide
};
NitrousKit nitrousKitFor(int kw, double redlineRpm, double ratedManifoldPa, const ChargeConditions& conditions = {}, const ChargeFuel& fuel = {});

struct NitrousBottle {
    double massKg = 0.0;
    double kelvin = 293.0;
    double pressurePa = 101325.0;
    double liquidKg = 0.0;
};
NitrousBottle fullBottle(const NitrousKit& kit, double ambientK);
/** La pression de la bouteille, et sa part liquide, pour cette masse et cette température. */
void settleBottle(NitrousBottle& bottle, const NitrousKit& kit);
/** Ce qui passe par le gicleur (kg/s) : du liquide tant qu'il en reste, sinon de la vapeur. */
double nitrousFlowKgS(const NitrousKit& kit, const NitrousBottle& bottle, double manifoldPa);
/** Un pas : le liquide qui part, celui qui bout pour remplir le vide, la paroi qui rend un peu de chaleur. */
void stepBottle(NitrousBottle& bottle, const NitrousKit& kit, double flowKgS, double dt, double ambientK);

/** Ce que le protoxyde apporte à la combustion. */
struct NitrousBurn {
    double indicatedW = 0.0;    // puissance indiquée ajoutée
    double burntFuelKgS = 0.0;  // essence brûlée par l'oxygène du protoxyde ; le reste du gicleur part imbrûlé
    double oxygenKgS = 0.0;
};
NitrousBurn nitrousBurn(double nitrousKgS, double fuelKgS, const ChargeFuel& fuel = {});
/** Le rendement indiqué que le kit rencontre : le même ordre que `consumption.cpp` (0,375 à 0,405). */
inline constexpr double kNitrousIndicatedEfficiency = 0.39;

/** La charge, l'air, le protoxyde à `bottleK` et l'essence ajoutée, mélangés à pression constante (K). */
double nitrousMixK(double chargeK, double airKgS, double nitrousKgS, double fuelKgS, double bottleK, const ChargeFuel& fuel = {});

// ── La charge au collecteur : ce que lit le cliquetis ──

/** Ce qui entre dans le cylindre. */
struct ManifoldCharge {
    double pressurePa = 101325.0;   // après le papillon
    double temperatureK = 293.0;    // après l'échangeur, l'eau et le protoxyde
    double waterPerAir = 0.0;       // eau injectée (kg/kg d'air)
    double liquidWaterPerAir = 0.0; // ce qui s'évaporera pendant la compression
    double nitrousPerAir = 0.0;
    double oxygenShare = 0.2314;    // oxygène dans le comburant (masse) : 0,23 dans l'air, 0,36 dans le protoxyde
    double airShare = 1.0;          // masse d'air admise, rapportée à la même charge sans eau ni protoxyde
};

/** Ce qu'il faut savoir du moteur pour mélanger la charge. */
struct ChargeMix {
    double chargeK = 293.0;       // après l'échangeur (ou l'air d'admission d'un atmosphérique)
    double plenumPa = 101325.0;
    double airKgS = 0.0;
    double waterPerAir = 0.0;
    double nitrousKgS = 0.0;
    double nitrousFuelKgS = 0.0;
    double bottleK = 293.0;
    double waterK = 293.0;        // l'eau du réservoir, à l'air ambiant
};
/** Protoxyde et essence d'abord, puis l'eau jusqu'à saturation de l'air refroidi. Fonction pure. */
ManifoldCharge mixCharge(const ChargeMix& mix, const ChargeFuel& fuel = {});

// ── Le moteur entier : ce que voient la fiche et `DrivingModel` ──

struct Powertrain;
/** Vrai si l'air de charge change quelque chose à ce moteur. Sinon, rien de tout cela ne s'exécute. */
bool chargeAirActive(const Powertrain& p);
/** Le même moteur, avec l'échangeur d'origine, sans eau ni protoxyde : la boîte et l'embrayage restent dimensionnés sur lui. */
Powertrain chargeAirStock(const Powertrain& p);
/** Le couple de la fiche, `baseNm` corrigé de la densité de la charge et augmenté du protoxyde (bouteille pleine). */
double chargeAirTorqueNm(const Powertrain& p, double rpm, double baseNm);
/** La charge au collecteur à pleine charge et à ce régime, à la vitesse de référence : pour la limite de cliquetis. */
ManifoldCharge steadyManifoldCharge(const Powertrain& p, double rpm);

// ── L'état de conduite ──

/** Ce que `DrivingModel` règle une fois par moteur : `chargeAirSetupOf`. */
struct ChargeAirSetup {
    bool active = false;          // rien à faire au défaut : le chemin de calcul reste celui d'origine
    bool turbo = false;
    bool supercharger = false;
    ChargeCoolerSpec cooler;
    bool water = false;
    NitrousKit nitrous;
    ChargeConditions conditions;
    ChargeFuel fuel;
};

ChargeAirSetup chargeAirSetupOf(const Powertrain& p);

/** Ce que l'air de charge reçoit du moteur à chaque pas. */
struct ChargeAirInput {
    double dt = 0.001;
    double rpm = 0.0;
    double throttle = 0.0;     // la pédale : le contacteur de plein gaz du kit
    double fill = 0.0;         // le remplissage, papillon
    double speedMs = 0.0;
    bool burning = false;
    double compressorOutK = 293.0;
    double chargeFlowKgS = 0.0;  // l'air qui traverse l'échangeur
    double chargeK = 293.0;      // après l'échangeur, ou l'air d'admission d'un atmosphérique
    double referenceK = 293.0;   // ce que le couple de base suppose à cet endroit
    double plenumPa = 101325.0;
    double airKgS = 0.0;         // l'air qu'avale le moteur
};

class ChargeAir {
public:
    /** `refill` : bouteille pleine et boucle d'eau à l'air ambiant. Sinon, l'état reste. */
    void configure(const ChargeAirSetup& setup, bool refill);
    const ChargeAirSetup& setup() const { return setup_; }

    /** La boucle d'eau avance d'un pas ; rend la sortie de l'échangeur. 0 pour l'air-air d'origine : le compresseur garde la sienne. */
    double stepCooler(double dt, double hotK, double flow, double speedMs);
    /** Le reste : l'eau, la bouteille, le mélange. Après le pas du compresseur. */
    void step(const ChargeAirInput& in);

    /** Ce que la charge change au couple indiqué de base : densité de l'air admis. 1 si rien ne change. */
    double torqueShare() const { return torqueShare_; }
    /** Le couple indiqué qu'ajoute le protoxyde (Nm). */
    double nitrousNm() const { return nitrousNm_; }
    bool nitrousOn() const { return nitrousOn_; }
    double nitrousKgS() const { return nitrousKgS_; }
    /** Tout le gicleur d'essence, brûlé ou non : c'est ce que paie le réservoir (g/s). */
    double nitrousFuelGps() const { return nitrousOn_ ? setup_.nitrous.fuelKgS * 1000.0 : 0.0; }
    double waterGps() const { return waterGps_; }
    const NitrousBottle& bottle() const { return bottle_; }
    double waterLoopK() const { return waterK_; }
    const ManifoldCharge& manifold() const { return manifold_; }
    void refill();

private:
    ChargeAirSetup setup_;
    double waterK_ = 293.0;
    NitrousBottle bottle_;
    bool nitrousOn_ = false;
    double nitrousKgS_ = 0.0;
    double nitrousNm_ = 0.0;
    double waterGps_ = 0.0;
    double torqueShare_ = 1.0;
    ManifoldCharge manifold_;
};

}  // namespace revforge
