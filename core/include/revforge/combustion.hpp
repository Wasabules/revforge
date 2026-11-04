#pragma once

#include <array>

#include "revforge/engine_config.hpp"

namespace revforge {

struct Powertrain;

// ── Les carburants : voir « La combustion » dans docs/modele.md ──

/** Ce que brûle le moteur : valeurs publiées, ou mélange calculé (E85). */
struct FuelProperties {
    double densityKgPerL;
    double lowerHeatingJPerG;    // pouvoir calorifique inférieur
    double co2GPerG;             // CO₂ par gramme brûlé : la teneur en carbone, rien d'autre
    double stoichiometricAfr;    // grammes d'air par gramme de carburant, à richesse 1
    double ron;                  // indice d'octane recherche ; 0 sur un diesel, qui s'allume par compression
    double mon;                  // indice d'octane moteur
    double vaporizationJPerG;    // chaleur latente de vaporisation, à 25 °C
    double molarMassGPerMol;     // de la vapeur : elle prend la place d'une part de l'air
    double boilingK;             // ébullition sous 1 atm : la tension de vapeur, par Clausius-Clapeyron
};

const FuelProperties& fuelProperties(FuelGrade grade);
const FuelProperties& dieselFuelProperties();
/** Le carburant de ce moteur : un diesel ignore `fuelGrade`, l'indice d'octane ne lui dit rien. */
const FuelProperties& fuelPropertiesOf(const EngineConfig& engine);
/** L'énergie qu'apporte un kilogramme d'air brûlé à la stœchiométrie (J/kg) : PCI / pouvoir comburivore. */
double energyPerAirJPerKg(const FuelProperties& fuel);
/**
 * Ce que la vaporisation retire à la charge (K) : `evaporatedShare` du carburant se vaporise en prenant sa chaleur à l'air,
 * jusqu'à saturation de sa vapeur à la température atteinte (Clausius-Clapeyron). Le reste la prend aux parois.
 */
double evaporationCoolingK(const FuelProperties& fuel, double lambda, double evaporatedShare, double airK, double pressurePa);
/**
 * Part du carburant qui se vaporise aux dépens de l'air avant la fermeture de l'admission (supposé). Intégration : le modèle ne la
 * lit plus, la part vient de l'alimentation (`mixtureAirShare`, injection.hpp) ; elle ne sert qu'au test de la formule.
 */
inline constexpr double kPortEvaporatedShare = 0.15;

// ── Le taux de compression ──

/** Le taux que suppose le moteur d'aujourd'hui : 10,5 atmosphérique, 9,5 suralimenté, 18 en diesel, celui du rotor sur un rotatif. */
float stockCompressionRatio(const EngineConfig& engine);
/** Le taux réel : celui d'origine à 0, sinon le réglage, borné à ce qui existe (un diesel sous 14 ne s'allume plus, un rotor sans creux). */
float compressionRatioOf(const EngineConfig& engine);
/** Rien de ce sujet n'a changé : le moteur garde exactement son chemin de calcul d'origine. */
bool combustionStock(const EngineConfig& engine);
/**
 * Le rendement indiqué selon le taux : cycle air-carburant (Otto, ou Diesel à 2 de rapport d'injection), à γ = 1,3, moins
 * la chaleur perdue aux parois, qui croît avec la surface de la chambre au point mort haut. `boreOverStroke` donne cette surface.
 */
double indicatedEfficiency(double ratio, double boreOverStroke, bool diesel);
/** Le rendement à ce taux, rapporté à celui d'origine : 1 au taux d'origine. Sur un rotatif, `rotaryEfficiencyShare` à ce taux. */
double compressionEfficiencyShare(const EngineConfig& engine);
/**
 * Ce que le carburant change au couple à pleine charge, face à l'essence de référence : l'énergie par kilogramme d'air, et la densité de
 * la charge une fois sa vaporisation faite (plus froide, mais sa vapeur prend la place d'un peu d'air).
 */
double fuelTorqueShare(const EngineConfig& engine, double lambda, double airK);
/** Le produit des deux, pour `fullLoadTorqueNm` : il ne s'applique jamais à un moteur `combustionStock`. */
double combustionTorqueShare(const EngineConfig& engine);
/** La pression à l'ouverture de l'échappement, rapportée à celle d'origine (cycle d'Otto) : un taux plus haut a plus détendu le gaz. */
double releasePressureShare(double ratio, double stockRatio);

// ── Le diesel : ni étincelle, ni indice d'octane, ni cliquetis au sens de l'essence ──

/**
 * Le délai d'auto-inflammation (degrés de vilebrequin) de Hardenberg et Hase (1979), au point mort haut d'une compression polytropique
 * depuis `intakeK` et `intakePa`. `cetane` : indice de cétane (EN 590 : 51 au moins).
 */
double dieselIgnitionDelayDeg(double ratio, double intakeK, double intakePa, double rpm, double strokeM, double cetane = 51.0);
/** Le claquement d'un diesel suit la part de carburant qui brûle d'un coup, donc le délai : rapport à celui d'origine, au ralenti. */
float dieselKnockScale(const EngineConfig& engine);

// ── Le cliquetis ──

/** Délai d'auto-inflammation des gaz frais (s), Douaud et Eyzat (1978) : τ = 17,68 (ON/100)^3,402 p^-1,7 exp(3800/T) ms, p en atm. */
double douaudEyzatDelayS(double octane, double pressurePa, double temperatureK);
/**
 * Une combustion, vue de la chambre : la fermeture de l'admission, la compression, la loi de Wiebe. Tout ce que lisent les autres
 * sujets y entre en paramètre : pression et température au collecteur (air de charge), eau (thermique), fermeture de l'admission et gaz
 * résiduels (came), refroidissement par l'injection directe ou l'eau.
 */
struct EndGasSetup {
    double boreOverStroke = 1.0;
    double compressionRatio = 10.5;
    double rodRatio = 3.2;         // bielle sur rayon de manivelle ; 0 : loi en cosinus pur (rotatif)
    double ivcAbdcDeg = 50.0;      // fermeture de l'admission après le point mort bas (degrés de cycle)
    double manifoldPa = 101325.0;
    double manifoldK = 293.15;
    double coolantK = 363.15;
    double residualShare = 0.04;   // gaz résiduels à pleine charge (supposé)
    double fillShare = 0.92;       // remplissage, rapporté à la densité du collecteur
    double extraCoolingK = 0.0;    // injection directe, eau : ce qui s'ajoute à la vaporisation
    double lambda = 0.88;
    double octane = 95.0;
    double energyPerAir = 2.918e6;  // J/kg d'air
    double vaporizationCoolingK = 0.0;
    double rpm = 3000.0;
    double cycleSpeedShare = 1.0;  // degrés de cycle par degré d'arbre : 2/3 sur un rotatif
    // La chambre en vrai (m) : l'aire des parois et la vitesse des gaz des pertes de Woschni. Un rotatif : un cylindre carré équivalent.
    double boreM = 0.086;
    double strokeM = 0.086;
};

/** Ce que donne une combustion à cette avance (degrés de cycle avant le point mort haut). */
struct EndGasCycle {
    double knockIntegral = 0.0;  // Livengood et Wu, jusqu'à 90 % brûlé
    double workShare = 0.0;      // travail indiqué, compression et détente, en part de la chaleur du carburant
    double peakPa = 0.0;
    double gasK = 0.0;           // température moyenne du gaz à 90 % brûlé : la vitesse du son de la résonance
    double ivcPa = 0.0;
    double ivcK = 0.0;
    double wallShare = 0.0;      // chaleur perdue aux parois (Woschni), en part de la chaleur du carburant
    double ca50Deg = 0.0;        // la moitié du mélange brûlée, degrés de cycle après le point mort haut
};

/**
 * Le cycle, de la fermeture de l'admission à l'ouverture de l'échappement. Les parois prennent leur chaleur pas à pas (Woschni) :
 * c'est ce qui place le meilleur couple, et ce que coûte une étincelle qui s'en écarte. Voir « Le cliquetis » dans docs/modele.md.
 */
EndGasCycle endGasCycle(const EndGasSetup& setup, double advanceDeg);
/** Dispersion d'un cycle à l'autre de la phase de combustion (degrés) : un cycle sur cinquante cliquette à la limite (supposé). */
inline constexpr double kPhaseSpreadDeg = 1.5;
/** Écart entre la limite du cycle moyen et la limite de cliquetis : deux écarts types. */
inline constexpr double kLimitSpreads = 2.0;
/** Ce que la carte d'origine garde sous la limite de cliquetis, aux conditions de référence (supposé). */
inline constexpr double kCalibrationMarginDeg = 2.0;
/** Part des cycles qui cliquettent quand le cycle moyen est à `excessDeg` au-delà de sa limite (0 sous trois écarts types). */
double knockingShare(double excessDeg);
/**
 * Fréquence de la résonance de la chambre au premier mode circonférentiel (Draper, 1938) : f = 1,841·c/(π·alésage), c du gaz
 * brûlé à `gasK`. Sur un rotatif, la chambre est une lame : son premier mode est en longueur, f = c/(2·L), L la corde entre deux sommets.
 */
double draperHz(double boreM, double gasK);
double burnedSoundMs(double gasK);

/** Ce que voit le son à chaque pas : où en est le cycle moyen de sa limite. */
struct KnockDrive {
    bool active = false;
    float excessDeg = -100.0f;   // avance appliquée moins limite du cycle moyen
    float spreadDeg = static_cast<float>(kPhaseSpreadDeg);
    float gasK = 2500.0f;
    float share = 0.0f;          // part des cycles qui cliquettent
};

/** Un point de la carte, à ce régime et à cette pression de charge. Avances en degrés d'arbre avant le point mort haut. */
struct KnockPoint {
    double mbtDeg = 0.0;         // le meilleur couple de ce moteur : le sommet du travail de son cycle
    double stockDeg = 0.0;       // carte d'origine : le meilleur couple, sauf 2° sous la limite de cliquetis du moteur d'origine
    double limitDeg = 0.0;       // limite de cliquetis de ce moteur, à ce carburant
    double meanLimitDeg = 0.0;   // limite du cycle moyen
    double gasK = 2500.0;
};

/**
 * Les limites de cliquetis d'un moteur à étincelle, tabulées par régime et par avance, une fois pour toutes par réglage : le modèle de
 * conduite les lit à chaque pas. La pression n'y est pas tabulée : à température égale, toute la trace de pression lui est proportionnelle
 * et l'intégrale de Livengood-Wu suit sa puissance 1,7.
 */
class KnockMap {
public:
    static constexpr int kRpmPoints = 10;
    static constexpr int kAdvancePoints = 15;

