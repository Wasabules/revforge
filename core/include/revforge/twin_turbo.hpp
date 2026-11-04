#pragma once

#include <array>

#include "revforge/engine_config.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/turbo.hpp"

namespace revforge {

struct Powertrain;

/**
 * Les montages à plus d'un organe, et la géométrie variable : deux turbos en parallèle ou en séquence, un compresseur et un
 * turbo en série (twincharger), des aubes devant la turbine. Le turbo simple à soupape reste `TurboCharger`, au bit près :
 * ce module ne s'exécute que si `usesTwinTurbo` le demande.
 *
 * Chaque arbre a son bilan de puissance, avec les lois de turbo.cpp (`turbo_laws`) :
 *
 *   J_i·ω_i·dω_i/dt = P_turbine,i − P_compresseur,i − P_paliers,i
 *
 * Les compresseurs soufflent dans un même volume avant le papillon, chacun par son conduit et son inertie d'air : c'est
 * ce volume commun qui les couple, et qui peut faire pomper le plus faible. Voir « Deux turbos » dans docs/modele.md.
 */
inline bool usesTwinTurbo(const EngineConfig& e) {
    return e.aspiration == Aspiration::Turbo && (e.turboLayout != TurboLayout::Single || e.variableGeometry);
}

struct TwinTurboSpec {
    TurboLayout layout = TurboLayout::Single;
    bool variableGeometry = false;
    bool diesel = false;
    double displacementM3 = 0.002;
    double targetBar = 1.0;
    double redlineRpm = 6800.0;
    /** Le turbo simple de même pression, sur tout le moteur : la ligne, l'échangeur commun, la contre-pression de référence. */
    TurboSpec reference;
    /** Les arbres : primaire, puis secondaire. Un seul sur le twincharger ou un turbo simple à géométrie variable. */
    int units = 1;
    std::array<TurboSpec, 2> unit{};
    /** Part des cylindres qui soufflent dans chaque turbine, collecteurs séparés (parallèle). */
    std::array<double, 2> gasShare{1.0, 0.0};
    /**
     * Le gaz de chaque turbine, en part de son écart à l'air : une rangée aux collecteurs plus longs le refroidit un peu plus.
     * C'est ce qui fait tourner deux turbos identiques à des vitesses différentes.
     */
    std::array<double, 2> heatShare{1.0, 1.0};
    double chargeVolumeM3 = 0.006;
    double blowOffArea = 4.5e-4;
    bool blowOffValve = true;

    // ── Séquentiel : le débit d'air du moteur (kg/s) qui lance, puis ouvre, puis referme le secondaire ──
    double preSpoolFlow = 0.0;
    double switchFlow = 0.0;
    double returnFlow = 0.0;
    /** Le volume entre le compresseur secondaire et sa vanne d'admission (m³), et la section de son retour à l'entrée (m²). */
    double secondaryVolumeM3 = 3e-4;
    double recirculationArea = 5e-4;

    // ── Twincharger ──
    SuperchargerSpec charger;
    /**
     * Sous `turboAloneRpm`, le turbo seul ne tient pas la pression demandée : le compresseur est embrayé dès qu'on la demande.
     * Au-dessus, seulement s'il en manque. Au-delà de `clutchOffRpm`, jamais : ses rotors y atteignent leur vitesse maximale.
     */
    double turboAloneRpm = 2400.0;
    double clutchOffRpm = 3500.0;
    /** Inertie des rotors (kg·m²) et couple que l'embrayage transmet en glissant (Nm, côté rotors). */
    double rotorInertia = 4e-4;
    double clutchNm = 10.0;
    /** Le volume entre le compresseur et le turbo (m³), et la section du volet qui court-circuite le compresseur (m²). */
    double interstageVolumeM3 = 1.5e-3;
    double flapArea = 2.5e-3;

