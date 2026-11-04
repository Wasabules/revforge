#include "revforge/cold_start.hpp"

#include <algorithm>
#include <cmath>

#include "portable_math.hpp"
#include "revforge/atmosphere.hpp"
#include "revforge/cam.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/combustion.hpp"
#include "revforge/injection.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/thermal.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAbsolute = 273.15;
constexpr double kAirR = 287.0;
constexpr double kAirCv = 718.0;
constexpr double kGamma = 1.4;
constexpr double kMolarR = 8.3143;
constexpr double kAirMolarG = 28.97;
constexpr double kAtmosphereBar = 1.01325;

// ── L'huile ──
// Loi de Vogel μ = a·exp(b/(T + c)) (Pa·s, °C), ajustée exactement sur trois points par grade : KV40 et KV100 d'une huile du
// commerce typique (densité et dilatation de la référence), et la limite CCS de la SAE J300 de sa classe W, à la même marge que
// la référence (4250 mPa·s mesurés à −30 °C pour une limite de 6600 : 0,644). Le monograde SAE 30 n'a pas de limite CCS : son
// troisième point est l'extrapolation ASTM D341 (Walther) à −10 °C, faite pour les huiles minérales sans améliorant.
// 0W-20 : 44 / 8,3 mm²/s, 6200 à −35 °C ; 10W-40 : 95 / 14,3, 7000 à −25 °C ; 15W-40 : 108 / 14,6, 7000 à −20 °C ;
// 20W-50 : 165 / 18,5, 9500 à −15 °C ; SAE 30 : 100 / 11,0. Ajustement : scratchpad froid/oils.py (voir docs/modele.md).
struct Vogel {
    double a, b, c;
};
constexpr Vogel kVogel[] = {
    {3.476900e-5, 1345.5610, 144.8707},  // 5W-30, la référence de la thermique (thermal.cpp)
    {3.160906e-5, 1324.3950, 147.7484},  // 0W-20
    {1.793476e-5, 1691.3855, 161.0233},  // 10W-40
    {1.672624e-5, 1670.6313, 153.6046},  // 15W-40
    {1.212967e-5, 1805.9218, 152.5312},  // 20W-50
    {3.974526e-5, 1117.9687, 105.3365},  // SAE 30
};
// Au-dessus, la référence : le moteur chaud est calé sur elle, quel que soit son grade.
constexpr double kGradeJoinC = kCrankingJoinC;
// Au lancement, la viscosité agit à la puissance 0,86 : ajustée sur la table d'Exide (puissance que demande un moteur au
// démarreur : 100, 155, 210 et 350 % à 27, 0, −18 et −35 °C), citadine à 150 tr/min ; Petroff donnerait 1, la loi du moteur
// qui tourne (Sandoval et Heywood) 0,5, soit 129 % à −35 °C seulement.
constexpr double kCrankViscosityExponent = 0.86;