    /** Les valeurs de référence des autres sujets : air à 20 °C et 101 325 Pa, eau à 90 °C, came et carte d'origine. */
    struct Inputs {
        double ambientK = 293.15;
        double ambientPa = 101325.0;
        double coolantK = 363.15;
        double ivcAbdcDeg = 50.0;
        double residualShare = 0.04;
        double extraCoolingK = 0.0;
        double advanceOffsetDeg = 0.0;
    };

    void build(const Powertrain& powertrain, const Inputs& inputs);
    void build(const Powertrain& powertrain) { build(powertrain, Inputs{}); }

    bool valid() const { return valid_; }
    /** Pas de capteur sur un carburateur : rien ne retire l'avance quand il cliquette. */
    bool sensor() const { return sensor_; }
    double advanceOffsetDeg() const { return offset_; }
    /** `chargeShare` : la pression de charge, rapportée à la pleine charge stabilisée à ce régime. */
    KnockPoint at(double rpm, double chargeShare) const;
    /**
     * Le même point, l'eau du moteur à `coolantK` (thermique) : une paroi plus chaude réchauffe la charge, et la limite recule.
     * La carte d'origine, elle, reste celle du moteur chaud. Exactement `at(rpm, chargeShare)` à l'eau de la carte.
     */
    KnockPoint at(double rpm, double chargeShare, double coolantK) const;
    /** L'eau à laquelle la carte est tabulée (K) : 363,15 K, moteur chaud. */
    double coolantK() const { return coolantK_; }
    /** La pression de charge à pleine charge stabilisée, en part de `boostBar` (0 sans suralimentation). */
    double fullBoost(double rpm) const;
    /** Le couple indiqué à l'avance `advanceDeg`, rapporté à celui à `referenceDeg` (degrés d'arbre). */
    double torqueShare(double rpm, double advanceDeg, double referenceDeg) const;
    /** À pleine charge stabilisée : l'avance que le capteur laisse, et le couple qui en reste face à la carte d'origine. */
    double steadyAdvanceDeg(double rpm) const;
    double steadyTorqueShare(double rpm) const;

private:
    struct Row {
        double rpm = 0.0;
        double fullBoost = 0.0;
        std::array<double, kAdvancePoints> logIntegral{};
        std::array<double, kAdvancePoints> work{};
        double gasK = 2500.0;
        double mbtDeg = 0.0;     // degrés de cycle : le sommet de `work`, tel que le lit `rowWork`
    };
    double meanLimit(const std::array<Row, kRpmPoints>& rows, double rpm, double chargeShare) const;
    static double invert(const Row& row, double target);
    double rowWork(const Row& row, double cycleDeg) const;
    static double rowBest(const Row& row);
    double rowsAt(const std::array<Row, kRpmPoints>& rows, double rpm, double Row::*field) const;

