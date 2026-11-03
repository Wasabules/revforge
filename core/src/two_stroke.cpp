#include "revforge/two_stroke.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "exhaust_line.hpp"
#include "portable_math.hpp"

#ifdef REVFORGE_TWO_STROKE_TRACE
#include <cstdio>
std::FILE* g_trace = nullptr;
#endif

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kGamma = 1.35;
constexpr double kGasR = 287.0;
constexpr double kCv = kGasR / (kGamma - 1.0);
constexpr double kCp = kGamma * kCv;
constexpr double kAtmosphere = 101325.0;
constexpr double kAmbientK = 293.0;
constexpr double kAirDensity = kAtmosphere / (kGasR * kAmbientK);

// ── Les lumières ──
// Un moteur qui tourne vite a des lumières plus hautes : il lui faut la même section de passage en moins de temps (la « surface-temps »
// de Blair). Les bornes, d'un moteur de tourisme à un moteur de course, sont des ordres de grandeur (supposés) : de 144° à 178°
// d'échappement, de 111° à 130° de transfert, et une admission par la jupe du piston de 140° à 160°.
constexpr double kExhaustHeightTouring = 0.28;
constexpr double kExhaustHeightRacing = 0.42;
constexpr double kTransferHeightTouring = 0.17;
constexpr double kTransferHeightRacing = 0.24;
constexpr double kInletTravelTouring = 0.36;
constexpr double kInletTravelRacing = 0.48;
constexpr double kTouringRpm = 4500.0;
constexpr double kRacingSpanRpm = 5000.0;
// Bielle de deux courses (l/r = 4), courante sur les deux-temps (supposé).
constexpr double kRodOverStroke = 2.0;
// Largeurs en part de l'alésage : une fenêtre d'échappement retient le segment jusqu'à 0,6 à 0,7 de l'alésage ; deux ou trois
// fenêtres de transfert de part et d'autre ; une admission un peu plus étroite que l'échappement (supposé).
constexpr double kExhaustWidth = 0.62;
constexpr double kTransferWidth = 0.95;
constexpr double kInletWidth = 0.6;
// Taux géométrique d'un deux-temps de tourisme (Saab 96 : 7,3 ; Trabant 601 : 7,8), et du carter (supposé, 1,3 à 1,5 d'ordinaire).
constexpr double kGeometricCompression = 7.5;
constexpr double kCrankcaseCompression = 1.35;

// ── La chambre de détente : voir docs/modele.md ──
// Le gaz qui la remplit, brûlé et dilué par ce qui court-circuite : 650 K, soit 501 m/s (supposé ; Jennings compte 1700 ft/s, 518 m/s).
constexpr double kChamberGasK = 650.0;
// Le collecteur un peu plus large que la lumière, le ventre 2,75 fois lui, la pointe 0,6 fois (Blair : 2,5 à 3,25 ; Jennings : 0,58 à 0,62).
constexpr double kHeaderOverPort = 1.1;
constexpr double kBellyOverHeader = 2.75;
constexpr double kStingerOverHeader = 0.6;
constexpr double kStingerLengthOverDiameter = 12.0;
// L'accord se compte de la lumière au milieu du cône de retour : la part de chaque pièce dans cette longueur (supposée, proportions
// de chambres de route), le cône compté pour moitié.
constexpr double kHeaderShare = 0.27;
constexpr double kDiffuserShare = 0.38;
constexpr double kBellyShare = 0.17;
constexpr double kBaffleShare = 0.36;
// Le régime visé : celui de la puissance nominale, comme pour le turbo.
constexpr double kTunedShare = 0.88;
// Un quatre-temps (CLI seulement) : la soupape d'échappement reste ouverte 240°, sa section vaut 0,19 de l'alésage (comme à l'écran).
constexpr double kValveOpenDeg = 240.0;
constexpr double kValvePulseDeg = 20.0;
constexpr double kValveReturnDeg = 30.0;
constexpr double kValveAreaShare = 0.19;

// ── Le cycle intégré ──
// Pas fixe de 40 µs : l'onde avance de 2 cm par pas dans la chambre, et le vilebrequin de 1,2° à 5000 tr/min.
constexpr double kStepS = 40e-6;
constexpr int kMaxSegments = 600;
constexpr int kMaxStretches = 32;
// Le collecteur d'un cylindre avant la ligne commune : celui d'un moteur en ligne dans le son (`headerM`).
constexpr double kRunnerM = 0.62;
// Coefficients de débit des lumières (supposés, de l'ordre de ceux d'une soupape), et la colonne d'admission : lumière, pipe, carburateur.
constexpr double kExhaustCd = 0.7;
constexpr double kTransferCd = 0.65;
constexpr double kInletCd = 0.7;
constexpr double kInletDuctM = 0.30;
// Une bouche ouverte renvoie l'onde changée de signe, presque entière aux basses fréquences (Levine et Schwinger).
constexpr double kOpenEndReflection = -0.95;
// La chaleur de l'air frais brûlé : pouvoir calorifique sur rapport stœchiométrique, dont les deux tiers restent au gaz, comme à
// l'écran (`cylHeat` de fluidGas.ts) : le reste part aux parois et dans les gaz qui finissent de brûler.
constexpr double kHeatPerKgAir = 3.0e6 * 0.66;
// Rendement indiqué d'un quatre-temps atmosphérique (`kEfficiencyPetrol` de consumption.cpp), à 10,5 de compression, et ce que
// l'air frais apporte de chaleur au cycle : même pouvoir calorifique et même stœchiométrie que là-bas (42,9 MJ/kg, 14,7).
constexpr double kFourStrokeIndicated = 0.405;
constexpr double kFourStrokeCompression = 10.5;
constexpr double kExpansionGamma = 1.3;
constexpr double kLowerHeatingJPerKg = 42.9e6;
constexpr double kStoichiometric = 14.7;
constexpr double kReferenceDensity = 1.2;
// Table : du dixième du régime maximal à 1,2 fois.
constexpr double kTableFrom = 0.1;
constexpr double kTableTo = 1.2;
constexpr int kWarmRevolutions = 4;
constexpr int kRevolutions = 2;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

