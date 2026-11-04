#pragma once

#include <array>

#include "revforge/engine_config.hpp"
#include "revforge/gearbox.hpp"
#include "revforge/injection.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/turbo.hpp"

namespace revforge {

enum class Body { City, Compact, Coupe, Supercar, Van, Retro };
enum class Drive { Front, Rear, All };

/** La voiture autour du moteur : ce que règle l'utilisateur. */
struct CarConfig {
    Body body = Body::City;
    int massKg = 1100;
    int rimInch = 15;
    Drive drive = Drive::Front;
    EnginePlacement placement = EnginePlacement::Natural;
    Insulation insulation = Insulation::Standard;
    bool windowsOpen = false;
};

/** Ce qui découle de la carrosserie. */
struct BodySpec {
    double dragArea;      // Cx × surface frontale, m²
    double wheelbaseM;
    double cgHeightM;
    double frontShare;    // part du poids sur l'essieu avant
    float exhaustScale;   // longueur de ligne, par rapport à une compacte
    double shiftSeconds;  // durée d'un passage de rapport
    int gears;
    // Dimensions : les mêmes que SHAPES dans web/src/components/CarStage.svelte, pour le dessin.
    float lengthM;
    float widthM;
    float heightM;
    float rearAxleM;      // essieu arrière, depuis l'arrière de la voiture
    float outletHeightM;  // hauteur de la sortie d'échappement
    EnginePlacement naturalPlacement;
};

const BodySpec& bodySpec(Body body);

/** Où est le moteur, une fois le choix « selon la carrosserie » résolu. */
EnginePlacement placementOf(const CarConfig& car);
/** Part du poids sur l'essieu avant : la carrosserie, et où on a mis le moteur. */
double frontShareOf(const CarConfig& car);
/** Longueur de ligne par rapport à une compacte à moteur avant : le trajet du moteur à l'arrière de la voiture. */
float exhaustScaleOf(const CarConfig& car);

/** Tout ce qui fait avancer et sonner la voiture. */
struct Powertrain {
    EngineConfig engine;
    bool electric = false;
    CarConfig car;
    GearboxConfig gearbox;

