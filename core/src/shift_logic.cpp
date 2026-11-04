#include "revforge/shift_logic.hpp"

#include "revforge/tyre.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
// Le moteur tourne entraîné par les roues, ou tout au bout de sa course : plus aucun mode ne garde ce rapport.
constexpr double kProtectShare = 0.998;
// Hystérésis : ce qu'on rétrograde pour accélérer doit retomber nettement sous le point de remontée,
// et ce qu'on monte doit retomber nettement au-dessus du plancher.
constexpr double kKickdownMargin = 0.88;
constexpr double kLandingMargin = 1.02;
constexpr double kMinAfterShiftS = 0.4;
// Le rapport inférieur n'apporte quelque chose que s'il tire vraiment plus aux roues.
constexpr double kKickdownGain = 1.02;
constexpr double kPullingMs2 = 0.8;
// Juste après un passage, on ne revient en arrière que si la voiture n'avance vraiment plus.
constexpr double kKickLockS = 2.5;
constexpr double kBoggedMs2 = 0.3;

constexpr double clampTo(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

constexpr ShiftTraits kNormal{2.5, 0.0, 0.0, 0.85, 1.4, 1.0, 2, 0.9, 0.85, 0.0};
constexpr ShiftTraits kEco{2.5, 0.0, 0.0, 0.96, 1.25, 1.0, 2, 0.84, 0.9, 0.0};
constexpr ShiftTraits kSport{1.0, 4.0, 0.08, 0.65, 1.8, 0.85, 2, 0.88, 1.01, 0.6};
constexpr ShiftTraits kTrack{0.5, 10.0, 0.12, 0.45, 2.6, 0.7, 1, 0.9, 1.01, 0.9};
constexpr ShiftTraits kPaddles{0.3, 0.0, 0.0, 2.0, 1.0, 1.0, 2, 0.9, 0.85, 0.0};

double softNormal(const ShiftMap& m) { return std::max(m.idle * 2.6, m.redline * 0.38); }
double hardNormal(const ShiftMap& m) { return m.redline * 0.965; }

double softSport(const ShiftMap& m, double step, double hard) {
    const double soft = std::max(downshiftFloor(m, ShiftMode::Sport) * 1.12 * step, softNormal(m) * 1.18);
    return std::min(soft, 0.85 * hard);
}

double hardSport(const ShiftMap& m, int gear) {
    const double crossing = m.crossing[static_cast<std::size_t>(std::clamp(gear, 1, kMaxGears) - 1)];
    return clampTo(crossing, hardNormal(m) + 0.4 * (m.redline - hardNormal(m)), m.redline * 0.985);
}

}  // namespace

const ShiftTraits& shiftTraits(ShiftMode mode) {
    switch (mode) {
        case ShiftMode::Eco: return kEco;
        case ShiftMode::Sport: return kSport;
        case ShiftMode::Track: return kTrack;
        case ShiftMode::Paddles: return kPaddles;
        case ShiftMode::Normal: break;
    }
    return kNormal;
}

double ShiftMap::torqueAt(double rpm) const {
    const double span = redline - idle;
    if (span <= 0.0) return torque[0];
    const double at = clampTo((rpm - idle) / span, 0.0, 1.0) * (kPoints - 1);
    const int low = std::min(static_cast<int>(at), kPoints - 2);
    const double share = at - low;
    return torque[static_cast<std::size_t>(low)] * (1.0 - share) + torque[static_cast<std::size_t>(low + 1)] * share;
}

double ShiftMap::stepAt(int gear) const {
    if (gear < 1 || gear >= gears) return 1.01;
    const double here = overall[static_cast<std::size_t>(gear - 1)];
    const double next = overall[static_cast<std::size_t>(gear)];
    return next > 0.0 ? std::max(here / next, 1.01) : 1.01;
}

ShiftMap buildShiftMap(const Powertrain& p, const Gearing& gearing) {
    ShiftMap m;
    const PowerSummary peak = powerSummary(p);
    m.idle = idleRpmOf(p);
    m.redline = redlineRpmOf(p);
    m.peakTorqueRpm = peak.peakTorqueRpm;
    m.peakPowerRpm = peak.peakPowerRpm;
    m.radius = wheelRadiusM(p.car);
    m.efficiency = gearing.box.meshEfficiency;
    m.gears = gearing.gears;
    m.overall = gearing.overall;
    for (int i = 0; i < ShiftMap::kPoints; ++i) {
        const double rpm = m.idle + (m.redline - m.idle) * i / (ShiftMap::kPoints - 1);
        m.torque[static_cast<std::size_t>(i)] = torqueCurveNm(p, rpm);
    }
    for (int gear = 1; gear < m.gears; ++gear) {
        const double step = m.stepAt(gear);
        const double here = m.overall[static_cast<std::size_t>(gear - 1)];
        const double next = m.overall[static_cast<std::size_t>(gear)];
        double crossing = m.redline;
        for (double rpm = m.peakTorqueRpm; rpm < m.redline; rpm += 25.0) {
            if (m.torqueAt(rpm / step) * next >= m.torqueAt(rpm) * here) {
                crossing = rpm;
                break;
            }
        }
        m.crossing[static_cast<std::size_t>(gear - 1)] = crossing;
    }
    return m;
}