double pushedShare(const EngineConfig& e) { return clamp((static_cast<double>(e.redlineRpm) - kTouringRpm) / kRacingSpanRpm, 0.0, 1.0); }

double ottoEfficiency(double ratio) { return 1.0 - portable::pow(ratio, 1.0 - kExpansionGamma); }

/**
 * Le débit d'un orifice, par m² : sonique au col, puis ça ne monte plus. ψ² = 2γ/(γ−1)·(r^(2/γ) − r^((γ+1)/γ)) se tabule une fois,
 * divisé par (1 − r) pour rester lisse près de r = 1 : le calcul du cycle l'évalue des centaines de milliers de fois.
 */
constexpr int kFluxPoints = 256;
constexpr double kCritical = 0.5398;  // (2/(γ+1))^(γ/(γ-1)) à γ = 1,35

const std::array<double, kFluxPoints + 1>& fluxTable() {
    static const std::array<double, kFluxPoints + 1> table = [] {
        std::array<double, kFluxPoints + 1> out{};
        for (int i = 0; i <= kFluxPoints; ++i) {
            const double r = kCritical + (1.0 - kCritical) * static_cast<double>(i) / kFluxPoints;
            if (i == kFluxPoints) {
                // La limite en r = 1 : la pente de ψ², 2γ/(γ−1)·((γ+1)/γ − 2/γ) = 2.
                out[static_cast<std::size_t>(i)] = 2.0;
                continue;
            }
            const double l = portable::log(r);
            const double square = 2.0 * kGamma / (kGamma - 1.0) * (portable::exp(l * (2.0 / kGamma)) - portable::exp(l * ((kGamma + 1.0) / kGamma)));
            out[static_cast<std::size_t>(i)] = square / (1.0 - r);
        }
        return out;
    }();
    return table;
}

double orificeFlux(double pUp, double tUp, double pDown) {
    double ratio = pDown / pUp;
    if (ratio >= 1.0) return 0.0;
    if (ratio < kCritical) ratio = kCritical;
    const std::array<double, kFluxPoints + 1>& table = fluxTable();
    const double at = (ratio - kCritical) / (1.0 - kCritical) * kFluxPoints;
    const int low = std::min(static_cast<int>(at), kFluxPoints - 1);
    const double share = at - low;
    const double smooth = table[static_cast<std::size_t>(low)] + (table[static_cast<std::size_t>(low + 1)] - table[static_cast<std::size_t>(low)]) * share;
    return pUp / std::sqrt(kGasR * tUp) * std::sqrt(smooth * (1.0 - ratio));
}

/** La course du piston, tabulée au demi-degré pour une bielle de deux courses : le calcul du cycle la lit à chaque pas. */
constexpr int kTravelPoints = 720;

const std::array<double, kTravelPoints + 1>& travelTable() {
    static const std::array<double, kTravelPoints + 1> table = [] {
        std::array<double, kTravelPoints + 1> out{};
        for (int i = 0; i <= kTravelPoints; ++i) out[static_cast<std::size_t>(i)] = pistonTravelShare(360.0 * i / kTravelPoints, kRodOverStroke);
        return out;
    }();
    return table;
}

/**
 * La ligne, vue par l'onde : chaque tronçon de section constante est un retard aller et un retard retour, et à chaque changement de
 * section l'onde se partage (Kelly et Lochbaum) : r = (A₁ − A₂)/(A₁ + A₂). Une bouche ouverte la renvoie changée de signe.
 */
struct Pipe {
    struct Tube {
        int offset = 0;
        int length = 1;
        double keep = 1.0;
        double reflect = 0.0;  // vers le tube suivant
    };
    std::array<Tube, kMaxStretches> tubes{};
    std::array<double, kMaxSegments> forward{};
    std::array<double, kMaxSegments> backward{};
    std::array<double, kMaxStretches> leaving{};
    std::array<double, kMaxStretches> returning{};
    int count = 0;
    long clock = 0;
    double impedance = 0.0;  // c/A à la lumière : Pa par kg/s

    void build(const LineStretch* stretches, int n, double soundMs) {
        const double dx = soundMs * kStepS;
        count = 0;
        int used = 0;
        for (int s = 0; s < n && count < kMaxStretches; ++s) {
            const int cells = std::max(1, static_cast<int>(std::floor(stretches[s].lengthM / dx + 0.5)));
            if (used + cells > kMaxSegments) break;
            Tube& t = tubes[static_cast<std::size_t>(count)];
            t.offset = used;
            t.length = cells;
            t.keep = portable::exp(-stretches[s].lossPerM * dx * cells);
            used += cells;
            ++count;
        }
        for (int j = 0; j + 1 < count; ++j) {
            const double a = stretches[j].areaM2;
            const double b = stretches[j + 1].areaM2;
            tubes[static_cast<std::size_t>(j)].reflect = (a - b) / (a + b);
        }
        impedance = soundMs / stretches[0].areaM2;
        clock = 0;
    }

    /** L'onde qui arrive à la lumière à ce pas (Pa). */
    double incoming() const {
        const Tube& t = tubes[0];
        return backward[static_cast<std::size_t>(t.offset + clock % t.length)] * t.keep;
    }

