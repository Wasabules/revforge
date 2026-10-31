#pragma once

#include <cmath>

// Mathématiques à résultat identique sur toutes les plateformes visées : calcul en
// double puis arrondi en float. Le rendu reste ainsi comparable, échantillon par
// échantillon, à la référence de calibration.
namespace revforge::km {

inline constexpr double kPi = 3.141592653589793;

inline float exp(float x) { return static_cast<float>(std::exp(static_cast<double>(x))); }
inline float sqrt(float x) { return static_cast<float>(std::sqrt(static_cast<double>(x))); }
inline float tanh(float x) { return static_cast<float>(std::tanh(static_cast<double>(x))); }
inline float pow(float x, float y) { return static_cast<float>(std::pow(static_cast<double>(x), static_cast<double>(y))); }
inline float floor(float x) { return static_cast<float>(std::floor(static_cast<double>(x))); }
inline float abs(float x) { return std::fabs(x); }
inline float max(float a, float b) { return a >= b ? a : b; }
inline float min(float a, float b) { return a <= b ? a : b; }
inline float sign(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : x); }
inline float coerceIn(float v, float low, float high) { return v < low ? low : (v > high ? high : v); }
inline float coerceAtLeast(float v, float low) { return v < low ? low : v; }

}  // namespace revforge::km
