#include "revforge/combustion.hpp"

#include <cmath>

#include "portable_math.hpp"
#include "revforge/atmosphere.hpp"
#include "revforge/cam.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/charge_air.hpp"
#include "revforge/injection.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/two_stroke.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAtmosphere = 101325.0;
constexpr double kAirR = 287.0;
constexpr double kAirCp = 1005.0;
constexpr double kAirMolarMass = 28.97;
constexpr double kGasConstant = 8.314;

// ── Les carburants. Voir « La combustion » dans docs/modele.md pour les sources. ──
// L'essence du commerce : les valeurs de `consumption.cpp`, inchangées. La chaleur de vaporisation d'une essence E5 ou E10 (Heywood donne
// 305 kJ/kg sans éthanol, l'éthanol en apporte un peu), une masse molaire et une ébullition médiane d'ordre de grandeur.
constexpr FuelProperties kRon95{0.745, 42900.0, 3.17, 14.7, 95.0, 85.0, 350.0, 100.0, 370.0};
// Le Super 98 et une essence de course sans plomb : mêmes chiffres, sauf l'octane (EN 228 pour le 98, valeurs courantes pour le 102).
constexpr FuelProperties kRon98{0.745, 42900.0, 3.17, 14.7, 98.0, 88.0, 350.0, 100.0, 370.0};
constexpr FuelProperties kRace102{0.745, 42900.0, 3.17, 14.7, 102.0, 90.0, 350.0, 100.0, 370.0};
// Les corps purs (Heywood, annexe D ; NIST pour la chaleur de vaporisation à 25 °C).
constexpr FuelProperties kEthanol{0.789, 26900.0, 2.0 * 44.01 / 46.07, 8.95, 109.0, 90.0, 918.6, 46.07, 351.4};
constexpr FuelProperties kHydrocarbons{0.740, 44000.0, 3.167, 14.6, 92.0, 82.0, 305.0, 100.0, 370.0};
constexpr FuelProperties kMethanol{0.792, 19900.0, 44.01 / 32.04, 6.47, 109.0, 89.0, 1168.0, 32.04, 337.8};
constexpr FuelProperties kDiesel{0.832, 43100.0, 3.15, 14.5, 0.0, 0.0, 250.0, 170.0, 540.0};

/** L'E85 d'été : 85 % d'éthanol en volume, le reste en hydrocarbures. Ses octanes sont mesurés, pas mélangés : l'éthanol les relève plus que sa part. */
constexpr FuelProperties blendE85() {
    const double ethanolKg = 0.85 * kEthanol.densityKgPerL;
    const double restKg = 0.15 * kHydrocarbons.densityKgPerL;
    const double w = ethanolKg / (ethanolKg + restKg);
    FuelProperties f{};
    f.densityKgPerL = ethanolKg + restKg;
    f.lowerHeatingJPerG = w * kEthanol.lowerHeatingJPerG + (1.0 - w) * kHydrocarbons.lowerHeatingJPerG;
    f.co2GPerG = w * kEthanol.co2GPerG + (1.0 - w) * kHydrocarbons.co2GPerG;
    f.stoichiometricAfr = w * kEthanol.stoichiometricAfr + (1.0 - w) * kHydrocarbons.stoichiometricAfr;
    f.ron = 107.0;
    f.mon = 89.0;
    f.vaporizationJPerG = w * kEthanol.vaporizationJPerG + (1.0 - w) * kHydrocarbons.vaporizationJPerG;
    f.molarMassGPerMol = 1.0 / (w / kEthanol.molarMassGPerMol + (1.0 - w) / kHydrocarbons.molarMassGPerMol);
    f.boilingK = kEthanol.boilingK;
    return f;
}
constexpr FuelProperties kE85 = blendE85();

// ── Le rendement selon le taux ──
// Cycle air-carburant : le gaz réel, chaud et dissocié, se détend comme un gaz parfait à γ ≈ 1,3 (Heywood, ch. 5).
constexpr double kCycleGamma = 1.3;
// Rapport d'injection d'un diesel à pleine charge (supposé) : le cycle de Diesel s'en écarte d'autant de celui d'Otto.
constexpr double kDieselCutoff = 2.0;
// Ce que perdent les parois croît avec la surface de la chambre au point mort haut. Le coefficient met l'optimum à 17 sur un moteur carré,
// comme l'ont mesuré Caris et Nelson (1959) : d/dr [η_ac(r)·(1 − k·(2(r−1)+4))] = 0 à r = 17 donne k = 0,00533.
constexpr double kWallLoss = 0.00533;
// Le gaz qui se vaporise le fait au contact d'une charge déjà réchauffée dans le conduit (supposé), et la pleine charge enrichit.
constexpr double kEvaporationAirK = 320.0;
constexpr double kFullLoadLambda = 0.88;
constexpr double kFullLoadLambdaBoosted = 0.82;

// ── Le diesel ──
constexpr double kDieselPolytropic = 1.35;
constexpr double kDieselIdleIntakeK = 330.0;
constexpr double kDieselRatioMin = 14.0;  // le plus bas d'un diesel de série (Mazda Skyactiv-D) : en dessous, il ne s'allume plus à froid
// Intégration : 4:1, pour les moteurs d'avant-guerre (Ford A 4,2, Cadillac V16 5,35, Old Cars Weekly) ; 6 jusque-là.
constexpr double kRatioMin = 4.0;
constexpr double kRatioMax = 24.0;
// Le plus haut d'un rotatif de série : 11,9 (Mazda 8C, 2023). Au-delà, le rotor n'a plus assez de creux pour la chambre (supposé).
constexpr double kRotaryRatioMax = 11.9;

