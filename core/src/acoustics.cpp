#include "acoustics.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

#include "kotlin_math.hpp"

namespace revforge::acoustics {

namespace {

// ── Le champ extérieur ──
constexpr float kMaxDelayS = 0.2f;  // de quoi s'éloigner à près de 70 m
constexpr float kMaxDistanceM = 65.0f;
constexpr float kMinDelaySamples = 1.0f;
constexpr float kSmoothS = 0.12f;
// Résistivité d'un enrobé : le sol renvoie presque tout (de l'ordre de 2·10⁷ Pa·s/m²).
constexpr double kAsphaltSigma = 2.0e7;
// La réverbération d'un lieu ouvert : sa pression ne dépend pas de la distance. Distance critique d'environ 18 m.
constexpr float kAmbientGain = 1.0f / 18.0f;

// ── La bouche du tube : un pôle et un zéro par angle, ajustés sur la fonction de gain de Levine et Schwinger (1948) ──
// En 1/ka : le gain monte au-delà du zéro et descend au-delà du pôle. Ajustement à moins de 2 dB jusqu'à ka = 3.
constexpr std::array<float, 9> kOutletAngles{0.0f, 30.0f, 45.0f, 60.0f, 90.0f, 120.0f, 135.0f, 150.0f, 180.0f};
constexpr std::array<float, 9> kOutletInversePole{0.15f, 0.313f, 0.68f, 0.85f, 0.667f, 0.97f, 0.98f, 0.89f, 0.54f};
constexpr std::array<float, 9> kOutletInverseZero{0.99f, 0.926f, 1.09f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

constexpr float kUnderCarHeight = 0.7f;
constexpr float kInsideGlideS = 0.2f;
constexpr float kOutletTopKa = 5.0f;
constexpr float kMirrorCoherentHz = 250.0f;
// Le compartiment moteur, capot fermé : presque tout ce qui est aigu reste dedans.
constexpr float kBayClosedHigh = 0.32f;
constexpr float kBayClosedPoleHz = 500.0f;
constexpr float kBayTopHz = 3000.0f;
constexpr float kBayClosedLevel = 0.7f;

float interpolate(const std::array<float, 9>& xs, const std::array<float, 9>& ys, float x) {
    if (x <= xs[0]) return ys[0];
    for (std::size_t i = 1; i < xs.size(); ++i) {
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[xs.size() - 1];
}

float length3(float x, float y, float z) { return km::sqrt(x * x + y * y + z * z); }

float acosClamped(float c) { return static_cast<float>(std::acos(static_cast<double>(km::coerceIn(c, -1.0f, 1.0f)))); }

/** Coefficient d'un pôle : e^(-2πf/fs). Vaut 0, donc rien, quand `hz` dépasse largement Nyquist. */
float poleOf(float hz, float rate) { return km::exp(-2.0f * static_cast<float>(kPi) * hz / rate); }

struct Point2 {
    float t, h;
};

/** Longueur de la chaîne tendue au-dessus de ces points, triés selon t : le plus court chemin qui les contourne. */
template <std::size_t N>
float tautLength(const std::array<Point2, N>& points, int count) {
    std::array<Point2, N> hull{};
    int size = 0;
    for (int i = 0; i < count; ++i) {
        const Point2 p = points[static_cast<std::size_t>(i)];
        while (size >= 2) {
            const Point2 a = hull[static_cast<std::size_t>(size - 2)];
            const Point2 b = hull[static_cast<std::size_t>(size - 1)];
            if ((b.t - a.t) * (p.h - a.h) - (b.h - a.h) * (p.t - a.t) < 0.0f) break;
            --size;
        }
        hull[static_cast<std::size_t>(size++)] = p;
    }
    float total = 0.0f;
    for (int i = 1; i < size; ++i) {
        const Point2 a = hull[static_cast<std::size_t>(i - 1)];
        const Point2 b = hull[static_cast<std::size_t>(i)];
        total += km::sqrt((b.t - a.t) * (b.t - a.t) + (b.h - a.h) * (b.h - a.h));
    }
    return total;
}

}  // namespace

double airAbsorptionDbPerM(double hz, double celsius, double humidityPercent, double kilopascal) {
    const double T = celsius + 273.15;
    constexpr double T0 = 293.15;
    constexpr double T01 = 273.16;
    const double pa = kilopascal / 101.325;
    const double saturation = std::pow(10.0, -6.8346 * std::pow(T01 / T, 1.261) + 4.6151);
    const double h = humidityPercent * saturation / pa;
    const double oxygen = pa * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double ratio = T / T0;
    const double nitrogen = pa * std::pow(ratio, -0.5) * (9.0 + 280.0 * h * std::exp(-4.170 * (std::pow(ratio, -1.0 / 3.0) - 1.0)));
    const double f2 = hz * hz;
    return 8.686 * f2 * (1.84e-11 / pa * std::sqrt(ratio) +
                         std::pow(ratio, -2.5) * (0.01275 * std::exp(-2239.1 / T) / (oxygen + f2 / oxygen) +
                                                  0.1068 * std::exp(-3352.0 / T) / (nitrogen + f2 / nitrogen)));
}

double aWeightingDb(double hz) {
    const double f2 = hz * hz;
    const double top = 12194.217 * 12194.217 * f2 * f2;
    const double bottom = (f2 + 20.598997 * 20.598997) * std::sqrt((f2 + 107.65265 * 107.65265) * (f2 + 737.86223 * 737.86223)) * (f2 + 12194.217 * 12194.217);
    return 20.0 * std::log10(top / bottom) + 2.0;
}

void groundReflection(double hz, double sigma, double cosIncidence, double& magnitude, double& phase) {
    const double x = hz / (sigma / 1000.0);
    const std::complex<double> z(1.0 + 9.08 * std::pow(x, -0.75), -11.9 * std::pow(x, -0.73));
    const std::complex<double> r = (z * cosIncidence - 1.0) / (z * cosIncidence + 1.0);
    magnitude = std::abs(r);
    phase = std::arg(r);
}

double barrierLossDb(double fresnel) { return fresnel <= 0.0 ? 0.0 : std::min(10.0 * std::log10(1.0 + 20.0 * fresnel), 24.0); }

float pathExcessM(const Point3& s, const Point3& r, const CarAcoustics& car) {
    const float hx = car.lengthM * 0.5f;
    const float hy = car.widthM * 0.5f;
    const float top = car.heightM;
    const float dx = r.x - s.x;
    const float dy = r.y - s.y;
    const float dz = r.z - s.z;
    const float plan = km::sqrt(dx * dx + dy * dy);
    if (plan < 1e-4f) return 0.0f;

    // Où le segment traverse le plan de la voiture.
    float from = 0.0f, to = 1.0f;
    const auto clip = [&from, &to](float start, float delta, float limit) {
        if (km::abs(delta) < 1e-6f) return km::abs(start) <= limit;
        float a = (-limit - start) / delta;
        float b = (limit - start) / delta;
        if (a > b) std::swap(a, b);
        from = km::max(from, a);
        to = km::min(to, b);
        return from < to;
    };
    if (!clip(s.x, dx, hx) || !clip(s.y, dy, hy)) return 0.0f;

    const bool sInside = km::abs(s.x) < hx && km::abs(s.y) < hy;
    const bool rInside = km::abs(r.x) < hx && km::abs(r.y) < hy;
    if (sInside && rInside) return 0.0f;
    const float straight = km::sqrt(plan * plan + dz * dz);

    // Par le toit, dans le plan vertical qui contient les deux points.
    std::array<Point2, 4> over{};
    int count = 0;
    over[static_cast<std::size_t>(count++)] = {0.0f, s.z};
    if (!sInside) over[static_cast<std::size_t>(count++)] = {from * plan, top};
    if (!rInside) over[static_cast<std::size_t>(count++)] = {to * plan, top};
    over[static_cast<std::size_t>(count++)] = {plan, r.z};
    float best = tautLength(over, count) - straight;

    // Par les côtés : on tourne autour du rectangle, d'une tangente à l'autre, dans les deux sens.
    if (!sInside && !rInside) {
        const std::array<Point2, 4> corners{{{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}}};
        // Le coin le plus à droite (ou à gauche) vu depuis un point : tous les autres sont du même côté de son rayon.
        const auto tangent = [&corners](float px, float py, float side) {
            int found = 0;
            for (int i = 0; i < 4; ++i) {
                const float ax = corners[static_cast<std::size_t>(i)].t - px;
                const float ay = corners[static_cast<std::size_t>(i)].h - py;
                bool extreme = true;
                for (int j = 0; j < 4 && extreme; ++j) {
                    const float bx = corners[static_cast<std::size_t>(j)].t - px;
                    const float by = corners[static_cast<std::size_t>(j)].h - py;
                    if ((ax * by - ay * bx) * side < -1e-6f) extreme = false;
                }
                if (extreme) found = i;
            }
            return found;
        };
        const float widths[4] = {2.0f * hx, 2.0f * hy, 2.0f * hx, 2.0f * hy};
        for (const float side : {1.0f, -1.0f}) {
            // Sens direct (la voiture à gauche du trajet) : tangente droite au départ, gauche à l'arrivée. L'inverse sinon.
            const int first = tangent(s.x, s.y, side);
            const int last = tangent(r.x, r.y, -side);
            const auto distance = [&corners](float px, float py, int corner) {
                const float cx = corners[static_cast<std::size_t>(corner)].t - px;
                const float cy = corners[static_cast<std::size_t>(corner)].h - py;
                return km::sqrt(cx * cx + cy * cy);
            };
            float around = distance(s.x, s.y, first) + distance(r.x, r.y, last);
            const int step = side > 0.0f ? 1 : 3;
            for (int corner = first; corner != last; corner = (corner + step) % 4) {
                // Le côté i relie les coins i et i + 1.
                around += widths[side > 0.0f ? corner : (corner + 3) % 4];
            }
            best = km::min(best, km::sqrt(around * around + dz * dz) - straight);
        }
    }
    return km::max(best, 0.0f);
}

float earPathM(float r, float theta, float a) {
    const float distance = km::max(r, a * 1.001f);
    const float tangent = acosClamped(a / distance);
    if (theta <= tangent) {
        return km::sqrt(km::max(distance * distance + a * a - 2.0f * a * distance * static_cast<float>(std::cos(static_cast<double>(theta))), 0.0f));
    }
    return km::sqrt(distance * distance - a * a) + a * (theta - tangent);
}

float headShadowAlpha(float theta) {
    constexpr float kMinAlpha = 0.1f;
    constexpr float kMinAngle = 150.0f / 180.0f * static_cast<float>(kPi);
    return (1.0f + kMinAlpha / 2.0f) + (1.0f - kMinAlpha / 2.0f) * static_cast<float>(std::cos(static_cast<double>(theta / kMinAngle) * kPi));
}

double outletFilterDb(double ka, double theta) {
    const float degrees = static_cast<float>(theta * 180.0 / kPi);
    const double zero = ka * static_cast<double>(interpolate(kOutletAngles, kOutletInverseZero, degrees));
    const double pole = ka * static_cast<double>(interpolate(kOutletAngles, kOutletInversePole, degrees));
    const double top = ka / static_cast<double>(kOutletTopKa);
    return 10.0 * std::log10((1.0 + zero * zero) / ((1.0 + pole * pole) * (1.0 + top * top)));
}

double panelLossDb(double hz, double surfaceMass, double coincidenceHz, double dampingDb, double trimFromHz, double trimCapDb, double leak) {
    const auto massLaw = [surfaceMass](double f) { return 20.0 * std::log10(f * surfaceMass) - 47.2; };
    double loss;
    const double plateauFrom = coincidenceHz * 0.5;
    if (hz < plateauFrom) loss = massLaw(hz);
    else if (hz < coincidenceHz) loss = massLaw(plateauFrom) - 3.0 + dampingDb;
    else loss = massLaw(plateauFrom) - 3.0 + dampingDb + 33.0 * std::log10(hz / coincidenceHz);
    if (hz > trimFromHz && trimCapDb > 0.0) loss += std::min(40.0 * std::log10(hz / trimFromHz), trimCapDb);
    loss = std::max(loss, 3.0);
    const double through = (1.0 - leak) * std::pow(10.0, -loss / 10.0) + leak;
    return -10.0 * std::log10(through);
}

const CabinSpec& cabinSpec(Insulation insulation) {
    // Voir docs/modele.md : tôle de 0,8 mm (6,3 kg/m²), vitre de 4 mm (10 kg/m²), barrière de plancher et de cloison.
    static const CabinSpec stripped{4.0, 4.8, 10900.0, 6.5, 100000.0, 0.0, 3e-3, 0.08, 5.0f};
    static const CabinSpec light{5.5, 8.0, 3800.0, 9.0, 800.0, 8.0, 8e-4, 0.3, 2.0f};
    static const CabinSpec standard{7.0, 10.0, 3100.0, 12.0, 400.0, 14.0, 2.5e-4, 0.55, 1.0f};
    static const CabinSpec comfort{10.0, 13.0, 3100.0, 17.0, 350.0, 18.0, 8e-5, 0.6, 0.6f};
    static const CabinSpec luxury{15.0, 20.0, 3500.0, 26.0, 300.0, 24.0, 2e-5, 0.65, 0.35f};
    switch (insulation) {
        case Insulation::Stripped: return stripped;
        case Insulation::Light: return light;
        case Insulation::Comfort: return comfort;
        case Insulation::Luxury: return luxury;
        case Insulation::Standard: break;
    }
    return standard;
}

double cabinTransfer(double hz, const CarAcoustics& car, Source source) {
    const CabinSpec& spec = cabinSpec(car.insulation);
    const SourcePlace& place = car.sources[static_cast<std::size_t>(source)];
    // Le bloc lui-même : c'est la cloison qui le voit, pas l'ouverture du compartiment.
    const Point3 from = source == Source::Bay ? car.engine : place.at;
    // Les pneus sont répartis autour de la voiture, mais chacun est tout près du plancher.
    const double extent = source == Source::Road ? 0.4 : static_cast<double>(place.extentM);

    const double x1 = static_cast<double>(car.seat.x) - static_cast<double>(car.cabinBackM);
    const double x2 = static_cast<double>(car.seat.x) + static_cast<double>(car.cabinFrontM);
    const double length = x2 - x1;
    const double middle = (x1 + x2) * 0.5;
    const double width = static_cast<double>(car.widthM);
    const double height = static_cast<double>(car.heightM);
    const bool engineBehind = car.placement != EnginePlacement::Front;

    struct Panel {
        double x, y, z, nx, ny, nz, area, mass, coincidence, damping;
        bool trimmed, glass, side;
    };
    const double steel = spec.steelKgM2;
    const double wall = spec.firewallKgM2;
    const double glass = spec.glassKgM2;
    const double glassAt = spec.glassCoincidenceHz;
    const std::array<Panel, 10> panels{{
        {middle, 0.0, 0.2, 0.0, 0.0, -1.0, length * width * 0.9, wall, 15000.0, 6.0, true, false, false},
        {middle, 0.0, height, 0.0, 0.0, 1.0, length * width * 0.75, steel, 15000.0, 6.0, true, false, false},
        {middle, width / 2.0, 0.6, 0.0, 1.0, 0.0, length * 0.5, steel, 15000.0, 6.0, true, false, false},
        {middle, -width / 2.0, 0.6, 0.0, -1.0, 0.0, length * 0.5, steel, 15000.0, 6.0, true, false, false},
        {middle, width / 2.0, height - 0.25, 0.0, 1.0, 0.0, length * 0.38, glass, glassAt, 3.0, false, true, true},
        {middle, -width / 2.0, height - 0.25, 0.0, -1.0, 0.0, length * 0.38, glass, glassAt, 3.0, false, true, true},
        {x2, 0.0, 0.55, 1.0, 0.0, 0.0, width * 0.5, engineBehind ? steel : wall, 15000.0, 6.0, true, false, false},
        {x2, 0.0, height - 0.28, 1.0, 0.0, 0.0, width * 0.62, glass * 1.2, glassAt, 6.0, false, true, false},
        {x1, 0.0, 0.6, -1.0, 0.0, 0.0, width * 0.6, engineBehind ? wall : steel, 15000.0, 6.0, true, false, false},
        {x1, 0.0, height - 0.28, -1.0, 0.0, 0.0, width * 0.42, glass, glassAt, 3.0, false, true, false},
    }};

    // Aux basses fréquences, l'onde contourne : toutes les parois reçoivent. Plus haut, seules celles qui font face à la source.
    const double spread = hz / (hz + 400.0);
    double sum = 0.0;
    double surface = 0.0;
    std::array<double, panels.size()> heard{};
    std::array<double, panels.size()> closeness{};
    int count = 0;
    for (const Panel& panel : panels) {
        const double dx = static_cast<double>(from.x) - panel.x;
        const double dy = static_cast<double>(from.y) - panel.y;
        const double dz = static_cast<double>(from.z) - panel.z;
        const double r2 = dx * dx + dy * dy + dz * dz;
        const double r = std::sqrt(r2);
        const double facing = r > 1e-3 ? std::max((dx * panel.nx + dy * panel.ny + dz * panel.nz) / r, 0.0) : 0.5;
        const double exposure = (1.0 - spread) + spread * (0.04 + 0.96 * facing * facing);
        // Une vitre ouverte : celle du conducteur. Ce qui passe par ce trou n'a plus rien à traverser.
        const double leak = car.windowsOpen && panel.side && panel.y > 0.0 ? 0.3 : spec.leak;
        const double trimFrom = panel.trimmed ? spec.trimFromHz : 1e9;
        const double loss = panelLossDb(hz, panel.mass, panel.coincidence, panel.damping, trimFrom, spec.trimCapDb, leak);
        // Tout près d'une paroi, on en reçoit aussi le champ direct : c'est ce qui fait un moteur derrière la nuque.
        const double hx = static_cast<double>(car.seat.x) - panel.x;
        const double hy = static_cast<double>(car.seat.y) - panel.y;
        const double hz2 = static_cast<double>(car.seat.z) - panel.z;
        const double toHead2 = std::max(hx * hx + hy * hy + hz2 * hz2, 0.0625);
        heard[static_cast<std::size_t>(count)] = panel.area * std::pow(10.0, -loss / 10.0) * exposure / (r2 + extent * extent);
        closeness[static_cast<std::size_t>(count)] = toHead2;
        ++count;
        surface += panel.area;
    }
    // Absorption de l'habitacle : les tissus et mousses travaillent surtout au-dessus de 500 Hz.
    const double shape = std::min(0.22 + 0.78 * std::sqrt(hz / 1000.0), 1.15);
    const double absorption = surface * spec.absorption * shape;
    // p² = 4·W/A dans le champ diffus, plus W/(2π·d²) près de la paroi, avec W = τ·S·p²/(2ρc) pour une paroi vue sous tous les angles.
    for (int i = 0; i < count; ++i) {
        const double direct = absorption / (8.0 * kPi * closeness[static_cast<std::size_t>(i)]);
        sum += heard[static_cast<std::size_t>(i)] * (1.0 + direct);
    }
    // Les pneus entrent aussi par les passages de roues, bien moins lourds que le plancher : 4 dB de plus.
    const double wheelArches = source == Source::Road ? 2.5 : 1.0;
    return std::sqrt(2.0 * wheelArches * sum / absorption);
}

// ── Filtres ──

void Shelf::set(float poleHz, float gainHigh, float rate) {
    const float pole = poleOf(poleHz, rate);
    const float zero = poleOf(poleHz / gainHigh, rate);
    b0 = (1.0f - pole) / (1.0f - zero);
    b1 = -b0 * zero;
    a1 = pole;
}

void Shelf::setPoleZero(float poleHz, float zeroHz, float rate) {
    const float pole = poleOf(poleHz, rate);
    const float zero = poleOf(zeroHz, rate);
    b0 = (1.0f - pole) / (1.0f - zero);
    b1 = -b0 * zero;
    a1 = pole;
}

void AWeighting::init(float rate) {
    rate_ = rate;
    const double fs = static_cast<double>(rate);
    // Pôles analogiques de la norme, en hertz. Le plus aigu est prédéformé pour rester juste jusqu'à 12 kHz.
    constexpr double f1 = 20.598997, f2 = 107.65265, f3 = 737.86223, f4 = 12194.217;
    const double warp = 2.0 * fs;
    const double w1 = 2.0 * kPi * f1, w2 = 2.0 * kPi * f2, w3 = 2.0 * kPi * f3;
    const double w4 = warp * std::tan(kPi * f4 / fs);
    // Bilinéaire d'une paire de pôles réels (s + a)(s + b), avec un numérateur donné.
    const auto section = [warp](double a, double b, bool highPass) {
        Section s;
        const double a0 = (warp + a) * (warp + b);
        s.a1 = 2.0 * (a * b - warp * warp) / a0;
        s.a2 = (warp - a) * (warp - b) / a0;
        if (highPass) {
            const double k = warp * warp / a0;
            s.b0 = k;
            s.b1 = -2.0 * k;
            s.b2 = k;
        } else {
            const double k = 1.0 / a0;
            s.b0 = k;
            s.b1 = 2.0 * k;
            s.b2 = k;
        }
        return s;
    };
    sections_[0] = section(w1, w1, true);
    sections_[1] = section(w2, w3, true);
    sections_[2] = section(w4, w4, false);
    // Le gain : 0 dB à 1 kHz, exactement.
    double gain = w4 * w4;
    sections_[2].b0 *= gain;
    sections_[2].b1 *= gain;
    sections_[2].b2 *= gain;
    gain = std::pow(10.0, -responseDb(1000.0) / 20.0);
    sections_[2].b0 *= gain;
    sections_[2].b1 *= gain;
    sections_[2].b2 *= gain;
}

double AWeighting::process(double x) {
    for (Section& s : sections_) {
        const double y = s.b0 * x + s.z1;
        s.z1 = s.b1 * x - s.a1 * y + s.z2;
        s.z2 = s.b2 * x - s.a2 * y;
        x = y;
    }
    return x;
}

double AWeighting::responseDb(double hz) const {
    const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / static_cast<double>(rate_));
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> total(1.0, 0.0);
    for (const Section& s : sections_) total *= (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
    return 20.0 * std::log10(std::abs(total));
}

void LevelMeter::init(float rate) {
    left_.init(rate);
    right_.init(rate);
    coefficient_ = 1.0 - std::exp(-1.0 / (0.125 * static_cast<double>(rate)));
}

void LevelMeter::add(float left, float right) {
    const double l = left_.process(static_cast<double>(left));
    const double r = right_.process(static_cast<double>(right));
    energy_ += (0.5 * (l * l + r * r) - energy_) * coefficient_;
}

void LevelMeter::addMono(float mono) {
    const double x = left_.process(static_cast<double>(mono));
    energy_ += (x * x - energy_) * coefficient_;
}

float LevelMeter::db() const {
    return static_cast<float>(10.0 * std::log10(std::max(energy_, 1e-12) / (kReferencePressure * kReferencePressure)));
}

// ── Le champ extérieur ──

SoundField::SoundField(float rate) : rate_(rate) {
    int size = 2;
    while (static_cast<float>(size) < kMaxDelayS * rate + 8.0f) size *= 2;
    mask_ = size - 1;
    for (Emitter& emitter : emitters_) emitter.ring.assign(static_cast<std::size_t>(size), 0.0f);
    airPerMetre8k_ = static_cast<float>(airAbsorptionDbPerM(8000.0));
    // Mêmes retards que l'ancienne pièce à gauche, d'autres à droite : les deux oreilles ne se ressemblent plus.
    ambientLeft_[0].init(rate, 0.0031f, 0.55f);
    ambientLeft_[1].init(rate, 0.0073f, 0.5f);
    ambientLeft_[2].init(rate, 0.0127f, 0.45f);
    ambientRight_[0].init(rate, 0.0037f, 0.55f);
    ambientRight_[1].init(rate, 0.0083f, 0.5f);
    ambientRight_[2].init(rate, 0.0113f, 0.45f);
    ambient_ = kAmbientGain;
}

void SoundField::setCar(const CarAcoustics& car, bool hasExhaust) {
    car_ = car;
    for (int i = 0; i < kSources; ++i) {
        Emitter& emitter = emitters_[static_cast<std::size_t>(i)];
        emitter.place = car.sources[static_cast<std::size_t>(i)];
        const Source source = static_cast<Source>(i);
        emitter.enabled = hasExhaust || (source != Source::ExhaustA && source != Source::ExhaustB && source != Source::Intake);
    }
    dirty_ = true;
    snap_ = true;
}

void SoundField::setTarget(const Point3& at, float facing, float maxSpeed) {
    target_ = at;
    facesCar_ = std::isnan(facing);
    if (!facesCar_) facingWanted_ = facing;
    maxSpeed_ = maxSpeed;
    dirty_ = true;
}

void SoundField::setStereo(bool stereo) {
    if (stereo == stereo_) return;
    stereo_ = stereo;
    dirty_ = true;
    snap_ = true;
}

void SoundField::control(int controlSamples) {
    const float dt = static_cast<float>(controlSamples) / rate_;
    bool moving = dirty_;
    if (dirty_) {
        const float dx = target_.x - head_.x;
        const float dy = target_.y - head_.y;
        const float dz = target_.z - head_.z;
        const float distance = length3(dx, dy, dz);
        if (snap_) {
            head_ = target_;
        } else if (distance > 1e-4f) {
            // Exponentielle, plafonnée : la tête ne se téléporte pas, et le retard ne saute jamais.
            const float glide = distance * (1.0f - km::exp(-dt / kSmoothS));
            const float step = km::min(glide, maxSpeed_ * dt);
            const float share = step / distance;
            head_.x += dx * share;
            head_.y += dy * share;
            head_.z += dz * share;
        } else {
            head_ = target_;
        }
        // À l'arrivée, plus rien ne change : on arrête les calculs de géométrie, une fois les rampes finies.
        dirty_ = distance > 1e-4f;
    }
    if (moving) {
        updateGeometry(controlSamples, snap_);
        snap_ = false;
    } else {
        for (Emitter& emitter : emitters_) {
            for (Ear& ear : emitter.ears) {
                ear.direct.delayStep = 0.0f;
                ear.direct.gainStep = 0.0f;
                ear.mirror.delayStep = 0.0f;
                ear.mirror.gainStep = 0.0f;
            }
        }
        outsideStep_ = 0.0f;
    }
}

void SoundField::updateGeometry(int controlSamples, bool snap) {
    // Dans l'habitacle ? Une courte transition à la traversée de la caisse.
    const float x1 = car_.seat.x - car_.cabinBackM;
    const float x2 = car_.seat.x + car_.cabinFrontM;
    const float halfWidth = car_.widthM * 0.5f - 0.05f;
    const float beyond = km::max(km::max(km::max(x1 - head_.x, head_.x - x2), km::abs(head_.y) - halfWidth), km::max(0.35f - head_.z, head_.z - (car_.heightM - 0.03f)));
    const float t = km::coerceIn((0.05f - beyond) / 0.15f, 0.0f, 1.0f);
    const float wantedInside = t * t * (3.0f - 2.0f * t);
    // On ne traverse pas une portière en un instant : le passage prend au moins ce temps, même à toute vitesse.
    if (snap) inside_ = wantedInside;
    else inside_ += (wantedInside - inside_) * (1.0f - km::exp(-static_cast<float>(controlSamples) / rate_ / kInsideGlideS));
    if (km::abs(wantedInside - inside_) > 0.001f) dirty_ = true;
    else inside_ = wantedInside;

    // Tout près du bloc, on est sous le capot : la source n'est plus l'ouverture, mais le moteur.
    const float toEngine = length3(head_.x - car_.engine.x, head_.y - car_.engine.y, head_.z - car_.engine.z);
    const float open = km::coerceIn((1.05f - toEngine) / 0.45f, 0.0f, 1.0f);
    bayOpen_ = open * open * (3.0f - 2.0f * open);

    // La tête regarde la voiture, ou la route depuis son siège.
    if (facesCar_) {
        facingWanted_ = static_cast<float>(std::atan2(-static_cast<double>(head_.y), -static_cast<double>(head_.x)));
        if (inside_ > 0.5f) facingWanted_ = 0.0f;
    }
    if (snap) {
        facing_ = facingWanted_;
    } else {
        float turn = facingWanted_ - facing_;
        const float full = 2.0f * static_cast<float>(kPi);
        if (turn > static_cast<float>(kPi)) turn -= full;
        if (turn < -static_cast<float>(kPi)) turn += full;
        facing_ += turn * (1.0f - km::exp(-static_cast<float>(controlSamples) / rate_ / 0.12f));
    }

    for (int i = 0; i < kSources; ++i) updateEmitter(emitters_[static_cast<std::size_t>(i)], i, controlSamples, snap);
    jetExposed_ = false;
    for (const auto& outlet : jetGain_) {
        for (float gain : outlet) jetExposed_ = jetExposed_ || gain > 1e-4f;
    }

    const float outside = km::sqrt(km::max(1.0f - inside_, 0.0f));
    outsideStep_ = snap ? 0.0f : (outside - outsideGain_) / static_cast<float>(controlSamples);
    if (snap) outsideGain_ = outside;
}

void SoundField::updateEmitter(Emitter& e, int index, int controlSamples, bool snap) {
    const Source source = static_cast<Source>(index);
    Point3 from = e.place.at;
    float level = 1.0f;
    if (source == Source::Bay) {
        from.x += (car_.engine.x - from.x) * bayOpen_;
        from.y += (car_.engine.y - from.y) * bayOpen_;
        from.z += (car_.engine.z - from.z) * bayOpen_;
        level = kBayClosedLevel + (1.0f - kBayClosedLevel) * bayOpen_;
        e.directivity.set(kBayClosedPoleHz, kBayClosedHigh + (1.0f - kBayClosedHigh) * bayOpen_, rate_);
    }

    const float tx = from.x - head_.x;
    const float ty = from.y - head_.y;
    const float tz = from.z - head_.z;
    const float centre = km::max(length3(tx, ty, tz), 1e-3f);

    if (source == Source::ExhaustA || source == Source::ExhaustB) {
        // L'axe de la sortie pointe vers l'arrière : l'angle entre lui et la tête.
        const float degrees = acosClamped(tx / centre) * 180.0f / static_cast<float>(kPi);
        const float ka1 = kSoundSpeed / (2.0f * static_cast<float>(kPi) * car_.outletRadiusM);
        e.directivity.setPoleZero(ka1 / km::max(interpolate(kOutletAngles, kOutletInversePole, degrees), 1e-3f),
                                  ka1 / km::max(interpolate(kOutletAngles, kOutletInverseZero, degrees), 1e-3f), rate_);
        e.flowCos = tx / centre;
    }

    // Le masquage par la caisse : 10·log10(1 + f/fc), soit 3 dB par octave au-delà de fc, avec fc = c/(40·excès).
    // Les pneus sont aux quatre coins, et le plancher rayonne par ses côtés : la caisse ne les cache pas.
    const float excess = source == Source::Road || source == Source::Underfloor ? 0.0f : pathExcessM(from, head_, car_);
    if (excess > 1e-3f) {
        const float fc = kSoundSpeed / (40.0f * excess);
        e.maskLow.set(fc * 1.2f, 0.32f, rate_);
        // Une source basse passe aussi sous la voiture : le masquage plafonne.
        if (from.z < kUnderCarHeight) e.maskHigh.bypass();
        else e.maskHigh.set(fc * 14.0f, 0.32f, rate_);
    } else {
        e.maskLow.bypass();
        e.maskHigh.bypass();
    }
    // Capot fermé, les aigus du bloc restent dans le compartiment : garnissage, et trop peu d'ouverture pour eux.
    if (source == Source::Bay) e.maskHigh.set(kBayTopHz, kBayClosedHigh + (1.0f - kBayClosedHigh) * bayOpen_, rate_);

    // L'air : presque rien de près, mais à 50 m il prend 6 dB à 8 kHz. Un pôle suffit : la loi est en f².
    const float airDb = airPerMetre8k_ * centre;
    const float excessPower = km::max(km::pow(10.0f, airDb / 10.0f) - 1.0f, 1e-6f);
    float airHz = 8000.0f / km::sqrt(excessPower);
    // Au-delà du mode plan (ka ≈ 3,8), le jet chaud et la couche de mélange diffusent les aigus : la bouche ne concentre plus rien.
    if (source == Source::ExhaustA || source == Source::ExhaustB) airHz = km::min(airHz, kOutletTopKa * kSoundSpeed / (2.0f * static_cast<float>(kPi) * car_.outletRadiusM));
    e.airCoef = 1.0f - poleOf(airHz, rate_);

    const float mirrorZ = -from.z;
    const float extent2 = e.place.extentM * e.place.extentM;
    const float mx = tx;
    const float my = ty;
    const float mz = mirrorZ - head_.z;
    const float mirrorCentre = km::max(length3(mx, my, mz), 1e-3f);
    // Sous 250 Hz, le trajet et son écho restent en phase, quoi qu'il arrive. Au-dessus, seule une part reste cohérente.
    const float mirrorCoef = 1.0f - poleOf(kMirrorCoherentHz, rate_);
    e.mirrorCoherence = e.place.groundCoherence;

    // Une source muette ne suit pas ses rampes : ses valeurs sautent directement, sans que rien ne s'entende.
    const bool jump = snap || e.idle > mask_;
    const auto setPath = [&](Path& path, float metres, float gain) {
        const float delay = km::coerceIn(km::min(metres, kMaxDistanceM) / kSoundSpeed * rate_, kMinDelaySamples, kMaxDelayS * rate_);
        if (jump) {
            path.delay = delay;
            path.gain = gain;
            path.delayStep = 0.0f;
            path.gainStep = 0.0f;
        } else {
            path.delayStep = (delay - path.delay) / static_cast<float>(controlSamples);
            path.gainStep = (gain - path.gain) / static_cast<float>(controlSamples);
        }
    };

    double reflectMagnitude = 1.0, reflectPhase = 0.0;
    groundReflection(1000.0, kAsphaltSigma, static_cast<double>(km::max((from.z + head_.z) / mirrorCentre, 0.05f)), reflectMagnitude, reflectPhase);
    const float reflect = static_cast<float>(reflectMagnitude);

    // Dans le jet ? Le gaz sort vers l'arrière, le long de x. Sa vitesse tombe en 6·D/z, sur un cône d'un demi-angle de 5,4° à mi-vitesse.
    const auto jetAt = [&](const Point3& ear) {
        const float along = from.x - ear.x;
        if (along <= 0.0f || (source != Source::ExhaustA && source != Source::ExhaustB)) return 0.0f;
        const float across2 = (ear.y - from.y) * (ear.y - from.y) + (ear.z - from.z) * (ear.z - from.z);
        const float radius = km::max(car_.outletRadiusM, 0.094f * along);
        const float diameter = 2.0f * car_.outletRadiusM;
        const float centreline = km::min(6.0f * diameter / km::max(along, 1e-3f), 1.0f);
        // La pression dynamique suit le carré de la vitesse ; profil gaussien.
        return centreline * centreline * km::exp(-2.0f * 0.693f * across2 / (radius * radius));
    };
    const int jetIndex = source == Source::ExhaustA ? 0 : (source == Source::ExhaustB ? 1 : -1);

    if (!stereo_) {
        Ear& ear = e.ears[0];
        ear.mirrorCoef = mirrorCoef;
        setPath(ear.direct, centre, level / km::sqrt(centre * centre + extent2));
        setPath(ear.mirror, mirrorCentre, level * reflect / km::sqrt(mirrorCentre * mirrorCentre + extent2));
        if (jetIndex >= 0) jetGain_[static_cast<std::size_t>(jetIndex)][0] = jetAt(head_);
        return;
    }

    const float sine = static_cast<float>(std::sin(static_cast<double>(facing_)));
    const float cosine = static_cast<float>(std::cos(static_cast<double>(facing_)));
    // L'axe des oreilles : à gauche du regard. Le regard vaut (cos, sin), donc la gauche (-sin, cos).
    const float axisX = -sine;
    const float axisY = cosine;
    for (int side = 0; side < 2; ++side) {
        Ear& ear = e.ears[static_cast<std::size_t>(side)];
        const float sign = side == 0 ? 1.0f : -1.0f;
        const float cosAngle = km::coerceIn((tx * axisX + ty * axisY) * sign / centre, -1.0f, 1.0f);
        const float theta = acosClamped(cosAngle);
        const float path = earPathM(centre, theta);
        const float cosMirror = km::coerceIn((mx * axisX + my * axisY) * sign / mirrorCentre, -1.0f, 1.0f);
        const float pathMirror = earPathM(mirrorCentre, acosClamped(cosMirror));
        ear.mirrorCoef = mirrorCoef;
        setPath(ear.direct, path, level / km::sqrt(path * path + extent2));
        setPath(ear.mirror, pathMirror, level * reflect / km::sqrt(pathMirror * pathMirror + extent2));
        ear.head.set(1248.0f, headShadowAlpha(theta), rate_);
        if (jetIndex >= 0) {
            const Point3 earAt{head_.x + axisX * sign * kHeadRadius, head_.y + axisY * sign * kHeadRadius, head_.z};
            jetGain_[static_cast<std::size_t>(jetIndex)][static_cast<std::size_t>(side)] = jetAt(earAt);
        }
    }
}

void SoundField::process(const std::array<float, kSources>& in, float& left, float& right) {
    float outL = 0.0f, outR = 0.0f, send = 0.0f;
    const int ears = stereo_ ? 2 : 1;
    for (int i = 0; i < kSources; ++i) {
        Emitter& e = emitters_[static_cast<std::size_t>(i)];
        if (!e.enabled) continue;
        float raw = in[static_cast<std::size_t>(i)];
        // L'écoulement porte le son vers l'aval : un dipôle s'ajoute au monopôle, en M·cos θ (Masovic et al., 2017).
        if (i <= static_cast<int>(Source::ExhaustB)) raw *= 1.0f + jetSpeed_ / kSoundSpeed * e.flowCos;
        // Une source muette depuis longtemps ne coûte rien : tout son état est à zéro.
        if (raw == 0.0f) {
            if (e.idle > mask_) continue;
            ++e.idle;
        } else {
            e.idle = 0;
        }
        const float shaped = e.maskHigh.process(e.maskLow.process(e.directivity.process(raw)));
        e.air += (shaped - e.air) * e.airCoef;
        const float x = e.air;
        send += x;
        e.at = (e.at + 1) & mask_;
        e.ring[static_cast<std::size_t>(e.at)] = x;
        if (outsideGain_ <= 0.0f && outsideStep_ == 0.0f) continue;
        for (int side = 0; side < ears; ++side) {
            Ear& ear = e.ears[static_cast<std::size_t>(side)];
            const auto tap = [&e, this](float delay) {
                const int whole = static_cast<int>(delay);
                const float t = delay - static_cast<float>(whole);
                const int newest = e.at - whole + 1;
                const float xm1 = e.ring[static_cast<std::size_t>(newest & mask_)];
                const float x0 = e.ring[static_cast<std::size_t>((newest - 1) & mask_)];
                const float x1 = e.ring[static_cast<std::size_t>((newest - 2) & mask_)];
                const float x2 = e.ring[static_cast<std::size_t>((newest - 3) & mask_)];
                // Interpolation de Lagrange d'ordre 3 : plate jusqu'aux aigus, sans clic quand le retard glisse.
                const float a = t - 1.0f;
                const float b = t - 2.0f;
                const float c = t + 1.0f;
                return (-t * a * b / 6.0f) * xm1 + (c * a * b / 2.0f) * x0 + (-c * t * b / 2.0f) * x1 + (c * t * a / 6.0f) * x2;
            };
            float y = tap(ear.direct.delay) * ear.direct.gain;
            const float echo = tap(ear.mirror.delay);
            ear.mirrorDamp += (echo - ear.mirrorDamp) * ear.mirrorCoef;
            y += (ear.mirrorDamp + (echo - ear.mirrorDamp) * e.mirrorCoherence) * ear.mirror.gain;
            ear.direct.delay += ear.direct.delayStep;
            ear.direct.gain += ear.direct.gainStep;
            ear.mirror.delay += ear.mirror.delayStep;
            ear.mirror.gain += ear.mirror.gainStep;
            if (stereo_) y = ear.head.process(y);
            if (side == 0) outL += y;
            else outR += y;
        }
    }
    send *= ambient_;
    float wetL = send;
    for (auto& room : ambientLeft_) wetL = room.process(wetL);
    outL += wetL;
    if (stereo_) {
        float wetR = send;
        for (auto& room : ambientRight_) wetR = room.process(wetR);
        outR += wetR;
    } else {
        outR = outL;
    }
    // Le vent du jet n'a pas de trajet : c'est une pression sur place.
    if (jetExposed_) {
        outL += jetSignal_[0] * jetGain_[0][0] + jetSignal_[1] * jetGain_[1][0];
        if (stereo_) outR += jetSignal_[0] * jetGain_[0][1] + jetSignal_[1] * jetGain_[1][1];
        else outR = outL;
    }
    left = outL * outsideGain_;
    right = outR * outsideGain_;
    outsideGain_ += outsideStep_;
}

// ── L'habitacle ──

CabinPath::CabinPath(float rate) : rate_(rate) {
    fft_.init(kFirLength * 2);
    re_.assign(kFirLength * 2, 0.0f);
    im_.assign(kFirLength * 2, 0.0f);
    fir_.assign(kFirLength, 0.0f);
    for (dsp::Convolver& filter : filters_) filter.init(128, kFirLength / 128);
    roomLeft_[0].init(rate, 0.0031f, 0.55f);
    roomLeft_[1].init(rate, 0.0073f, 0.5f);
    roomLeft_[2].init(rate, 0.0127f, 0.45f);
    roomRight_[0].init(rate, 0.0041f, 0.55f);
    roomRight_[1].init(rate, 0.0067f, 0.5f);
    roomRight_[2].init(rate, 0.0139f, 0.45f);
    boomBody_.set(36.0f, 3.0f, rate);
    boomFloor_.set(85.0f, 4.0f, rate);
}

int CabinPath::groupOf(Source source) {
    switch (source) {
        case Source::ExhaustA:
        case Source::ExhaustB: return 0;
        case Source::Intake:
        case Source::Bay: return 1;
        default: return 2;
    }
}

void CabinPath::design(int group, const CarAcoustics& car) {
    const Source source = group == 0 ? Source::ExhaustA : (group == 1 ? Source::Bay : Source::Road);
    const int size = kFirLength * 2;
    const int half = kFirLength;
    // Module voulu, en log : le filtre à phase minimale s'en déduit par le cepstre réel.
    for (int k = 0; k <= half; ++k) {
        const double hz = std::max(static_cast<double>(k) * static_cast<double>(rate_) / size, 15.0);
        const float value = static_cast<float>(std::log(std::max(cabinTransfer(hz, car, source), 1e-7)));
        re_[static_cast<std::size_t>(k)] = value;
        im_[static_cast<std::size_t>(k)] = 0.0f;
        if (k > 0 && k < half) {
            re_[static_cast<std::size_t>(size - k)] = value;
            im_[static_cast<std::size_t>(size - k)] = 0.0f;
        }
    }
    fft_.transform(re_.data(), im_.data(), true);
    const float scale = 1.0f / static_cast<float>(size);
    for (int n = 0; n < size; ++n) {
        const float folded = n == 0 || n == half ? 1.0f : (n < half ? 2.0f : 0.0f);
        re_[static_cast<std::size_t>(n)] *= scale * folded;
        im_[static_cast<std::size_t>(n)] = 0.0f;
    }
    fft_.transform(re_.data(), im_.data(), false);
    for (int k = 0; k < size; ++k) {
        const double magnitude = std::exp(static_cast<double>(re_[static_cast<std::size_t>(k)]));
        const double angle = static_cast<double>(im_[static_cast<std::size_t>(k)]);
        re_[static_cast<std::size_t>(k)] = static_cast<float>(magnitude * std::cos(angle));
        im_[static_cast<std::size_t>(k)] = static_cast<float>(magnitude * std::sin(angle));
    }
    fft_.transform(re_.data(), im_.data(), true);
    for (int n = 0; n < kFirLength; ++n) {
        const float rest = static_cast<float>(kFirLength - n) / (static_cast<float>(kFirLength) * 0.25f);
        const float fade = rest < 1.0f ? rest * rest * (3.0f - 2.0f * rest) : 1.0f;
        fir_[static_cast<std::size_t>(n)] = re_[static_cast<std::size_t>(n)] * scale * fade;
    }
    filters_[static_cast<std::size_t>(group)].setImpulse(fir_.data(), kFirLength);
}

void CabinPath::setCar(const CarAcoustics& car) {
    for (int group = 0; group < kGroups; ++group) design(group, car);
    const CabinSpec& spec = cabinSpec(car.insulation);
    // Les modes de la caisse : c/2L dans chaque direction, plus francs quand rien n'absorbe.
    const float depth = car.cabinBackM + car.cabinFrontM;
    const float q = spec.absorption < 0.1 ? 14.0f : (spec.absorption < 0.3 ? 9.0f : 6.0f);
    modes_[0].set(kSoundSpeed / (2.0f * depth), q, rate_);
    modes_[1].set(kSoundSpeed / (2.0f * (car.widthM - 0.3f)), q, rate_);
    modes_[2].set(kSoundSpeed / (2.0f * (car.heightM - 0.45f)), q, rate_);
    const float strength = spec.absorption < 0.1 ? 0.5f : (spec.absorption < 0.3 ? 0.35f : 0.22f);
    modeGain_ = {strength, strength * 0.6f, strength * 0.4f};
    roomMix_ = spec.absorption < 0.1 ? 0.6f : (spec.absorption < 0.3 ? 0.45f : 0.35f);
    structureGain_ = spec.structureGain;
    reset();
}

void CabinPath::reset() {
    for (dsp::Convolver& filter : filters_) filter.reset();
}

void CabinPath::process(const std::array<float, kSources>& in, float structure, float& left, float& right, bool stereo) {
    const auto at = [&in](Source source) { return in[static_cast<std::size_t>(source)]; };
    float sum = filters_[0].process(at(Source::ExhaustA) + at(Source::ExhaustB));
    sum += filters_[1].process(at(Source::Bay) + at(Source::Intake));
    sum += filters_[2].process(at(Source::Road) + at(Source::Underfloor));
    // Les modes s'ajoutent à ce qui entre : c'est le bourdonnement quand un ordre moteur tombe dessus.
    float modal = 0.0f;
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        modes_[i].process(sum);
        modal += modes_[i].bp * modeGain_[i];
    }
    sum += modal;
    // Les supports moteur et les attaches de ligne : la caisse vibre, surtout dans les bas ordres.
    boomBody_.process(structure);
    boomFloor_.process(structure);
    sum += (boomBody_.bp + boomFloor_.bp) * structureGain_;

    float wet = sum;
    for (auto& room : roomLeft_) wet = room.process(wet);
    left = sum * (1.0f - roomMix_) + wet * roomMix_;
    if (stereo) {
        wet = sum;
        for (auto& room : roomRight_) wet = room.process(wet);
        right = sum * (1.0f - roomMix_) + wet * roomMix_;
    } else {
        right = left;
    }
}

}  // namespace revforge::acoustics