    /** Un pas : ce qui sort de chaque tube se partage aux changements de section et y rentre. `sent` : ce que la lumière envoie. */
    void advance(double sent) {
        for (int j = 0; j < count; ++j) {
            const Tube& t = tubes[static_cast<std::size_t>(j)];
            const std::size_t at = static_cast<std::size_t>(t.offset + clock % t.length);
            leaving[static_cast<std::size_t>(j)] = forward[at] * t.keep;
            returning[static_cast<std::size_t>(j)] = backward[at] * t.keep;
        }
        for (int j = 0; j < count; ++j) {
            const Tube& t = tubes[static_cast<std::size_t>(j)];
            const std::size_t at = static_cast<std::size_t>(t.offset + clock % t.length);
            const double out = leaving[static_cast<std::size_t>(j)];
            if (j + 1 < count) {
                const Tube& next = tubes[static_cast<std::size_t>(j + 1)];
                const std::size_t nextAt = static_cast<std::size_t>(next.offset + clock % next.length);
                const double back = returning[static_cast<std::size_t>(j + 1)];
                forward[nextAt] = (1.0 + t.reflect) * out - t.reflect * back;
                backward[at] = t.reflect * out + (1.0 - t.reflect) * back;
            } else {
                backward[at] = kOpenEndReflection * out;
            }
        }
        forward[static_cast<std::size_t>(tubes[0].offset + clock % tubes[0].length)] = sent;
        ++clock;
    }
};

/**
 * Ce qui vient de sortir par la lumière d'échappement, degré par degré : le dernier sorti est le plus près d'elle, c'est lui que
 * l'onde de retour repousse d'abord dans le cylindre.
 */
struct Header {
    std::array<double, 200> mass{};
    std::array<double, 200> fresh{};
    int top = -1;

    void clear() {
        for (int i = 0; i <= top; ++i) {
            mass[static_cast<std::size_t>(i)] = 0.0;
            fresh[static_cast<std::size_t>(i)] = 0.0;
        }
        top = -1;
    }
    void push(int degree, double dm, double freshDm) {
        const int at = degree < 0 ? 0 : (degree > 199 ? 199 : degree);
        mass[static_cast<std::size_t>(at)] += dm;
        fresh[static_cast<std::size_t>(at)] += freshDm;
        if (at > top) top = at;
    }
    /** Reprend `dm` : renvoie l'air frais qu'il contient. Au-delà de ce qui est sorti, c'est du gaz brûlé de la ligne. */
    double pop(double dm) {
        double freshOut = 0.0;
        while (dm > 0.0 && top >= 0) {
            double& m = mass[static_cast<std::size_t>(top)];
            double& f = fresh[static_cast<std::size_t>(top)];
            const double taken = std::min(dm, m);
            const double share = m > 0.0 ? f / m : 0.0;
            freshOut += taken * share;
            m -= taken;
            f = std::max(f - taken * share, 0.0);
            dm -= taken;
            if (m <= 1e-15) {
                m = 0.0;
                f = 0.0;
                --top;
            }
        }
        return freshOut;
    }
};

/** Un volume de gaz : masse (kg), énergie interne (J), et ce qu'il contient d'air frais (kg). */
struct Gas {
    double mass = 0.0;
    double energy = 0.0;
    double fresh = 0.0;

    double pressure(double volume) const { return (kGamma - 1.0) * energy / volume; }
    double temperature() const { return energy / (mass * kCv); }
    void fill(double volume, double p, double t, double freshShare) {
        mass = p * volume / (kGasR * t);
        energy = mass * kCv * t;
        fresh = mass * freshShare;
    }
    /** Ce qui sort, à la composition de ce qui reste (part d'air frais donnée). */
    void take(double dm, double freshShare) {
        const double t = temperature();
        energy -= dm * kCp * t;
        fresh = std::max(fresh - dm * freshShare, 0.0);
        mass -= dm;
    }
    void give(double dm, double t, double freshShare) {
        energy += dm * kCp * t;
        fresh += dm * freshShare;
        mass += dm;
    }
};

/** Tout ce que le calcul d'un régime laisse au suivant : on repart de là où le précédent s'est arrêté. */
struct CycleState {
    Pipe pipe;
    Header header;
    Gas cylinder;
    Gas crankcase;
    double phi = 0.0;
    double inletSpeed = 0.0;
    bool started = false;
};

/** La géométrie que le calcul lit, en unités du système. */
struct CycleGeometry {
    double swept = 0.0;       // m³, un cylindre
    double clearance = 0.0;   // m³
    double crankcaseMin = 0.0;
    double stroke = 0.0;
    double exhaustWidth = 0.0;  // m
    double transferWidth = 0.0;
    double inletWidth = 0.0;
    double exhaustFrom = 0.0;   // course depuis le PMH où la lumière commence (part)
    double transferFrom = 0.0;
    double inletTravel = 0.0;
    double rodOverStroke = 2.0;
    double exhaustOpenDeg = 0.0;
    double ductArea = 0.0;
    double scavengeDisplacement = kScavengeDisplacement;
    double scavengeShortCircuit = kScavengeShortCircuit;
};

CycleGeometry geometryOf(const EngineConfig& e, const TwoStrokePorts& ports) {
    CycleGeometry g;
    const double bore = static_cast<double>(e.boreMm) / 1000.0;
    g.swept = static_cast<double>(e.displacementCc) * 1e-6 / e.pistons();
    g.stroke = g.swept / (kPi / 4.0 * bore * bore);
    g.clearance = g.swept / (ports.geometricCompression - 1.0);
    g.crankcaseMin = g.swept / (ports.crankcaseCompression - 1.0);
    g.exhaustWidth = ports.exhaustWidth * bore;
    g.transferWidth = ports.transferWidth * bore;
    g.inletWidth = ports.inletWidth * bore;
    g.exhaustFrom = 1.0 - ports.exhaustHeight;
    g.transferFrom = 1.0 - ports.transferHeight;
    g.inletTravel = ports.inletTravel;
    g.rodOverStroke = ports.rodOverStroke;
    g.exhaustOpenDeg = ports.exhaustOpenDeg;
    // La pipe d'admission a la section de la lumière grande ouverte.
    g.ductArea = g.inletWidth * g.inletTravel * g.stroke;
    return g;
}