// ── La combustion vue de la chambre (supposés, voir docs/modele.md) ──
// Une seule zone : compression et combustion à γ = 1,3 ; les gaz frais, comprimés à part, perdent un peu de chaleur aux parois.
constexpr double kZoneGamma = 1.3;
constexpr double kUnburnedGamma = 1.30;
// La chaleur reçue nette, combustion moins parois, d'un cycle d'Otto ramassé en un point : celle de `releasePressureShare`.
constexpr double kNetHeatShare = 0.80;
// Part de l'énergie de l'air que la combustion libère vraiment (Heywood, ch. 4 : 95 à 98 % à la stœchiométrie et au-delà, moins
// les imbrûlés des interstices ; riche, l'oxygène manque, et l'énergie par kilogramme d'air reste à peu près celle de λ 1). Supposé.
constexpr double kCombustionShare = 0.95;
// Les parois, pas à pas : Woschni (1967), Heywood éq. 12.19. h = 3,26·B^−0,2·p^0,8·T^−0,55·w^0,8 (W/m²K, B en m, p en kPa,
// T en K, w en m/s), w = 2,28·vp + 3,24·10⁻³·(Vd·T₁/(p₁·V₁))·(p − p_entraîné), le second terme pendant la combustion et la détente.
constexpr double kWoschniH = 3.26;
constexpr double kWoschniPiston = 2.28;
constexpr double kWoschniCombustion = 3.24e-3;
// Culasse, piston et chemise : 400 à 600 K à pleine charge (Heywood, ch. 12). Leur moyenne, 60 K au-dessus de l'eau : supposé.
constexpr double kChamberWallOverCoolantK = 60.0;
// Loi de Wiebe à a = 5, m = 2 (Heywood), sur 45° de cycle, après le noyau de flamme : un temps, donc un angle qui croît avec le régime.
constexpr double kWiebeA = 5.0;
constexpr double kBurnDeg = 45.0;
constexpr double kKernelS = 0.00035;
// Le cliquetis ne compte que tant qu'il reste des gaz frais à faire détoner.
constexpr double kKnockEndBurned = 0.9;
// La charge prend la chaleur du conduit et de la chemise, plus chauds que l'eau ; d'autant moins qu'elle est dense (h ∝ (ρv)^0,8).
constexpr double kWallPickup = 0.20;
constexpr double kWallOverCoolantK = 40.0;
constexpr double kResidualK = 1000.0;
constexpr double kVolumetric = 0.92;
constexpr double kStepDeg = 1.0;
// La grille des avances de la carte (degrés de cycle avant le point mort haut).
constexpr double kAdvanceMin = -20.0;
constexpr double kAdvanceStep = 5.0;
// La limite de cliquetis tombe hors de la grille : l'avance n'y cliquette jamais.
constexpr double kNoLimitDeg = 80.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** Le volume au-dessus du piston, en part de la cylindrée, à `thetaDeg` du point mort haut. */
double chamberVolume(double thetaDeg, double compressionRatio, double rodRatio) {
    const double clearance = 1.0 / (compressionRatio - 1.0);
    const double radians = thetaDeg * kPi / 180.0;
    const double c = portable::cos(radians);
    if (rodRatio <= 0.0) return clearance + 0.5 * (1.0 - c);
    const double s = portable::sin(radians);
    const double rod = rodRatio * 0.5;
    return clearance + 0.5 * (1.0 - c) + rod - std::sqrt(rod * rod - 0.25 * s * s);
}

/** Tension de vapeur (Pa), par Clausius-Clapeyron depuis l'ébullition sous 1 atm. */
double vaporPressurePa(const FuelProperties& fuel, double temperatureK) {
    const double latent = fuel.vaporizationJPerG * fuel.molarMassGPerMol;  // J/mol
    return kAtmosphere * portable::exp(-latent / kGasConstant * (1.0 / temperatureK - 1.0 / fuel.boilingK));
}

/** La densité de la charge une fois ce carburant vaporisé, rapportée à celle de l'air seul : plus froide, mais un peu d'air en moins. */
double densityAfterEvaporation(const FuelProperties& fuel, double lambda, double airK, double airShare) {
    const double cooling = evaporationCoolingK(fuel, lambda, airShare, airK, kAtmosphere);
    const double vapourKg = cooling * kAirCp / (fuel.vaporizationJPerG * 1000.0);
    const double vapourMoles = vapourKg / fuel.molarMassGPerMol * kAirMolarMass;
    return airK / (airK - cooling) / (1.0 + vapourMoles);
}

double fullLoadLambda(const EngineConfig& e) { return e.aspiration == Aspiration::Natural ? kFullLoadLambda : kFullLoadLambdaBoosted; }

double kernelDeg(double rpm, double cycleSpeedShare) { return kKernelS * 6.0 * rpm * cycleSpeedShare; }

}  // namespace

// ── Les carburants ──

const FuelProperties& fuelProperties(FuelGrade grade) {
    switch (grade) {
        case FuelGrade::Ron98: return kRon98;
        case FuelGrade::Race102: return kRace102;
        case FuelGrade::E85: return kE85;
        case FuelGrade::Methanol: return kMethanol;
        case FuelGrade::Ron95: break;
    }
    return kRon95;
}

const FuelProperties& dieselFuelProperties() { return kDiesel; }