// ── La batterie ──
// Essai EN 50342-1 à −18 °C : 7,5 V après 10 s au courant nominal I, repos de 10 s, puis 6 V au bout de 73 s à 0,6·I. Avec
// V = E − I·R − V_K·x/(1 − x), x = q/Q (Shepherd), q la charge tirée que l'acide revient remplacer en 60 s (supposé), et le coude
// atteint à la fin de l'essai (x = 0,96, supposé) : I·R = 5,129 V, V_K = 0,151 V, Q = 28,8 s × I (froid/battery.py).
constexpr double kFullVolts = 12.7;
constexpr double kEmptyVolts = 11.8;
constexpr double kRatedDropV = 5.059;
constexpr double kPolarizationV = 0.139;
constexpr double kHighRateSeconds = 33.27;
constexpr double kRecoveryS = 60.0;
constexpr double kRatedK = kAbsolute - 18.0;
// La résistance suit la convention des courants nominaux (BCI) : le courant à 0 °C (CA) vaut 1,2 fois celui à −18 °C (CCA),
// d'où une loi d'Arrhenius de 706 K. La charge à fort débit tombe plus vite : 55 % à −18 °C de celle à 25 °C (de l'ordre de
// Battery University), 1060 K.
constexpr double kOhmsActivationK = 706.0;
constexpr double kChargeActivationK = 1060.0;
// Une batterie à moitié vide a une résistance plus forte (supposé : +25 % à mi-charge) ; gelée, elle ne débite presque plus.
constexpr double kEmptyOhmsShare = 0.5;
constexpr double kFrozenOhms = 30.0;
// L'électrolyte gèle selon sa densité, donc la charge : −7, −15, −24, −38 et −60 °C de vide à pleine (tables de densité).
constexpr double kFreezeCharge[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
constexpr double kFreezeC[5] = {-7.0, -15.0, -24.0, -38.0, -60.0};
// L'alternateur tient 14,2 V (supposé) ; la batterie n'accepte qu'un courant faible, d'autant plus qu'elle est froide et pleine :
// quatre fois sa résistance de décharge (supposé), au plus un quart de sa capacité en ampères (le surplus de l'alternateur).
constexpr double kAlternatorVolts = 14.2;
constexpr double kChargeOhmsShare = 4.0;
constexpr double kChargeCapShare = 0.25;
// Dimensionnement, au catalogue (Varta Blue Dynamic, de mémoire) : 44 Ah 440 A, 52 Ah 470 A, 60 Ah 540 A, 74 Ah 680 A,
// 77 Ah 780 A, 95 Ah 800 A, 100 Ah 830 A ; un diesel de 2 L monte 72 à 74 Ah 680 A.
constexpr double kBatteryBaseA = 300.0;
constexpr double kBatteryPerLitreA = 120.0;
constexpr double kDieselBattery = 1.25;
constexpr double kAmpsPerAh = 9.0;
// Le compartiment, moteur chaud : 12 K au-dessus de l'air, comme l'air du compartiment de air_filter.cpp (supposé).
constexpr double kBayRiseK = 12.0;

// ── Le démarreur ──
// Câbles (2 m de 25 à 35 mm², cosses et tresse de masse) : 1,5 mΩ, supposé. L'induit et ses balais : 60 % de cuivre, dont la
// résistance suit 0,393 %/K ; le reste (contact des balais) ne bouge pas (supposé).
constexpr double kCableOhms = 0.0015;
constexpr double kCopperShare = 0.6;
constexpr double kCopperPerK = 0.00393;

// ── La compression au lancement ──
// Hohenberg (1979) : h = 130·V^−0,06·p^0,8·T^−0,4·(v̄p + 1,4)^0,8 (W/m²·K, p en bar). Fuite aux segments : un orifice
// compressible vers le carter, de 0,02 mm² par millimètre d'alésage (Cd 0,7) : l'essai de compression donne alors 14,7 bar à
// 10,5:1 et 31,6 bar à 18:1, à 250 tr/min, moteur chaud (manuels : 12 à 14 bar de surpression, 25 à 31 bar sur un diesel).
constexpr double kLeakMm2PerMm = 0.02;
constexpr double kLeakDischarge = 0.7;
constexpr double kRodRatio = 3.3;
constexpr int kCompressionSteps = 240;

// ── L'essence d'hiver ──
// Neuf corps purs, parts massiques réglées sur la classe d'hiver de l'EN 228 : E70 25 %, E100 48 %, E150 87 % (distillation de
// Rayleigh), tension de vapeur vraie à 37,8 °C de 79 kPa (DVPE 60 à 90 kPa). Antoine : log10 p(mmHg) = A − B/(C + T °C)
// (Lange's Handbook). L'E85 : 86 % d'éthanol en masse ; le méthanol : pur. Raoult suppose un mélange idéal : un alcool dans
// l'essence s'en écarte, et serait un peu plus volatil.
struct Species {
    double molarG;
    double a, b, c;
};
constexpr int kSpecies = 11;
constexpr Species kSpeciesTable[kSpecies] = {
    {58.12, 6.80896, 935.86, 238.73},     // n-butane
    {72.15, 6.78967, 1020.012, 233.097},  // isopentane
    {72.15, 6.87632, 1075.78, 233.205},   // n-pentane
    {86.18, 6.87601, 1171.17, 224.41},    // n-hexane
    {114.23, 6.81189, 1257.84, 220.735},  // isooctane
    {92.14, 6.95464, 1344.8, 219.482},    // toluène
    {106.17, 6.99891, 1474.679, 213.686}, // o-xylène
    {120.19, 7.04383, 1573.267, 208.564}, // triméthylbenzène
    {142.28, 6.94363, 1495.17, 193.858},  // n-décane
    {46.07, 8.20417, 1642.89, 230.3},     // éthanol
    {32.04, 8.08097, 1582.271, 239.726},  // méthanol
};
constexpr double kWinterPetrol[9] = {0.06, 0.16, 0.07, 0.16, 0.17, 0.13, 0.11, 0.09, 0.05};
constexpr double kE85Ethanol = 0.857;
// Le calculateur (ou le starter) vise, au lancement, une vapeur riche : λ ≈ 0,77, comme le starter de la thermique à 20 °C
// (supposé). Il ne peut pas injecter plus de 20 fois la stœchiométrie par cycle (supposé, l'ordre des plus fortes injections de
// démarrage) : au-delà, la bougie se noie.
constexpr double kStartVaporPhi = 1.3;
constexpr double kMaxStartFuel = 20.0;
// Le remplissage au lancement : le collecteur est presque à la pression ambiante, le papillon fermé laisse passer ce débit minime.
constexpr double kCrankFilling = 0.85;
// La dispersion du délai d'un diesel (supposé), et le rendement de la chaleur dégagée au lancement : parois froides, flamme lente.
constexpr double kDieselDelaySpread = 0.2;
constexpr double kCrankHeatShare = 0.5;

// ── Le préchauffage ──
// Bougie métallique (pompe mécanique) : 1000 °C en régime, constante de 3 s ; céramique (rampe commune) : 1150 °C, 1 s (supposés,
// de l'ordre des fiches Bosch et Beru). Le calculateur chauffe jusqu'à 850 °C quand l'eau est sous 20 °C. Près de la bougie, le
// jet trouve un gaz réchauffé du quart de l'écart (supposé).
constexpr double kGlowReadyC = 850.0;
constexpr double kGlowBelowC = 20.0;
constexpr double kGlowMetalC = 1000.0;
constexpr double kGlowMetalTauS = 3.0;
constexpr double kGlowLocalShare = 0.25;
constexpr double kGlowAmps = 12.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

const Vogel& vogelOf(OilGrade grade) {
    const int index = static_cast<int>(grade);
    return kVogel[index < 0 || index > 5 ? 0 : index];
}

double vogelPaS(const Vogel& v, double oilC) { return v.a * portable::exp(v.b / std::max(oilC + v.c, 20.0)); }

double interpolate(const double* xs, const double* ys, int count, double x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < count; ++i) {
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[count - 1];
}

/** Tension de vapeur (bar), Antoine. */
double vaporBar(const Species& s, double celsius) { return portable::exp(portable::kLn10 * (s.a - s.b / (s.c + celsius))) * 133.322 / 1e5; }

/** La composition du carburant en parts massiques, sur les onze corps. */
void compositionOf(FuelGrade grade, double* mass) {
    for (int i = 0; i < kSpecies; ++i) mass[i] = 0.0;
    if (grade == FuelGrade::Methanol) {
        mass[10] = 1.0;
        return;
    }
    const double petrol = grade == FuelGrade::E85 ? 1.0 - kE85Ethanol : 1.0;
    for (int i = 0; i < 9; ++i) mass[i] = kWinterPetrol[i] * petrol;
    if (grade == FuelGrade::E85) mass[9] = kE85Ethanol;
}

/** La somme de trois tirages uniformes sur [−1, 1] : variance 1, bornée à ±3. */
double boundedNormalBelow(double z) {
    // Fonction de répartition de la somme de trois uniformes (Irwin-Hall recentrée), en z ∈ [−3, 3].
    const double u = clamp(z, -3.0, 3.0) + 3.0;  // somme de trois U(0,2) : 0 à 6
    const double v = u / 2.0;                    // somme de trois U(0,1) : 0 à 3
    double cdf = 0.0;
    if (v <= 1.0) cdf = v * v * v / 6.0;
    else if (v <= 2.0) cdf = (-2.0 * v * v * v + 9.0 * v * v - 9.0 * v + 3.0) / 6.0;
    else cdf = 1.0 - (3.0 - v) * (3.0 - v) * (3.0 - v) / 6.0;
    return clamp(cdf, 0.0, 1.0);
}

}  // namespace

