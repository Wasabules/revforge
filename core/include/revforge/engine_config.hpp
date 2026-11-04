#pragma once

#include <array>

namespace revforge {

/** Seize cylindres : le V16 et le W16. Les tableaux par cylindre du son et du profil en ont cette taille. */
inline constexpr int kMaxCylinders = 16;
/** Six rotors au plus : la borne d'avant les seize cylindres, qui ne change pas pour eux. */
inline constexpr int kMaxRotors = 6;
/** Un deux-temps balayé par son carter : au plus 6 cylindres (`coherentTwoStroke`), comme les rotors. */
inline constexpr int kMaxTwoStrokeCylinders = 6;

/**
 * `Rotary` : moteur à pistons rotatifs (Wankel). `cylinders` y compte les rotors, et `displacementCc` une chambre par rotor.
 * `Vr` : un V très fermé sous une seule culasse (VR5, VR6). `W` : deux bancs VR côte à côte (W8, W12, W16). Voir architecture.hpp.
 */
enum class Layout { Inline, V, CrossPlane, Boxer, Rotary, Vr, W };
// L'ordre compte : la position donne la valeur envoyée par le site (web/src/lib/config.ts). On ajoute à la fin.
/**
 * `Expansion` : la chambre de détente d'un deux-temps, accordée sur lui (`expansionChamberFor`). Sur un quatre-temps, les soupapes
 * ferment le cylindre avant le retour de l'onde : la CLI et le site la ramènent à la ligne directe (`coherentTwoStroke`).
 */
enum class Exhaust { Stock, Sport, Straight, MufflerDelete, ResonatorDelete, Quiet, Expansion };
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
/**
 * L'arbre à cames : deux en tête, quatre soupapes par cylindre ; un seul en tête, deux soupapes ; un seul dans le bloc,
 * avec poussoirs hydrauliques, tiges et culbuteurs (`Ohv`), deux soupapes en ligne. Voir « Les culbuteurs et l'affolement ».
 */
enum class Camshaft { Dohc, Sohc, Ohv };
/** Le filtre à air : papier d'origine dans sa boîte, panneau sport dans la boîte, cône à l'air libre, cornets sans boîte. */
enum class AirFilter { Stock, Panel, Cone, Stacks };
/**
 * L'alimentation en essence : injection indirecte, carburateur unique sur une pipe commune, un corps de carburateur par
 * cylindre, injection directe dans la chambre (voir « L'injection » dans docs/modele.md).
 */
enum class FuelSystem { Injection, Carburetor, MultiCarb, Direct };

// ── Les réglages du socle moteur : posés de bout en bout, sans effet sur le calcul. Voir CLAUDE.md. ──
/**
 * Le moteur au démarrage, selon le temps qu'il est resté arrêté : il vient de rouler (chaud, à la référence), il a passé la
 * nuit dehors (`Cold`, tout à l'air ambiant), ou entre les deux, une demi-heure ou deux heures d'arrêt, dont le réseau
 * thermique calcule le refroidissement (`ThermalNetwork::soak`). Les valeurs ajoutées vont en fin : la position est la valeur.
 */
enum class StartTemp { Warm, Cold, HalfHour, TwoHours };
/** Le carburant d'un moteur à essence : indice d'octane recherche, éthanol E85, méthanol. */
enum class FuelGrade { Ron95, Ron98, Race102, E85, Methanol };
/** Le rupteur : celui d'aujourd'hui, un retard puis une coupure partielle, ou une coupure d'allumage qui laisse passer l'essence. */
enum class RevLimiter { Stock, Soft, IgnitionCut };
/** L'antipatinage : aucun, une cible de glissement sportive, ou serrée. */
enum class TractionControl { Off, Sport, Full };
/** L'injection d'un diesel : pompe mécanique, ou rampe commune avec injection pilote. */
enum class DieselInjection { Mechanical, CommonRail };
/** Le calage variable : fixe, déphaseur d'admission, deux profils, ou les deux. */
enum class ValveTiming { Fixed, Phased, TwoStep, PhasedTwoStep };
/** Les ressorts de soupape : d'origine, ou plus raides. */
enum class ValveSprings { Stock, Stiff };
/** Les turbos : un seul, deux en parallèle, deux en séquence, ou un compresseur et un turbo en série. */
enum class TurboLayout { Single, TwinParallel, TwinSequential, Twincharger };
/** La régulation de pression : wastegate à ressort, électrovanne pilotée, ou cible selon le rapport. */
enum class BoostControl { Spring, Electronic, ByGear };
/** L'échangeur de l'air de charge : air-air, air-eau, ou aucun. */
enum class Intercooler { AirToAir, AirToWater, None };
/** Le cycle : quatre temps, ou deux (lumières, carter-pompe : voir two_stroke.hpp). Un rotatif garde le sien. */
enum class EngineCycle { FourStroke, TwoStroke };
/**
 * L'huile, selon l'époque du moteur (SAE J300) : la 5W-30 de référence, sur laquelle le moteur chaud est calé, puis 0W-20,
 * 10W-40, 15W-40, 20W-50 et un monograde SAE 30. Elle ne change que le moteur froid : voir cold_start.hpp.
 */
enum class OilGrade { Sae5W30, Sae0W20, Sae10W40, Sae15W40, Sae20W50, Sae30 };

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

/**
 * L'air autour de la voiture, pour le son : il porte les retards, les modes de l'habitacle, le rayonnement et l'absorption.
 * Par défaut, l'air de référence (20 °C, 0 m) : voir `airAcousticsOf` (atmosphere.hpp).
 */
struct AirAcoustics {
    float soundSpeedMs = 343.0f;
    /** √θ et σ = ρ/ρ₀ : exactement 1 dans l'air de référence. */
    float soundSpeedRatio = 1.0f;
    float densityRatio = 1.0f;
    double celsius = 20.0;
    double kilopascal = 101.325;

