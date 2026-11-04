#pragma once

#include <cmath>

// Sinus, cosinus et puissance au résultat identique partout. Ceux de la bibliothèque C ne sont pas arrondis
// correctement et diffèrent d'un dernier bit entre plateformes : dans DrivingModel, cet écart s'amplifie d'une
// étape à l'autre, jusqu'à décaler un passage de rapport. Ici, tout passe par des opérations exactes en IEEE 754
// (+, −, ×, ÷, fmod, frexp, ldexp) : précision de l'ordre de 1e-13, largement suffisante pour de la mécanique.
namespace revforge::portable {

inline constexpr double kPi = 3.141592653589793;
inline constexpr double kLn2 = 0.6931471805599453;
inline constexpr double kLn10 = 2.302585092994046;

inline double sin(double x) {
    double r = std::fmod(x, 2.0 * kPi);
    if (r > kPi) r -= 2.0 * kPi;
    else if (r < -kPi) r += 2.0 * kPi;
    if (r > kPi / 2.0) r = kPi - r;
    else if (r < -kPi / 2.0) r = -kPi - r;
    const double s = r * r;
    return r * (1.0 + s * (-1.0 / 6.0 + s * (1.0 / 120.0 + s * (-1.0 / 5040.0 + s * (1.0 / 362880.0 + s * (-1.0 / 39916800.0 + s * (1.0 / 6227020800.0 + s * (-1.0 / 1307674368000.0))))))));
}

inline double cos(double x) { return sin(x + kPi / 2.0); }

inline double log(double x) {
    int exponent = 0;
    double m = std::frexp(x, &exponent);
    if (m < 0.7071067811865476) {
        m *= 2.0;
        --exponent;
    }
    const double z = (m - 1.0) / (m + 1.0);
    const double s = z * z;
    const double series = 2.0 * z * (1.0 + s * (1.0 / 3.0 + s * (1.0 / 5.0 + s * (1.0 / 7.0 + s * (1.0 / 9.0 + s * (1.0 / 11.0 + s * (1.0 / 13.0 + s * (1.0 / 15.0))))))));
    return series + exponent * kLn2;
}

inline double exp(double x) {
    const double k = std::floor(x / kLn2 + 0.5);
    const double r = x - k * kLn2;
    const double series = 1.0 + r * (1.0 + r / 2.0 * (1.0 + r / 3.0 * (1.0 + r / 4.0 * (1.0 + r / 5.0 * (1.0 + r / 6.0 * (1.0 + r / 7.0 * (1.0 + r / 8.0 * (1.0 + r / 9.0 * (1.0 + r / 10.0 * (1.0 + r / 11.0))))))))));
    return std::ldexp(series, static_cast<int>(k));
}

inline double pow(double x, double y) { return exp(y * log(x)); }

/** Arc tangente : réduite sous 1, puis sous tan(π/12) par atan(a) = π/6 + atan((a√3 − 1)/(a + √3)), puis la série. */
inline double atan(double x) {
    constexpr double kSqrt3 = 1.7320508075688772;
    constexpr double kTanPi12 = 0.2679491924311227;
    const bool negative = x < 0.0;
    double a = negative ? -x : x;
    const bool inverted = a > 1.0;
    if (inverted) a = 1.0 / a;
    const bool shifted = a > kTanPi12;
    if (shifted) a = (a * kSqrt3 - 1.0) / (a + kSqrt3);
    const double s = a * a;
    double r = a * (1.0 + s * (-1.0 / 3.0 + s * (1.0 / 5.0 + s * (-1.0 / 7.0 + s * (1.0 / 9.0 + s * (-1.0 / 11.0 + s * (1.0 / 13.0 + s * (-1.0 / 15.0 + s * (1.0 / 17.0 + s * (-1.0 / 19.0 + s * (1.0 / 21.0 + s * (-1.0 / 23.0 + s * (1.0 / 25.0)))))))))))));
    if (shifted) r += kPi / 6.0;
    if (inverted) r = kPi / 2.0 - r;
    return negative ? -r : r;
}

}  // namespace revforge::portable