// ── L'huile ──

double oilViscosityPaS(OilGrade grade, double oilC) {
    if (grade == OilGrade::Sae5W30) return oilViscosityPaS(oilC);
    return vogelPaS(vogelOf(grade), oilC);
}

double runningViscosityExcess(OilGrade grade, double oilC) {
    if (grade == OilGrade::Sae5W30 || oilC >= kGradeJoinC) return coldViscosityExcess(oilC);
    const Vogel& v = vogelOf(grade);
    const double join = oilViscosityPaS(kGradeJoinC) / oilViscosityPaS(kWarmOilC);
    return std::sqrt(join * vogelPaS(v, oilC) / vogelPaS(v, kGradeJoinC)) - 1.0;
}

double crankingViscosityExcess(OilGrade grade, double oilC) {
    if (oilC >= kGradeJoinC) return runningViscosityExcess(grade, oilC);
    // Sous 80 °C, au lancement : (μ/μ₈₀)^0,86, raccordé à la loi du moteur qui tourne à 80 °C. Entre Petroff (1) et la racine du
    // moteur qui tourne (0,5), l'exposant que demande la table d'Exide (froid/exide.py).
    const double join = 1.0 + coldViscosityExcess(kGradeJoinC);
    return join * portable::pow(oilViscosityPaS(grade, oilC) / oilViscosityPaS(grade, kGradeJoinC), kCrankViscosityExponent) - 1.0;
}

const char* oilGradeName(OilGrade grade) {
    switch (grade) {
        case OilGrade::Sae0W20: return "0W-20";
        case OilGrade::Sae10W40: return "10W-40";
        case OilGrade::Sae15W40: return "15W-40";
        case OilGrade::Sae20W50: return "20W-50";
        case OilGrade::Sae30: return "SAE 30";
        case OilGrade::Sae5W30: break;
    }
    return "5W-30";
}

// ── La batterie ──

BatterySpec batterySpecOf(const EngineConfig& e) {
    BatterySpec s;
    const double litres = e.sweptCcPerTwoTurns() / 1000.0;
    s.coldCrankingA = clamp((kBatteryBaseA + kBatteryPerLitreA * litres) * (e.burnsDiesel() ? kDieselBattery : 1.0), kBatteryBaseA, 1000.0);
    s.capacityAh = s.coldCrankingA / kAmpsPerAh;
    s.ohmsAtMinus18 = kRatedDropV / s.coldCrankingA;
    s.highRateAs = kHighRateSeconds * s.coldCrankingA;
    return s;
}

double batteryOpenVolts(double charge) { return kEmptyVolts + (kFullVolts - kEmptyVolts) * clamp(charge, 0.0, 1.0); }

double electrolyteFreezeC(double charge) { return interpolate(kFreezeCharge, kFreezeC, 5, clamp(charge, 0.0, 1.0)); }

double batteryOhms(const BatterySpec& spec, double celsius, double charge) {
    const double k = celsius + kAbsolute;
    double ohms = spec.ohmsAtMinus18 * portable::exp(kOhmsActivationK * (1.0 / k - 1.0 / kRatedK)) * (1.0 + kEmptyOhmsShare * (1.0 - clamp(charge, 0.0, 1.0)));
    if (celsius < electrolyteFreezeC(charge)) ohms *= kFrozenOhms;
    return ohms;
}

