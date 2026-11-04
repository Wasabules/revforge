#pragma once

#include "revforge/supercharger.hpp"

/**
 * Les lois du compresseur mécanique (supercharger.cpp), pour le twincharger, où il souffle dans un turbo au lieu du
 * moteur (twin_turbo.cpp). Un renvoi ajouté à la fin de supercharger.cpp : rien n'y est recopié.
 */
namespace revforge::supercharger_laws {

/** Ce que déplace le compresseur (kg/s) pour cette vitesse d'arbre et ces pressions. */
double compressorFlow(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa);
/** Puissance sur l'arbre (W), sans l'entraînement, et la température de sortie. */
double shaftPower(const SuperchargerSpec& spec, double omega, double inletPa, double inletK, double outletPa, double flow, double* outK);
/** Un orifice régularisé : débit volumique par unité de section, signé. */
double orificeSpeed(double across, double rho);
/** Coefficient de débit des soupapes du compresseur. */
double discharge();

}  // namespace revforge::supercharger_laws
