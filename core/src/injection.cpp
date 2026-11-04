#include "revforge/injection.hpp"

#include <algorithm>
#include <cmath>

#include "portable_math.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/combustion.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAirCp = 1005.0;
constexpr double kAirR = 287.0;
constexpr double kMolarR = 8.3143;
constexpr double kAtmosphereBar = 1.01325;
constexpr double kWarmK = 273.15 + kWarmCoolantC;
constexpr double kRatedShare = 0.88;

// ── Le refroidissement de la charge ──
// Part de la chaleur de vaporisation prise à l'air. Injection directe : environ 30 % du jet atteint le piston et la chemise
// et s'y vaporise (supposé, d'après Zhao, Lai et Harrington, 1999). Indirecte : la soupape et le conduit en donnent l'essentiel
// (Anderson et al., SAE 962018). Un carburateur pulvérise dans l'air, mais sa pipe est réchauffée : la moitié (supposé).
constexpr double kDirectAirShare = 0.7;
constexpr double kPortAirShare = 0.2;
constexpr double kCarbAirShare = 0.5;
// La charge à la fermeture de l'admission : l'air d'admission, réchauffé par les parois et les gaz résiduels (Heywood : 20 à 40 K).
constexpr double kChargeRiseK = 30.0;
constexpr double kCompressionExponent = 1.32;
// Une goutte se vaporise selon la loi en d² : d² = d₀² − K·t. Diamètre de Sauter d'un injecteur multitrou à 200 bar, et K dans
// une charge vers 330 K brassée par l'admission (supposés, de l'ordre de ce que publient Zhao et al.).
constexpr double kSauterAt200BarM = 13e-6;
constexpr double kSauterPressureExponent = 0.25;
constexpr double kEvaporationM2PerS = 5e-8;
// Début d'injection à 60° après le point mort haut d'admission, fermeture de l'admission à 50° après le point mort bas :
// 170° de vilebrequin pour vaporiser.
constexpr double kInjectionToIvcDeg = 170.0;

// La pompe haute pression : rendement d'une pompe à piston entraînée par came (supposé).
constexpr double kPumpEfficiency = 0.8;
// L'injecteur : à 200 bar, une pleine charge au régime maximal tient en 60 % d'un demi-tour (dimensionnement supposé),
// soit 15 cm³/s à 100 bar pour un cylindre de 500 cm³, l'ordre d'un injecteur de série.
constexpr double kNozzleReferenceM2 = 9.2e-8;
constexpr double kNozzleReferenceM3 = 0.0005;

// ── Le film d'une injection indirecte ──
// Part du jet qui touche la soupape et le conduit, injection soupape fermée (supposé : 55 %).
constexpr double kPortImpact = 0.55;
// La paroi du conduit et le dos de la soupape, au-dessus de l'eau (supposé).
constexpr double kWallOverCoolantK = 15.0;
// Le film repart en s'évaporant, ou entraîné par l'air. τ d'évaporation à chaud et à la pression atmosphérique ; τ
// d'entraînement à 50 m/s, en v^−0,75 comme celui du carburateur. Supposés : un τ total de 0,1 à 0,3 s à chaud, plus d'une
// seconde à froid, l'ordre des valeurs publiées depuis Aquino (SAE 810494).
constexpr double kEvaporationTauS = 0.25;
constexpr double kShearTauS = 2.0;
constexpr double kShearRefMs = 50.0;
constexpr double kShearMinMs = 5.0;

// Distillation d'une essence EN 228 : part évaporée selon la température (°C). Au milieu des classes d'été : E70 de 22 à 50 %,
// E100 de 46 à 71 %, E150 au moins 75 %.
constexpr int kDistillationPoints = 9;
constexpr double kDistillationC[kDistillationPoints] = {0.0, 35.0, 50.0, 70.0, 100.0, 125.0, 150.0, 180.0, 205.0};
constexpr double kDistilled[kDistillationPoints] = {0.0, 0.03, 0.10, 0.33, 0.56, 0.75, 0.88, 0.96, 1.0};