    bool operator==(const AirAcoustics&) const = default;
};

/** Ce que le son doit savoir de la voiture : ses dimensions, ses sources, sa caisse, et l'air qui l'entoure. */
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
    AirAcoustics air;

    bool operator==(const CarAcoustics&) const = default;
};

/** Les régimes où la came est calculée, du bas jusqu'à 1,25 fois le régime maximal (voir cam.hpp). */
inline constexpr int kCamPoints = 14;
/**
 * Ce que le son tire de la distribution en plus de l'arbre à cames : l'affolement des soupapes, et le choc des poussoirs
 * d'une distribution à culbuteurs. Rempli par `valvetrainProfileOf` (core/src/valve_float.cpp), lu par `floatOverload`…
 * Voir « Les culbuteurs et l'affolement » dans docs/modele.md.
 */
struct ValvetrainProfile {
    /** Régime d'affolement (tr/min) ; 0 sans soupapes (rotatif, deux-temps, électrique). */
    double floatRpm = 0.0;
    /** Ce que la souplesse de la chaîne ajoute à la décélération à ce régime : (f_d / f_n)². */
    double stiffening = 0.0;
    /** Durée de la décélération au nez (degrés de vilebrequin) : le retard de fermeture par unité de surcharge. */
    double decelSpanDeg = 0.0;
    /** Le flanc de fermeture (degrés de vilebrequin) : au-delà, la soupape retombe sur son siège sans la came. */
    double maxLateDeg = 0.0;
    /** Vitesse de la soupape au flanc par tr/min (m/s), et celle de la rampe de fermeture : la force du choc au siège. */
    double flankMsPerRpm = 0.0;
    double rampMsPerRpm = 0.0;
    /** Entre deux rebonds sur le siège, par m/s de vitesse d'arrivée (s) : 2·e·m / F₀. */
    double bounceSPerMs = 0.0;
    /** Taux de compression, et fermeture de l'admission après le point mort bas (°) : ce que garde un cylindre qui ferme tard. */
    double compression = 10.5;
    double ivcAfterBdcDeg = 50.0;
    /** Culbuteurs : l'impulsion du choc au poussoir, rapportée à celle du culbuteur d'un simple arbre, et le mode de la tige (Hz). */
    double pushrodTick = 0.0;
    double pushrodHz = 0.0;

    bool operator==(const ValvetrainProfile&) const = default;
};

/**
 * La chambre de détente d'un deux-temps, pour un cylindre (m) : collecteur, diffuseur, ventre, cône de retour, pointe.
 * Accordée sur le moteur par `expansionChamberFor` (two_stroke.hpp) ; tout à 0 sur une autre ligne.
 */
struct ExpansionChamber {
    float headerM = 0.0f;
    float diffuserM = 0.0f;
    float bellyM = 0.0f;
    float baffleM = 0.0f;
    float stingerM = 0.0f;
    float headerDiameterM = 0.0f;
    float bellyDiameterM = 0.0f;
    float stingerDiameterM = 0.0f;
    /** Vitesse du son du gaz chaud qui la remplit (m/s), et régime auquel l'onde de retour bouche la lumière (tr/min). */
    float soundMs = 0.0f;
    float tunedRpm = 0.0f;

    bool operator==(const ExpansionChamber&) const = default;
};

