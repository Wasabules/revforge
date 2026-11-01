#pragma once

#include "revforge/powertrain.hpp"

namespace revforge {

/**
 * Ce que ce réglage donnerait à un sonomètre, mesuré comme sur un banc en jouant le son : des estimations,
 * à comparer à ce qu'on trouve publié (cartes grises, essais de presse).
 */
struct SoundSheet {
    enum Item { Standstill, OutsideFull, CabinIdle, CabinFull, Count };
    /** ISO 5130 : à vide, à 50 cm de la sortie et à 45° de son axe, au régime de l'essai. */
    float standstillDb = 0.0f;
    /** Plein gaz aux trois quarts du régime, à 5 m derrière la voiture. */
    float outsideFullDb = 0.0f;
    /** À la place du conducteur, au ralenti, puis plein gaz. */
    float cabinIdleDb = 0.0f;
    float cabinFullDb = 0.0f;

    float level(int item) const {
        switch (item) {
            case Standstill: return standstillDb;
            case OutsideFull: return outsideFullDb;
            case CabinIdle: return cabinIdleDb;
            case CabinFull: return cabinFullDb;
            default: return 0.0f;
        }
    }
};

SoundSheet soundSheet(const Powertrain& powertrain);

/** Le régime de l'essai sur place : ISO 5130 (R51.03, annexe 3) : 75 % de S sous 5000 tr/min, 3750 jusqu'à 7500, puis 50 % de S. */
double standstillTestRpm(const Powertrain& powertrain);

/** Ce que retient la ligne de cet échappement dans une bande, par rapport à un tube droit (dB). */
float lineLossDb(const Powertrain& powertrain, double fromHz, double toHz);

}  // namespace revforge
