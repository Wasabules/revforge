#include "revforge/cam.hpp"

#include <array>
#include <cmath>

#include "portable_math.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/valvetrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kDegToRad = kPi / 180.0;

// ── Le gaz : un seul γ pour l'air et le gaz brûlé, l'air ambiant de référence (20 °C, 101 325 Pa) ──
constexpr double kGamma = 1.35;
constexpr double kGasR = 287.0;
constexpr double kCv = kGasR / (kGamma - 1.0);
constexpr double kCp = kGamma * kCv;
constexpr double kAmbientPa = 101325.0;
constexpr double kAmbientK = 293.0;
constexpr double kSoundMs = 343.0;

// ── Les soupapes : les mêmes que `valvetrain.cpp` ──
constexpr double kLiftPerBore = 0.12;
constexpr double kThroatPerHead = 0.636;
constexpr double kDischarge = 0.7;
constexpr double kRodToCrank = 3.2;
// Le bossage, siège à siège, ne doit pas atteindre le point mort haut d'allumage : l'admission ferme avant 360 + axe + 210 < 720
// (axe de 140° au plus, déphaseur compris), l'échappement ouvre après 360 − axe − 210 > 0. Sous 0,25 mm de jeu, la soupape ne
// décolle pas : ce qu'elle tient vraiment reste plus court.
constexpr double kHalfSpanMaxDeg = 210.0;

// ── Le cycle : la combustion au point mort haut, d'un coup. Le rendement ne compte pas : on compare deux cames. ──
// 2,9 MJ par kg d'air à la stœchiométrie, dont 75 % restent au gaz (supposé) : 5 bar à l'ouverture, pleine charge, comme
// `tools/valve_filling.py`.
constexpr double kHeatPerAir = 2.9e6 * 0.75;
// Le conduit d'admission et le tube primaire : coefficients de perte (supposés, de l'ordre d'une entrée et d'un coude).
constexpr double kRunnerLoss = 1.0;
constexpr double kHeaderLoss = 1.0;
// Aucun des deux ne renvoie toute son onde : une part rayonne dans le plénum, une part part dans la ligne. Résistance en
// part de ρc, supposée. Sans elle, le conduit sonne comme une cloche et le remplissage ondule de ±5 % d'un régime à l'autre.
constexpr double kRunnerDamping = 0.15;
constexpr double kHeaderDamping = 0.25;
// Sa longueur : celle d'un quatre cylindres en ligne dans `toProfile`.
constexpr double kHeaderM = 0.62;
// Les volumes entre la soupape et sa colonne : le conduit coulé dans la culasse, et le début du tube primaire (supposés).
// Le conduit est le col d'un résonateur dont le cylindre est le volume (Engelman) : ces volumes restent petits.
constexpr double kPortShare = 0.15;
constexpr double kHeaderShare = 0.3;
// Le jeu aux soupapes (m) : la came doit le rattraper avant que la soupape décolle (supposé, jeu mécanique courant).
constexpr double kLashM = 0.25e-3;
// La contre-pression moyenne de la ligne, en carré du débit : 8 kPa au régime nominal (supposé, ligne d'origine).
constexpr double kBackPressureRatedPa = 8000.0;
constexpr double kRatedShare = 0.88;
// Le gaz qui revient de la ligne dans le tube (K).
constexpr double kLineBackK = 800.0;
// L'accord du conduit : sa résonance de Helmholtz avec le cylindre vaut 2,5 fois la fréquence de rotation au régime visé
// (voir docs/modele.md). Il est accordé pour la came d'origine, au pic de couple du modèle de base.
constexpr double kTuningIndex = 2.5;
constexpr int kCycles = 3;

// ── Les tables ──
constexpr double kFirstRpm = 600.0;
constexpr double kLastShare = 1.25;
// Le déphaseur : trois calages (tout retardé, de base, tout avancé), puis le sommet de la parabole qui passe par eux.
constexpr int kPhases = 3;
constexpr double kPhaseStepDeg = kCamPhaserAuthorityDeg;
// Au-dessous de ce régime, le pas du calcul passe au demi-degré : le cycle dure plus longtemps, les écoulements aussi.
constexpr double kFineBelowRpm = 2000.0;

// ── Le ralenti ──
// Au ralenti, l'avance est retardée pour garder une réserve de couple : la chaleur utile y est plus faible. Cette part est
// choisie pour qu'un 2 L d'origine tienne son ralenti vers 39 kPa, soit 18 inHg de dépression (valeur d'atelier courante :
// de 17 à 21).
constexpr double kIdleHeatShare = 0.5;
// La consommation des accessoires, en pression moyenne (bar, supposé) : alternateur, pompe à eau, direction.
constexpr double kIdleAccessoryBar = 0.3;

