#include "revforge/consumption.hpp"

#include <algorithm>

#include "revforge/powertrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAtmosphere = 1e5;

// Carburants du commerce (voir docs/modele.md pour les sources).
constexpr FuelSpec kPetrol{0.745, 42900.0, 3.17, 14.7};
constexpr FuelSpec kDiesel{0.832, 43100.0, 3.15, 14.5};

// Rendement indiqué brut, de la combustion au piston, au meilleur de la plage : un moteur suralimenté comprime moins.
constexpr double kEfficiencyPetrol = 0.405;
constexpr double kEfficiencyPetrolTurbo = 0.375;
constexpr double kEfficiencyDiesel = 0.46;
// À bas régime, le gaz a plus de temps pour céder sa chaleur aux parois.
constexpr double kHeatLossShare = 0.08;
constexpr double kHeatLossBelowRpm = 2000.0;
// À faible remplissage, les gaz résiduels ralentissent la combustion ; un diesel n'en souffre presque pas.
constexpr double kLowFillLossPetrol = 0.22;
constexpr double kLowFillLossDiesel = 0.10;
// Écart de pression de part et d'autre d'un papillon fermé : jamais plus d'une atmosphère.
constexpr double kThrottlingBar = 1.0;
// À pleine charge, l'essence enrichit : pour le couple, puis pour protéger l'échappement. Le diesel reste pauvre.
constexpr double kFullLoadLambda = 0.88;
constexpr double kFullLoadLambdaTurbo = 0.82;
constexpr double kEnrichFromShare = 0.85;
constexpr double kEnrichAtAnyPower = 0.4;
// Air par litre de cylindrée à la pression atmosphérique, et remplissage au pic de couple.
constexpr double kAirGPerL = 1.2;
constexpr double kPeakVolumetric = 0.9;

// Huile. Évaporation et projection sur la chemise, qui suivent le régime, et passage aux segments, qui suit la charge.
// Un ordre de grandeur, à un facteur trois près : voir docs/modele.md.
constexpr double kOilGPerHPerLitreKrpm = 0.6;
constexpr double kOilGPerKwh = 0.1;
constexpr double kOilTurbo = 1.3;
constexpr double kOilDiesel = 1.2;
constexpr double kOilDensityKgPerL = 0.85;

// Machine et onduleur, de la batterie à l'arbre, dans les deux sens.
constexpr double kElectricChainEfficiency = 0.90;

// Sous cette vitesse, un chiffre par 100 km n'a pas de sens.
constexpr double kMovingMs = 1.4;
constexpr double kMovingForAverageM = 100.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

}  // namespace

const FuelSpec& fuelSpecOf(Fuel fuel) { return fuel == Fuel::Diesel ? kDiesel : kPetrol; }

void ConsumptionModel::configure(const Powertrain& p) {
    electric_ = p.electric;
    diesel_ = p.engine.fuel == Fuel::Diesel;
    turbo_ = p.engine.aspiration == Aspiration::Turbo;
    fuel_ = &fuelSpecOf(p.engine.fuel);
    litres_ = static_cast<double>(p.engine.displacementCc) / 1000.0;
    redline_ = redlineRpmOf(p);
    baseEfficiency_ = diesel_ ? kEfficiencyDiesel : (turbo_ ? kEfficiencyPetrolTurbo : kEfficiencyPetrol);
    fullRichness_ = diesel_ ? 1.0 : 1.0 / (turbo_ ? kFullLoadLambdaTurbo : kFullLoadLambda);
    throttlingNm_ = diesel_ ? 0.0 : kThrottlingBar * kAtmosphere * litres_ * 1e-3 / (4.0 * kPi);
    peakNaturalNm_ = 1.0;
    peakGrossW_ = 1.0;
    if (electric_) return;
    // Calculer ce qui vaut pour tout le régime une fois par réglage : `fullLoadTorqueNm` est trop lent pour chaque pas.
    for (int i = 0; i < kCurvePoints; ++i) {
        const double rpm = redline_ * i / (kCurvePoints - 1);
        const double gross = fullLoadTorqueNm(p, rpm, 0.0) + frictionTorqueNm(p, rpm);
        naturalCurve_[static_cast<std::size_t>(i)] = gross;
        peakNaturalNm_ = std::max(peakNaturalNm_, gross);
        const double fullGross = fullLoadTorqueNm(p, rpm, steadyBoost(p, rpm)) + frictionTorqueNm(p, rpm);
        peakGrossW_ = std::max(peakGrossW_, fullGross * rpm * 2.0 * kPi / 60.0);
    }
}

