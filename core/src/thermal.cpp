#include "revforge/thermal.hpp"

#include <algorithm>
#include <cmath>

#include "portable_math.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/powertrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;

// Vogel, ajustée sur trois points d'une 5W-30 synthétique (Motorcraft, fiche « Product typicals ») : 60 mm²/s à 40 °C,
// 10,5 mm²/s à 100 °C, 4250 mPa·s à −30 °C (viscosité au démarreur, CCS) ; densité 0,853 à 15,6 °C, dilatation de 6,5·10⁻⁴/K.
constexpr double kVogelA = 3.476900e-5;  // Pa·s
constexpr double kVogelB = 1345.5610;    // K
constexpr double kVogelC = 144.8707;     // °C
// Patton, Nitschke et Heywood (SAE 890836) : paliers 3,03·10⁻⁴ · N · D³ · L · n / (B² · S · n_c) et jupes 294 · v̄p / B, en kPa,
// millimètres, tr/min et m/s. Coefficients tels que les reproduisent les travaux qui en partent, non relus dans l'article.
constexpr double kBearingKpa = 3.03e-4;
constexpr double kSkirtKpa = 294.0;
// Dimensions des paliers, rapportées à l'alésage (ordre de grandeur d'un moteur de série : 50 × 19 mm pour 75 mm d'alésage).
constexpr double kMainDiameter = 0.67;
constexpr double kMainWidth = 0.25;
constexpr double kRodDiameter = 0.56;
constexpr double kRodWidth = 0.22;
// Le même quart de frottements en moins sur un rotatif que dans `frictionTorqueNm` (supposé).
constexpr double kRotaryShare = 0.75;

// Le calculateur de la Saab 9-5 (Trionic) vise 1200 tr/min à −30 °C d'eau, puis 825 tr/min une fois chaud. Fin supposée à 70 °C.
constexpr double kIdleRiseAtMinus30 = 1200.0 / 825.0 - 1.0;
constexpr double kWarmUpEndC = 70.0;
// Enrichissement supposé : λ ≈ 0,87 à 20 °C d'eau avec un calculateur, 0,77 avec un starter, qui n'a pas de sonde.
constexpr double kEnrichPerK = 0.15 / (kWarmUpEndC - 20.0);
constexpr double kChokePerK = 0.30 / (kWarmUpEndC - 20.0);

// Part de l'énergie du carburant qui va aux parois au quart du débit maximal, métal chaud : dans le bilan de Heywood
// (17 à 26 % à l'eau à pleine charge sur un moteur à essence), une fois les frottements ôtés. Gaz à 1100 °C en moyenne (supposé).
constexpr double kWallShareRef = 0.28;
constexpr double kWallFlowRef = 0.25;
constexpr double kWallShareMax = 0.5;
constexpr double kEffectiveGasC = 1100.0;
constexpr double kWarmMetalC = 100.0;
// Chaleur des frottements : paliers, distribution et pompe à huile la donnent à l'huile, environ 45 % du total dans la répartition
// de Patton et al. ; pistons et segments la donnent aux chemises, donc au métal (ordre de grandeur).
constexpr double kFrictionToOil = 0.45;

