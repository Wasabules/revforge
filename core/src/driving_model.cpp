#include "revforge/driving_model.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kRatios[] = {3.42, 1.81, 1.28, 0.98, 0.77};
constexpr int kGears = 5;
constexpr double kFinalDrive = 3.94;
constexpr double kWheelRadius = 0.285;
constexpr double kMassKg = 1100.0;
constexpr double kDrivelineEfficiency = 0.9;
constexpr double kEngineBrakeNm = 16.0;
constexpr double kThrottlePerKmh = 0.09;
constexpr double kPedalUpPerS = 1.6;
constexpr double kPedalDownPerS = 3.5;
constexpr double kBrakeMarginKmh = 4.0;
constexpr double kBrakeRatePerS = 1.2;
constexpr double kShiftSec = 0.42;
constexpr double kClutchOutPerS = 9.0;
constexpr double kClutchInPerS = 3.0;
constexpr double kClutchLaunchPerS = 0.75;
constexpr double kLaunchEndKmh = 14.0;
constexpr double kLaunchClutch = 0.85;
constexpr double kLaunchRpm = 1300.0;
constexpr double kMinShiftGapSec = 2.5;
constexpr double kDownshiftRpm = 1450.0;
constexpr double kNeutralBelowKmh = 8.0;
constexpr double kRpmTauS = 0.09;
constexpr double kRpmUpPerS = 3800.0;
constexpr double kRpmDownPerS = 2600.0;
// Au point mort, le moteur est libre : il prend et perd ses tours bien plus vite.
constexpr double kFreeRevUpPerS = 12000.0;
constexpr double kFreeRevDownPerS = 5000.0;
constexpr double kFreeRevTauS = 0.05;
constexpr double kLimiterDrop = 180.0;

double approach(double from, double to, double maxStep) {
    const double delta = to - from;
    return std::fabs(delta) <= maxStep ? to : from + maxStep * (delta > 0 ? 1 : -1);
}

double coerceIn(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** Courbe de couple d'un 1.4 atmosphérique. */
double torqueAt(double r) {
    const double x = coerceIn((r - 3200.0) / 3500.0, -1.0, 1.0);
    return 118.0 * (1.0 - 0.32 * x * x);
}

double resistance(double speed) {
    return 0.5 * 1.2 * 0.68 * speed * speed + (speed > 0.1 ? 0.012 * kMassKg * 9.81 : 0.0);
}

}  // namespace

void DrivingModel::step(double dt) {
    // Boucle de kLoopSec secondes : ville, feu, voie rapide, dépassement, arrêt.
    static const Leg legs[] = {
        {0.0, 0.0, 0.0},
        {5.0, 50.0, 0.55},
        {20.0, 50.0, 0.4},
        {26.0, 0.0, 0.0},
        {36.0, 112.0, 1.0},
        {62.0, 90.0, 0.35},
        {76.0, 128.0, 1.0},
        {92.0, 0.0, 0.0},
    };
    clock_ += dt;
    cycleSec_ = std::fmod(clock_, kLoopSec);
    const Leg* leg = &legs[0];
    for (const Leg& candidate : legs) {
        if (candidate.fromSec <= cycleSec_) leg = &candidate;
    }
    const double speed = speedKmh_ / 3.6;

    drive(*leg, speed, dt);
    selectGear(leg->targetKmh > 0.0, dt);
    moveCar(speed, dt);
    turnEngine(dt);
}

void DrivingModel::stepPedal(double dt, double pedal, double brakePedal) {
    clock_ += dt;
    const double speed = speedKmh_ / 3.6;
    const double wantedThrottle = shiftLeft_ > 0.0 ? 0.0 : coerceIn(pedal, 0.0, 1.0);
    throttle_ = approach(throttle_, wantedThrottle, (wantedThrottle > throttle_ ? kPedalUpPerS * 2.0 : kPedalDownPerS * 2.0) * dt);
    brake_ = approach(brake_, coerceIn(brakePedal, 0.0, 1.0) * 0.8, kBrakeRatePerS * 3.0 * dt);
    selectGear(pedal > 0.02, dt);
    moveCar(speed, dt);
    turnEngine(dt);
}

void DrivingModel::stepNeutral(double dt, double pedal) {
    clock_ += dt;
    const double speed = speedKmh_ / 3.6;
    gear_ = 0;
    pendingGear_ = 0;
    shiftLeft_ = 0.0;
    brake_ = approach(brake_, speedKmh_ > 0.0 ? 0.4 : 0.0, kBrakeRatePerS * 3.0 * dt);
    clutch_ = approach(clutch_, 0.0, kClutchOutPerS * dt);
    const double wanted = limiterCut_ ? 0.0 : coerceIn(pedal, 0.0, 1.0);
    throttle_ = approach(throttle_, wanted, (wanted > throttle_ ? 9.0 : 14.0) * dt);
    moveCar(speed, dt);

    const double idle = kIdleRpm + 9.0 * std::sin(clock_ * 2.1) + 4.0 * std::sin(clock_ * 5.3);
    // Le rupteur coupe l'injection au régime maximal, puis la rend un peu plus bas.
    const double target = idle + throttle_ * (kRedlineRpm + 400.0 - idle);
    const double rate = target > rpm_ ? kFreeRevUpPerS : kFreeRevDownPerS;
    const double glide = (target - rpm_) * std::min(1.0, dt / kFreeRevTauS);
    rpm_ = coerceIn(rpm_ + coerceIn(glide, -rate * dt, rate * dt), std::min(idle, rpm_), kRedlineRpm);
    if (rpm_ >= kRedlineRpm) {
        limiterCut_ = true;
    } else if (rpm_ < kRedlineRpm - kLimiterDrop) {
        limiterCut_ = false;
    }
}