// ── La dispersion des combustions ──
constexpr double kDilutionSlope = 2.06;
constexpr double kDilutionPower = 0.77;
// COV = 1,04 % × (S_L0/S_L)^1,62 : 3 à 4 % à un ralenti d'origine, 10 % (la limite de conduite) vers 27 % de gaz brûlés.
constexpr double kCovUndiluted = 0.0104;
constexpr double kCovPower = 1.62;
constexpr double kFlameFloor = 0.03;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** La part du cylindre au-dessus du volume mort, à cet angle depuis le point mort haut (0 à 1). Table au demi-degré. */
const std::array<double, 1441>& strokeShareTable() {
    static const std::array<double, 1441> table = [] {
        std::array<double, 1441> t{};
        for (int i = 0; i <= 1440; ++i) {
            const double a = static_cast<double>(i) * 0.5 * kDegToRad;
            const double s = portable::sin(a);
            t[static_cast<std::size_t>(i)] = 0.5 * (1.0 - portable::cos(a) + kRodToCrank - std::sqrt(kRodToCrank * kRodToCrank - s * s));
        }
        return t;
    }();
    return table;
}

// Débit d'un orifice compressible, en part de A·p/√(RT) : Ψ, tabulé selon √(1 − p_aval/p_amont), où il est lisse.
constexpr int kPsiPoints = 512;
const double kCriticalRatio = portable::pow(2.0 / (kGamma + 1.0), kGamma / (kGamma - 1.0));
const double kChokedPsi = std::sqrt(kGamma) * portable::pow(2.0 / (kGamma + 1.0), (kGamma + 1.0) / (2.0 * (kGamma - 1.0)));
const double kPsiSpan = std::sqrt(1.0 - kCriticalRatio);

const std::array<double, kPsiPoints + 1>& psiTable() {
    static const std::array<double, kPsiPoints + 1> table = [] {
        std::array<double, kPsiPoints + 1> t{};
        for (int i = 0; i <= kPsiPoints; ++i) {
            const double s = kPsiSpan * static_cast<double>(i) / kPsiPoints;
            const double r = 1.0 - s * s;
            const double inner = portable::pow(r, 2.0 / kGamma) - portable::pow(r, (kGamma + 1.0) / kGamma);
            t[static_cast<std::size_t>(i)] = std::sqrt(std::fmax(2.0 * kGamma / (kGamma - 1.0) * inner, 0.0));
        }
        return t;
    }();
    return table;
}

double psi(double ratio) {
    if (!(ratio < 1.0)) return 0.0;
    if (!(ratio > kCriticalRatio)) return kChokedPsi;
    const double position = std::sqrt(1.0 - ratio) / kPsiSpan * kPsiPoints;
    const int index = position >= kPsiPoints ? kPsiPoints - 1 : static_cast<int>(position);
    const auto& t = psiTable();
    const double a = t[static_cast<std::size_t>(index)];
    return a + (t[static_cast<std::size_t>(index + 1)] - a) * (position - static_cast<double>(index));
}

/** Section efficace de `count` soupapes de tête `headM`, levées de `liftM` : la jupe et la gorge en série. */
double valveArea(double headM, int count, double liftM) {
    if (liftM <= 0.0) return 0.0;
    const double curtain = kPi * headM * liftM;
    const double throat = kThroatPerHead * headM * headM;
    return static_cast<double>(count) * kDischarge / std::sqrt(1.0 / (curtain * curtain) + 1.0 / (throat * throat));
}

