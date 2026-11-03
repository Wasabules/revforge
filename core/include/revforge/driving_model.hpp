#pragma once

#include "revforge/consumption.hpp"
#include "revforge/driver_scenarios.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/shift_logic.hpp"

namespace revforge {

/**
 * La voiture et son conducteur, à partir du moteur réglé : couple, inertie, embrayage,
 * boîte, motricité des pneus. Régime et vitesse découlent de ces forces, ils ne sont
 * jamais imposés.
 *
 * Quatre façons de la conduire :
 *  - `step` : le pilote automatique suit un trajet (`setDriveScenario`) ;
 *  - `stepPedal` : on tient l'accélérateur et le frein, boîte automatique ou manuelle ;
 *  - `stepNeutral` : au point mort, pour les coups de gaz, jusqu'au rupteur ;
 *  - `stepOff` : contact coupé, le moteur finit de tourner sur son élan.
 *
 * Le calcul avance par pas fixes : le résultat ne dépend pas de la durée demandée.
 */
class DrivingModel {
public:
    static constexpr double kLoopSec = 120.0;

    DrivingModel();
    explicit DrivingModel(const Powertrain& powertrain);

    /** Change de moteur ou de voiture sans repartir de zéro. */
    void setPowertrain(const Powertrain& powertrain);

    void step(double dt);
    void stepPedal(double dt, double pedal, double brakePedal);
    void stepNeutral(double dt, double pedal);
    void stepOff(double dt);

    /** Lance le démarreur : quelques tours sans combustion, puis le moteur prend. */
    void crank();
    /** Moteur à l'arrêt, sans passer par son élan. */
    void halt();

    /** Comment l'automatique choisit ses rapports. Sans effet sur une boîte manuelle ni sur une voiture électrique. */
    void setShiftMode(ShiftMode mode) { shiftMode_ = mode; }
    ShiftMode shiftMode() const { return shiftMode_; }
    /** Le trajet du pilote automatique. Il change en roulant : la voiture garde sa vitesse, et prend le trajet en route. */
    void setDriveScenario(DriveScenario scenario);
    DriveScenario driveScenario() const { return scenario_; }
    /** L'étape en cours du trajet, et la vitesse qu'elle vise. */
    int legIndex() const { return legIndex_; }
    LegKind legKind() const { return scenarioOf(scenario_).legs[legIndex_].kind; }
    double legTarget() const { return legTarget_; }
    /** Le pied est au plancher, et la voiture n'atteint pas la consigne : elle n'en a pas la force. */
    bool underpowered() const { return underpowered_; }
    /** La pente de la route, en % : positive en montée. Le trajet la remplace tant que le pilote conduit. */
    void setRoadGrade(double percent);
    double roadGradePct() const { return grade_ * 100.0; }
    /** Une remorque : autant de masse à tirer et à freiner, plus son air et ses pneus. */
    void setTrailer(double kg) { trailerKg_ = kg; }
    /** Le conducteur dit qu'il est en virage : les modes sportifs tiennent leur rapport. 0 : tout droit. */
    void setCornerRadius(double metres) { cornerRadius_ = metres; }

    void setManual(bool manual);
    bool manual() const { return manual_; }
    /** 0 : relâchée, 1 : enfoncée à fond. */
    void setClutchPedal(double pedal) { clutchPedal_ = pedal < 0.0 ? 0.0 : (pedal > 1.0 ? 1.0 : pedal); }
    /** Boîte manuelle : +1 ou -1. Refusé s'il ferait sur-régimer le moteur. Sous le point mort : la marche arrière. */
    void requestShift(int direction);
    /**
     * Assisté (par défaut) : passer un rapport actionne aussi l'embrayage. Sinon, seule la pédale l'ouvre : passer
     * sans débrayer gratte, sauf si le régime est déjà au bon niveau.
     */
    void setClutchAssist(bool assisted) { clutchAssist_ = assisted; }
    /** La boîte a cédé : la remplacer. */
    void repairGearbox();

