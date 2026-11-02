#pragma once

#include <array>

namespace revforge {

inline constexpr int kMaxCylinders = 12;

enum class Layout { Inline, V, CrossPlane, Boxer };
// L'ordre compte : la position donne la valeur envoyée par le site (web/src/lib/config.ts). On ajoute à la fin.
enum class Exhaust { Stock, Sport, Straight, MufflerDelete, ResonatorDelete, Quiet };
/** Le chemin des gaz : une seule ligne, deux lignes séparées (une par rangée), reliées par un pont en H ou en X. */
enum class ExhaustRouting { Single, Dual, HPipe, XPipe };
enum class Aspiration { Natural, Turbo, Supercharger };
enum class Fuel { Petrol, Diesel };
enum class PopLevel { Off, Light, Strong };
enum class TurboRelease { DumpValve, Flutter };
/** Petit : réveillé tôt, à bout de souffle en haut. Gros : long à lancer, de l'air jusqu'au rupteur. */
enum class TurboSize { Small, Medium, Large };
/** Le compresseur mécanique : à lobes, à vis, ou centrifuge. N'agit que sur `Aspiration::Supercharger`. */
enum class SuperchargerKind { Roots, TwinScrew, Centrifugal };
/** L'anti-lag : aucun, retard d'allumage, air injecté au collecteur, coupure de cylindres. N'agit que sur un turbo. */
enum class AntiLag { Off, Retard, AirInjection, CylinderCut };
/** L'arbre à cames : deux en tête, quatre soupapes par cylindre ; un seul en tête, deux soupapes. */
enum class Camshaft { Dohc, Sohc };
/** Le filtre à air : papier d'origine dans sa boîte, panneau sport dans la boîte, cône à l'air libre, cornets sans boîte. */
enum class AirFilter { Stock, Panel, Cone, Stacks };

/**
 * Ce qui dépend de l'échappement en dehors de sa géométrie, calculée par ExhaustLine :
 * ce que deviennent les détonations à la sortie.
 */
struct ExhaustSpec {
    float pipeM;
    // Détonations dans le tube : force en sortie, fréquence, brillance, durée.
    float popOpening;
    float popRate;
    float popCutoffHz;
    float popDecayS;
    // Le souffle du silencieux à la sortie : il ne traverse plus rien, donc il s'entend sur une ligne silencieuse.
    float tailNoise = 1.0f;
    // Son niveau au ralenti : une ligne libre souffle plus qu'une ligne d'origine.
    float tailFloor = 1.0f;
};

const ExhaustSpec& exhaustSpec(Exhaust exhaust);

/** Détonations par seconde juste après un lever de pied, avec un échappement sport. */
float popsPerSecond(PopLevel level);

enum class StarterKind { Direct, Reduction };

/**
 * Le démarreur, choisi selon le moteur à lancer. À entraînement direct : gros moteur lent, pignon droit,
 * plus rugueux. À réducteur : petit moteur rapide derrière un train planétaire, denture inclinée.
 */
struct StarterSpec {
    StarterKind kind = StarterKind::Reduction;
    float powerKw = 0.0f;       // 0 : pas de démarreur (électrique)
    int ringTeeth = 130;        // couronne du volant moteur
    int pinionTeeth = 10;
    float helixDeg = 0.0f;      // angle de denture : plus il est grand, plus l'engrènement est doux
    float reduction = 1.0f;     // train planétaire, entre l'induit et le pignon
    int commutatorBars = 25;
    float motorFreeRpm = 6000.0f;
    float motorInertia = 0.002f;  // induit (kg·m²)
    float compressionRatio = 10.0f;

    bool operator==(const StarterSpec&) const = default;
};

/** Où le moteur est monté : la longueur de ligne, la répartition des masses et ce qu'on entend en dépendent. */
enum class EnginePlacement { Front, Mid, Rear, Natural };
/** L'insonorisation de la caisse, de la voiture de course dépouillée à la berline très isolée. */
enum class Insulation { Stripped, Light, Standard, Comfort, Luxury };

/** Un point de la voiture, en mètres : au sol, au centre ; x vers l'avant, y vers la gauche, z vers le haut. */
struct Point3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    bool operator==(const Point3&) const = default;
};

/**
 * Les bruits placés sur la voiture. Pour en ajouter un : une valeur ici, sa place dans `carAcousticsOf`
 * (powertrain.cpp), son niveau dans le mélange d'EngineSound. Tout le reste en découle : distance,
 * sol, masquage, oreilles, habitacle, dB(A).
 */
enum class Source { ExhaustA, ExhaustB, Intake, Bay, Underfloor, Road, Count };
inline constexpr int kSources = static_cast<int>(Source::Count);

struct SourcePlace {
    Point3 at;
    /** Taille de la source (m) : on ne peut pas s'approcher plus près d'elle que cette distance. */
    float extentM = 0.1f;
    /** Part de l'écho du sol qui reste cohérente dans les médiums : 1 pour un point dans l'air calme. */
    float groundCoherence = 1.0f;

    bool operator==(const SourcePlace&) const = default;
};