/** Le x où la levée vaut cette part de la levée maximale, entre 0 et 1. */
double shareAt(double share) {
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (camLiftShare(middle) > share ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

/** Le moteur tel que le voit le calcul du remplissage. */
struct Geometry {
    double vd = 0.0, vc = 0.0;
    int intakes = 2, exhausts = 2;
    double intakeM = 0.0, exhaustM = 0.0;
    double runnerArea = 0.0, runnerM = 0.0;
    double headerArea = 0.0;
    double ratedRpm = 6000.0;
};

double compressionOf(const EngineConfig& e) {
    if (e.compressionRatio > 0.0f) return static_cast<double>(e.compressionRatio);
    if (e.burnsDiesel()) return 18.0;
    return e.aspiration != Aspiration::Natural ? 9.5 : 10.5;
}

Geometry geometryOf(const EngineConfig& e) {
    Geometry g;
    const double bore = static_cast<double>(e.boreMm) / 1000.0;
    const double stroke = static_cast<double>(e.strokeMm()) / 1000.0;
    g.vd = kPi / 4.0 * bore * bore * stroke;
    g.vc = g.vd / (compressionOf(e) - 1.0);
    const ValveTrainGeometry v = valveTrainOf(e.valvetrain(), static_cast<double>(e.boreMm));
    g.intakes = v.perCylinder / 2;
    g.exhausts = v.perCylinder / 2;
    g.intakeM = v.intakeMm / 1000.0;
    g.exhaustM = v.exhaustMm / 1000.0;
    g.runnerArea = static_cast<double>(g.intakes) * kThroatPerHead * g.intakeM * g.intakeM;
    g.headerArea = static_cast<double>(g.exhausts) * kThroatPerHead * g.exhaustM * g.exhaustM;
    g.ratedRpm = kRatedShare * static_cast<double>(e.redlineRpm);
    // Helmholtz : f = c/2π · √(A / (L·V)), V le cylindre à mi-course. Accordé là où le modèle de base met son pic de couple.
    const double tunedRpm = (e.aspiration == Aspiration::Natural ? 0.62 : 0.6) * static_cast<double>(e.redlineRpm);
    const double midVolume = g.vd / 2.0 * (compressionOf(e) + 1.0) / (compressionOf(e) - 1.0);
    const double reach = kSoundMs / (2.0 * kPi * kTuningIndex * tunedRpm / 60.0);
    g.runnerM = clamp(g.runnerArea / midVolume * reach * reach, 0.1, 1.2);
    return g;
}

/** Ce que donne un cycle établi. */
struct Cycle {
    double imepBar = 0.0;
    double fill = 0.0;
    double residual = 0.0;
    double openPa = 0.0;
};

/** Ce qui passe de 1 vers 2 pendant dt (négatif : de 2 vers 1), jamais au-delà de l'égalité des pressions. */
double exchange(double area, double p1, double t1, double v1, double p2, double t2, double v2, double dt) {
    if (area <= 0.0 || p1 == p2) return 0.0;
    const double share = 1.0 / (1.0 / v1 + 1.0 / v2);
    if (p1 > p2) {
        const double dm = area * p1 / std::sqrt(kGasR * t1) * psi(p2 / p1) * dt;
        return std::fmin(dm, (p1 - p2) / (kGamma * kGasR * t1) * share);
    }
    const double dm = area * p2 / std::sqrt(kGasR * t2) * psi(p1 / p2) * dt;
    return -std::fmin(dm, (p2 - p1) / (kGamma * kGasR * t2) * share);
}

/** L'état à la fin d'un cycle : le point de départ du régime suivant, déjà presque établi. */
struct Warm {
    bool valid = false;
    double m = 0.0, t = 0.0, y = 0.0, mr = 0.0, tr = 0.0, ur = 0.0, slug = 0.0, te = 0.0, me = 0.0, ue = 0.0;
};

/**
 * Un cylindre à zéro dimension, son conduit d'admission (un volume et sa colonne d'air, qui a de l'inertie) et son tube
 * primaire (de même), sur trois cycles depuis un état grossier, ou deux depuis celui du régime voisin (`warm`). Ce qui reflue
 * dans le conduit y reste en bouchon contre la soupape, et revient le premier.
 */
Cycle runCycle(const Geometry& g, const CamProfile& cam, double rpm, double plenumPa, double backPa, double heatShare, Warm* warm = nullptr) {
    const auto& stroke = strokeShareTable();
    const int half = rpm < kFineBelowRpm ? 1 : 2;
    const int steps = 1440 / half;
    const double stepDeg = 0.5 * static_cast<double>(half);
    const double dt = stepDeg / (6.0 * rpm);
    const double liftIn = cam.liftMm / 1000.0;
    const double inCenter = 360.0 + cam.intakeCenterDeg;
    const double exCenter = 360.0 - cam.exhaustCenterDeg;
    const double span = cam.halfSpanDeg;

    double v = g.vc;
    double m = plenumPa * v / (kGasR * 1200.0);
    double t = 1200.0;
    double y = 1.0;
    const double portV = kPortShare * g.vd;
    const double rho0 = plenumPa / (kGasR * kAmbientK);
    double mr = rho0 * portV, tr = kAmbientK, ur = 0.0, slug = 0.0;
    const double headerV = kHeaderShare * g.vd;
    double te = 900.0;
    double me = backPa * headerV / (kGasR * te);
    double ue = 0.0;
    const bool started = warm != nullptr && warm->valid;
    if (started) {
        m = warm->m;
        t = warm->t;
        y = warm->y;
        mr = warm->mr;
        tr = warm->tr;
        ur = warm->ur;
        slug = warm->slug;
        te = warm->te;
        me = warm->me;
        ue = warm->ue;
    }
    const int cycles = started ? kCycles - 1 : kCycles;
    Cycle out;
    double work = 0.0;
    for (int k = 0; k < cycles; ++k) {
        const bool last = k == cycles - 1;
        for (int i = 0; i < steps; ++i) {
            const double deg = static_cast<double>(i) * stepDeg;
            if (i == 0) {
                // L'étincelle : la charge fraîche brûle d'un coup.
                const double air = m * (1.0 - y);
                t += air * kHeatPerAir * heatShare / (m * kCv);
                y = 1.0;
            }
            const double vn = g.vc + g.vd * stroke[static_cast<std::size_t>(((i + 1) * half) % 1440)];
            const double p = m * kGasR * t / v;
            const double at = deg + 0.5 * stepDeg;
            const double ai = valveArea(g.intakeM, g.intakes, liftIn * camLiftShare((at - inCenter) / span) - kLashM);
            const double ae = valveArea(g.exhaustM, g.exhausts, liftIn * camLiftShare((at - exCenter) / span) - kLashM);
            const double pr = mr * kGasR * tr / portV;
            const double pe = me * kGasR * te / headerV;
            const double dmi = exchange(ai, pr, tr, portV, p, t, v, dt);
            const double dme = exchange(ae, p, t, v, pe, te, headerV, dt);
            double yi = y;
            if (dmi > 0.0) {
                const double back = std::fmin(dmi, slug);
                slug -= back;
                yi = back / dmi;
            } else if (dmi < 0.0) {
                slug = std::fmin(slug - dmi * y, mr);
            }
            const double ti = dmi > 0.0 ? tr : t;
            const double yx = dme > 0.0 ? y : 1.0;
            const double tx = dme > 0.0 ? t : te;
            if (last) work += p * (vn - v);
            const double energy = m * kCv * t + dmi * kCp * ti - dme * kCp * tx - p * (vn - v);
            const double burnt = m * y + dmi * yi - dme * yx;
            m += dmi - dme;
            y = clamp(burnt / m, 0.0, 1.0);
            t = energy / (m * kCv);
            v = vn;
            if (last && out.openPa == 0.0 && deg + stepDeg > cam.evoDeg && deg <= cam.evoDeg) out.openPa = m * kGasR * t / v;

            if (!(m > 1e-9) || !(t > 150.0)) {
                // Garde-fou : un état aberrant repart de l'air du plénum.
                m = std::fmax(plenumPa, 1e4) * v / (kGasR * kAmbientK);
                t = kAmbientK;
            }
            t = std::fmin(t, 4000.0);

            // Le conduit : la colonne apporte de l'air du plénum, la soupape le prend.
            const double column = std::fmax(rho0 * g.runnerArea * ur * dt, -0.5 * (mr - dmi));
            const double portEnergy = mr * kCv * tr + column * kCp * (column > 0.0 ? kAmbientK : tr) - dmi * kCp * ti;
            mr += column - dmi;
            tr = portEnergy / (mr * kCv);
            const double prNext = mr * kGasR * tr / portV;
            ur += dt / (rho0 * g.runnerM) * (plenumPa - prNext - 0.5 * rho0 * kRunnerLoss * ur * std::fabs(ur) - kRunnerDamping * rho0 * kSoundMs * ur);

            // Le tube primaire : la soupape apporte, la colonne emporte vers la ligne.
            const double rhoE = pe / (kGasR * te);
            const double out_ = std::fmin(rhoE * g.headerArea * ue * dt, 0.5 * (me + dme));
            const double headerEnergy = me * kCv * te + dme * kCp * tx - out_ * kCp * (out_ > 0.0 ? te : kLineBackK);
            me += dme - out_;
            te = headerEnergy / (me * kCv);
            const double peNext = me * kGasR * te / headerV;
            const double rhoNext = peNext / (kGasR * te);
            ue += dt / (rhoNext * kHeaderM) *
                  (peNext - backPa - 0.5 * rhoNext * kHeaderLoss * ue * std::fabs(ue) - kHeaderDamping * rhoNext * std::sqrt(kGamma * kGasR * te) * ue);
        }
    }
    if (warm != nullptr) *warm = Warm{true, m, t, y, mr, tr, ur, slug, te, me, ue};
    out.fill = m * (1.0 - y) / (kAmbientPa / (kGasR * kAmbientK) * g.vd);
    out.residual = y;
    out.imepBar = work / g.vd / 1e5;
    return out;
}

double backPressurePa(const Geometry& g, double rpm) {
    const double share = rpm / g.ratedRpm;
    return kAmbientPa + kBackPressureRatedPa * share * share;
}

/** Les cycles d'un profil, régime par régime, et le meilleur calage du déphaseur s'il y en a un. */
struct Curve {
    std::array<double, kCamPoints> imep{};
    std::array<double, kCamPoints> fill{};
    std::array<double, kCamPoints> open{};
    std::array<double, kCamPoints> phase{};
};

struct Tables {
    bool valid = false;
    // La clé : tout ce qui change le calcul.
    int boreMm = 0, displacementCc = 0, cylinders = 0, redlineRpm = 0;
    Camshaft valvetrain = Camshaft::Dohc;
    Aspiration aspiration = Aspiration::Natural;
    bool diesel = false;
    float compressionRatio = 0.0f;
    int camDurationDeg = 0;
    float camLiftMm = 0.0f;
    int lobeSeparationDeg = 0;
    ValveTiming valveTiming = ValveTiming::Fixed;

    double firstRpm = kFirstRpm;
    double stepRpm = 100.0;
    Curve stock, low, high;
    double switchRpm = 0.0;
    // Le ralenti, calculé à la première demande : il faut connaître son régime, que donne le profil du son.
    bool idleKnown = false;
    CamIdle idleStock, idleLow, idleHigh;
    unsigned age = 0;

    bool matches(const EngineConfig& e) const {
        return valid && boreMm == e.boreMm && displacementCc == e.displacementCc && cylinders == e.cylinders && redlineRpm == e.redlineRpm &&
               valvetrain == e.valvetrain() && aspiration == e.aspiration && diesel == e.burnsDiesel() && compressionRatio == e.compressionRatio &&
               camDurationDeg == e.camDurationDeg && camLiftMm == e.camLiftMm && lobeSeparationDeg == e.lobeSeparationDeg && valveTiming == e.valveTiming;
    }
};

double rpmAt(const Tables& t, int i) { return t.firstRpm + t.stepRpm * static_cast<double>(i); }

void fillCurve(const Geometry& g, const Tables& t, const CamProfile& cam, bool phased, Curve& c) {
    // Chaque calage part de l'état que lui a laissé le régime précédent.
    std::array<Warm, kPhases> warm{};
    for (int i = 0; i < kCamPoints; ++i) {
        const double rpm = rpmAt(t, i);
        const double back = backPressurePa(g, rpm);
        const std::size_t at = static_cast<std::size_t>(i);
        if (!phased) {
            const Cycle cycle = runCycle(g, cam, rpm, kAmbientPa, back, 1.0, &warm[0]);
            c.imep[at] = cycle.imepBar;
            c.fill[at] = cycle.fill;
            c.open[at] = cycle.openPa;
            continue;
        }
        // Le déphaseur : cinq calages, puis le sommet d'une parabole par les trois meilleurs.
        std::array<Cycle, kPhases> tried{};
        int best = 0;
        for (int k = 0; k < kPhases; ++k) {
            tried[static_cast<std::size_t>(k)] = runCycle(g, advancedIntake(cam, kPhaseStepDeg * static_cast<double>(k - kPhases / 2)), rpm, kAmbientPa, back, 1.0, &warm[static_cast<std::size_t>(k)]);
            if (tried[static_cast<std::size_t>(k)].imepBar > tried[static_cast<std::size_t>(best)].imepBar) best = k;
        }
        const int middle = best == 0 ? 1 : (best == kPhases - 1 ? kPhases - 2 : best);
        const Cycle& a = tried[static_cast<std::size_t>(middle - 1)];
        const Cycle& b = tried[static_cast<std::size_t>(middle)];
        const Cycle& d = tried[static_cast<std::size_t>(middle + 1)];
        const double curvature = a.imepBar - 2.0 * b.imepBar + d.imepBar;
        double offset = curvature < 0.0 ? clamp(0.5 * (a.imepBar - d.imepBar) / curvature, -1.0, 1.0) : static_cast<double>(best - middle);
        const auto through = [offset](double left, double centre, double right) {
            return centre + 0.5 * offset * (right - left) + 0.5 * offset * offset * (left - 2.0 * centre + right);
        };
        c.imep[at] = through(a.imepBar, b.imepBar, d.imepBar);
        c.fill[at] = through(a.fill, b.fill, d.fill);
        c.open[at] = through(a.openPa, b.openPa, d.openPa);
        c.phase[at] = kPhaseStepDeg * (static_cast<double>(middle - kPhases / 2) + offset);
    }
}

bool sameCam(const CamProfile& a, const CamProfile& b) {
    return a.durationDeg == b.durationDeg && a.liftMm == b.liftMm && a.lobeSeparationDeg == b.lobeSeparationDeg && a.intakeCenterDeg == b.intakeCenterDeg;
}

// Quelques moteurs gardés : le site et les tests passent de l'un à l'autre, et chacun coûte quelques dizaines de millisecondes.
constexpr int kKept = 4;
std::array<Tables, kKept> gKept{};
unsigned gAge = 0;

const Tables& tablesOf(const EngineConfig& e) {
    for (Tables& t : gKept) {
        if (t.matches(e)) {
            t.age = ++gAge;
            return t;
        }
    }
    Tables* slot = &gKept[0];
    for (Tables& t : gKept) {
        if (!t.valid) {
            slot = &t;
            break;
        }
        if (t.age < slot->age) slot = &t;
    }
    Tables& t = *slot;
    t.valid = true;
    t.boreMm = e.boreMm;
    t.displacementCc = e.displacementCc;
    t.cylinders = e.cylinders;
    t.redlineRpm = e.redlineRpm;
    t.valvetrain = e.valvetrain();
    t.aspiration = e.aspiration;
    t.diesel = e.burnsDiesel();
    t.compressionRatio = e.compressionRatio;
    t.camDurationDeg = e.camDurationDeg;
    t.camLiftMm = e.camLiftMm;
    t.lobeSeparationDeg = e.lobeSeparationDeg;
    t.valveTiming = e.valveTiming;
    t.age = ++gAge;
    t.idleKnown = false;
    t.firstRpm = kFirstRpm;
    t.stepRpm = (kLastShare * static_cast<double>(e.redlineRpm) - kFirstRpm) / static_cast<double>(kCamPoints - 1);

    const Geometry g = geometryOf(e);
    const CamProfile stock = stockCamProfile(e);
    const CamProfile low = camProfileOf(e);
    const bool phased = camPhased(e);
    fillCurve(g, t, stock, false, t.stock);
    if (!phased && sameCam(low, stock)) t.low = t.stock;
    else fillCurve(g, t, low, phased, t.low);
    t.switchRpm = 0.0;
    if (camTwoStep(e)) {
        fillCurve(g, t, highCamProfileOf(e), phased, t.high);
        // Le premier régime où le second profil passe devant, en montant : là où les deux courbes de couple se croisent.
        for (int i = 1; i < kCamPoints; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            const double before = t.high.imep[at - 1] - t.low.imep[at - 1];
            const double after = t.high.imep[at] - t.low.imep[at];
            if (before <= 0.0 && after > 0.0) {
                t.switchRpm = rpmAt(t, i - 1) + t.stepRpm * before / (before - after);
                break;
            }
        }
    } else {
        t.high = t.low;
    }
    return t;
}

/** La valeur d'une courbe à ce régime : linéaire entre les points, tenue aux bouts. */
double sample(const Tables& t, const std::array<double, kCamPoints>& curve, double rpm) {
    const double position = (rpm - t.firstRpm) / t.stepRpm;
    if (position <= 0.0) return curve[0];
    if (position >= static_cast<double>(kCamPoints - 1)) return curve[kCamPoints - 1];
    const int index = static_cast<int>(position);
    const double a = curve[static_cast<std::size_t>(index)];
    return a + (curve[static_cast<std::size_t>(index + 1)] - a) * (position - static_cast<double>(index));
}

double meanExhaustArea(const Geometry& g, const CamProfile& cam) {
    // La bouffée : les 60 premiers degrés après l'ouverture à 1 mm (supposé), là où le cylindre se vide sous sa propre pression.
    double sum = 0.0;
    for (int i = 0; i < 60; ++i) {
        const double deg = cam.evoDeg + static_cast<double>(i) + 0.5;
        sum += valveArea(g.exhaustM, g.exhausts, cam.liftMm / 1000.0 * camLiftShare((deg - (360.0 - cam.exhaustCenterDeg)) / cam.halfSpanDeg));
    }
    return sum / 60.0;
}

CamIdle idleOf(const EngineConfig& e, const CamProfile& cam, double idleRpm) {
    CamIdle idle;
    if (!camHasValves(e)) return idle;
    Powertrain powertrain;
    powertrain.engine = e;
    idle.rpm = idleRpm;
    const Geometry g = geometryOf(e);
    // Ce que le moteur doit fournir au ralenti : ses frottements et ses accessoires, en pression moyenne.
    const double litres = e.sweptCcPerTwoTurns() / 1000.0;
    const double needBar = frictionTorqueNm(powertrain, idle.rpm) * 4.0 * kPi / (litres * 1e-3) / 1e5 + kIdleAccessoryBar;
    const double back = backPressurePa(g, idle.rpm);
    double low = 0.12 * kAmbientPa, high = kAmbientPa;
    for (int i = 0; i < 12; ++i) {
        const double middle = 0.5 * (low + high);
        (runCycle(g, cam, idle.rpm, middle, back, kIdleHeatShare).imepBar < needBar ? low : high) = middle;
    }
    idle.manifoldKpa = 0.5 * (low + high) / 1000.0;
    const ValveTrainGeometry valves = valveTrainOf(e.valvetrain(), static_cast<double>(e.boreMm));
    const double factor = overlapFactor(cam, static_cast<double>(e.boreMm), valves.intakeMm * (valves.perCylinder / 2));
    idle.residual = residualGasFraction(factor, idle.rpm, idle.manifoldKpa, back / 1000.0, compressionOf(e));
    idle.cov = combustionCov(idle.residual);
    return idle;
}

/** Les tables de ce moteur, ralenti compris : trois ralentis à résoudre, une seule fois. */
const Tables& idleTables(const EngineConfig& e, double idleRpm) {
    const Tables& found = tablesOf(e);
    Tables& t = const_cast<Tables&>(found);
    if (!t.idleKnown) {
        t.idleStock = idleOf(e, stockCamProfile(e), idleRpm);
        const CamProfile low = camProfileOf(e);
        t.idleLow = sameCam(low, stockCamProfile(e)) ? t.idleStock : idleOf(e, low, idleRpm);
        t.idleHigh = camTwoStep(e) ? idleOf(e, highCamProfileOf(e), idleRpm) : t.idleLow;
        t.idleKnown = true;
    }
    return t;
}

}  // namespace

double camLiftShare(double x) {
    if (!(x > -1.0 && x < 1.0)) return 0.0;
    const double x2 = x * x;
    const double u = 1.0 - x2;
    return u * u * (1.0 - x2 * x2);
}

CamProfile makeCamProfile(double durationDeg, double liftMm, double lobeSeparationDeg, double intakeCenterDeg) {
    CamProfile c;
    c.durationDeg = durationDeg;
    c.liftMm = liftMm;
    c.lobeSeparationDeg = lobeSeparationDeg;
    c.intakeCenterDeg = intakeCenterDeg;
    c.exhaustCenterDeg = 2.0 * lobeSeparationDeg - intakeCenterDeg;
    // La demi-durée siège à siège, depuis la levée de référence : une petite levée l'allonge, car le millimètre est plus haut.
    const double reference = shareAt(clamp(kCamReferenceLiftMm / liftMm, 0.0, 0.99));
    c.halfSpanDeg = std::fmin(0.5 * durationDeg / reference, kHalfSpanMaxDeg);
    // Une durée qui ne tiendrait pas dans un tour se lit à ce qui en reste.
    c.durationDeg = std::fmin(durationDeg, 2.0 * c.halfSpanDeg * reference);
    const double halfDuration = 0.5 * c.durationDeg;
    c.ivoDeg = 360.0 + c.intakeCenterDeg - halfDuration;
    c.ivcDeg = 360.0 + c.intakeCenterDeg + halfDuration;
    c.evoDeg = 360.0 - c.exhaustCenterDeg - halfDuration;
    c.evcDeg = 360.0 - c.exhaustCenterDeg + halfDuration;
    return c;
}

CamProfile advancedIntake(const CamProfile& base, double advanceDeg) {
    CamProfile c = base;
    c.intakeCenterDeg = base.intakeCenterDeg - advanceDeg;
    c.ivoDeg = base.ivoDeg - advanceDeg;
    c.ivcDeg = base.ivcDeg - advanceDeg;
    return c;
}

bool camHasValves(const EngineConfig& e) { return !isRotary(e) && e.cycle == EngineCycle::FourStroke; }

bool camIsStock(const EngineConfig& e) {
    if (!camHasValves(e)) return true;
    return e.camDurationDeg == 0 && e.camLiftMm == 0.0f && e.lobeSeparationDeg == 0 && e.valveTiming == ValveTiming::Fixed;
}

bool camPhased(const EngineConfig& e) {
    return camHasValves(e) && (e.valveTiming == ValveTiming::Phased || e.valveTiming == ValveTiming::PhasedTwoStep);
}

bool camTwoStep(const EngineConfig& e) {
    return camHasValves(e) && (e.valveTiming == ValveTiming::TwoStep || e.valveTiming == ValveTiming::PhasedTwoStep);
}

double stockCamLiftMm(double boreMm) { return kLiftPerBore * boreMm; }

CamProfile stockCamProfile(const EngineConfig& e) {
    return makeCamProfile(kCamStockDurationDeg, stockCamLiftMm(e.effectiveBoreMm()), kCamStockLobeSeparationDeg, kCamStockLobeSeparationDeg);
}

namespace {

/** Le profil réglé, tel qu'il est saisi : 0, d'origine. */
CamProfile chosenCamProfile(const EngineConfig& e) {
    const double duration = e.camDurationDeg > 0 ? static_cast<double>(e.camDurationDeg) : kCamStockDurationDeg;
    const double lift = e.camLiftMm > 0.0f ? static_cast<double>(e.camLiftMm) : stockCamLiftMm(e.effectiveBoreMm());
    const double separation = e.lobeSeparationDeg > 0 ? static_cast<double>(e.lobeSeparationDeg) : kCamStockLobeSeparationDeg;
    return makeCamProfile(duration, lift, separation, separation);
}

/** Deux profils de part et d'autre du profil réglé, sur le même axe, bornés comme les réglages. */
CamProfile spreadCamProfile(const EngineConfig& e, double side) {
    const CamProfile chosen = chosenCamProfile(e);
    return makeCamProfile(clamp(chosen.durationDeg + side * kCamTwoStepSpreadDeg, 180.0, 320.0), clamp(chosen.liftMm * (1.0 + side * kCamTwoStepLiftSpread), 5.0, 16.0),
                          chosen.lobeSeparationDeg, chosen.intakeCenterDeg);
}

}  // namespace

CamProfile camProfileOf(const EngineConfig& e) { return camTwoStep(e) ? spreadCamProfile(e, -1.0) : chosenCamProfile(e); }

CamProfile highCamProfileOf(const EngineConfig& e) { return camTwoStep(e) ? spreadCamProfile(e, 1.0) : chosenCamProfile(e); }

double camValveLiftMm(const CamProfile& cam, double cycleDeg, bool intake) {
    const double center = intake ? 360.0 + cam.intakeCenterDeg : 360.0 - cam.exhaustCenterDeg;
    double offset = cycleDeg - center;
    while (offset > 360.0) offset -= 720.0;
    while (offset < -360.0) offset += 720.0;
    return cam.liftMm * camLiftShare(offset / cam.halfSpanDeg);
}

double intakeClosingAbdcDeg(const CamProfile& cam) { return cam.ivcDeg - 540.0; }

double valveOverlapDeg(const CamProfile& cam) { return std::fmax(cam.evcDeg - cam.ivoDeg, 0.0); }

double camDurationAtLiftDeg(const CamProfile& cam, double liftMm) {
    if (liftMm >= cam.liftMm) return 0.0;
    return 2.0 * cam.halfSpanDeg * shareAt(liftMm / cam.liftMm);
}

double effectiveCompressionRatio(double geometricRatio, double ivcAbdcDeg, double rodToCrank) {
    const double a = (180.0 + ivcAbdcDeg) * kDegToRad;
    const double s = portable::sin(a);
    const double share = 0.5 * (1.0 - portable::cos(a) + rodToCrank - std::sqrt(rodToCrank * rodToCrank - s * s));
    return 1.0 + (geometricRatio - 1.0) * share;
}

double camNoseDecelerationMs2(const CamProfile& cam, double rpm) {
    const double omega = rpm * 2.0 * kPi / 60.0;
    const double span = cam.halfSpanDeg * kDegToRad;
    return -kCamNoseCurvature * cam.liftMm / 1000.0 * omega * omega / (span * span);
}

double overlapFactor(const CamProfile& cam, double boreMm, double intakeValvesMm) {
    const double overlap = valveOverlapDeg(cam);
    return 1.45 / boreMm * (107.0 + 7.8 * overlap + overlap * overlap) * (cam.liftMm * intakeValvesMm / (boreMm * boreMm));
}

double residualGasFraction(double overlapFactorValue, double rpm, double intakeKpa, double exhaustKpa, double compressionRatio, double equivalenceRatio) {
    const double ratio = intakeKpa / exhaustKpa;
    const double through = 1.266 * overlapFactorValue / std::fmax(rpm, 100.0) * portable::pow(ratio, -0.87) * std::sqrt(std::fabs(exhaustKpa - intakeKpa));
    const double trapped = 0.632 * equivalenceRatio / compressionRatio * portable::pow(ratio, -0.74);
    return clamp(through + trapped, 0.0, 0.9);
}

double combustionCov(double residualFraction) {
    const double flame = std::fmax(1.0 - kDilutionSlope * portable::pow(std::fmax(residualFraction, 1e-6), kDilutionPower), kFlameFloor);
    return kCovUndiluted * portable::pow(flame, -kCovPower);
}

CamEffect camEffect(const EngineConfig& e, double rpm, bool high) {
    CamEffect effect;
    if (camIsStock(e)) return effect;
    const Tables& t = tablesOf(e);
    const Curve& c = high && camTwoStep(e) ? t.high : t.low;
    effect.torque = sample(t, c.imep, rpm) / sample(t, t.stock.imep, rpm);
    effect.filling = sample(t, c.fill, rpm) / sample(t, t.stock.fill, rpm);
    effect.opening = sample(t, c.open, rpm) / sample(t, t.stock.open, rpm);
    return effect;
}

double camTorqueRatio(const EngineConfig& e, double rpm) {
    if (camIsStock(e)) return 1.0;
    const double low = camEffect(e, rpm, false).torque;
    return camTwoStep(e) ? std::fmax(low, camEffect(e, rpm, true).torque) : low;
}

double camPhaserTargetDeg(const EngineConfig& e, double rpm, bool high) {
    if (!camPhased(e)) return 0.0;
    const Tables& t = tablesOf(e);
    return sample(t, high && camTwoStep(e) ? t.high.phase : t.low.phase, rpm);
}

double camSwitchRpm(const EngineConfig& e, double floatRpm) {
    if (!camTwoStep(e)) return 0.0;
    const double rpm = tablesOf(e).switchRpm;
    return floatRpm > 0.0 && rpm > floatRpm ? 0.0 : rpm;
}

CamIdle camIdle(const EngineConfig& e) {
    Powertrain powertrain;
    powertrain.engine = e;
    const double rpm = idleRpmOf(powertrain);
    if (camIsStock(e)) return idleOf(e, stockCamProfile(e), rpm);
    return idleTables(e, rpm).idleLow;
}

CamIdle stockIdle(const EngineConfig& e) {
    Powertrain powertrain;
    powertrain.engine = e;
    const double rpm = idleRpmOf(powertrain);
    if (camIsStock(e)) return idleOf(e, stockCamProfile(e), rpm);
    return idleTables(e, rpm).idleStock;
}

void applyCam(const EngineConfig& e, EngineProfile& p) {
    if (camIsStock(e)) return;
    const Tables& t = idleTables(e, static_cast<double>(p.idleRpm));
    const Geometry g = geometryOf(e);
    const CamProfile stock = stockCamProfile(e);
    const CamProfile low = camProfileOf(e);
    const CamProfile high = camTwoStep(e) ? highCamProfileOf(e) : low;
    p.camTuned = true;
    const auto timing = [](const CamProfile& c) {
        return std::array<float, 4>{static_cast<float>(c.evoDeg / 720.0), static_cast<float>(c.evcDeg / 720.0), static_cast<float>(c.ivoDeg / 720.0),
                                    static_cast<float>(c.ivcDeg / 720.0)};
    };
    p.camTiming = timing(low);
    p.camHighTiming = timing(high);
    p.camFirstRpm = static_cast<float>(t.firstRpm);
    p.camStepRpm = static_cast<float>(t.stepRpm);
    for (int i = 0; i < kCamPoints; ++i) {
        const std::size_t at = static_cast<std::size_t>(i);
        p.camOpening[at] = static_cast<float>(t.low.open[at] / t.stock.open[at]);
        p.camOpeningHigh[at] = static_cast<float>(t.high.open[at] / t.stock.open[at]);
    }
    const double stockArea = meanExhaustArea(g, stock);
    p.camBlowStretch = static_cast<float>(stockArea / meanExhaustArea(g, low));
    p.camBlowStretchHigh = static_cast<float>(stockArea / meanExhaustArea(g, high));
    const ValveTrainGeometry valves = valveTrainOf(e.valvetrain(), static_cast<double>(e.boreMm));
    const double intakeMm = valves.intakeMm * (valves.perCylinder / 2);
    const double bore = static_cast<double>(e.boreMm);
    p.camOverlapFactor = static_cast<float>(overlapFactor(low, bore, intakeMm));
    p.camOverlapFactorHigh = static_cast<float>(overlapFactor(high, bore, intakeMm));
    p.camStockOverlapFactor = static_cast<float>(overlapFactor(stock, bore, intakeMm));
    p.camCompression = static_cast<float>(compressionOf(e));
    p.camRatedRpm = static_cast<float>(g.ratedRpm);
    p.camIdleKpa = static_cast<float>(t.idleLow.manifoldKpa);
    p.camIdleKpaHigh = static_cast<float>(t.idleHigh.manifoldKpa);
    p.camStockIdleKpa = static_cast<float>(t.idleStock.manifoldKpa);
    // La flamme laminaire et sa dilution : un moteur à étincelle. Un diesel s'enflamme de lui-même, et ses résiduels l'aident.
    p.camDispersion = !e.burnsDiesel();
    p.camTwoStep = camTwoStep(e);
    p.camSwitchRpm = static_cast<float>(t.switchRpm);
}

}  // namespace revforge