    // ── Géométrie variable : section de la turbine, en part de celle d'une turbine fixe de même roue ──
    double vaneMin = 1.0;
    double vaneMax = 1.0;
};

/** Ce qui change d'une milliseconde à l'autre. */
struct TwinTurboState {
    /** Chaque arbre, avec les grandeurs communes recopiées : pression de charge, papillon, échangeur, débit du moteur. */
    std::array<TurboState, 2> unit{};
    /** Aubes : section de la turbine, en part d'une turbine fixe. 1 sans géométrie variable. */
    std::array<double, 2> vane{1.0, 1.0};
    /** Séquentiel : 0 le primaire seul, 1 le secondaire lancé à part, 2 les deux en charge. */
    int stage = 0;
    /** Vanne d'échappement du secondaire (0 fermée), et sa vanne d'admission, un clapet qui s'ouvre sur la charge. */
    double exhaustValve = 0.0;
    bool secondaryOnline = false;
    double secondaryPa = 101325.0;
    double recirculationFlow = 0.0;
    /** Twincharger : l'embrayage, les rotors, le volet de dérivation (1 ouvert), le volume entre les deux étages. */
    bool clutch = false;
    bool clutchLocked = false;
    double rotorOmega = 0.0;
    double flap = 1.0;
    double flapFlow = 0.0;     // kg/s rendus à l'entrée du compresseur, négatif quand l'air le contourne
    double chargerFlow = 0.0;  // kg/s
    double interstagePa = 101325.0;
    double interstageK = 293.0;
    double chargerOutK = 293.0;
    double chargerNm = 0.0;    // couple pris au vilebrequin, glissement compris
    double slipW = 0.0;        // chaleur dans l'embrayage qui glisse
    double flapOpenFor = 0.0;  // secondes passées volet grand ouvert, embrayage fermé
    /** L'air qui entre dans l'échangeur commun : le mélange des compresseurs qui débitent sur la charge (K, kg/s). */
    double coolerInK = 293.0;
    double coolerFlow = 0.0;
};

struct TwinTurboInput {
    double rpm = 0.0;
    double fill = 0.0;
    double breathing = 1.0;
    bool burning = false;
    /** La pression demandée (bar relatifs) : négative, celle du réglage. La régulation de la pression la donnera. */
    double targetBar = -1.0;
    /** L'ouverture voulue des soupapes de décharge (ou des aubes) : négative, le ressort d'aujourd'hui. */
    double wastegate = -1.0;
    /** La vitesse de la voiture (m/s), dont l'air traverse l'échangeur air-air. Par défaut, la soufflerie du banc de la fiche. */
    double coolingAirMs = kChargeReferenceSpeedMs;
    /** Le banc d'étalonnage de la régulation : le faisceau commun déjà stabilisé, comme pour un turbo seul (`TurboInput::settledCooler`). */
    bool settledCooler = false;
};

/** L'équilibre à pleine charge : ce que donnent les courbes de la fiche. */
struct TwinTurboSteady {
    double boost = 0.0;
    double chargePa = 101325.0;
    double flow = 0.0;
    /** Couple perdu par la contre-pression, et pris par le compresseur d'un twincharger (Nm). */
    double backPressureNm = 0.0;
    double chargerNm = 0.0;
    int stage = 0;
    bool clutch = false;
    /** Ce que l'air-air ε-NTU change au couple de base, calibré sur l'échangeur d'origine (`turboCoolerTorqueShare`). */
    double coolShare = 1.0;
    std::array<double, 2> shaftRpm{0.0, 0.0};
    std::array<double, 2> turbineInPa{101325.0, 101325.0};
    std::array<double, 2> vane{1.0, 1.0};
};

class TwinTurbo {
public:
    /** Garde l'état : changer un réglage en marche ne doit pas arrêter les arbres. */
    void setSpec(const TwinTurboSpec& spec);
    void step(double dt, const TwinTurboInput& input);

    const TwinTurboSpec& spec() const { return spec_; }
    const TwinTurboState& state() const { return state_; }
    /** Pression avant le papillon, en part de la pression demandée. */
    double boost() const;
    /** Couple que coûte la contre-pression au moteur (Nm), cylindre par cylindre devant sa turbine. */
    double backPressureNm(double fill) const;
    /** Twincharger : ce que le compresseur prend au vilebrequin (Nm), glissement de l'embrayage compris. */
    double chargerDriveNm() const { return state_.chargerNm; }
    bool hasCharger() const { return spec_.layout == TurboLayout::Twincharger; }
    /** Le son d'un arbre : 0 le primaire, 1 le secondaire. Sans second arbre, tout à zéro. */
    TurboDrive drive(int unit) const;
    SuperchargerDrive chargerDrive() const;

private:
    void stepCharger(double dt, double rpm, double fill, double targetPa);
    void stepSequence(double dt, double fill);

    TwinTurboSpec spec_;
    TwinTurboState state_;
    std::array<double, 2> omegaSquared_{0.0, 0.0};
    /** Le gaz qui arrive à chaque turbine et à sa soupape (kg/s). */
    std::array<double, 2> gas_{0.0, 0.0};
    bool valveHeld_ = false;
    /** Séquentiel : la vanne qui rend l'air du secondaire à son entrée (1 ouverte). */
    double recirculation_ = 1.0;
    /** Twincharger : l'entrée du compresseur et le régime, pour son son. */
    double lastInletPa_ = 101325.0;
    double lastRpm_ = 0.0;
    /** Le faisceau de l'échangeur air-air commun (finitions). */
    AirCoolerCore core_;
};

/** Dimensionne le montage pour ce moteur (voir « Deux turbos » dans docs/modele.md). */
TwinTurboSpec twinTurboSpecOf(const Powertrain& p);
/** L'équilibre à pleine charge, à ce régime. */
TwinTurboSteady twinTurboSteady(const TwinTurboSpec& spec, double rpm, double breathing);
/** Pour `Powertrain` : courbe de couple, pression d'équilibre, perte du filtre, quand `usesTwinTurbo`. */
double twinTorqueCurveNm(const Powertrain& p, double rpm);
double twinSteadyBoost(const Powertrain& p, double rpm);
double twinIntakeDropPa(const Powertrain& p, double rpm);
/** Le son d'un twincharger : ses poches et son rapport, posés sur le profil comme pour un compresseur seul. */
void twinchargerProfile(const Powertrain& p, EngineProfile& profile);
/**
 * Ce que `TwinTurboInput::wastegate` doit valoir pour tenir `targetBar` à pleine charge stabilisée, à ce régime : l'ouverture
 * commune des soupapes, ou la course des aubes (0 fermées, 1 grandes ouvertes). La précommande de la régulation électronique,
 * qui pilote les deux soupapes par une seule électrovanne (boost_control.cpp). 0 si la pression n'est pas atteinte.
 */
double twinSteadyGate(const TwinTurboSpec& spec, double rpm, double breathing, double targetBar);

}  // namespace revforge
