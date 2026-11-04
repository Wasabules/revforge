#pragma once

#include <cstdint>

#include "revforge/engine_config.hpp"

namespace revforge {

struct Powertrain;

/**
 * Démarrer un moteur froid : la batterie au plomb, le démarreur qu'elle alimente, l'huile de chaque époque, puis ce qui fait
 * qu'une explosion a lieu ou non au lancement (vapeur d'essence dans ses limites, auto-inflammation d'un diesel). Voir « Le
 * démarreur, la batterie et la prise à froid » dans docs/modele.md.
 *
 * Des fonctions pures, qui reçoivent les températures d'eau, d'huile et de batterie en paramètres, et un état (`StartModel`) que
 * `DrivingModel` fait avancer au pas de 1 ms. Pas d'allocation, et pas de bibliothèque mathématique en dehors de `sqrt` : ce
 * qui sert au modèle de conduite doit donner le même résultat partout.
 *
 * Le démarrage d'origine (`DrivingModel::crank`, moteur chaud) reste la référence au bit près : la batterie n'agit que par
 * l'écart de son courant à celui de la batterie de référence au même régime, et un moteur dont chaque cycle allume à coup sûr
 * prend à la première explosion, comme à l'origine.
 */

// ── L'huile ──

/**
 * Au-dessus de cette huile (°C), le moteur est chaud : ses frottements de lancement sont ceux du moteur qui tourne, et il se
 * synchronise en secondes, comme le démarrage d'origine. Au-dessous, sa viscosité agit linéairement au lancement.
 */
inline constexpr double kCrankingJoinC = 80.0;
/** Viscosité dynamique (Pa·s) d'une huile de ce grade : loi de Vogel ajustée sur KV40, KV100 et la limite CCS de la SAE J300. */
double oilViscosityPaS(OilGrade grade, double oilC);
/**
 * Ce que l'huile ajoute aux frottements hydrodynamiques du moteur qui tourne, en part de leur valeur à 100 °C : la loi en
 * racine de la thermique (`coldViscosityExcess`). Au-dessus de 80 °C, celle de la référence (5W-30), sur laquelle le moteur
 * chaud est calé ; au-dessous, la forme de la courbe de ce grade, raccordée à 80 °C.
 */
double runningViscosityExcess(OilGrade grade, double oilC);
/**
 * La même chose au lancement : au-dessous de 80 °C, la viscosité agit linéairement (Petroff). À 100 tr/min, le film n'a pas le
 * temps de chauffer, et la viscosité CCS est mesurée au cisaillement d'un lancement (ASTM D5293). Au-dessus, celle du moteur
 * qui tourne : un démarrage moteur chaud ne change pas.
 */
double crankingViscosityExcess(OilGrade grade, double oilC);
/** L'huile qu'un moteur avait de son temps : le préréglage le dit, sinon la référence. */
const char* oilGradeName(OilGrade grade);

// ── La batterie au plomb ──

/**
 * Une batterie de 12 V, dimensionnée sur le moteur : son courant de démarrage à froid (EN 50342-1, à −18 °C) et sa capacité.
 * La résistance et la charge disponible à fort débit sont calées sur l'essai EN : 7,5 V après 10 s au courant nominal, puis
 * 6 V au bout de 73 s à 60 % de ce courant.
 */
struct BatterySpec {
    double coldCrankingA = 450.0;
    double capacityAh = 50.0;
    double ohmsAtMinus18 = 0.0114;   // résistance interne à −18 °C, pleine charge
    double highRateAs = 13000.0;     // charge disponible à fort débit à −18 °C, avant le coude de la courbe (A·s)
};
BatterySpec batterySpecOf(const EngineConfig& engine);

/** Tension à vide (V) selon l'état de charge (0 à 1) : 12,7 V pleine, 11,8 V vide. */
double batteryOpenVolts(double charge);
/** Résistance interne (Ω) à cette température et cette charge. Gelée, la batterie ne débite presque plus. */
double batteryOhms(const BatterySpec& spec, double celsius, double charge);
/** Charge disponible à fort débit (A·s) à cette température et cette charge. */
double batteryHighRateAs(const BatterySpec& spec, double celsius, double charge);
/** Température sous laquelle l'électrolyte gèle (°C), selon la charge. */
double electrolyteFreezeC(double charge);
/**
 * L'air autour de la batterie, et la batterie elle-même une fois à l'équilibre : une nuit dehors, l'air ambiant ; moteur chaud,
 * l'air du compartiment. Entre les deux, à la part de chemin que l'eau a faite (`coolantC`). Une batterie hors du compartiment
 * (moteur central ou arrière) reste à l'air ambiant. Le réseau thermique en fait suivre la batterie avec son inertie
 * (`ThermalNetwork::batteryC`) : c'est elle que lit le démarreur.
 */
double batteryStartC(double ambientC, double coolantC, EnginePlacement placement);
/**
 * La batterie comme masse thermique, tirée de sa taille : sa capacité (J/K) et sa conductance vers l'air qui l'entoure (W/K).
 * Une Varta Blue Dynamic D24 (60 Ah, 540 A) pèse 14,7 kg pour 242 × 175 × 190 mm ; un plomb-acide garde 896 J/(kg·K) (la valeur
 * de la littérature que cite le NREL, qui en mesure 872 sur un module Optima) ; ses cinq faces hors du bac rendent leur chaleur
 * à l'air calme du compartiment au coefficient du réseau thermique (`kStillAirWm2K`), à l'échelle de la masse puissance 2/3.
 * La citadine (52 Ah) : 12,6 kg, 11,3 kJ/K, 1,44 W/K, une constante de temps de 2,2 h, celle de son eau moteur coupé. Le
 * réseau thermique l'intègre (`ThermalNetwork::batteryC`), voir « Quand les réglages se rencontrent (3) » dans docs/modele.md.
 */
struct BatteryThermal {
    double heatJK = 11000.0;
    double airWK = 1.4;
};
BatteryThermal batteryThermalOf(const BatterySpec& spec);
/** Le démarreur, vissé au carter d'embrayage : à mi-chemin entre l'air et l'eau. */
double starterWindingC(double ambientC, double coolantC);

/** Ce qu'il reste dans la batterie : charge, acide appauvri dans les plaques, température. */
struct BatteryState {
    double charge = 1.0;     // état de charge, 0 à 1
    double drawnAs = 0.0;    // charge tirée à fort débit, que l'acide du bac n'a pas encore remplacée (A·s)
    double celsius = 20.0;
};

/** Tension aux bornes (V) à ce courant (A, positif en décharge). */
double batteryVolts(const BatterySpec& spec, const BatteryState& state, double amps);
/** Un pas : la charge tirée, puis l'acide qui revient dans les plaques. `amps` négatif : l'alternateur recharge. */
void stepBattery(const BatterySpec& spec, BatteryState& state, double amps, double dt);
/** Le courant que l'alternateur rend (A), moteur tournant : ce que la batterie accepte à sa température, sous 14,2 V. */
double alternatorChargeA(const BatterySpec& spec, const BatteryState& state);

// ── Le démarreur ──

/**
 * Le circuit du démarreur : la batterie, les câbles, l'induit et ses balais. La droite couple-vitesse d'origine est celle du
 * démarreur sur sa batterie de référence : force électromotrice de 12,7 V, résistance totale E²/(4P), qui donne sa puissance P.
 */
struct StarterCircuit {
    double emfRef = 12.7;
    double ohmsRef = 0.03;        // résistance totale de référence
    double motorOhmsRef = 0.02;   // induit et balais, à leur température de référence
    double cableOhms = 0.0015;
    double motorRefC = 55.0;
    double pinionFreeOmega = 400.0;  // rad/s au pignon, à vide, sous emfRef
    double newtonMetresPerAmp = 0.03;  // au pignon
};
StarterCircuit starterCircuitOf(const StarterSpec& starter, const BatterySpec& battery, double batteryRefC, double motorRefC);
/** Le courant du démarreur (A), pignon à `pinionOmega`, sur cette batterie, induit à `motorC`. */
double starterAmps(const StarterCircuit& circuit, const BatterySpec& spec, const BatteryState& state, double motorC, double pinionOmega);

// ── Ce qui se passe dans le cylindre au lancement ──

/** La fin de compression d'un lancement : pression (bar) et température (K) au point mort haut, exposant polytropique équivalent. */
struct CrankCompression {
    double tdcBar = 0.0;
    double tdcK = 0.0;
    double polytropic = 1.35;
};
/**
 * Intégrée de la fermeture de l'admission au point mort haut : pertes aux parois par la corrélation de Hohenberg, qui garde un
 * échange même piston presque immobile, et fuites aux segments, calées sur un essai de compression (14 bar en essence, 31 bar
 * en diesel à 250 tr/min, moteur chaud).
 */
CrankCompression crankCompression(double rpm, double ratio, double boreMm, double strokeMm, double intakeC, double wallC, double intakeBar, double ivcAbdcDeg);

/**
 * La part du carburant reçu qui se vaporise dans l'air du cylindre, à l'équilibre, à la température de la paroi : une essence
 * d'hiver en neuf corps purs (Raoult, Antoine), ou l'E85 et le méthanol avec leur alcool. `phiDelivered` : carburant reçu,
 * en part de la stœchiométrie de l'air enfermé.
 */
double vaporizedShare(FuelGrade grade, double phiDelivered, double wallC, double pressureBar);
/** Le carburant que le calculateur (ou le starter) fait arriver par cycle, en part de la stœchiométrie, pour viser la vapeur `phiTarget`. */
double startFuelShare(FuelGrade grade, double coolantC, double pressureBar, double phiTarget);

/** Limites d'allumage d'un moteur au lancement, en richesse de la vapeur : 0,6 (Shin et al., 1997) et 3 (vapeur légère, Zabetakis). */
inline constexpr double kLeanFiringPhi = 0.6;
inline constexpr double kRichFiringPhi = 3.0;
/** Dispersion du mélange à la bougie d'un cycle à l'autre (écart type relatif), au lancement : supposé. */
inline constexpr double kStartMixtureSpread = 0.15;
/** La part des cycles qui allument, à cette vapeur, dispersion comprise (loi bornée : somme de trois tirages uniformes). */
double petrolFiringChance(double phiVapor);

/** Délai d'auto-inflammation d'un diesel au lancement (°V) : Hardenberg et Hase, à la fin de compression, ou près de la bougie de préchauffage. */
double crankIgnitionDelayDeg(const CrankCompression& tdc, double meanPistonMs, double glowLocalK);
/** Fenêtre au-delà de laquelle le gaz, détendu, ne s'allume plus (°V, supposé). */
inline constexpr double kDieselWindowDeg = 30.0;
/** La part des injections qui s'allument dans la fenêtre, dispersion du délai comprise. */
double dieselFiringChance(double delayDeg);
/** Préchauffage, selon l'eau (s) : le temps que la bougie atteigne 850 °C. 0 au-dessus de 20 °C d'eau. */
double glowPreheatS(double coolantC);
/** La bougie après `seconds` d'alimentation, partie de `startC` (°C). */
double glowPlugC(double startC, double seconds);

/** Ce que le son doit savoir du lancement : la part des explosions qui ont lieu, et la charge de celles qui ont lieu. */
struct StartDrive {
    bool active = false;
    float firing = 0.0f;
    float fill = 0.0f;
};

/** Ce que voit le lancement à ce pas. */
struct StartConditions {
    double ambientC = 20.0;
    double coolantC = 90.0;
    double oilC = 100.0;
    double intakeC = 20.0;
    double ambientBar = 1.01325;
    /** La batterie, du réseau thermique (`ThermalNetwork::batteryC`) : la référence, moteur chaud par 20 °C, au compartiment. */
    double batteryC = 32.0;
};

/** Le relevé d'un démarrage, pour la ligne de commande et les essais. */
struct StartStats {
    int attempts = 0;
    bool caught = false;
    double caughtAfterS = 0.0;    // de la clé à la prise, essai réussi
    double crankedS = 0.0;        // temps de démarreur, tous essais
    double meanCrankRpm = 0.0;    // régime moyen pignon engagé, avant toute explosion
    double minVolts = 99.0;
    double meanAmps = 0.0;
    int firings = 0;
    int misfires = 0;
    double drawnAh = 0.0;
};

/**
 * L'état du lancement : la batterie réelle et celle de référence, le film de carburant qui se construit, la bougie de
 * préchauffage, ce que chaque cylindre a fait à son dernier point mort haut, et les compteurs. Son propre générateur : les
 * tirages du lancement ne décalent rien d'autre.
 */
class StartModel {
public:
    void configure(const Powertrain& powertrain);
    /** La clé tourne : la batterie et le démarreur à leurs températures, la référence à neuf. */
    void beginAttempt(const StartConditions& c);
    /** Le démarreur s'arrête, le moteur ayant pris ou non. */
    void endAttempt(bool caught, double sinceKeyS);

