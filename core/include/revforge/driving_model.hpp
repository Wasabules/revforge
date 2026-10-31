#pragma once

namespace revforge {

/**
 * Une petite citadine et son conducteur : pédale, embrayage, cinq rapports.
 * Régime et vitesse découlent de ce que fait le conducteur, ils ne sont jamais
 * imposés, d'où de vraies montées en régime et de vrais passages de vitesses.
 *
 * Trois façons de la conduire :
 *  - `step` : la boucle de démonstration de 120 s (ville, feu, voie rapide, dépassement) ;
 *  - `stepPedal` : on tient l'accélérateur et le frein, la boîte reste automatique ;
 *  - `stepNeutral` : au point mort, pour les coups de gaz, jusqu'au rupteur.
 */
class DrivingModel {
public:
    static constexpr double kLoopSec = 120.0;
    static constexpr double kIdleRpm = 850.0;
    static constexpr double kRedlineRpm = 6200.0;

    void step(double dt);
    void stepPedal(double dt, double pedal, double brakePedal);
    void stepNeutral(double dt, double pedal);

    double speedKmh() const { return speedKmh_; }
    double rpm() const { return rpm_; }
    /** 0 = point mort. */
    int gear() const { return gear_; }
    double throttle() const { return throttle_; }
    double cycleSec() const { return cycleSec_; }

private:
    struct Leg {
        double fromSec;
        double targetKmh;
        double eagerness;
    };

    void drive(const Leg& leg, double speed, double dt);
    void selectGear(bool wantsToMove, double dt);
    void beginShift(int next);
    bool settled() const;
    double upshiftRpm() const;
    void moveCar(double speed, double dt);
    void turnEngine(double dt);
    double rpmInGear(int gear) const;
    double driveRatio() const;

    double speedKmh_ = 0.0;
    double rpm_ = kIdleRpm;
    int gear_ = 0;
    double throttle_ = 0.0;
    double cycleSec_ = 0.0;
    double brake_ = 0.0;
    double clutch_ = 0.0;
    double shiftLeft_ = 0.0;
    int pendingGear_ = 0;
    double clock_ = 0.0;
    double sinceShift_ = 0.0;
    bool limiterCut_ = false;
};

}  // namespace revforge