void DrivingModel::drive(const Leg& leg, double speed, double dt) {
    const double error = leg.targetKmh - speedKmh_;
    double wantedThrottle;
    if (shiftLeft_ > 0.0 || leg.targetKmh == 0.0) {
        wantedThrottle = 0.0;
    } else {
        const double hold = resistance(speed) * kWheelRadius / driveRatio() / std::max(torqueAt(rpm_), 1.0);
        wantedThrottle = coerceIn(hold + error * kThrottlePerKmh, 0.0, std::max(leg.eagerness, hold));
    }
    const double pedalRate = wantedThrottle > throttle_ ? kPedalUpPerS : kPedalDownPerS;
    throttle_ = approach(throttle_, wantedThrottle, pedalRate * dt);

    const double wantedBrake = error < -kBrakeMarginKmh ? std::min(-error * 0.03, 0.42) : 0.0;
    const double comfort = (leg.targetKmh == 0.0 && speedKmh_ < 12.0) ? 0.3 : wantedBrake;
    brake_ = approach(brake_, comfort, kBrakeRatePerS * dt);
}

void DrivingModel::selectGear(bool wantsToMove, double dt) {
    sinceShift_ += dt;
    if (shiftLeft_ > 0.0) {
        shiftLeft_ -= dt;
        clutch_ = approach(clutch_, 0.0, kClutchOutPerS * dt);
        if (shiftLeft_ <= 0.0) gear_ = pendingGear_;
        return;
    }
    if (gear_ == 0 && wantsToMove) {
        // Démarrage : l'embrayage patine, le moteur prend des tours.
        gear_ = 1;
        clutch_ = 0.0;
    } else if (gear_ > 0 && !wantsToMove && speedKmh_ < kNeutralBelowKmh) {
        gear_ = 0;
    } else if (gear_ >= 1 && gear_ < kGears && throttle_ > 0.05 && rpm_ > upshiftRpm() && settled()) {
        // Pied levé, on ne monte pas un rapport : on se prépare à ralentir.
        beginShift(gear_ + 1);
    } else if (gear_ > 1 && rpm_ < kDownshiftRpm && clutch_ > 0.95) {
        beginShift(gear_ - 1);
    } else if (gear_ > 1 && throttle_ > 0.85 && rpm_ < 2600.0 && rpmInGear(gear_ - 1) < 4800.0 && settled()) {
        // Rétrogradage franc pour dépasser, s'il ne fait pas sur-régimer.
        beginShift(gear_ - 1);
    }
    // Au démarrage l'embrayage patine tant que la voiture n'a pas assez de vitesse.
    const bool launching = gear_ == 1 && speedKmh_ < kLaunchEndKmh;
    const double engage = launching ? kClutchLaunchPerS : kClutchInPerS;
    const double fullClutch = launching ? kLaunchClutch : 1.0;
    clutch_ = gear_ == 0 ? approach(clutch_, 0.0, kClutchOutPerS * dt) : approach(clutch_, fullClutch, engage * dt);
}

bool DrivingModel::settled() const { return clutch_ > 0.95 && sinceShift_ > kMinShiftGapSec; }

void DrivingModel::beginShift(int next) {
    pendingGear_ = next;
    shiftLeft_ = kShiftSec;
    sinceShift_ = 0.0;
}

double DrivingModel::upshiftRpm() const { return 2300.0 + 3400.0 * throttle_ * throttle_; }

void DrivingModel::moveCar(double speed, double dt) {
    double drive = 0.0;
    if (gear_ != 0) {
        if (throttle_ > 0.02) {
            drive = throttle_ * torqueAt(rpm_) * driveRatio() * kDrivelineEfficiency * clutch_ / kWheelRadius;
        } else {
            drive = -kEngineBrakeNm * (rpm_ / 3000.0) * driveRatio() * clutch_ / kWheelRadius;
        }
    }
    const double stopping = brake_ * 9.81 * kMassKg;
    const double force = drive - resistance(speed) - (speed > 0.0 ? stopping : 0.0);
    const double next = std::max(speed + force / kMassKg * dt, 0.0);
    speedKmh_ = next * 3.6;
}

void DrivingModel::turnEngine(double dt) {
    const double idle = kIdleRpm + 9.0 * std::sin(clock_ * 2.1) + 4.0 * std::sin(clock_ * 5.3);
    const double free = idle + throttle_ * (kRedlineRpm - idle) * 0.85;
    const double tied = std::max(rpmInGear(gear_), idle);
    double wanted = gear_ == 0 ? free : free + (tied - free) * clutch_;
    if (gear_ == 1 && speedKmh_ < kLaunchEndKmh && throttle_ > 0.02) {
        wanted = std::max(wanted, kLaunchRpm + 1200.0 * throttle_);
    }
    const double rate = wanted > rpm_ ? kRpmUpPerS : kRpmDownPerS;
    const double glide = (wanted - rpm_) * std::min(1.0, dt / kRpmTauS);
    rpm_ = coerceIn(rpm_ + coerceIn(glide, -rate * dt, rate * dt), std::min(idle, rpm_), kRedlineRpm);
}

double DrivingModel::rpmInGear(int gear) const {
    if (gear == 0) return 0.0;
    return speedKmh_ / 3.6 * kRatios[gear - 1] * kFinalDrive * 60.0 / (2.0 * kPi * kWheelRadius);
}

double DrivingModel::driveRatio() const {
    return gear_ == 0 ? kRatios[0] * kFinalDrive : kRatios[gear_ - 1] * kFinalDrive;
}

}  // namespace revforge