double batteryHighRateAs(const BatterySpec& spec, double celsius, double charge) {
    const double k = celsius + kAbsolute;
    return spec.highRateAs * portable::exp(-kChargeActivationK * (1.0 / k - 1.0 / kRatedK)) * std::max(clamp(charge, 0.0, 1.0), 0.05);
}

double batteryStartC(double ambientC, double coolantC, EnginePlacement placement) {
    if (placement == EnginePlacement::Mid || placement == EnginePlacement::Rear) return ambientC;
    const double span = kWarmCoolantC - ambientC;
    const double warmth = span > 1.0 ? clamp((coolantC - ambientC) / span, 0.0, 1.0) : 1.0;
    return ambientC + kBayRiseK * warmth;
}

double starterWindingC(double ambientC, double coolantC) { return ambientC + 0.5 * (coolantC - ambientC); }

namespace {
double polarizationVolts(const BatterySpec& spec, const BatteryState& state) {
    const double x = std::min(std::max(state.drawnAs, 0.0) / batteryHighRateAs(spec, state.celsius, state.charge), 0.98);
    return kPolarizationV * x / (1.0 - x);
}
}  // namespace

double batteryVolts(const BatterySpec& spec, const BatteryState& state, double amps) {
    return batteryOpenVolts(state.charge) - polarizationVolts(spec, state) - amps * batteryOhms(spec, state.celsius, state.charge);
}

void stepBattery(const BatterySpec& spec, BatteryState& state, double amps, double dt) {
    state.charge = clamp(state.charge - amps * dt / (spec.capacityAh * 3600.0), 0.0, 1.0);
    state.drawnAs = std::max(state.drawnAs + (amps - state.drawnAs / kRecoveryS) * dt, 0.0);
}

double alternatorChargeA(const BatterySpec& spec, const BatteryState& state) {
    const double push = kAlternatorVolts - batteryOpenVolts(state.charge);
    if (push <= 0.0 || state.charge >= 1.0) return 0.0;
    const double ohms = kChargeOhmsShare * batteryOhms(spec, state.celsius, state.charge);
    return std::min(push / ohms, kChargeCapShare * spec.capacityAh);
}

// ── Le démarreur ──

StarterCircuit starterCircuitOf(const StarterSpec& starter, const BatterySpec& battery, double batteryRefC, double motorRefC) {
    StarterCircuit c;
    const double power = std::max(static_cast<double>(starter.powerKw), 0.1) * 1000.0;
    c.pinionFreeOmega = static_cast<double>(starter.motorFreeRpm) * 2.0 * kPi / 60.0 / std::max(static_cast<double>(starter.reduction), 0.1);
    c.ohmsRef = c.emfRef * c.emfRef / (4.0 * power);
    c.cableOhms = kCableOhms;
    c.motorOhmsRef = std::max(c.ohmsRef - batteryOhms(battery, batteryRefC, 1.0) - c.cableOhms, 0.25 * c.ohmsRef);
    c.motorRefC = motorRefC;
    c.newtonMetresPerAmp = c.emfRef / c.pinionFreeOmega;
    return c;
}

double starterAmps(const StarterCircuit& c, const BatterySpec& spec, const BatteryState& state, double motorC, double pinionOmega) {
    const double motor = c.motorOhmsRef * ((1.0 - kCopperShare) + kCopperShare * (1.0 + kCopperPerK * (motorC - c.motorRefC)));
    const double ohms = batteryOhms(spec, state.celsius, state.charge) + c.cableOhms + motor;
    const double emf = batteryOpenVolts(state.charge) - polarizationVolts(spec, state);
    return std::max((emf - c.newtonMetresPerAmp * pinionOmega) / ohms, 0.0);
}

// ── La compression ──

CrankCompression crankCompression(double rpm, double ratio, double boreMm, double strokeMm, double intakeC, double wallC, double intakeBar, double ivcAbdcDeg) {
    CrankCompression out;
    const double bore = boreMm / 1000.0;
    const double stroke = strokeMm / 1000.0;
    const double piston = kPi * bore * bore / 4.0;
    const double swept = piston * stroke;
    const double clearance = swept / std::max(ratio - 1.0, 1.0);
    const auto volume = [&](double theta) {
        const double s = portable::sin(theta);
        return clearance + swept / 2.0 * (kRodRatio + 1.0 - portable::cos(theta) - std::sqrt(kRodRatio * kRodRatio - s * s));
    };
    const double omega = std::max(rpm, 5.0) * 2.0 * kPi / 60.0;
    const double meanPiston = 2.0 * stroke * std::max(rpm, 5.0) / 60.0;
    const double leak = kLeakMm2PerMm * boreMm * 1e-6;
    const double start = kPi - ivcAbdcDeg * kPi / 180.0;
    const double inK = intakeC + kAbsolute;
    const double wallK = wallC + kAbsolute;
    double theta = start;
    double kelvin = inK;
    double pressure = intakeBar * 1e5;
    double mass = pressure * volume(theta) / (kAirR * kelvin);
    const double step = start / kCompressionSteps;
    const double dt = step / omega;
    const double critical = portable::pow(2.0 / (kGamma + 1.0), kGamma / (kGamma - 1.0));
    const double choked = std::sqrt(kGamma) * portable::pow(2.0 / (kGamma + 1.0), (kGamma + 1.0) / (2.0 * (kGamma - 1.0)));
    for (int i = 0; i < kCompressionSteps; ++i) {
        const double v0 = volume(theta);
        const double v1 = volume(theta - step);
        const double h = 130.0 * portable::pow(v0, -0.06) * portable::pow(pressure / 1e5, 0.8) * portable::pow(kelvin, -0.4) * portable::pow(meanPiston + 1.4, 0.8);
        const double area = 2.0 * piston + kPi * bore * (v0 - clearance) / piston;
        const double heat = h * area * (kelvin - wallK) * dt;
        const double ratioOut = std::max(1e5 / pressure, 1e-6);
        double psi = 0.0;
        if (ratioOut < critical) psi = choked;
        else if (ratioOut < 1.0) psi = std::sqrt(2.0 * kGamma / (kGamma - 1.0) * (portable::pow(ratioOut, 2.0 / kGamma) - portable::pow(ratioOut, (kGamma + 1.0) / kGamma)));
        const double lost = -kLeakDischarge * leak * pressure / std::sqrt(kAirR * kelvin) * psi * dt;
        kelvin += (-pressure * (v1 - v0) - heat + kAirR * kelvin * lost) / (mass * kAirCv);
        mass = std::max(mass + lost, 1e-9);
        pressure = mass * kAirR * kelvin / v1;
        theta -= step;
    }
    out.tdcBar = pressure / 1e5;
    out.tdcK = kelvin;
    const double trapped = volume(start) / clearance;
    out.polytropic = 1.0 + portable::log(kelvin / inK) / portable::log(std::max(trapped, 1.01));
    return out;
}