const FuelProperties& fuelPropertiesOf(const EngineConfig& engine) { return engine.burnsDiesel() ? kDiesel : fuelProperties(engine.fuelGrade); }

double energyPerAirJPerKg(const FuelProperties& fuel) { return fuel.lowerHeatingJPerG * 1000.0 / fuel.stoichiometricAfr; }

double evaporationCoolingK(const FuelProperties& fuel, double lambda, double evaporatedShare, double airK, double pressurePa) {
    const double fuelPerAir = 1.0 / (lambda * fuel.stoichiometricAfr);
    const double wanted = clamp(evaporatedShare, 0.0, 1.0) * fuelPerAir;
    const double perKg = fuel.vaporizationJPerG * 1000.0 / kAirCp;  // K par kg de carburant vaporisé dans un kg d'air
    const auto saturated = [&](double vapourKg) {
        const double temperature = airK - vapourKg * perKg;
        if (temperature <= 150.0) return true;
        const double moles = vapourKg / fuel.molarMassGPerMol * kAirMolarMass;
        return pressurePa * moles / (1.0 + moles) > vaporPressurePa(fuel, temperature);
    };
    if (wanted <= 0.0) return 0.0;
    if (!saturated(wanted)) return wanted * perKg;
    // La vapeur sature avant que tout ne soit vaporisé : on s'arrête là où sa pression rejoint la tension de vapeur.
    double low = 0.0;
    double high = wanted;
    for (int i = 0; i < 40; ++i) {
        const double middle = 0.5 * (low + high);
        (saturated(middle) ? high : low) = middle;
    }
    return low * perKg;
}

// ── Le taux de compression ──

float stockCompressionRatio(const EngineConfig& e) {
    if (isRotary(e)) return rotaryCompressionRatio(e.aspiration);
    // Un deux-temps : son taux géométrique (le curseur part de lui) ; son démarreur, lui, comprime le taux réel des lumières.
    if (e.twoStroke()) return static_cast<float>(kTwoStrokeGeometricCompression);
    // Les mêmes que le démarreur (`EngineConfig::toProfile`) : un moteur suralimenté comprime moins pour ne pas détoner.
    return e.burnsDiesel() ? 18.0f : (e.aspiration != Aspiration::Natural ? 9.5f : 10.5f);
}

float compressionRatioOf(const EngineConfig& e) {
    if (e.compressionRatio <= 0.0f) return stockCompressionRatio(e);
    const double low = e.burnsDiesel() ? kDieselRatioMin : kRatioMin;
    const double high = isRotary(e) ? kRotaryRatioMax : kRatioMax;
    return static_cast<float>(clamp(static_cast<double>(e.compressionRatio), low, high));
}

bool combustionStock(const EngineConfig& e) {
    // Un diesel ignore le carburant choisi : seul son taux compte.
    return e.compressionRatio <= 0.0f && (e.fuelGrade == FuelGrade::Ron95 || e.burnsDiesel());
}

double indicatedEfficiency(double ratio, double boreOverStroke, bool diesel) {
    const double expansion = portable::pow(ratio, 1.0 - kCycleGamma);
    double cycle = 1.0 - expansion;
    if (diesel) cycle = 1.0 - expansion * (portable::pow(kDieselCutoff, kCycleGamma) - 1.0) / (kCycleGamma * (kDieselCutoff - 1.0));
    const double walls = 2.0 * (ratio - 1.0) * boreOverStroke + 4.0;
    return cycle * (1.0 - kWallLoss * walls);
}

double compressionEfficiencyShare(const EngineConfig& e) {
    // Un deux-temps : son cycle intégré compte déjà le taux réglé (`twoStrokePortsOf`, son taux réel) : le compter ici aussi le
    // compterait deux fois.
    if (e.twoStroke()) return 1.0;
    const double ratio = static_cast<double>(compressionRatioOf(e));
    const double stock = static_cast<double>(stockCompressionRatio(e));
    if (isRotary(e)) return rotaryCycleEfficiency(ratio) / rotaryCycleEfficiency(stock);
    const double boreOverStroke = static_cast<double>(e.boreMm) / std::max(static_cast<double>(e.strokeMm()), 1.0);
    const bool diesel = e.burnsDiesel();
    return indicatedEfficiency(ratio, boreOverStroke, diesel) / indicatedEfficiency(stock, boreOverStroke, diesel);
}

double fuelTorqueShare(const EngineConfig& e, double lambda, double airK) {
    if (e.burnsDiesel()) return 1.0;
    const FuelProperties& fuel = fuelPropertiesOf(e);
    const double energy = energyPerAirJPerKg(fuel) / energyPerAirJPerKg(kRon95);
    // La part prise à l'air vient de l'alimentation (injection) : la même pour les deux carburants comparés.
    const double share = mixtureAirShare(e);
    return energy * densityAfterEvaporation(fuel, lambda, airK, share) / densityAfterEvaporation(kRon95, lambda, airK, share);
}

double combustionTorqueShare(const EngineConfig& e) { return compressionEfficiencyShare(e) * fuelTorqueShare(e, fullLoadLambda(e), kEvaporationAirK); }

double releasePressureShare(double ratio, double stockRatio) {
    // Otto : p4/p1 = 1 + q/(cv·T1·r^(γ−1)). La chaleur reçue par kilogramme de gaz, rapportée à cv·T1, d'une charge à 340 K.
    const double cv = kAirR / (kZoneGamma - 1.0);
    const double heat = kNetHeatShare * energyPerAirJPerKg(kRon95) / (1.0 + 1.0 / (kFullLoadLambda * kRon95.stoichiometricAfr)) / (cv * 340.0);
    const double now = 1.0 + heat * portable::pow(ratio, 1.0 - kZoneGamma);
    const double before = 1.0 + heat * portable::pow(stockRatio, 1.0 - kZoneGamma);
    return now / before;
}

