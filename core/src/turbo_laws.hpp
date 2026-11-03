#pragma once

#include "revforge/turbo.hpp"

/**
 * Les lois du turbo simple (turbo.cpp), pour qui en assemble plusieurs : twin_turbo.cpp. Ce sont les mêmes fonctions et
 * les mêmes constantes, appelées par un renvoi ajouté à la fin de turbo.cpp : rien n'y est recopié, et une loi changée
 * là-bas change ici.
 */
namespace revforge::turbo_laws {

struct Constants {
    double atmosphere;
    double ambientK;
    double airR;
    double airCp;
    double gasCp;
    double volumetric;
    double ratedShare;
    double ductLossAtRated;
    double ductDampedShare;
    double pulsePa;
    double heatUpS;
    double coolDownS;
    double outletS;
    double coolerMassS;
    double coolerRestS;
    double wastegateBand;
    double wastegateS;
    double wastegateLeadS;
    double blowOffS;
    double blowOffVacuumPa;
    double blowOffVacuumSpanPa;
    double blowOffOpensAbovePa;
    double blowOffDischarge;
    double holdX;
    double turbineEta;
    double ratedTurbineShare;
    double ratedTurbineShareDiesel;
    double minTipSpeed;
};

const Constants& constants();
double compressorHead(const TurboSpec& spec, double inletK, double flow, double tipSpeed, double* x = nullptr);
double compressorEta(double x);
double pressureRatio(double head, double inletK);
/** La hauteur isentropique (J/kg) qui donne ce rapport de pression. */
double headForRatio(double ratio, double inletK);
double inletTemperature(const TurboSpec& spec, double rpm);
double fuelPerAir(const TurboSpec& spec, double fill, bool burning);
double exhaustTemperature(const TurboSpec& spec, double intakeK, double rpm, double fill, double fuel);
double coolerOutlet(double hotK, double flow, double rated, double ambientK);
double turbinePower(const TurboSpec& spec, double flow, double inletK, double inPa, double outPa, double omega, double pulsePa);
double bearingPower(const TurboSpec& spec, double omega);
double windagePower(const TurboSpec& spec, double tipSpeed);
double tipSpeedFor(const TurboSpec& spec, double inletK, double flow, double head);

}  // namespace revforge::turbo_laws
