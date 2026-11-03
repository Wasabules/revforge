#pragma once

#include <cstdint>

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * L'anti-lag : pied levé, le calculateur continue d'envoyer de l'énergie à la turbine au lieu de couper l'injection.
 * Trois façons d'y arriver, qui ne diffèrent que par ce qu'elles font du carburant et de l'air : voir « L'anti-lag » dans
 * docs/modele.md. Tout le reste, la vitesse de l'arbre et la pression à la reprise, sort du bilan de puissance de `TurboCharger`.
 *
 * Sans effet sur un diesel : ni papillon à entrouvrir, ni étincelle à retarder, ni mélange à faire passer imbrûlé.
 */

/** Ce que le calculateur décide à ce pas. */
struct AntiLagCommand {
    AntiLag kind = AntiLag::Off;
    bool active = false;
};

/** Ce que le système fait de l'air et du carburant à ce pas, en unités physiques. */
struct AntiLagState {
    double airValve = 0.0;        // ouverture de la vanne d'air, 0 à 1
    double airFlow = 0.0;         // air envoyé au collecteur (kg/s)
    double surplusFuel = 0.0;     // carburant au-delà de la richesse 1 du cylindre (kg/s)
    double collectorHeatW = 0.0;  // chaleur libérée hors du travail du piston : fin de combustion ou collecteur (W)
    double evoRatio = 1.0;        // pression à l'ouverture de la soupape, rapportée à un allumage normal
};

/** Ce que le son doit savoir : tout vient du calcul, rien n'y est deviné. */
struct AntiLagDrive {
    bool active = false;
    AntiLag kind = AntiLag::Off;
    float evoRatio = 1.0f;
    float heatShare = 0.0f;    // chaleur au collecteur, rapportée à ce que brûle le moteur au point nominal
    float heatW = 0.0f;        // la même, en watts
    float ignition = 0.0f;     // 0 à 1 : le collecteur est-il assez chaud pour enflammer ce qui arrive ?
    float exhaustM3s = 0.0f;   // débit volumique du gaz, à sa température
};

/** Le papillon entrouvert par le calculateur, en part du remplissage. */
double antiLagOpening(AntiLag kind);
/** La part du couple qu'il reste pour cette charge d'air, retard et cylindres coupés compris. */
double antiLagTorqueShare(AntiLag kind);
/** Le rythme de la coupure : la part des explosions qui ont lieu, et si celle-ci en est une, dans l'ordre d'allumage. */
inline constexpr double kAntiLagFiredShare = 0.4;
inline constexpr bool antiLagFires(std::uint32_t event) { return (event % 5u) * 2u % 5u < 2u; }
/** Le collecteur enflamme-t-il ce qui lui arrive : un mélange s'allume seul au contact de gaz à 800-1100 K (ordre de grandeur). */
double antiLagIgnition(double collectorK);

}  // namespace revforge