// ── Le diesel ──
// La pompe mécanique (pompe distributrice) : sa pression suit le régime de la pompe et le débit (supposé, de 250 à 850 bar).
constexpr double kPumpBaseBar = 150.0;
constexpr double kPumpSpanBar = 700.0;
// La rampe commune : de 250 à 1600 bar selon la charge et le régime (deuxième génération).
constexpr double kRailBaseBar = 250.0;
constexpr double kRailSpanBar = 1350.0;
// Une pleine charge au régime maximal tient en 25° de vilebrequin, quel que soit le système : chacun a l'injecteur de sa pression.
constexpr double kFullInjectionDeg = 25.0;
// Injection pilote : 1,5 mg pour un cylindre de 500 cm³, jamais plus de 30 % de l'injection (supposé, de 1 à 2 mg publiés).
constexpr double kPilotMgPer500 = 1.5;
constexpr double kPilotMaxShare = 0.3;
constexpr double kPilotLeadDeg = 12.0;
// Le pic de prémélange dure environ 0,8 ms (7° vers 1500 tr/min, Heywood : de 5 à 10°).
constexpr double kPremixSeconds = 0.0008;
// Le mélange après la fin de l'injection : le tourbillon d'admission suit le régime, si bien qu'il dure à peu près le même angle
// (Heywood : la combustion d'un diesel à injection directe dure de 40 à 60° à tout régime), et le jet l'accélère en √Δp
// (supposé : 30° à 800 bar).
constexpr double kMixingDeg = 30.0;
constexpr double kMixingRefBar = 800.0;
constexpr double kRailNozzleM = 0.13e-3;
// Limite de fumée : λ_min = 1 + c/√p. À 600 bar, λ = 1,43 : l'excès d'air de la pleine charge du diesel d'origine,
// 8,5 bar de PME plus 2 bar de frottements à 46 % de rendement indiqué (voir docs/modele.md).
constexpr double kSmokeCoefficient = 0.43 * 24.494897427831781;
constexpr double kConnectingRodRatio = 3.3;
constexpr double kExpansionGamma = 1.3;
// Le centre de gravité de la combustion, réglé au meilleur couple (8° après le point mort haut).
constexpr double kCentroidDeg = 8.0;
// Pertes aux parois, en part de l'énergie du carburant à charge partielle (Heywood : de 15 à 35 %).
constexpr double kWallLossPetrol = 0.22;
constexpr double kWallLossDiesel = 0.18;
// Wiebe (a = 6,9) : moyenne et variance, en part de la durée. m = 1 pour la diffusion d'un diesel, m = 2 pour un essence.
constexpr double kWiebeMeanM1 = 0.337381;
constexpr double kWiebeVarianceM1 = 0.031101;
constexpr double kWiebeMeanM2 = 0.469030;
constexpr double kWiebeVarianceM2 = 0.029057;
// La combustion d'un moteur à essence, de 0 à 100 % : 45° (supposé, de l'ordre de Heywood), taux de compression de référence.
constexpr double kPetrolBurnDeg = 45.0;
constexpr double kPetrolCompression = 10.5;

// ── La recirculation ──
// Part la plus forte : 15 % sur un essence (EGR externe refroidi), 30 % sur un diesel à faible charge (supposés, plages publiées).
constexpr double kEgrPetrol = 0.15;
constexpr double kEgrDiesel = 0.30;
// La flamme : température des gaz frais en fin de compression, élévation adiabatique, paroi.
constexpr double kUnburnedK = 700.0;
constexpr double kFlameRiseK = 1900.0;
constexpr double kWallK = 450.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