    /** Un pas de démarreur : le courant au pignon, la batterie qui s'épuise. Renvoie l'écart de couple au pignon (Nm) à la référence. */
    double stepStarter(double pinionOmega, double dt, bool engaged);
    /** Moteur arrêté ou qui tourne : la batterie se repose, ou l'alternateur la recharge. */
    void stepRest(bool running, double dt);
    /**
     * Le moteur reste arrêté `seconds` secondes sans que le modèle avance (le son suspendu, sur le site) : l'acide revient dans
     * les plaques, comme `stepRest` moteur arrêté l'aurait fait pas à pas.
     */
    void rest(double seconds);
    /** Un modèle reconstruit pour la même voiture (changer de mode sur le site) reprend la batterie de l'ancien. */
    void restoreBattery(const BatteryState& state);

    /**
     * À la synchronisation : le calculateur commence à injecter. Vrai si chaque cycle allume à coup sûr, dispersion comprise : le
     * moteur prend alors à la première explosion, comme à l'origine. `cyclesPerS` : cycles par seconde et par cylindre.
     */
    bool beginFiring(const StartConditions& c, double rpm, double cyclesPerS);
    /** Un cylindre arrive au point mort haut : explose-t-il ? Tire trois nombres à chaque appel, de son propre générateur. */
    bool fire(const StartConditions& c, double rpm);
    /** La force de la dernière explosion : son couple en part de celui du meilleur mélange (0 : ratée). */
    double lastStrength() const { return lastStrength_; }
    /** La chaleur que la dernière explosion dégage dans le cylindre (J) : ce qu'elle rend au vilebrequin en découle. */
    double lastHeatJ() const;
    /** La part des cycles qui allument en ce moment, sans tirage : pour le son et la reprise. */
    double firingChance() const { return chance_; }
    /** La même, recalculée à ces conditions, sans tirage : le moteur qui vient de prendre. */
    double refreshChance(const StartConditions& c, double rpm);
    /** Le film suit le carburant qui arrive, pas de temps `dt`, à `cyclesPerS` cycles par seconde et par cylindre, paroi à `wallC`. */
    void stepFilm(double dt, double cyclesPerS, double throttle, double wallC);
    /** La vitesse libre du pignon sur cette batterie (rad/s) : au-delà, le démarreur n'entraîne plus, le moteur a pris. */
    double pinionFreeOmega() const;