/** Ce que le son doit savoir de la voiture : ses dimensions, ses sources, sa caisse. */
struct CarAcoustics {
    float lengthM = 4.3f;
    float widthM = 1.8f;
    float heightM = 1.45f;
    float wheelbaseM = 2.62f;
    std::array<SourcePlace, kSources> sources{};
    /** Le moteur lui-même, et la tête du conducteur. */
    Point3 engine;
    Point3 seat;
    /** Volume de l'habitacle : ce que la tête parcourt avant de quitter la caisse (m, depuis le siège). */
    float cabinBackM = 0.9f;
    float cabinFrontM = 0.9f;
    float outletRadiusM = 0.03f;
    EnginePlacement placement = EnginePlacement::Front;
    Insulation insulation = Insulation::Standard;
    bool windowsOpen = false;

    bool operator==(const CarAcoustics&) const = default;
};

/** Le moteur vu par EngineSound : sa géométrie, d'où découle le son. */
struct EngineProfile {
    int cylinders = 0;
    float idleRpm = 0.0f;
    float redlineRpm = 0.0f;
    /** Tours moteur simulés par tour de la voiture, au-dessus du ralenti. */
    float revMultiplier = 1.0f;

    /** Instant de chaque explosion dans le cycle 4 temps, dans [0, 1[. */
    std::array<float, kMaxCylinders> firing{};
    /** Force de chaque explosion : un écart entre cylindres crée les sous-harmoniques. */
    std::array<float, kMaxCylinders> firingGain{};
    /** Longueur de chaque collecteur (m), différente selon l'architecture. */
    std::array<float, kMaxCylinders> headerM{};
    int firingCount = 0;

    /** Dimensions d'un cylindre : elles fixent la durée de la bouffée d'échappement. */
    float boreM = 0.08f;
    float strokeM = 0.08f;
    Exhaust exhaust = Exhaust::Stock;
    bool diesel = false;
    /** Moteur électrique : coupure du filtre de sortie, au repos puis à plein régime. */
    float lowpassIdleHz = 1800.0f;
    float lowpassRedlineHz = 6500.0f;
    /** Sifflement du turbo et décharge au lever de pied. */
    bool turbo = false;
    PopLevel pops = PopLevel::Off;
    float popForce = 1.0f;
    float popBodyHz = 110.0f;
    TurboRelease turboRelease = TurboRelease::DumpValve;
    /** Pression maximale du turbo, au-dessus de la pression atmosphérique (bar). */
    float boostBar = 1.0f;
    /** Pas d'explosions : sifflement de l'onduleur et du moteur. */
    bool electric = false;
    StarterSpec starter;
    /** Longueur de la ligne selon la voiture : plus courte sur un moteur central. */
    float exhaustScale = 1.0f;
    /** Le chemin des gaz, et la rangée de chaque cylindre (0 ou 1) dans l'ordre d'allumage. */
    ExhaustRouting routing = ExhaustRouting::Single;
    std::array<int, kMaxCylinders> bank{};
    /** Deux rangées séparées ? Sinon, tous les cylindres rejoignent la même ligne. */
    bool twoBanks = false;
    CarAcoustics car;

    /**
     * Régime simulé pour un régime réel de la voiture. Au ralenti, les deux ralentis
     * coïncident ; ensuite le multiplicateur s'applique. La courbe se couche à
     * l'approche du régime maximal, sans jamais butter : pas de cassure.
     */
    float fromCarRpm(float carRpm) const;

    bool operator==(const EngineProfile&) const = default;
};

inline constexpr float kCarIdleRpm = 850.0f;
inline constexpr float kCarRedlineRpm = 6200.0f;

float fullRangeMultiplier(float idle, float redline);

/** Le moteur tel qu'on le construirait : le son découle de ces choix. */
struct EngineConfig {
    int cylinders = 4;
    Layout layout = Layout::Inline;
    int displacementCc = 2000;
    int boreMm = 84;
    int redlineRpm = 6800;
    Exhaust exhaust = Exhaust::Stock;
    Aspiration aspiration = Aspiration::Natural;
    Fuel fuel = Fuel::Petrol;
    /** Tours moteur par tour de la voiture ; <= 0 : la plage de la voiture couvre celle du moteur. */
    float revMultiplier = 0.0f;
    PopLevel pops = PopLevel::Off;
    TurboRelease turboRelease = TurboRelease::DumpValve;
    /** Pression maximale du turbo, au-dessus de la pression atmosphérique (bar). */
    float boostBar = 1.0f;
    ExhaustRouting routing = ExhaustRouting::Single;
    TurboSize turboSize = TurboSize::Medium;
    SuperchargerKind superchargerKind = SuperchargerKind::Roots;
    AntiLag antiLag = AntiLag::Off;
    /** Longueur du tube entre les cylindres et la turbine (mm) : 400 est la référence de la calibration. */
    int turboPipeMm = 400;
    Camshaft camshaft = Camshaft::Dohc;
    AirFilter airFilter = AirFilter::Stock;

    bool operator==(const EngineConfig&) const = default;

    float cylinderCc() const;
    /** Déduite de la cylindrée d'un cylindre et de son alésage. */
    float strokeMm() const;
    EngineProfile toProfile() const;
};

/** Sifflement d'un moteur électrique : pas de cylindres, pas de configuration. */
EngineProfile electricProfile();

}  // namespace revforge