/** Points de la table de débit d'un deux-temps, du dixième du régime maximal à 1,2 fois : voir `twoStrokeTableRpm`. */
inline constexpr int kTwoStrokePoints = 16;

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
    /** Le filtre à air, et la longueur et le rayon de ses cornets s'il en a : le bruit d'admission en dépend. */
    AirFilter airFilter = AirFilter::Stock;
    float stackM = 0.0f;
    float stackRadiusM = 0.0f;
    /** Deux soupapes par cylindre au lieu de quatre : la bouffée s'allonge, la distribution change de voix. */
    Camshaft camshaft = Camshaft::Dohc;
    /** Le compresseur mécanique : son sifflement et l'air qu'il aspire. Sans lui, rien de cela ne s'exécute. */
    bool supercharged = false;
    SuperchargerKind superchargerKind = SuperchargerKind::Roots;
    /** Tours d'arbre par tour de vilebrequin, et volume d'une poche (m³) : pour quand aucun modèle ne donne l'état réel. */
    float superchargerRatio = 0.0f;
    float superchargerPocketM3 = 0.0f;
    /** La pression que la vis atteint d'elle-même, rapportée à l'entrée : 1 pour un Roots. */
    float superchargerInnerPressure = 1.0f;
    /** Le carburateur s'il y en a un : corps, col du venturi (m), longueur de conduit jusqu'à la soupape (m). */
    FuelSystem fuelSystem = FuelSystem::Injection;
    int carbThroats = 0;
    float carbThroatM = 0.0f;
    float carbRunnerM = 0.0f;
    /** Pied levé, le circuit de ralenti alimente encore : part des cycles où ce mélange brûle encore. 0 sur une injection, qui coupe. */
    float overrunFuel = 0.0f;
    /**
     * Moteur rotatif : `firing` y reste un cycle de deux tours d'arbre, avec deux explosions par rotor, donc
     * `cylinders` et `firingCount` valent deux fois le nombre de rotors, et `boreM`, `strokeM` donnent le volume
     * d'une chambre : le débit de gaz, calculé sur deux tours, tombe juste sans autre correction.
     */
    bool rotary = false;
    int rotors = 0;
    /** Durée de la bouffée, pleine charge : le volume de la chambre à l'ouverture, face à la section de la lumière (s). */
    float blowdownS = 0.0f;
    /** Diesel : le claquement suit le délai d'auto-inflammation, rapporté à celui du taux d'origine (`dieselKnockScale`). */
    float dieselKnockScale = 1.0f;
    /** L'injection directe d'essence : le tic des injecteurs et de la pompe haute pression (voir `InjectorSound`). */
    bool directInjection = false;
    /**
     * Diesel à rampe commune, et recirculation des gaz : ils changent la voix de la combustion (voir `combustionNoiseDb`).
     * Sans eux, rien ne se calcule.
     */
    bool commonRail = false;
    bool egr = false;
    /** Le filtre à particules dans la ligne : sa résistance, rapportée à ρc/S du tube. 0 : pas de filtre. */
    float filterResistance = 0.0f;
    /**
     * La came, quand elle n'est pas celle d'origine ou que son calage varie (voir `applyCam`, cam.hpp). Sans elle, rien de
     * ce qui suit n'est lu. Calage à 1 mm de levée, en part du cycle (ouverture et fermeture de l'échappement, puis de
     * l'admission), du profil de base et du second profil ; pression à l'ouverture de l'échappement face à la came
     * d'origine, régime par régime ; durée de la bouffée face à elle ; de quoi calculer les gaz résiduels.
     */
    bool camTuned = false;
    std::array<float, 4> camTiming{};
    std::array<float, 4> camHighTiming{};
    float camFirstRpm = 0.0f;
    float camStepRpm = 1.0f;
    std::array<float, kCamPoints> camOpening{};
    std::array<float, kCamPoints> camOpeningHigh{};
    float camBlowStretch = 1.0f;
    float camBlowStretchHigh = 1.0f;
    float camOverlapFactor = 0.0f;
    float camOverlapFactorHigh = 0.0f;
    float camStockOverlapFactor = 0.0f;
    float camCompression = 10.5f;
    float camRatedRpm = 6000.0f;
    /** La pression au collecteur que demande le ralenti (kPa), avec cette came, son second profil, et la came d'origine. */
    float camIdleKpa = 40.0f;
    float camIdleKpaHigh = 40.0f;
    float camStockIdleKpa = 40.0f;
    /** La dispersion des combustions selon les gaz résiduels : un moteur à étincelle seulement. */
    bool camDispersion = false;
    bool camTwoStep = false;
    float camSwitchRpm = 0.0f;
    /** L'affolement des soupapes et les poussoirs : voir `ValvetrainProfile`. */
    ValvetrainProfile valves;
    /**
     * Deux temps : `firing` reste un cycle de deux tours, avec deux explosions par cylindre à 360° l'une de l'autre, donc
     * `cylinders` et `firingCount` valent deux fois `pistons`, et les explosions i et i + pistons sont du même cylindre.
     * Les lumières, en part du cycle de 720° compté depuis l'explosion : échappement, transfert, admission dans le carter.
     * `blowdownS` y vaut aussi pour la bouffée. Voir « Le deux-temps » dans docs/modele.md.
     */
    bool twoStroke = false;
    int pistons = 0;
    float portEvo = 0.0f;
    float portEvc = 0.0f;
    float portTo = 0.0f;
    float portTc = 0.0f;
    float portIo = 0.0f;
    float portIc = 0.0f;
    /** Longueur de bielle sur course : l'aire découverte suit la course du piston. */
    float rodOverStroke = 2.0f;
    /** Rapport de débit à pleine charge, de `twoStrokeTableRpm(0)` à `twoStrokeTableRpm(kTwoStrokePoints - 1)`, et pureté gardée. */
    std::array<float, kTwoStrokePoints> delivery{};
    std::array<float, kTwoStrokePoints> purity{};
    /** Balayage de Benson et Brandham : part chassée en déplacement, part qui file droit à l'échappement. */
    float scavengeDisplacement = 0.0f;
    float scavengeShortCircuit = 0.0f;
    ExpansionChamber chamber;

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
    FuelSystem fuelSystem = FuelSystem::Injection;

    // ── Les réglages du socle moteur, dans l'ordre de `rf_set_tuning` : sans effet sur le calcul pour l'instant. ──
    // Leur défaut est le moteur d'aujourd'hui ; un nombre à 0 marqué « d'origine » laisse au moteur ce qu'il a.
    /** Conditions : air ambiant (°C), altitude (m), moteur chaud ou froid au démarrage. */
    float ambientC = 20.0f;
    int altitudeM = 0;
    StartTemp startTemp = StartTemp::Warm;
    /** Combustion : taux de compression géométrique (0 : d'origine), carburant. */
    float compressionRatio = 0.0f;
    FuelGrade fuelGrade = FuelGrade::Ron95;
    /**
     * Calculateur : décalage de la carte d'avance (°), richesse à pleine charge (0 : carte d'origine), rupteur, régime tenu
     * par le launch control (tr/min, 0 : sans), passage des rapports sans lever le pied, antipatinage.
     */
    int ignitionOffsetDeg = 0;
    float fullLoadLambda = 0.0f;
    RevLimiter revLimiter = RevLimiter::Stock;
    int launchRpm = 0;
    bool flatShift = false;
    TractionControl tractionControl = TractionControl::Off;
    /** Alimentation : injection d'un diesel, recirculation des gaz d'échappement, filtre à particules. */
    DieselInjection dieselInjection = DieselInjection::Mechanical;
    bool egr = false;
    bool particulateFilter = false;
    /** Distribution : durée (°) et levée (mm) de came, angle entre cames (°), tous à 0 : d'origine ; calage variable, ressorts. */
    int camDurationDeg = 0;
    float camLiftMm = 0.0f;
    int lobeSeparationDeg = 0;
    ValveTiming valveTiming = ValveTiming::Fixed;
    ValveSprings valveSprings = ValveSprings::Stock;
    /** Suralimentation : turbos, géométrie variable, régulation, échangeur, injection d'eau, protoxyde d'azote (kW, 0 : sans). */
    TurboLayout turboLayout = TurboLayout::Single;
    bool variableGeometry = false;
    BoostControl boostControl = BoostControl::Spring;
    Intercooler intercooler = Intercooler::AirToAir;
    bool waterInjection = false;
    int nitrousKw = 0;
    /** Architecture : le cycle. */
    EngineCycle cycle = EngineCycle::FourStroke;
    /** L'huile du carter (démarreur) : la référence, 5W-30, ne change rien au moteur chaud. */
    OilGrade oilGrade = OilGrade::Sae5W30;

    bool operator==(const EngineConfig&) const = default;

    float cylinderCc() const;
    /** Déduite de la cylindrée d'un cylindre et de son alésage. */
    float strokeMm() const;
    EngineProfile toProfile() const;

    /** Le nombre de rotors, borné à `kMaxRotors` (deux explosions par rotor et par cycle). N'a de sens que sur un rotatif. */
    int rotors() const { return cylinders < 1 ? 1 : (cylinders > kMaxRotors ? kMaxRotors : cylinders); }
    /** Un deux-temps : une explosion par tour et par cylindre. Un rotatif garde son propre cycle, quoi que dise `cycle`. */
    bool twoStroke() const { return cycle == EngineCycle::TwoStroke && layout != Layout::Rotary; }
    /** Les cylindres d'un deux-temps, au plus `kMaxTwoStrokeCylinders` (`TWO_STROKE_MAX_CYLINDERS` sur le site) : chacun allume deux fois. */
    int pistons() const { return cylinders < 1 ? 1 : (cylinders > kMaxTwoStrokeCylinders ? kMaxTwoStrokeCylinders : cylinders); }
    /** Explosions en deux tours d'arbre : une par cylindre en quatre temps, deux par rotor ou par cylindre d'un deux-temps. */
    int firingsPerTwoTurns() const { return layout == Layout::Rotary ? 2 * rotors() : (twoStroke() ? 2 * pistons() : cylinders); }
    /**
     * Le volume balayé en deux tours d'arbre (cm³) : ce qui compte pour l'air, le gaz et le couple. La cylindrée d'un
     * quatre temps ; deux fois celle d'un rotatif, dont chaque rotor balaie une chambre à chaque tour, ou d'un deux-temps.
     */
    double sweptCcPerTwoTurns() const { return (layout == Layout::Rotary || twoStroke() ? 2.0 : 1.0) * static_cast<double>(displacementCc); }
    /**
     * La distribution réelle : un rotatif et un deux-temps n'ont ni soupapes ni arbre à cames, quoi que dise `camshaft`. Le
     * double arbre est le modèle d'origine ; le simple arbre et les culbuteurs ont deux soupapes (`twoValves`, valve_float.hpp).
     */
    Camshaft valvetrain() const { return layout == Layout::Rotary || twoStroke() ? Camshaft::Dohc : camshaft; }
    /** Le diamètre d'un cylindre : `boreMm`, sauf sur un rotatif, qui n'a pas d'alésage et prend le cylindre carré de sa chambre (voir `applyRotary`). */
    double effectiveBoreMm() const;
    /** Un rotatif ne brûle que de l'essence, et le deux-temps simulé aussi (balayé par son carter) : un réglage diesel y reste sans effet. */
    bool burnsDiesel() const { return fuel == Fuel::Diesel && layout != Layout::Rotary && !twoStroke(); }
};