double downshiftFloor(const ShiftMap& m, ShiftMode mode) {
    switch (mode) {
        case ShiftMode::Eco: return std::max(m.idle * 1.2, m.redline * 0.14);
        case ShiftMode::Sport: return std::max(m.idle * 1.7, std::min(m.peakTorqueRpm * 0.55, m.redline * 0.35));
        case ShiftMode::Track: return std::max(m.idle * 2.0, std::min(m.peakTorqueRpm * 0.75, m.redline * 0.5));
        case ShiftMode::Paddles: return std::max(m.idle * 1.25, m.redline * 0.15);
        case ShiftMode::Normal: break;
    }
    return std::max(m.idle * 1.7, m.redline * 0.23);
}

double upshiftPoint(const ShiftMap& m, ShiftMode mode, int gear, double demand) {
    const double step = m.stepAt(gear);
    switch (mode) {
        case ShiftMode::Eco: {
            // Assez bas pour retomber juste au-dessus du plancher, jamais au-dessus de ce que ferait le mode normal.
            const double soft = std::min(std::max(downshiftFloor(m, mode) * 1.1 * step, softNormal(m) * 0.72), softNormal(m) * 0.92);
            // Pied au plancher, on quitte le moteur là où il commence à consommer pour peu de puissance en plus.
            const double hard = clampTo(m.peakTorqueRpm + 0.35 * (m.peakPowerRpm - m.peakTorqueRpm), soft * 1.25, m.redline * 0.8);
            return soft + (hard - soft) * demand * demand;
        }
        case ShiftMode::Sport: {
            const double hard = hardSport(m, gear);
            const double soft = softSport(m, step, hard);
            return soft + (hard - soft) * demand;
        }
        case ShiftMode::Track: {
            const double hard = m.redline * 0.995;
            const double sport = softSport(m, step, hardSport(m, gear));
            const double soft = std::min(std::max({sport, downshiftFloor(m, mode) * 1.12 * step, softNormal(m) * 1.5}), 0.92 * hard);
            return soft + (hard - soft) * std::sqrt(clampTo(demand, 0.0, 1.0));
        }
        case ShiftMode::Paddles:
            return m.redline * kProtectShare;
        case ShiftMode::Normal:
            break;
    }
    // À fond, on tire jusqu'à la fin de la puissance ; en douceur, on passe tôt.
    const double soft = softNormal(m);
    const double hard = hardNormal(m);
    return soft + (hard - soft) * demand * demand;
}

double brakingCeiling(const ShiftMap& m, ShiftMode mode, bool descending) {
    switch (mode) {
        case ShiftMode::Sport: return std::min(m.peakPowerRpm * 0.8, m.redline * 0.75);
        case ShiftMode::Track: return m.redline * 0.88;
        case ShiftMode::Eco: return m.redline * 0.3;
        case ShiftMode::Normal: return descending ? m.redline * 0.55 : 0.0;
        case ShiftMode::Paddles: break;
    }
    return 0.0;
}