/** La ligne d'un cylindre : son collecteur puis sa part de la ligne commune, ou sa propre chambre de détente. */
int pipeOf(const EngineConfig& e, LineStretch* out) {
    const int cylinders = e.pistons();
    const double displacement = e.sweptCcPerTwoTurns() * 1e-6;
    const ExpansionChamber chamber = expansionChamberFor(e);
    if (e.exhaust == Exhaust::Expansion) return lineStretches(e.exhaust, 1.0f, displacement, chamber, 1, out, kMaxStretches);
    const TwoStrokePorts ports = twoStrokePortsOf(e);
    const double bore = static_cast<double>(e.boreMm) / 1000.0;
    const double stroke = static_cast<double>(e.displacementCc) * 1e-6 / cylinders / (kPi / 4.0 * bore * bore);
    const double portDiameter = std::sqrt(4.0 * ports.exhaustWidth * bore * ports.exhaustHeight * stroke / kPi);
    const double runner = kHeaderOverPort * portDiameter;
    out[0] = {kRunnerM, kPi * runner * runner / 4.0, 0.05};
    const int n = lineStretches(e.exhaust, 1.0f, displacement, chamber, cylinders, out + 1, kMaxStretches - 1);
    // Chaque cylindre n'a que sa part de la ligne commune : les bouffées des autres passent à d'autres moments.
    for (int i = 1; i <= n; ++i) out[i].areaM2 /= static_cast<double>(cylinders);
    return n + 1;
}

double travel(const CycleGeometry&, double phiDeg) {
    const std::array<double, kTravelPoints + 1>& table = travelTable();
    const double at = phiDeg / 360.0 * kTravelPoints;
    const int low = std::min(std::max(static_cast<int>(at), 0), kTravelPoints - 1);
    const double share = at - low;
    return table[static_cast<std::size_t>(low)] + (table[static_cast<std::size_t>(low + 1)] - table[static_cast<std::size_t>(low)]) * share;
}

/** Ce qu'on retient d'un tour. */
struct Revolution {
    double openPa = 0.0;
    double inducted = 0.0;
    double delivered = 0.0;
    double trappedFresh = 0.0;
    double trappedMass = 0.0;
};