// ── Le diesel ──

double dieselIgnitionDelayDeg(double ratio, double intakeK, double intakePa, double rpm, double strokeM, double cetane) {
    const double temperature = intakeK * portable::pow(ratio, kDieselPolytropic - 1.0);
    const double pressureBar = intakePa / 1e5 * portable::pow(ratio, kDieselPolytropic);
    const double pistonSpeed = 2.0 * strokeM * rpm / 60.0;
    const double activation = 618840.0 / (cetane + 25.0);
    // La corrélation ne vaut qu'au-dessus de 12,4 bar : un taux qui n'y mène pas ne s'allume plus.
    const double margin = std::max(pressureBar - 12.4, 0.5);
    const double exponent = activation * (1.0 / (8.3143 * temperature) - 1.0 / 17190.0) * portable::pow(21.2 / margin, 0.63);
    return (0.36 + 0.22 * pistonSpeed) * portable::exp(exponent);
}

float dieselKnockScale(const EngineConfig& e) {
    if (!e.burnsDiesel() || e.compressionRatio <= 0.0f) return 1.0f;
    // Au ralenti, sans pression de suralimentation : c'est là que le claquement s'entend. Le régime et la course s'annulent dans le rapport.
    const double stroke = static_cast<double>(e.strokeMm()) / 1000.0;
    const double now = dieselIgnitionDelayDeg(static_cast<double>(compressionRatioOf(e)), kDieselIdleIntakeK, kAtmosphere, 900.0, stroke);
    const double before = dieselIgnitionDelayDeg(static_cast<double>(stockCompressionRatio(e)), kDieselIdleIntakeK, kAtmosphere, 900.0, stroke);
    return static_cast<float>(now / before);
}

// ── Le cliquetis ──

double douaudEyzatDelayS(double octane, double pressurePa, double temperatureK) {
    return 17.68e-3 * portable::pow(octane / 100.0, 3.402) * portable::pow(pressurePa / kAtmosphere, -1.7) * portable::exp(3800.0 / temperatureK);
}

