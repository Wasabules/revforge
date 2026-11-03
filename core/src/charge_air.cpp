#include "revforge/charge_air.hpp"

#include <cmath>

#include "portable_math.hpp"
#include "revforge/powertrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAirCp = 1005.0;
constexpr double kAirR = 287.0;

// ── L'échangeur d'origine, recopié de turbo.cpp et supercharger.cpp ──
constexpr double kCoolerEffectLow = 0.86;
constexpr double kCoolerEffectFall = 0.14;

// ── L'air de route ──
// Une calandre, un radiateur derrière : la face ne voit passer qu'une part de la vitesse (supposé, 0,2 à 0,4 selon les mesures
// en soufflerie). À l'arrêt, le ventilateur du radiateur moteur tire encore de l'air à travers elle (supposé, 1 m/s).
constexpr double kRamShare = 0.3;
constexpr double kFanFaceMs = 1.0;

// ── Air-air (ε-NTU) ──
// Un faisceau de 600 × 300 mm pour 0,21 kg/s (2 L, 1,3 bar) : la face suit le débit nominal (ordre de grandeur des séries).
constexpr double kFacePerFlow = 0.85;

// ── Air-eau ──
// Une brique dessinée pour retirer 85 % de l'écart au débit nominal (supposé : une brique à eau est plus dense qu'un faisceau à air).
constexpr double kBrickEffect = 0.85;
// L'eau mouille mieux que l'air : côté liquide, quatre fois la conductance du côté air au nominal (supposé).
constexpr double kBrickWaterShare = 4.0;
// La pompe électrique : 24 L/min pour 0,21 kg/s d'air, deux kilos de liquide par kilo d'air (ordre de grandeur des pompes de 50 à
// 100 W). Moins, le liquide ne porte plus la chaleur au nominal : il s'échauffe de plus de 20 K dans la brique, et aucun radiateur
// de cette taille ne le ramène assez près de l'air.
constexpr double kPumpPerFlow = 2.0;
constexpr double kCoolantCp = 3500.0;  // eau et glycol à 50 %
// Le radiateur basse température : 0,15 m² pour 0,21 kg/s, côté liquide trois fois le côté air au point de référence (supposé).
constexpr double kRadiatorFacePerFlow = 0.7;
constexpr double kRadiatorWaterShare = 3.0;
// La boucle : 3 L de liquide, 3,5 kg d'aluminium (brique, radiateur, tubes) pour 0,21 kg/s d'air, soit 68 kJ/K par kg/s.
constexpr double kLoopPerFlow = (3.2 * kCoolantCp + 3.5 * 900.0) / 0.21;
// Ce que le compartiment rend à la boucle : brique contre le collecteur, tubes, de l'ordre de 15 W/K pour 0,21 kg/s (supposé).
constexpr double kSoakPerFlow = 70.0;
// Le compartiment à l'arrêt : la moitié de l'écart entre l'eau du moteur et l'air (35 K moteur chaud) ; la moitié de cela à 36 km/h.
constexpr double kBaySoakShare = 0.5;
constexpr double kBaySpeedMs = 10.0;

// ── Le chemin de l'air, pour 2 L : longueurs (m), section rapportée au conduit, coudes. Recopié par `fluidGas.ts`. ──
constexpr double kPipeFriction = 0.02;  // tube lisse, comme `kColdPipeFriction` de turbo.cpp
constexpr double kBendLoss = 0.3;       // coude à 90°, rayon de 1,5 diamètre
constexpr double kReferenceDuctM2 = 0.7854 * 0.055 * 0.055;
// Turbo : tube chaud, faisceau à l'avant, tube froid qui remonte. Air-eau : une brique près du papillon, un tube froid court.
// Aucun : un seul tube, du compresseur au papillon.
constexpr ChargePath kTurboStock{{0.45, 1.0, 2, false}, {0.45, 4.0, 0, true}, {0.5, 1.0, 2, false}, 1.0};
constexpr ChargePath kTurboWater{{0.45, 1.0, 2, false}, {0.25, 4.0, 0, true}, {0.2, 1.0, 1, false}, 0.5};
constexpr ChargePath kTurboNone{{0.45, 1.0, 2, false}, {0.0, 1.0, 0, true}, {0.35, 1.0, 1, false}, 0.0};
// Compresseur : posé sur le collecteur, l'échangeur d'origine est déjà une brique courte. Air-eau : la même, refroidie à l'eau.
constexpr ChargePath kChargerStock{{0.2, 1.0, 1, false}, {0.2, 4.0, 0, true}, {0.2, 1.0, 1, false}, 1.0};
constexpr ChargePath kChargerNone{{0.2, 1.0, 1, false}, {0.0, 1.0, 0, true}, {0.2, 1.0, 1, false}, 0.0};

// ── L'eau ──
// h_fg = a − b·T : 2,501 MJ/kg à 0 °C, 2,257 MJ/kg à 100 °C, à 0,5 % près (tables de vapeur).
constexpr double kWaterLatentA = 3.1615e6;
constexpr double kWaterLatentB = 2386.0;
constexpr double kVapourR = 461.5;
constexpr double kBoilK = 373.15;
constexpr double kBoilPa = 101325.0;
constexpr double kWaterCp = 4186.0;
constexpr double kVapourPerAirMolar = 0.622;  // 18,015 / 28,97
// La pompe débite à partir de ce remplissage, et tout au-delà de celui-ci (supposé : l'eau ne sert qu'à pleine charge).
constexpr double kWaterFromFill = 0.8;
constexpr double kWaterFullFill = 0.95;

