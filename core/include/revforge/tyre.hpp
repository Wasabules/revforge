#pragma once

namespace revforge {

/**
 * Le pneu en glissement longitudinal pur : la « formule magique » de Pacejka,
 *
 *     F = D · sin(C · atan(φ)),   φ = B·κ − E·(B·κ − atan(B·κ)),   D = μ · N,   B · C · D = K,
 *
 * avec κ = (ωr − v) / max(v, v_bas) le glissement de la norme SAE J670. La force monte avec le glissement jusqu'à un
 * sommet vers 11 %, puis retombe : 76 % du sommet roue bloquée ou patinant franchement (κ = 1). B, C et E sont tirés de trois
 * caractéristiques publiées, pas d'un essai de pneu : voir « Le pneu » dans docs/modele.md, et ce qui y est supposé.
 */

/** μ au sommet, sur route sèche : D / N. */
inline constexpr double kTyreGrip = 1.05;
/** K / N, la raideur de glissement rapportée à la charge : ≈ 21,5 pour un pneu de tourisme (Pacejka, pKx1 d'un 205/60R15). */
inline constexpr double kTyreSlipStiffness = 21.5;
/** C, la forme : elle fixe ce qui reste en glissement franc, 0,65/0,85 du sommet sur béton sec (Bosch). */
inline constexpr double kTyreShape = 1.5;
/** E, la courbure : elle place le sommet vers 11 % de glissement. */
inline constexpr double kTyreCurvature = -0.45;
/** Sous cette vitesse, le glissement se rapporte à elle : à l'arrêt, ωr − v n'a plus de vitesse à diviser (supposé). */
inline constexpr double kTyreLowSpeedMs = 1.0;

/** B, tiré des précédents : K / (C · μ). */
double tyreStiffnessFactor();
/** La part du sommet que donne ce glissement, de −1 à 1 (impaire). */
double tyreGripShare(double slipRatio);
/** Sa pente, d(part)/dκ : négative au-delà du sommet. */
double tyreGripSlope(double slipRatio);
/** Le glissement du sommet, κ*. */
double tyrePeakSlip();
/** Le glissement, sur la montée de la courbe, où le pneu donne cette part de son sommet (0 à 1). */
double tyreSlipForShare(double share);
/** Le glissement longitudinal : (roue − voiture) / max(voiture, `kTyreLowSpeedMs`), vitesses en m/s. */
double tyreSlipRatio(double wheelMs, double carMs);

/** Le contact des roues motrices à ce pas. */
struct TyreContact {
    double forceN = 0.0;      // la force au sol, positive quand la roue tire
    double perWheelMs = 0.0;  // dF/d(ωr) (N·s/m), jamais négative : de quoi intégrer la roue sans osciller
    double slipRatio = 0.0;   // κ
};

/** La force des roues motrices, dont `peakN` est le sommet (μ · N), à ces vitesses de roue et de voiture (m/s). */
TyreContact tyreContact(double peakN, double wheelMs, double carMs);

}  // namespace revforge