EndGasCycle endGasCycle(const EndGasSetup& s, double advanceDeg) {
    EndGasCycle out;
    const double ratio = s.compressionRatio;
    // La charge à la fermeture de l'admission : réchauffée par les parois, refroidie par ce qui se vaporise, mêlée aux gaz résiduels.
    const double density = s.manifoldPa / (kAirR * s.manifoldK);
    const double pickup = kWallPickup * portable::pow(std::max(density / 1.2, 0.05), -0.2);
    const double fresh = s.manifoldK + pickup * (s.coolantK + kWallOverCoolantK - s.manifoldK) - s.vaporizationCoolingK - s.extraCoolingK;
    const double ivcK = fresh + s.residualShare * (kResidualK - fresh);
    const double start = -(180.0 - s.ivcAbdcDeg);
    double volume = chamberVolume(start, ratio, s.rodRatio);
    // La masse que le remplissage fait entrer, à la température qu'elle a vraiment dans le cylindre.
    const double ivcPa = s.fillShare * s.manifoldPa * (ivcK / s.manifoldK) / volume / (1.0 - s.residualShare);
    const double mass = ivcPa * volume / (kAirR * ivcK);
    const double fuelHeat = mass * (1.0 - s.residualShare) * s.energyPerAir;
    const double heat = kCombustionShare * fuelHeat;
    const double secondsPerDeg = 1.0 / (6.0 * std::max(s.rpm, 1.0) * s.cycleSpeedShare);
    const double ignition = -advanceDeg + kernelDeg(s.rpm, s.cycleSpeedShare);
    // Douaud-Eyzat, sans les termes qui ne changent pas d'un pas à l'autre : 1/τ = exp(1,7·ln(p/p_atm) − 3800/T) / τ₀.
    const double tauZero = 17.68e-3 * portable::pow(s.octane / 100.0, 3.402);
    const double unburnedExponent = (kUnburnedGamma - 1.0) / kUnburnedGamma;
    const double logIvc = portable::log(ivcPa / kAtmosphere);
    // Woschni, de l'étincelle à l'ouverture de l'échappement : la compression reste adiabatique (la charge y est d'abord plus
    // froide que les parois, puis plus chaude ; le bilan est petit, et le même à toute avance). Tout ce qui suit est rapporté à la
    // cylindrée, comme `volume` : une chaleur en J par m³ balayé, comme p·V. Aire des parois : culasse et piston plats, et la
    // chemise découverte, πB²/2 + πB·x, x la hauteur de gaz (supposé : sans le toit de la culasse).
    const double bore = std::max(s.boreM, 0.02);
    const double stroke = std::max(s.strokeM, 0.02);
    const double pistonSpeed = 2.0 * stroke * std::max(s.rpm, 1.0) * s.cycleSpeedShare / 60.0;
    const double wallK = s.coolantK + kChamberWallOverCoolantK;
    const double boreTerm = kWoschniH * portable::pow(bore, -0.2);
    const double swirl = kWoschniCombustion * ivcK / (ivcPa * volume);
    double pressure = ivcPa;
    double motored = ivcPa;  // le même cylindre, entraîné sans combustion : la référence du terme de combustion de Woschni
    double burned = 0.0;
    double integral = 0.0;
    double work = 0.0;
    double walls = 0.0;
    double peak = ivcPa;
    double gasK = 0.0;
    double ca50 = 0.0;
    bool ended = false;
    for (double theta = start; theta < 130.0; theta += kStepDeg) {
        const double next = theta + kStepDeg;
        const double nextVolume = chamberVolume(next, ratio, s.rodRatio);
        double nextBurned = 0.0;
        if (next > ignition) {
            const double t = (next - ignition) / kBurnDeg;
            nextBurned = t >= 1.0 ? 1.0 : 1.0 - portable::exp(-kWiebeA * t * t * t);
        }
        if (burned < 0.5 && nextBurned >= 0.5) ca50 = theta + kStepDeg * (0.5 - burned) / (nextBurned - burned);
        const double dv = nextVolume - volume;
        double wall = 0.0;
        if (theta >= ignition) {
            // Les parois prennent leur part à l'état du début du pas : h·A·(T − T_paroi)·dt.
            const double meanK = pressure * volume / (mass * kAirR);
            const double gasSpeed = kWoschniPiston * pistonSpeed + swirl * std::max(pressure - motored, 0.0);
            const double h = boreTerm * portable::exp(0.8 * portable::log(pressure / 1000.0) - 0.55 * portable::log(meanK) + 0.8 * portable::log(gasSpeed));
            wall = h * (2.0 / stroke + 4.0 * volume / bore) * (meanK - wallK) * kStepDeg * secondsPerDeg;
            walls += wall;
        }
        motored = (motored * volume / (kZoneGamma - 1.0) - 0.5 * motored * dv) / (nextVolume / (kZoneGamma - 1.0) + 0.5 * dv);
        const double dq = heat * (nextBurned - burned) - wall;
        // Premier principe, gaz parfait à γ constant, pression moyenne sur le pas : U = pV/(γ−1).
        const double inner = pressure * volume / (kZoneGamma - 1.0);
        const double nextPressure = (inner + dq - 0.5 * pressure * dv) / (nextVolume / (kZoneGamma - 1.0) + 0.5 * dv);
        const double middle = 0.5 * (pressure + nextPressure);
        work += middle * dv;
        if (!ended) {
            // Les gaz frais en bout de chambre : comprimés depuis la fermeture, par le piston puis par la combustion.
            const double logRatio = portable::log(middle / kAtmosphere);
            const double unburnedK = ivcK * portable::exp(unburnedExponent * (logRatio - logIvc));
            integral += kStepDeg * secondsPerDeg * portable::exp(1.7 * logRatio - 3800.0 / unburnedK) / tauZero;
            if (nextBurned >= kKnockEndBurned) {
                ended = true;
                gasK = nextPressure * nextVolume / (mass * kAirR);
            }
        }
        if (nextPressure > peak) peak = nextPressure;
        pressure = nextPressure;
        volume = nextVolume;
        burned = nextBurned;
    }
    out.knockIntegral = integral;
    // Le travail en part de la chaleur apportée : le rapport d'une avance à l'autre est celui du couple.
    out.workShare = work / fuelHeat;
    out.wallShare = walls / fuelHeat;
    out.peakPa = peak;
    out.gasK = gasK > 0.0 ? gasK : 2500.0;
    out.ivcPa = ivcPa;
    out.ivcK = ivcK;
    out.ca50Deg = ca50;
    return out;
}

double knockingShare(double excessDeg) {
    // Somme de trois tirages uniformes, ramenée à un écart type : une cloche bornée à trois écarts types. Le cycle cliquette si sa phase
    // dépasse de `excess` ce qui manque au cycle moyen.
    const double z = -excessDeg / kPhaseSpreadDeg;
    const double s = 1.5 + 0.5 * z;
    if (s <= 0.0) return 1.0;
    if (s >= 3.0) return 0.0;
    double below = 0.0;
    if (s <= 1.0) below = s * s * s / 6.0;
    else if (s <= 2.0) below = (-2.0 * s * s * s + 9.0 * s * s - 9.0 * s + 3.0) / 6.0;
    else below = 1.0 - (3.0 - s) * (3.0 - s) * (3.0 - s) / 6.0;
    return 1.0 - below;
}

double burnedSoundMs(double gasK) {
    // Le gaz brûlé : γ ≈ 1,25 et une masse molaire de 28,7 g/mol à ces températures (Heywood, ch. 4).
    return std::sqrt(1.25 * 289.7 * gasK);
}

double draperHz(double boreM, double gasK) { return 1.841 * burnedSoundMs(gasK) / (kPi * boreM); }

// ── La carte ──