// Calorstat à cire : il commence à s'ouvrir à 87 °C, l'une des trois valeurs courantes (82, 87, 92 °C), et il lui faut 8 K pour
// s'ouvrir en grand, comme celui du Land Rover Freelander 2 (83 à 91 °C, manuel d'atelier). Le jeu de la cire et du siège le
// referme 2 K plus bas (supposé). Son orifice de purge laisse passer 0,5 % du débit (supposé).
constexpr double kThermostatOpenC = 87.0;
constexpr double kThermostatFullC = 95.0;
constexpr double kThermostatPlayK = 2.0;
constexpr double kThermostatLeak = 0.005;
// Thermocontact au bas du radiateur : 95 °C en marche, 84 °C à l'arrêt (VW T3, contact simple).
constexpr double kFanOnC = 95.0;
constexpr double kFanOffC = 84.0;
// L'eau glycolée à 50 % bout vers 129 °C sous le bouchon de 1,4 bar : au-delà, elle part, et le réseau ne suit plus.
constexpr double kBoilC = 129.0;
constexpr double kCoolantJLK = 3640.0;  // 1,07 kg/L × 3,4 kJ/(kg·K)
constexpr double kAirCp = 1006.0;
// Ce qui traverse le faisceau : une part de la vitesse de la voiture, et le ventilateur (supposés).
constexpr double kRamShare = 0.3;
constexpr double kFanMs = 3.0;
// Le tirage naturel d'un faisceau chaud, voiture arrêtée et ventilateur coupé (supposé).
constexpr double kDraftMs = 0.3;
// Le côté air domine : h ∝ Re^0,6 sur des ailettes à persiennes.
constexpr double kAirExponent = 0.6;
// Dimensionnement : 60 % de l'énergie du carburant maximale à 50 km/h, 40 °C dehors, eau à 110 °C en entrée, ventilateur en
// marche, pompe à 3500 tr/min ; efficacité de 0,6 à ce point. Tout cela est supposé : un motoriste vise un cas de ce genre.
constexpr double kDesignLoad = 0.6;
constexpr double kDesignFrictionShare = 0.05;
constexpr double kDesignSpeedMs = 50.0 / 3.6;
constexpr double kDesignAmbientC = 40.0;
constexpr double kDesignCoolantC = 110.0;
constexpr double kDesignRpm = 3500.0;
constexpr double kDesignEffectiveness = 0.6;
// La pompe à eau : 0,25 L/s par litre de cylindrée et par 1000 tr/min (environ 2 L/s à 6000 tr/min sur un 1,4 L, supposé).
constexpr double kPumpLPerSPerLitreKrpm = 0.25;
// L'huile passe sur le métal d'autant plus que la pompe tourne vite.
constexpr double kOilFlowRefRpm = 3000.0;
// L'échangeur eau-huile à plaques, monté sur le petit circuit d'eau (il est traversé calorstat fermé), comme sur la plupart des
// moteurs de série depuis les années 1990. Sans lui, le réseau laissait l'huile à 111-126 °C sur le WLTC, quand un carter
// mesure 90 à 110 °C (voir « La thermique » dans docs/modele.md). Dans un échangeur à plaques, l'huile reste laminaire : son
// coefficient d'échange dépend peu du débit, et la conductance est tenue fixe. 150 W/K par litre de cylindrée (supposé :
// 2 à 4 kW rejetés pour 20 à 30 K d'écart sur un 1,4 à 2 L, l'ordre de grandeur des échangeurs de 6 à 12 plaques).
constexpr double kOilCoolerUaPerLitre = 150.0;
// La pompe à huile, volumétrique : 0,12 L/s par litre et par 1000 tr/min (30 L/min à 3000 tr/min sur un 1,4 L), jusqu'à son
// clapet de décharge, vers 3000 tr/min (ordres de grandeur). 0,85 kg/L, 2 kJ/(kg·K).
constexpr double kOilPumpLPerSPerLitreKrpm = 0.12;
constexpr double kOilReliefRpm = 3000.0;
constexpr double kOilJLK = 1700.0;
// La part de l'eau de la pompe qui passe dans l'échangeur, en dérivation (supposé).
constexpr double kOilCoolerWaterShare = 0.15;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

double airDensity(double celsius) { return 101325.0 / (287.05 * (celsius + 273.15)); }

double thermostatCurve(double celsius) { return clamp((celsius - kThermostatOpenC) / (kThermostatFullC - kThermostatOpenC), 0.0, 1.0); }

}  // namespace

double oilViscosityPaS(double oilC) {
    const double above = std::max(oilC + kVogelC, 20.0);
    return kVogelA * portable::exp(kVogelB / above);
}

double hydrodynamicNmPerRpm(const EngineConfig& e) {
    const double bore = e.effectiveBoreMm();
    const double stroke = isRotary(e) ? bore : static_cast<double>(e.strokeMm());
    const double cylinders = static_cast<double>(std::max(e.cylinders, 1));
    // En ligne (et VR, sous une seule culasse), un palier entre chaque cylindre ; en V, à plat ou en W, entre chaque paire.
    const bool single = e.layout == Layout::Inline || e.layout == Layout::Vr || isRotary(e);
    const double mains = single ? cylinders + 1.0 : std::floor(cylinders / 2.0) + 1.0;
    const double main = kMainDiameter * bore;
    const double rod = kRodDiameter * bore;
    const double journals = main * main * main * kMainWidth * bore * mains + rod * rod * rod * kRodWidth * bore * cylinders;
    const double bearingsKpa = kBearingKpa * journals / (bore * bore * stroke * cylinders);
    // Vitesse moyenne du piston : 2 · S · N / 60, en m/s pour S en mm.
    const double skirtKpa = isRotary(e) ? 0.0 : kSkirtKpa * stroke / 30000.0 / bore;
    const double nmPerKpa = 1000.0 * e.sweptCcPerTwoTurns() * 1e-6 / (4.0 * kPi);
    return (bearingsKpa + skirtKpa) * nmPerKpa * (isRotary(e) ? kRotaryShare : 1.0);
}