int decideShift(const ShiftMap& m, ShiftMode mode, const ShiftView& v) {
    if (v.gear < 1) return 0;
    const ShiftTraits& traits = shiftTraits(mode);
    const bool canUp = v.gear < m.gears;
    const bool canDown = v.gear > 1;
    const double rpmAbove = v.roadRpm / m.stepAt(v.gear);
    const double rpmBelow = canDown ? v.roadRpm * m.stepAt(v.gear - 1) : 0.0;
    const bool gapOver = v.sinceShift > traits.gapSec;
    const double floor = downshiftFloor(m, mode);

    // Protéger le moteur vient avant tout le reste, même en virage, même au frein.
    if (canUp && v.rpm >= m.redline * kProtectShare && v.sinceShift > kMinAfterShiftS && v.coupled) return 1;
    // Les pneus sont à leur limite, passé le sommet de leur courbe (tyre.hpp) : plus de couple n'y ajouterait rien, et un
    // rapport de moins ne ferait que patiner.
    const bool traction = v.slipRatio > tyrePeakSlip();
    // Bas dans les tours mais la voiture accélère franchement : le moteur n'a pas besoin d'aide.
    if (canDown && v.coupled && v.roadRpm < floor && v.sinceShift > kMinAfterShiftS && !traction && v.accelMs2 < kPullingMs2) return -1;
    if (mode == ShiftMode::Paddles) return 0;

    if (v.braking) {
        const double ceiling = brakingCeiling(m, mode, v.descending);
        if (canDown && rpmBelow < ceiling && v.sinceShift > std::min(traits.gapSec, 0.5)) return -1;
        return 0;
    }

    // Rétrograder pour accélérer : si le pied le demande, si le moteur reste sous le point de remontée, et si les roues y gagnent.
    const bool kickReady = gapOver && (v.sinceShift > kKickLockS || v.accelMs2 < kBoggedMs2);
    if (canDown && !traction && v.throttle >= traits.kickdownPedal && kickReady && rpmBelow < upshiftPoint(m, mode, v.gear - 1, v.demand) * kKickdownMargin) {
        // Ce qui compte est ce qui passe au sol : au-delà de ce que les pneus transmettent, un rapport de moins ne ferait que patiner.
        const double toWheels = m.efficiency / m.radius;
        const double here = std::min(m.torqueAt(v.roadRpm) * m.overall[static_cast<std::size_t>(v.gear - 1)] * toWheels, v.tractionN);
        const double below = std::min(m.torqueAt(rpmBelow) * m.overall[static_cast<std::size_t>(v.gear - 2)] * toWheels, v.tractionN);
        if (below > here * kKickdownGain) return -1;
    }

    // La route a changé sous le rapport : une côte, une remorque. Il ne tient plus la réserve qu'on lui demandait pour y monter, le pied
    // demande plus que cette réserve n'en laisse (au-delà de 1/réserve de la pleine charge), et la voiture n'avance plus : on reprend le
    // rapport inférieur, s'il tire plus aux roues et ne remonterait pas aussitôt. Sous le seuil du kickdown, rien d'autre ne le ferait.
    if (canDown && !traction && gapOver && v.coupled && v.throttle * traits.reserve >= 1.0 && v.accelMs2 < kBoggedMs2) {
        const double toWheels = m.efficiency / m.radius;
        const double pull = m.torqueAt(v.roadRpm) * m.overall[static_cast<std::size_t>(v.gear - 1)] * toWheels;
        if (pull < traits.reserve * v.resistanceN && rpmBelow < upshiftPoint(m, mode, v.gear - 1, v.demand) * kKickdownMargin) {
            const double here = std::min(pull, v.tractionN);
            const double below = std::min(m.torqueAt(rpmBelow) * m.overall[static_cast<std::size_t>(v.gear - 2)] * toWheels, v.tractionN);
            if (below > here * kKickdownGain) return -1;
        }
    }

    if (!canUp || !gapOver || rpmAbove < floor * kLandingMargin) return 0;
    // Les roues patinent au-delà du sommet : le régime ne dit plus la vitesse de la voiture, et monter un rapport à l'allure d'un
    // piéton le ferait caler. La boîte attend, comme une boîte qui lit la vitesse aux roues non motrices (ABS) ; seule la
    // protection du moteur, plus haut, monte encore.
    if (traction) return 0;
    // Le rapport suivant doit tenir la route, même si la vitesse fléchit un peu : c'est ce qui évite d'hésiter dans une côte,
    // surtout sous un turbo, dont le couple s'effondre avec le régime.
    const double pull = m.torqueAt(rpmAbove * 0.9) * m.overall[static_cast<std::size_t>(v.gear)] * m.efficiency / m.radius;
    if (pull < traits.reserve * v.resistanceN) return 0;

    const bool holding = v.cornering && (mode == ShiftMode::Sport || mode == ShiftMode::Track);
    if (holding) return 0;
    if (v.throttle > 0.05) {
        return v.coupled && v.rpm > upshiftPoint(m, mode, v.gear, v.demand) ? 1 : 0;
    }
    // Pied levé, l'éco monte quand même : la voiture court plus loin sur son élan, sans frein moteur.
    if (mode == ShiftMode::Eco && v.speedKmh > 25.0 && v.coupled && v.rpm > upshiftPoint(m, mode, v.gear, 0.0)) return 1;
    return 0;
}

}  // namespace revforge