namespace {

/** Ce que voit la chambre de ce moteur à ce régime, à pleine charge stabilisée. */
EndGasSetup setupAt(const Powertrain& p, const KnockMap::Inputs& in, double rpm, double& fullBoost) {
    const EngineConfig& e = p.engine;
    const FuelProperties& fuel = fuelPropertiesOf(e);
    EndGasSetup s;
    const bool rotary = isRotary(e);
    s.compressionRatio = static_cast<double>(compressionRatioOf(e));
    s.rodRatio = rotary ? 0.0 : 3.2;
    s.cycleSpeedShare = rotary ? 2.0 / 3.0 : 1.0;
    // Le rotatif ferme sa lumière 40° d'arbre après le point mort bas : 27° de cycle (voir `kPortIvc` dans engine_sound.cpp).
    s.ivcAbdcDeg = rotary ? 40.0 * 2.0 / 3.0 : in.ivcAbdcDeg;
    // Un deux-temps comprime depuis la fermeture de sa lumière d'échappement : son taux réel (`trappedCompression`) en découle.
    if (e.twoStroke()) s.ivcAbdcDeg = 180.0 - twoStrokePortsOf(e).exhaustOpenDeg;
    s.coolantK = in.coolantK;
    s.residualShare = in.residualShare;
    s.extraCoolingK = in.extraCoolingK;
    s.lambda = fullLoadLambda(e);
    s.octane = fuel.ron;
    s.energyPerAir = energyPerAirJPerKg(fuel);
    s.rpm = rpm;
    // Un rotatif : le cylindre carré de sa chambre (`strokeMm`), faute de mieux pour l'aire de ses parois (supposé).
    s.boreM = (rotary ? static_cast<double>(e.strokeMm()) : static_cast<double>(e.boreMm)) / 1000.0;
    s.strokeM = static_cast<double>(e.strokeMm()) / 1000.0;
    const double breathing = breathingOf(p, rpm);
    fullBoost = 0.0;
    if (e.aspiration == Aspiration::Turbo) {
        const TurboSteady steady = turboSteady(turboSpecOf(p), rpm, breathing);
        s.manifoldPa = steady.chargePa;
        s.manifoldK = steady.coolK + (in.ambientK - 293.15);
        fullBoost = steady.boost;
    } else if (e.aspiration == Aspiration::Supercharger) {
        const SuperchargerSteady steady = superchargerSteady(superchargerSpecOf(p), rpm, breathing);
        s.manifoldPa = steady.chargePa;
        s.manifoldK = steady.coolK + (in.ambientK - 293.15);
        fullBoost = steady.boost;
    } else {
        s.manifoldPa = in.ambientPa - intakeDropPa(p, rpm);
        s.manifoldK = intakeAirK(p, rpm) + (in.ambientK - 293.15);
    }
    // L'air de charge, quand il agit : la charge que donnent son échangeur, l'eau et le protoxyde (`steadyManifoldCharge`).
    if (chargeAirActive(p)) {
        const ManifoldCharge charge = steadyManifoldCharge(p, rpm);
        s.manifoldPa = charge.pressurePa;
        s.manifoldK = charge.temperatureK + (in.ambientK - 293.15);
    }
    s.vaporizationCoolingK = evaporationCoolingK(fuel, s.lambda, mixtureAirShare(e), s.manifoldK, s.manifoldPa);
    // Plus froide, la charge est aussi plus dense : autant de masse en plus qu'à pleine charge (voir `fuelTorqueShare`).
    s.fillShare = kVolumetric * breathing * densityAfterEvaporation(fuel, s.lambda, s.manifoldK, mixtureAirShare(e)) /
                  densityAfterEvaporation(kRon95, s.lambda, s.manifoldK, mixtureAirShare(e));
    return s;
}

}  // namespace

void KnockMap::build(const Powertrain& p, const Inputs& in) {
    const EngineConfig& e = p.engine;
    valid_ = !p.electric && !e.burnsDiesel();
    if (!valid_) return;
    sensor_ = fuelSystemOf(e) == FuelSystem::Injection;
    offset_ = in.advanceOffsetDeg;
    degPerCycle_ = isRotary(e) ? 1.5 : 1.0;
    idleRpm_ = idleRpmOf(p);
    redlineRpm_ = redlineRpmOf(p);
    // La carte d'origine a été réglée sur le moteur d'origine, à l'essence de référence, dans l'air de référence.
    Powertrain stock = p;
    stock.engine.compressionRatio = 0.0f;
    stock.engine.fuelGrade = FuelGrade::Ron95;
    stock.engine.airFilter = AirFilter::Stock;
    // Intégration : la came, l'injection, l'air de charge et l'avance d'origine, dans l'air de référence. Rien de cela ne change
    // un moteur qui les avait déjà.
    stock.engine.ambientC = 20.0f;
    stock.engine.altitudeM = 0;
    stock.engine.camDurationDeg = 0;
    stock.engine.camLiftMm = 0.0f;
    stock.engine.lobeSeparationDeg = 0;
    stock.engine.valveTiming = ValveTiming::Fixed;
    if (stock.engine.fuelSystem == FuelSystem::Direct) stock.engine.fuelSystem = FuelSystem::Injection;
    stock.engine.ignitionOffsetDeg = 0;
    if (chargeAirActive(stock)) stock = chargeAirStock(stock);
    const Inputs reference;
    for (int i = 0; i < kRpmPoints; ++i) {
        const double rpm = idleRpm_ + (redlineRpm_ - idleRpm_) * i / (kRpmPoints - 1);
        Row& row = actual_[static_cast<std::size_t>(i)];
        Row& base = stock_[static_cast<std::size_t>(i)];
        row.rpm = rpm;
        base.rpm = rpm;
        const EndGasSetup setup = setupAt(p, in, rpm, row.fullBoost);
        const EndGasSetup stockSetup = setupAt(stock, reference, rpm, base.fullBoost);
        for (int j = 0; j < kAdvancePoints; ++j) {
            const double advance = kAdvanceMin + kAdvanceStep * j;
            const EndGasCycle cycle = endGasCycle(setup, advance);
            const EndGasCycle stockCycle = endGasCycle(stockSetup, advance);
            row.logIntegral[static_cast<std::size_t>(j)] = portable::log(std::max(cycle.knockIntegral, 1e-20));
            row.work[static_cast<std::size_t>(j)] = cycle.workShare;
            base.logIntegral[static_cast<std::size_t>(j)] = portable::log(std::max(stockCycle.knockIntegral, 1e-20));
            base.work[static_cast<std::size_t>(j)] = stockCycle.workShare;
        }
        // Le meilleur couple : le sommet du travail, celui du cycle lui-même. La carte d'origine part de celui du moteur d'origine.
        row.mbtDeg = rowBest(row);
        base.mbtDeg = rowBest(base);
        row.gasK = endGasCycle(setup, row.mbtDeg).gasK;
        base.gasK = row.gasK;
    }
}