double smoothstep(double edge0, double edge1, double x) {
    const double t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/** Volume au-dessus du piston, en part du volume mort : bielle-manivelle, angle depuis le point mort haut (degrés). */
double volumeShare(double ratio, double deg) {
    const double theta = deg * kPi / 180.0;
    const double s = portable::sin(theta);
    const double root = std::sqrt(kConnectingRodRatio * kConnectingRodRatio - s * s);
    return 1.0 + 0.5 * (ratio - 1.0) * (kConnectingRodRatio + 1.0 - portable::cos(theta) - root);
}

/** Rendement de la chaleur dégagée à cet angle, détendue jusqu'au point mort bas, rapporté au cycle idéal. */
double releaseEfficiency(double ratio, double deg) {
    const double expansion = portable::pow(volumeShare(ratio, deg) / ratio, kExpansionGamma - 1.0);
    return (1.0 - expansion) / (1.0 - portable::pow(ratio, 1.0 - kExpansionGamma));
}

/**
 * Degré d'isochore d'une combustion de cet étalement autour de son centre : η(θc) + ½·η''(θc)·σ². Une combustion plus
 * longue dégage de la chaleur plus loin du point mort haut, où elle se détend moins.
 */
double spreadEfficiency(double ratio, double varianceDeg2) {
    constexpr double kStep = 10.0;
    const double center = releaseEfficiency(ratio, kCentroidDeg);
    const double curvature = (releaseEfficiency(ratio, kCentroidDeg + kStep) + releaseEfficiency(ratio, kCentroidDeg - kStep) - 2.0 * center) / (kStep * kStep);
    return center + 0.5 * curvature * varianceDeg2;
}

double interpolate(const double* xs, const double* ys, int count, double x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < count; ++i) {
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[count - 1];
}

/** La température de paroi où l'essence s'évapore autant qu'à la pression atmosphérique : Clausius-Clapeyron. */
double equivalentC(double wallK, double pressureShare, double volatilityK) {
    const double share = std::max(pressureShare, 0.05);
    return 1.0 / (1.0 / wallK + portable::log(share) / volatilityK) - 273.15;
}

}  // namespace

// ── Le refroidissement de la charge ──

double vaporizationDropK(double lambda, const InjectedFuel& fuel) {
    const double fuelPerAir = 1.0 / (fuel.stoichiometricAfr * std::max(lambda, 0.3));
    return fuelPerAir * fuel.vaporizationJPerKg / (kAirCp + fuelPerAir * fuel.vaporCpJPerKgK);
}

double directRailBar(double load, double rpm, double redlineRpm) {
    return 40.0 + 160.0 * clamp(0.7 * load + 0.3 * rpm / std::max(redlineRpm, 1.0), 0.0, 1.0);
}

double evaporatedBeforeIvc(double rpm, double railBar) {
    const double sauter = kSauterAt200BarM * portable::pow(200.0 / std::max(railBar, 10.0), kSauterPressureExponent);
    const double lifetime = sauter * sauter / kEvaporationM2PerS;
    const double available = kInjectionToIvcDeg / (6.0 * std::max(rpm, 100.0));
    // Une goutte perd sa masse en d³ : ce qui reste est (1 − t/t_v)^1,5.
    const double left = std::max(1.0 - available / lifetime, 0.0);
    return 1.0 - left * std::sqrt(left);
}

double mixtureAirShare(const EngineConfig& e) { return hasCarburetor(e) ? kCarbAirShare : kPortAirShare; }

InjectedFuel injectedFuelOf(const EngineConfig& e) {
    const FuelProperties& props = fuelPropertiesOf(e);
    InjectedFuel fuel;
    fuel.vaporizationJPerKg = props.vaporizationJPerG * 1000.0;
    fuel.stoichiometricAfr = props.stoichiometricAfr;
    fuel.densityKgM3 = props.densityKgPerL * 1000.0;
    fuel.lowerHeatingJPerKg = props.lowerHeatingJPerG * 1000.0;
    // L·M/R : la pente de Clausius-Clapeyron de ce qui reste en film.
    fuel.volatilityK = props.vaporizationJPerG * props.molarMassGPerMol / 8.314;
    return fuel;
}

ChargeCooling chargeCooling(FuelSystem system, double rpm, double load, double redlineRpm, double lambda, double effectiveCompression, const InjectedFuel& fuel) {
    ChargeCooling out;
    const double full = vaporizationDropK(lambda, fuel);
    const double amplify = portable::pow(std::max(effectiveCompression, 1.0), kCompressionExponent - 1.0);
    if (system == FuelSystem::Direct) {
        out.airShare = kDirectAirShare;
        out.evaporatedShare = evaporatedBeforeIvc(rpm, directRailBar(load, rpm, redlineRpm));
        out.atIvcK = kDirectAirShare * out.evaporatedShare * full;
        // Ce qui reste se vaporise pendant la compression, vers le milieu de la course : la moitié de l'élévation.
        const double late = kDirectAirShare * (1.0 - out.evaporatedShare) * full;
        out.atTdcK = out.atIvcK * amplify + late * portable::pow(0.5 * (std::max(effectiveCompression, 1.0) + 1.0), kCompressionExponent - 1.0);
        return out;
    }
    out.airShare = system == FuelSystem::Injection ? kPortAirShare : kCarbAirShare;
    out.atIvcK = out.airShare * full;
    out.atTdcK = out.atIvcK * amplify;
    return out;
}

