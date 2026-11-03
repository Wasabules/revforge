#include "intake_path.hpp"

#include <algorithm>
#include <cmath>

#include "exhaust_line.hpp"
#include "revforge/air_filter.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
// L'air d'admission est froid, ce n'est pas la ligne d'échappement : celui du jour, dans `IntakeGeometry`.
constexpr double kReferenceM3 = 0.002;

// Pour 2 L. Le conduit suit le débit, donc la cylindrée ; la boîte reste une boîte : ses longueurs en V^(1/3).
constexpr double kDuctDiameterM = 0.065;
constexpr double kDuctLengthM = 0.40;
constexpr double kBoxLitres = 8.0;  // quatre cylindrées : ordre de grandeur courant, supposé
constexpr double kBoxLengthM = 0.28;
constexpr double kSnorkelLengthM = 0.35;
constexpr double kSnorkelArea = 0.8;
// Le cône : sur un conduit raccourci, trois fois sa section, 16 cm de long, à travers une mousse ou un coton huilé.
constexpr double kConeDuctM = 0.20;
constexpr double kConeLengthM = 0.16;
constexpr double kConeArea = 3.0;
// Le débit de dessin, en volume : ΔP = k·Q², donc une résistance acoustique dΔP/dQ = 2·ΔP/Q.
constexpr double kDesignFlowM3S = 0.092;

constexpr double kWall = 0.004;     // néper/m/√Hz : parois lisses, air froid
constexpr double kPaper = 0.00025;  // néper/m/Hz : dans la boîte, l'élément plissé absorbe un peu les aigus
constexpr double kFoam = 0.0016;    // néper/m/Hz : le cône garde presque tout en bas, et perd ses aigus dans sa mousse
// Comme dans la ligne d'échappement : le débit brouille les allers-retours vers les aigus. Sans quoi, un peigne qui sonne creux.
constexpr double kBlurFromHz = 700.0;
constexpr double kBlurPerKhz2 = 1.0;
constexpr double kBlurMax = 8.0;
// Impédance de source, en ρc/S : plusieurs cylindres mis en commun tendent vers 1 (Prasad et Crocker, 1983) ; une soupape
// seule devant son tube est bien plus fermée.
constexpr double kManifoldSource = 2.0;
constexpr double kValveSource = 6.0;

// Une boîte en plastique de 2 à 3 mm : loi de masse, TL = 20·log10(m·f) − 47 dB. C'est par ses parois que l'aigu sort.
constexpr double kBoxWallKgM2 = 2.5;
constexpr double kMassLawOffsetDb = 47.0;

// Ce qui s'entend d'une admission s'éteint vite au-dessus de quelques kilohertz : la soupape et le conduit ne font plus d'onde
// plane, et le souffle reste en bas (`kNoiseTopHz`, engine_sound.cpp). Le papier d'origine le cachait ; sans boîte, il faut le dire.
constexpr double kOpenTopHz = 3000.0;

constexpr double kGainFloorDb = -14.0;
constexpr double kGainCeilingDb = 30.0;

struct Complex {
    double re, im;
};

