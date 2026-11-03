#include "revforge/turbo_pipe.hpp"

#include <cmath>

#include "portable_math.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kReferenceM = 0.4;
constexpr double kAmbientK = 293.0;
constexpr double kGasCp = 1150.0;
constexpr double kGasR = 287.0;
constexpr double kGasGamma = 4.0 / 3.0;

// ── Ordres de grandeur, tous supposés : aucun n'a été réglé sur un retard voulu ──
// Le tube chaud : 50 mm sur un 2 L, le même diamètre que le gaz à l'écran (`pipeDiameter` dans fluidGas.ts).
constexpr double kPipeDiameterAt2L = 0.05;
// Acier inoxydable de 1,5 mm : 7900 kg/m³, 500 J/(kg·K).
constexpr double kWallJPerM2K = 7900.0 * 0.0015 * 500.0;
// Le collecteur coulé et la volute, en équivalent de chaleur à prendre sur un 2 L : 3 kg d'acier.
constexpr double kHeadJPerKAt2L = 1500.0;
// Gaz à 900 K environ : viscosité et conductivité de l'air chaud, Pr^0,4 pour Pr = 0,7 (tables usuelles).
constexpr double kGasViscosity = 3.9e-5;
constexpr double kGasConductivity = 0.06;
constexpr double kPrandtlTerm = 0.867;
// Le flux pulsé échange davantage qu'un tube en régime établi (Caton et Heywood, 1981, le mesurent près des soupapes).
constexpr double kPulsingHeat = 1.5;
// Un filet de gaz n'échange plus que par conduction : Nu ≈ 4 en laminaire.
constexpr double kLaminarNusselt = 4.0;
// Dehors : convection sous la voiture et rayonnement linéarisé (ε ≈ 0,6 à 600 K donne déjà 30 W/(m²·K)).
constexpr double kOutsideWPerM2K = 50.0;
// Frottement d'un tube d'acier, et densité du gaz à 1,3 bar et 1100 K, pour la perte de charge.
constexpr double kPipeFriction = 0.02;
constexpr double kPipeGasDensity = 0.41;
// Sous ce débit, le calcul de l'échange ne dit plus rien : c'est un moteur à l'arrêt.
constexpr double kMinFlow = 1e-4;

double clampDouble(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** Ce que la paroi échange avec le gaz (W/(m²·K)) : Dittus-Boelter, majoré parce que le flux pulse. */
double insideCoefficient(const TurboPipeSpec& pipe, double gas) {
    const double reynolds = 4.0 * gas / (kPi * pipe.diameterM * kGasViscosity);
    const double nusselt = 0.023 * portable::pow(reynolds, 0.8) * kPrandtlTerm * kPulsingHeat;
    return (nusselt > kLaminarNusselt ? nusselt : kLaminarNusselt) * kGasConductivity / pipe.diameterM;
}

/** La part de l'écart entre gaz et paroi que le gaz perd sur la surface du tube ajouté ou retiré. */
double exchangedShare(const TurboPipeSpec& pipe, double gas) {
    const double units = insideCoefficient(pipe, gas) * pipe.surfaceM2 / (gas * kGasCp);
    return 1.0 - portable::exp(units < 30.0 ? -units : -30.0);
}

/** À l'équilibre, le gaz chauffe la paroi autant qu'elle se refroidit dehors : la part de l'écart à l'ambiance que garde la paroi. */
double steadyWallShare(const TurboPipeSpec& pipe, double gas, double exchanged) {
    const double taken = exchanged * gas * kGasCp;
    return taken / (taken + kOutsideWPerM2K * pipe.surfaceM2);
}

/** Le temps que met ce volume à suivre un écart de débit : τ = V·(dp/dṁ)/(γ·R·T), dp/dṁ tiré de la loi de l'ellipse. */
double fillSeconds(double volumeM3, double inPa, double outPa, double gasK, double passage) {
    const double over = inPa * inPa - outPa * outPa;
    if (over <= 0.0) return 0.0;
    return volumeM3 * std::sqrt(over) / (kGasGamma * kGasR * std::sqrt(gasK) * passage * inPa);
}

}  // namespace

TurboPipeSpec turboPipeSpecFor(int pipeMm, double displacementM3, int cylinders) {
    TurboPipeSpec p;
    if (pipeMm == 400) return p;
    const double length = clampDouble(static_cast<double>(pipeMm) * 1e-3, 0.15, 3.0);
    p.active = true;
    p.extraM = length - kReferenceM;
    p.cylinders = cylinders;
    p.diameterM = clampDouble(kPipeDiameterAt2L * std::sqrt(displacementM3 / 0.002), 0.032, 0.09);
    const double section = 0.25 * kPi * p.diameterM * p.diameterM;
    const double along = p.extraM > 0.0 ? p.extraM : -p.extraM;
    p.surfaceM2 = kPi * p.diameterM * along;
    p.volumeM3 = section * length;
    p.referenceVolumeM3 = section * kReferenceM;
    p.extraVolumeM3 = p.extraM > 0.0 ? section * p.extraM : 0.0;
    p.wallJPerK = kWallJPerM2K * p.surfaceM2;
    p.frictionDrop = kPipeFriction * p.extraM / p.diameterM / (2.0 * kPipeGasDensity * section * section);
    // Sous la référence, c'est la chaleur à prendre qui baisse : le collecteur et la volute restent, le tube retiré non.
    if (p.extraM < 0.0) {
        const double head = kHeadJPerKAt2L * displacementM3 / 0.002;
        const double perMetre = kWallJPerM2K * kPi * p.diameterM;
        p.headShare = (head + perMetre * length) / (head + perMetre * kReferenceM);
    }
    return p;
}