double ConsumptionModel::naturalGrossNm(double rpm) const {
    const double at = clamp(rpm / redline_, 0.0, 1.0) * (kCurvePoints - 1);
    const int low = std::min(static_cast<int>(at), kCurvePoints - 2);
    const double share = at - low;
    const double a = naturalCurve_[static_cast<std::size_t>(low)];
    const double b = naturalCurve_[static_cast<std::size_t>(low + 1)];
    return a + (b - a) * share;
}

double ConsumptionModel::fuelGps(double rpm, double brakeNm, double frictionNm, double fullGrossNm) const {
    if (electric_) return 0.0;
    const double natural = std::max(naturalGrossNm(rpm), 1.0);
    const double onPistons = brakeNm + frictionNm;
    // Le pompage dépend de la pression du collecteur, elle-même de ce travail : au-dessus de la pleine charge
    // atmosphérique, plus de papillon ; en dessous, une droite, donc une solution directe.
    double gross = onPistons + throttlingNm_ > natural ? onPistons : (onPistons + throttlingNm_) / (1.0 + throttlingNm_ / natural);
    // Le moteur retient autant qu'un moteur entraîné : rien ne brûle.
    if (gross <= 0.0) return 0.0;
    const double pressure = std::min(gross / natural, 1.0);
    const double fullGross = std::max(fullGrossNm, 1.0);
    // Un diesel n'a pas de papillon : ce qui compte est la part du carburant maximal, pas la pression.
    const double fill = diesel_ ? std::min(gross / fullGross, 1.0) : pressure;

    const double slow = 1.0 - std::min(rpm / kHeatLossBelowRpm, 1.0);
    const double lowFill = (1.0 - fill) * (1.0 - fill);
    const double efficiency = baseEfficiency_ * (1.0 - kHeatLossShare * slow * slow) * (1.0 - (diesel_ ? kLowFillLossDiesel : kLowFillLossPetrol) * lowFill);

    const double omega = rpm * 2.0 * kPi / 60.0;
    double richness = 1.0;
    if (fullRichness_ > 1.0) {
        const double nearFull = clamp((gross / fullGross - kEnrichFromShare) / (1.0 - kEnrichFromShare), 0.0, 1.0);
        const double heat = clamp(gross * omega / peakGrossW_, 0.0, 1.0);
        richness = 1.0 + (fullRichness_ - 1.0) * nearFull * (kEnrichAtAnyPower + (1.0 - kEnrichAtAnyPower) * heat);
    }
    return gross * omega / efficiency * richness / fuel_->lowerHeatingJPerG;
}

double ConsumptionModel::airGps(double rpm, double load, double fullGrossNm) const {
    if (electric_) return 0.0;
    // Le couple à pleine charge suit l'air que le moteur avale : sa courbe donne le remplissage, et son rapport à la
    // courbe atmosphérique la pression du turbo. Un diesel n'est pas étranglé : il avale tout, quelle que soit la charge.
    const double admitted = (diesel_ ? 1.0 : clamp(load, 0.0, 1.0)) * std::max(fullGrossNm, 0.0) / peakNaturalNm_;
    return kAirGPerL * litres_ * rpm / 120.0 * kPeakVolumetric * admitted;
}

double ConsumptionModel::oilGps(double rpm, double brakeNm) const {
    if (electric_ || rpm <= 0.0) return 0.0;
    const double kw = std::max(brakeNm, 0.0) * rpm * 2.0 * kPi / 60.0 / 1000.0;
    const double perHour = kOilGPerHPerLitreKrpm * litres_ * rpm / 1000.0 + kOilGPerKwh * kw;
    return perHour * (turbo_ ? kOilTurbo : 1.0) * (diesel_ ? kOilDiesel : 1.0) / 3600.0;
}

double ConsumptionModel::batteryW(double motorNm, double omega) const {
    const double shaft = motorNm * omega;
    return shaft >= 0.0 ? shaft / kElectricChainEfficiency : shaft * kElectricChainEfficiency;
}

void ConsumptionMeter::resetTrip() {
    fuelG_ = 0.0;
    airG_ = 0.0;
    oilG_ = 0.0;
    energyJ_ = 0.0;
    distanceM_ = 0.0;
}

void ConsumptionMeter::step(const ConsumptionInput& in, double dt) {
    speedMs_ = in.speedMs;
    distanceM_ += in.speedMs * dt;
    if (model_.electric()) {
        fuelGps_ = 0.0;
        airGps_ = 0.0;
        oilGps_ = 0.0;
        batteryW_ = model_.batteryW(in.motorNm, in.omega);
        energyJ_ += batteryW_ * dt;
        return;
    }
    batteryW_ = 0.0;
    fuelGps_ = in.burning ? model_.fuelGps(in.rpm, in.brakeNm, in.frictionNm, in.fullGrossNm) : 0.0;
    // L'air ne compte que là où quelque chose brûle : c'est ce qu'on compare au carburant.
    airGps_ = fuelGps_ > 0.0 ? model_.airGps(in.rpm, in.load, in.fullGrossNm) : 0.0;
    // Tant que le moteur tourne, l'huile passe, même pied levé : c'est la seule chose qui ne se coupe pas.
    oilGps_ = model_.oilGps(in.rpm, in.burning ? in.brakeNm : 0.0);
    fuelG_ += fuelGps_ * dt;
    airG_ += airGps_ * dt;
    oilG_ += oilGps_ * dt;
}