/** Un tour de vilebrequin, d'un point mort haut au suivant, à ce régime. */
Revolution revolution(CycleState& s, const CycleGeometry& g, double rpm) {
    Revolution out;
    const double step = 6.0 * rpm * kStepS;
    const double epc = 360.0 - g.exhaustOpenDeg;
    const double pipeK = kChamberGasK;
    const double x = g.scavengeDisplacement;
    const double y = g.scavengeShortCircuit;
    double scavenged = 0.0;
    double reference = 0.0;
    double xNow = travel(g, s.phi);
    double cylinderV = g.clearance + g.swept * xNow;
    double crankcaseV = g.crankcaseMin + g.swept * (1.0 - xNow);
    bool transferWasOpen = false;
    bool done = false;
    while (!done) {
        const double before = s.phi;
        s.phi += step;
        if (s.phi >= 360.0) {
            s.phi -= 360.0;
            done = true;
        }
        xNow = travel(g, s.phi);
        const double cylinderNew = g.clearance + g.swept * xNow;
        const double crankcaseNew = g.crankcaseMin + g.swept * (1.0 - xNow);
        // Le piston comprime ou détend : le travail des parois mobiles.
        s.cylinder.energy -= s.cylinder.pressure(cylinderV) * (cylinderNew - cylinderV);
        s.crankcase.energy -= s.crankcase.pressure(crankcaseV) * (crankcaseNew - crankcaseV);
        cylinderV = cylinderNew;
        crankcaseV = crankcaseNew;
        // L'explosion, au point mort haut : l'air frais enfermé brûle d'un coup.
        if (done) {
            s.cylinder.energy += s.cylinder.fresh * kHeatPerKgAir;
            s.cylinder.fresh = 0.0;
        }
        // La lumière d'échappement s'ouvre : le balayage repart de zéro.
        if (before < g.exhaustOpenDeg && s.phi >= g.exhaustOpenDeg) {
            out.openPa = s.cylinder.pressure(cylinderV);
            scavenged = 0.0;
            s.header.clear();
        }
        if (before < epc && s.phi >= epc) {
            out.trappedFresh = s.cylinder.fresh;
            out.trappedMass = s.cylinder.mass;
        }
        const double pCyl = s.cylinder.pressure(cylinderV);
        const double tCyl = s.cylinder.temperature();
        const double pCase = s.crankcase.pressure(crankcaseV);
        const double tCase = s.crankcase.temperature();
        const double caseFresh = s.crankcase.mass > 0.0 ? s.crankcase.fresh / s.crankcase.mass : 1.0;

        // ── L'admission : une colonne d'air qui a sa propre inertie, et la jupe du piston qui l'ouvre ──
        const double inletOpen = g.inletTravel - xNow;
        double inletDm = 0.0;
        if (inletOpen > 0.0) {
            const double area = kInletCd * g.inletWidth * inletOpen * g.stroke;
            const double loss = 0.5 * (g.ductArea / area) * (g.ductArea / area);
            s.inletSpeed = (s.inletSpeed + kStepS * (kAtmosphere - pCase) / (kAirDensity * kInletDuctM)) / (1.0 + kStepS * loss * std::fabs(s.inletSpeed) / kInletDuctM);
            const double rho = s.inletSpeed > 0.0 ? kAirDensity : s.crankcase.mass / crankcaseV;
            inletDm = rho * g.ductArea * s.inletSpeed * kStepS;
            if (inletDm < 0.0) inletDm = std::max(inletDm, -0.2 * s.crankcase.mass);
        } else {
            s.inletSpeed = 0.0;
        }

        // ── Les transferts : du carter au cylindre, ou en retour tant que le cylindre est plus haut ──
        const double transferOpen = xNow - g.transferFrom;
        double transferDm = 0.0;
        if (transferOpen > 0.0) {
            if (!transferWasOpen) {
                transferWasOpen = true;
                // Benson et Brandham comptent en volumes, à une seule température : chasser la part `x` du cylindre, c'est chasser la
                // part `x` de son gaz brûlé. Le cylindre n'a ici qu'une température : on compte donc le gaz brûlé chassé, pas l'air
                // entré, qui est froid et dense.
                reference = s.cylinder.mass - s.cylinder.fresh;
                scavenged = 0.0;
            }
            const double area = kTransferCd * g.transferWidth * transferOpen * g.stroke;
            if (pCase > pCyl) {
                const double limit = 0.5 * (pCase - pCyl) * std::min(cylinderV, crankcaseV) / (kGamma * kGasR * tCase);
                transferDm = std::min(area * orificeFlux(pCase, tCase, pCyl) * kStepS, limit);
            } else {
                const double limit = 0.5 * (pCyl - pCase) * std::min(cylinderV, crankcaseV) / (kGamma * kGasR * tCyl);
                transferDm = -std::min(area * orificeFlux(pCyl, tCyl, pCase) * kStepS, limit);
            }
        }

        // ── L'échappement : la lumière, et l'onde qui revient de la ligne ──
        const double exhaustOpen = xNow - g.exhaustFrom;
        const double incoming = s.pipe.incoming();
        double exhaustDm = 0.0;
        double sent = incoming;
        if (exhaustOpen > 0.0) {
            const double area = kExhaustCd * g.exhaustWidth * exhaustOpen * g.stroke;
            const double still = kAtmosphere + 2.0 * incoming;
            const double z = s.pipe.impedance;
            // La pression à la lumière dépend du débit, qui dépend d'elle : on la cherche entre les deux pressions (fausse position,
            // variante d'Illinois, qui garde l'encadrement).
            const auto excess = [&](double port) {
                const double q = pCyl >= port ? area * orificeFlux(pCyl, tCyl, port) : -area * orificeFlux(port, pipeK, pCyl);
                return port - still - z * q;
            };
            double low = std::min(pCyl, still);
            double high = std::max(pCyl, still);
            double fLow = excess(low);
            double fHigh = excess(high);
            double port = low;
            int side = 0;
            for (int i = 0; i < 8 && fHigh - fLow > 0.0; ++i) {
                port = (low * fHigh - high * fLow) / (fHigh - fLow);
                const double f = excess(port);
                if (f > 0.0) {
                    high = port;
                    fHigh = f;
                    if (side == 1) fLow *= 0.5;
                    side = 1;
                } else {
                    low = port;
                    fLow = f;
                    if (side == -1) fHigh *= 0.5;
                    side = -1;
                }
            }
            double flow = (port - still) / z;
            exhaustDm = flow * kStepS;
            if (exhaustDm > 0.0) exhaustDm = std::min(exhaustDm, 0.25 * s.cylinder.mass);
            else exhaustDm = std::max(exhaustDm, -0.5 * (port - pCyl) * cylinderV / (kGamma * kGasR * pipeK));
            sent = incoming + z * exhaustDm / kStepS;
        }
        s.pipe.advance(sent);
#ifdef REVFORGE_TWO_STROKE_TRACE
        if (g_trace != nullptr) std::fprintf(g_trace, "%.2f %.0f %.0f %.0f %.3e %.3e %.3e %.4f\n", s.phi, pCyl, pCase, kAtmosphere + incoming + sent, exhaustDm / kStepS, transferDm / kStepS, inletDm / kStepS, s.cylinder.mass > 0.0 ? s.cylinder.fresh / s.cylinder.mass : 0.0);
#endif

        // ── Les bilans : ce qui entre et sort de chaque volume, et de quoi c'est fait ──
        if (inletDm > 0.0) s.crankcase.give(inletDm, kAmbientK, 1.0);
        else if (inletDm < 0.0) s.crankcase.take(-inletDm, caseFresh);
        out.inducted += inletDm;

        double shortCircuit = 0.0;
        if (transferDm > 0.0) {
            s.crankcase.take(transferDm, caseFresh);
            // Une part du frais file droit vers la lumière d'échappement, si elle est en train de vider.
            if (exhaustDm > 0.0) shortCircuit = std::min(y * transferDm * caseFresh, exhaustDm);
            // Le court-circuit n'entre pas : il ne fait que passer de la lumière de transfert à celle d'échappement.
            const double entering = transferDm - shortCircuit;
            s.cylinder.give(entering, tCase, entering > 0.0 ? (transferDm * caseFresh - shortCircuit) / entering : 0.0);
            out.delivered += transferDm * caseFresh;
        } else if (transferDm < 0.0) {
            const double share = s.cylinder.mass > 0.0 ? s.cylinder.fresh / s.cylinder.mass : 0.0;
            s.cylinder.take(-transferDm, share);
            s.crankcase.give(-transferDm, tCyl, share);
            out.delivered += transferDm * share;
        }

        if (exhaustDm > 0.0) {
            // Déplacement parfait d'abord : le gaz brûlé sort seul ; puis mélange parfait, à la pureté du cylindre (Benson et Brandham).
            const double fromCylinder = exhaustDm - shortCircuit;
            double freshOut = 0.0;
            if (!transferWasOpen || scavenged < x * reference) freshOut = std::max(fromCylinder - (s.cylinder.mass - s.cylinder.fresh), 0.0);
            else freshOut = fromCylinder * s.cylinder.fresh / s.cylinder.mass;
            if (transferWasOpen) scavenged += fromCylinder - freshOut;
            s.cylinder.take(fromCylinder, fromCylinder > 0.0 ? freshOut / fromCylinder : 0.0);
            s.header.push(static_cast<int>(s.phi - g.exhaustOpenDeg), exhaustDm, freshOut + shortCircuit);
        } else if (exhaustDm < 0.0) {
            // L'onde repousse dans le cylindre ce qui venait d'en sortir, le dernier sorti d'abord : ce frais-là est rattrapé.
            const double back = -exhaustDm;
            const double freshBack = s.header.pop(back);
            s.cylinder.give(back, pipeK, freshBack / back);
        }
        if (s.cylinder.energy < 150.0 * kCv * s.cylinder.mass) s.cylinder.energy = 150.0 * kCv * s.cylinder.mass;
        if (s.crankcase.energy < 150.0 * kCv * s.crankcase.mass) s.crankcase.energy = 150.0 * kCv * s.crankcase.mass;
    }
    return out;
}