// ── L'essence ──

double vaporizedShare(FuelGrade grade, double phiDelivered, double wallC, double pressureBar) {
    if (phiDelivered <= 0.0) return 1.0;
    double mass[kSpecies];
    compositionOf(grade, mass);
    const double afr = fuelProperties(grade).stoichiometricAfr;
    // Un gramme d'air, et ce qu'il reçoit de carburant.
    const double fuelG = phiDelivered / afr;
    const double air = 1.0 / kAirMolarG;
    double moles[kSpecies];
    double k[kSpecies];
    double total = 0.0;
    double allGone = air;
    for (int i = 0; i < kSpecies; ++i) {
        moles[i] = fuelG * mass[i] / kSpeciesTable[i].molarG;
        k[i] = vaporBar(kSpeciesTable[i], wallC) / std::max(pressureBar, 0.05);
        total += moles[i];
        if (moles[i] > 0.0) allGone += moles[i] - moles[i] / k[i];
    }
    // Tout se vaporise : l'air peut porter tout ce carburant à cette température.
    if (allGone >= 0.0) return 1.0;
    // Rachford-Rice avec un gaz qui ne condense pas : R = gaz / liquide, g(R) = n_air + Σn − (1 + R)·Σ n_i/(1 + K_i·R) = 0.
    const auto g = [&](double r) {
        double liquid = 0.0;
        for (int i = 0; i < kSpecies; ++i) {
            if (moles[i] > 0.0) liquid += moles[i] / (1.0 + k[i] * r);
        }
        return air + total - (1.0 + r) * liquid;
    };
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 60 && g(high) > 0.0; ++i) high *= 2.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (low + high);
        (g(mid) > 0.0 ? low : high) = mid;
    }
    const double r = 0.5 * (low + high);
    double vapourG = 0.0;
    for (int i = 0; i < kSpecies; ++i) {
        if (moles[i] > 0.0) vapourG += (moles[i] - moles[i] / (1.0 + k[i] * r)) * kSpeciesTable[i].molarG;
    }
    return clamp(vapourG / fuelG, 0.0, 1.0);
}

double startFuelShare(FuelGrade grade, double coolantC, double pressureBar, double phiTarget) {
    const auto vapour = [&](double phi) { return phi * vaporizedShare(grade, phi, coolantC, pressureBar); };
    if (vapour(kMaxStartFuel) < phiTarget) return kMaxStartFuel;
    double low = phiTarget * 0.5, high = kMaxStartFuel;
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (low + high);
        (vapour(mid) < phiTarget ? low : high) = mid;
    }
    return 0.5 * (low + high);
}

double petrolFiringChance(double phiVapor) {
    if (phiVapor <= 0.0) return 0.0;
    // Le cycle allume si φ·(1 + σ·ξ) tombe entre les deux limites.
    const double lower = (kLeanFiringPhi / phiVapor - 1.0) / kStartMixtureSpread;
    const double upper = (kRichFiringPhi / phiVapor - 1.0) / kStartMixtureSpread;
    return clamp(boundedNormalBelow(upper) - boundedNormalBelow(lower), 0.0, 1.0);
}

// ── Le diesel ──

double crankIgnitionDelayDeg(const CrankCompression& tdc, double meanPistonMs, double glowLocalK) {
    const double kelvin = std::max(tdc.tdcK, glowLocalK);
    return ignitionDelayDeg(tdc.tdcBar, kelvin, meanPistonMs, 51.0);
}

double dieselFiringChance(double delayDeg) {
    if (delayDeg <= 0.0) return 1.0;
    // Allume si le délai, dispersé, tient dans la fenêtre : δ·(1 + σ·ξ) < W.
    return boundedNormalBelow((kDieselWindowDeg / delayDeg - 1.0) / kDieselDelaySpread);
}