double coldViscosityExcess(double oilC) {
    // La référence est exacte : aucun écart calculé à 100 °C. Au-delà, l'huile plus fluide retire sa part.
    if (oilC == kWarmOilC) return 0.0;
    return std::sqrt(oilViscosityPaS(oilC) / oilViscosityPaS(kWarmOilC)) - 1.0;
}

double coldFrictionNm(const Powertrain& p, double oilC, double rpm) {
    if (p.electric) return 0.0;
    return hydrodynamicNmPerRpm(p.engine) * std::max(rpm, 0.0) * coldViscosityExcess(oilC);
}

double frictionFactor(const Powertrain& p, double oilC, double rpm) {
    const double warm = frictionTorqueNm(p, rpm);
    return warm > 0.0 ? 1.0 + coldFrictionNm(p, oilC, rpm) / warm : 1.0;
}

double coldIdleRise(double coolantC) {
    return kIdleRiseAtMinus30 / (kWarmUpEndC + 30.0) * std::max(kWarmUpEndC - coolantC, 0.0);
}

double coldEnrichment(const EngineConfig& e, double coolantC) {
    if (e.burnsDiesel()) return 0.0;
    const double perK = fuelSystemOf(e) == FuelSystem::Injection ? kEnrichPerK : kChokePerK;
    return perK * std::max(kWarmUpEndC - coolantC, 0.0);
}

double wallHeatShare(double fuelShare, double metalC) {
    const double flow = std::max(fuelShare, 0.005);
    const double scaling = portable::pow(flow / kWallFlowRef, -0.25);
    const double wall = (kEffectiveGasC - metalC) / (kEffectiveGasC - kWarmMetalC);
    return clamp(kWallShareRef * scaling * wall, 0.0, kWallShareMax);
}

double counterFlowEffectiveness(double ntu, double capacityRatio) {
    if (ntu <= 0.0) return 0.0;
    if (capacityRatio > 0.999) return ntu / (1.0 + ntu);
    const double e = portable::exp(-ntu * (1.0 - capacityRatio));
    return (1.0 - e) / (1.0 - capacityRatio * e);
}

double crossFlowEffectiveness(double ntu, double capacityRatio) {
    if (ntu <= 0.0) return 0.0;
    if (capacityRatio < 1e-6) return 1.0 - portable::exp(-ntu);
    const double inner = portable::exp(-capacityRatio * portable::pow(ntu, 0.78)) - 1.0;
    return 1.0 - portable::exp(portable::pow(ntu, 0.22) / capacityRatio * inner);
}

