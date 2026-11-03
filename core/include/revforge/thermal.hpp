#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

struct Powertrain;

/**
 * Les températures du moteur : un réseau de capacités (métal, eau côté moteur, eau du radiateur, huile) relié par des
 * conductances. Voir « La thermique » dans docs/modele.md.
 *
 * Le moteur chaud de référence est celui sur lequel tout le reste est calé : eau à 90 °C, huile à 100 °C. Les effets d'un
 * moteur froid (frottements, ralenti, enrichissement) ne sont que des écarts à cette référence, nuls quand elle est
 * atteinte. Pas d'allocation, et pas de bibliothèque mathématique en dehors de `sqrt` : `DrivingModel` intègre ce réseau.
 */
inline constexpr double kWarmCoolantC = 90.0;
inline constexpr double kWarmOilC = 100.0;

// ── Fonctions pures, pour la combustion, l'injection, le calculateur et les frottements ──

/** Viscosité dynamique de l'huile (Pa·s), loi de Vogel μ = a·exp(b / (T + c)), calée sur une 5W-30 du commerce. */
double oilViscosityPaS(double oilC);
/**
 * Les frottements hydrodynamiques du moteur chaud, par tour par minute (Nm par tr/min) : paliers de vilebrequin et de
 * bielle, jupes de piston, dans la forme du modèle de Patton, Nitschke et Heywood. Ils sont proportionnels au régime.
 */
double hydrodynamicNmPerRpm(const EngineConfig& engine);
/**
 * Ce que l'huile froide ajoute à ces frottements, en part de leur valeur à chaud : (μ/μ₁₀₀)^0,5 − 1, à la façon de
 * Sandoval et Heywood. Nul dès 100 °C : au-delà, le modèle de frottement d'origine, calé à chaud, reste tel quel.
 */
double coldViscosityExcess(double oilC);
/** Les frottements que l'huile froide ajoute à ce régime (Nm). 0 à chaud, et sur un moteur électrique. */
double coldFrictionNm(const Powertrain& p, double oilC, double rpm);
/** Les frottements à cette température d'huile, rapportés à ceux du moteur chaud : 1 à chaud. */
double frictionFactor(const Powertrain& p, double oilC, double rpm);
/** Le ralenti que vise le calculateur, en part du ralenti chaud ajoutée : 0 dès 70 °C d'eau. */
double coldIdleRise(double coolantC);
/**
 * Le carburant qu'ajoute le calculateur, ou le starter d'un carburateur, en part de celui qui brûle : ce qui reste liquide
 * sur les parois froides et repart imbrûlé. 0 dès 70 °C d'eau, et sur un diesel.
 */
double coldEnrichment(const EngineConfig& engine, double coolantC);
/**
 * La part de l'énergie du carburant qui passe aux parois, donc à l'eau : la corrélation de Taylor et Toong (Nu ∝ Re^0,75)
 * la fait baisser comme le débit à la puissance −0,25. `fuelShare` est le débit de carburant rapporté à celui de la
 * puissance maximale ; une paroi froide reçoit un peu plus.
 */
double wallHeatShare(double fuelShare, double metalC);
/** Efficacité d'un échangeur à courants croisés, les deux fluides non brassés (ε-NTU). */
double crossFlowEffectiveness(double ntu, double capacityRatio);

/** Ce qui découle du moteur et de la voiture : masses, échanges, radiateur. */
struct ThermalSpec {
    // Capacités (J/K).
    double metalJK = 30000.0;
    double coolantJK = 9000.0;
    double radiatorJK = 7000.0;
    double oilJK = 6000.0;
    // Conductances (W/K) : métal et eau, métal et huile (au régime de référence), métal et huile vers l'air (à l'arrêt, puis par m/s).
    double metalCoolantWK = 800.0;
    double metalOilWK = 150.0;
    double metalAirWK = 5.0;
    double metalAirWKPerMs = 0.6;
    double oilAirWK = 1.5;
    double oilAirWKPerMs = 0.4;
    double radiatorStillWK = 8.0;
    // La pompe à eau : débit de capacité (W/K) par tr/min, et la fuite du calorstat fermé.
    double pumpWKPerRpm = 1.2;
    // Le radiateur : section frontale (m²), conductance au point de dimensionnement (W/K) et débit d'air de ce point (kg/s).
    double coreM2 = 0.2;
    double uaDesignWK = 600.0;
    double airDesignKgS = 1.0;
    /** Ce que le ventilateur fait passer à travers le faisceau, à l'arrêt (m/s). 0 : il est en panne. */
    double fanMs = 3.0;
    // Énergie du carburant à la puissance maximale (W) : l'échelle du débit pour la part qui passe aux parois.
    double ratedFuelW = 150000.0;
};

ThermalSpec thermalSpecOf(const Powertrain& p, double ratedFuelW);

/** Ce que le moteur donne au réseau à ce pas. */
struct ThermalInput {
    double fuelW = 0.0;      // énergie du carburant qui brûle dans les cylindres
    double frictionW = 0.0;  // puissance des frottements : de la chaleur dans l'huile et le métal
    double rpm = 0.0;
    double speedMs = 0.0;
    double ambientC = 20.0;
};

class ThermalNetwork {
public:
    void configure(const ThermalSpec& spec) { spec_ = spec; }
    /** Moteur chaud (eau 90 °C, huile 100 °C) ou froid, tout à l'air ambiant. */
    void reset(StartTemp start, double ambientC);
    void step(const ThermalInput& in, double dt);

    double coolantC() const { return coolantC_; }
    double oilC() const { return oilC_; }
    double metalC() const { return metalC_; }
    /** L'eau du radiateur, celle qui en sort : c'est elle que lit le thermocontact du ventilateur. */
    double radiatorC() const { return radiatorC_; }
    /** Levée du calorstat, 0 fermé à 1 grand ouvert. */
    double thermostat() const { return lift_; }
    bool fan() const { return fan_; }
    /** Chaleur rendue à l'air par le radiateur, et reçue par les parois (W). */
    double radiatorW() const { return radiatorW_; }
    double wallW() const { return wallW_; }
    const ThermalSpec& spec() const { return spec_; }

private:
    ThermalSpec spec_;
    double metalC_ = kWarmCoolantC;
    double coolantC_ = kWarmCoolantC;
    double radiatorC_ = 55.0;
    double oilC_ = kWarmOilC;
    double lift_ = 0.0;
    bool fan_ = false;
    double radiatorW_ = 0.0;
    double wallW_ = 0.0;
};

}  // namespace revforge