    EngineProfile profile() const;
};

/** La référence des tests : une citadine 1.4 de 75 ch. */
Powertrain referencePowertrain();

// ── Le moteur ──

/** Pression du turbo à pleine charge et à ce régime, une fois l'arbre stabilisé : 0 à 1. Calculée, pas tabulée : voir `TurboCharger`. */
double steadyBoost(const Powertrain& p, double rpm);
/** Le turbo que ce moteur monte. */
TurboSpec turboSpecOf(const Powertrain& p);
/**
 * L'équilibre d'un turbo simple à pleine charge, à la pression que tient sa régulation dans l'air du jour : un ressort tient sa
 * pression au-dessus de l'atmosphère ; un calculateur (`electronic`, `by_gear`) la pression absolue de sa calibration, que les roues
 * bornent d'elles-mêmes. `boost` y est rapporté à la pression du réglage : plus de 1 en altitude, sous calculateur. Dans l'air de
 * référence, ou sur un ressort, c'est `turboSteady(turboSpecOf(p), …)` au bit près. La fiche, comme la conduite.
 */
TurboSteady fullLoadTurboSteady(const Powertrain& p, double rpm);
/** L'air à l'entrée du moteur ou du compresseur à pleine charge, derrière le filtre : sa température (K). */
double intakeAirK(const Powertrain& p, double rpm);
/** La perte de pression de ce filtre à pleine charge et à ce régime (Pa), boîte et prise d'air comprises. */
double intakeDropPa(const Powertrain& p, double rpm);
/** Le compresseur mécanique que ce moteur monte. */
SuperchargerSpec superchargerSpecOf(const Powertrain& p);
/** Ce qu'il prend au vilebrequin à ce régime et à ce remplissage, à l'équilibre (Nm). 0 sans compresseur. */
double superchargerDriveNm(const Powertrain& p, double rpm, double fill);
/** Respiration du moteur à ce régime : 1 au pic de couple. */
double breathingOf(const Powertrain& p, double rpm);
/** Couple au vilebrequin à pleine charge, net des frottements, pour une pression de turbo donnée (Nm). */
double fullLoadTorqueNm(const Powertrain& p, double rpm, double boost);
/** Courbe affichée : couple à pleine charge, turbo stabilisé (Nm). */
double torqueCurveNm(const Powertrain& p, double rpm);
/** Frottements et pompage (Nm, positif) : c'est le frein moteur. */
double frictionTorqueNm(const Powertrain& p, double rpm);
/** Pompage contre un papillon fermé (Nm) : ce qui ramène vite un moteur au ralenti. */
double pumpingTorqueNm(const Powertrain& p, double rpm, double manifold);
/** Inertie du vilebrequin et du volant moteur (kg·m²) : gros moteur, montée en régime plus lente. */
double engineInertia(const Powertrain& p);
double idleRpmOf(const Powertrain& p);
double redlineRpmOf(const Powertrain& p);

// ── L'injection, la recirculation et le filtre à particules : voir « L'injection » dans docs/modele.md ──

/** Ce qui sort du moteur à ce régime et à ce remplissage (0 à 1), pression de turbo donnée : débit, température, richesse. */
struct ExhaustState {
    double kgS = 0.0;
    double kelvin = 300.0;
    double fuelGps = 0.0;
    double lambda = 1.0;
};
ExhaustState exhaustStateOf(const Powertrain& p, double rpm, double fill, double boost);
/** Ce que prend la pompe haute pression d'une injection directe à ce remplissage (Nm). 0 sans injection directe. */
double directPumpTorqueNm(const Powertrain& p, double rpm, double fill, double boost);
/** Le pompage contre le filtre à particules, à cette charge de suie (Nm). 0 sans filtre. */
double filterPumpingTorqueNm(const Powertrain& p, double rpm, double fill, double boost, double sootG);
/** La suie d'un filtre en service, en moyenne : à mi-chemin entre deux régénérations (g). */
double meanSootG(const Powertrain& p);
/** Excès d'air sous lequel ce diesel fume, à pleine charge et à ce régime. */
double smokeLambdaOf(const Powertrain& p, double rpm);
/** Le point de fonctionnement d'un diesel, pour sa combustion : cylindre, régime, part de la pleine charge, pression du turbo. */
DieselPoint dieselPointOf(const Powertrain& p, double rpm, double load, double boost);

struct PowerSummary {
    double peakTorqueNm = 0.0;
    double peakTorqueRpm = 0.0;
    double peakPowerKw = 0.0;
    double peakPowerRpm = 0.0;
};

PowerSummary powerSummary(const Powertrain& p);

inline constexpr double kKwToHp = 1.35962;  // chevaux DIN

// ── La transmission ──

struct Gearing {
    int gears = 5;
    std::array<double, kMaxGears> overall{};  // démultiplication totale de chaque rapport, boîte et pont
    double reverseOverall = 0.0;
    GearboxSpec box;
};

/** Rayon de roulement selon la jante (m). */
double wheelRadiusM(const CarConfig& car);
/**
 * La boîte montée sur cette voiture. Sans pont choisi, il l'est pour que le dernier rapport atteigne la puissance
 * maximale à la vitesse de pointe ; avec un pont et des rapports choisis, rien ne dépend plus du moteur.
 */
Gearing gearingOf(const Powertrain& p);
/** Résistance à l'avancement à cette vitesse (N). */
double roadLoadN(const Powertrain& p, double speedMs);

struct Performance {
    double zeroTo100S = 0.0;  // 0 si jamais atteint
    double topSpeedKmh = 0.0;
};

/**
 * Mesuré en conduisant le modèle, comme sur une piste : le 0 à 100 km/h est le meilleur départ d'un essayeur, qui dose
 * lui-même la pédale sans antipatinage ni launch control ; la vitesse de pointe est `topSpeedKmh`.
 */
Performance measurePerformance(const Powertrain& p);
/** La vitesse de pointe, sans conduire : la plus haute où, sur un rapport et sous le régime maximal, le couple vainc l'air et le roulement. */
double topSpeedKmh(const Powertrain& p);

// ── L'air ambiant (atmosphere.hpp) ──

/** Ce montage dans l'air de référence : pour ce qui se dimensionne une fois pour toutes (boîte, embrayage, carte de passage). */
Powertrain withReferenceAir(const Powertrain& p);
/**
 * L'air piégé dans l'air du jour, rapporté à celui de l'air de référence, à pleine charge, à ce régime et à cette part de la
 * pression demandée. Exactement 1 dans l'air de référence et sur un moteur électrique. C'est ce qui change le couple indiqué.
 */
double ambientChargeShare(const Powertrain& p, double rpm, double boost);

}  // namespace revforge