double directFillGain(double rpm, double redlineRpm, double intakeK, const InjectedFuel& fuel) {
    // À pleine charge, la richesse du meilleur couple. La référence est le même moteur en injection indirecte.
    constexpr double kFullLambda = 0.88;
    const double rail = directRailBar(1.0, rpm, redlineRpm);
    const double full = vaporizationDropK(kFullLambda, fuel);
    const double extra = (kDirectAirShare * evaporatedBeforeIvc(rpm, rail) - kPortAirShare) * full;
    const double charge = intakeK + kChargeRiseK;
    return charge / (charge - extra) - 1.0;
}

double directPumpNm(double fuelGps, double railBar, double rpm, const InjectedFuel& fuel) {
    const double omega = std::max(rpm, 100.0) * 2.0 * kPi / 60.0;
    const double flowM3s = fuelGps * 1e-3 / fuel.densityKgM3;
    return flowM3s * railBar * 1e5 / kPumpEfficiency / omega;
}

double directNozzleAreaM2(double cylinderM3) { return kNozzleReferenceM2 * cylinderM3 / kNozzleReferenceM3; }

double injectionSeconds(double fuelKg, double railBar, double areaM2, double densityKgM3) {
    const double speed = std::sqrt(2.0 * std::max(railBar, 1.0) * 1e5 / densityKgM3);
    return fuelKg / (densityKgM3 * areaM2 * speed);
}

// ── Le film ──

double distilledShare(double tempC) { return interpolate(kDistillationC, kDistilled, kDistillationPoints, tempC); }

FilmParameters portFilmParameters(double coolantC, double manifoldShare, double portSpeedMs, const InjectedFuel& fuel) {
    FilmParameters out;
    const double wallK = coolantC + 273.15 + kWallOverCoolantK;
    const double share = clamp(manifoldShare, 0.05, 3.0);
    // Ce qui s'évapore d'un coup sur la paroi chaude ne reste pas : sous vide, l'essence distille plus bas.
    out.deposit = kPortImpact * (1.0 - distilledShare(equivalentC(wallK, share, fuel.volatilityK)));
    // Le reste s'évapore d'autant plus lentement que sa tension de vapeur est basse, et que la pression est haute.
    const double referenceK = kWarmK + kWallOverCoolantK;
    const double evaporation = kEvaporationTauS * portable::exp(fuel.volatilityK * (1.0 / wallK - 1.0 / referenceK)) * share;
    const double root = std::sqrt(kShearRefMs / std::max(portSpeedMs, kShearMinMs));
    const double shear = kShearTauS * root * std::sqrt(root);
    out.tauS = 1.0 / (1.0 / evaporation + 1.0 / shear);
    return out;
}

FilmParameters coldFilmShare(double coolantC, double manifoldShare, const InjectedFuel& fuel) {
    const FilmParameters cold = portFilmParameters(coolantC, manifoldShare, kShearRefMs, fuel);
    const FilmParameters warm = portFilmParameters(kWarmCoolantC, manifoldShare, kShearRefMs, fuel);
    FilmParameters out;
    out.deposit = cold.deposit / std::max(warm.deposit, 1e-6);
    out.tauS = cold.tauS / warm.tauS;
    return out;
}