ThermalSpec thermalSpecOf(const Powertrain& p, double ratedFuelW) {
    ThermalSpec s;
    // La taille du moteur : sa cylindrée réelle, celle d'un rotor compris, plutôt que le volume balayé en deux tours.
    const double litres = std::max(static_cast<double>(p.engine.displacementCc) / 1000.0, 0.3);
    const double area = portable::pow(litres, 2.0 / 3.0);
    // Masses supposées : 30 kg de bloc et de culasse par litre (fonte et aluminium, 600 J/(kg·K)), côté eau ; 1,2 L d'eau plus 1 L
    // par litre côté moteur, 1 L plus 0,6 L par litre dans le radiateur ; 2,5 L d'huile plus 0,8 L par litre (0,85 kg/L,
    // 2 kJ/(kg·K)), avec ce qu'elle mouille et qui la suit : vilebrequin, bielles, carter, bas du bloc, 15 kg par litre (500 J/(kg·K)).
    s.metalJK = 18000.0 * litres;
    s.coolantJK = (1.2 + litres) * kCoolantJLK;
    s.radiatorJK = (1.0 + 0.6 * litres) * kCoolantJLK;
    s.oilJK = (2.5 + 0.8 * litres) * 1700.0 + 7500.0 * litres;
    // Chemises d'eau : 3000 W/(m²·K) sur une surface en litres^(2/3) (supposé). Carter et bloc dans le compartiment : 8 W/(m²·K) à l'arrêt.
    s.metalCoolantWK = 700.0 * area;
    s.metalOilWK = 150.0 * area;
    s.metalAirWK = 0.5 * area * 8.0;
    s.metalAirWKPerMs = 0.5 * area * 1.0;
    s.oilAirWK = 0.15 * area * 8.0;
    s.oilAirWKPerMs = 0.15 * area * 2.0;
    s.pumpWKPerRpm = kCoolantJLK * kPumpLPerSPerLitreKrpm * litres / 1000.0;
    s.oilCoolerUaWK = kOilCoolerUaPerLitre * litres;
    s.oilPumpWKPerRpm = kOilJLK * kOilPumpLPerSPerLitreKrpm * litres / 1000.0;
    s.oilCoolerWaterShare = kOilCoolerWaterShare;
    s.ratedFuelW = std::max(ratedFuelW, 1000.0);

    // Le radiateur, dimensionné pour son cas le plus dur : le débit d'air vient de l'efficacité visée, la conductance de ε-NTU.
    const double rejected = (wallHeatShare(kDesignLoad, kWarmMetalC) + kDesignFrictionShare) * kDesignLoad * s.ratedFuelW;
    const double approach = kDesignCoolantC - kDesignAmbientC;
    const double airWK = rejected / (kDesignEffectiveness * approach);
    const double coolantWK = s.pumpWKPerRpm * kDesignRpm;
    const double coreMs = std::sqrt(kRamShare * kDesignSpeedMs * kRamShare * kDesignSpeedMs + kFanMs * kFanMs);
    s.airDesignKgS = airWK / kAirCp;
    s.coreM2 = s.airDesignKgS / (airDensity(kDesignAmbientC) * coreMs);
    const double cMin = std::min(airWK, coolantWK);
    const double cRatio = cMin / std::max(airWK, coolantWK);
    const double wanted = std::min(rejected / (cMin * approach), 0.95);
    double low = 0.0, high = 20.0;
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (crossFlowEffectiveness(middle, cRatio) < wanted ? low : high) = middle;
    }
    s.uaDesignWK = 0.5 * (low + high) * cMin;
    // Le faisceau sans débit d'eau : le tirage naturel le traverse, et l'eau y est à une seule température (ε = 1 − e^−NTU).
    const double draftKgS = airDensity(20.0) * s.coreM2 * kDraftMs;
    const double draftUa = s.uaDesignWK * portable::pow(draftKgS / s.airDesignKgS, kAirExponent);
    s.radiatorStillWK = (1.0 - portable::exp(-draftUa / (draftKgS * kAirCp))) * draftKgS * kAirCp;
    s.fanMs = kFanMs;
    return s;
}

double parkedSeconds(StartTemp start) {
    switch (start) {
        case StartTemp::Warm: return 0.0;
        case StartTemp::HalfHour: return 1800.0;
        case StartTemp::TwoHours: return 7200.0;
        case StartTemp::Cold: break;
    }
    return -1.0;
}

void ThermalNetwork::reset(StartTemp start, double ambientC) {
    if (start == StartTemp::Cold) {
        metalC_ = coolantC_ = radiatorC_ = oilC_ = ambientC;
        lift_ = 0.0;
    } else {
        coolantC_ = kWarmCoolantC;
        oilC_ = kWarmOilC;
        metalC_ = kWarmCoolantC + 5.0;
        radiatorC_ = ambientC + 0.5 * (kWarmCoolantC - ambientC);
        lift_ = thermostatCurve(coolantC_);
    }
    fan_ = false;
    radiatorW_ = 0.0;
    wallW_ = 0.0;
    oilCoolerW_ = 0.0;
    // Un arrêt d'une durée donnée : le moteur chaud, puis le refroidissement moteur coupé du même réseau. Aucune courbe saisie.
    if (start == StartTemp::HalfHour || start == StartTemp::TwoHours) soak(parkedSeconds(start), ambientC);
}

void ThermalNetwork::soak(double seconds, double ambientC) {
    // Le pas le plus court du réseau moteur coupé est l'échange du métal et de l'eau : 8 à 11 s selon la cylindrée. Un pas de
    // 1 s le suit sans osciller, et l'écart au pas de 10 ms du modèle de conduite reste sous le centième de degré (test).
    constexpr double kSoakStepS = 1.0;
    if (!(seconds > 0.0)) return;
    ThermalInput still;
    still.ambientC = ambientC;
    double left = seconds;
    while (left > kSoakStepS) {
        step(still, kSoakStepS);
        left -= kSoakStepS;
    }
    step(still, left);
    // Arrêté, rien ne passe au radiateur ni aux parois : ce que le réseau montre est ce qu'il vient de rendre à l'air.
    wallW_ = 0.0;
    oilCoolerW_ = 0.0;
}