inline Complex operator*(Complex a, Complex b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
inline Complex operator+(Complex a, Complex b) { return {a.re + b.re, a.im + b.im}; }
inline Complex scaled(Complex a, double k) { return {a.re * k, a.im * k}; }
inline Complex inverse(Complex a) {
    const double d = a.re * a.re + a.im * a.im;
    return {a.re / d, -a.im / d};
}

struct Matrix {
    Complex a, b, c, d;
};

inline Matrix operator*(const Matrix& x, const Matrix& y) {
    return {x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d, x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d};
}

Matrix tube(const IntakeGeometry& g, double omega, double alpha, double length, double area) {
    const double impedance = g.densityKgM3 * g.soundSpeedMs / area;
    const double phase = omega / g.soundSpeedMs * length;
    const double decay = alpha * length;
    const double cosPhase = std::cos(phase), sinPhase = std::sin(phase);
    const double up = std::exp(decay), down = 1.0 / up;
    const double ch = 0.5 * (up + down), sh = 0.5 * (up - down);
    const Complex cosine{cosPhase * ch, sinPhase * sh};
    const Complex sine{sinPhase * ch, -cosPhase * sh};
    const Complex jSine{-sine.im, sine.re};
    return {cosine, scaled(jSine, impedance), scaled(jSine, 1.0 / impedance), cosine};
}

/** L'élément filtrant : une résistance en série sur le débit. */
Matrix element(double resistance) { return {{1.0, 0.0}, {resistance, 0.0}, {0.0, 0.0}, {1.0, 0.0}}; }

/** Du débit à l'entrée d'une chaîne au débit rayonné à sa bouche, plafonné quand elle cesse de rayonner en (ka)². */
double radiatedMagnitude(const IntakeGeometry& g, const Matrix& total, double omega, double mouthArea, double sourceArea, double sourceRatio, double restored) {
    const double radius = std::sqrt(mouthArea / kPi);
    const double ka = omega / g.soundSpeedMs * radius;
    const double reflection = ExhaustLine::outletReflectionMagnitude(ka);
    const double lengthShare = ExhaustLine::outletEndCorrection(ka);
    const double angle = 2.0 * ka * lengthShare;
    const Complex r{-reflection * std::cos(angle), reflection * std::sin(angle)};
    const Complex normalized = Complex{1.0 + r.re, r.im} * inverse(Complex{1.0 - r.re, -r.im});
    const Complex radiation = scaled(normalized, g.densityKgM3 * g.soundSpeedMs / mouthArea);
    Complex denominator = total.c * radiation + total.d;
    denominator = denominator + scaled(total.a * radiation + total.b, sourceArea / (sourceRatio * g.densityKgM3 * g.soundSpeedMs));
    const Complex transfer = inverse(denominator);
    const double low = 0.25 * ka * ka;
    const double saturation = std::sqrt(std::max(normalized.re, 0.0) / std::max(low, 1e-12));
    return restored * saturation * std::sqrt(transfer.re * transfer.re + transfer.im * transfer.im);
}

double blurAt(double hz) {
    const double over = std::max(hz - kBlurFromHz, 0.0) / 1000.0;
    return std::min(kBlurPerKhz2 * over * over, kBlurMax);
}

/** Module du trajet, pour ce filtre. */
double pathMagnitude(AirFilter filter, const IntakeGeometry& g, double hzAsked) {
    const double hz = std::max(hzAsked, 15.0);
    const double size = g.displacementM3 / kReferenceM3;
    const double omega = 2.0 * kPi * hz;
    const double blur = blurAt(hz);
    const double wall = kWall * std::sqrt(hz) + blur;
    const double diameter = std::clamp(kDuctDiameterM * std::sqrt(size), 0.04, 0.12);
    const double duct = kPi * diameter * diameter / 4.0;
    const AirFilterSpec& spec = airFilterSpec(filter);
    const double scale = std::cbrt(size);

    if (filter == AirFilter::Stacks) {
        const double area = kPi * g.stackRadiusM * g.stackRadiusM;
        return radiatedMagnitude(g, tube(g, omega, wall, g.stackM, area), omega, area, area, kValveSource, std::exp(blur * g.stackM));
    }

    const double resistance = 2.0 * spec.elementDropPa / (kDesignFlowM3S * size);
    if (filter == AirFilter::Cone) {
        const double length = kConeLengthM * scale;
        const Matrix total = tube(g, omega, wall, kConeDuctM, duct) * element(resistance) * tube(g, omega, wall + kFoam * hz, length, duct * kConeArea);
        return radiatedMagnitude(g, total, omega, duct * kConeArea, duct, kManifoldSource, std::exp(blur * (kConeDuctM + length)));
    }

    // La boîte, et les parois par où elle fuit.
    const double boxLength = kBoxLengthM * scale;
    const double boxArea = kBoxLitres * 1e-3 * size / boxLength;
    const double snorkel = duct * kSnorkelArea;
    const double half = 0.5 * boxLength;
    const double boxAlpha = wall + kPaper * hz;
    const Matrix line = tube(g, omega, wall, kDuctLengthM, duct) * tube(g, omega, boxAlpha, half, boxArea) * element(resistance) * tube(g, omega, boxAlpha, half, boxArea) * tube(g, omega, wall, kSnorkelLengthM, snorkel);
    const double through = radiatedMagnitude(g, line, omega, snorkel, duct, kManifoldSource, std::exp(blur * (kDuctLengthM + boxLength + kSnorkelLengthM)));
    const double bareLength = kDuctLengthM + kSnorkelLengthM;
    const double open = radiatedMagnitude(g, tube(g, omega, wall, bareLength, duct), omega, duct, duct, kManifoldSource, std::exp(blur * bareLength));
    const double lossDb = std::max(20.0 * std::log10(kBoxWallKgM2 * hz) - kMassLawOffsetDb, 3.0);
    const double leak = open * std::pow(10.0, -lossDb / 20.0);
    return std::sqrt(through * through + leak * leak);
}

}  // namespace

