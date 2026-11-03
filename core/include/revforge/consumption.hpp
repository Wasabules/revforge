#pragma once

#include <array>

#include "revforge/carburetor.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

struct Powertrain;

/** Ce que brûle le moteur. */
struct FuelSpec {
    double densityKgPerL;
    double lowerHeatingJPerG;
    double co2GPerG;          // CO₂ émis par gramme brûlé : la teneur en carbone, rien d'autre
    double stoichiometricAfr;  // grammes d'air par gramme de carburant, à richesse 1
};

const FuelSpec& fuelSpecOf(Fuel fuel);

/**
 * Ce que dépense ce moteur à chaque instant : un bilan d'énergie, calé sur le couple qu'il donne vraiment.
 *
 *   puissance du carburant = (couple au frein + frottements + pompage derrière le papillon) × ω / rendement indiqué
 *
 * Voir « La consommation » dans docs/modele.md. Rien ici ne revient vers la physique : ces calculs lisent, ils
 * n'écrivent pas. Pas d'allocation, et pas de bibliothèque mathématique en dehors de `sqrt`.
 */
class ConsumptionModel {
public:
    void configure(const Powertrain& powertrain);

    /** Carburant demandé (g/s) par ce couple au frein, frottements et pleine charge donnés. 0 si le moteur est entraîné. */
    double fuelGps(double rpm, double brakeNm, double frictionNm, double fullGrossNm) const;
    /** Air admis (g/s) selon la seconde méthode : cylindrée, régime, remplissage. Sert à vérifier la première. */
    double airGps(double rpm, double load, double fullGrossNm) const;
    /** Huile brûlée (g/s), un ordre de grandeur. */
    double oilGps(double rpm, double brakeNm) const;
    /** Électrique : puissance tirée de la batterie (W), négative quand elle récupère. */
    double batteryW(double motorNm, double omega) const;

    /** Pleine charge à ce régime sans turbo, frottements compris (Nm) : c'est la pression atmosphérique. */
    double naturalGrossNm(double rpm) const;

    bool electric() const { return electric_; }
    const FuelSpec& fuel() const { return *fuel_; }

private:
    static constexpr int kCurvePoints = 33;

    const FuelSpec* fuel_ = nullptr;
    bool electric_ = false;
    bool diesel_ = false;
    bool turbo_ = false;
    double litres_ = 2.0;
    double oilPerKrpm_ = 1.2;
    double redline_ = 6000.0;
    double baseEfficiency_ = 0.4;
    double fullRichness_ = 1.0;
    double throttlingNm_ = 0.0;
    double peakNaturalNm_ = 1.0;
    double peakGrossW_ = 1.0;
    std::array<double, kCurvePoints> naturalCurve_{};
    // Carburateur : son mélange, ce que le circuit de ralenti débite encore pied levé, et l'air que le venturi retire au remplissage.
    bool carburetor_ = false;
    CarburetorSpec carbSpec_;
    double idleCircuitGps_ = 0.0;
    double idleRpm_ = 1.0;
    double carbAirShare_ = 1.0;
    // L'air du jour, rapporté à l'air de référence : ce qu'il reste d'air dans un litre de cylindrée (atmosphere.hpp).
    double ambientAirShare_ = 1.0;
};

/** L'état du moteur à ce pas, tel que `DrivingModel` le donne. */
struct ConsumptionInput {
    double rpm = 0.0;
    double omega = 0.0;
    double speedMs = 0.0;
    double brakeNm = 0.0;      // couple au frein que la combustion demande
    double frictionNm = 0.0;
    double fullGrossNm = 0.0;  // ce que donnerait la pleine charge à ce régime et à cette pression de turbo
    double load = 0.0;         // remplissage du modèle
    double motorNm = 0.0;      // électrique : couple de la machine, avant ses frottements
    double pumpGps = 0.0;      // carburateur : ce que donne en plus la pompe de reprise
    bool burning = false;
    double extraFuelGps = 0.0;  // l'anti-lag : carburant au-delà de ce que demande le cylindre
    double extraAirGps = 0.0;   // l'anti-lag : air qui n'est pas passé par les cylindres
    double enrichment = 0.0;    // moteur froid : carburant ajouté, en part de celui qui brûle, qui repart imbrûlé
};

/**
 * Le compteur : intègre les débits à chaque pas du modèle. Le trajet se remet à zéro à la demande, et de lui-même quand
 * le moteur change. Il ne dépend ni du temps réel ni de la taille des blocs, puisque le modèle avance à pas fixes.
 */
class ConsumptionMeter {
public:
    void configure(const Powertrain& powertrain) { model_.configure(powertrain); }
    void resetTrip();
    void step(const ConsumptionInput& in, double dt);

    const ConsumptionModel& model() const { return model_; }

    double fuelGps() const { return fuelGps_; }
    double fuelLph() const;
    /** En roulant seulement : à l'arrêt, des L/100 km ne veulent rien dire, et la lecture vaut 0. */
    double fuelLper100km() const;
    double fuelTripL() const;
    double fuelAvgLper100km() const;
    double tripKm() const { return distanceM_ / 1000.0; }
    /** Moyenne du trajet (g/km), tirée de la teneur en carbone du carburant. */
    double co2GPerKm() const;

    double oilGph() const { return oilGps_ * 3600.0; }
    double oilTripMl() const;
    double oilMlPer1000km() const;

    /** Électrique, à la batterie : négatif en récupération. */
    double energyKwhPer100km() const;
    double energyTripKwh() const { return energyJ_ / 3.6e6; }
    double energyAvgKwhPer100km() const;

    /** Vérification : rapport air/carburant du trajet, d'après les deux méthodes, en richesse 1/λ. 0 si rien n'a brûlé. */
    double lambdaAverage() const;
    double lambdaNow() const;

private:
    ConsumptionModel model_;
    double speedMs_ = 0.0;
    double fuelGps_ = 0.0;
    double airGps_ = 0.0;
    double oilGps_ = 0.0;
    double batteryW_ = 0.0;
    double fuelG_ = 0.0;
    double airG_ = 0.0;
    double oilG_ = 0.0;
    double energyJ_ = 0.0;
    double distanceM_ = 0.0;
};

/** Le point le plus sobre de la carte du moteur, et ce qu'il dépense à pleine puissance. */
struct FuelMapSummary {
    double bestGPerKwh = 0.0;
    double bestRpm = 0.0;
    double bestBmepBar = 0.0;
    double peakPowerGPerKwh = 0.0;
};

/** Consommation spécifique (g/kWh) à ce régime et à ce couple au frein, turbo stabilisé. */
double brakeSpecificFuel(const Powertrain& powertrain, double rpm, double brakeNm);
/** Balaye la carte. Pas pour le temps réel. */
FuelMapSummary fuelMapSummary(const Powertrain& powertrain);

}  // namespace revforge