TwoStrokeCharge chargeOf(const Revolution& r, const CycleGeometry& g) {
    TwoStrokeCharge c;
    const double full = kAirDensity * g.swept;
    c.delivery = r.inducted / full;
    c.charging = r.trappedFresh / full;
    c.scavenging = r.trappedMass > 0.0 ? r.trappedFresh / r.trappedMass : 0.0;
    c.trapping = r.delivered > 0.0 ? clamp(r.trappedFresh / r.delivered, 0.0, 1.0) : 0.0;
    c.openPa = r.openPa;
    return c;
}

void start(CycleState& s, const CycleGeometry& g) {
    const double x = travel(g, 0.0);
    s.cylinder.fill(g.clearance + g.swept * x, kAtmosphere, kAmbientK, 1.0);
    s.crankcase.fill(g.crankcaseMin + g.swept * (1.0 - x), kAtmosphere, kAmbientK, 1.0);
    s.phi = 0.0;
    s.inletSpeed = 0.0;
    s.started = true;
}

void buildPipe(CycleState& s, const EngineConfig& e) {
    std::array<LineStretch, kMaxStretches> stretches{};
    const int n = pipeOf(e, stretches.data());
    s.pipe = Pipe{};
    s.pipe.build(stretches.data(), n, chamberSoundMs());
}

/** Ce qui fait un autre cycle : la mémoire garde une table par moteur, pour quelques moteurs. */
struct TableKey {
    int cylinders = 0;
    int displacementCc = 0;
    int boreMm = 0;
    int redlineRpm = 0;
    Exhaust exhaust = Exhaust::Stock;
    bool operator==(const TableKey&) const = default;
};

struct Table {
    TableKey key;
    bool filled = false;
    std::array<TwoStrokeCharge, kTwoStrokePoints> points{};
};

TableKey keyOf(const EngineConfig& e) { return {e.pistons(), e.displacementCc, e.boreMm, e.redlineRpm, e.exhaust}; }

const Table& tableFor(const EngineConfig& e) {
    static std::array<Table, 4> tables{};
    static int next = 0;
    const TableKey key = keyOf(e);
    for (const Table& t : tables) {
        if (t.filled && t.key == key) return t;
    }
    Table& t = tables[static_cast<std::size_t>(next)];
    next = (next + 1) % static_cast<int>(tables.size());
    t.key = key;
    t.filled = true;
    const TwoStrokePorts ports = twoStrokePortsOf(e);
    const CycleGeometry g = geometryOf(e, ports);
    // Sur la pile, l'état d'un calcul pèse une quarantaine de ko : on le garde avec la table.
    static CycleState state;
    state = CycleState{};
    buildPipe(state, e);
    start(state, g);
    for (int i = 0; i < kTwoStrokePoints; ++i) {
        const double rpm = twoStrokeTableRpm(e, i);
        Revolution last;
        const int turns = i == 0 ? kWarmRevolutions : kRevolutions;
        for (int r = 0; r < turns; ++r) last = revolution(state, g, rpm);
        t.points[static_cast<std::size_t>(i)] = chargeOf(last, g);
    }
    return t;
}

/** La chaîne des ratés, de la pureté après une explosion. */
double purityAfter(double scavenging, int misfires) {
    double purity = scavenging;
    for (int k = 0; k < misfires; ++k) purity = 1.0 - (1.0 - scavenging) * (1.0 - purity);
    return purity;
}

}  // namespace

double pistonTravelShare(double crankDeg, double rodOverStroke) {
    const double a = crankDeg * kPi / 180.0;
    const double s = portable::sin(a);
    const double c = portable::cos(a);
    const double lever = 1.0 / (2.0 * rodOverStroke);
    return 0.5 * (1.0 - c) + rodOverStroke * (1.0 - std::sqrt(1.0 - lever * lever * s * s));
}

