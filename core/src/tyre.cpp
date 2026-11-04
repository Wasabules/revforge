#include "revforge/tyre.hpp"

#include "portable_math.hpp"

#include <cmath>

namespace revforge {

// Tout passe par `portable` : la force du pneu revient à chaque pas de `DrivingModel`, et un dernier bit d'écart entre
// plateformes y finirait par décaler un passage de rapport.

namespace {

double curved(double x) { return x - kTyreCurvature * (x - portable::atan(x)); }

}  // namespace

double tyreStiffnessFactor() { return kTyreSlipStiffness / (kTyreShape * kTyreGrip); }

double tyreGripShare(double slipRatio) { return portable::sin(kTyreShape * portable::atan(curved(tyreStiffnessFactor() * slipRatio))); }

double tyreGripSlope(double slipRatio) {
    const double b = tyreStiffnessFactor();
    const double x = b * slipRatio;
    const double phi = curved(x);
    const double dPhi = b * (1.0 - kTyreCurvature + kTyreCurvature / (1.0 + x * x));
    return portable::cos(kTyreShape * portable::atan(phi)) * kTyreShape / (1.0 + phi * phi) * dPhi;
}

double tyrePeakSlip() {
    // sin(C · atan(φ)) vaut 1 quand φ = tan(π / 2C) ; φ croît avec Bκ tant que E < 1 : une dichotomie sur Bκ.
    const double angle = portable::kPi / (2.0 * kTyreShape);
    const double wanted = portable::sin(angle) / portable::cos(angle);
    double low = 0.0, high = wanted * 4.0 + 4.0;
    for (int i = 0; i < 80; ++i) {
        const double middle = 0.5 * (low + high);
        (curved(middle) < wanted ? low : high) = middle;
    }
    return 0.5 * (low + high) / tyreStiffnessFactor();
}

double tyreSlipForShare(double share) {
    if (share <= 0.0) return 0.0;
    double low = 0.0, high = tyrePeakSlip();
    if (share >= 1.0) return high;
    // La courbe monte jusqu'au sommet : une dichotomie, au résultat identique partout.
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (tyreGripShare(middle) < share ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

double tyreSlipRatio(double wheelMs, double carMs) {
    const double speed = std::fabs(carMs);
    return (wheelMs - carMs) / (speed > kTyreLowSpeedMs ? speed : kTyreLowSpeedMs);
}

TyreContact tyreContact(double peakN, double wheelMs, double carMs) {
    TyreContact c;
    const double speed = std::fabs(carMs);
    const double reference = speed > kTyreLowSpeedMs ? speed : kTyreLowSpeedMs;
    c.slipRatio = (wheelMs - carMs) / reference;
    c.forceN = peakN * tyreGripShare(c.slipRatio);
    const double slope = tyreGripSlope(c.slipRatio);
    c.perWheelMs = slope > 0.0 ? peakN * slope / reference : 0.0;
    return c;
}

}  // namespace revforge