double turboPipeSteadyK(const TurboPipeSpec& pipe, double gas, double inletK) {
    double flow = gas > kMinFlow ? gas : kMinFlow;
    // Plus court, on rend au gaz ce que le tube retiré lui aurait pris. Moteur entraîné, presque sans débit, cette inversion n'a plus de sens :
    // le tube aurait tout pris, et le gaz atteindrait 2000 °C. On s'arrête au débit d'un ralenti soutenu : 20 g/s sur 2 L.
    if (pipe.extraM <= 0.0) {
        const double scale = pipe.diameterM / kPipeDiameterAt2L;
        const double floor = 0.02 * scale * scale;
        if (flow < floor) flow = floor;
    }
    const double exchanged = exchangedShare(pipe, flow);
    const double wall = steadyWallShare(pipe, flow, exchanged);
    // Sortie du tube : une paroi à `wall`, et ce que le gaz garde de l'écart avec elle.
    const double kept = wall + (1.0 - exchanged) * (1.0 - wall);
    if (pipe.extraM > 0.0) return kAmbientK + (inletK - kAmbientK) * kept;
    // La calibration compte déjà 400 mm : les retirer rend au gaz ce qu'il y aurait perdu.
    return kAmbientK + (inletK - kAmbientK) / kept;
}

double turboPipePulseShare(const TurboPipeSpec& pipe, double rpm, double inPa, double outPa, double gasK, double passage) {
    const double omega = 2.0 * kPi * static_cast<double>(pipe.cylinders) * rpm / 120.0;
    const double reference = omega * fillSeconds(pipe.referenceVolumeM3, inPa, outPa, gasK, passage);
    const double here = omega * fillSeconds(pipe.volumeM3, inPa, outPa, gasK, passage);
    return std::sqrt((1.0 + reference * reference) / (1.0 + here * here));
}

void TurboPipeState::adopt(const TurboPipeSpec& pipe, double gas, double gasK) {
    portK = gasK;
    outK = gasK;
    const double flow = gas > kMinFlow ? gas : kMinFlow;
    wallK = pipe.extraM > 0.0 ? kAmbientK + (gasK - kAmbientK) * steadyWallShare(pipe, flow, exchangedShare(pipe, flow)) : kAmbientK;
}

double TurboPipeState::stepHeat(const TurboPipeSpec& pipe, double dt, double gas, double targetK, double pressurePa, double tauUpS, double tauDownS) {
    const double flow = gas > kMinFlow ? gas : kMinFlow;
    if (pipe.extraM <= 0.0) {
        const double wanted = targetK > kAmbientK ? turboPipeSteadyK(pipe, flow, targetK) : targetK;
        outK += (wanted - outK) * dt / ((wanted > outK ? tauUpS : tauDownS) * pipe.headShare);
        return outK;
    }
    // Le collecteur et la volute, comme à la référence. Puis le tube ajouté : le gaz y cède sa chaleur à une paroi qui a sa masse.
    portK += (targetK - portK) * dt / (targetK > portK ? tauUpS : tauDownS);
    const double exchanged = exchangedShare(pipe, flow);
    const double leaving = wallK + (portK - wallK) * (1.0 - exchanged);
    const double fromGas = flow * kGasCp * (portK - leaving);
    const double toOutside = kOutsideWPerM2K * pipe.surfaceM2 * (wallK - kAmbientK);
    wallK += (fromGas - toOutside) * dt / pipe.wallJPerK;
    // Le temps de parcours : la masse de gaz que contient le tube, renouvelée par le débit.
    const double residence = pressurePa * pipe.extraVolumeM3 / (kGasR * outK) / flow;
    outK += (leaving - outK) * (1.0 - portable::exp(-dt / residence));
    return outK;
}

double TurboPipeState::stepPressure(const TurboPipeSpec& pipe, double dt, double pressurePa, double wantedPa, double outPa, double gasK, double passage) const {
    if (pipe.extraVolumeM3 <= 0.0) return wantedPa;
    const double tau = fillSeconds(pipe.extraVolumeM3, pressurePa > wantedPa ? pressurePa : wantedPa, outPa, gasK, passage);
    if (tau <= 0.0) return wantedPa;
    return pressurePa + (wantedPa - pressurePa) * (1.0 - portable::exp(-dt / tau));
}

double turboPipeOutflow(double pressurePa, double outPa, double gasK, double passage) {
    const double over = pressurePa * pressurePa - outPa * outPa;
    return over > 0.0 ? passage * std::sqrt(over / gasK) : 0.0;
}

}  // namespace revforge