void PortFuelFilm::step(const PortFilmInput& in, double dt) {
    const FilmParameters real = portFilmParameters(in.coolantC, in.manifoldShare, in.portSpeedMs);
    // Le calculateur a sa table, calibrée moteur chaud : à 90 °C, elle est la vraie.
    const FilmParameters known = portFilmParameters(kWarmCoolantC, in.manifoldShare, in.portSpeedMs);
    const double wanted = std::max(in.wantedGps, 0.0);
    if (!primed_) {
        // À l'équilibre, ce que la paroi rend égale ce qu'elle reçoit : film = X·injecté·τ, et injecté = demandé.
        film_.massG = real.deposit * wanted * real.tauS;
        estimate_.massG = known.deposit * wanted * known.tauS;
        primed_ = true;
    }
    // Le modèle inverse : injecter ce qui, une fois la paroi servie, donne au cylindre ce que l'air demande.
    const double believed = estimate_.release(known.tauS);
    injectedGps_ = std::max((wanted - believed) / (1.0 - known.deposit), 0.0);
    estimate_.advance(known.deposit * injectedGps_, believed, dt);
    const double fromWall = film_.release(real.tauS);
    film_.advance(real.deposit * injectedGps_, fromWall, dt);
    const double received = (1.0 - real.deposit) * injectedGps_ + fromWall;
    if (wanted <= 1e-9) {
        richnessShare_ = 1.0;
        torqueShare_ = 1.0;
        return;
    }
    richnessShare_ = received / wanted;
    // Une injection régulée vise λ = 1 : le couple suit la richesse autour de ce point.
    torqueShare_ = torqueOfRichness(richnessShare_) / torqueOfRichness(1.0);
}

// ── Le diesel ──

double dieselInjectionBar(bool commonRail, double rpm, double redlineRpm, double load) {
    const double rated = kRatedShare * std::max(redlineRpm, 1000.0);
    const double regime = rpm / rated;
    if (!commonRail) return kPumpBaseBar + kPumpSpanBar * std::min(regime, 1.0) * (0.5 + 0.5 * clamp(load, 0.0, 1.0));
    const double demand = clamp(0.6 * clamp(load, 0.0, 1.0) + 0.4 * std::min(regime, 1.0), 0.0, 1.0);
    return kRailBaseBar + kRailSpanBar * demand * std::sqrt(demand);
}

double ignitionDelayDeg(double tdcBar, double tdcK, double meanPistonMs, double cetane) {
    // Hardenberg et Hase (1979), Heywood éq. 10.37 : énergie d'activation apparente selon l'indice de cétane.
    const double activation = 618840.0 / (cetane + 25.0);
    const double pressure = std::max(tdcBar - 12.4, 1.0);
    const double exponent = activation * (1.0 / (kMolarR * tdcK) - 1.0 / 17190.0) + portable::pow(21.2 / pressure, 0.63);
    return (0.36 + 0.22 * meanPistonMs) * portable::exp(exponent);
}

double sprayBreakupSeconds(double nozzleM, double fuelKgM3, double airKgM3, double injectionBar) {
    return 28.65 * fuelKgM3 * nozzleM / std::sqrt(airKgM3 * injectionBar * 1e5);
}

double smokeLimitLambda(double injectionBar) { return 1.0 + kSmokeCoefficient / std::sqrt(std::max(injectionBar, 50.0)); }

