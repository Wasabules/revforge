#pragma once

#include <array>

#include "revforge/powertrain.hpp"

namespace revforge {

/**
 * Comment l'automatique choisit ses rapports. L'ordre compte : la position est la valeur envoyée par le site
 * (`SHIFT_MODES` dans web/src/lib/config.ts). Normal, le premier, reste la logique d'origine, au bit près.
 */
enum class ShiftMode {
    Normal,
    Eco,      // monte tôt, reste bas, rétrograde tard, ponte tôt
    Sport,    // tient les rapports, rétrograde au freinage, ne monte pas en virage
    Track,    // jusqu'au rupteur, passages les plus courts, rapport tenu au lever de pied
    Paddles,  // le conducteur décide (+/-) ; l'automatique ne fait que protéger le moteur
};

inline constexpr int kShiftModes = 5;

/** Ce qui distingue un mode, hors des points de passage. Aucune valeur n'est propre à une voiture. */
struct ShiftTraits {
    double gapSec;          // délai minimal entre deux passages choisis par la logique : le logiciel, pas la mécanique
    double demandDecaySec;  // la demande retient le pied le plus lourd, et l'oublie en ce temps : 0, aucune mémoire
    double pedalRateSec;    // une pédale qui s'enfonce vite compte d'avance : poids de sa vitesse (s)
    double kickdownPedal;   // pédale à partir de laquelle on rétrograde pour accélérer
    double reserve;         // le rapport suivant doit tirer autant de fois la résistance de la route : assez pour ne pas retomber sous kickdownPedal
    double pace;            // durée des passages, rapportée à celle de la boîte (voir docs/modele.md)
    int lockGear;           // pontage du convertisseur : à partir de ce rapport,
    double lockSlip;        // dès ce rapport de vitesses turbine/pompe,
    double lockThrottle;    // et sous cette ouverture du papillon
    double brakeDemand;     // au frein, la demande remonte au moins à ce niveau : le rapport reste pour la sortie
};

const ShiftTraits& shiftTraits(ShiftMode mode);

/**
 * Ce que la logique sait de la voiture : courbe de couple, rapports. Calculé une fois à chaque changement de moteur
 * ou de voiture, sans allocation. Tous les points de passage en découlent : aucune table de régimes par voiture.
 */
struct ShiftMap {
    static constexpr int kPoints = 48;

    double idle = 850.0;
    double redline = 6200.0;
    double peakTorqueRpm = 3500.0;
    double peakPowerRpm = 5500.0;
    double radius = 0.28;
    double efficiency = 0.9;
    int gears = 5;
    std::array<double, kMaxGears> overall{};
    // À pleine charge, le régime où le rapport suivant tire autant aux roues : monter plus tard fait perdre du temps.
    std::array<double, kMaxGears> crossing{};
    std::array<double, kPoints> torque{};  // couple à pleine charge, turbo stabilisé, du ralenti au régime maximal

    /** Couple à pleine charge à ce régime, lu sur la courbe (Nm). */
    double torqueAt(double rpm) const;
    /** L'écart entre ce rapport et le suivant : plus de 1. */
    double stepAt(int gear) const;
};

ShiftMap buildShiftMap(const Powertrain& powertrain, const Gearing& gearing);

/** Régime de passage au rapport supérieur, à cette demande (0 à 1). Eco < Normal < Sport <= Track, à toute demande. */
double upshiftPoint(const ShiftMap& map, ShiftMode mode, int gear, double demand);
/** Sous ce régime, on rétrograde quoi qu'il arrive : le moteur ne tire plus assez. */
double downshiftFloor(const ShiftMap& map, ShiftMode mode);
/** Au freinage, on rétrograde dès que le rapport inférieur laisse le moteur sous ce régime : 0, jamais. */
double brakingCeiling(const ShiftMap& map, ShiftMode mode, bool descending);

/** L'état de la voiture, tel que la logique le voit à cet instant. */
struct ShiftView {
    int gear = 1;
    double rpm = 0.0;          // le moteur
    double roadRpm = 0.0;      // ce que les roues imposent dans ce rapport : le convertisseur peut glisser entre les deux
    double throttle = 0.0;
    double demand = 0.0;       // le pied, sa vitesse et sa mémoire : voir ShiftTraits
    double speedKmh = 0.0;
    double sinceShift = 0.0;
    double slipMs = 0.0;       // glissement des pneus moteurs
    double accelMs2 = 0.0;
    double tractionN = 1e9;    // ce que les pneus moteurs peuvent transmettre
    double resistanceN = 0.0;  // route, pente, remorque
    bool coupled = false;      // embrayage collé, ou convertisseur
    bool braking = false;
    bool cornering = false;    // le scénario dit qu'on est en virage
    bool descending = false;
};

/** La décision : +1, -1 ou 0. Fonction pure : la mécanique du passage reste dans `DrivingModel`. */
int decideShift(const ShiftMap& map, ShiftMode mode, const ShiftView& view);

}  // namespace revforge
