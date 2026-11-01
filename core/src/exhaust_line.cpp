#include "exhaust_line.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kSoundSpeed = 500.0;  // m/s, gaz chaud
constexpr double kAirDensity = 0.6;    // kg/m³, gaz chaud
// Le débit et la température font bouger les résonances d'un instant à l'autre. Vers les aigus, les allers-retours
// se brouillent donc en moyenne, et il reste le trajet direct. Sans cela, la ligne forme un peigne de bosses
// régulières et fixes, qui sonne comme une canette, et que de vrais moteurs n'ont pas.
constexpr double kBlurFromHz = 700.0;
constexpr double kBlurPerKhz2 = 1.0;  // néper/m, au carré des kHz au-dessus de kBlurFromHz
constexpr double kBlurMax = 8.0;      // néper/m : les allers-retours sont déjà éteints, et l'exponentielle reste calculable

/**
 * Un tronçon : longueur (m), section par rapport au tube, pertes aux parois et absorption. Ou, si `branchLitres` n'est
 * pas nul, un résonateur de Helmholtz greffé sur le côté, accordé sur le bourdonnement (voir `compute`).
 */
struct Section {
    double length;
    double area;
    double wallLoss;    // néper/m/√Hz
    double absorption;  // néper/m/Hz : nid d'abeille d'un catalyseur, chambre garnie
    bool stretches;     // les tubes suivent la longueur de la voiture, pas les volumes
    double fibre = 0.0;  // néper/m à 1 kHz, en f^0,45 : tube perforé dans la laine, qui laisse passer les graves
    double branchLitres = 0.0;
};

constexpr double kWall = 0.008;
// Laine de 100 kg/m³ dans le gaz chaud, autour d'un tube perforé : environ 52 dB/m à 1 kHz (Lee, 2005, et dispersion calculée).
constexpr double kWool = 6.0;
// Le nid d'abeille d'un catalyseur retient surtout les aigus : quelques décibels au total.
constexpr double kCat = 0.001;
constexpr double kBranchQ = 8.0;
// Impédance de source, en ρc/S : un multicylindre tend vers 1, à un facteur 2 près. Elle amortit les résonances du tube.
constexpr double kSourceImpedance = 2.0;

// Origine : un catalyseur, un résonateur accordé, et un silencieux à deux chambres (une fois replié, c'est un 3 passes).
constexpr std::array<Section, 10> kStock{{
    {0.40, 1.0, kWall, 0.0, false},
    {0.26, 3.5, kWall, kCat, false},       // catalyseur
    {0.90, 1.0, kWall, 0.0, true},
    {0.0, 1.0, 0.0, 0.0, false, 0.0, 4.0},  // résonateur, et sa chambre
    {0.30, 2.5, kWall, 0.0004, false},
    {0.70, 1.0, kWall, 0.0, true},
    {0.20, 5.5, kWall, 0.0006, false},
    {0.12, 0.9, kWall, 0.0, false},
    {0.24, 5.5, kWall, 0.0012, false},
    {0.45, 1.0, kWall, 0.0, false},        // sortie
}};
// Sport : un catalyseur plus libre, un tube plus gros, et un silencieux droit garni : il retient les aigus, pas les graves.
constexpr std::array<Section, 5> kSport{{
    {0.40, 1.0, kWall, 0.0, false},
    {0.20, 3.0, kWall, kCat * 0.7, false},
    {1.25, 1.2, kWall, 0.0, true},
    {0.36, 4.0, kWall, 0.0006, false, kWool * 0.32},
    {0.35, 1.2, kWall, 0.0, false},
}};
constexpr std::array<Section, 2> kStraight{{
    {0.40, 1.0, kWall, 0.0, false},
    {1.60, 1.3, kWall, 0.0, true},
}};
// Silencieux arrière retiré, un tube à la place : le catalyseur et le résonateur restent.
constexpr std::array<Section, 6> kNoMuffler{{
    {0.40, 1.0, kWall, 0.0, false},
    {0.26, 3.5, kWall, kCat, false},
    {0.90, 1.0, kWall, 0.0, true},
    {0.0, 1.0, 0.0, 0.0, false, 0.0, 4.0},
    {0.30, 2.5, kWall, 0.0004, false},
    {1.30, 1.0, kWall, 0.0, true},
}};
// Résonateur retiré : tout le reste de la ligne d'origine. Il change peu le niveau, mais le bourdonnement revient.
constexpr std::array<Section, 7> kNoResonator{{
    {0.40, 1.0, kWall, 0.0, false},
    {0.26, 3.5, kWall, kCat, false},
    {1.90, 1.0, kWall, 0.0, true},
    {0.20, 5.5, kWall, 0.0006, false},
    {0.12, 0.9, kWall, 0.0, false},
    {0.24, 5.5, kWall, 0.0012, false},
    {0.45, 1.0, kWall, 0.0, false},
}};
// Berline très insonorisée : un résonateur plus gros, un silencieux central, et un arrière plus volumineux.
constexpr std::array<Section, 11> kQuiet{{
    {0.40, 1.0, kWall, 0.0, false},
    {0.28, 3.5, kWall, kCat, false},
    {0.60, 1.0, kWall, 0.0, true},
    {0.0, 1.0, 0.0, 0.0, false, 0.0, 6.0},
    {0.30, 3.0, kWall, 0.0006, false},
    {0.60, 1.0, kWall, 0.0, true},
    {0.22, 9.0, kWall, 0.0008, false},
    {0.12, 0.9, kWall, 0.0, false},
    {0.26, 9.0, kWall, 0.0012, false},
    {0.12, 1.0, kWall, 0.0, false},
    {0.45, 1.0, kWall, 0.0, false},
}};

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