/** Vrai pour un moteur rotatif : ni alésage, ni course, ni arbre à cames, ni diesel. */
inline bool isRotary(const EngineConfig& config) { return config.layout == Layout::Rotary; }
/** Vrai pour un deux-temps : des lumières au lieu des soupapes, un carter qui pompe, ni diesel ni suralimentation. */
inline bool isTwoStroke(const EngineConfig& config) { return config.twoStroke(); }

/** La géométrie d'un rotor (mm) : rayon générateur R, excentricité e, largeur b. V = 3√3·R·e·b par chambre. */
struct RotaryGeometry {
    double radiusMm = 0.0;
    double eccentricityMm = 0.0;
    double widthMm = 0.0;
};

/** Déduite du volume d'une chambre, aux proportions du moteur de référence (R 105, e 15, b 80 : 654 cm³). */
RotaryGeometry rotaryGeometry(double chamberCc);
/** Taux de compression géométrique : la forme de la chambre le borne, et il baisse sous pression. */
float rotaryCompressionRatio(Aspiration aspiration);
/** Surface de chambre sur volume au point mort haut, face à un cylindre carré de 500 cm³ : la cause des pertes aux parois. */
double rotaryWallRatio(float compressionRatio);
/**
 * Le rendement indiqué d'un rotatif, en part de celui d'un moteur à pistons de même aspiration. Il ne dépend que de sa
 * forme : la chambre allongée perd plus de chaleur aux parois, et le taux de compression est plus bas.
 */
double rotaryEfficiencyShare(Aspiration aspiration);
/** Le rendement du cycle d'un rotatif à ce taux : Otto, moins ce que perd sa chambre allongée. Ne sert qu'à comparer deux taux. */
double rotaryCycleEfficiency(double compressionRatio);

/** Sifflement d'un moteur électrique : pas de cylindres, pas de configuration. */
EngineProfile electricProfile();

/**
 * La cylindrée (cm³, entière) qui donne cette course à cet alésage et à ces cylindres : la course n'est pas
 * stockée, elle se déduit. Même calcul que `displacementFor` dans web/src/lib/stroke.ts.
 */
int displacementForStroke(int boreMm, int cylinders, double strokeMm);

}  // namespace revforge
