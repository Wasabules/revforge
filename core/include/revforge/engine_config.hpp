#pragma once

#include <array>

namespace revforge {

inline constexpr int kMaxCylinders = 12;

enum class Layout { Inline, V, CrossPlane, Boxer };
enum class Exhaust { Stock, Sport, Straight };
enum class Aspiration { Natural, Turbo };
enum class Fuel { Petrol, Diesel };
enum class PopLevel { Off, Light, Strong };
enum class TurboRelease { DumpValve, Flutter };

/**
 * Ce que le silencieux fait subir à l'onde : `chambers` la disperse (donc l'étouffe
 * et la fait durer), `cutoffHz` retire le haut du spectre, `reflection` renvoie une
 * part de l'onde vers le moteur (le caractère creux d'un tube libre).
 */
struct ExhaustSpec {
    int chambers;
    float cutoffHz;
    float reflection;
    float pipeM;
    float level;
    // Détonations dans le tube : force en sortie, fréquence, brillance, durée.
    float popOpening;
    float popRate;
    float popCutoffHz;
    float popDecayS;
};

const ExhaustSpec& exhaustSpec(Exhaust exhaust);

/** Détonations par seconde juste après un lever de pied, avec un échappement sport. */
float popsPerSecond(PopLevel level);

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
    /** Pas d'explosions : sifflement de l'onduleur et du moteur. */
    bool electric = false;

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

    float cylinderCc() const;
    /** Déduite de la cylindrée d'un cylindre et de son alésage. */
    float strokeMm() const;
    EngineProfile toProfile() const;
};

/** Sifflement d'un moteur électrique : pas de cylindres, pas de configuration. */
EngineProfile electricProfile();

}  // namespace revforge