// ── Le protoxyde (ESDU 91022) ──
constexpr double kN2oCriticalK = 309.57;
constexpr double kN2oCriticalPa = 7.251e6;
constexpr double kN2oCriticalKgM3 = 452.0;
constexpr double kN2oMolarKg = 0.044013;
// 2 N₂O → 2 N₂ + O₂ : 32 g d'oxygène pour 88 g, 82,05 kJ/mol libérés (enthalpie de formation du N₂O gazeux).
constexpr double kOxygenInNitrous = 31.998 / (2.0 * 44.013);
constexpr double kOxygenInAir = 0.2314;
constexpr double kNitrousDecompositionJPerKg = 82.05e3 / kN2oMolarKg;
constexpr double kNitrousVapourCp = 880.0;
constexpr double kAirMolarKg = 0.02897;
constexpr double kPetrolMolarKg = 0.1;     // ordre de grandeur d'une essence vaporisée
constexpr double kPetrolLiquidCp = 2000.0;
// Les kits enrichissent : 6 kg de protoxyde par kg d'essence, contre 9,4 à la stœchiométrie (recommandation courante des fabricants).
constexpr double kNitrousPerFuel = 6.0;
// Une bouteille de 10 lb, remplie au taux réglementaire de 0,68 kg par litre de capacité ; 6,8 kg d'aluminium (ordre de grandeur).
constexpr double kBottleFillKg = 4.536;
constexpr double kBottleFillingRatio = 680.0;  // kg par m³ de capacité
constexpr double kBottleShellJK = 6.8 * 900.0;
// Le liquide : 2 à 3 kJ/(kg·K) entre 0 et 20 °C (supposé 2,5). La paroi : convection naturelle sur 0,33 m² (supposé 6 W/(m²·K)).
constexpr double kNitrousLiquidCp = 2500.0;
constexpr double kBottleWallWK = 2.0;
// La vapeur seule, par un col sonique : ṁ = Cd·A·√(γ·ρ·p)·(2/(γ+1))^((γ+1)/(2(γ−1))), avec γ = 1,3 ; le col l'est sous 0,546.
constexpr double kNitrousChokedRatio = 0.546;
constexpr double kNitrousChokedFlux = 0.6672;  // √1,3 × 0,8696^3,833
// Le contacteur de plein gaz, et la fenêtre de régime : pas sous 3000 tr/min, où le couple ajouté, qui suit 1/ω, casse des
// bielles (recommandation courante des fabricants) ; coupé juste avant le rupteur.
constexpr double kNitrousPedal = 0.95;
constexpr double kNitrousMinRpm = 3000.0;
constexpr double kNitrousMinShare = 0.4;
constexpr double kNitrousCutoffShare = 0.97;

double clampTo(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }
double positive(double v) { return v > 0.0 ? v : 0.0; }

/** ṁ^0,8 sur le côté de la charge : Dittus-Boelter. Nul à débit nul. */
double chargeSideHa(const ChargeCoolerSpec& spec, double flow) {
    if (flow <= 0.0) return 0.0;
    return spec.chargeHaW * portable::pow(flow / spec.ratedFlow, 0.8);
}

/** Deux résistances en série. */
double series(double a, double b) { return a > 0.0 && b > 0.0 ? a * b / (a + b) : 0.0; }

/** Ce que reçoit le fluide chaud, rapporté à C_chaud·(Tchaud − Tfroid) : l'efficacité vue du côté de la charge. */
double hotSideEffectiveness(double ua, double hotCapacity, double coldCapacity) {
    if (hotCapacity <= 0.0) return 1.0;
    if (ua <= 0.0 || coldCapacity <= 0.0) return 0.0;
    const double cMin = hotCapacity < coldCapacity ? hotCapacity : coldCapacity;
    const double cMax = hotCapacity < coldCapacity ? coldCapacity : hotCapacity;
    return crossflowEffectiveness(ua / cMin, cMin / cMax) * cMin / hotCapacity;
}

double roadSideHa(double haAtReference, double faceM2, double speedMs, const ChargeConditions& c) {
    const double reference = roadAirFlowKgS(faceM2, kChargeReferenceSpeedMs, c.ambientK, c.ambientPa);
    return haAtReference * std::sqrt(roadAirFlowKgS(faceM2, speedMs, c.ambientK, c.ambientPa) / reference);
}

