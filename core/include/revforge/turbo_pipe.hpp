#pragma once

namespace revforge {

/**
 * Le tube entre les cylindres et la turbine, là où la calibration en compte 400 mm.
 *
 * Seul l'écart à cette référence existe ici : à 400 mm, `active` est faux et aucun de ces calculs ne s'exécute. Le reste
 * découle de la géométrie du tube : un volume à remplir, une paroi qui prend la chaleur du gaz, une masse de gaz à
 * renouveler, des bouffées lissées. Aucune constante de retard.
 */
struct TurboPipeSpec {
    bool active = false;
    double extraM = 0.0;             // longueur en plus de la référence, négative sur un tube plus court
    int cylinders = 4;
    double diameterM = 0.05;
    double surfaceM2 = 0.0;          // la paroi ajoutée, ou retirée
    double volumeM3 = 0.0;           // tout le tube
    double referenceVolumeM3 = 0.0;  // celui qu'aurait un tube de 400 mm
    double extraVolumeM3 = 0.0;      // ce qui reste à remplir en plus : rien sur un tube plus court
    double wallJPerK = 1.0;          // masse thermique de la paroi ajoutée
    double frictionDrop = 0.0;       // perte de charge en plus, Pa par (kg/s)² ; négative sur un tube plus court
    double headShare = 1.0;          // chaleur à prendre avant la turbine, rapportée à la référence : sous 1 sur un tube plus court
};

/** Ce que le tube garde d'une milliseconde à l'autre. */
struct TurboPipeState {
    double wallK = 293.0;  // la paroi du tube ajouté
    double portK = 293.0;  // le gaz à l'entrée de ce tube
    double outK = 293.0;   // le gaz qui arrive à la turbine

    /** Le réglage vient de changer en marche : un tube déjà chaud, pas une pièce neuve qui sort de l'atelier. */
    void adopt(const TurboPipeSpec& pipe, double gas, double gasK);
    /** La température du gaz à la turbine (K). `targetK` : ce que brûle le moteur ; les deux lenteurs sont celles des parois de la référence. */
    double stepHeat(const TurboPipeSpec& pipe, double dt, double gas, double targetK, double pressurePa, double tauUpS, double tauDownS);
    /** La pression avant la turbine (Pa) : elle rejoint celle de l'ellipse à la vitesse où le volume se remplit. */
    double stepPressure(const TurboPipeSpec& pipe, double dt, double pressurePa, double wantedPa, double outPa, double gasK, double passage) const;
};

TurboPipeSpec turboPipeSpecFor(int pipeMm, double displacementM3, int cylinders);
/** La température à la turbine (K), paroi à l'équilibre : ce que comptent les courbes de la fiche. */
double turboPipeSteadyK(const TurboPipeSpec& pipe, double gas, double inletK);
/** Ce qui reste des bouffées, rapporté à un tube de 400 mm : le volume du tube est un filtre du premier ordre pour la cadence d'allumage. */
double turboPipePulseShare(const TurboPipeSpec& pipe, double rpm, double inPa, double outPa, double gasK, double passage);
/** Le débit qui sort du volume par la turbine et la wastegate, selon la loi de l'ellipse (kg/s). */
double turboPipeOutflow(double pressurePa, double outPa, double gasK, double passage);

}  // namespace revforge