    double speedKmh() const { return speedMs_ * 3.6; }
    double rpm() const;
    /** 0 = point mort, -1 = marche arrière. Le levier a déjà rejoint le nouveau rapport pendant la synchronisation. */
    int gear() const { return gear_; }
    int gears() const { return gearing_.gears; }
    const GearboxSpec& box() const { return gearing_.box; }
    const Gearing& gearing() const { return gearing_; }
    /** Le rapport est vraiment en prise : les crabots sont entrés. */
    bool meshed() const { return meshed_; }
    /** Arbre primaire, et arbre de sortie avant le pont (tr/min). */
    double inputRpm() const;
    double outputRpm() const;
    /** L'état de la boîte pour son bruit : à donner à `EngineSound::setGearbox`. */
    GearboxDrive gearboxDrive() const;
    /** Couple à l'entrée de la boîte, rapporté à ce qu'elle admet : au-delà de 1, elle s'use vite. */
    double overload() const { return overload_; }
    /** Part de la vie de la boîte déjà consommée en surcharge, 0 à 1. */
    double wear() const { return wear_; }
    bool broken() const { return broken_; }
    /** Le baladeur force sur les crabots : écart de vitesses (rad/s). */
    double grinding() const { return grinding_; }
    /** Étape du passage en cours : 0 aucune, 1 débrayage, 2 sortie, 3 course, 4 synchronisation, 5 engagement, 6 rembrayage, 7 relais. */
    int shiftStep() const { return static_cast<int>(step_); }
    /** Le rapport visé par le passage en cours : 0 aucun ou point mort, -1 marche arrière. Pour l'affichage. */
    int pendingGear() const { return step_ == ShiftStep::None ? 0 : pendingGear_; }
    /** Écart de vitesses sur le cône pendant la synchronisation (rad/s). Pour l'affichage. */
    double coneSlip() const { return coneSlip_; }
    double throttle() const { return throttle_; }
    double cycleSec() const { return cycleSec_; }

    /** Remplissage du moteur : 0 pied levé, 1 plein gaz. C'est lui qui fait le son. */
    double load() const { return manifold_; }
    /** Calé : plus rien ne brûle, il faut relancer le démarreur. */
    bool stalled() const { return stalled_; }
    /** Trop bas en prise : le moteur broute. */
    bool lugging() const { return lugging_; }
    /** Le démarreur est alimenté, et son pignon est engagé dans la couronne. */
    bool starterPowered() const { return crankLeft_ > 0.0 || overrunLeft_ > 0.0; }
    bool starterMeshed() const { return (crankLeft_ > 0.0 && pullInLeft_ <= 0.0) || meshHoldLeft_ > 0.0; }
    /** Couple transmis par les dents, rapporté au couple de calage du démarreur. */
    double starterLoad() const { return starterLoad_; }
    /** Pression avant le papillon, en part de `boostBar` : c'est celle du vrai turbo, au retard près. */
    double boost() const { return boost_; }
    /** L'arbre, les pressions, les soupapes : tout est dans `TurboState`. Rien de neuf sur un moteur sans turbo. */
    const TurboCharger& turbo() const { return turbo_; }
    TurboDrive turboDrive() const { return turbo_.drive(); }
    /** L'anti-lag travaille en ce moment : pied levé, moteur lancé, sur un turbo à essence. */
    bool antiLagActive() const { return antiLag_.active; }
    /** Le couple que le retard et les cylindres coupés retirent à ce que donnerait cette charge d'air (Nm). */
    double antiLagLostNm() const { return antiLagLostNm_; }
    /** Le compresseur mécanique : mêmes pressions, en unités physiques. Rien de neuf sur un moteur sans compresseur. */
    bool supercharged() const { return superchargerEngine_; }
    const Supercharger& supercharger() const { return supercharger_; }
    SuperchargerDrive superchargerDrive() const { return supercharger_.drive(); }
    /** Couple au vilebrequin, négatif en frein moteur (Nm). */
    double torqueNm() const { return torque_; }
    double powerKw() const;
    /** Part du couple que l'embrayage transmet : 0 ouvert, 1 fermé. */
    double clutch() const { return clutch_; }
    /** Puissance que l'embrayage dissipe en patinant (kW). */
    double clutchSlipKw() const { return clutchSlipKw_; }
    /** Couple que l'embrayage fait passer (Nm) : sa capacité s'il patine ; s'il colle, ce que demande l'arbre primaire. */
    double clutchTorqueNm() const {
        if (locked_) return gear_ == 0 ? torque_ * gearing_.box.inputInertia / (inertia_ + gearing_.box.inputInertia) : torque_;
        const double capacity = clutchCapacity_ * clutch_;
        return omega_ >= inputOmega_ ? capacity : -capacity;
    }
    /** Tours par minute des roues motrices. */
    double wheelRpm() const;
    /** Glissement des pneus moteurs (m/s). */
    double wheelSlip() const { return wheelSpeedMs() - speedMs_; }
    /** Combustion coupée : rupteur, ou contact coupé. */
    bool cut() const { return cut_; }
    bool shifting() const { return step_ != ShiftStep::None; }
    bool cranking() const { return crankLeft_ > 0.0; }
    bool stopped() const { return omega_ <= 0.0; }
    double idleRpm() const { return idle_; }
    int powertrainCylinders() const { return powertrain_.engine.cylinders; }
    double redlineRpm() const { return redline_; }

