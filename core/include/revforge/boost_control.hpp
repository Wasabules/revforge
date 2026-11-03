#pragma once

#include <array>

#include "revforge/engine_config.hpp"
#include "revforge/gearbox.hpp"
#include "revforge/turbo.hpp"

namespace revforge {

struct Powertrain;
struct Gearing;
class TwinTurbo;

/**
 * La régulation de la pression d'un turbo (docs/modele.md, « La régulation de pression »).
 *
 * `Spring` : la wastegate d'origine du modèle, dans `TurboCharger` : une membrane, un ressort taré à la pression demandée,
 * au-dessus de l'atmosphère. Rien ici ne la change.
 *
 * `Electronic` : une électrovanne découpée à rapport cyclique règle le vide d'une membrane ouverte au repos ; un PID en
 * temps discret, avec précommande et anti-emballement de l'intégrale, la pilote vers une pression *absolue* selon le régime.
 *
 * `ByGear` : la même, avec une cible bornée sur chaque rapport par le couple que les pneus et la boîte peuvent passer.
 *
 * Tout ce qui suit est déterministe et sans fonction de la bibliothèque mathématique autre que `sqrt` : il tourne dans
 * `DrivingModel`, à chaque pas de 1 ms.
 */

/** L'électrovanne et la membrane (ordres de grandeur supposés : voir docs/modele.md). */
struct BoostActuatorSpec {
    double pwmHz = 25.0;       // fréquence de découpage de l'électrovanne
    double openS = 0.005;      // temps de réponse de l'électrovanne à l'ouverture…
    double closeS = 0.003;     // …et à la fermeture
    double chamberS = 0.04;    // la membrane et son tuyau : constante de temps de leur pression
    double taskS = 0.01;       // période de la tâche du calculateur
};

/** Le modèle linéarisé de l'arbre, mesuré au banc : δp = −K·e^(−θs)/(τs + 1)·δg, g l'ouverture de la wastegate. */
struct BoostPlant {
    double rpm = 0.0;
    double gainPa = 0.0;  // K : Pa de pression de charge perdus par unité d'ouverture
    double tauS = 0.5;    // τ : la constante de temps de l'arbre
    double deadS = 0.05;  // θ : ce qui ressemble à un retard (la membrane, le volume de charge)
};

/** Un PID sous forme idéale, sur l'écart de pression en Pa : u = Kc·(e + 1/τI ∫e + τD·de/dt). */
struct BoostPid {
    double kc = 0.0;      // rapport cyclique par Pa
    double tauI = 1.0;    // s
    double tauD = 0.0;    // s
};

inline constexpr int kBoostGrid = 24;

/** Tout ce que la régulation a calculé une fois pour ce moteur et cette voiture. */
struct BoostControlSpec {
    BoostControl kind = BoostControl::Spring;
    double boostBar = 1.0;           // la pression du préréglage, au-dessus de l'atmosphère de référence
    double referencePa = 101325.0;   // l'atmosphère où le moteur a été calibré
    double calibratedPa = 201325.0;  // la cible absolue de la calibration : référence + boostBar
    // La grille en régime : la cible, la précommande et les gains y sont tabulés, puis interpolés.
    double firstRpm = 1000.0;
    double stepRpm = 250.0;
    std::array<double, kBoostGrid> protectPa{};             // la pression où les roues atteindraient leur vitesse limite (abs.)
    std::array<double, kBoostGrid> gateAtCalibrated{};      // ouverture à l'équilibre, pleine charge, à la cible calibrée…
    std::array<double, kBoostGrid> gateAtHalf{};            // …et à mi-pression : la précommande les interpole
    std::array<BoostPid, kBoostGrid> pid{};
    std::array<std::array<double, kBoostGrid>, kMaxGears> gearCapPa{};  // `ByGear` : le plafond de chaque rapport (abs.)
    int gears = 0;
    BoostActuatorSpec actuator;
    std::array<BoostPlant, 4> plants{};  // le banc : où et comment l'arbre a répondu
    int plantCount = 0;
    /** Les essais seulement : sans anti-emballement, l'intégrale grossit tant que l'électrovanne est en butée. */
    bool antiWindup = true;
    /**
     * Deux turbos en parallèle ou en séquence, ou des aubes : une électrovanne commande les deux soupapes (ou les aubes) à la fois,
     * et tout ce qui précède a été mesuré sur le banc de ce montage (`TwinTurbo`), pas sur celui d'un turbo seul. Faux sur un
     * twincharger : son volet et l'embrayage de son compresseur règlent la pression, le calculateur ne donne que la cible.
     */
    bool twin = false;
};

/** Ce que le calculateur sait à ce pas. */
struct BoostControlInput {
    double rpm = 0.0;
    int gear = 0;                 // le rapport engagé : 0 au point mort, qui prend la cible de la première
    double chargePa = 101325.0;   // la pression de charge mesurée, absolue
    double ambientPa = 101325.0;  // l'air ambiant : à brancher sur l'atmosphère
    /**
     * Le calculateur bascule un séquentiel : il a ouvert la vanne d'échappement du second turbo, dont le clapet ne débite pas
     * encore. Le creux qui suit n'est pas une erreur à corriger : il tient son rapport cyclique, et l'intégrale, jusqu'à ce
     * que le second débite (gestion des 2JZ-GTE et 959, qui commandent eux-mêmes l'étagement).
     */
    bool staging = false;
};

// ── Les fonctions pures ──

/**
 * La pression absolue demandée (Pa). Ressort : l'atmosphère plus la pression du ressort, puisque la membrane respire l'air
 * ambiant. Électronique : la cible absolue de la calibration, bornée par la vitesse des roues ; selon le rapport, aussi par
 * le couple que ce rapport peut passer.
 */
double boostTargetPa(const BoostControlSpec& spec, double rpm, int gear, double ambientPa);
/** La même, au-dessus de l'atmosphère (bar) : ce que `turboSteady` attend comme `targetBar`. */
double boostTargetGaugeBar(const BoostControlSpec& spec, double rpm, int gear, double ambientPa);

/**
 * L'ouverture que demande la wastegate à ressort (0 à 1, avant sa propre inertie et la protection des roues), même calcul que
 * `TurboCharger::step` : la membrane commence à s'ouvrir à 97,6 % de la pression du ressort, est grande ouverte 12 % plus
 * haut, et le modèle anticipe de 0,12 s la pression qui monte. `chargeRisePaS` : la pente de la pression de charge.
 */
double springGateDemand(double targetBar, double chargePa, double chargeRisePaS, double ambientPa);

/** Rapport cyclique réellement ouvert, compte tenu des temps de réponse de l'électrovanne : une zone morte à chaque bout. */
double effectiveDuty(const BoostActuatorSpec& actuator, double duty);
/** L'ouverture de la wastegate que donne le vide de la membrane à l'équilibre : ouverte au repos, fermée sous plein vide. */
double gateForVacuum(double vacuumShare);
/** Le rapport cyclique qui tient cette ouverture à l'équilibre : la précommande. */
double dutyForGate(const BoostActuatorSpec& actuator, double gate);

/**
 * Les gains, par la méthode SIMC de Skogestad (2003) sur le modèle mesuré : la dérivée compense la membrane (τD = τ du
 * tuyau), et la constante de boucle fermée vaut le retard effectif, ce qui donne environ 60° de marge de phase.
 */
BoostPid simcPid(const BoostPlant& plant, const BoostActuatorSpec& actuator);
/** Le retard effectif que voit le régulateur : celui de l'arbre, plus la moitié de la période de la tâche et du découpage. */
double boostLoopDelayS(const BoostPlant& plant, const BoostActuatorSpec& actuator);

/**
 * Le banc : l'arbre tenu à ce régime, plein gaz, wastegate fixée à `gate`, puis un échelon d'ouverture. Rend le gain et
 * les temps de la réponse (méthode des deux points de Smith, à 28 % et 63 %). `turbo` est réutilisé d'un régime à l'autre.
 */
BoostPlant identifyBoostPlant(TurboCharger& turbo, double rpm, double breathing, double gate, double settleS);
/** Le même banc, sur deux turbos (ou des aubes) : l'échelon s'applique aux deux soupapes à la fois, comme le fait l'électrovanne. */
BoostPlant identifyTwinBoostPlant(TwinTurbo& twin, double rpm, double breathing, double gate, double settleS);

/** Le plafond de couple du rapport `gear` (1 = première) au vilebrequin : pneus et boîte (Nm). */
double gearTorqueCapNm(const Powertrain& p, const Gearing& gearing, int gear, double tyreGrip);

/** Tout ce que la régulation calcule une fois : cibles, précommande, banc, gains. Rien à calculer sur un ressort. */
BoostControlSpec boostControlSpecFor(const Powertrain& p, const Gearing& gearing, double tyreGrip);

/** Interpole une table de la grille au régime voulu. */
double boostGridAt(const BoostControlSpec& spec, const std::array<double, kBoostGrid>& table, double rpm);

/**
 * Le calculateur et son électrovanne, pas à pas. `step` rend l'ouverture que la membrane demande à la wastegate : à donner à
 * `TurboInput::gateDemand`. Le PID tourne toutes les `taskS`, l'électrovanne prend le nouveau rapport cyclique au début de
 * chaque période de découpage, et la membrane suit avec son tuyau.
 */
class BoostController {
public:
    void reset();
    double step(const BoostControlSpec& spec, double dt, const BoostControlInput& input);

    double targetPa() const { return target_; }
    /** Le rapport cyclique que commande le calculateur, et celui que l'électrovanne tient en ce moment. */
    double duty() const { return command_; }
    double appliedDuty() const { return latched_; }
    double vacuum() const { return vacuum_; }
    double integral() const { return integral_; }

private:
    double target_ = 0.0;
    double command_ = 1.0;
    double latched_ = 1.0;
    double vacuum_ = 1.0;
    double integral_ = 0.0;
    double derivative_ = 0.0;
    double lastPa_ = 0.0;
    bool started_ = false;
    int taskTicks_ = 0;
    int pwmTicks_ = 0;
};

}  // namespace revforge
