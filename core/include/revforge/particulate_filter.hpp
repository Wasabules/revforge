#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le filtre à particules : un monolithe à canaux bouchés en alternance, dont le gaz traverse les parois poreuses. La suie
 * s'y dépose en gâteau, la contre-pression croît avec elle (Darcy), et une régénération la brûle. Sur un diesel, le
 * calculateur la déclenche par post-injection ; sur une injection directe d'essence, elle se fait d'elle-même pendant les
 * coupures d'injection, quand l'oxygène arrive sur un filtre chaud. Voir « L'injection » dans docs/modele.md.
 */
struct ParticulateFilterSpec {
    bool diesel = true;
    double volumeL = 2.5;            // 1,25 fois la cylindrée
    double filtrationM2 = 2.0;       // surface des parois filtrantes : 0,8 m² par litre (200 cellules par pouce carré)
    double frontM2 = 0.0167;         // section frontale
    double wallM = 3.0e-4;           // épaisseur de paroi
    double wallPermeabilityM2 = 1e-12;
    double startGPerL = 7.0;         // le calculateur demande la régénération
    double endGPerL = 1.0;           // et l'arrête
    double heatJPerK = 2500.0;       // masse thermique du monolithe et de son enveloppe
    double displacementM3 = 0.002;   // le volume balayé en deux tours, pour le travail de pompage
};

ParticulateFilterSpec particulateFilterSpecOf(const EngineConfig& engine);

/** Viscosité du gaz d'échappement (Pa·s), loi de Sutherland de l'air. */
double exhaustViscosity(double kelvin);
/** Contre-pression (Pa) : parois et gâteau de suie (Darcy), plus l'entrée dans les canaux. */
double filterBackPressurePa(const ParticulateFilterSpec& spec, double sootG, double exhaustKgS, double exhaustK);
/** Couple de pompage que coûte cette contre-pression (Nm) : le piston chasse le gaz contre elle, Δp·V/4π. */
double filterPumpingNm(const ParticulateFilterSpec& spec, double backPressurePa);
/** Résistance acoustique du filtre, rapportée à ρc/S du tube : la part linéaire (Darcy) de sa perte de charge. */
double filterLineResistance(const ParticulateFilterSpec& spec, double sootG);
/** Suie que rejette le moteur, en g par kg de carburant : plus elle approche la limite de fumée, plus il en fume. */
double sootIndexGPerKg(bool diesel, double lambda, double smokeLambda);
/** Température des gaz à la sortie du moteur (K), d'après le rapport carburant/air : la part de l'énergie qui part en chaleur. */
double exhaustGasK(bool diesel, double intakeK, double fuelPerAir);

/** Ce que reçoit le filtre à ce pas. */
struct FilterInput {
    double exhaustKgS = 0.0;
    double exhaustK = 600.0;
    double fuelGps = 0.0;       // carburant brûlé, pour la suie
    double lambda = 1.0;
    double smokeLambda = 1.3;   // limite de fumée de ce moteur
    bool burning = true;        // une coupure d'injection envoie de l'air pur
    bool running = true;
};

class ParticulateFilter {
public:
    void configure(const ParticulateFilterSpec& spec);
    void step(const FilterInput& in, double dt);

    /** Suie retenue (g), et par litre de filtre. */
    double sootG() const { return sootG_; }
    double loadGPerL() const { return sootG_ / spec_.volumeL; }
    /** Remplissage, de la fin de la dernière régénération (0) à la demande de la suivante (1). */
    double loadShare() const;
    double backPressurePa() const { return backPressurePa_; }
    double temperatureK() const { return temperatureK_; }
    bool regenerating() const { return regenerating_; }
    /** Carburant de post-injection (g/s) : il chauffe le filtre, ne fait aucun couple, mais se paie. */
    double extraFuelGps() const { return extraFuelGps_; }
    const ParticulateFilterSpec& spec() const { return spec_; }
    /** Pour les essais : poser la suie. */
    void setSootG(double grams) { sootG_ = grams; }

private:
    ParticulateFilterSpec spec_;
    double sootG_ = 0.0;
    double temperatureK_ = 500.0;
    double backPressurePa_ = 0.0;
    double extraFuelGps_ = 0.0;
    bool regenerating_ = false;
};

}  // namespace revforge