DieselBurn dieselBurn(const DieselPoint& point, const DieselFuel& fuel) {
    DieselBurn out;
    const double rpm = std::max(point.rpm, 300.0);
    const double load = clamp(point.load, 0.02, 1.0);
    const double rated = kRatedShare * std::max(point.redlineRpm, 1000.0);
    const double degPerSecond = 6.0 * rpm;
    const double ratio = point.compressionRatio;
    const double fuelMg = load * point.fullFuelMg;
    out.injectionBar = dieselInjectionBar(point.commonRail, rpm, point.redlineRpm, load);
    // Le débit de l'injection, par degré. Une pompe mécanique refoule par sa came : le même volume par degré à tout régime, sa
    // pression en découle. Une rampe commune débite par un orifice, à sa pression : le même débit par seconde, donc moins par
    // degré quand le régime monte. Chacune a l'injecteur qui fait la pleine charge au régime maximal en 25°.
    double rate = point.fullFuelMg / kFullInjectionDeg;
    if (point.commonRail) rate *= std::sqrt(out.injectionBar / (kRailBaseBar + kRailSpanBar)) * rated / rpm;
    // La charge au point mort haut, compression polytropique.
    const double ivcK = point.intakeK + kChargeRiseK;
    const double tdcK = ivcK * portable::pow(ratio, 0.35);
    const double tdcBar = point.intakeBar * portable::pow(ratio, 1.35);
    const double piston = 2.0 * point.strokeM * rpm / 60.0;
    // Les gaz recirculés apportent moins d'oxygène : 23,2 % × (1 − x/λ) au lieu de 23,2 %. L'allumage traîne, la flamme ralentit.
    const double oxygen = clamp(1.0 - point.egrRate / std::max(point.lambda, 1.0), 0.3, 1.0);
    const double premixDeg = kPremixSeconds * degPerSecond / oxygen;

    double pilot = 0.0;
    if (point.commonRail) {
        pilot = std::min(kPilotMgPer500 * point.cylinderM3 / 0.0005, kPilotMaxShare * fuelMg);
        // L'injection principale arrive dans la flamme de la pilote : il ne reste que le délai physique, le jet qui se brise.
        const double airKgM3 = point.intakeBar * 1e5 / (kAirR * ivcK) * ratio;
        out.delayDeg = sprayBreakupSeconds(kRailNozzleM, fuel.densityKgM3, airKgM3, out.injectionBar) * degPerSecond / oxygen;
    } else {
        out.delayDeg = ignitionDelayDeg(tdcBar, tdcK, piston, fuel.cetane) / oxygen;
    }
    const double main = fuelMg - pilot;
    // Ce qui est injecté pendant le délai brûle d'un coup, prémélangé ; le reste brûle en se mélangeant.
    const double premix = std::min(rate * out.delayDeg, main);
    out.premixShare = main > 0.0 ? premix / main : 0.0;
    const double diffusion = main - premix;
    // La durée de la diffusion : l'injection, puis le mélange, en 1/√Δp.
    const double mixing = kMixingDeg * std::sqrt(kMixingRefBar / out.injectionBar);
    out.durationDeg = (main / rate + mixing) / oxygen;

    // L'étalement autour du centre de gravité : pilote en avance, prémélange, diffusion (Wiebe, m = 1).
    const double share = 1.0 / std::max(fuelMg, 1e-6);
    const double pilotShare = pilot * share;
    const double premixShare = premix * share;
    const double diffusionShare = diffusion * share;
    const double pilotMean = -kPilotLeadDeg + 0.5 * premixDeg;
    const double premixMean = 0.5 * premixDeg;
    const double diffusionMean = kWiebeMeanM1 * out.durationDeg;
    const double mean = pilotShare * pilotMean + premixShare * premixMean + diffusionShare * diffusionMean;
    const double triangleVariance = premixDeg * premixDeg / 24.0;
    const double variance = pilotShare * (triangleVariance + (pilotMean - mean) * (pilotMean - mean)) +
                            premixShare * (triangleVariance + (premixMean - mean) * (premixMean - mean)) +
                            diffusionShare * (kWiebeVarianceM1 * out.durationDeg * out.durationDeg + (diffusionMean - mean) * (diffusionMean - mean));
    out.efficiency = spreadEfficiency(ratio, variance);

    // La pente de pression que la combustion impose : dp/dθ = (γ − 1)·(dQ/dθ)/V, à chaque pic, placé autour du centre de
    // gravité. Un pic tardif tombe dans un volume plus grand : il monte moins. La détente, elle, ne fait pas de bruit.
    const double start = kCentroidDeg - mean;
    const double clearance = point.cylinderM3 / (ratio - 1.0);
    const double joulesPerMg = fuel.lowerHeatingJPerKg * 1e-6;
    const auto rise = [&](double deg, double mgPerDeg) {
        return (kExpansionGamma - 1.0) * mgPerDeg * joulesPerMg / (volumeShare(ratio, deg) * clearance) / 1e5;
    };
    // Le triangle du prémélange culmine à mi-course, au double de sa moyenne ; la diffusion à u = 1/√(2a), au plus au débit injecté.
    constexpr double kWiebeModeM1 = 0.26919;
    constexpr double kWiebePeakM1 = 2.2532;
    double peak = rise(start + premixMean, 2.0 * premix / premixDeg);
    if (pilot > 0.0) peak = std::max(peak, rise(start + pilotMean, 2.0 * pilot / premixDeg));
    const double diffusionRate = std::min(kWiebePeakM1 * diffusion / out.durationDeg, rate * oxygen);
    peak = std::max(peak, rise(start + kWiebeModeM1 * out.durationDeg, diffusionRate));
    out.peakRiseBarPerDeg = peak;
    return out;
}

