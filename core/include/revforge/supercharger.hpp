#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le compresseur mécanique, tel que le moteur le dimensionne. Il est entraîné par le vilebrequin : sa vitesse est un
 * rapport fixe du régime, sans turbine ni arbre à lancer. Tout découle de la cylindrée, du régime maximal, de la
 * pression demandée et du type choisi.
 */
struct SuperchargerSpec {
    SuperchargerKind kind = SuperchargerKind::Roots;
    double displacementM3 = 0.002;
    double targetBar = 1.0;       // pression tenue par la soupape de dérivation, au-dessus de l'atmosphère
    double redlineRpm = 6800.0;
    double driveRatio = 2.0;      // tours de l'arbre du compresseur par tour de vilebrequin

    // Volumétriques (Roots, à vis)
    double rotorVolumeM3 = 1e-3;  // volume engendré par tour d'arbre
    double leakArea = 1e-4;       // jeux entre rotors et carter, m²
    double volumeRatio = 1.0;     // compression interne : 1 pour un Roots, qui n'en a aucune
    double innerRise = 1.0;       // volumeRatio^(γ-1)
    double innerPressure = 1.0;   // volumeRatio^γ
    int pulsesPerTurn = 6;        // poches vidées par tour d'arbre, ou aubes de la roue

    // Centrifuge
    double wheelDiameterM = 0.07;
    double curveConductance = 1e-5;  // pente de la caractéristique : kg/s par Pa perdu sur la hauteur à vide
    double chokeFlow = 0.3;          // débit au blocage, à la vitesse nominale (kg/s)

    double driveEfficiency = 0.96;   // courroie et pignons
    double frictionW = 1000.0;       // paliers et pignons, à `referenceOmega`
    double referenceOmega = 1000.0;  // l'arbre à 88 % du régime maximal (rad/s)
    double ratedFlow = 0.2;          // débit du moteur à ce point, à la pression demandée (kg/s)
    double chargeVolumeM3 = 0.004;   // entre le compresseur et le papillon
    double filterDrop = 1.0;         // perte du filtre à l'entrée, Pa par (kg/s)²
    double coolerDrop = 1.0;         // perte de l'échangeur, Pa·(kg/m³) par (kg/s)²
    double bypassArea = 2e-3;        // soupape de dérivation grande ouverte (m²)
    AirFilter inletFilter = AirFilter::Stock;  // ce qui fixe la température et la perte à l'entrée du compresseur
    EnginePlacement inletPlacement = EnginePlacement::Front;
    /** L'air du jour, où il aspire (voir `TurboSpec`). Le dimensionnement est fait dans l'air de référence. */
    double ambientPa = 101325.0;
    double ambientK = 293.0;
};

/** Ce qui change d'une milliseconde à l'autre, en unités physiques. */
struct SuperchargerState {
    double rotorOmega = 0.0;       // rad/s
    double chargePa = 101325.0;    // avant le papillon, après l'échangeur
    double plenumPa = 101325.0;    // après le papillon
    double outletPa = 101325.0;    // sortie du compresseur, avant l'échangeur
    double inletPa = 101325.0;     // entrée du compresseur, après le filtre
    double compressorFlow = 0.0;   // kg/s, ce que déplace le compresseur
    double engineFlow = 0.0;       // kg/s, ce qu'avale le moteur
    double bypass = 0.0;           // 0 fermée, 1 ouverte par le vide du collecteur
    double spillShare = 0.0;       // part du débit rendue à l'entrée : vide au collecteur, ou pression demandée atteinte
    double compressorOutK = 293.0;
    double coolerOutK = 293.0;
    double shaftW = 0.0;           // puissance prise au vilebrequin
};

/** Ce que le son doit savoir du compresseur : sa vitesse et l'écart de pression entre ses deux côtés. */
struct SuperchargerDrive {
    float ratio = 0.0f;       // tours d'arbre par tour de vilebrequin
    float liftPa = 0.0f;      // sortie moins entrée
    float inletPa = 101325.0f;
    float flowShare = 0.0f;   // débit du moteur, rapporté au débit nominal
    float speedShare = 0.0f;  // vitesse de l'arbre, rapportée à sa vitesse nominale
};

/** Le point d'équilibre pour ce régime et ce remplissage : ce que donnent les courbes de la fiche. */
struct SuperchargerSteady {
    double boost = 0.0;        // part de la pression demandée, 0 à 1
    double chargePa = 101325.0;
    double outK = 293.0;
    double coolK = 293.0;
    double flow = 0.0;         // débit du moteur (kg/s)
    double spillShare = 0.0;
    double shaftW = 0.0;
    double driveNm = 0.0;      // couple pris au vilebrequin
    double rotorRpm = 0.0;
};

struct SuperchargerInput {
    double rpm = 0.0;
    double fill = 0.0;       // remplissage 0-1 : le papillon, ou le carburant d'un diesel
    double breathing = 1.0;  // respiration du moteur à ce régime, 1 au pic de couple
};

/**
 * L'air entre le compresseur et le papillon : un volume, entre ce que le compresseur y met et ce que le moteur en retire.
 * Son temps de remplissage, de l'ordre de 50 ms, est tout le retard qu'il y a. Avance par pas fixes : déterministe, et sans
 * fonction de la bibliothèque mathématique autre que `sqrt`.
 */
class Supercharger {
public:
    /** Garde la pression : changer un réglage en marche ne doit pas la faire sauter. */
    void setSpec(const SuperchargerSpec& spec);
    void step(double dt, const SuperchargerInput& input);

    const SuperchargerSpec& spec() const { return spec_; }
    const SuperchargerState& state() const { return state_; }
    /** Pression avant le papillon, en part de la pression demandée. Négative : sous l'atmosphère. */
    double boost() const;
    /** Couple pris au vilebrequin (Nm). */
    double driveNm(double rpm) const;
    SuperchargerDrive drive() const;

private:
    SuperchargerSpec spec_;
    SuperchargerState state_;
};

/** La part du régime maximal où un compresseur volumétrique atteint la pression demandée : c'est là qu'on le dimensionne. */
inline constexpr double kVolumetricSizingShare = 0.4;
/** Le point nominal, comme pour un turbo. */
inline constexpr double kSuperchargerRatedShare = 0.88;

/** Dimensionne le compresseur. `breathingAtSizing` : respiration à 40 % du régime maximal ; `breathingAtRated` : à 88 %. */
SuperchargerSpec superchargerSpecFor(const EngineConfig& engine, double breathingAtSizing, double breathingAtRated);
/** L'équilibre à ce régime et à ce remplissage, soupape de dérivation comprise. */
SuperchargerSteady superchargerSteady(const SuperchargerSpec& spec, double rpm, double breathing, double fill = 1.0);
/**
 * L'air admis, rapporté à celui de la pression atmosphérique : p/T, à cette pression et à ce régime. C'est ce qui fait le
 * couple : un air plus chaud pèse moins, d'où l'écart entre un Roots et une vis à même pression. Toujours dans l'air de
 * référence, quel que soit celui de `spec`.
 */
double superchargerAirShare(const SuperchargerSpec& spec, double rpm, double breathing, double boost);
/**
 * L'air piégé dans l'air du jour de `spec`, rapporté à celui de l'air de référence, à même part `boost` de la pression
 * demandée : la charge part de l'atmosphère du jour, puis le compresseur, l'échangeur et les parois. Exactement 1 dans l'air de référence.
 */
double superchargerAmbientChargeShare(const SuperchargerSpec& spec, double rpm, double breathing, double boost);

}  // namespace revforge