    /** Essence, huile ou énergie : une lecture de ce que fait le moteur. Rien n'en revient vers la physique. */
    const ConsumptionMeter& consumption() const { return consumption_; }
    void resetTrip() { consumption_.resetTrip(); }

private:
    enum class Driver { Pilot, Pedal, Neutral, Off };
    enum class ShiftStep { None, Declutch, PullOut, Flight, Sync, Engage, Reclutch, Handover };

    void advance(double dt, Driver driver, double pedal, double brakePedal);
    void substep(Driver driver, double pedal, double brakePedal);
    void pilot(double& pedal, double& brakePedal);
    void selectGear(bool wantsToMove);
    void autoGear(bool wantsToMove);
    void chooseGear();
    void updateDemand();
    double roadExtraN() const;
    double slopeShare() const;
    /** Une manuelle n'a pas de mode, et le pilote automatique ne se sert pas des palettes. */
    ShiftMode activeMode() const { return manual_ || (shiftMode_ == ShiftMode::Paddles && piloted_) ? ShiftMode::Normal : shiftMode_; }
    void beginShift(int next);
    void advanceShift();
    void enterStep(ShiftStep next);
    void leaveOldGear();
    void engageGear();
    void abandonShift();
    void trackOverload(double torque);
    void updateClutch(bool wantsToMove);
    void updateEngine(double pedal, bool off);
    void integrate(double brakePedal);
    void drivenStep(double tyre, double wheelInertia);
    void freeStep(double tyre, double wheelInertia);
    void converterStep(double tyre, double wheelInertia);
    void converterTorque(double& pump, double& turbine) const;
    bool settled() const;
    bool assisted() const { return !manual_ || clutchAssist_; }
    bool dogBox() const { return gearing_.box.kind == GearboxKind::Dog || gearing_.box.kind == GearboxKind::Sequential; }
    bool powerShift() const { return gearing_.box.kind == GearboxKind::DualClutch || gearing_.box.kind == GearboxKind::Converter; }
    bool shiftLiftsPedal() const;
    double stepSeconds(ShiftStep step) const;
    double inputDrag() const;
    double upshiftRpm() const;
    double rpmInGear(int gear) const;
    double ratio(int gear) const;
    double compressionLoadNm() const;
    void stepTurbo(double rpmNow, bool burning);
    void updateAntiLag(double throttle, double rpmNow, bool off);
    void stepSupercharger(double rpmNow);
    double bmepPumping(double rpm) const;
    double wheelSpeedMs() const { return wheelOmega_ * radius_; }
    void meterConsumption();

