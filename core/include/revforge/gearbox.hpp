#pragma once

#include <array>
#include <cstdint>

namespace revforge {

inline constexpr int kMaxGears = 8;

/** Comment les rapports s'engagent. L'ordre compte : la position est la valeur envoyée par le site. */
enum class GearboxKind {
    Synchro,     // manuelle à synchros : le cône met les arbres à la même vitesse avant d'engager
    Dog,         // à crabots, grille en H : pas de cône, les crabots tombent quand l'écart le permet
    Sequential,  // à crabots et barillet : un rapport à la fois, coupure d'allumage à la montée
    DualClutch,  // deux embrayages : le rapport suivant est déjà engagé, le couple passe de l'un à l'autre
    Converter,   // automatique : convertisseur de couple et embrayages sous charge
    Reducer,     // voiture électrique : un seul rapport, deux étages
};

enum class ToothForm { Helical, Spur };

/** Familles de rapports : leur écart, et de combien le dernier rapport tire long ou court. */
enum class GearFamily { Matched, Short, Close, Long, Custom };

enum class Flywheel { Single, DualMass };

/**
 * La boîte comme pièce : ce qu'on choisit et qu'on règle. Une valeur nulle laisse le choix au constructeur :
 * le pont et le couple admissible s'adaptent alors à la voiture et au moteur. Tout renseigner rend la boîte
 * indépendante du moteur : elle peut être mal adaptée, et les conséquences sont réelles.
 */
struct GearboxConfig {
    GearboxKind kind = GearboxKind::Synchro;
    int gears = 0;                           // 0 : selon la carrosserie
    GearFamily family = GearFamily::Matched;
    std::array<float, kMaxGears> ratios{};   // famille Custom : rapport de chaque vitesse
    float finalDrive = 0.0f;                 // 0 : choisi pour atteindre la vitesse de pointe
    ToothForm form = ToothForm::Helical;
    float helixDeg = 0.0f;                   // 0 : 28° en denture hélicoïdale
    float moduleMm = 0.0f;                   // 0 : selon la taille de la boîte
    float limitNm = 0.0f;                    // 0 : dimensionnée pour le moteur
    Flywheel flywheel = Flywheel::Single;

    bool operator==(const GearboxConfig&) const = default;
};

/** Une paire de pignons : des nombres de dents entiers, dont découlent le rapport et les fréquences. */
struct GearPair {
    int driver = 0;
    int driven = 0;
    float profile = 1.5f;  // rapport de conduite de profil, εα
    float overlap = 0.0f;  // rapport de recouvrement, εβ : zéro en denture droite

    double ratio() const { return driver > 0 ? static_cast<double>(driven) / driver : 0.0; }

    bool operator==(const GearPair&) const = default;
};

/** Une roue folle : elle tourne librement dans son jeu, et c'est elle qui claque. */
struct LooseGear {
    float inertia = 0.0f;      // kg·m²
    float speedRatio = 0.0f;   // vitesse de la roue rapportée à l'arbre primaire
    float gap = 0.0f;          // demi-jeu angulaire à la roue (rad)
    float drag = 0.0f;         // couple de traînée de l'huile et du roulement (Nm)