    double batteryVolts() const { return volts_; }
    double starterAmps() const { return amps_; }
    /** Écart du courant à celui de la batterie de référence, rapporté au courant de calage de référence : ce que reçoit le son. */
    double supplyShare() const { return supplyShare_; }
    const BatteryState& battery() const { return battery_; }
    void setBatteryCharge(double charge) { battery_.charge = charge; }
    const BatterySpec& batterySpec() const { return spec_; }
    const StarterCircuit& circuit() const { return circuit_; }
    double glowC() const { return glowC_; }
    /** Le régime d'entraînement moyen avant la première explosion (tr/min). */
    double crankRpm() const { return crankSteps_ > 0.0 ? crankRpmSum_ / crankSteps_ : 1e9; }
    double preheatS() const { return preheatS_; }
    const StartStats& stats() const { return stats_; }
    StartDrive drive(bool cranking) const;
    OilGrade oil() const { return oil_; }

private:
    double uniform();
    double phiVapor(const StartConditions& c) const;
    void filmOf(double wallC, double& deposit, double& tau, double& knownDeposit, double& knownTau) const;
    double injectedGps(double wanted, double knownDeposit, double knownTau) const;

    BatterySpec spec_;
    StarterCircuit circuit_;
    BatteryState battery_;
    BatteryState reference_;
    double referenceMotorC_ = 55.0;
    double motorC_ = 55.0;
    EnginePlacement placement_ = EnginePlacement::Front;
    OilGrade oil_ = OilGrade::Sae5W30;
    FuelGrade grade_ = FuelGrade::Ron95;
    FuelSystem system_ = FuelSystem::Injection;
    bool diesel_ = false;
    bool glow_ = false;
    double ratio_ = 10.0;
    double boreMm_ = 80.0;
    double strokeMm_ = 80.0;
    double ivcDeg_ = 50.0;
    double redline_ = 6000.0;
    double wallShare_ = 0.5;
    double pumpG_ = 0.0;
    double cylinderAirG_ = 0.5;
    double stoich_ = 14.7;
    // Le film et ce que le calculateur croit du film (injection indirecte), en grammes par cylindre.
    double filmG_ = 0.0;
    double believedG_ = 0.0;
    double startFuel_ = 1.0;
    double deliveredPhi_ = 0.0;
    double lastThrottle_ = 0.0;
    double chance_ = 1.0;
    double lastStrength_ = 0.0;
    double tdcK_ = 600.0;
    double ringRatio_ = 12.0;
    double volts_ = 12.7;
    double amps_ = 0.0;
    double supplyShare_ = 0.0;
    double glowC_ = 20.0;
    double preheatS_ = 0.0;
    double glowOnS_ = 0.0;
    double sinceKeyS_ = 0.0;
    bool fired_ = false;
    double crankRpmSum_ = 0.0;
    double crankSteps_ = 0.0;
    double ampSum_ = 0.0;
    double engagedSteps_ = 0.0;
    std::uint32_t rng_ = 0x5EED1Du;
    StartStats stats_;
};

}  // namespace revforge