double crankAngleForTravel(double share, double rodOverStroke) {
    double low = 0.0, high = 180.0;
    for (int i = 0; i < 48; ++i) {
        const double middle = 0.5 * (low + high);
        (pistonTravelShare(middle, rodOverStroke) < share ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

TwoStrokePorts twoStrokePortsOf(const EngineConfig& e) {
    TwoStrokePorts p;
    const double k = pushedShare(e);
    p.rodOverStroke = kRodOverStroke;
    p.exhaustHeight = kExhaustHeightTouring + (kExhaustHeightRacing - kExhaustHeightTouring) * k;
    p.transferHeight = kTransferHeightTouring + (kTransferHeightRacing - kTransferHeightTouring) * k;
    p.inletTravel = kInletTravelTouring + (kInletTravelRacing - kInletTravelTouring) * k;
    p.exhaustWidth = kExhaustWidth;
    p.transferWidth = kTransferWidth;
    p.inletWidth = kInletWidth;
    p.exhaustOpenDeg = crankAngleForTravel(1.0 - p.exhaustHeight, p.rodOverStroke);
    p.transferOpenDeg = crankAngleForTravel(1.0 - p.transferHeight, p.rodOverStroke);
    p.inletOpenBtdcDeg = crankAngleForTravel(p.inletTravel, p.rodOverStroke);
    p.geometricCompression = kGeometricCompression;
    // Le cylindre ne comprime qu'une fois la lumière d'échappement fermée.
    p.trappedCompression = 1.0 + (1.0 - p.exhaustHeight) * (p.geometricCompression - 1.0);
    p.crankcaseCompression = kCrankcaseCompression;
    return p;
}

double chamberSoundMs() { return std::sqrt(kGamma * kGasR * kChamberGasK); }

ExpansionChamber expansionChamberFor(const EngineConfig& e) {
    ExpansionChamber c;
    const double bore = static_cast<double>(e.boreMm) / 1000.0;
    const int cylinders = std::max(e.twoStroke() ? e.pistons() : e.cylinders, 1);
    const double stroke = static_cast<double>(e.displacementCc) * 1e-6 / cylinders / (kPi / 4.0 * bore * bore);
    double portArea = kValveAreaShare * kPi / 4.0 * bore * bore;
    // Le trajet de l'onde, en degrés : de la bouffée à son retour. Sur un quatre-temps, de 20° après l'ouverture à 30° avant la fermeture.
    double travelDeg = kValveOpenDeg - kValvePulseDeg - kValveReturnDeg;
    if (e.twoStroke()) {
        const TwoStrokePorts ports = twoStrokePortsOf(e);
        portArea = ports.exhaustWidth * bore * ports.exhaustHeight * stroke;
        // La bouffée culmine quand le transfert s'ouvre : c'est pour cela qu'il s'ouvre là, la pression du cylindre tombée à celle du
        // carter. Le bouchon doit revenir tant que la lumière d'échappement est encore à moitié ouverte, sans quoi il ne repousse rien.
        const double halfOpen = 360.0 - crankAngleForTravel(1.0 - 0.5 * ports.exhaustHeight, ports.rodOverStroke);
        travelDeg = halfOpen - ports.transferOpenDeg;
    }
    const double header = kHeaderOverPort * std::sqrt(4.0 * portArea / kPi);
    const double sound = chamberSoundMs();
    const double tuned = kTunedShare * static_cast<double>(e.redlineRpm);
    // L'aller et retour jusqu'au milieu du cône de retour dure ce trajet au régime visé : 2 L = c · trajet / (6 N).
    const double length = sound * travelDeg / (12.0 * tuned);
    c.headerM = static_cast<float>(kHeaderShare * length);
    c.diffuserM = static_cast<float>(kDiffuserShare * length);
    c.bellyM = static_cast<float>(kBellyShare * length);
    c.baffleM = static_cast<float>(kBaffleShare * length);
    c.headerDiameterM = static_cast<float>(header);
    c.bellyDiameterM = static_cast<float>(kBellyOverHeader * header);
    c.stingerDiameterM = static_cast<float>(kStingerOverHeader * header);
    c.stingerM = static_cast<float>(kStingerLengthOverDiameter * kStingerOverHeader * header);
    c.soundMs = static_cast<float>(sound);
    c.tunedRpm = static_cast<float>(tuned);
    return c;
}

double twoStrokeTableRpm(const EngineConfig& e, int point) {
    return static_cast<double>(e.redlineRpm) * (kTableFrom + (kTableTo - kTableFrom) * static_cast<double>(point) / (kTwoStrokePoints - 1));
}

TwoStrokeCharge twoStrokeFullLoad(const EngineConfig& e, double rpm) {
    const Table& t = tableFor(e);
    const double redline = static_cast<double>(e.redlineRpm);
    const double at = clamp((rpm / redline - kTableFrom) / (kTableTo - kTableFrom), 0.0, 1.0) * (kTwoStrokePoints - 1);
    const int low = std::min(static_cast<int>(at), kTwoStrokePoints - 2);
    const double share = at - low;
    const TwoStrokeCharge& a = t.points[static_cast<std::size_t>(low)];
    const TwoStrokeCharge& b = t.points[static_cast<std::size_t>(low + 1)];
    TwoStrokeCharge out;
    out.delivery = a.delivery + (b.delivery - a.delivery) * share;
    out.trapping = a.trapping + (b.trapping - a.trapping) * share;
    out.scavenging = a.scavenging + (b.scavenging - a.scavenging) * share;
    out.charging = a.charging + (b.charging - a.charging) * share;
    out.openPa = a.openPa + (b.openPa - a.openPa) * share;
    return out;
}

TwoStrokeCharge twoStrokeCycle(const EngineConfig& e, double rpm) {
    const TwoStrokePorts ports = twoStrokePortsOf(e);
    const CycleGeometry g = geometryOf(e, ports);
    static CycleState state;
    state = CycleState{};
    buildPipe(state, e);
    start(state, g);
    Revolution last;
    for (int r = 0; r < kWarmRevolutions + kRevolutions; ++r) last = revolution(state, g, rpm);
    return chargeOf(last, g);
}

double scavengingEfficiency(double scavengeRatio, double displacement, double shortCircuit) {
    const double entered = (1.0 - shortCircuit) * std::max(scavengeRatio, 0.0);
    if (entered <= displacement) return entered;
    return 1.0 - (1.0 - displacement) * portable::exp(displacement - entered);
}

double ignitionChance(double purity) {
    // Une dispersion triangulaire : la somme de deux tirages uniformes, comme dans le son.
    const double u = (purity - kPurityMin) / kPuritySpread;
    if (u <= -1.0) return 0.0;
    if (u >= 1.0) return 1.0;
    return u < 0.0 ? 0.5 * (1.0 + u) * (1.0 + u) : 1.0 - 0.5 * (1.0 - u) * (1.0 - u);
}

TwoStrokeFiring twoStrokeFiring(double scavenging) {
    TwoStrokeFiring out;
    // La chaîne : après k ratés de suite, la charge a la pureté `purityAfter(·, k)`. Elle part avec la chance `ignitionChance`.
    double weight = 1.0, total = 0.0, fired = 0.0, burnt = 0.0;
    for (int k = 0; k < 24 && weight > 1e-9; ++k) {
        const double purity = purityAfter(scavenging, k);
        const double chance = ignitionChance(purity);
        total += weight;
        fired += weight * chance;
        burnt += weight * chance * purity;
        weight *= 1.0 - chance;
    }
    if (total <= 0.0 || scavenging <= 0.0) return out;
    out.firing = fired / total;
    // Chaque tour, le balayage enferme autant d'air frais ; ce qui ne brûle pas repart avec le balayage suivant.
    out.burnt = clamp(burnt / total / scavenging, 0.0, 1.0);
    return out;
}

TwoStrokePartLoad twoStrokePartLoad(const EngineConfig& e, double rpm, double fill) {
    TwoStrokePartLoad out;
    const TwoStrokeCharge full = twoStrokeFullLoad(e, rpm);
    const double share = clamp(fill, 0.0, 1.0);
    // À charge partielle, la même boucle de balayage, moins de débit : le rapport des deux modèles de Benson et Brandham garde le calcul
    // de pleine charge à pleine charge.
    const double fullModel = scavengingEfficiency(full.delivery, kScavengeDisplacement, kScavengeShortCircuit);
    const double partModel = scavengingEfficiency(full.delivery * share, kScavengeDisplacement, kScavengeShortCircuit);
    const double ratio = fullModel > 0.0 ? partModel / fullModel : 0.0;
    out.scavenging = clamp(full.scavenging * ratio, 0.0, 1.0);
    const TwoStrokeFiring firing = twoStrokeFiring(out.scavenging);
    out.firing = firing.firing;
    out.burnt = firing.burnt;
    // Le couple suit l'air frais enfermé qui brûle, rapporté à celui de pleine charge (où tout brûle).
    const double fullFiring = twoStrokeFiring(full.scavenging).burnt;
    out.torqueShare = share > 0.0 ? ratio * firing.burnt / std::max(fullFiring, 1e-9) / share : 0.0;
    // Ce qui est piégé, sur ce qui passe : meilleur à faible débit, le court-circuit seul reste.
    out.trapping = share > 0.0 ? clamp(full.trapping * ratio / share, 0.05, 1.0) : 1.0;
    return out;
}

double twoStrokeReleaseShare(const EngineConfig& e) {
    // Le quatre-temps de référence, avec les mêmes chaleurs : 1 m³ balayé, l'air à 330 K avec ses gaz résiduels.
    constexpr double kVolumetric = 0.92;
    constexpr double kChargeK = 330.0;
    constexpr double kOpenedShare = 0.821;
    const double clearance = 1.0 / (kFourStrokeCompression - 1.0);
    const double air = kAirDensity * kVolumetric;
    const double gas = air * (1.0 + 1.0 / kStoichiometric);
    const double compressed = gas * kGasR * kChargeK / (1.0 + clearance) * portable::pow(kFourStrokeCompression, kGamma);
    const double hot = kChargeK * portable::pow(kFourStrokeCompression, kGamma - 1.0);
    const double burnt = compressed * (1.0 + air * kHeatPerKgAir / (gas * kCv * hot));
    const double reference = burnt * portable::pow(clearance / (clearance + kOpenedShare), kGamma);
    return twoStrokeFullLoad(e, kTunedShare * static_cast<double>(e.redlineRpm)).openPa / reference;
}

double twoStrokeEfficiencyShare(const EngineConfig& e) {
    const TwoStrokePorts ports = twoStrokePortsOf(e);
    return ottoEfficiency(ports.trappedCompression) / ottoEfficiency(kFourStrokeCompression);
}

double twoStrokeIndicatedBar(const EngineConfig& e, double rpm) {
    const TwoStrokeCharge charge = twoStrokeFullLoad(e, rpm);
    const double efficiency = kFourStrokeIndicated * twoStrokeEfficiencyShare(e);
    return efficiency * charge.charging * kReferenceDensity * kLowerHeatingJPerKg / kStoichiometric / 1e5;
}

bool coherentTwoStroke(EngineConfig& e) {
    if (!e.twoStroke()) return false;
    const EngineConfig before = e;
    // Balayé par son carter : ni turbo seul (rien ne balaierait au démarrage), ni compresseur (voir docs/modele.md), ni diesel.
    e.aspiration = Aspiration::Natural;
    e.fuel = Fuel::Petrol;
    if (e.layout == Layout::CrossPlane) e.layout = Layout::V;
    if (e.cylinders > e.pistons()) e.cylinders = e.pistons();
    return !(before == e);
}

}  // namespace revforge