double commonRailTorqueShare(const DieselPoint& fullLoad) {
    DieselPoint mechanical = fullLoad;
    mechanical.commonRail = false;
    mechanical.load = 1.0;
    DieselPoint rail = mechanical;
    rail.commonRail = true;
    const DieselBurn before = dieselBurn(mechanical);
    const DieselBurn after = dieselBurn(rail);
    // À la limite de fumée, le carburant suit l'air sur λ_min : la rampe commune en brûle plus avant de fumer.
    const double smoke = smokeLimitLambda(before.injectionBar) / smokeLimitLambda(after.injectionBar);
    return smoke * after.efficiency / before.efficiency;
}

double dieselKnockShare(const DieselPoint& point) {
    DieselPoint mechanical = point;
    mechanical.commonRail = false;
    mechanical.egrRate = 0.0;
    const DieselBurn before = dieselBurn(mechanical);
    const DieselBurn after = dieselBurn(point);
    return after.peakRiseBarPerDeg / before.peakRiseBarPerDeg;
}

// ── La recirculation ──

double egrRate(bool diesel, double load, double rpm, double redlineRpm) {
    const double regime = rpm / std::max(redlineRpm, 1000.0);
    const double speed = 1.0 - smoothstep(0.55, 0.85, regime);
    if (diesel) return kEgrDiesel * (1.0 - smoothstep(0.35, 0.75, load)) * speed;
    // Pas au ralenti : la combustion y est déjà fragile. Pas à pleine charge : chaque gramme d'air y fait du couple.
    return kEgrPetrol * smoothstep(0.12, 0.3, load) * (1.0 - smoothstep(0.55, 0.85, load)) * speed;
}

double egrFlameSpeedShare(double rate) {
    if (rate <= 0.0) return 1.0;
    return std::max(1.0 - 2.06 * portable::pow(rate, 0.77), 0.1);
}

double egrBurnStretch(double rate) {
    if (rate <= 0.0) return 1.0;
    return portable::pow(egrFlameSpeedShare(rate), -1.0 / 3.0);
}

double egrWallLossShare(double dilution) {
    if (dilution <= 0.0) return 1.0;
    // La chaleur dégagée chauffe une masse plus grande de 1/(1 − x) : la flamme est plus froide de x·ΔT_ad.
    const double hot = kUnburnedK + kFlameRiseK;
    const double diluted = kUnburnedK + kFlameRiseK * (1.0 - dilution);
    return portable::pow(diluted / hot, -0.53) * (diluted - kWallK) / (hot - kWallK);
}

double egrEfficiencyShare(bool diesel, double rate, double lambda) {
    if (rate <= 0.0) return 1.0;
    const double wall = diesel ? kWallLossDiesel : kWallLossPetrol;
    // Moins chaude, la flamme perd moins aux parois : ce qui n'y part plus devient du travail, au rendement du cycle. Un diesel
    // brûle à la flamme de diffusion, à richesse 1 : seul l'oxygène en moins la refroidit.
    const double dilution = diesel ? rate / std::max(lambda, 1.0) : rate;
    const double cooler = (1.0 - wall * egrWallLossShare(dilution)) / (1.0 - wall);
    double slower = 1.0;
    if (diesel) {
        // L'oxygène manque : la diffusion s'allonge en 1/[O₂].
        const double oxygen = clamp(1.0 - rate / std::max(lambda, 1.0), 0.3, 1.0);
        const double duration = 50.0;
        const double before = spreadEfficiency(18.0, kWiebeVarianceM1 * duration * duration);
        const double stretched = duration / oxygen;
        slower = spreadEfficiency(18.0, kWiebeVarianceM1 * stretched * stretched) / before;
    } else {
        const double stretched = kPetrolBurnDeg * egrBurnStretch(rate);
        slower = spreadEfficiency(kPetrolCompression, kWiebeVarianceM2 * stretched * stretched) /
                 spreadEfficiency(kPetrolCompression, kWiebeVarianceM2 * kPetrolBurnDeg * kPetrolBurnDeg);
    }
    return cooler * slower;
}