double KnockMap::rowBest(const Row& row) {
    // Le point de la grille le plus haut, puis le sommet de la parabole que `rowWork` passe par lui et ses deux voisins.
    int best = 0;
    for (int j = 1; j < kAdvancePoints; ++j) {
        if (row.work[static_cast<std::size_t>(j)] > row.work[static_cast<std::size_t>(best)]) best = j;
    }
    const int middle = best < 1 ? 1 : (best > kAdvancePoints - 2 ? kAdvancePoints - 2 : best);
    const double before = row.work[static_cast<std::size_t>(middle - 1)];
    const double here = row.work[static_cast<std::size_t>(middle)];
    const double after = row.work[static_cast<std::size_t>(middle + 1)];
    const double curvature = after - 2.0 * here + before;
    const double x = curvature < 0.0 ? clamp(0.5 * (before - after) / curvature, -1.0, 1.0) : static_cast<double>(best - middle);
    return kAdvanceMin + kAdvanceStep * (middle + x);
}

double KnockMap::rowsAt(const std::array<Row, kRpmPoints>& rows, double rpm, double Row::*field) const {
    const double at = clamp((rpm - idleRpm_) / std::max(redlineRpm_ - idleRpm_, 1.0), 0.0, 1.0) * (kRpmPoints - 1);
    const int low = std::min(static_cast<int>(at), kRpmPoints - 2);
    const double share = at - low;
    const double a = rows[static_cast<std::size_t>(low)].*field;
    return a + (rows[static_cast<std::size_t>(low + 1)].*field - a) * share;
}

double KnockMap::invert(const Row& row, double target) {
    // Du plus retardé au plus avancé : la première avance où l'intégrale atteint la cible. Au-delà du sommet, la combustion finit
    // pendant la compression et l'intégrale retombe : ce n'est plus une avance qu'on règle.
    if (row.logIntegral[0] >= target) return kAdvanceMin - kAdvanceStep * (row.logIntegral[0] - target) / std::max(row.logIntegral[1] - row.logIntegral[0], 0.05);
    for (int j = 1; j < kAdvancePoints; ++j) {
        const double now = row.logIntegral[static_cast<std::size_t>(j)];
        const double before = row.logIntegral[static_cast<std::size_t>(j - 1)];
        if (now < before) break;
        if (now >= target) return kAdvanceMin + kAdvanceStep * (j - 1 + (target - before) / std::max(now - before, 1e-9));
    }
    return kNoLimitDeg;
}

double KnockMap::meanLimit(const std::array<Row, kRpmPoints>& rows, double rpm, double chargeShare) const {
    // L'intégrale suit la pression à la puissance 1,7 : à `chargeShare` de la pleine charge, la limite est là où elle vaut chargeShare^-1,7.
    const double target = -1.7 * portable::log(std::max(chargeShare, 1e-3));
    const double at = clamp((rpm - idleRpm_) / std::max(redlineRpm_ - idleRpm_, 1.0), 0.0, 1.0) * (kRpmPoints - 1);
    const int low = std::min(static_cast<int>(at), kRpmPoints - 2);
    const double share = at - low;
    const double a = invert(rows[static_cast<std::size_t>(low)], target);
    const double b = invert(rows[static_cast<std::size_t>(low + 1)], target);
    return (a + (b - a) * share) * degPerCycle_;
}

KnockPoint KnockMap::at(double rpm, double chargeShare) const {
    KnockPoint point;
    point.mbtDeg = rowsAt(actual_, rpm, &Row::mbtDeg) * degPerCycle_;
    point.meanLimitDeg = meanLimit(actual_, rpm, chargeShare);
    point.limitDeg = point.meanLimitDeg - kLimitSpreads * kPhaseSpreadDeg;
    const double stockLimit = meanLimit(stock_, rpm, chargeShare) - kLimitSpreads * kPhaseSpreadDeg;
    // La carte d'origine : le meilleur couple du moteur d'origine, sauf là où il cliquetterait.
    point.stockDeg = std::min(rowsAt(stock_, rpm, &Row::mbtDeg) * degPerCycle_, stockLimit - kCalibrationMarginDeg);
    point.gasK = rowsAt(actual_, rpm, &Row::gasK);
    return point;
}

double KnockMap::fullBoost(double rpm) const {
    const double at = clamp((rpm - idleRpm_) / std::max(redlineRpm_ - idleRpm_, 1.0), 0.0, 1.0) * (kRpmPoints - 1);
    const int low = std::min(static_cast<int>(at), kRpmPoints - 2);
    const double share = at - low;
    const double a = actual_[static_cast<std::size_t>(low)].fullBoost;
    return a + (actual_[static_cast<std::size_t>(low + 1)].fullBoost - a) * share;
}