    Powertrain powertrain_;
    Gearing gearing_;
    double radius_ = 0.28;
    double idle_ = 850.0;
    double redline_ = 6200.0;
    double inertia_ = 0.15;
    double clutchCapacity_ = 300.0;
    double efficiency_ = 0.9;
    double shiftPace_ = 0.3;
    double converterK_ = 10.0;
    double rippleNm_ = 0.0;
    double rippleIdleNm_ = 0.0;
    double manifoldTau_ = 0.1;
    double peakTorqueRpm_ = 3500.0;
    ShiftMap shiftMap_;
    ShiftMode shiftMode_ = ShiftMode::Normal;
    DriveScenario scenario_ = DriveScenario::City;
    double scenarioStart_ = 0.0;
    int legIndex_ = 0;
    double legTarget_ = 0.0;
    double strain_ = 0.0;
    bool underpowered_ = false;
    bool piloted_ = false;
    double demand_ = 0.0;
    double lastThrottle_ = 0.0;
    // La route : tan de la pente, sa consigne, le virage, la remorque.
    double grade_ = 0.0;
    double gradeTarget_ = 0.0;
    double cornerRadius_ = 0.0;
    double trailerKg_ = 0.0;

    double pending_ = 0.0;
    double clock_ = 0.0;
    double cycleSec_ = 0.0;

    // Moteur
    double omega_ = 0.0;
    double throttle_ = 0.0;
    double manifold_ = 0.0;
    double boost_ = 0.0;
    TurboCharger turbo_;
    bool turboEngine_ = false;
    bool antiLagEngine_ = false;
    AntiLagCommand antiLag_;
    double antiLagLostNm_ = 0.0;
    Supercharger supercharger_;
    bool superchargerEngine_ = false;
    double torque_ = 0.0;
    bool cut_ = false;
    bool limiterCut_ = false;
    double limiterDrop_ = 0.04;
    unsigned limiterSeed_ = 0x2F6E2B1u;
    double cutHeld_ = 0.0;
    double crankLeft_ = 0.0;
    bool catching_ = false;
    StarterSpec starter_;
    double pullInLeft_ = 0.0;
    double meshHoldLeft_ = 0.0;
    double starterLoad_ = 0.0;
    double overrunLeft_ = 0.0;
    double crankAngle_ = 0.0;
    bool stalled_ = false;
    bool lugging_ = false;
    double fastIdle_ = 0.0;

    // Transmission
    int gear_ = 0;
    int pendingGear_ = 0;
    int queuedShift_ = 0;
    ShiftStep step_ = ShiftStep::None;
    double stepLeft_ = 0.0;
    double stepHeld_ = 0.0;
    double shiftEffort_ = 1.0;
    bool meshed_ = false;
    bool fromNeutral_ = false;
    bool ignitionCut_ = false;
    double torqueScale_ = 1.0;
    double inputOmega_ = 0.0;
    double twist_ = 0.0;
    double gearTorque_ = 0.0;
    bool drivingFlank_ = true;
    double coneSlip_ = 0.0;
    double coneTorque_ = 0.0;
    double grinding_ = 0.0;
    double clutchSlipKw_ = 0.0;
    double impacts_ = 0.0;
    double selectorClicks_ = 0.0;
    double overload_ = 0.0;
    double wear_ = 0.0;
    bool broken_ = false;
    bool clutchAssist_ = true;
    double sinceShift_ = 100.0;
    double clutch_ = 0.0;
    double clutchPedal_ = 0.0;
    bool locked_ = false;
    bool manual_ = false;
    bool downshifting_ = false;

    // Consommation : ce que la combustion a demandé à ce pas, et le compteur qui l'intègre.
    ConsumptionMeter consumption_;
    ConsumptionInput burnt_;

    // Voiture
    double speedMs_ = 0.0;
    double wheelOmega_ = 0.0;
    double brake_ = 0.0;
    double acceleration_ = 0.0;
};

}  // namespace revforge