    bool operator==(const LooseGear&) const = default;
};

inline constexpr int kLooseGears = 4;

/** La boîte telle qu'elle a été construite : tout ce qui découle des réglages. */
struct GearboxSpec {
    GearboxKind kind = GearboxKind::Synchro;
    ToothForm form = ToothForm::Helical;
    Flywheel flywheel = Flywheel::Single;
    bool threeShafts = false;  // boîte longitudinale : une paire en prise constante, et un rapport en prise directe
    bool hypoid = false;       // pont à couple conique
    int gears = 0;
    std::array<GearPair, kMaxGears> pairs{};
    GearPair inputPair;                      // prise constante, en boîte à trois arbres
    GearPair reverseIn, reverseOut;          // marche arrière : par un pignon intermédiaire, en denture droite
    GearPair finalPair;
    int directGear = 0;                      // rapport en prise directe : aucune denture chargée dans la boîte (0 : aucun)
    std::array<double, kMaxGears> boxRatio{};
    double reverseRatio = 0.0;
    double finalDrive = 0.0;
    double limitNm = 0.0;                    // couple admissible à l'entrée
    double centerMm = 0.0;                   // entraxe
    double faceMm = 0.0;                     // largeur de denture
    double moduleMm = 0.0;
    double helixDeg = 0.0;
    double inputInertia = 0.0;               // arbre primaire, disque d'embrayage et ce qu'ils entraînent (kg·m²)
    double coneTorqueNm = 0.0;               // couple du cône à simple effet
    double coneRadiusM = 0.0;
    double dragNm = 0.0;                     // traînée à l'arrêt, à l'arbre primaire
    double dragPerRad = 0.0;                 // traînée selon la vitesse (Nm par rad/s)
    double meshEfficiency = 1.0;             // rendement sous charge, jusqu'aux roues
    int dogTeeth = 0;                        // dents de la couronne d'engagement
    std::array<LooseGear, kLooseGears> loose{};

    bool operator==(const GearboxSpec&) const = default;

    /** Double cône sur les premiers rapports, où l'écart de vitesses et l'inertie sont les plus grands. */
    double coneTorqueFor(int gear) const;
    /** Fréquence d'engrènement du rapport engagé, pour un arbre primaire à ce régime (Hz). */
    double meshHz(int gear, double inputRpm) const;
};

/**
 * L'état mécanique de la boîte à cet instant, tel que son bruit en a besoin. Le son ne regarde jamais le régime du
 * moteur pour faire siffler les dents : seulement les vitesses des arbres et le couple qui les traverse.
 */
struct GearboxDrive {
    float inputOmega = 0.0f;     // arbre primaire (rad/s)
    float outputOmega = 0.0f;    // arbre de sortie, avant le pont (rad/s)
    float torqueNm = 0.0f;       // couple à l'entrée : positif si le moteur tire, négatif en frein moteur
    float acyclism = 0.0f;       // accélération angulaire de l'arbre primaire à chaque explosion (rad/s²)
    int firings = 4;             // explosions par cycle de 720°
    int gear = 0;                // rapport en prise : 0 aucun, -1 marche arrière
    bool running = false;        // le moteur a pris
    float coneSlip = 0.0f;       // écart de vitesses sur le cône pendant la synchronisation (rad/s)
    float coneTorque = 0.0f;     // couple que porte le cône (Nm)
    float grind = 0.0f;          // écart de vitesses quand le baladeur force sur les crabots (rad/s) : ça gratte
    float clutchSlip = 0.0f;     // puissance dissipée dans l'embrayage (kW)
    float pedal = 0.0f;          // butée d'embrayage appuyée, 0 à 1
    double impacts = 0.0;        // énergie cumulée des chocs : engagement, crabots, reprise du jeu (J)
    double selectorClicks = 0.0; // nombre cumulé de mouvements du baladeur, pour l'étouffer selon la force
};

/** Ce que le moteur et la voiture imposent à une boîte : rien de tout cela n'est fixé par elle. */
struct GearboxContext {
    int defaultGears = 5;
    double engineTorqueNm = 200.0;
    double topOverall = 0.0;     // démultiplication totale qui atteint la puissance maximale à la vitesse de pointe
    bool rearOrAll = false;      // propulsion ou intégrale : boîte longitudinale, pont conique
    bool allWheels = false;
    bool electric = false;
};

/** Construit la boîte : rapports, dents, inerties, couple admissible. Déterministe, sans bibliothèque mathématique. */
GearboxSpec designGearbox(const GearboxConfig& config, const GearboxContext& context);

/** Le rendement de la chaîne seul, avant d'avoir construit la boîte. */
double drivelineEfficiency(GearboxKind kind, ToothForm form, bool rearOrAll, bool allWheels);

}  // namespace revforge