/** Matrice de transfert d'une ligne : (pression, débit) d'entrée en fonction de ceux de sortie. */
struct Matrix {
    Complex a, b, c, d;
};

inline Matrix operator*(const Matrix& x, const Matrix& y) {
    return {x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d, x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d};
}

/** Tube à pertes : κ = ω/c - jα, donc cos(κL) et sin(κL) d'un argument complexe. */
inline Matrix tube(double omega, double alpha, double length, double impedance) {
    const double phase = omega / kSoundSpeed * length;
    const double decay = alpha * length;
    const double cosPhase = std::cos(phase), sinPhase = std::sin(phase);
    const double up = std::exp(decay), down = 1.0 / up;
    const double ch = 0.5 * (up + down), sh = 0.5 * (up - down);
    // cos(a - jb) = cos a · ch b + j sin a · sh b ;  sin(a - jb) = sin a · ch b - j cos a · sh b.
    const Complex cosine{cosPhase * ch, sinPhase * sh};
    const Complex sine{sinPhase * ch, -cosPhase * sh};
    const Complex jSine{-sine.im, sine.re};
    return {cosine, scaled(jSine, impedance), scaled(jSine, 1.0 / impedance), cosine};
}

/** Levine et Schwinger, par l'ajustement de Silva, Guillemain, Kergomard, Mallaroni et Norris (2009) : |R| et l/a. */
void outletReflection(double ka, double& magnitude, double& lengthOverRadius) {
    const double x2 = ka * ka;
    const double x4 = x2 * x2;
    const double x6 = x4 * x2;
    magnitude = (1.0 + 0.800 * x2) / (1.0 + 1.300 * x2 + 0.266 * x4 + 0.0263 * x6);
    lengthOverRadius = 0.6133 * (1.0 + 0.0599 * x2) / (1.0 + 0.238 * x2 - 0.0153 * x4 + 0.00150 * x6);
}

/** Impédance de la bouche, normalisée par ρc/S : R = -|R|·exp(-2j·k·l), et Z = (1 + R) / (1 - R). */
Complex outletImpedance(double ka) {
    double magnitude = 0.0, length = 0.0;
    outletReflection(ka, magnitude, length);
    const double angle = 2.0 * ka * length;
    const Complex r{-magnitude * std::cos(angle), magnitude * std::sin(angle)};
    return Complex{1.0 + r.re, r.im} * inverse(Complex{1.0 - r.re, -r.im});
}

}  // namespace

double ExhaustLine::outletReflectionMagnitude(double ka) {
    double magnitude = 0.0, length = 0.0;
    outletReflection(ka, magnitude, length);
    return magnitude;
}

double ExhaustLine::outletEndCorrection(double ka) {
    double magnitude = 0.0, length = 0.0;
    outletReflection(ka, magnitude, length);
    return length;
}