double ConsumptionMeter::fuelLph() const {
    return model_.electric() ? 0.0 : fuelGps_ * 3.6 / model_.fuel().densityKgPerL;
}

double ConsumptionMeter::fuelLper100km() const {
    if (speedMs_ < kMovingMs) return 0.0;
    return fuelLph() / (speedMs_ * 3.6) * 100.0;
}

double ConsumptionMeter::fuelTripL() const {
    return model_.electric() ? 0.0 : fuelG_ / 1000.0 / model_.fuel().densityKgPerL;
}

double ConsumptionMeter::fuelAvgLper100km() const {
    if (distanceM_ < kMovingForAverageM) return 0.0;
    return fuelTripL() / tripKm() * 100.0;
}

double ConsumptionMeter::co2GPerKm() const {
    if (distanceM_ < kMovingForAverageM || model_.electric()) return 0.0;
    return fuelG_ * model_.fuel().co2GPerG / tripKm();
}

double ConsumptionMeter::oilTripMl() const { return oilG_ / kOilDensityKgPerL; }

double ConsumptionMeter::oilMlPer1000km() const {
    if (distanceM_ < kMovingForAverageM) return 0.0;
    return oilTripMl() / tripKm() * 1000.0;
}

double ConsumptionMeter::energyKwhPer100km() const {
    if (speedMs_ < kMovingMs) return 0.0;
    return batteryW_ / speedMs_ / 36.0;
}

double ConsumptionMeter::energyAvgKwhPer100km() const {
    if (distanceM_ < kMovingForAverageM) return 0.0;
    return energyTripKwh() / tripKm() * 100.0;
}

double ConsumptionMeter::lambdaAverage() const {
    if (fuelG_ <= 0.0 || model_.electric()) return 0.0;
    return airG_ / fuelG_ / model_.fuel().stoichiometricAfr;
}

double ConsumptionMeter::lambdaNow() const {
    if (fuelGps_ <= 0.0 || model_.electric()) return 0.0;
    return airGps_ / fuelGps_ / model_.fuel().stoichiometricAfr;
}

double brakeSpecificFuel(const Powertrain& p, double rpm, double brakeNm) {
    if (p.electric || brakeNm <= 0.0) return 0.0;
    ConsumptionModel model;
    model.configure(p);
    const double friction = frictionTorqueNm(p, rpm);
    const double fullGross = fullLoadTorqueNm(p, rpm, steadyBoost(p, rpm)) + friction;
    const double kw = brakeNm * rpm * 2.0 * kPi / 60.0 / 1000.0;
    return model.fuelGps(rpm, brakeNm, friction, fullGross) * 3600.0 / kw;
}

FuelMapSummary fuelMapSummary(const Powertrain& p) {
    FuelMapSummary out;
    if (p.electric) return out;
    ConsumptionModel model;
    model.configure(p);
    const double idle = idleRpmOf(p);
    const double redline = redlineRpmOf(p);
    const double perBar = kAtmosphere * static_cast<double>(p.engine.displacementCc) * 1e-6 / (4.0 * kPi);
    for (double rpm = idle; rpm <= redline; rpm += 100.0) {
        const double friction = frictionTorqueNm(p, rpm);
        const double full = fullLoadTorqueNm(p, rpm, steadyBoost(p, rpm));
        const double kw = rpm * 2.0 * kPi / 60.0 / 1000.0;
        for (double share = 0.05; share <= 1.0001; share += 0.05) {
            const double brake = share * full;
            const double gPerKwh = model.fuelGps(rpm, brake, friction, full + friction) * 3600.0 / (brake * kw);
            if (out.bestGPerKwh == 0.0 || gPerKwh < out.bestGPerKwh) {
                out.bestGPerKwh = gPerKwh;
                out.bestRpm = rpm;
                out.bestBmepBar = brake / perBar;
            }
        }
    }
    const PowerSummary peak = powerSummary(p);
    out.peakPowerGPerKwh = brakeSpecificFuel(p, peak.peakPowerRpm, peak.peakTorqueRpm == peak.peakPowerRpm ? peak.peakTorqueNm : torqueCurveNm(p, peak.peakPowerRpm));
    return out;
}

}  // namespace revforge
