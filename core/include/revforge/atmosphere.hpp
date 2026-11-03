#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * L'air ambiant : la température choisie et la pression de l'atmosphère type (ISA) à l'altitude choisie. Toutes les
 * calibrations du modèle sont faites dans l'air de référence, 20 °C au niveau de la mer : à ces conditions, chaque grandeur
 * ci-dessous vaut exactement la constante que le calcul utilisait avant elle, et les rapports valent exactement 1.
 * Voir « L'air ambiant » dans docs/modele.md.
 */
inline constexpr double kReferencePa = 101325.0;
/** 20 °C : le kelvin du modèle, à 0,15 K près, celui que supposaient tous ses calculs. */
inline constexpr double kReferenceK = 293.0;
inline constexpr double kReferenceCelsius = 20.0;
/** La vitesse du son dans l'air de référence (m/s). */
inline constexpr double kReferenceSoundMs = 343.0;
inline constexpr double kAirGasConstant = 287.0;
/** Humidité relative supposée pour l'absorption de l'air (ISO 9613-1) : celle des mesures de référence. */
inline constexpr double kReferenceHumidityPercent = 50.0;
/** Les parois de la culasse d'un moteur chaud : l'eau à 90 °C, la valeur de référence du moteur chaud. */
inline constexpr double kWarmWallK = 363.0;

struct AmbientAir {
    double celsius = kReferenceCelsius;
    double altitudeM = 0.0;
    /** Pression (Pa), atmosphère type : p = 101 325·(1 − 2,25577·10⁻⁵·h)^5,25588. */
    double pressurePa = kReferencePa;
    /** Température (K), dans le kelvin du modèle : 293 à 20 °C. */
    double temperatureK = kReferenceK;
    /** δ = p/p₀, θ = T/T₀, σ = ρ/ρ₀ = δ/θ, et √θ, le rapport des vitesses du son. Exactement 1 dans l'air de référence. */
    double pressureRatio = 1.0;
    double temperatureRatio = 1.0;
    double densityRatio = 1.0;
    double soundSpeedRatio = 1.0;
    /** Masse volumique (kg/m³), gaz parfait : p/(R·T). */
    double densityKgM3 = kReferencePa / (kAirGasConstant * kReferenceK);
    /** Vitesse du son (m/s), en √T : 343 dans l'air de référence. */
    double soundSpeedMs = kReferenceSoundMs;

    /** 20 °C au niveau de la mer : rien de ce qui dépend de l'air ne s'y exécute. */
    bool reference() const { return pressureRatio == 1.0 && temperatureRatio == 1.0; }
};

/** Pression de l'atmosphère type à cette altitude (Pa), valable dans la troposphère. Exactement 101 325 à 0 m. */
double isaPressurePa(double altitudeM);

/** L'air à cette température (°C) et à cette altitude (m). Bornée à la troposphère et à des températures de plein air. */
AmbientAir ambientAir(double celsius, double altitudeM);

/** L'air dans lequel tourne ce moteur : `ambientC` et `altitudeM`. */
AmbientAir ambientAirOf(const EngineConfig& engine);

/** Vrai dans l'air de référence : 20 °C au niveau de la mer. */
bool referenceAirOf(const EngineConfig& engine);

/** σ = ρ/ρ₀ de l'air du jour, exactement 1 dans l'air de référence : la traînée, et tout ce qui rayonne dans l'air. */
double airDensityRatioOf(const EngineConfig& engine);

/**
 * Ce réglage, ramené à l'air de référence. Pour dimensionner ce qui ne change pas avec l'air du jour : le turbo, le
 * compresseur, la boîte, l'embrayage, le convertisseur et la carte de passage sont des pièces et un étalonnage.
 */
EngineConfig withReferenceAir(EngineConfig engine);

/**
 * La température de l'air piégé dans le cylindre (K) : l'air admis, réchauffé par les parois du conduit et de la chambre avant
 * la fermeture de l'admission, T = T_air + ε·(T_paroi − T_air). Un air plus chaud reçoit moins des parois : le remplissage suit
 * T^−0,5 près de 20 °C, comme le montrent les essais (Taylor ; SAE J1349). `wallK` : l'eau du moteur, chaud par défaut.
 */
double trappedAirK(double airK, double wallK = kWarmWallK);

/**
 * L'air piégé à ces conditions, rapporté à celui de la référence : (p / p_réf)·T_piégé(T_réf)/T_piégé(T). `pressureRatio` :
 * pression à l'admission, rapportée à celle de la référence ; `referenceK` et `airK` : l'air admis dans les deux cas.
 */
double trappedAirShare(double pressureRatio, double referenceK, double airK, double wallK = kWarmWallK);

/** Absorption de l'air ambiant (dB/m) à cette fréquence : ISO 9613-1, à l'humidité de référence. */
double ambientAbsorptionDbPerM(const AmbientAir& air, double hz);

/** Ce que le son doit savoir de l'air : vitesse du son, masse volumique, température et pression. */
AirAcoustics airAcousticsOf(const AmbientAir& air);

}  // namespace revforge
