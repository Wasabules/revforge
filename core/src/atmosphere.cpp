#include "revforge/atmosphere.hpp"

#include <cmath>

#include "acoustics.hpp"
#include "portable_math.hpp"

namespace revforge {

namespace {

// L'atmosphère type (ISA, OACI) dans la troposphère : gradient de 6,5 K/km, exposant g·M/(R·L).
constexpr double kIsaLapse = 2.25577e-5;
constexpr double kIsaExponent = 5.25588;
// Au-delà de 11 km, la tropopause : une autre loi. Ici, de quoi couvrir tout ce qui roule.
constexpr double kLowestM = -500.0;
constexpr double kHighestM = 11000.0;
constexpr double kColdestC = -60.0;
constexpr double kHottestC = 60.0;
/**
 * La part de l'écart avec les parois que l'air prend avant la fermeture de l'admission. Choisie pour que le remplissage suive
 * T^−0,5 à 20 °C, parois à 90 °C : (1 − ε)·T / (T + ε·(T_p − T)) = 0,5 donne ε = 0,447. L'air y gagne 31 K, l'ordre de
 * l'échauffement de la charge dans le conduit et au contact des gaz résiduels. Voir « L'air ambiant » dans docs/modele.md.
 */
constexpr double kWallHeatShare = 0.45;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

}  // namespace

double isaPressurePa(double altitudeM) {
    const double h = clamp(altitudeM, kLowestM, kHighestM);
    // À 0 m, (1 − 0)^n vaut exactement 1 : la pression de référence, au bit près.
    return kReferencePa * portable::pow(1.0 - kIsaLapse * h, kIsaExponent);
}

AmbientAir ambientAir(double celsius, double altitudeM) {
    AmbientAir a;
    a.celsius = clamp(celsius, kColdestC, kHottestC);
    a.altitudeM = clamp(altitudeM, kLowestM, kHighestM);
    a.pressureRatio = portable::pow(1.0 - kIsaLapse * a.altitudeM, kIsaExponent);
    a.pressurePa = kReferencePa * a.pressureRatio;
    // Le kelvin du modèle : 293 à 20 °C, puis un degré par degré. À 20 °C, exactement 293.
    a.temperatureK = kReferenceK + (a.celsius - kReferenceCelsius);
    a.temperatureRatio = a.temperatureK / kReferenceK;
    a.densityRatio = a.pressureRatio / a.temperatureRatio;
    a.soundSpeedRatio = std::sqrt(a.temperatureRatio);
    a.densityKgM3 = a.pressurePa / (kAirGasConstant * a.temperatureK);
    a.soundSpeedMs = kReferenceSoundMs * a.soundSpeedRatio;
    return a;
}

AmbientAir ambientAirOf(const EngineConfig& engine) { return ambientAir(static_cast<double>(engine.ambientC), static_cast<double>(engine.altitudeM)); }

bool referenceAirOf(const EngineConfig& engine) { return ambientAirOf(engine).reference(); }

double airDensityRatioOf(const EngineConfig& engine) {
    // Le cas courant, sans calcul : il sert à chaque pas de la conduite.
    if (engine.ambientC == static_cast<float>(kReferenceCelsius) && engine.altitudeM == 0) return 1.0;
    return ambientAirOf(engine).densityRatio;
}

EngineConfig withReferenceAir(EngineConfig engine) {
    engine.ambientC = static_cast<float>(kReferenceCelsius);
    engine.altitudeM = 0;
    return engine;
}

double trappedAirK(double airK, double wallK) { return airK + kWallHeatShare * (wallK - airK); }

double trappedAirShare(double pressureRatio, double referenceK, double airK, double wallK) {
    return pressureRatio * trappedAirK(referenceK, wallK) / trappedAirK(airK, wallK);
}

double ambientAbsorptionDbPerM(const AmbientAir& air, double hz) {
    return acoustics::airAbsorptionDbPerM(hz, air.celsius, kReferenceHumidityPercent, air.pressurePa / 1000.0);
}

AirAcoustics airAcousticsOf(const AmbientAir& air) {
    AirAcoustics a;
    a.soundSpeedMs = static_cast<float>(air.soundSpeedMs);
    a.soundSpeedRatio = static_cast<float>(air.soundSpeedRatio);
    a.densityRatio = static_cast<float>(air.densityRatio);
    a.celsius = air.celsius;
    a.kilopascal = air.pressurePa / 1000.0;
    return a;
}

}  // namespace revforge