double glowPreheatS(double coolantC) {
    if (coolantC >= kGlowBelowC) return 0.0;
    const double span = kGlowMetalC - coolantC;
    return -kGlowMetalTauS * portable::log(1.0 - (kGlowReadyC - coolantC) / span);
}

double glowPlugC(double startC, double seconds) {
    return kGlowMetalC + (startC - kGlowMetalC) * portable::exp(-std::max(seconds, 0.0) / kGlowMetalTauS);
}

// ── L'état du lancement ──

void StartModel::configure(const Powertrain& p) {
    const EngineConfig& e = p.engine;
    const StarterSpec starter = p.profile().starter;
    spec_ = batterySpecOf(e);
    ringRatio_ = static_cast<double>(starter.ringTeeth) / std::max(starter.pinionTeeth, 1);
    placement_ = p.car.placement == EnginePlacement::Natural ? EnginePlacement::Front : p.car.placement;
    // La batterie de référence : celle d'un moteur chaud par 20 °C, celle sur laquelle le démarreur d'origine est calé.
    referenceMotorC_ = starterWindingC(20.0, kWarmCoolantC);
    circuit_ = starterCircuitOf(starter, spec_, batteryStartC(20.0, kWarmCoolantC, placement_), referenceMotorC_);
    oil_ = e.oilGrade;
    grade_ = e.fuelGrade;
    diesel_ = e.burnsDiesel();
    system_ = fuelSystemOf(e);
    glow_ = diesel_;
    ratio_ = static_cast<double>(starter.compressionRatio);
    boreMm_ = e.effectiveBoreMm();
    strokeMm_ = isRotary(e) ? boreMm_ : static_cast<double>(e.strokeMm());
    ivcDeg_ = diesel_ ? 30.0 : (isRotary(e) || e.twoStroke() ? 60.0 : intakeClosingAbdcDeg(camProfileOf(e)));
    redline_ = static_cast<double>(e.redlineRpm);
    stoich_ = diesel_ ? 14.5 : fuelProperties(grade_).stoichiometricAfr;
    const double chamberCc = e.sweptCcPerTwoTurns() / static_cast<double>(std::max(e.firingsPerTwoTurns(), 1));
    const AmbientAir air = ambientAirOf(e);
    cylinderAirG_ = chamberCc * 1e-6 * air.pressurePa / (kAirR * air.temperatureK) * 1000.0 * kCrankFilling;
    if (hasCarburetor(e)) {
        const CarburetorSpec carb = carburetorSpecOf(e);
        wallShare_ = carb.wallShare;
        pumpG_ = carb.pumpG / std::max(static_cast<double>(carb.throats), 1.0);
    } else {
        wallShare_ = 0.0;
        pumpG_ = 0.0;
    }
}

StartDrive StartModel::drive(bool cranking) const {
    StartDrive d;
    if (!cranking || !fired_) return d;
    d.active = true;
    d.firing = static_cast<float>(chance_);
    d.fill = static_cast<float>(kCrankFilling * std::max(lastStrength_, 0.4));
    return d;
}

double StartModel::uniform() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<double>(rng_) / 4294967295.0 * 2.0 - 1.0;
}

void StartModel::beginAttempt(const StartConditions& c) {
    battery_.celsius = batteryStartC(c.ambientC, c.coolantC, placement_);
    motorC_ = starterWindingC(c.ambientC, c.coolantC);
    reference_ = BatteryState{};
    reference_.celsius = batteryStartC(20.0, kWarmCoolantC, placement_);
    fired_ = false;
    sinceKeyS_ = 0.0;
    crankRpmSum_ = 0.0;
    crankSteps_ = 0.0;
    ampSum_ = 0.0;
    engagedSteps_ = 0.0;
    stats_.attempts += 1;
    preheatS_ = glow_ ? glowPreheatS(c.coolantC) : 0.0;
    glowC_ = c.coolantC;
    glowOnS_ = 0.0;
    volts_ = revforge::batteryVolts(spec_, battery_, 0.0);
}

void StartModel::endAttempt(bool caught, double sinceKeyS) {
    if (caught && !stats_.caught) {
        stats_.caught = true;
        stats_.caughtAfterS = sinceKeyS;
    }
    if (crankSteps_ > 0.0) stats_.meanCrankRpm = crankRpmSum_ / crankSteps_;
}

double StartModel::stepStarter(double pinionOmega, double dt, bool engaged) {
    const double actual = engaged ? revforge::starterAmps(circuit_, spec_, battery_, motorC_, pinionOmega) : 0.0;
    const double reference = engaged ? revforge::starterAmps(circuit_, spec_, reference_, referenceMotorC_, pinionOmega) : 0.0;
    // Les bougies de préchauffage restent alimentées pendant le lancement (post-chauffage) : 4 à 6 fois 12 A.
    const double glowA = glow_ && preheatS_ > 0.0 ? kGlowAmps * 4.0 : 0.0;
    volts_ = revforge::batteryVolts(spec_, battery_, actual + glowA);
    amps_ = actual;
    stepBattery(spec_, battery_, actual + glowA, dt);
    stepBattery(spec_, reference_, reference, dt);
    if (glow_ && preheatS_ > 0.0) {
        glowOnS_ += dt;
        glowC_ = glowPlugC(glowC_, dt);
    }
    supplyShare_ = (actual - reference) / (circuit_.emfRef / circuit_.ohmsRef);
    sinceKeyS_ += dt;
    stats_.crankedS += dt;
    stats_.minVolts = std::min(stats_.minVolts, volts_);
    stats_.drawnAh += (actual + glowA) * dt / 3600.0;
    if (engaged) {
        ampSum_ += actual;
        engagedSteps_ += 1.0;
        if (!fired_) {
            crankRpmSum_ += pinionOmega / ringRatio_ * 60.0 / (2.0 * kPi);
            crankSteps_ += 1.0;
        }
        stats_.meanAmps = ampSum_ / engagedSteps_;
    }
    // L'écart de couple au pignon : nul, au bit près, quand la batterie est celle de référence.
    return circuit_.newtonMetresPerAmp * (actual - reference);
}