void ThermalNetwork::place(double coolantC, double oilC, double metalC) {
    coolantC_ = coolantC;
    oilC_ = oilC;
    metalC_ = metalC;
    radiatorC_ = coolantC;
    lift_ = thermostatCurve(coolantC);
}

void ThermalNetwork::step(const ThermalInput& in, double dt) {
    const ThermalSpec& s = spec_;
    const double ambient = in.ambientC;
    const double rpm = std::max(in.rpm, 0.0);
    const double speed = std::max(in.speedMs, 0.0);

    // Le calorstat : la cire suit l'eau, le jeu fait l'hystérésis.
    const double opening = thermostatCurve(coolantC_);
    const double closing = thermostatCurve(coolantC_ + kThermostatPlayK);
    lift_ = std::min(std::max(lift_, opening), closing);
    const double flowWK = s.pumpWKPerRpm * rpm * (kThermostatLeak + (1.0 - kThermostatLeak) * lift_);
    if (radiatorC_ >= kFanOnC) fan_ = true;
    else if (radiatorC_ <= kFanOffC) fan_ = false;

    // Le radiateur : ε-NTU, courants croisés, l'air qui traverse le faisceau du côté froid.
    const double ram = kRamShare * speed;
    const double coreMs = std::sqrt(ram * ram + (fan_ ? s.fanMs * s.fanMs : 0.0) + kDraftMs * kDraftMs);
    const double rho = s.airPressureRatio == 1.0 ? airDensity(ambient) : airDensity(ambient) * s.airPressureRatio;
    const double airKgS = rho * s.coreM2 * coreMs;
    const double airWK = airKgS * kAirCp;
    double radiator = 0.0;
    if (flowWK > 0.0) {
        const double ua = s.uaDesignWK * portable::pow(airKgS / s.airDesignKgS, kAirExponent);
        const double cMin = std::min(flowWK, airWK);
        const double cMax = std::max(flowWK, airWK);
        radiator = crossFlowEffectiveness(ua / cMin, cMin / cMax) * cMin * (coolantC_ - ambient);
    } else {
        // Moteur arrêté, pompe immobile : l'eau du faisceau refroidit sur place, dans le tirage naturel.
        radiator = s.radiatorStillWK * (radiatorC_ - ambient);
    }
    radiatorW_ = radiator;

    wallW_ = in.fuelW > 0.0 ? wallHeatShare(in.fuelW / s.ratedFuelW, metalC_) * in.fuelW : 0.0;
    const double toOil = kFrictionToOil * in.frictionW;
    const double toMetal = in.frictionW - toOil;
    const double metalOil = s.metalOilWK * (0.3 + 0.7 * std::min(rpm / kOilFlowRefRpm, 2.0));
    const double metalCoolant = s.metalCoolantWK * (metalC_ - coolantC_);
    const double metalToOil = metalOil * (metalC_ - oilC_);
    const double metalToAir = (s.metalAirWK + s.metalAirWKPerMs * speed) * (metalC_ - ambient);
    const double oilToAir = (s.oilAirWK + s.oilAirWKPerMs * speed) * (oilC_ - ambient);
    const double exchanged = flowWK * (coolantC_ - radiatorC_);
    // L'échangeur eau-huile : à contre-courant, entre le débit de la pompe à huile et la dérivation de la pompe à eau. Il tient
    // l'huile près de l'eau, et la réchauffe par elle au départ à froid.
    double oilCooler = 0.0;
    if (s.oilCoolerUaWK > 0.0 && rpm > 0.0) {
        const double oilWK = s.oilPumpWKPerRpm * std::min(rpm, kOilReliefRpm);
        const double waterWK = s.pumpWKPerRpm * rpm * s.oilCoolerWaterShare;
        const double cMin = std::min(oilWK, waterWK);
        const double cMax = std::max(oilWK, waterWK);
        oilCooler = counterFlowEffectiveness(s.oilCoolerUaWK / cMin, cMin / cMax) * cMin * (oilC_ - coolantC_);
    }
    oilCoolerW_ = oilCooler;

    metalC_ += (wallW_ + toMetal - metalCoolant - metalToOil - metalToAir) / s.metalJK * dt;
    coolantC_ = std::min(coolantC_ + (metalCoolant - exchanged + oilCooler) / s.coolantJK * dt, kBoilC);
    radiatorC_ += (exchanged - radiator) / s.radiatorJK * dt;
    oilC_ += (toOil + metalToOil - oilToAir - oilCooler) / s.oilJK * dt;
}

}  // namespace revforge
