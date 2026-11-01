#pragma once

namespace revforge {

/**
 * Les trajets du pilote automatique. L'ordre compte : la position est la valeur envoyée par le site
 * (`DRIVE_SCENARIOS` dans web/src/lib/config.ts). City, le premier, est la boucle d'origine, au bit près.
 */
enum class DriveScenario { City, StopAndGo, Country, Motorway, Mountain, Drag, Track, HillStart, Towing };

inline constexpr int kDriveScenarios = 9;

/** De quoi nommer l'étape en cours. L'ordre compte : voir `LEG_KINDS` dans web/src/lib/config.ts. */
enum class LegKind {
    Stopped,
    PullAway,
    CityPace,
    TrafficLight,
    Expressway,
    Cruise,
    Overtake,
    Stopping,
    Queue,
    Straight,
    Bend,
    Climb,
    Hairpin,
    Summit,
    Descent,
    Launch,
    HeavyBraking,
    Corner,
    FlatOut,
    HillStart,
    SlipRoad,
    Exit,
};

/** Une consigne si haute que seule la voiture fixe la limite : on ne dira pas qu'elle n'atteint pas 400 km/h. */
inline constexpr double kFlatOutKmh = 400.0;

/** Une étape : à partir de quand, quelle vitesse, quel pied, quelle route. */
struct ScenarioLeg {
    double fromSec;
    double targetKmh;
    double eagerness;      // pédale au plus, hors de ce qu'il faut pour tenir la vitesse
    double gradePct;       // pente : positive en montée
    double cornerRadiusM;  // 0 : tout droit. Sinon, la vitesse est limitée par l'accélération latérale du conducteur.
    LegKind kind;
};

/** La façon de conduire : la même d'un bout à l'autre du trajet. */
struct DriverStyle {
    double pedalUpPerS;
    double pedalDownPerS;
    double throttlePerKmh;   // pédale par km/h d'écart à la consigne
    double brakeMarginKmh;   // pas de frein tant qu'on ne dépasse pas la consigne d'autant
    double brakePerKmh;
    double brakeMax;
    double stopBrake;        // freins tenus pour finir de s'arrêter, sous stopBelowKmh
    double stopBelowKmh;
    double anticipateDecel;  // décélération visée pour arriver à la vitesse de l'étape suivante (m/s²) ; 0 : réagit, n'anticipe pas
    double lateralG;         // accélération latérale acceptée en virage (g)
};

struct Scenario {
    const char* id;          // pour la ligne de commande
    const ScenarioLeg* legs;
    int legCount;
    double loopSec;
    double loopBackSec;      // la boucle reprend ici : ce qui précède (le départ arrêté) n'a lieu qu'une fois
    double trailerKg;
    DriverStyle style;
};

const Scenario& scenarioOf(DriveScenario scenario);

/** Où on en est du trajet après tant de secondes, boucle comprise. */
double scenarioCycle(const Scenario& scenario, double seconds);
/** L'étape en cours à ce moment du trajet. */
int scenarioLegAt(const Scenario& scenario, double cycleSec);
/** La vitesse visée dans cette étape : la consigne, limitée par le virage. */
double legTargetKmh(const Scenario& scenario, const ScenarioLeg& leg);

/** Ce que le conducteur a sous les yeux. */
struct DriverView {
    double kmh = 0.0;
    double holdPedal = 0.0;  // ce qu'il faut de pédale pour tenir cette vitesse
    double cycleSec = 0.0;
    bool lifting = false;    // le passage en cours demande de lever le pied
};

struct DriverOrder {
    double pedal = 0.0;
    double brake = 0.0;
    double targetKmh = 0.0;
};

/** Pédales pour cette étape. Regarde la suivante : freiner à temps pour y arriver à la bonne vitesse. */
DriverOrder followLeg(const Scenario& scenario, int legIndex, const DriverView& view);

}  // namespace revforge