double ExhaustLine::chamberLossDb(double hz, double length, double areaRatio) {
    const double omega = 2.0 * kPi * hz;
    const double pipeArea = 1e-3;
    const double z0 = kAirDensity * kSoundSpeed / pipeArea;
    const Matrix chamber = tube(omega, 0.0, length, z0 / areaRatio);
    // Sortie anéchoïque : A + B/Z0 + C·Z0 + D, définition de la perte par transmission à sections égales.
    const Complex sum = chamber.a + scaled(chamber.b, 1.0 / z0) + scaled(chamber.c, z0) + chamber.d;
    return 20.0 * std::log10(0.5 * std::sqrt(sum.re * sum.re + sum.im * sum.im));
}

ExhaustLine::ExhaustLine(float sampleRate) : sr_(sampleRate) {
    fft_.init(kFftSize);
    re_.assign(kFftSize, 0.0f);
    im_.assign(kFftSize, 0.0f);
    impulse_.assign(kImpulseLength, 0.0f);
    outlet_.assign(kFftSize / 2 + 1, 0.0f);
}

double ExhaustLine::lossDb(double fromHz, double toHz) const {
    double sum = 0.0;
    int count = 0;
    for (int k = 1; k <= kFftSize / 2; ++k) {
        const double hz = static_cast<double>(k) * static_cast<double>(sr_) / kFftSize;
        if (hz < fromHz || hz >= toHz) continue;
        const double value = static_cast<double>(outlet_[static_cast<std::size_t>(k)]);
        sum += value * value;
        ++count;
    }
    return count == 0 ? 0.0 : -10.0 * std::log10(std::max(sum / count, 1e-20));
}