double StartModel::pinionFreeOmega() const {
    const double emf = batteryOpenVolts(battery_.charge) - polarizationVolts(spec_, battery_);
    return emf / circuit_.newtonMetresPerAmp;
}

void StartModel::stepRest(bool running, double dt) {
    const double amps = running ? -alternatorChargeA(spec_, battery_) : 0.0;
    stepBattery(spec_, battery_, amps, dt);
    volts_ = running ? kAlternatorVolts : revforge::batteryVolts(spec_, battery_, 0.0);
    amps_ = 0.0;
    supplyShare_ = 0.0;
}

double StartModel::phiVapor(const StartConditions& c) const {
    if (diesel_) return 0.0;
    return deliveredPhi_ * vaporizedShare(grade_, deliveredPhi_, c.coolantC, c.ambientBar);
}

void StartModel::filmOf(double wallC, double& deposit, double& tau, double& knownDeposit, double& knownTau) const {
    deposit = 0.0;
    tau = 1.0;
    knownDeposit = 0.0;
    knownTau = 1.0;
    if (system_ == FuelSystem::Injection) {
        // Le conduit, au lancement : l'air y passe à peine (la vitesse plancher de la loi d'entraînement), à la pression ambiante.
        const FilmParameters real = portFilmParameters(wallC, 1.0, 5.0);
        const FilmParameters known = portFilmParameters(kWarmCoolantC, 1.0, 5.0);
        deposit = real.deposit;
        tau = real.tauS;
        knownDeposit = known.deposit;
        knownTau = known.tauS;
    } else if (system_ == FuelSystem::Carburetor || system_ == FuelSystem::MultiCarb) {
        const FilmParameters cold = crankingCarburetorFilm(wallShare_, wallC);
        deposit = cold.deposit;
        tau = cold.tauS;
    }
}

/** Le carburant que le calculateur injecte (g/s) : il compense le film qu'il croit, celui d'un moteur chaud. Un starter ne compense rien. */
double StartModel::injectedGps(double wanted, double knownDeposit, double knownTau) const {
    if (system_ != FuelSystem::Injection) return wanted;
    return std::max((wanted - believedG_ / knownTau) / (1.0 - knownDeposit), 0.0);
}

bool StartModel::beginFiring(const StartConditions& c, double rpm, double cyclesPerS) {
    fired_ = true;
    // Une huile froide : jamais le démarrage d'origine, calé moteur chaud ; chaque cycle se joue.
    const bool warm = c.oilC >= kCrankingJoinC;
    if (diesel_) {
        const double piston = 2.0 * strokeMm_ / 1000.0 * rpm / 60.0;
        const CrankCompression tdc = crankCompression(rpm, ratio_, boreMm_, strokeMm_, c.intakeC, c.coolantC, c.ambientBar, ivcDeg_);
        const double local = tdc.tdcK + kGlowLocalShare * (glowC_ + kAbsolute - tdc.tdcK) * (preheatS_ > 0.0 ? 1.0 : 0.0);
        const double delay = crankIgnitionDelayDeg(tdc, piston, local);
        tdcK_ = tdc.tdcK;
        chance_ = dieselFiringChance(delay);
        lastStrength_ = 1.0;
        return warm && delay * (1.0 + 3.0 * kDieselDelaySpread) < kDieselWindowDeg;
    }
    tdcK_ = crankCompression(rpm, ratio_, boreMm_, strokeMm_, c.intakeC, c.coolantC, c.ambientBar, ivcDeg_).tdcK;
    startFuel_ = startFuelShare(grade_, c.coolantC, c.ambientBar, kStartVaporPhi);
    // Le film : celui d'un moteur qui vient de tourner n'a pas eu le temps de s'évaporer, il disparaît à mesure que le moteur
    // refroidit (supposé : à la part de chemin que l'eau a faite) ; celui d'une nuit dehors est sec.
    const double span = kWarmCoolantC - c.ambientC;
    const double warmth = span > 1.0 ? clamp((c.coolantC - c.ambientC) / span, 0.0, 1.0) : 1.0;
    const double perS = std::max(cyclesPerS, 0.05);
    double deposit = 0.0, tau = 1.0, knownDeposit = 0.0, knownTau = 1.0;
    filmOf(c.coolantC, deposit, tau, knownDeposit, knownTau);
    // Un starter n'a pas de calculateur : il est calé pour le démarrage à froid, et compense d'avance la part que mouille une pipe
    // sèche, au plus 20 fois la stœchiométrie (supposé). Moteur chaud, la pipe est encore mouillée et le volet ouvert. Le
    // calculateur d'une injection compense, lui, le film d'un moteur chaud.
    if (system_ == FuelSystem::Carburetor || system_ == FuelSystem::MultiCarb)
        startFuel_ = std::min(startFuel_ / std::max(1.0 - deposit * (1.0 - warmth), 0.05), kMaxStartFuel);
    const double wanted = startFuel_ * cylinderAirG_ / stoich_ * perS;
    // À l'équilibre, la paroi rend ce qu'elle reçoit : film = X·injecté·τ (g).
    filmG_ = warmth * deposit * wanted * tau;
    believedG_ = warmth * knownDeposit * wanted * knownTau;
    // Le premier cycle : ce qui arrive directement, et ce que rend le film.
    const double injected = injectedGps(wanted, knownDeposit, knownTau);
    deliveredPhi_ = ((1.0 - deposit) * injected + filmG_ / tau) / perS / (cylinderAirG_ / stoich_);
    const double vapour = phiVapor(c);
    chance_ = petrolFiringChance(vapour);
    lastStrength_ = torqueOfRichness(vapour);
    return warm && vapour * (1.0 - 3.0 * kStartMixtureSpread) > kLeanFiringPhi && vapour * (1.0 + 3.0 * kStartMixtureSpread) < kRichFiringPhi;
}