double egrCombustionNoiseDb(bool diesel, double rate, double lambda) {
    if (rate <= 0.0) return 0.0;
    if (diesel) {
        // Le pic de prémélange s'étale en 1/[O₂] ; le délai plus long y met plus de carburant, sauf quand tout y est déjà.
        const double oxygen = clamp(1.0 - rate / std::max(lambda, 1.0), 0.3, 1.0);
        return 20.0 / portable::kLn10 * portable::log(oxygen);
    }
    // Un essence : la pente de pression suit la vitesse de combustion.
    return -20.0 / portable::kLn10 * portable::log(egrBurnStretch(rate));
}

double combustionNoiseDb(const CombustionNoiseEngine& e, double load, double rpm) {
    if (!e.diesel) return e.egr ? egrCombustionNoiseDb(false, egrRate(false, load, rpm, e.redlineRpm), 1.0) : 0.0;
    DieselPoint point;
    point.rpm = rpm;
    point.load = std::max(load, 0.05);
    point.redlineRpm = e.redlineRpm;
    point.cylinderM3 = e.cylinderM3;
    point.strokeM = e.strokeM;
    // La pression du turbo vient avec la charge et le régime : elle ne compte ici que pour le délai d'allumage.
    point.intakeBar = e.charged ? 1.0 + e.boostBar * clamp(load, 0.0, 1.0) * std::min(rpm / (0.5 * e.redlineRpm), 1.0) : 1.0;
    const double smoke = smokeLimitLambda(dieselInjectionBar(false, rpm, e.redlineRpm, 1.0));
    point.fullFuelMg = 1.2 * point.intakeBar * point.cylinderM3 * 0.92 * 1e6 / (14.5 * smoke);
    point.commonRail = e.commonRail;
    point.lambda = smoke / point.load;
    point.egrRate = e.egr ? egrRate(true, load, rpm, e.redlineRpm) : 0.0;
    return 20.0 / portable::kLn10 * portable::log(dieselKnockShare(point));
}

// ── La fiche ──

InjectionSummary injectionSummary(const EngineConfig& e) {
    InjectionSummary out;
    const double redline = static_cast<double>(e.redlineRpm);
    const double peakRpm = 0.6 * redline;
    if (directInjected(e)) {
        const InjectedFuel fuel = injectedFuelOf(e);
        out.coolingK = chargeCooling(FuelSystem::Direct, peakRpm, 1.0, redline, 0.88, 9.0, fuel).atIvcK;
        out.fillGainShare = directFillGain(peakRpm, redline, 293.0, fuel);
        out.railBar = directRailBar(1.0, peakRpm, redline);
    }
    if (e.burnsDiesel()) {
        const bool rail = commonRailed(e);
        out.railBar = dieselInjectionBar(rail, peakRpm, redline, 1.0);
        if (rail) {
            DieselPoint point;
            point.redlineRpm = redline;
            point.cylinderM3 = static_cast<double>(e.displacementCc) * 1e-6 / std::max(e.cylinders, 1);
            point.strokeM = static_cast<double>(e.strokeMm()) / 1000.0;
            point.fullFuelMg = 80.0 * point.cylinderM3 / 0.0005 * (e.aspiration == Aspiration::Natural ? 0.5 : 1.0);
            point.intakeBar = e.aspiration == Aspiration::Natural ? 1.0 : 1.0 + 0.6 * static_cast<double>(e.boostBar);
            point.rpm = 1500.0;
            point.commonRail = true;
            out.lowEndTorqueShare = commonRailTorqueShare(point) - 1.0;
            point.rpm = 0.2 * redline;
            point.load = 0.15;
            point.intakeBar = 1.0;
            out.knockDb = 20.0 / portable::kLn10 * portable::log(dieselKnockShare(point));
        }
    }
    if (recirculated(e)) {
        double most = 0.0;
        for (int i = 1; i <= 20; ++i) most = std::max(most, egrRate(e.burnsDiesel(), 0.05 * i, 0.3 * redline, redline));
        out.egrShare = most;
    }
    return out;
}

}  // namespace revforge