const std::vector<float>& ExhaustLine::compute(Exhaust exhaust, float scale, double displacementM3, int cylinders) {
    const Section* sections = kStock.data();
    std::size_t count = kStock.size();
    if (exhaust == Exhaust::Sport) {
        sections = kSport.data();
        count = kSport.size();
    } else if (exhaust == Exhaust::Straight) {
        sections = kStraight.data();
        count = kStraight.size();
    } else if (exhaust == Exhaust::MufflerDelete) {
        sections = kNoMuffler.data();
        count = kNoMuffler.size();
    } else if (exhaust == Exhaust::ResonatorDelete) {
        sections = kNoResonator.data();
        count = kNoResonator.size();
    } else if (exhaust == Exhaust::Quiet) {
        sections = kQuiet.data();
        count = kQuiet.size();
    }
    // Plus de cylindrée, plus de débit, donc un plus gros tube : 50 mm pour 2 litres.
    const double diameter = std::clamp(0.05 * std::sqrt(displacementM3 / 0.002), 0.032, 0.09);
    const double baseArea = kPi * diameter * diameter / 4.0;
    // Un résonateur vise le bourdonnement de la croisière : la cadence d'allumage à 2500 tr/min.
    const double branchHz = std::clamp(static_cast<double>(cylinders) / 2.0 * 2500.0 / 60.0, 60.0, 260.0);
    const double branchOmega = 2.0 * kPi * branchHz;

    const int half = kFftSize / 2;
    for (int k = 0; k <= half; ++k) {
        const double hz = static_cast<double>(k) * static_cast<double>(sr_) / kFftSize;
        const double omega = 2.0 * kPi * hz;
        const double root = std::sqrt(hz);
        const double over = std::max(hz - kBlurFromHz, 0.0) / 1000.0;
        const double blur = std::min(kBlurPerKhz2 * over * over, kBlurMax);
        double blurred = 0.0;
        Matrix total{{1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}};
        for (std::size_t i = 0; i < count; ++i) {
            const Section& s = sections[i];
            if (s.branchLitres > 0.0) {
                // En parallèle sur le tube : compliance du volume, masse du col accordée sur la bonne fréquence, et des pertes.
                const double compliance = s.branchLitres * 1e-3 / (kAirDensity * kSoundSpeed * kSoundSpeed);
                const double reactance = omega > 0.0 ? (omega / branchOmega - branchOmega / omega) / (branchOmega * compliance) : 0.0;
                const double resistance = 1.0 / (branchOmega * compliance * kBranchQ);
                const Complex admittance = omega > 0.0 ? inverse(Complex{resistance, reactance}) : Complex{0.0, 0.0};
                total = total * Matrix{{1.0, 0.0}, {0.0, 0.0}, admittance, {1.0, 0.0}};
                continue;
            }
            const double length = s.length * (s.stretches ? static_cast<double>(scale) : 1.0);
            const double area = baseArea * s.area;
            double alpha = s.wallLoss * root + s.absorption * hz + blur;
            if (s.fibre > 0.0 && hz > 0.0) alpha += s.fibre * std::pow(hz / 1000.0, 0.45);
            blurred += blur * length;
            total = total * tube(omega, alpha, length, kAirDensity * kSoundSpeed / area);
        }
        // Sortie à l'air libre : tube non bafflé, Levine et Schwinger par l'ajustement de Silva et al. (2009).
        const double outletArea = baseArea * sections[count - 1].area;
        const double radius = std::sqrt(outletArea / kPi);
        const double ka = omega / kSoundSpeed * radius;
        const double z0 = kAirDensity * kSoundSpeed / outletArea;
        const Complex normalized = outletImpedance(ka);
        const Complex radiation = scaled(normalized, z0);
        // Débit de sortie pour un débit d'entrée, puis rayonnement : la pression suit sa dérivée.
        Complex denominator = total.c * radiation + total.d;
        double sourceRatio = kSourceImpedance;
        // Le moteur n'impose pas son débit : vu de la ligne, c'est une impédance de l'ordre de ρc/S (Prasad et Crocker, 1983).
        if (sourceRatio > 0.0) denominator = denominator + scaled(total.a * radiation + total.b, baseArea / (sourceRatio * kAirDensity * kSoundSpeed));
        const Complex transfer = inverse(denominator);
        // Le trajet direct retrouve ce que le brouillage lui a pris : seuls les allers-retours y perdent.
        const double restored = std::exp(blurred);
        outlet_[static_cast<std::size_t>(k)] = static_cast<float>(restored * std::sqrt(transfer.re * transfer.re + transfer.im * transfer.im));
        // La pression à 1 m suit la dérivée du débit, celle d'un monopôle, tant que ka reste petit. Au-delà, la bouche
        // ne rayonne plus en (ka)² : toute l'énergie passe, et pas plus. La directivité est ailleurs (acoustics).
        const double low = 0.25 * ka * ka;
        const double saturation = k > 0 ? std::sqrt(normalized.re / std::max(low, 1e-12)) : 1.0;
        const Complex radiated{-transfer.im * omega * restored * saturation, transfer.re * omega * restored * saturation};
        re_[static_cast<std::size_t>(k)] = static_cast<float>(radiated.re);
        im_[static_cast<std::size_t>(k)] = static_cast<float>(radiated.im);
        if (k > 0 && k < half) {
            re_[static_cast<std::size_t>(kFftSize - k)] = static_cast<float>(radiated.re);
            im_[static_cast<std::size_t>(kFftSize - k)] = static_cast<float>(-radiated.im);
        }
    }
    im_[0] = 0.0f;
    im_[static_cast<std::size_t>(half)] = 0.0f;
    fft_.transform(re_.data(), im_.data(), true);

    // Le trajet jusqu'à la sortie n'apporte qu'un retard : on commence au premier front.
    float top = 0.0f;
    for (int i = 0; i < kFftSize; ++i) top = std::max(top, std::fabs(re_[static_cast<std::size_t>(i)]));
    int start = 0;
    while (start < kFftSize - kImpulseLength && std::fabs(re_[static_cast<std::size_t>(start)]) < top * 0.002f) ++start;
    start = std::max(start - 8, 0);

    double energy = 0.0;
    const float unscale = 1.0f / static_cast<float>(kFftSize);
    for (int i = 0; i < kImpulseLength; ++i) {
        // Une fin en fondu : la réponse s'éteint sans couper net.
        const float rest = static_cast<float>(kImpulseLength - i) / (static_cast<float>(kImpulseLength) * 0.3f);
        const float fade = rest < 1.0f ? rest * rest * (3.0f - 2.0f * rest) : 1.0f;
        const float value = re_[static_cast<std::size_t>(start + i)] * fade * unscale;
        impulse_[static_cast<std::size_t>(i)] = value;
        energy += static_cast<double>(value) * value;
    }
    gain_ = static_cast<float>(std::sqrt(energy));
    return impulse_;
}

}  // namespace revforge
