#pragma once

#include "revforge/anti_lag.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le turbocompresseur, tel que le moteur le dimensionne : roues, inertie de l'arbre, turbine, volumes.
 * Tout découle de la cylindrée, du régime maximal, de la pression demandée et de la taille choisie.
 */
struct TurboSpec {
    bool diesel = false;
    double displacementM3 = 0.002;
    double targetBar = 1.0;      // pression tenue par la soupape de décharge, au-dessus de l'atmosphère
    double redlineRpm = 6800.0;

    double wheelDiameterM = 0.056;  // sortie de la roue du compresseur
    double turbineRadiusM = 0.023;
    double inertia = 2.7e-5;        // arbre et deux roues (kg·m²)
    double turbineK = 2e-5;         // débit de la turbine, loi de l'ellipse (kg·√K/(s·Pa))
    double wastegateK = 2e-5;
    double frictionW = 800.0;       // paliers, à `referenceOmega`
    double referenceOmega = 14000.0;  // l'arbre au point nominal (rad/s) : c'est lui, le 1 de `TurboState::shaftShare`
    double ratedFlow = 0.2;         // débit d'air au point nominal (kg/s)
    double tipLimit = 520.0;        // vitesse de bout de pale que les roues supportent (m/s)

    double chargeVolumeM3 = 0.006;  // conduits et échangeur, entre le compresseur et le papillon
    double ductInertance = 500.0;   // L/S du conduit (1/m) : c'est lui qui donne son rythme au pompage
    double filterDrop = 1.0;        // perte du filtre, Pa par (kg/s)²
    double coolerDrop = 1.0;        // perte de l'échangeur, Pa·(kg/m³) par (kg/s)²
    double lineDrop = 1.0;          // contre-pression de la ligne, Pa par (kg/s)²
    double ratedBackPa = 0.0;       // ce que coûte un turbo bien apparié au point nominal : pression d'échappement moins pression de charge
    bool blowOffValve = true;       // sans soupape : l'air revient par le compresseur (pompage, flutter)
    double blowOffArea = 4.5e-4;    // m²
};

/** Ce qui change d'une milliseconde à l'autre, en unités physiques. */
struct TurboState {
    double shaftOmega = 0.0;      // rad/s
    double shaftShare = 0.0;      // rapportée à la vitesse nominale
    double chargePa = 101325.0;   // avant le papillon, après l'échangeur
    double plenumPa = 101325.0;   // après le papillon
    double outletPa = 101325.0;   // sortie du compresseur, avant l'échangeur
    double inletPa = 101325.0;    // entrée du compresseur, après le filtre
    double turbineInPa = 101325.0;
    double compressorFlow = 0.0;  // kg/s, négatif quand l'air revient (pompage)
    double blowOffFlow = 0.0;     // kg/s
    double engineFlow = 0.0;      // kg/s
    double wastegate = 0.0;       // 0 fermée, 1 ouverte
    double blowOff = 0.0;         // 0 fermée, 1 ouverte
    double compressorOutK = 293.0;
    double coolerOutK = 293.0;
    double turbineInK = 293.0;
    double turbinePowerW = 0.0;
    double compressorPowerW = 0.0;
};

/** Ce que le son doit savoir du turbo : la vitesse réelle de l'arbre et les débits. */
struct TurboDrive {
    float shaftHz = 0.0f;
    float shaftShare = 0.0f;
    float flowShare = 0.0f;       // débit du compresseur, rapporté au débit nominal
    float blowOffShare = 0.0f;    // force du jet par la soupape : 1 pour tout le débit nominal à la pression demandée
    float backflowShare = 0.0f;   // de même, pour l'air qui revient par le compresseur
    float wastegate = 0.0f;
    float wastegateFlowShare = 0.0f;  // ce qui contourne la turbine, rapporté au débit nominal
    AntiLagDrive antiLag;
};

/** Ce qu'il faut savoir du moteur à chaque pas. */
struct TurboInput {
    double rpm = 0.0;
    double fill = 0.0;      // remplissage 0-1 : le papillon, ou le carburant d'un diesel
    double breathing = 1.0; // respiration du moteur à ce régime, 1 au pic de couple
    bool burning = false;
    AntiLagCommand antiLag;
};

/** Le point d'équilibre à pleine charge : ce que donnent les courbes de la fiche. */
struct TurboSteady {
    double boost = 0.0;       // part de la pression demandée, 0 à 1
    double chargePa = 101325.0;
    double turbineInPa = 101325.0;
    double shaftRpm = 0.0;
    double wastegate = 0.0;
    double flow = 0.0;
};

/**
 * L'arbre, et l'air autour de lui.
 *
 *   J·ω·dω/dt = P_turbine − P_compresseur − P_paliers
 *
 * Turbine : loi de l'ellipse de Stodola pour la pression d'admission, rendement selon la vitesse réduite U/c.
 * Compresseur : caractéristique cubique de Moore et Greitzer, inertie de l'air dans le conduit, volume entre le
 * compresseur et le papillon. Le pompage et le flutter en sortent seuls : pas de minuterie, pas d'enveloppe.
 *
 * Avance par pas fixes : déterministe, et sans fonction de la bibliothèque mathématique autre que `sqrt`.
 */
class TurboCharger {
public:
    /** Garde l'état : changer un réglage en marche ne doit pas arrêter l'arbre. */
    void setSpec(const TurboSpec& spec);
    void step(double dt, const TurboInput& input);

    const TurboSpec& spec() const { return spec_; }
    const TurboState& state() const { return state_; }
    /** Pression avant le papillon, en part de la pression demandée. Négative : sous l'atmosphère. */
    double boost() const;
    /** Couple que coûte la contre-pression au moteur (Nm), positif quand elle freine. */
    double backPressureNm(double fill) const;
    TurboDrive drive() const;
    /** Ce que fait l'anti-lag à ce pas. Tout à zéro sans lui. */
    const AntiLagState& antiLag() const { return antiLag_; }

private:
    struct AntiLagExhaust {
        AntiLagCommand command;
        double dt = 0.001;
        double engineAir = 0.0;   // kg/s
        double fuelPerAir = 0.0;
        double heatValue = 0.0;   // J/kg de carburant
        double gasCp = 0.0;
        double heatRiseK = 0.0;   // échauffement du gaz si tout le carburant lui revenait
    };
    /** Le gaz et sa température quand l'anti-lag s'en mêle : anti_lag.cpp. */
    void antiLagExhaust(const AntiLagExhaust& in, double& gas, double& hotTarget);

    TurboSpec spec_;
    TurboState state_;
    AntiLagState antiLag_;
    double lastGas_ = 0.0;
    bool antiLagOn_ = false;
    AntiLag antiLagKind_ = AntiLag::Off;
    double omegaSquared_ = 0.0;
    bool valveHeld_ = false;
};

/** Ce dont le calcul a besoin pour dimensionner le turbo. */
TurboSpec turboSpecFor(const EngineConfig& engine, double breathingAtRated);
/** L'équilibre à pleine charge, pour ce régime. */
TurboSteady turboSteady(const TurboSpec& spec, double rpm, double breathing);
/** Le couple perdu au point d'équilibre à cause de la contre-pression (Nm). */
double turboBackPressureNm(const TurboSpec& spec, double chargePa, double turbineInPa, double flow, double fill);

}  // namespace revforge