/** Une dichotomie sur une conductance pour atteindre une efficacité qui croît avec elle. */
template <typename F>
double solveConductance(F effectivenessFor, double wanted, double high) {
    double low = 0.0;
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (effectivenessFor(middle) < wanted ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

double pathVolume(const ChargePath& p) { return p.hot.lengthM * p.hot.areaShare + p.core.lengthM * p.core.areaShare + p.cold.lengthM * p.cold.areaShare; }

/** La perte des tubes (sans faisceau) : Darcy et coudes, en coefficient de ½ρv² sur la section du conduit. */
double pipeLossCoefficient(const ChargePath& p, double ductAreaM2) {
    const double diameter = std::sqrt(ductAreaM2 / 0.7854);
    // Les longueurs sont celles d'un 2 L : elles suivent le diamètre, comme le reste du chemin.
    const double scale = diameter / std::sqrt(kReferenceDuctM2 / 0.7854);
    const double length = (p.hot.lengthM + p.cold.lengthM) * scale;
    return kPipeFriction * length / diameter + kBendLoss * static_cast<double>(p.hot.bends + p.cold.bends);
}

}  // namespace

// ── ε-NTU ──

double crossflowEffectiveness(double ntu, double capacityRatio) {
    if (ntu <= 0.0) return 0.0;
    if (capacityRatio < 1e-6) return 1.0 - portable::exp(-ntu);
    const double inner = portable::exp(-capacityRatio * portable::pow(ntu, 0.78)) - 1.0;
    return 1.0 - portable::exp(portable::pow(ntu, 0.22) / capacityRatio * inner);
}

double roadAirFlowKgS(double faceM2, double speedMs, double ambientK, double ambientPa) {
    const double rho = ambientPa / (kAirR * ambientK);
    return rho * faceM2 * (kRamShare * positive(speedMs) + kFanFaceMs);
}

double stockCoolerOutletK(double hotK, double flowShare, double ambientK) {
    const double effect = kCoolerEffectLow - kCoolerEffectFall * clampTo(flowShare, 0.0, 1.5);
    return hotK - effect * (hotK - ambientK);
}

// ── Air-air ──

ChargeCoolerSpec airToAirCoolerFor(double ratedFlow, double ratedHotK, const ChargeConditions& c) {
    ChargeCoolerSpec s;
    s.kind = Intercooler::AirToAir;
    s.ratedFlow = ratedFlow;
    s.ratedHotK = ratedHotK;
    s.faceM2 = kFacePerFlow * ratedFlow;
    // Deux faces d'air, de même conductance au point de calcul : celle qui rend l'efficacité d'origine au nominal, à la vitesse de référence.
    const double hot = ratedFlow * kAirCp;
    const double cold = roadAirFlowKgS(s.faceM2, kChargeReferenceSpeedMs, c.ambientK, c.ambientPa) * kAirCp;
    const double wanted = kCoolerEffectLow - kCoolerEffectFall;
    const double ha = solveConductance([hot, cold](double side) { return hotSideEffectiveness(series(side, side), hot, cold); }, wanted, 1e5 * ratedFlow);
    s.chargeHaW = ha;
    s.roadHaW = ha;
    return s;
}

double airToAirOutletK(const ChargeCoolerSpec& spec, double hotK, double flow, double speedMs, const ChargeConditions& c) {
    if (flow <= 0.0) return hotK;
    const double road = roadAirFlowKgS(spec.faceM2, speedMs, c.ambientK, c.ambientPa);
    const double ua = series(chargeSideHa(spec, flow), roadSideHa(spec.roadHaW, spec.faceM2, speedMs, c));
    return hotK - hotSideEffectiveness(ua, flow * kAirCp, road * kAirCp) * (hotK - c.ambientK);
}

// ── Air-eau ──

double brickEffectiveness(const ChargeCoolerSpec& spec, double flow) {
    if (flow <= 0.0) return 1.0;
    return hotSideEffectiveness(series(chargeSideHa(spec, flow), spec.brickWaterHaW), flow * kAirCp, spec.coolantCapacityWK);
}

double radiatorConductanceWK(const ChargeCoolerSpec& spec, double speedMs, const ChargeConditions& c) {
    const double air = roadAirFlowKgS(spec.radiatorFaceM2, speedMs, c.ambientK, c.ambientPa) * kAirCp;
    const double ua = series(roadSideHa(spec.radiatorAirHaW, spec.radiatorFaceM2, speedMs, c), spec.radiatorWaterHaW);
    // Vue du liquide : ce qu'il cède, par kelvin d'écart entre son entrée et l'air.
    return hotSideEffectiveness(ua, spec.coolantCapacityWK, air) * spec.coolantCapacityWK;
}

double bayAirK(double speedMs, const ChargeConditions& c) {
    return c.ambientK + kBaySoakShare * (c.engineCoolantK - c.ambientK) * kBaySpeedMs / (positive(speedMs) + kBaySpeedMs);
}

double waterLoopSteadyK(const ChargeCoolerSpec& spec, double hotK, double flow, double speedMs, const ChargeConditions& c) {
    const double charge = brickEffectiveness(spec, positive(flow)) * positive(flow) * kAirCp;
    const double radiator = radiatorConductanceWK(spec, speedMs, c);
    return (charge * hotK + spec.soakWK * bayAirK(speedMs, c) + radiator * c.ambientK) / (charge + spec.soakWK + radiator);
}

namespace {

/**
 * Le dernier échangeur dimensionné. Les courbes de la fiche redemandent le même à chaque régime, et un compresseur mécanique
 * à chaque pas (`superchargerSpecOf`) : deux dichotomies imbriquées, une fois suffit. Rien d'autre ne garde d'état ici.
 */
struct CoolerMemo {
    bool valid = false;
    double ratedFlow = 0.0;
    double ratedHotK = 0.0;
    ChargeConditions conditions;
    ChargeCoolerSpec spec;
};
CoolerMemo g_waterCooler;

}  // namespace

ChargeCoolerSpec chargeCoolerFor(Intercooler kind, double ratedFlow, double ratedHotK, const ChargeConditions& c) {
    ChargeCoolerSpec s;
    s.kind = kind;
    s.ratedFlow = ratedFlow;
    s.ratedHotK = ratedHotK;
    if (kind != Intercooler::AirToWater) return s;
    CoolerMemo& memo = g_waterCooler;
    if (memo.valid && memo.ratedFlow == ratedFlow && memo.ratedHotK == ratedHotK && memo.conditions.ambientK == c.ambientK && memo.conditions.ambientPa == c.ambientPa &&
        memo.conditions.engineCoolantK == c.engineCoolantK) {
        return memo.spec;
    }
    // La brique : la conductance côté air qui retire 85 % de l'écart au nominal.
    s.coolantCapacityWK = kPumpPerFlow * ratedFlow * kCoolantCp;
    const double hot = ratedFlow * kAirCp;
    const double pump = s.coolantCapacityWK;
    s.chargeHaW = solveConductance([hot, pump](double side) { return hotSideEffectiveness(series(side, kBrickWaterShare * side), hot, pump); }, kBrickEffect, 1e5 * ratedFlow);
    s.brickWaterHaW = kBrickWaterShare * s.chargeHaW;
    s.loopHeatJK = kLoopPerFlow * ratedFlow;
    s.soakWK = kSoakPerFlow * ratedFlow;
    // Le radiateur : assez grand pour rendre, à l'équilibre, au nominal et à la vitesse de référence, la température de l'air-air d'origine.
    s.radiatorFaceM2 = kRadiatorFacePerFlow * ratedFlow;
    const double wanted = stockCoolerOutletK(ratedHotK, 1.0, c.ambientK);
    const double needed = (ratedHotK - wanted) / (ratedHotK - c.ambientK);
    ChargeCoolerSpec trial = s;
    s.radiatorAirHaW = solveConductance([&trial, ratedHotK, ratedFlow, &c](double side) {
        trial.radiatorAirHaW = side;
        trial.radiatorWaterHaW = kRadiatorWaterShare * side;
        const double outK = chargeCoolerSteadyK(trial, ratedHotK, ratedFlow, c);
        return (ratedHotK - outK) / (ratedHotK - c.ambientK);
    }, needed, 1e5 * ratedFlow);
    s.radiatorWaterHaW = kRadiatorWaterShare * s.radiatorAirHaW;
    memo.valid = true;
    memo.ratedFlow = ratedFlow;
    memo.ratedHotK = ratedHotK;
    memo.conditions = c;
    memo.spec = s;
    return s;
}

double chargeCoolerSteadyK(const ChargeCoolerSpec& spec, double hotK, double flow, const ChargeConditions& c) {
    switch (spec.kind) {
        case Intercooler::None: return hotK;
        case Intercooler::AirToWater: {
            const double water = waterLoopSteadyK(spec, hotK, flow, kChargeReferenceSpeedMs, c);
            return hotK - brickEffectiveness(spec, positive(flow)) * (hotK - water);
        }
        case Intercooler::AirToAir: break;
    }
    return stockCoolerOutletK(hotK, flow / spec.ratedFlow, c.ambientK);
}

// ── Le chemin de l'air ──

const ChargePath& chargePathOf(Intercooler kind, bool turbo) {
    if (turbo) return kind == Intercooler::None ? kTurboNone : (kind == Intercooler::AirToWater ? kTurboWater : kTurboStock);
    return kind == Intercooler::None ? kChargerNone : kChargerStock;
}

void applyChargePath(Intercooler kind, bool turbo, double ductAreaM2, double& volumeM3, double& coolerDrop) {
    const ChargePath& stock = chargePathOf(Intercooler::AirToAir, turbo);
    const ChargePath& path = chargePathOf(kind, turbo);
    if (&stock == &path) return;
    volumeM3 *= pathVolume(path) / pathVolume(stock);
    // Ce que coûte un coefficient K en Pa·(kg/m³)/(kg/s)² : Δp = K·ṁ²/(2ρA²).
    const double perK = 1.0 / (2.0 * ductAreaM2 * ductAreaM2);
    const double stockPipes = pipeLossCoefficient(stock, ductAreaM2) * perK;
    const double core = positive(coolerDrop - stockPipes);
    coolerDrop = core * path.coreDropShare + pipeLossCoefficient(path, ductAreaM2) * perK;
}

// ── L'eau ──

double waterLatentJPerKg(double kelvin) { return kWaterLatentA - kWaterLatentB * kelvin; }

double waterSaturationPa(double kelvin) {
    // d ln p / dT = h_fg / (R·T²), avec h_fg = a − b·T : intégrée depuis l'ébullition à 1 atm.
    const double t = clampTo(kelvin, 200.0, 450.0);
    const double exponent = kWaterLatentA / kVapourR * (1.0 / kBoilK - 1.0 / t) - kWaterLatentB / kVapourR * portable::log(t / kBoilK);
    return kBoilPa * portable::exp(exponent);
}

namespace {

/** L'eau que l'air peut porter à saturation (kg/kg d'air sec). Très grande quand l'eau bout à cette pression. */
double saturatedPerAir(double kelvin, double pa) {
    const double ps = waterSaturationPa(kelvin);
    if (ps >= 0.95 * pa) return 1e3;
    return kVapourPerAirMolar * ps / (pa - ps);
}

}  // namespace

WaterSpray waterSpray(double chargeK, double chargePa, double waterPerAir, double waterK) {
    WaterSpray out;
    out.outK = chargeK;
    if (waterPerAir <= 0.0 || chargePa <= 0.0) return out;
    // Ce que l'air cède = l'eau portée à la température de sortie + ce qui s'en évapore à cette température.
    const auto balance = [chargeK, chargePa, waterPerAir, waterK](double t) {
        double vapour = saturatedPerAir(t, chargePa);
        if (vapour > waterPerAir) vapour = waterPerAir;
        return kAirCp * (chargeK - t) - waterPerAir * kWaterCp * (t - waterK) - vapour * waterLatentJPerKg(t);
    };
    double low = 200.0;
    double high = chargeK > waterK ? chargeK : waterK;
    // 32 moitiés de 200 K : un millionième de kelvin.
    for (int i = 0; i < 32; ++i) {
        const double middle = 0.5 * (low + high);
        (balance(middle) > 0.0 ? low : high) = middle;
    }
    const double t = 0.5 * (low + high);
    double vapour = saturatedPerAir(t, chargePa);
    if (vapour > waterPerAir) vapour = waterPerAir;
    out.outK = t;
    out.vapourPerAir = vapour;
    out.liquidPerAir = waterPerAir - vapour;
    out.vapourPa = chargePa * vapour / (kVapourPerAirMolar + vapour);
    out.densityShare = (chargePa - out.vapourPa) / chargePa * chargeK / t;
    return out;
}

double waterPerAirAt(double fill, const ChargeFuel& fuel) {
    const double share = clampTo((fill - kWaterFromFill) / (kWaterFullFill - kWaterFromFill), 0.0, 1.0);
    return share * kWaterPerFuel / (fuel.stoichiometricAfr * fuel.fullLoadLambda);
}

// ── Le protoxyde ──

NitrousSaturation nitrousSaturation(double kelvin) {
    NitrousSaturation s;
    const double t = clampTo(kelvin, 190.0, kN2oCriticalK - 0.5);
    const double tr = t / kN2oCriticalK;
    const double below = 1.0 - tr;
    const double root = std::sqrt(below);
    const double third = portable::pow(below, 1.0 / 3.0);
    s.pressurePa = kN2oCriticalPa * portable::exp((-6.71893 * below + 1.35966 * below * root - 1.3779 * below * below * root - 4.051 * below * below * below * below * below) / tr);
    s.liquidKgM3 = kN2oCriticalKgM3 * portable::exp(1.72328 * third - 0.83950 * third * third + 0.51060 * below - 0.10412 * below * third);
    const double over = 1.0 / tr - 1.0;
    const double overThird = portable::pow(over, 1.0 / 3.0);
    s.vapourKgM3 = kN2oCriticalKgM3 * portable::exp(-1.00900 * overThird - 6.28792 * overThird * overThird + 7.50332 * over - 7.90463 * over * overThird + 0.629427 * over * overThird * overThird);
    const double liquidH = -200.0 + 116.043 * third - 917.225 * third * third + 794.779 * below - 589.587 * below * third;
    const double vapourH = -200.0 + 440.055 * third - 459.701 * third * third + 434.081 * below - 485.338 * below * third;
    s.latentJPerKg = (vapourH - liquidH) * 1000.0;
    return s;
}

NitrousBurn nitrousBurn(double nitrousKgS, double fuelKgS, const ChargeFuel& fuel) {
    NitrousBurn b;
    if (nitrousKgS <= 0.0) return b;
    b.oxygenKgS = nitrousKgS * kOxygenInNitrous;
    // L'oxygène du protoxyde brûle l'essence à la stœchiométrie ; le reste du gicleur part imbrûlé. Le protoxyde se décompose
    // tout entier dans la flamme, au-delà de 565 °C, et y rend sa chaleur de formation.
    const double stoichiometric = b.oxygenKgS / (fuel.stoichiometricAfr * kOxygenInAir);
    b.burntFuelKgS = fuelKgS < stoichiometric ? fuelKgS : stoichiometric;
    b.indicatedW = kNitrousIndicatedEfficiency * (b.burntFuelKgS * fuel.lowerHeatingJPerKg + nitrousKgS * kNitrousDecompositionJPerKg);
    return b;
}

NitrousKit nitrousKitFor(int kw, double redlineRpm, double ratedManifoldPa, const ChargeConditions& c, const ChargeFuel& fuel) {
    NitrousKit k;
    if (kw <= 0) return k;
    k.ratedKw = static_cast<double>(kw);
    // L'énergie d'un kilo de protoxyde, avec l'essence qu'il brûle : 6,4 MJ, dont 1,9 de sa décomposition.
    const double perKg = nitrousBurn(1.0, 1.0, fuel).indicatedW;
    k.nitrousKgS = k.ratedKw * 1000.0 / perKg;
    k.fuelKgS = k.nitrousKgS / kNitrousPerFuel;
    k.ratedManifoldPa = ratedManifoldPa;
    k.bottleM3 = kBottleFillKg / kBottleFillingRatio;
    k.fillKg = kBottleFillKg;
    k.shellJK = kBottleShellJK;
    // Le gicleur : un orifice où passe du liquide (modèle incompressible), dimensionné bouteille pleine à l'air ambiant.
    const NitrousSaturation s = nitrousSaturation(c.ambientK);
    const double across = s.pressurePa - ratedManifoldPa;
    k.jetCdAM2 = across > 0.0 ? k.nitrousKgS / std::sqrt(2.0 * s.liquidKgM3 * across) : 0.0;
    k.activationRpm = kNitrousMinShare * redlineRpm > kNitrousMinRpm ? kNitrousMinShare * redlineRpm : kNitrousMinRpm;
    k.cutoffRpm = kNitrousCutoffShare * redlineRpm;
    return k;
}

void settleBottle(NitrousBottle& b, const NitrousKit& kit) {
    const NitrousSaturation s = nitrousSaturation(b.kelvin);
    if (b.massKg <= 0.0 || kit.bottleM3 <= 0.0) {
        b.massKg = 0.0;
        b.liquidKg = 0.0;
        b.pressurePa = 101325.0;
        return;
    }
    // Le volume est plein : le liquide, et la vapeur saturée au-dessus.
    const double liquid = (b.massKg - s.vapourKgM3 * kit.bottleM3) / (1.0 - s.vapourKgM3 / s.liquidKgM3);
    if (liquid > 0.0) {
        b.liquidKg = liquid;
        b.pressurePa = s.pressurePa;
        return;
    }
    // Plus de liquide : la vapeur seule se détend, à peu près comme un gaz.
    b.liquidKg = 0.0;
    b.pressurePa = s.pressurePa * b.massKg / (kit.bottleM3 * s.vapourKgM3);
}

NitrousBottle fullBottle(const NitrousKit& kit, double ambientK) {
    NitrousBottle b;
    b.massKg = kit.fillKg;
    b.kelvin = ambientK;
    settleBottle(b, kit);
    return b;
}

double nitrousFlowKgS(const NitrousKit& kit, const NitrousBottle& b, double manifoldPa) {
    const double across = b.pressurePa - manifoldPa;
    if (across <= 0.0 || b.massKg <= 0.0 || kit.jetCdAM2 <= 0.0) return 0.0;
    if (b.liquidKg > 0.0) return kit.jetCdAM2 * std::sqrt(2.0 * nitrousSaturation(b.kelvin).liquidKgM3 * across);
    // De la vapeur seule : un gaz par le même gicleur, sonique tant que le collecteur est sous 0,55 fois la bouteille (γ ≈ 1,3).
    const double rho = b.massKg / kit.bottleM3;
    if (manifoldPa < kNitrousChokedRatio * b.pressurePa) return kit.jetCdAM2 * kNitrousChokedFlux * std::sqrt(rho * b.pressurePa);
    return kit.jetCdAM2 * std::sqrt(2.0 * rho * across);
}

void stepBottle(NitrousBottle& b, const NitrousKit& kit, double flowKgS, double dt, double ambientK) {
    if (kit.bottleM3 <= 0.0) return;
    double out = flowKgS * dt;
    if (out > b.massKg) out = b.massKg;
    double heat = kBottleWallWK * (ambientK - b.kelvin) * dt;
    if (b.liquidKg > 0.0 && out > 0.0) {
        // Le liquide parti laisse un volume que du liquide doit vaporiser pour le remplir : il prend sa chaleur à la bouteille.
        const NitrousSaturation s = nitrousSaturation(b.kelvin);
        heat -= out * s.vapourKgM3 / (s.liquidKgM3 - s.vapourKgM3) * s.latentJPerKg;
    }
    b.massKg -= out;
    b.kelvin += heat / (b.massKg * kNitrousLiquidCp + kit.shellJK);
    settleBottle(b, kit);
}

double nitrousMixK(double chargeK, double airKgS, double nitrousKgS, double fuelKgS, double bottleK, const ChargeFuel& fuel) {
    // Le liquide se vaporise à la température de la bouteille, puis la vapeur et l'essence rejoignent celle du mélange.
    const double latent = nitrousSaturation(bottleK).latentJPerKg;
    const double capacity = airKgS * kAirCp + nitrousKgS * kNitrousVapourCp + fuelKgS * kPetrolLiquidCp;
    if (capacity <= 0.0) return chargeK;
    const double t = (airKgS * kAirCp * chargeK + (nitrousKgS * kNitrousVapourCp + fuelKgS * kPetrolLiquidCp) * bottleK - nitrousKgS * latent - fuelKgS * fuel.latentJPerKg) / capacity;
    return t > 200.0 ? t : 200.0;
}

// ── La charge au collecteur ──

ManifoldCharge mixCharge(const ChargeMix& mix, const ChargeFuel& fuel) {
    ManifoldCharge m;
    m.pressurePa = mix.plenumPa;
    m.temperatureK = mix.chargeK;
    double airShare = 1.0;
    if (mix.nitrousKgS > 0.0 && mix.airKgS > 0.0) {
        const double cooled = nitrousMixK(mix.chargeK, mix.airKgS, mix.nitrousKgS, mix.nitrousFuelKgS, mix.bottleK, fuel);
        // À pression égale, le cylindre se partage entre les moles : le protoxyde et l'essence prennent leur place à l'air.
        const double air = mix.airKgS / kAirMolarKg;
        const double moles = air + mix.nitrousKgS / kN2oMolarKg + mix.nitrousFuelKgS / kPetrolMolarKg;
        airShare = mix.chargeK / cooled * air / moles;
        m.temperatureK = cooled;
        m.nitrousPerAir = mix.nitrousKgS / mix.airKgS;
        m.oxygenShare = (mix.airKgS * kOxygenInAir + mix.nitrousKgS * kOxygenInNitrous) / (mix.airKgS + mix.nitrousKgS);
    }
    if (mix.waterPerAir > 0.0) {
        const WaterSpray spray = waterSpray(m.temperatureK, mix.plenumPa, mix.waterPerAir, mix.waterK);
        m.temperatureK = spray.outK;
        m.waterPerAir = mix.waterPerAir;
        m.liquidWaterPerAir = spray.liquidPerAir;
        airShare *= spray.densityShare;
    }
    m.airShare = airShare;
    return m;
}

// ── Le moteur entier ──

bool chargeAirActive(const Powertrain& p) {
    if (p.electric) return false;
    const EngineConfig& e = p.engine;
    const bool boosted = e.aspiration != Aspiration::Natural;
    // L'eau et le protoxyde n'ont de sens que sur un moteur à étincelle : voir docs/modele.md.
    const bool spark = !e.burnsDiesel();
    return (boosted && e.intercooler != Intercooler::AirToAir) || (spark && (e.waterInjection || e.nitrousKw > 0));
}

Powertrain chargeAirStock(const Powertrain& p) {
    Powertrain stock = p;
    stock.engine.intercooler = Intercooler::AirToAir;
    stock.engine.waterInjection = false;
    stock.engine.nitrousKw = 0;
    return stock;
}

ChargeAirSetup chargeAirSetupOf(const Powertrain& p) {
    ChargeAirSetup s;
    if (!chargeAirActive(p)) return s;
    const EngineConfig& e = p.engine;
    s.active = true;
    s.turbo = e.aspiration == Aspiration::Turbo;
    s.supercharger = e.aspiration == Aspiration::Supercharger;
    if (s.turbo) s.cooler = turboSpecOf(p).cooler;
    else if (s.supercharger) s.cooler = superchargerSpecOf(p).cooler;
    const bool spark = !e.burnsDiesel();
    s.water = spark && e.waterInjection;
    if (spark && e.nitrousKw > 0) {
        // Les gicleurs se choisissent pour la pression du collecteur à pleine charge : l'atmosphère, plus celle du turbo.
        const double manifold = s.conditions.ambientPa + (e.aspiration == Aspiration::Natural ? 0.0 : static_cast<double>(e.boostBar) * 1e5);
        s.nitrous = nitrousKitFor(e.nitrousKw, static_cast<double>(e.redlineRpm), manifold, s.conditions, s.fuel);
    }
    return s;
}

namespace {

/** La charge à pleine charge, à l'équilibre : sortie de l'échangeur, et ce que suppose le couple de base. */
struct SteadyCharge {
    double chargeK = 293.0;
    double referenceK = 293.0;
    double plenumPa = 101325.0;
    double airKgS = 0.0;
};

SteadyCharge steadyChargeAt(const Powertrain& p, double rpm, const ChargeConditions& c) {
    SteadyCharge s;
    const EngineConfig& e = p.engine;
    if (e.aspiration == Aspiration::Turbo) {
        const TurboSpec spec = turboSpecOf(p);
        const TurboSteady t = turboSteady(spec, rpm, breathingOf(p, rpm));
        s.chargeK = t.coolK;
        // La courbe du turbo suppose l'échangeur d'origine : `kBoostEfficiency` le compte déjà.
        s.referenceK = spec.cooler.kind == Intercooler::AirToAir ? t.coolK : stockCoolerOutletK(t.outK, t.flow / spec.ratedFlow, c.ambientK);
        s.plenumPa = t.chargePa;
        s.airKgS = t.flow;
        return s;
    }
    if (e.aspiration == Aspiration::Supercharger) {
        // Celle du compresseur lit déjà la température de son échangeur, quel qu'il soit (`superchargerAirShare`).
        const SuperchargerSteady t = superchargerSteady(superchargerSpecOf(p), rpm, breathingOf(p, rpm));
        s.chargeK = t.coolK;
        s.referenceK = t.coolK;
        s.plenumPa = t.chargePa;
        s.airKgS = t.flow;
        return s;
    }
    s.chargeK = intakeAirK(p, rpm);
    s.referenceK = s.chargeK;
    s.plenumPa = c.ambientPa;
    // Comme `naturalFlowKgS` (powertrain.cpp) : 0,92 de remplissage au pic de couple, l'air à sa densité.
    s.airKgS = 0.92 * breathingOf(p, rpm) * c.ambientPa / (kAirR * s.chargeK) * e.sweptCcPerTwoTurns() * 1e-6 * rpm / 120.0;
    return s;
}

struct SteadyMix {
    ManifoldCharge manifold;
    double share = 1.0;
    double nitrousNm = 0.0;
};

SteadyMix steadyMix(const Powertrain& p, double rpm) {
    SteadyMix out;
    const ChargeAirSetup setup = chargeAirSetupOf(p);
    const SteadyCharge s = steadyChargeAt(p, rpm, setup.conditions);
    ChargeMix mix;
    mix.chargeK = s.chargeK;
    mix.plenumPa = s.plenumPa;
    mix.airKgS = s.airKgS;
    mix.waterPerAir = setup.water ? waterPerAirAt(1.0, setup.fuel) : 0.0;
    mix.bottleK = setup.conditions.ambientK;
    mix.waterK = setup.conditions.ambientK;
    const NitrousKit& kit = setup.nitrous;
    if (kit.ratedKw > 0.0 && rpm >= kit.activationRpm && rpm <= kit.cutoffRpm) {
        // La fiche suppose la bouteille pleine, à l'air ambiant.
        mix.nitrousKgS = nitrousFlowKgS(kit, fullBottle(kit, setup.conditions.ambientK), s.plenumPa);
        mix.nitrousFuelKgS = kit.fuelKgS;
        out.nitrousNm = nitrousBurn(mix.nitrousKgS, mix.nitrousFuelKgS, setup.fuel).indicatedW / (rpm * 2.0 * kPi / 60.0);
    }
    out.manifold = mixCharge(mix, setup.fuel);
    out.share = s.referenceK / s.chargeK * out.manifold.airShare;
    return out;
}

}  // namespace

double chargeAirTorqueNm(const Powertrain& p, double rpm, double baseNm) {
    if (rpm <= 0.0) return baseNm;
    const SteadyMix m = steadyMix(p, rpm);
    // La densité ne change que le travail des gaz : les frottements restent. Le protoxyde s'ajoute, à sa propre combustion.
    const double friction = frictionTorqueNm(p, rpm);
    return (baseNm + friction) * m.share - friction + m.nitrousNm;
}

ManifoldCharge steadyManifoldCharge(const Powertrain& p, double rpm) { return steadyMix(p, rpm > 1.0 ? rpm : 1.0).manifold; }

// ── L'état de conduite ──

void ChargeAir::configure(const ChargeAirSetup& setup, bool fresh) {
    const bool newKit = setup.nitrous.ratedKw != setup_.nitrous.ratedKw || setup.nitrous.jetCdAM2 != setup_.nitrous.jetCdAM2;
    setup_ = setup;
    if (fresh) waterK_ = setup.conditions.ambientK;
    if (fresh || newKit) refill();
    nitrousOn_ = false;
    nitrousKgS_ = 0.0;
    nitrousNm_ = 0.0;
    waterGps_ = 0.0;
    torqueShare_ = 1.0;
}

void ChargeAir::refill() {
    bottle_ = setup_.nitrous.ratedKw > 0.0 ? fullBottle(setup_.nitrous, setup_.conditions.ambientK) : NitrousBottle{};
}

double ChargeAir::stepCooler(double dt, double hotK, double flow, double speedMs) {
    const ChargeCoolerSpec& spec = setup_.cooler;
    if (spec.kind == Intercooler::AirToAir) return 0.0;
    if (spec.kind == Intercooler::None) return hotK;
    const ChargeConditions& c = setup_.conditions;
    // La boucle : ce que la brique prend à la charge, ce que le radiateur rend à l'air, ce que le compartiment lui donne.
    const double through = positive(flow);
    const double effect = brickEffectiveness(spec, through);
    const double fromCharge = effect * through * kAirCp * (hotK - waterK_);
    const double toRoad = radiatorConductanceWK(spec, speedMs, c) * (waterK_ - c.ambientK);
    const double fromBay = spec.soakWK * (bayAirK(speedMs, c) - waterK_);
    waterK_ += dt * (fromCharge - toRoad + fromBay) / spec.loopHeatJK;
    return hotK - effect * (hotK - waterK_);
}

void ChargeAir::step(const ChargeAirInput& in) {
    const NitrousKit& kit = setup_.nitrous;
    double nitrousFuel = 0.0;
    nitrousOn_ = false;
    nitrousKgS_ = 0.0;
    nitrousNm_ = 0.0;
    if (kit.ratedKw > 0.0) {
        // Le contacteur de plein gaz et la fenêtre de régime ouvrent les deux électrovannes ensemble.
        nitrousOn_ = in.burning && in.throttle >= kNitrousPedal && in.rpm >= kit.activationRpm && in.rpm <= kit.cutoffRpm && bottle_.massKg > 0.0;
        if (nitrousOn_) {
            nitrousKgS_ = nitrousFlowKgS(kit, bottle_, in.plenumPa);
            nitrousFuel = kit.fuelKgS;
            const double omega = in.rpm * 2.0 * kPi / 60.0;
            nitrousNm_ = nitrousBurn(nitrousKgS_, nitrousFuel, setup_.fuel).indicatedW / omega;
        }
        stepBottle(bottle_, kit, nitrousKgS_, in.dt, setup_.conditions.ambientK);
    }
    ChargeMix mix;
    mix.chargeK = in.chargeK;
    mix.plenumPa = in.plenumPa;
    mix.airKgS = in.airKgS;
    mix.waterPerAir = setup_.water ? waterPerAirAt(in.fill, setup_.fuel) : 0.0;
    mix.nitrousKgS = nitrousKgS_;
    mix.nitrousFuelKgS = nitrousFuel;
    mix.bottleK = bottle_.kelvin;
    mix.waterK = setup_.conditions.ambientK;
    manifold_ = mixCharge(mix, setup_.fuel);
    waterGps_ = mix.waterPerAir * in.airKgS * 1000.0;
    // Le couple de base suppose `referenceK` : un air plus chaud pèse moins, à pression égale.
    torqueShare_ = in.referenceK / in.chargeK * manifold_.airShare;
}

}  // namespace revforge