double IntakePath::pathDb(AirFilter filter, const IntakeGeometry& geometry, double hz) {
    return 20.0 * std::log10(std::max(pathMagnitude(filter, geometry, hz), 1e-9));
}

double IntakePath::gainDb(AirFilter filter, const IntakeGeometry& geometry, double hz) {
    if (filter == AirFilter::Stock) return 0.0;
    double gain = pathDb(filter, geometry, hz) - pathDb(AirFilter::Stock, geometry, hz);
    if (airFilterSpec(filter).underBonnet) gain -= 20.0 * std::log10(1.0 + (hz / kOpenTopHz) * (hz / kOpenTopHz));
    return std::clamp(gain, kGainFloorDb, kGainCeilingDb);
}

IntakePath::IntakePath(float sampleRate) : sr_(sampleRate) {
    fft_.init(kFftSize);
    re_.assign(kFftSize, 0.0f);
    im_.assign(kFftSize, 0.0f);
    impulse_.assign(kLength, 0.0f);
}

const std::vector<float>& IntakePath::compute(AirFilter filter, const IntakeGeometry& geometry) {
    const int half = kFftSize / 2;
    const double top = 0.45 * static_cast<double>(sr_);
    // Le module voulu, en log : la phase minimale s'en déduit par le cepstre réel, comme pour l'habitacle.
    const double napier = std::log(10.0) / 20.0;
    for (int k = 0; k <= half; ++k) {
        const double hz = std::min(std::max(static_cast<double>(k) * static_cast<double>(sr_) / kFftSize, 15.0), top);
        const float value = static_cast<float>(gainDb(filter, geometry, hz) * napier);
        re_[static_cast<std::size_t>(k)] = value;
        im_[static_cast<std::size_t>(k)] = 0.0f;
        if (k > 0 && k < half) {
            re_[static_cast<std::size_t>(kFftSize - k)] = value;
            im_[static_cast<std::size_t>(kFftSize - k)] = 0.0f;
        }
    }
    fft_.transform(re_.data(), im_.data(), true);
    const float scale = 1.0f / static_cast<float>(kFftSize);
    for (int n = 0; n < kFftSize; ++n) {
        const float folded = n == 0 || n == half ? 1.0f : (n < half ? 2.0f : 0.0f);
        re_[static_cast<std::size_t>(n)] *= scale * folded;
        im_[static_cast<std::size_t>(n)] = 0.0f;
    }
    fft_.transform(re_.data(), im_.data(), false);
    for (int k = 0; k < kFftSize; ++k) {
        const double magnitude = std::exp(static_cast<double>(re_[static_cast<std::size_t>(k)]));
        const double angle = static_cast<double>(im_[static_cast<std::size_t>(k)]);
        re_[static_cast<std::size_t>(k)] = static_cast<float>(magnitude * std::cos(angle));
        im_[static_cast<std::size_t>(k)] = static_cast<float>(magnitude * std::sin(angle));
    }
    fft_.transform(re_.data(), im_.data(), true);
    for (int n = 0; n < kLength; ++n) {
        const float rest = static_cast<float>(kLength - n) / (static_cast<float>(kLength) * 0.25f);
        const float fade = rest < 1.0f ? rest * rest * (3.0f - 2.0f * rest) : 1.0f;
        impulse_[static_cast<std::size_t>(n)] = re_[static_cast<std::size_t>(n)] * scale * fade;
    }
    return impulse_;
}

}  // namespace revforge