double KnockMap::rowWork(const Row& row, double cycleDeg) const {
    // Une parabole par les trois points les plus proches : exacte près du meilleur couple, où la courbe en est une.
    const double at = clamp((cycleDeg - kAdvanceMin) / kAdvanceStep, 0.0, static_cast<double>(kAdvancePoints - 1));
    int middle = static_cast<int>(at + 0.5);
    middle = middle < 1 ? 1 : (middle > kAdvancePoints - 2 ? kAdvancePoints - 2 : middle);
    const double x = at - middle;
    const double before = row.work[static_cast<std::size_t>(middle - 1)];
    const double here = row.work[static_cast<std::size_t>(middle)];
    const double after = row.work[static_cast<std::size_t>(middle + 1)];
    return here + 0.5 * x * (after - before) + 0.5 * x * x * (after - 2.0 * here + before);
}

double KnockMap::torqueShare(double rpm, double advanceDeg, double referenceDeg) const {
    const double at = clamp((rpm - idleRpm_) / std::max(redlineRpm_ - idleRpm_, 1.0), 0.0, 1.0) * (kRpmPoints - 1);
    const int low = std::min(static_cast<int>(at), kRpmPoints - 2);
    const double share = at - low;
    const auto work = [&](double deg) {
        const double a = rowWork(actual_[static_cast<std::size_t>(low)], deg / degPerCycle_);
        const double b = rowWork(actual_[static_cast<std::size_t>(low + 1)], deg / degPerCycle_);
        return a + (b - a) * share;
    };
    return work(advanceDeg) / std::max(work(referenceDeg), 1e-9);
}

double KnockMap::steadyAdvanceDeg(double rpm) const {
    const KnockPoint point = at(rpm, 1.0);
    const double wanted = point.stockDeg + offset_;
    return sensor_ ? std::min(wanted, point.limitDeg) : wanted;
}

double KnockMap::steadyTorqueShare(double rpm) const {
    const KnockPoint point = at(rpm, 1.0);
    return torqueShare(rpm, steadyAdvanceDeg(rpm), point.stockDeg);
}

// ── La carte gardée ──

namespace {

struct CachedMap {
    bool used = false;
    EngineConfig engine;
    EnginePlacement placement = EnginePlacement::Front;
    KnockMap map;
};

// Quelques moteurs à la fois : la page et ses courbes en demandent un, le modèle de conduite le même. Rien ne tourne en parallèle.
constexpr int kCached = 4;
constinit CachedMap g_cache[kCached]{};
constinit int g_nextCached = 0;

}  // namespace

bool knockConditionsStock(const Powertrain& p) {
    const EngineConfig& e = p.engine;
    // Un deux-temps garde la carte de la combustion seule : ses lumières ne sont pas une came, et son air n'est pas calibré ici.
    if (e.twoStroke()) return true;
    return referenceAirOf(e) && camIsStock(e) && e.ignitionOffsetDeg == 0 && !directInjected(e) && !chargeAirActive(p);
}

KnockMap::Inputs knockInputsOf(const Powertrain& p) {
    KnockMap::Inputs in;
    const EngineConfig& e = p.engine;
    if (e.twoStroke()) return in;
    // L'air du jour arrive déjà par `turboSpecOf`, `superchargerSpecOf` et `intakeAirK` : seule la pression d'un atmosphérique
    // se lit ici. `ambientK` reste 293,15 K, sans quoi l'air serait réchauffé deux fois.
    if (!referenceAirOf(e)) in.ambientPa = ambientAirOf(e).pressurePa;
    // La came : la fermeture de l'admission place le début de la compression (cam.hpp).
    if (!camIsStock(e)) in.ivcAbdcDeg = intakeClosingAbdcDeg(camProfileOf(e));
    // L'injection directe : ce que la vaporisation dans le cylindre prend de plus qu'une injection indirecte, au point nominal.
    if (directInjected(e)) {
        const double rated = 0.88 * static_cast<double>(e.redlineRpm);
        const double ratio = static_cast<double>(compressionRatioOf(e));
        const InjectedFuel fuel = injectedFuelOf(e);
        in.extraCoolingK = chargeCooling(FuelSystem::Direct, rated, 1.0, static_cast<double>(e.redlineRpm), 0.88, ratio, fuel).atIvcK -
                           chargeCooling(FuelSystem::Injection, rated, 1.0, static_cast<double>(e.redlineRpm), 0.88, ratio, fuel).atIvcK;
    }
    // L'avance du calculateur, en écart à la carte d'origine.
    in.advanceOffsetDeg = static_cast<double>(e.ignitionOffsetDeg);
    return in;
}

const KnockMap* knockMapOf(const Powertrain& p) {
    // Intégration : la carte se construit aussi quand l'air, la came, l'avance, l'injection directe ou l'air de charge ne sont plus
    // ceux d'origine. L'eau du moteur, elle, reste celle d'un moteur chaud : la carte ne se reconstruit pas pendant la mise en
    // température (un moteur froid cliquette moins).
    if (p.electric || p.engine.burnsDiesel() || (combustionStock(p.engine) && knockConditionsStock(p))) return nullptr;
    const EnginePlacement placement = placementOf(p.car);
    for (CachedMap& entry : g_cache) {
        if (entry.used && entry.placement == placement && entry.engine == p.engine) return &entry.map;
    }
    CachedMap& entry = g_cache[g_nextCached];
    g_nextCached = (g_nextCached + 1) % kCached;
    entry.used = true;
    entry.engine = p.engine;
    entry.placement = placement;
    entry.map.build(p, knockInputsOf(p));
    return &entry.map;
}

}  // namespace revforge