void StartModel::stepFilm(double dt, double cyclesPerS, double throttle, double wallC) {
    if (diesel_) return;
    const double rise = throttle - lastThrottle_;
    lastThrottle_ = throttle;
    // La pompe de reprise : un coup de pédale jette de l'essence dans la pipe.
    if (pumpG_ > 0.0 && rise > 0.0) filmG_ += pumpG_ * rise;
    if (cyclesPerS < 0.05) return;
    const double wanted = startFuel_ * cylinderAirG_ / stoich_ * cyclesPerS;
    double deposit = 0.0, tau = 1.0, knownDeposit = 0.0, knownTau = 1.0;
    filmOf(wallC, deposit, tau, knownDeposit, knownTau);
    const double injected = injectedGps(wanted, knownDeposit, knownTau);
    if (system_ == FuelSystem::Injection) believedG_ = std::max(believedG_ + (knownDeposit * injected - believedG_ / knownTau) * dt, 0.0);
    const double released = filmG_ / tau;
    filmG_ = std::max(filmG_ + (deposit * injected - released) * dt, 0.0);
    deliveredPhi_ = ((1.0 - deposit) * injected + released) / cyclesPerS / (cylinderAirG_ / stoich_);
}

double StartModel::refreshChance(const StartConditions& c, double rpm) {
    if (!diesel_) {
        chance_ = petrolFiringChance(phiVapor(c));
        return chance_;
    }
    const double piston = 2.0 * strokeMm_ / 1000.0 * rpm / 60.0;
    const CrankCompression tdc = crankCompression(rpm, ratio_, boreMm_, strokeMm_, c.intakeC, c.coolantC, c.ambientBar, ivcDeg_);
    const double local = tdc.tdcK + kGlowLocalShare * (glowC_ + kAbsolute - tdc.tdcK) * (preheatS_ > 0.0 ? 1.0 : 0.0);
    chance_ = dieselFiringChance(crankIgnitionDelayDeg(tdc, piston, local));
    return chance_;
}

double StartModel::lastHeatJ() const {
    // Le carburant que brûle une charge pleine à la stœchiométrie (un diesel démarre vers λ = 1,5, son débit de démarrage),
    // à la force de cette explosion ; un lancement n'en garde que la moitié (parois froides, flamme lente : supposé).
    const double lhv = diesel_ ? 43.1e6 : fuelProperties(grade_).lowerHeatingJPerG * 1000.0;
    const double fuelKg = cylinderAirG_ / 1000.0 / ((diesel_ ? 1.5 : 1.0) * stoich_);
    return kCrankHeatShare * fuelKg * lhv * lastStrength_;
}

bool StartModel::fire(const StartConditions& c, double rpm) {
    const double draw = uniform() + uniform() + uniform();
    bool lit = false;
    const CrankCompression tdc = crankCompression(rpm, ratio_, boreMm_, strokeMm_, c.intakeC, c.coolantC, c.ambientBar, ivcDeg_);
    tdcK_ = tdc.tdcK;
    if (diesel_) {
        const double piston = 2.0 * strokeMm_ / 1000.0 * rpm / 60.0;
        const double local = tdc.tdcK + kGlowLocalShare * (glowC_ + kAbsolute - tdc.tdcK) * (preheatS_ > 0.0 ? 1.0 : 0.0);
        const double delay = crankIgnitionDelayDeg(tdc, piston, local);
        chance_ = dieselFiringChance(delay);
        lit = delay * (1.0 + kDieselDelaySpread * draw) < kDieselWindowDeg;
        // Plus tard il s'allume, plus loin du point mort haut il dégage sa chaleur : moins de travail (supposé, linéaire).
        lastStrength_ = lit ? clamp(1.0 - 0.5 * delay / kDieselWindowDeg, 0.3, 1.0) : 0.0;
    } else {
        const double vapour = phiVapor(c);
        const double local = vapour * (1.0 + kStartMixtureSpread * draw);
        chance_ = petrolFiringChance(vapour);
        lit = local > kLeanFiringPhi && local < kRichFiringPhi;
        lastStrength_ = lit ? torqueOfRichness(local) : 0.0;
    }
    if (lit) stats_.firings += 1;
    else stats_.misfires += 1;
    return lit;
}

}  // namespace revforge
