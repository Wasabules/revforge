/**
 * Les soupapes d'un cylindre, déduites de ce qui tient dans l'alésage : recopie de `valveTrainOf`
 * (`core/src/valvetrain.cpp`), pour le dessin et le gaz à l'écran. Une erreur ici ne change pas le son, seulement ce qu'on voit.
 */
import type { Camshaft } from './config';

export const VALVE_EDGE_MM = 2.5;
export const VALVE_BRIDGE_TWO_MM = 5;
export const VALVE_BRIDGE_FOUR_MM = 4;
export const VALVE_EXHAUST_SHARE = 0.83;
export const VALVE_INTAKE_REACH = 0.6527036446661393;
export const VALVE_EXHAUST_REACH = 0.7778619134302062;
export const VALVE_LIFT_PER_BORE = 0.12;
export const VALVE_THROAT_PER_HEAD = 0.636;
export const VALVE_DISCHARGE = 0.7;

export interface ValveSizes {
  perCylinder: number;
  /** Diamètre de tête de chaque soupape (mm). */
  intakeMm: number;
  exhaustMm: number;
  /** Section efficace moyenne, toutes soupapes du cylindre (mm²). */
  intakeAreaMm2: number;
  exhaustAreaMm2: number;
}

/** Les orifices en série : la jupe de la levée, puis la gorge du siège. */
function valveArea(headMm: number, liftMm: number): number {
  const curtain = Math.PI * headMm * liftMm * (2 / Math.PI);
  const throat = VALVE_THROAT_PER_HEAD * headMm * headMm;
  return VALVE_DISCHARGE / Math.sqrt(1 / (curtain * curtain) + 1 / (throat * throat));
}

const quarterValve = (boreMm: number, reach: number) => (boreMm / 2 - VALVE_EDGE_MM - reach * VALVE_BRIDGE_FOUR_MM) / (reach + 0.5);

export function valveSizes(camshaft: Camshaft, boreMm: number): ValveSizes {
  const lift = VALVE_LIFT_PER_BORE * boreMm;
  // Socle : les culbuteurs (`ohv`) ont deux soupapes, comme le simple arbre.
  if (camshaft === 'sohc' || camshaft === 'ohv') {
    const intakeMm = (boreMm - 2 * VALVE_EDGE_MM - VALVE_BRIDGE_TWO_MM) / (1 + VALVE_EXHAUST_SHARE);
    const exhaustMm = VALVE_EXHAUST_SHARE * intakeMm;
    return { perCylinder: 2, intakeMm, exhaustMm, intakeAreaMm2: valveArea(intakeMm, lift), exhaustAreaMm2: valveArea(exhaustMm, lift) };
  }
  const intakeMm = quarterValve(boreMm, VALVE_INTAKE_REACH);
  const exhaustMm = quarterValve(boreMm, VALVE_EXHAUST_REACH);
  return { perCylinder: 4, intakeMm, exhaustMm, intakeAreaMm2: 2 * valveArea(intakeMm, lift), exhaustAreaMm2: 2 * valveArea(exhaustMm, lift) };
}

/** Ce que les soupapes d'un simple arbre laissent passer, face à celles d'un double arbre de même alésage. */
export function sohcSectionShare(boreMm: number): { intake: number; exhaust: number } {
  const two = valveSizes('sohc', boreMm);
  const four = valveSizes('dohc', boreMm);
  return { intake: two.intakeAreaMm2 / four.intakeAreaMm2, exhaust: two.exhaustAreaMm2 / four.exhaustAreaMm2 };
}