    std::array<Row, kRpmPoints> actual_{};
    std::array<Row, kRpmPoints> stock_{};
    // Les mêmes cycles, l'eau à `kColdTableK` : l'intégrale de cliquetis se lit entre les deux, linéaire en température d'eau.
    std::array<Row, kRpmPoints> cold_{};
    double coolantK_ = 363.15;
    double degPerCycle_ = 1.0;
    double idleRpm_ = 800.0;
    double redlineRpm_ = 7000.0;
    double offset_ = 0.0;
    bool sensor_ = true;
    bool valid_ = false;
};

/**
 * La carte de ce moteur, gardée tant qu'il ne change pas. `nullptr` quand rien de ce sujet n'a changé, ou sur un diesel ou un électrique :
 * la carte d'origine reste alors sous sa limite, et le calcul garde son chemin d'origine.
 */
/** Vrai quand l'air, la came, l'avance, l'injection et l'air de charge sont ceux d'origine : la carte n'a rien à voir de plus. */
bool knockConditionsStock(const Powertrain& powertrain);
/** Ce que lit la carte : l'air du jour, la fermeture de l'admission, la vaporisation d'une injection directe, l'avance. */
KnockMap::Inputs knockInputsOf(const Powertrain& powertrain);
const KnockMap* knockMapOf(const Powertrain& powertrain);

}  // namespace revforge
